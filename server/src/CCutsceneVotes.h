#pragma once
#include "network/packets/cutscene.h"
class CNetworkPlayer;
namespace Packets::Scripts { class OpCodeSync; }
class CCutsceneVotes {
public:
    static void Join(CNetworkPlayer* player);
    static void GameplayReady(CNetworkPlayer* player);
    static void Leave(CNetworkPlayer* player);
    static void HostChanged(CNetworkPlayer* player);
    static void MissionEnded();
    static bool ObserveOpcode(uint16_t opcode, CNetworkPlayer* sender, uint32_t& relayTime);
    static bool RelayOpcode(Packets::Scripts::OpCodeSync& packet, CNetworkPlayer* sender);
    static void Begin(const Packets::Cutscene::Begin& packet, CNetworkPlayer* sender);
    static void Vote(const Packets::Cutscene::Vote& packet, CNetworkPlayer* sender);
};
