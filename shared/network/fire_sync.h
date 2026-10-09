#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace FireSync {
constexpr uint32_t MaxCounter = 0x7fffffff;
constexpr int MaxFires = 60, MaxPlayers = 8, MaxEntities = 255;
enum class Kind : uint8_t { World, Player, Ped, Vehicle, Object };
enum class Intent : uint8_t { Ground, Attached, Water, Stop, Heat };
struct Vec {
    float x = 0, y = 0, z = 0;
    bool Valid() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
        std::abs(x) <= 20000 && std::abs(y) <= 20000 && z >= -5000 && z <= 10000; }
};
inline float Distance2(const Vec& a, const Vec& b) {
    return (a.x-b.x)*(a.x-b.x) + (a.y-b.y)*(a.y-b.y) + (a.z-b.z)*(a.z-b.z);
}
struct Entity {
    Kind kind = Kind::World;
    uint32_t id = 0, generation = 0, ownerEpoch = 0;
    int owner = -1, model = 0;
    bool Valid() const {
        if (kind == Kind::World) return !id && !generation && !ownerEpoch && owner == -1 && !model;
        if (!generation || generation > MaxCounter || !ownerEpoch || ownerEpoch > MaxCounter ||
            owner < 0 || owner >= MaxPlayers || model < 0 || model > 19999) return false;
        switch (kind) {
        case Kind::Player: return id < MaxPlayers;
        case Kind::Ped: case Kind::Vehicle: return id < MaxEntities;
        case Kind::Object: return id > 0 && id <= 0x07ffffff;
        default: return false;
        }
    }
};
inline bool Matches(const Entity& a, const Entity& b) {
    return a.Valid() && b.Valid() && a.kind == b.kind && a.id == b.id && a.generation == b.generation &&
        a.ownerEpoch == b.ownerEpoch && a.owner == b.owner && a.model == b.model;
}
struct Key {
    uint32_t epoch = 0, id = 0, generation = 0, sequence = 0;
    bool Valid() const { return epoch && epoch <= MaxCounter && id && id <= MaxFires && generation &&
        generation <= MaxCounter && sequence && sequence <= MaxCounter; }
};
inline bool SameFire(const Key& a, const Key& b) { return a.epoch == b.epoch && a.id == b.id && a.generation == b.generation; }
struct State {
    Key key{}; Vec position{}; Entity target{}, creator{};
    float strength = 1;
    uint32_t remaining = 0;
    uint8_t generations = 0;
    bool script = false, noise = true;
    bool Valid() const { return key.Valid() && position.Valid() && target.Valid() && creator.Valid() &&
        std::isfinite(strength) && strength >= 0 && strength <= 128 && remaining <= 120000; }
};
struct Slot { State state{}; bool live = false; };
class Cache {
public:
    uint32_t epoch = 0; int host = -1;
    std::array<Slot, MaxFires> slots{};
    bool Reset(uint32_t next, int owner) {
        if (!next || next > MaxCounter || next <= epoch || owner < -1 || owner >= MaxPlayers) return false;
        epoch = next; host = owner; slots = {}; return true;
    }
    bool Fresh(const Key& key) const {
        if (!key.Valid() || key.epoch != epoch) return false;
        const auto& old = slots[key.id-1].state.key;
        return key.generation > old.generation || (key.generation == old.generation && key.sequence > old.sequence);
    }
    bool Accept(const State& state) {
        if (!state.Valid() || !Fresh(state.key)) return false;
        const auto& old = slots[state.key.id-1];
        if (old.state.key.generation == state.key.generation && !old.live) return false;
        slots[state.key.id-1] = {state, true}; return true;
    }
    bool Remove(const Key& key) {
        if (!Fresh(key)) return false;
        auto& slot = slots[key.id-1]; slot.state.key = key; slot.live = false; return true;
    }
};
class Ledger {
public:
    Cache active{}, pending{};
    bool State(const FireSync::State& state) {
        if (!state.Valid() || state.key.epoch < active.epoch) return false;
        if (state.key.epoch == active.epoch) return active.Accept(state);
        if (state.key.epoch > pending.epoch) pending.Reset(state.key.epoch, -1);
        return pending.Accept(state);
    }
    bool Remove(const Key& key) {
        if (!key.Valid() || key.epoch < active.epoch) return false;
        if (key.epoch == active.epoch) return active.Remove(key);
        if (key.epoch > pending.epoch) pending.Reset(key.epoch, -1);
        return pending.Remove(key);
    }
    bool Reset(uint32_t epoch, int host) {
        if (!active.Reset(epoch,host)) return false;
        if (pending.epoch == epoch) { active.slots = pending.slots; pending = {}; }
        return true;
    }
};
inline bool AllowDamage(bool host, int localId, const Entity& target, bool locallyOwned,
    bool canonical, bool lifetimeValid) {
    if (!lifetimeValid) return false;
    if (target.kind == Kind::World) return host && locallyOwned;
    return canonical && target.Valid() && target.owner == localId && locallyOwned;
}
struct Request {
    uint32_t epoch = 0, connection = 0, sequence = 0;
    uint32_t gameGeneration = 0;
    Intent intent = Intent::Ground;
    Key fire{}; Vec position{}; Entity creator{}, target{}, issuer{};
    float radius = 0, water = 0;
    bool Valid() const {
        if (!epoch || epoch > MaxCounter || !connection || connection > MaxCounter || !sequence || sequence > MaxCounter ||
            !gameGeneration || gameGeneration > MaxCounter || !issuer.Valid() || issuer.kind != Kind::Player ||
            int(intent) > int(Intent::Heat) || !position.Valid() || !creator.Valid() || !target.Valid() ||
            !std::isfinite(radius) || radius < 0 || radius > 8 || !std::isfinite(water) || water < 0 || water > 2) return false;
        return intent == Intent::Ground || intent == Intent::Attached || fire.Valid();
    }
};
struct Peer {
    uint32_t connection = 0, sequence = 0, nativeReference = 0, lastRequest = 0, window = 0;
    uint32_t gameGeneration = 0;
    uint8_t requests = 0;
    bool ready = false, watching = false;
    Vec position{};
};
inline bool AcceptRequest(Peer& peer, const Request& request, uint32_t epoch, uint32_t now) {
    if (!peer.ready || !request.Valid() || request.epoch != epoch || request.connection != peer.connection ||
        request.gameGeneration != peer.gameGeneration || request.sequence <= peer.sequence || !peer.position.Valid()) return false;
    if (Distance2(peer.position, request.position) > 3600) return false;
    if (now - peer.window >= 1000) { peer.window = now; peer.requests = 0; }
    if (peer.requests >= 24) return false;
    ++peer.requests; peer.sequence = request.sequence; peer.lastRequest = now; return true;
}
}
