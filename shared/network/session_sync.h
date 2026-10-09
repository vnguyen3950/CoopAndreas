#pragma once
#include <algorithm>
#include <cstddef>
#include <array>
#include <cstdint>
#include <deque>
#include <limits>

// The room epoch survives host migration. Connection incarnations never reuse
// a former occupant's sequence/receipt namespace, even when its player slot does.
namespace SessionSync
{
constexpr int MAX_PEERS = 8, CHEATS = 92;
constexpr int32_t MONEY_LIMIT = 999999999, DELTA_LIMIT = 1999999998;
constexpr uint32_t MAX_COUNTER = 0x7fffffff;
constexpr size_t MAX_PENDING = 256;
enum class Kind : uint8_t { Money, WantedRaise, WantedLower, WantedRules, CheatAction, CheatToggle };
enum class Reason : uint8_t { Natural, HostScript, HostDecay, Bribe, Respray };
enum class CheatMode : uint8_t { Unsupported, Action, FlagToggle, FunctionToggle };
inline CheatMode Mode(int id)
{
    switch (id)
    {
    case 0: case 1: case 2: case 3: case 4: case 5: case 63: case 64: case 66: return CheatMode::Action;
    case 65: case 69: return CheatMode::FunctionToggle;
    // These are explicitly null-function flag toggles in the native 92-entry table.
    case 11: case 16: case 27: case 28: case 30: case 31: case 34: case 35:
    case 48: case 49: case 52: case 53: case 54: case 60: case 61: case 62:
    case 67: case 68: case 72: case 74: case 76: case 77: case 78: return CheatMode::FlagToggle;
    default: return CheatMode::Unsupported;
    }
}
struct Operation
{
    uint32_t epoch = 0, incarnation = 0, sequence = 0;
    Kind kind = Kind::Money;
    Reason reason = Reason::Natural;
    int32_t delta = 0;
    uint8_t level = 0, cheat = 0;
    bool active = false, policeIgnore = false, everyoneIgnore = false;
    bool Valid() const
    {
        if (!epoch || epoch > MAX_COUNTER || !incarnation || incarnation > MAX_COUNTER
            || !sequence || sequence > MAX_COUNTER || unsigned(kind) > unsigned(Kind::CheatToggle)
            || unsigned(reason) > unsigned(Reason::Respray)) return false;
        switch (kind)
        {
        case Kind::Money: return delta >= -DELTA_LIMIT && delta <= DELTA_LIMIT && reason == Reason::Natural;
        case Kind::WantedRaise: return level <= 6 && reason == Reason::Natural;
        case Kind::WantedLower: return level <= 6 && reason != Reason::Natural;
        case Kind::WantedRules: return level <= 6 && reason == Reason::HostScript;
        case Kind::CheatAction: return Mode(cheat) == CheatMode::Action;
        case Kind::CheatToggle: return Mode(cheat) == CheatMode::FlagToggle || Mode(cheat) == CheatMode::FunctionToggle;
        }
        return false;
    }
};
struct Snapshot
{
    uint32_t epoch = 0, revision = 0, incarnation = 0, acknowledged = 0;
    int recipient = -1, host = -1;
    bool ready = false;
    int32_t money = 0;
    uint8_t wanted = 0, maximumWanted = 6;
    bool policeIgnore = false, everyoneIgnore = false;
    std::array<bool, CHEATS> toggles{};
    bool Valid() const
    {
        if (!epoch || epoch > MAX_COUNTER || !revision || revision > MAX_COUNTER
            || !incarnation || incarnation > MAX_COUNTER || acknowledged > MAX_COUNTER
            || recipient < 0 || recipient >= MAX_PEERS || host < -1 || host >= MAX_PEERS
            || money < -MONEY_LIMIT || money > MONEY_LIMIT || wanted > maximumWanted || maximumWanted > 6) return false;
        for (int i = 0; i < CHEATS; ++i)
            if (toggles[i] && Mode(i) != CheatMode::FlagToggle && Mode(i) != CheatMode::FunctionToggle) return false;
        return !toggles[65] || wanted == 0;
    }
};
enum class Status { Accepted, Duplicate, Gap, Invalid, Rejected, NotReady };
struct Receipt { Status status = Status::Invalid; bool action = false; uint8_t cheat = 0; };
class Server
{
public:
    struct Peer { uint32_t incarnation = 0, acknowledged = 0; };
    std::array<Peer, MAX_PEERS> peers{};
    Snapshot state;
    uint32_t Join(int id, int host)
    {
        if (id < 0 || id >= MAX_PEERS || peers[id].incarnation || m_nextIncarnation > MAX_COUNTER
            || host < 0 || host >= MAX_PEERS || (host != id && !peers[host].incarnation)
            || (!Empty() && (host != state.host || state.revision == MAX_COUNTER))) return 0;
        if (Empty())
        {
            if (m_epoch == MAX_COUNTER) return 0;
            state = {}; state.epoch = ++m_epoch; state.revision = 1; state.host = host;
        }
        peers[id] = {m_nextIncarnation++, 0};
        Bump(); return peers[id].incarnation;
    }
    bool Empty() const { for (const auto& p : peers) if (p.incarnation) return false; return true; }
    void Leave(int id)
    {
        if (id < 0 || id >= MAX_PEERS || !peers[id].incarnation || state.revision == MAX_COUNTER) return;
        peers[id] = {};
        if (Empty()) { state = {}; return; }
        if (state.host == id) state.host = -1;
        Bump();
    }
    void HostChanged(int id)
    {
        if (!Empty() && id >= 0 && id < MAX_PEERS && peers[id].incarnation
            && id != state.host && state.revision != MAX_COUNTER) { state.host = id; Bump(); }
    }
    Snapshot For(int id) const
    {
        auto s = state;
        if (id >= 0 && id < MAX_PEERS)
        { s.recipient = id; s.incarnation = peers[id].incarnation; s.acknowledged = peers[id].acknowledged; }
        return s;
    }
    bool Seed(int sender, const Snapshot& seed)
    {
        if (sender != state.host || state.ready || state.revision == MAX_COUNTER || sender < 0 || sender >= MAX_PEERS
            || seed.epoch != state.epoch || seed.incarnation != peers[sender].incarnation
            || seed.recipient != sender || seed.host != state.host || seed.acknowledged != 0 || !seed.Valid()) return false;
        state.money = seed.money; state.maximumWanted = seed.maximumWanted; state.wanted = seed.wanted;
        state.policeIgnore = seed.policeIgnore; state.everyoneIgnore = seed.everyoneIgnore;
        state.toggles = seed.toggles; state.ready = true; return Bump();
    }
    Receipt Apply(int sender, const Operation& op)
    {
        if (sender < 0 || sender >= MAX_PEERS || !op.Valid() || op.epoch != state.epoch
            || op.incarnation != peers[sender].incarnation || !peers[sender].incarnation) return {};
        if (!state.ready) return {Status::NotReady};
        auto& peer = peers[sender];
        if (op.sequence <= peer.acknowledged) return {Status::Duplicate};
        if (op.sequence != peer.acknowledged + 1) return {Status::Gap};
        if (state.revision == MAX_COUNTER) return {Status::Invalid};
        Receipt receipt{Status::Accepted};
        auto money = [this](int64_t delta)
        {
            int64_t next = int64_t(state.money) + delta;
            if (next < -MONEY_LIMIT || next > MONEY_LIMIT) return false;
            state.money = int32_t(next); return true;
        };
        switch (op.kind)
        {
        case Kind::Money: if (!money(op.delta)) receipt.status = Status::Rejected; break;
        case Kind::WantedRaise:
            if (!state.toggles[65] && op.level > state.wanted)
                state.wanted = op.level < state.maximumWanted ? op.level : state.maximumWanted;
            break;
        case Kind::WantedLower:
            if (op.reason == Reason::Bribe) { if (state.wanted) --state.wanted; }
            else if (op.reason == Reason::Respray) state.wanted = 0;
            else if (sender != state.host) receipt.status = Status::Rejected;
            else if (op.reason == Reason::HostScript) state.wanted = state.toggles[65] ? 0 : std::min(op.level, state.maximumWanted);
            else if (op.level < state.wanted) state.wanted = op.level;
            break;
        case Kind::WantedRules:
            if (sender != state.host) receipt.status = Status::Rejected;
            else
            {
                state.maximumWanted = op.level;
                if (state.wanted > op.level) state.wanted = op.level;
                state.policeIgnore = op.policeIgnore; state.everyoneIgnore = op.everyoneIgnore;
            }
            break;
        case Kind::CheatAction:
            if (op.cheat == 3 && !money(250000)) { receipt.status = Status::Rejected; break; }
            if (op.cheat == 4 && !state.toggles[65])
                state.wanted = uint8_t(std::min(int(state.maximumWanted), int(state.wanted) + 2));
            if (op.cheat == 5) state.wanted = 0;
            if (op.cheat == 66 && !state.toggles[65]) state.wanted = state.maximumWanted;
            receipt.action = true; receipt.cheat = op.cheat; break;
        case Kind::CheatToggle:
            state.toggles[op.cheat] = op.active;
            if (op.cheat == 65 && op.active) state.wanted = 0;
            break;
        }
        peer.acknowledged = op.sequence;
        Bump(); return receipt;
    }
private:
    bool Bump() { if (state.revision == MAX_COUNTER) return false; ++state.revision; return true; }
    uint32_t m_epoch = 0, m_nextIncarnation = 1;
};

// Pending deltas belong to the signed ledger. The game receives a nonnegative
// spendable projection so native death/arrest clamps cannot forgive room debt.
// Snapshot receipts discard only this incarnation's acknowledged operations.
class Client
{
public:
    Snapshot state;
    std::deque<Operation> pending;
    uint32_t nextSequence = 0;
    bool Accept(const Snapshot& s, int localId)
    {
        if (!s.Valid() || s.recipient != localId) return false;
        if (state.epoch && s.epoch < state.epoch) return false;
        if (s.epoch == state.epoch && s.incarnation < state.incarnation) return false;
        bool identityChanged = s.epoch != state.epoch || s.incarnation != state.incarnation;
        if (!identityChanged && (s.revision < state.revision || s.acknowledged < state.acknowledged)) return false;
        if (!identityChanged && s.acknowledged > nextSequence) return false;
        if (!identityChanged && s.revision == state.revision) return false;
        if (identityChanged) { pending.clear(); nextSequence = s.acknowledged; }
        while (!pending.empty() && pending.front().sequence <= s.acknowledged) pending.pop_front();
        state = s; return true;
    }
    bool Queue(Operation& op)
    {
        if (!state.ready || nextSequence == MAX_COUNTER || pending.size() >= MAX_PENDING) return false;
        op.epoch = state.epoch; op.incarnation = state.incarnation; op.sequence = nextSequence + 1;
        if (!op.Valid()) return false;
        ++nextSequence; pending.push_back(op); return true;
    }
    int64_t ProjectedMoney() const
    {
        int64_t money = state.money;
        for (const auto& op : pending) if (op.kind == Kind::Money) money += op.delta;
        return money;
    }
    Snapshot Predicted() const
    {
        Snapshot s = state;
        for (const auto& op : pending)
        {
            if (op.kind == Kind::WantedRules && s.recipient == s.host)
            { s.maximumWanted = op.level; s.wanted = std::min(s.wanted, op.level); s.policeIgnore = op.policeIgnore; s.everyoneIgnore = op.everyoneIgnore; }
            if (op.kind == Kind::WantedRaise && !s.toggles[65]) s.wanted = std::max(s.wanted, std::min(op.level, s.maximumWanted));
            if (op.kind == Kind::WantedLower)
            {
                if (op.reason == Reason::Bribe && s.wanted) --s.wanted;
                else if (op.reason == Reason::Respray) s.wanted = 0;
                else if (s.recipient == s.host)
                {
                    if (op.reason == Reason::HostScript) s.wanted = s.toggles[65] ? 0 : std::min(op.level, s.maximumWanted);
                    else s.wanted = std::min(s.wanted, op.level);
                }
            }
            if (op.kind == Kind::CheatToggle)
            { s.toggles[op.cheat] = op.active; if (op.cheat == 65 && op.active) s.wanted = 0; }
            if (op.kind == Kind::CheatAction)
            {
                if (op.cheat == 5) s.wanted = 0;
                if (op.cheat == 4 && !s.toggles[65]) s.wanted = uint8_t(std::min(int(s.maximumWanted), int(s.wanted) + 2));
                if (op.cheat == 66 && !s.toggles[65]) s.wanted = s.maximumWanted;
            }
        }
        return s;
    }
};
inline int32_t NativeBalance(int64_t money)
{ return int32_t(std::max<int64_t>(0, std::min<int64_t>(money, MONEY_LIMIT))); }
inline int32_t DisplayBalance(int64_t money)
{ return int32_t(std::max<int64_t>(0, std::min<int64_t>(money, MONEY_LIMIT))); }
// Native-only side effects are executed inside this production seam. Recorded
// test doubles can prove cash/wanted feedback is restored while health remains.
template<class Money, class Wanted, class Native>
void ApplyWithoutFeedback(Money& money, Wanted& wanted, Native native)
{
    struct Restore
    {
        Money& money; Wanted& wanted; Money beforeMoney; Wanted beforeWanted;
        ~Restore() { money = beforeMoney; wanted = beforeWanted; }
    } restore{money, wanted, money, wanted};
    native();
}
inline bool SkipRewardReplay(uint16_t opcode, bool authenticated)
{ return authenticated && opcode == 0x0109; }

// Fed by actual game/script/player readiness in the client. A new lifecycle
// rebases cash; its default balance is never interpreted as an earn/spend delta.
class MoneyObservation
{
public:
    bool armed = false;
    uint64_t lifecycle = 0;
    int32_t lastWritten = 0;
    struct Change { bool rebase = false; int64_t delta = 0; };
    Change Observe(bool ready, uint64_t identity, int32_t nativeMoney)
    {
        if (!ready) { armed = false; return {}; }
        if (!armed || identity != lifecycle)
        { armed = true; lifecycle = identity; lastWritten = nativeMoney; return {true, 0}; }
        int64_t delta = int64_t(nativeMoney) - lastWritten;
        lastWritten = nativeMoney; return {false, delta};
    }
    void Written(int32_t value) { lastWritten = value; }
};
// SetWantedLevel can clamp/early-return. Cache only what the actual setter left,
// and avoid re-setting an already-correct level (which clears queued crimes).
template<class Read, class Set> int ApplyWantedLevel(int desired, Read read, Set set)
{
    if (read() != desired) set(desired);
    return read();
}
}
