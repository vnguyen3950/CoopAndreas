#include "stdafx.h"
#include "CCutsceneVotes.h"
#include "network/packets/scripts.h"

namespace {
CutsceneVotes::Room& Room() { static CutsceneVotes::Room room; return room; }
bool startAvailable = false;
CNetworkPlayer* loadOwner = nullptr;
bool Authenticated(CNetworkPlayer* player) {
    return player && player->m_pPeer && player->m_pPeer->state == ENET_PEER_STATE_CONNECTED &&
        CNetworkPlayerManager::GetPlayer(player->m_pPeer) == player;
}
void Broadcast(bool begin = false) {
    for (auto* player : CNetworkPlayerManager::m_pPlayers) {
        if (!Authenticated(player)) continue;
        const auto state = Room().For(player->m_iPlayerId);
        if (begin) {
            Packets::Cutscene::Begin packet; packet.state = state; GetPacketFactory().Send(packet, player);
        } else {
            Packets::Cutscene::Update packet; packet.state = state; GetPacketFactory().Send(packet, player);
        }
        if (state.phase == CutsceneVotes::Phase::Committed) {
            Packets::Cutscene::Commit packet; packet.state = state; GetPacketFactory().Send(packet, player);
        }
    }
}
}
static_assert(Config::MAX_SERVER_PLAYERS == CutsceneVotes::MaxPeers, "Vote identity capacity must match room capacity");
void CCutsceneVotes::Join(CNetworkPlayer* player) {
    if (Authenticated(player)) Room().Join(player->m_iPlayerId);
    // Late joins are observers; no BEGIN is sent without a corresponding START.
}
void CCutsceneVotes::GameplayReady(CNetworkPlayer* player) {
    if (Authenticated(player)) Room().GameplayReady(player->m_iPlayerId);
}
void CCutsceneVotes::Leave(CNetworkPlayer* player) {
    if (!player) return;
    if (player->m_bIsHost) { startAvailable = false; loadOwner = nullptr; }
    Room().Leave(player->m_iPlayerId); Broadcast();
}
void CCutsceneVotes::HostChanged(CNetworkPlayer* player) {
    startAvailable = false; loadOwner = nullptr; Room().HostChanged(player ? player->m_iPlayerId : -1); Broadcast();
}
void CCutsceneVotes::MissionEnded() { startAvailable = false; loadOwner = nullptr; Room().Cancel(); Broadcast(); }
bool CCutsceneVotes::ObserveOpcode(uint16_t opcode, CNetworkPlayer* sender, uint32_t& relayTime) {
    if (opcode != 0x02E4 && opcode != 0x02E7 && opcode != 0x02EA && opcode != 0x0701) return true;
    // Only the room host may supply the lifecycle that binds a skip vote.
    if (!Authenticated(sender) || !sender->m_bIsHost || Room().Host() != sender->m_iPlayerId) return false;
    relayTime = g_serverTime; // Same clock as announcements; reliable order survives the client timestamp buffer.
    if (opcode == 0x02E4) {
        GameplayReady(sender); Room().ClearPreparation(); Room().Prepare(sender->m_iPlayerId); loadOwner = sender;
    }
    if (opcode == 0x02E7) {
        if (loadOwner != sender || !Room().Prepared(sender->m_iPlayerId)) return false;
        Room().Start(sender->m_iPlayerId);
    }
    if (opcode == 0x02EA) loadOwner = nullptr;
    Room().Cancel(); startAvailable = opcode == 0x02E7; Broadcast(); return true;
}
bool CCutsceneVotes::RelayOpcode(Packets::Scripts::OpCodeSync& packet, CNetworkPlayer* sender) {
    uint16_t opcode = 0; if (packet.size >= 4) std::memcpy(&opcode, packet.buffer, sizeof opcode);
    if (opcode != 0x02E4 && opcode != 0x02E7 && opcode != 0x02EA && opcode != 0x0701) return false;
    for (auto* player : CNetworkPlayerManager::m_pPlayers) {
        if (player == sender || !Authenticated(player)) continue;
        const int id = player->m_iPlayerId;
        if (opcode == 0x02E4) {
            if (!Room().Ready(id)) continue;
            GetPacketFactory().Send(packet, player); Room().Prepare(id);
        } else {
            if (!Room().Prepared(id)) continue;
            GetPacketFactory().Send(packet, player);
            if (opcode == 0x02E7) Room().Start(id);
        }
    }
    if (opcode == 0x02EA) Room().ClearPreparation();
    return true;
}
void CCutsceneVotes::Begin(const Packets::Cutscene::Begin& packet, CNetworkPlayer* sender) {
    if (!Authenticated(sender) || !sender->m_bIsHost || Room().Host() != sender->m_iPlayerId) return;
    if (!packet.active) {
        if (Room().Cancel(sender->m_iPlayerId, packet.state.serial)) { startAvailable = false; Broadcast(); }
        return;
    }
    if (!startAvailable || packet.state.generation || packet.state.identity || packet.state.phase != CutsceneVotes::Phase::Idle) return;
    if (Room().Begin(sender->m_iPlayerId, packet.state.serial, packet.state.name)) {
        startAvailable = false; Broadcast(true);
    }
}
void CCutsceneVotes::Vote(const Packets::Cutscene::Vote& packet, CNetworkPlayer* sender) {
    if (Authenticated(sender) && Room().Vote(sender->m_iPlayerId, packet.generation, packet.identity)) Broadcast();
}
