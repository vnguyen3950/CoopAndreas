#include "stdafx.h"

std::vector<CNetworkPlayer*> CNetworkPlayerManager::m_pPlayers;
CPad CNetworkPlayerManager::m_pPads[Config::MAX_SERVER_PLAYERS + 2];
int CNetworkPlayerManager::m_nMyId;
std::atomic_bool CNetworkPlayerManager::m_bResetPending{false};

void CNetworkPlayerManager::Add(CNetworkPlayer* player)
{
    if (!player || std::find(m_pPlayers.begin(), m_pPlayers.end(), player) != m_pPlayers.end()) return;
    RemoveById(player->m_iPlayerId);
    m_pPlayers.push_back(player);
}

void CNetworkPlayerManager::Remove(CNetworkPlayer* player)
{
    auto it = std::find(m_pPlayers.begin(), m_pPlayers.end(), player);
    if (it != m_pPlayers.end())
    {
        m_pPlayers.erase(it);
    }
}

void CNetworkPlayerManager::RemoveById(int playerid)
{
    for (auto it = m_pPlayers.begin(); it != m_pPlayers.end();)
    {
        auto* player = *it;
        if (player && player->m_iPlayerId == playerid)
        {
            it = m_pPlayers.erase(it);
            delete player;
        }
        else ++it;
    }
}
void CNetworkPlayerManager::Reset()
{
    m_bResetPending.exchange(false, std::memory_order_acq_rel);
    auto previous = std::move(m_pPlayers);
    m_pPlayers.clear();
    for (auto* player : previous) delete player;
}
void CNetworkPlayerManager::RequestReset()
{ m_bResetPending.store(true, std::memory_order_release); }
void CNetworkPlayerManager::ProcessPendingReset()
{ if (m_bResetPending.load(std::memory_order_acquire)) Reset(); }

CNetworkPlayer* CNetworkPlayerManager::GetPlayer(SenderPlayerId playerid)
{
    return GetPlayer(playerid.value);
}

CNetworkPlayer* CNetworkPlayerManager::GetPlayer(int playerid)
{
    for (size_t i = 0; i != m_pPlayers.size(); i++)
    {
        if (m_pPlayers[i]->m_iPlayerId == playerid)
        {
            return m_pPlayers[i];
        }
    }
    return nullptr;
}

CNetworkPlayer* CNetworkPlayerManager::GetPlayer(CEntity* entity)
{
    for (size_t i = 0; i != m_pPlayers.size(); i++)
    {
        if (m_pPlayers[i]->m_pPed == entity)
        {
            return m_pPlayers[i];
        }
    }
    return nullptr;
}
