#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

// No game dependencies: these are the actual server/client vote state machines.
namespace CutsceneVotes {
constexpr int MaxPeers = 8;
using Token = uint64_t;
using SceneName = std::array<char, 8>;
enum class Phase : uint8_t { Idle, Active, Committed };
inline SceneName Name(const char* text) {
    SceneName name{};
    for (size_t i = 0; i < name.size() && text[i]; ++i) name[i] = text[i];
    return name;
}
inline bool ValidName(const SceneName& name) {
    if (!name[0]) return false;
    for (char c : name) {
        if (!c) break;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) return false;
    }
    auto lower = name;
    for (char& c : lower) if (c >= 'A' && c <= 'Z') c = char(c + ('a' - 'A'));
    return lower != Name("finale");
}
struct Snapshot {
    Token generation = 0, serial = 0, identity = 0;
    int8_t host = -1;
    SceneName name{};
    uint8_t total = 0, votes = 0;
    Phase phase = Phase::Idle;
    bool eligible = false, voted = false;
    bool Valid() const {
        return int(phase) <= int(Phase::Committed) && total <= MaxPeers && votes <= total &&
            host >= -1 && host < MaxPeers &&
            (phase == Phase::Idle || (generation && serial && host >= 0 && ValidName(name) && total &&
                (phase != Phase::Committed || votes == total))) && (!voted || eligible);
    }
};
class Room {
    struct Peer { Token identity = 0, captured = 0, prepared = 0, started = 0; bool voted = false, ready = false; };
    std::array<Peer, MaxPeers> peers{};
    Snapshot scene{};
    Token nextIdentity = 0, nextGeneration = 0, lastSerial = 0;
    int host = -1;
    static bool ValidId(int id) { return id >= 0 && id < MaxPeers; }
    void Recount() {
        scene.total = scene.votes = 0;
        for (const auto& p : peers) if (p.captured && p.captured == p.identity) {
            ++scene.total; if (p.voted) ++scene.votes;
        }
        if (scene.phase == Phase::Active && scene.total && scene.votes == scene.total)
            scene.phase = Phase::Committed;
    }
public:
    const Snapshot& Current() const { return scene; }
    int Host() const { return host; }
    void Join(int id) {
        if (!ValidId(id) || peers[id].identity || nextIdentity == std::numeric_limits<Token>::max()) return;
        peers[id].identity = ++nextIdentity;
        // A join never inherits a captured slot or adds to the current threshold.
    }
    void GameplayReady(int id) { if (ValidId(id) && peers[id].identity) peers[id].ready = true; }
    bool Ready(int id) const { return ValidId(id) && peers[id].identity && peers[id].ready; }
    bool Prepared(int id) const { return Ready(id) && peers[id].prepared == peers[id].identity; }
    bool Started(int id) const { return Prepared(id) && peers[id].started == peers[id].identity; }
    void Prepare(int id) { if (Ready(id)) { peers[id].prepared = peers[id].identity; peers[id].started = 0; } }
    void Start(int id) { if (Prepared(id)) peers[id].started = peers[id].identity; }
    void ClearPreparation() { for (auto& p : peers) p.prepared = p.started = 0; }
    void Cancel() {
        scene.phase = Phase::Idle; scene.total = scene.votes = 0;
        for (auto& p : peers) { p.captured = 0; p.voted = false; }
    }
    void HostChanged(int id) {
        if (host == id) return;
        Cancel(); host = id; scene.host = int8_t(id); lastSerial = 0;
        for (auto& p : peers) p.started = 0; // Retain prepared recipients for native CLEAR, never voting.
    }
    void Leave(int id) {
        if (!ValidId(id)) return;
        peers[id] = {};
        if (host == id) { Cancel(); host = -1; lastSerial = 0; }
        else Recount();
    }
    bool Begin(int sender, Token serial, const SceneName& name) {
        if (!ValidId(sender) || sender != host || !Started(sender) || !serial ||
            serial <= lastSerial || !ValidName(name) || nextGeneration == std::numeric_limits<Token>::max()) return false;
        Cancel(); lastSerial = serial;
        scene.generation = ++nextGeneration; scene.serial = serial; scene.name = name; scene.phase = Phase::Active;
        scene.host = int8_t(host);
        for (auto& p : peers) { p.captured = p.started == p.identity && p.prepared == p.identity ? p.identity : 0; p.voted = false; }
        Recount(); return true;
    }
    bool Cancel(int sender, Token serial) {
        if (sender != host || scene.phase == Phase::Idle || serial != scene.serial) return false;
        Cancel(); return true;
    }
    bool Vote(int sender, Token generation, Token identity) {
        if (!ValidId(sender) || scene.phase != Phase::Active || scene.generation != generation) return false;
        auto& p = peers[sender];
        if (!identity || p.identity != identity || p.captured != identity || p.voted) return false;
        p.voted = true; Recount(); return true;
    }
    Snapshot For(int id) const {
        auto s = scene;
        if (ValidId(id)) {
            const auto& p = peers[id]; s.identity = p.identity;
            s.eligible = scene.phase != Phase::Idle && p.identity && p.identity == p.captured;
            s.voted = s.eligible && p.voted;
        }
        return s;
    }
};

class Client {
    SceneName name{};
    Token ticket = 0, boundTicket = 0, highestGeneration = 0;
    bool started = false, sent = false, released = false;
    Snapshot state{};
public:
    const Snapshot& State() const { return state; }
    bool Started() const { return started; }
    const SceneName& CurrentName() const { return name; }
    Token SceneTicket() const { return ticket; }
    void Unbind() { boundTicket = 0; state = {}; sent = released = false; }
    void Invalidate() { started = false; boundTicket = 0; state = {}; sent = false; released = false; }
    void Reset() { Invalidate(); highestGeneration = 0; ticket = 0; }
    void Start(const SceneName& sceneName) {
        Invalidate(); if (ticket == std::numeric_limits<Token>::max()) return;
        ++ticket; name = sceneName; started = ValidName(name);
    }
    bool Begin(const Snapshot& snapshot) {
        if (!snapshot.Valid() || snapshot.phase != Phase::Active || !snapshot.identity || !started ||
            snapshot.name != name || snapshot.generation <= highestGeneration) return false;
        highestGeneration = snapshot.generation; boundTicket = ticket; state = snapshot;
        sent = released = false; return true;
    }
    bool Matches(const Snapshot& snapshot) const {
        return started && boundTicket == ticket && snapshot.Valid() && snapshot.generation == state.generation &&
            snapshot.serial == state.serial && snapshot.host == state.host && snapshot.name == name && snapshot.identity == state.identity;
    }
    bool Update(const Snapshot& snapshot) {
        if (!Matches(snapshot)) return false;
        if (snapshot.phase == Phase::Idle) { Invalidate(); return true; }
        // COMMIT is the only packet that releases the native skip path.
        if (snapshot.phase != Phase::Active || state.phase != Phase::Active || snapshot.total > state.total ||
            int(snapshot.votes) + int(state.total - snapshot.total) < int(state.votes)) return false;
        state = snapshot; return true;
    }
    bool Commit(const Snapshot& snapshot) {
        if (!Matches(snapshot) || snapshot.phase != Phase::Committed) return false;
        state = snapshot; return true;
    }
    bool WantsVote(bool pressed, bool focused) {
        if (!focused) { released = false; return false; }
        if (focused && !pressed) released = true;
        return focused && pressed && released && state.phase == Phase::Active && state.eligible && !state.voted && !sent;
    }
    void MarkVoteSent() { sent = true; }
    bool Skip(bool playing, bool finished) {
        return started && boundTicket == ticket && state.phase == Phase::Committed && playing && !finished;
    }
};
}
