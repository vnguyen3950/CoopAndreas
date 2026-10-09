#include "stdafx.h"
#include "CSessionSync.h"
SessionSync::Server& CSessionSync::Room() { static SessionSync::Server room; return room; }
void CSessionSync::Send(CNetworkPlayer* player)
{
    if (!player) return;
    auto s = Room().For(player->m_iPlayerId);
    if (!s.Valid()) return;
    Packets::Session::Update packet; packet.state = s;
    GetPacketFactory().Send(packet, player);
}
void CSessionSync::Broadcast() { for (auto* player : CNetworkPlayerManager::m_pPlayers) Send(player); }
void CSessionSync::Join(CNetworkPlayer* player)
{
    auto* host = CNetworkPlayerManager::GetHost();
    if (player && host) Room().Join(player->m_iPlayerId, host->m_iPlayerId);
    Broadcast();
}
void CSessionSync::Leave(CNetworkPlayer* player)
{
    if (player) Room().Leave(player->m_iPlayerId);
    Broadcast();
}
void CSessionSync::HostChanged(CNetworkPlayer* player)
{
    if (player) Room().HostChanged(player->m_iPlayerId);
    Broadcast();
}
