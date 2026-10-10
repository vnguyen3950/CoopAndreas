#include "stdafx.h"
#include <network/packets/peds.h>
#include <unordered_map>
namespace { uint32_t generation = 0; std::unordered_map<CNetworkPlayer*, uint32_t> requestHighWater; }

std::vector<CNetworkPed*> CNetworkPedManager::m_pPeds;

void CNetworkPedManager::Add(CNetworkPed* ped)
{
    m_pPeds.push_back(ped);
}

void CNetworkPedManager::Remove(CNetworkPed* ped)
{
    auto it = std::find(m_pPeds.begin(), m_pPeds.end(), ped);
    // std::find()
    if (it != m_pPeds.end())
    {
        m_pPeds.erase(it);
    }
}

CNetworkPed* CNetworkPedManager::GetPed(int pedid)
{
    for (int i = 0; i != m_pPeds.size(); i++)
    {
        if (m_pPeds[i]->m_nPedId == pedid)
        {
            return m_pPeds[i];
        }
    }
    return nullptr;
}

int CNetworkPedManager::GetFreeId()
{
    for (int i = 0; i < Config::MAX_SERVER_PEDS; i++)
    {
        if (CNetworkPedManager::GetPed(i) == nullptr)
            return i;
    }

    return -1;
}

bool CNetworkPedManager::Authenticated(CNetworkPlayer* player)
{
    return player && player->m_pPeer && player->m_pPeer->state == ENET_PEER_STATE_CONNECTED &&
        CNetworkPlayerManager::GetPlayer(player->m_pPeer) == player;
}
uint32_t CNetworkPedManager::AllocateGeneration()
{
    return generation == NPCSync::MaxCounter ? 0 : ++generation;
}
bool CNetworkPedManager::AcceptRequest(CNetworkPlayer* player, uint32_t token)
{
    if (!Authenticated(player) || token == 0 || token > NPCSync::MaxCounter || token <= requestHighWater[player]) return false;
    requestHighWater[player] = token;
    return true;
}
void CNetworkPedManager::ClearClaims(CNetworkPed* ped)
{
    for (auto* player : CNetworkPlayerManager::m_pPlayers)
        player->m_vPedClaims.erase(std::remove(player->m_vPedClaims.begin(), player->m_vPedClaims.end(), ped), player->m_vPedClaims.end());
}
void CNetworkPedManager::Replay(CNetworkPed* ped, CNetworkPlayer* recipient)
{
    if (ped && ped->m_deathStamp.State() && Authenticated(recipient)) {
        Packets::Peds::PedDeath death; death.pedid = ped->m_nPedId; death.stamp = ped->m_deathStamp;
        death.position = ped->m_deathPosition; death.area = ped->m_deathArea; death.serverTime = g_serverTime;
        GetPacketFactory().Send(death, recipient);
    }
    if (ped && ped->m_hasState && Authenticated(recipient)) {
        auto packet = ped->m_lastState; packet.serverTime = g_serverTime;
        GetPacketFactory().Send(packet, recipient);
    }
}
bool CNetworkPedManager::AssignOwner(CNetworkPed* ped, CNetworkPlayer* player)
{
    if (!ped || !Authenticated(player) || (ped->m_bPinned && !player->m_bIsHost)) return false;
    if (ped->m_pSyncer == player) return true;
    if (ped->m_ownerEpoch == NPCSync::MaxCounter) return false;
    ped->m_pSyncer = player; ++ped->m_ownerEpoch;
    if (ped->m_hasState) {
        auto& state = ped->m_lastState;
        if (state.mode == 1) state.onFoot.stamp = ped->GetStamp();
        else if (state.mode == 2) state.driver.stamp = ped->GetStamp();
        else state.passenger.stamp = ped->GetStamp();
    }
    Packets::Peds::AssignPedSyncer packet; packet.pedid = ped->m_nPedId;
    packet.stamp = ped->GetStamp(); packet.ownerid = player->m_iPlayerId;
    GetPacketFactory().SendToAll(packet);
    Replay(ped, player); // Same EVENT order: assignment then retained native state, before new-owner updates.
    ClearClaims(ped);
    return true;
}
void CNetworkPedManager::DeleteAndNotify(CNetworkPed* ped, CNetworkPlayer* ignore)
{
    if (!ped) return;
    ClearClaims(ped);
    Packets::Peds::PedRemove packet; packet.pedid = ped->m_nPedId; packet.stamp = ped->GetStamp();
    GetPacketFactory().SendToAll(packet, ignore);
    Remove(ped); delete ped;
}
void CNetworkPedManager::RemoveAllHostedAndNotify(CNetworkPlayer* player)
{
    auto previous = m_pPeds;
    for (auto* ped : previous) {
        if (ped->m_pSyncer != player) continue;
        CNetworkPlayer* successor = nullptr;
        if (!ped->m_bPinned) for (auto* candidate : CNetworkPlayerManager::m_pPlayers)
            if (candidate != player && Authenticated(candidate) &&
                std::find(candidate->m_vPedClaims.begin(), candidate->m_vPedClaims.end(), ped) != candidate->m_vPedClaims.end())
                { successor = candidate; break; }
        if (!successor || !AssignOwner(ped, successor)) DeleteAndNotify(ped, player);
    }
    player->m_vPedClaims.clear(); requestHighWater.erase(player);
}

bool CNetworkPedManager::GetDeathProducer(CNetworkPlayer* sender, int pedId, const NPCSync::Stamp& sealedDeath)
{
    if (!Authenticated(sender) || !sealedDeath.State()) return false;
    auto* ped = GetPed(pedId);
    return ped && ped->m_generation == sealedDeath.generation && ped->m_deathStamp.State()
        && ped->m_deathStamp.SameOwner(sealedDeath) && ped->m_deathStamp.sequence == sealedDeath.sequence
        && ped->m_deathProducer == sender && sender->m_vitals.generation == ped->m_deathProducerGeneration;
}
