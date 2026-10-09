#include "native_doubles.h"
#include "server/src/CCutsceneVotes.h"
#include "extracted_server.inc"
using CutsceneVotes::Phase;
using CutsceneVotes::Name;
int main() {
    ENetPeer hp, gp, outsiderPeer;
    CNetworkPlayer host{&hp, 0, true}, guest{&gp, 1, false}, outsider{&outsiderPeer, 2, true};
    CNetworkPlayerManager::m_pPlayers = {&host, &guest};
    CCutsceneVotes::Join(&host); CCutsceneVotes::Join(&guest); CCutsceneVotes::HostChanged(&host);
    uint32_t relayTime = 50000;
    expect(!CCutsceneVotes::ObserveOpcode(0x02E7, &outsider, relayTime), "Unregistered peer cannot announce START");
    expect(!CCutsceneVotes::ObserveOpcode(0x02E7, &guest, relayTime), "Registered guest cannot replace host scene");
    expect(!CCutsceneVotes::ObserveOpcode(0x0701, &guest, relayTime), "Guest cannot cancel through legacy END_SCENE_SKIP replay");
    expect(relayTime == 50000, "Rejected lifecycle does not mutate timestamp");
    CCutsceneVotes::ObserveOpcode(0x02E4, &host, relayTime);
    expect(CCutsceneVotes::ObserveOpcode(0x02E7, &host, relayTime), "Authenticated host START accepted");
    expect(relayTime == g_serverTime, "Ahead host timestamp normalized to announcement clock");
    Packets::Cutscene::Begin begin; begin.state.serial = 1; begin.state.name = Name("INTRO1A");
    CCutsceneVotes::Begin(begin, &guest); expect(::Room().Current().phase == Phase::Idle, "Guest BEGIN rejected");
    CCutsceneVotes::Begin(begin, &host); expect(::Room().Current().phase == Phase::Active, "Host BEGIN binds accepted START");
    expect(::Room().Current().total == 1 && !::Room().For(1).eligible, "Native START proves host readiness, menu-only guest excluded");
    CCutsceneVotes::GameplayReady(&guest);
    expect(::Room().Current().total == 1 && !::Room().For(1).eligible, "Late validated gameplay snapshot does not enlarge quorum");
    CCutsceneVotes::Leave(&guest); CCutsceneVotes::Join(&guest); CCutsceneVotes::GameplayReady(&guest);
    expect(::Room().Current().total == 1, "Departure/rejoin does not inherit captured eligibility");
    CCutsceneVotes::ObserveOpcode(0x02E4, &host, relayTime);
    Packets::Scripts::OpCodeSync load; uint16_t loadCommand = 0x02E4; std::memcpy(load.buffer, &loadCommand, 2);
    CCutsceneVotes::RelayOpcode(load, &host);
    CCutsceneVotes::ObserveOpcode(0x02E7, &host, relayTime);
    Packets::Scripts::OpCodeSync start; uint16_t startCommand = 0x02E7; std::memcpy(start.buffer, &startCommand, 2);
    CCutsceneVotes::RelayOpcode(start, &host); begin.state.serial = 2; CCutsceneVotes::Begin(begin, &host);
    expect(::Room().Current().total == 2, "Next scene captures both gameplay-ready identities");
    auto generation = ::Room().Current().generation;
    CCutsceneVotes::Begin(begin, &host);
    expect(::Room().Current().generation == generation, "Duplicate BEGIN cannot reset roster");
    Packets::Cutscene::Vote vote; vote.generation = generation; vote.identity = ::Room().For(1).identity;
    CCutsceneVotes::Vote(vote, &outsider); expect(::Room().Current().votes == 0, "Unregistered voter rejected");
    gp.state = 0; CCutsceneVotes::Vote(vote, &guest); expect(::Room().Current().votes == 0, "Disconnected ENet peer rejected");
    gp.state = ENET_PEER_STATE_CONNECTED;
    CCutsceneVotes::Vote(vote, &host); expect(::Room().Current().votes == 0, "Sender cannot claim another connection identity");
    CCutsceneVotes::Vote(vote, &guest); CCutsceneVotes::Vote(vote, &guest);
    expect(::Room().Current().votes == 1, "Authenticated unique guest vote accepted once");
    GetPacketFactory().sent.clear();
    vote.identity = ::Room().For(0).identity; CCutsceneVotes::Vote(vote, &host);
    expect(::Room().Current().phase == Phase::Committed, "Host explicitly votes to reach unanimity");
    unsigned commits = 0;
    for (const auto& record : GetPacketFactory().sent) {
        if (record.second->GetType() == ePacketType::CUTSCENE_VOTE_COMMIT) ++commits;
        expect(record.second->serverTime == relayTime, "State and COMMIT share normalized lifecycle clock in same tick");
    }
    expect(commits == 2, "Real commit packets sent to both authenticated recipients");
    g_serverTime = 1100; relayTime = 60000;
    CCutsceneVotes::ObserveOpcode(0x02E4, &host, relayTime);
    expect(::Room().Current().phase == Phase::Idle && relayTime == 1100, "Replacement LOAD cancels and normalizes");
    begin.state.serial = 3; CCutsceneVotes::Begin(begin, &host);
    expect(::Room().Current().phase == Phase::Idle, "BEGIN without new START rejected");
    CCutsceneVotes::ObserveOpcode(0x02E7, &host, relayTime); CCutsceneVotes::Begin(begin, &host);
    CCutsceneVotes::MissionEnded(); expect(::Room().Current().phase == Phase::Idle, "Mission end cancels pending vote");
    relayTime = 12345; expect(CCutsceneVotes::ObserveOpcode(0x1234, &guest, relayTime) && relayTime == 12345,
        "Unrelated opcode and timestamps unchanged");
    std::cout << checks << " assertions, " << failures << " failures\n";
    return failures ? 1 : 0;
}
