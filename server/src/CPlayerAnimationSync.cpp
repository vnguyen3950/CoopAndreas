#include "stdafx.h"
#include "CPlayerAnimationSync.h"
#include "network/packets/players.h"

namespace
{
bool Owner(CNetworkPlayer* player)
{
    return player && player->m_pPeer && player->m_vitals.generation
        && CNetworkPlayerManager::GetPlayer(player->m_pPeer) == player
        && CNetworkPlayerManager::GetPlayer(player->m_iPlayerId) == player;
}
bool Accept(PlayerAnimation::Life& life, const PlayerAnimation::State& visual,
    int claimedOwner, CNetworkPlayer* owner)
{
    if (!Owner(owner) || claimedOwner != owner->m_iPlayerId || life.generation
        || !life.Valid() || !visual.Valid()) return false;
    life.generation = owner->m_vitals.generation;
    return owner->m_actorLife.Accept(life, visual, g_serverTime);
}
}
bool CPlayerAnimationServer::Receive(Packets::Players::PlayerAnimationState packet, CNetworkPlayer* sender)
{
    if (!packet.Valid() || !Accept(packet.life, packet.state, packet.playerid, sender)) return false;
    packet.sampledAt = packet.serverTime = g_serverTime;
    // Echo acknowledges the exact owner birth; pickup must not use an unbound life.
    GetPacketFactory().SendToAll(packet);
    return true;
}
bool CPlayerAnimationServer::Respawn(Packets::Players::RespawnPlayer packet, CNetworkPlayer* sender)
{
    if (packet.life.ready || !Owner(sender) || (sender->m_actorLife.hasLife
        && packet.life.birth <= sender->m_actorLife.life.birth)
        || !Accept(packet.life, {}, packet.playerid.value, sender)) return false;
    packet.serverTime = g_serverTime;
    GetPacketFactory().SendToAll(packet, sender);
    // Owner acknowledgment shares the same birth/sequence as the reset boundary.
    Packets::Players::PlayerAnimationState ack;
    ack.playerid = sender->m_iPlayerId; ack.life = packet.life;
    ack.sampledAt = ack.serverTime = g_serverTime;
    GetPacketFactory().Send(ack, sender);
    return true;
}
void CPlayerAnimationServer::Replay(CNetworkPlayer* owner, CNetworkPlayer* recipient)
{
    PlayerAnimation::Life life;
    if (!recipient || !GetActorLife(owner, life)) return;
    Packets::Players::PlayerAnimationState packet;
    packet.playerid = owner->m_iPlayerId; packet.life = life;
    packet.state = owner->m_actorLife.state; packet.sampledAt = owner->m_actorLife.sampledAt;
    // Never mutate/reuse a cached outer timestamp; keep original phase sample time.
    packet.serverTime = g_serverTime;
    GetPacketFactory().Send(packet, recipient);
}
bool CPlayerAnimationServer::GetActorLife(const CNetworkPlayer* player, PlayerAnimation::Life& out)
{
    if (!player || !player->m_pPeer || CNetworkPlayerManager::GetPlayer(player->m_pPeer) != player
        || CNetworkPlayerManager::GetPlayer(player->m_iPlayerId) != player
        || !player->m_actorLife.hasLife || !player->m_actorLife.life.ready || !player->m_actorLife.life.Valid(true)
        || player->m_actorLife.life.generation != player->m_vitals.generation) return false;
    out = player->m_actorLife.life; return true;
}
