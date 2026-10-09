#include "stdafx.h"
#include "CPlayerVitalsSync.h"

uint32_t CPlayerVitalsServer::AllocateGeneration()
{ static PlayerVitals::GenerationCounter counter; return counter.Next(); }
void CPlayerVitalsServer::Announce(CNetworkPlayer* player)
{
    if (!player || !player->m_vitals.generation) return;
    Packets::Players::Vitals identity;
    identity.playerid = player->m_iPlayerId; identity.generation = player->m_vitals.generation;
    GetPacketFactory().SendToAll(identity, player);
}
void CPlayerVitalsServer::Replay(CNetworkPlayer* owner, CNetworkPlayer* recipient)
{
    if (!owner || !recipient || owner == recipient || !owner->m_vitals.generation) return;
    Packets::Players::Vitals packet;
    packet.playerid = owner->m_iPlayerId; packet.generation = owner->m_vitals.generation;
    // Bind before the full snapshot even when native spawn has not happened.
    GetPacketFactory().Send(packet, recipient);
    if (owner->m_vitals.hasState)
    {
        packet.hasState = true; packet.sequence = owner->m_vitals.sequence; packet.state = owner->m_vitals.state;
        GetPacketFactory().Send(packet, recipient);
    }
}
bool CPlayerVitalsServer::Receive(Packets::Players::Vitals packet, CNetworkPlayer* sender)
{
    if (!sender || !sender->m_pPeer || CNetworkPlayerManager::GetPlayer(sender->m_pPeer) != sender
        || CNetworkPlayerManager::GetPlayer(sender->m_iPlayerId) != sender
        || !packet.Valid() || !packet.hasState) return false;
    if (!sender->m_vitals.AcceptOwner(sender->m_iPlayerId, packet.playerid, packet.generation,
        packet.sequence, packet.state)) return false;
    packet.generation = sender->m_vitals.generation;
    GetPacketFactory().SendToAll(packet, sender);
    return true;
}
