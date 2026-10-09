#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
struct RegistrationStub { template<class T> void RegisterPacket(T* p) { delete p; } };
static RegistrationStub& GetPacketFactory() { static RegistrationStub factory; return factory; }
#include "network/packets/vitals.h"

using namespace PlayerVitals;
using Vitals = Packets::Players::Vitals;
static unsigned checks = 0, failures = 0;
static void expect(bool value, const char* description)
{ ++checks; if (!value) { ++failures; std::cout << "FAIL: " << description << '\n'; } }
static State sample()
{ State s; s.maxHealth = 176; s.pedMaxHealth = 100.144f; s.airCapacity = 4000; s.breath = 3000; s.submerged = true; return s; }
struct Wire { std::array<uint32_t,128> words{}; int bytes = 0; uint8_t* data() { return reinterpret_cast<uint8_t*>(words.data()); } };
static Wire encode(Vitals p)
{
    Wire w; serialize::MeasureStream measure; Packet& packet = p;
    expect(packet.SerializeMeasure(measure), "Valid production packet measures.");
    serialize::WriteStream writer(w.data(),int(w.words.size()*sizeof(uint32_t)));
    expect(packet.SerializeWrite(writer), "Valid production packet writes."); writer.Flush(); w.bytes = writer.GetBytesProcessed();
    expect(w.bytes == measure.GetBytesProcessed(), "Measurement matches byte length without aligned raw-byte fields."); return w;
}
static bool decode(Wire& w, Vitals& p, int bytes = -1)
{ serialize::ReadStream reader(w.data(),bytes < 0 ? w.bytes : bytes); return static_cast<Packet&>(p).SerializeRead(reader); }
static void patch(Wire& wire, int offset, int bits, uint32_t raw)
{
    for (int i = 0; i < bits; ++i) {
        auto& byte = wire.data()[(offset+i)/8]; uint8_t mask = uint8_t(1u << ((offset+i)%8));
        byte = uint8_t((byte & uint8_t(~mask)) | ((raw & (1u << i)) ? mask : 0));
    }
}
static void patch_float(Wire& wire, int offset, float value)
{ uint32_t bits = 0; std::memcpy(&bits,&value,sizeof(bits)); patch(wire,offset,32,bits); }
static void state_tests()
{
    State s = sample(); expect(s.Valid(), "Independently different PlayerInfo and ped maxima are valid.");
    expect(HealthPercent(88,s) == 50, "Visible health fraction uses owner PlayerInfo 176, not ped maximum 100.144 or local 100.");
    expect(BreathPercent(s) == 75 && ShowBreath(s), "Air fraction uses owner's capacity, not global local lung stats.");
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity()}) {
        auto bad = s; bad.breath = invalid; expect(!bad.Valid() && !NormalizeNative(bad), "Nonfinite remaining breath rejected before normalization.");
        bad = s; bad.airCapacity = invalid; expect(!bad.Valid() && !NormalizeNative(bad), "Nonfinite ceiling rejected.");
        bad = s; bad.pedMaxHealth = invalid; expect(!bad.Valid() && !NormalizeNative(bad), "Nonfinite ped maximum rejected.");
    }
    for (float invalid : {0.0f,0.99f,255.01f}) { auto bad = s; bad.pedMaxHealth = invalid; expect(!bad.Valid(), "Ped maximum is bounded to supported health domain."); }
    for (float invalid : {999.0f,4001.01f}) { auto bad = s; bad.airCapacity = invalid; expect(!bad.Valid(), "Ceiling outside verified stock envelope is rejected."); }
    auto overshoot = s; overshoot.breath = 4002;
    expect(!overshoot.Valid() && NormalizeNative(overshoot) && overshoot.breath == 4000, "Legitimate recovery overshoot is normalized only on native capture.");
    overshoot = s; overshoot.breath = -1;
    expect(NormalizeNative(overshoot) && overshoot.breath == 0, "Finite negative native transient clamps to empty air.");
    auto empty = s; empty.breath = 0; expect(empty.Valid() && BreathPercent(empty) == 0, "Exhausted air is valid.");
    auto full = s; full.breath = full.airCapacity; full.submerged = false;
    expect(full.Valid() && !ShowBreath(full), "Full air outside water does not add an unnecessary bar.");
    full.submerged = true; expect(ShowBreath(full), "Submerged player may display full air.");
    auto bad = s; bad.maxHealth = 0; expect(!bad.Valid(), "Zero PlayerInfo max HP cannot divide visible health.");
    expect(HealthPercent(1000,s) == 100 && HealthPercent(-1,s) == 0, "Health bar values are bounded without rewriting actual health.");
}
static void authority_tests()
{
    GenerationCounter counter; uint32_t a = counter.Next(), b = counter.Next();
    expect(a != 0 && b > a, "Server generation allocation is monotonic.");
    Cache owner; expect(owner.Bind(3,a), "Authenticated owner binds its connection cache.");
    expect(!owner.AcceptOwner(2,3,0,1,sample()) && !owner.AcceptOwner(3,2,0,1,sample()), "Foreign authenticated sender and forged owner are rejected.");
    expect(!owner.AcceptOwner(3,3,a,1,sample()), "Client cannot stamp a server generation.");
    expect(owner.AcceptOwner(3,3,0,1,sample()), "Own finite sample accepted.");
    auto original = owner.state; auto invalid = sample(); invalid.breath = std::numeric_limits<float>::quiet_NaN();
    expect(!owner.AcceptOwner(3,3,0,2,invalid) && owner.sequence == 1 && owner.state == original, "Malformed update does not consume sequence or overwrite cache.");
    expect(!owner.AcceptOwner(3,3,0,1,sample()), "Duplicate and stale publication cannot overwrite latest.");
    expect(owner.AcceptOwner(3,3,0,MAX_COUNTER,sample()) && !owner.AcceptOwner(3,3,0,MAX_COUNTER+1,sample()), "Sequence counter does not wrap across the wire limit.");
    Cache unspawned; expect(unspawned.Bind(3,a) && unspawned.Accept(3,a,owner.sequence,owner.state)
        && unspawned.hasState, "Join replay stores a sample independently of any native ped spawn.");
    expect(!unspawned.Accept(3,b,1,sample()), "Different connection generation cannot target the existing player.");
    unspawned = {}; expect(!unspawned.Accept(3,a,2,sample()), "Disconnected cache rejects updates until new identity binding.");
    expect(unspawned.Bind(3,b) && !unspawned.Accept(3,a,MAX_COUNTER,sample())
        && unspawned.Accept(3,b,1,sample()), "Reused player slot accepts only the new generation and restarts sequence safely.");
    expect(!unspawned.Bind(3,a), "Delayed old identity cannot rebind an established connection.");
    Cache badOwner; expect(!badOwner.Bind(-1,a) && !badOwner.Bind(MAX_PLAYERS,a) && !badOwner.Bind(0,0), "Out-of-room owners and zero identities are rejected.");
}
static void packet_tests()
{
    Vitals p; p.playerid = 3; p.generation = 9; p.sequence = 1; p.hasState = true; p.state = sample();
    Packet& base = p;
    expect(base.GetType() == ePacketType::PLAYER_VITALS && base.GetChannel() == ePacketChannel::SYSTEM
        && GetChannelReliability(base.GetChannel()) == ePacketReliability::RELIABLE, "Actual packet uses reserved ID and reliable SYSTEM lifecycle ordering.");
    for (int owner = 0; owner < MAX_PLAYERS; ++owner) for (bool identity : {false,true}) {
        auto input = p; input.playerid = owner; input.hasState = !identity; input.sequence = identity ? 0 : MAX_COUNTER;
        input.generation = MAX_COUNTER;
        for (float air : {0.0f,4000.0f}) {
            input.state.breath = air;
            auto wire = encode(input); Vitals output;
            expect(decode(wire,output) && output.playerid == input.playerid && output.generation == input.generation
                && output.sequence == input.sequence && output.hasState == input.hasState
                && (identity || output.state == input.state), "All slots and identity/full replay packets round trip through real serializer.");
            for (int bytes = 0; bytes < wire.bytes; ++bytes) { Vitals truncated;
                expect(!decode(wire,truncated,bytes), "Every shortened payload is rejected with padded read storage."); }
        }
    }
    auto initial = encode(p);
    for (int offset : {74,106,138}) for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}) {
        auto wire = initial; patch_float(wire,offset,invalid); Vitals q;
        expect(!decode(wire,q), "Raw nonfinite field encoding is rejected by actual packet validation.");
    }
    const struct { int offset; float value; } bad[] = {{74,0},{74,256},{106,-1},{106,4001},{138,999},{138,4002}};
    for (auto field : bad) { auto wire = initial; patch_float(wire,field.offset,field.value); Vitals q;
        expect(!decode(wire,q), "Out-of-envelope raw float payload is rejected rather than clamped on receive."); }
    auto zero = initial; patch(zero,34,31,0); Vitals q;
    expect(!decode(zero,q), "Sample sequence zero is malformed.");
    zero = initial; patch(zero,66,8,255);
    expect(!decode(zero,q), "Unused byte max-health representation cannot alias a valid maximum.");
    auto client = p; client.generation = 0; auto wire = encode(client);
    expect(decode(wire,q) && q.generation == 0, "Client publication uses unstamped generation for server authority validation.");
    auto invalid = p; invalid.state.breath = std::numeric_limits<float>::quiet_NaN(); serialize::MeasureStream measure;
    expect(!static_cast<Packet&>(invalid).SerializeMeasure(measure), "Invalid outbound snapshot fails safely before field serialization.");
    auto identity = p; identity.hasState = false; identity.sequence = 0;
    auto idWire = encode(identity);
    std::cout << "WIRE: full state " << initial.bytes << " payload bytes; identity " << idWire.bytes << " payload bytes\n";
}
int main()
{
    state_tests(); authority_tests(); packet_tests();
    std::cout << checks << " assertions, " << failures << " failures\n";
    return failures ? 1 : 0;
}
