#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

// Only static registration is stubbed. Production Packet wrappers, packet
// classes, field serialization, validation and streams are included unchanged.
struct RegistrationStub
{
    template<class T> void RegisterPacket(T* p) { delete p; }
};
static RegistrationStub& GetPacketFactory() { static RegistrationStub factory; return factory; }
#include "network/packet.h"
#include "network/packets/session.h"

using namespace SessionSync;
namespace P = Packets::Session;
static unsigned checks = 0, failures = 0;
static void expect(bool condition, const char* message)
{
    ++checks;
    if (!condition) { ++failures; std::cout << "FAIL: " << message << '\n'; }
}
struct Wire
{
    std::array<uint32_t, 128> words{};
    int bytes = 0, bits = 0;
    uint8_t* data() { return reinterpret_cast<uint8_t*>(words.data()); }
};
template<class T> static Wire encode(T p)
{
    Wire wire;
    serialize::MeasureStream measure;
    Packet& packet = p;
    expect(packet.SerializeMeasure(measure), "Production packet measures valid fields.");
    serialize::WriteStream stream(wire.data(), int(wire.words.size() * sizeof(uint32_t)));
    expect(packet.SerializeWrite(stream), "Production packet writes valid fields.");
    stream.Flush(); wire.bytes = stream.GetBytesProcessed(); wire.bits = stream.GetBitsProcessed();
    // Production MeasureStream deliberately budgets seven alignment bits for
    // SerializeBytes regardless of its final location. It is an upper bound.
    expect(wire.bits <= measure.GetBitsProcessed() && wire.bytes <= measure.GetBytesProcessed(),
        "Production conservative measure covers actual complete payload length.");
    return wire;
}
template<class T> static bool decode(Wire& wire, T& p, int bytes = -1)
{
    serialize::ReadStream stream(wire.data(), bytes < 0 ? wire.bytes : bytes);
    Packet& packet = p;
    return packet.SerializeRead(stream);
}
template<class T> static T roundtrip(T input)
{
    auto wire = encode(input); T output;
    expect(decode(wire, output), "Production packet reads complete payload.");
    Packet& p = output;
    expect(p.GetBytesRead() == size_t(wire.bytes), "Packet wrapper records exact payload bytes consumed.");
    for (int bytes = 0; bytes < wire.bytes; ++bytes) {
        T truncated;
        expect(!decode(wire, truncated, bytes), "Every shorter byte payload is rejected by actual read wrapper.");
    }
    return output;
}
static void patch(Wire& wire, int offset, int bits, uint32_t value)
{
    // Mutate the real encoded payload to malformed field bit patterns. This is
    // not a second serializer or an implementation of the production protocol.
    for (int bit = 0; bit < bits; ++bit) {
        const int index = offset + bit;
        auto& byte = wire.data()[index / 8];
        const uint8_t mask = uint8_t(1u << (index % 8));
        byte = uint8_t((byte & uint8_t(~mask)) | ((value & (1u << bit)) ? mask : 0));
    }
}
static bool equal(const Snapshot& a, const Snapshot& b)
{
    return a.epoch == b.epoch && a.revision == b.revision && a.incarnation == b.incarnation
        && a.acknowledged == b.acknowledged && a.recipient == b.recipient && a.host == b.host
        && a.ready == b.ready && a.money == b.money && a.wanted == b.wanted
        && a.maximumWanted == b.maximumWanted && a.policeIgnore == b.policeIgnore
        && a.everyoneIgnore == b.everyoneIgnore && a.toggles == b.toggles;
}
static bool equal(const Operation& a, const Operation& b)
{
    return a.epoch == b.epoch && a.incarnation == b.incarnation && a.sequence == b.sequence
        && a.kind == b.kind && a.reason == b.reason && a.delta == b.delta && a.level == b.level
        && a.cheat == b.cheat && a.active == b.active && a.policeIgnore == b.policeIgnore
        && a.everyoneIgnore == b.everyoneIgnore;
}
static Snapshot normal()
{
    Snapshot s; s.epoch = 1; s.revision = 2; s.incarnation = 3; s.recipient = 0; s.host = 0;
    s.ready = true; s.money = 100; s.wanted = 3; return s;
}
template<class T> static void assignment(ePacketType type)
{
    T value; Packet& packet = value;
    expect(packet.GetType() == type, "Packet class has its production packet ID.");
    expect(packet.GetChannel() == ePacketChannel::SCRIPT, "Session packet uses SCRIPT channel.");
    expect(GetChannelReliability(packet.GetChannel()) == ePacketReliability::RELIABLE,
        "Session SCRIPT channel uses production reliable assignment.");
    std::unique_ptr<Packet> cloned(packet.Clone());
    expect(cloned->GetType() == type && cloned->GetChannel() == ePacketChannel::SCRIPT,
        "Actual packet Clone retains type and channel.");
}
static void state_tests()
{
    const int32_t money[] = {-MONEY_LIMIT, -50, -1, 0, 1, MONEY_LIMIT};
    for (int32_t cash : money) for (bool maximum : {false, true}) {
        P::Seed seed; seed.state = normal(); seed.state.money = cash;
        if (maximum) {
            seed.state.epoch = seed.state.revision = seed.state.incarnation = MAX_COUNTER;
            seed.state.acknowledged = MAX_COUNTER; seed.state.recipient = MAX_PEERS - 1;
            seed.state.host = -1; seed.state.ready = false; seed.state.policeIgnore = true;
            seed.state.everyoneIgnore = true; seed.state.wanted = seed.state.maximumWanted = 6;
        }
        auto result = roundtrip(seed); expect(equal(seed.state, result.state), "Seed snapshot preserves every field and signed money.");
        P::Update update; update.state = seed.state;
        auto decoded = roundtrip(update); expect(equal(update.state, decoded.state), "Update snapshot preserves every field and signed money.");
    }
    for (int recipient = 0; recipient < MAX_PEERS; ++recipient) for (int host = -1; host < MAX_PEERS; ++host) {
        P::Update p; p.state = normal(); p.state.recipient = recipient; p.state.host = host;
        auto q = roundtrip(p);
        expect(q.state.recipient == recipient && q.state.host == host, "All representable recipient and host positions survive.");
    }
    P::Update all; all.state = normal(); all.state.wanted = 0;
    for (int i = 0; i < CHEATS; ++i) {
        auto mode = Mode(i);
        all.state.toggles[i] = mode == CheatMode::FlagToggle || mode == CheatMode::FunctionToggle;
    }
    expect(equal(all.state, roundtrip(all).state), "All supported toggles true together survive without reordering.");
    P::Update baseline; baseline.state = normal(); baseline.state.wanted = 0;
    auto initial = encode(baseline);
    for (int id = 0; id < CHEATS; ++id) {
        P::Update p = baseline; p.state.toggles[id] = true;
        auto mode = Mode(id); const bool supported = mode == CheatMode::FlagToggle || mode == CheatMode::FunctionToggle;
        if (supported) expect(equal(p.state, roundtrip(p).state), "Individual supported toggle preserves its exact position.");
        else {
            serialize::MeasureStream measure;
            expect(!static_cast<Packet&>(p).SerializeMeasure(measure), "Unsupported or action toggle fails production state validation.");
        }
        auto malformed = initial; patch(malformed, 171 + id, 1, 1); P::Update decoded;
        expect(decode(malformed, decoded) == supported, "Every toggle bit position enforces its actual supported classification.");
        if (supported) expect(decoded.state.toggles[id], "Patched supported toggle maps to the intended slot.");
    }
    // State field offsets reflect the frozen public wire declaration, and the
    // round-trip tests above exercise that declaration through real streams.
    const struct { int offset, bits; uint32_t raw; } malformed[] = {
        {0,31,MAX_COUNTER}, {31,31,MAX_COUNTER}, {62,31,MAX_COUNTER},
        {127,4,15}, {132,31,MAX_COUNTER}, {163,3,7}, {166,3,7}
    };
    for (auto bad : malformed) {
        auto wire = initial; patch(wire, bad.offset, bad.bits, bad.raw); P::Update update; P::Seed seed;
        expect(!decode(wire, update) && !decode(wire, seed), "Both state packet classes reject unused integer range encodings.");
    }
    auto impossible = initial; patch(impossible,166,3,0); patch(impossible,163,3,1); P::Update lower;
    expect(!decode(impossible, lower), "Wanted greater than maximum is rejected after field decoding.");
    auto never = initial; patch(never,171+65,1,1); patch(never,163,3,1); P::Update invalid;
    expect(!decode(never, invalid), "Never-wanted with nonzero stars is rejected on wire.");
    auto wire = encode(baseline);
    std::cout << "WIRE: Seed/Update " << wire.bits << " bits, " << wire.bytes << " payload bytes\n";
}
static Operation normal_op()
{
    Operation op; op.epoch = 1; op.incarnation = 2; op.sequence = 3;
    return op;
}
static void operation_tests()
{
    const int32_t deltas[] = {-DELTA_LIMIT, -MONEY_LIMIT-1, -MONEY_LIMIT, -1, 0, 1, MONEY_LIMIT, MONEY_LIMIT+1, DELTA_LIMIT};
    for (auto delta : deltas) for (bool maximum : {false,true}) {
        P::Transaction p; p.op = normal_op(); p.op.delta = delta;
        if (maximum) { p.op.epoch = p.op.incarnation = p.op.sequence = MAX_COUNTER;
            p.op.level = 6; p.op.cheat = CHEATS-1; p.op.active = p.op.policeIgnore = p.op.everyoneIgnore = true; }
        expect(equal(p.op, roundtrip(p).op), "Transaction preserves full signed delta and every field without compression loss.");
    }
    for (int kind = 0; kind <= int(Kind::CheatToggle); ++kind) for (int reason = 0; reason <= int(Reason::Respray); ++reason) {
        P::Transaction p; p.op = normal_op(); p.op.kind = Kind(kind); p.op.reason = Reason(reason);
        p.op.level = 6; p.op.cheat = p.op.kind == Kind::CheatToggle ? 65 : 3;
        p.op.active = p.op.policeIgnore = p.op.everyoneIgnore = true;
        if (p.op.Valid()) expect(equal(p.op, roundtrip(p).op), "Every valid kind/reason pair uses actual packet serialization.");
        else {
            serialize::MeasureStream measure;
            expect(!static_cast<Packet&>(p).SerializeMeasure(measure), "Invalid kind/reason pair fails actual packet validation.");
        }
    }
    for (int id = 0; id < CHEATS; ++id) {
        auto mode = Mode(id);
        if (mode == CheatMode::Unsupported) continue;
        P::Transaction p; p.op = normal_op(); p.op.cheat = uint8_t(id);
        p.op.kind = mode == CheatMode::Action ? Kind::CheatAction : Kind::CheatToggle;
        for (bool active : {false,true}) { p.op.active = active;
            expect(equal(p.op, roundtrip(p).op), "Supported cheat operation and both boolean states survive exactly."); }
    }
    P::Transaction p; p.op = normal_op(); auto initial = encode(p);
    const struct { int offset, bits; uint32_t raw; } bad[] = {
        {0,31,MAX_COUNTER}, {31,31,MAX_COUNTER}, {62,31,MAX_COUNTER},
        {93,3,7}, {96,3,7}, {136,3,7}, {139,7,127}
    };
    for (auto field : bad) {
        auto wire = initial; patch(wire,field.offset,field.bits,field.raw); P::Transaction q;
        expect(!decode(wire,q), "Transaction rejects malformed identity, kind, reason, level and cheat ranges.");
    }
    auto misaligned = initial; patch(misaligned,99,5,31); P::Transaction alignment;
    expect(!decode(misaligned,alignment), "Nonzero byte-alignment padding before signed delta is rejected.");
    for (int32_t delta : {std::numeric_limits<int32_t>::min(), -DELTA_LIMIT-1,
                         DELTA_LIMIT+1, std::numeric_limits<int32_t>::max()}) {
        auto wire = initial; uint32_t raw = 0; std::memcpy(&raw,&delta,sizeof(raw)); patch(wire,104,32,raw);
        P::Transaction q; expect(!decode(wire,q), "Raw byte delta must also pass signed semantic bounds.");
    }
    std::cout << "WIRE: Transaction " << initial.bits << " bits, " << initial.bytes << " payload bytes\n";
}
static void action_tests()
{
    P::CheatAction baseline; baseline.epoch = 1; baseline.revision = 2;
    baseline.incarnation = 3; baseline.sequence = 4; baseline.cheat = 3;
    for (int id = 0; id < CHEATS; ++id) {
        if (Mode(id) == CheatMode::Action) for (int sender = 0; sender < MAX_PEERS; ++sender) {
            auto p = baseline; p.cheat = uint8_t(id); p.sender = sender;
            if (sender == MAX_PEERS-1) p.epoch = p.revision = p.incarnation = p.sequence = MAX_COUNTER;
            auto q = roundtrip(p);
            expect(p.epoch == q.epoch && p.revision == q.revision && p.incarnation == q.incarnation
                && p.sequence == q.sequence && p.sender == q.sender && p.cheat == q.cheat,
                "Every supported action and sender preserves complete receipt identity.");
        } else {
            auto wire = encode(baseline); patch(wire,127,7,uint32_t(id)); P::CheatAction q;
            expect(!decode(wire,q), "Action packet rejects unsupported and toggle-only cheat IDs.");
        }
    }
    auto initial = encode(baseline);
    for (int offset : {0,31,62,93}) {
        auto wire = initial; patch(wire,offset,31,MAX_COUNTER); P::CheatAction q;
        expect(!decode(wire,q), "Action packet rejects out-of-range receipt counters.");
    }
    auto bad = initial; patch(bad,127,7,127); P::CheatAction q;
    expect(!decode(bad,q), "Action packet rejects unused cheat range encoding.");
    std::cout << "WIRE: CheatAction " << initial.bits << " bits, " << initial.bytes << " payload bytes\n";
}
int main()
{
    assignment<P::Hello>(ePacketType::SESSION_HELLO);
    assignment<P::Seed>(ePacketType::SESSION_SEED);
    assignment<P::Update>(ePacketType::SESSION_STATE);
    assignment<P::Transaction>(ePacketType::SESSION_OPERATION);
    assignment<P::CheatAction>(ePacketType::SESSION_CHEAT_ACTION);
    P::Hello hello; auto wire = encode(hello); P::Hello decoded;
    expect(wire.bits == 0 && wire.bytes == 0 && decode(wire,decoded,0), "Hello legitimately has an empty payload.");
    state_tests(); operation_tests(); action_tests();
    std::cout << checks << " assertions, " << failures << " failures\n";
    std::cout << "SCOPE: Actual packet classes, Packet wrappers and serialize.h. Registration only is stubbed.\n";
    return failures ? 1 : 0;
}
