#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <type_traits>
struct RegistrationStub { template<class T> void RegisterPacket(T* p) { delete p; } };
static RegistrationStub& GetPacketFactory() { static RegistrationStub factory; return factory; }
#include "network/packets/cutscene.h"
namespace P = Packets::Cutscene;
using namespace CutsceneVotes;
static unsigned checks = 0, failures = 0;
static void expect(bool ok, const char* message) {
    ++checks; if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
template<class T> void roundtrip(T input) {
    alignas(4) std::array<uint8_t, 128> bytes{};
    serialize::MeasureStream measure;
    expect(static_cast<Packet&>(input).SerializeMeasure(measure), "Real packet measures");
    serialize::WriteStream writer(bytes.data(), int(bytes.size()));
    expect(static_cast<Packet&>(input).SerializeWrite(writer), "Real packet writes"); writer.Flush();
    expect(writer.GetBitsProcessed() == measure.GetBitsProcessed(), "Exact measured/serialized bit count");
    T result;
    serialize::ReadStream reader(bytes.data(), writer.GetBytesProcessed());
    expect(static_cast<Packet&>(result).SerializeRead(reader), "Real packet reads");
    expect(static_cast<Packet&>(result).GetChannel() == ePacketChannel::SCRIPT &&
        GetChannelReliability(static_cast<Packet&>(result).GetChannel()) == ePacketReliability::RELIABLE, "Reliable SCRIPT lane");
    if constexpr (std::is_same_v<T, P::Vote>) {
        expect(input.generation == result.generation && input.identity == result.identity, "Full 64-bit vote identity round trip");
    } else {
        expect(input.state.host == result.state.host && input.state.generation == result.state.generation && input.state.serial == result.state.serial &&
            input.state.identity == result.state.identity && input.state.name == result.state.name &&
            input.state.phase == result.state.phase && input.state.eligible == result.state.eligible &&
            input.state.voted == result.state.voted && input.state.votes == result.state.votes &&
            input.state.total == result.state.total, "Scene, identity, lifecycle and counts round trip");
    }
    for (int n = 0; n < writer.GetBytesProcessed(); ++n) {
        T truncated; serialize::ReadStream stream(bytes.data(), n);
        expect(!static_cast<Packet&>(truncated).SerializeRead(stream), "Every truncated payload rejected");
    }
}
int main() {
    for (uint8_t total = 1; total <= MaxPeers; ++total) for (uint8_t votes = 0; votes <= total; ++votes) {
        for (int flags = 0; flags < 3; ++flags) {
            Snapshot state;
            state.generation = UINT64_MAX; state.serial = UINT64_MAX - 1; state.identity = UINT64_MAX - 2;
            state.host = 0; state.name = Name("INTRO1A"); state.total = total; state.votes = votes;
            state.phase = Phase::Active; state.eligible = flags != 0; state.voted = flags == 2;
            P::Begin begin; begin.state = state; roundtrip(begin);
            P::Update update; update.state = state; roundtrip(update);
            if (votes == total) { P::Commit commit; commit.state = state; commit.state.phase = Phase::Committed; roundtrip(commit); }
        }
    }
    P::Begin request; request.state.serial = 1; request.state.host = 0; request.state.name = Name("INTRO1A"); roundtrip(request);
    request.active = false; request.state.name = {}; roundtrip(request);
    P::Vote vote; vote.generation = UINT64_MAX; vote.identity = UINT64_MAX - 3; roundtrip(vote);
    serialize::MeasureStream measure;
    P::Commit invalid; expect(!static_cast<Packet&>(invalid).SerializeMeasure(measure), "Inactive commit rejected");
    Snapshot bad; bad.total = MaxPeers + 1;
    expect(!bad.Valid(), "Out-of-range roster rejected");
    bad = {}; bad.phase = Phase(255); expect(!bad.Valid(), "Unknown lifecycle rejected");
    bad = {}; bad.voted = true; expect(!bad.Valid(), "Voted without eligibility rejected");
    std::cout << checks << " assertions, " << failures << " failures\n";
    return failures ? 1 : 0;
}
