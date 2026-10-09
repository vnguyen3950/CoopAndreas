#include "native_doubles.h"
#include "server/src/CCutsceneVotes.h"
#include "network/session_sync.h"
#include "network/object_sync.h"
#include "extracted_server.inc"
struct CNetworkObjectManager { static bool RemapOpcode(uint8_t*, int, CNetworkPlayer*) { return false; } };
#include "extracted_relay.inc"
using CutsceneVotes::Phase;
static void RelayCommand(uint16_t opcode, CNetworkPlayer* sender) {
    Packets::Scripts::OpCodeSync packet; packet.size = 4; std::memcpy(packet.buffer, &opcode, sizeof opcode);
    Relay(&packet, sender);
}
static bool Delivered(int id) {
    for (const auto& record : GetPacketFactory().sent)
        if (record.first == id && record.second->GetType() == ePacketType::OPCODE_SYNC) return true;
    return false;
}
int main() {
    ENetPeer hp, ep, mp;
    CNetworkPlayer host{&hp, 0, true}, early{&ep, 1, false}, menu{&mp, 2, false};
    CNetworkPlayerManager::m_pPlayers = {&host, &early, &menu};
    CCutsceneVotes::Join(&host); CCutsceneVotes::Join(&early); CCutsceneVotes::Join(&menu);
    CCutsceneVotes::HostChanged(&host); CCutsceneVotes::GameplayReady(&early);
    GetPacketFactory().sent.clear(); RelayCommand(0x02E4, &host);
    expect(!Delivered(2), "Independent vote-menu-relay-002 repro: menu-only peer never receives native LOAD");
    expect(Delivered(1) && ::Room().Prepared(1), "Ready peer is recorded only when LOAD is relayed");
    CCutsceneVotes::GameplayReady(&menu);
    GetPacketFactory().sent.clear(); RelayCommand(0x02E7, &host);
    expect(Delivered(1) && ::Room().Started(1), "Prepared ready peer receives START");
    expect(!Delivered(2) && !::Room().Started(2), "Becoming ready after LOAD cannot receive an unmatched START");
    Packets::Cutscene::Begin begin; begin.state.serial = 1; begin.state.name = CutsceneVotes::Name("INTRO1A");
    CCutsceneVotes::Begin(begin, &host);
    expect(::Room().Current().total == 2 && !::Room().For(2).eligible, "Only actual LOAD/START recipients enter electorate");
    GetPacketFactory().sent.clear(); RelayCommand(0x02EA, &host);
    expect(Delivered(1) && !Delivered(2), "CLEAR reaches prepared recipients, not late-ready peers");
    expect(!::Room().Prepared(1) && !::Room().Started(1), "CLEAR removes delivery ledger for retry");
    GetPacketFactory().sent.clear(); RelayCommand(0x02E7, &host);
    expect(!Delivered(1) && !Delivered(2), "START without a fresh synchronized LOAD is rejected");
    GetPacketFactory().sent.clear(); RelayCommand(0x1234, &host);
    expect(Delivered(1) && Delivered(2), "Unrelated opcode retains original relay recipients");
    GetPacketFactory().sent.clear(); RelayCommand(0x02E4, &host); RelayCommand(0x02E7, &host);
    expect(Delivered(2) && ::Room().Started(2), "Next LOAD/START admits previously late-ready peer");
    begin.state.serial = 2; CCutsceneVotes::Begin(begin, &host);
    expect(::Room().Current().total == 3 && ::Room().For(2).eligible, "Late-ready peer enters only next scene's roster");
    CCutsceneVotes::Leave(&early); CCutsceneVotes::Join(&early); CCutsceneVotes::GameplayReady(&early);
    GetPacketFactory().sent.clear(); RelayCommand(0x02E7, &host);
    expect(!Delivered(1), "Reused connection cannot inherit earlier scene preparation");
    CCutsceneVotes::MissionEnded();
    GetPacketFactory().sent.clear(); RelayCommand(0x02EA, &host);
    expect(Delivered(2), "Mission-end cancellation preserves necessary CLEAR delivery");
    std::cout << checks << " assertions, " << failures << " failures\n";
    return failures ? 1 : 0;
}
