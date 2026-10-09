#include "network/packets/peds.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "CTrailerSync.h"
#include "network/vehicle_authority.h"

PACKET_HANDLER(ePacketType::PED_SPAWN, Packets::Peds::PedSpawn* pPedSpawn, CNetworkPlayer* pNetworkPlayer)
{
    if (!CNetworkPedManager::Authenticated(pNetworkPlayer) || !pPedSpawn->Valid() ||
        pPedSpawn->stamp.generation || pPedSpawn->stamp.epoch || pPedSpawn->tempid == 255) return;
    static char allowedSpecialActors[52][8] = {"ANDRE", "BBTHIN", "BB", "CAT", "CESAR", "COPGRL1", "COPGRL2", "COPGRL3",
        "CLAUDE", "CROGRL1", "CROGRL2", "CROGRL3", "DWAYNE", "EMMET", "FORELLI", "GANGRL1", "GANGRL2", "GANGRL3",
        "GUNGRL1", "GUNGRL2", "GUNGRL3", "HERN", "JANITOR", "JETHRO", "JIZZY", "KENDL", "MACCER", "MADDOGG", "MECGRL1",
        "MECGRL2", "MECGRL3", "NURGRL1", "NURGRL2", "NURGRL3", "OGLOC", "PAUL", "PULASKI", "ROSE", "RYDER1", "RYDER2",
        "RYDER3", "SINDACO", "SMOKE", "SMOKEV", "SUZIE", "SWEET", "TBONE", "TENPEN", "TORINO", "TRUTH", "WUZIMU",
        "ZERO"};

    if (pPedSpawn->modelId >= MODEL_SPECIAL01 && pPedSpawn->modelId <= MODEL_SPECIAL10)
    {
        bool isSpecialModelValid = false;

        for (int i = 0; i < ARRAY_SIZE(allowedSpecialActors); i++)
        {
            if (std::strncmp(pPedSpawn->specialModelName, allowedSpecialActors[i], sizeof pPedSpawn->specialModelName) == 0)
            {
                isSpecialModelValid = true;
                break;
            }
        }

        if (!isSpecialModelValid)
            return;
    }

    const int id = CNetworkPedManager::GetFreeId();
    if (id < 0 || !CNetworkPedManager::AcceptRequest(pNetworkPlayer, pPedSpawn->requestToken)) return;
    const uint32_t generation = CNetworkPedManager::AllocateGeneration();
    if (!generation) return;
    pPedSpawn->pedid = id; pPedSpawn->stamp = {generation, 1, 0};
    pPedSpawn->ownerid = pNetworkPlayer->m_iPlayerId;
    pPedSpawn->serverTime = g_serverTime;

    CNetworkPed* pNetworkPed = new CNetworkPed(
        pPedSpawn->pedid, pNetworkPlayer, pPedSpawn->modelId, pPedSpawn->pedType, pPedSpawn->pos, pPedSpawn->createdBy);
    snprintf(pNetworkPed->m_szSpecialModelName, sizeof(pNetworkPed->m_szSpecialModelName), "%s",
        pPedSpawn->specialModelName);

    pNetworkPed->m_generation = generation; pNetworkPed->m_requestToken = pPedSpawn->requestToken;
    CNetworkPedManager::Add(pNetworkPed);
    GetPacketFactory().SendToAll(*pPedSpawn, pNetworkPlayer);

    // send it back to the syncer of the ped so that he knows the id
    Packets::Peds::PedConfirm pedConfirmPacket{};
    pedConfirmPacket.tempid = pPedSpawn->tempid;
    pedConfirmPacket.pedid = pPedSpawn->pedid;
    pedConfirmPacket.stamp = pNetworkPed->GetStamp();
    pedConfirmPacket.requestToken = pPedSpawn->requestToken;
    pedConfirmPacket.ownerid = pNetworkPlayer->m_iPlayerId;
    GetPacketFactory().Send(pedConfirmPacket, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_REMOVE, Packets::Peds::PedRemove* packet, CNetworkPlayer* sender)
{
    auto* ped = CNetworkPedManager::GetPed(packet->pedid);
    if (!CNetworkPedManager::Authenticated(sender) || !ped || ped->m_pSyncer != sender ||
        !packet->stamp.SameOwner(ped->GetStamp())) return;
    if (!ped->m_bPinned) for (auto* candidate : CNetworkPlayerManager::m_pPlayers) {
        if (candidate == sender || !CNetworkPedManager::Authenticated(candidate) ||
            std::find(candidate->m_vPedClaims.begin(), candidate->m_vPedClaims.end(), ped) == candidate->m_vPedClaims.end()) continue;
        if (!CNetworkPedManager::AssignOwner(ped, candidate)) break;
        auto spawn = ped->SpawnPacket(); GetPacketFactory().Send(spawn, sender);
        CNetworkPedManager::Replay(ped, sender); // Recreate the original owner's removed local actor as a replica.
        return;
    }
    CNetworkPedManager::DeleteAndNotify(ped, sender);
}

PACKET_HANDLER(ePacketType::PED_ONFOOT, Packets::Peds::PedOnFoot* pPedOnFoot, CNetworkPlayer* pNetworkPlayer)
{
    if (!CNetworkPedManager::Authenticated(pNetworkPlayer) || !pPedOnFoot->Valid()) return;
    CNetworkPed* pPed = CNetworkPedManager::GetPed(pPedOnFoot->pedid);
    if (pPed)
    {
        if (pPed->m_pSyncer != pNetworkPlayer)
        {
            logger::warn("%s tries to update (on foot) someone else's ped", pNetworkPlayer->GetName().c_str());
            return;
        }

        if (!pPed->AcceptState(pPedOnFoot->stamp)) return;
        pPed->m_lastState.mode = 1; pPed->m_lastState.onFoot = *pPedOnFoot; pPed->m_hasState = true;
        pPed->m_vecPos = pPedOnFoot->pos;
        GetPacketFactory().SendToAll(*pPedOnFoot, pNetworkPlayer);
    }
}

PACKET_HANDLER(
    ePacketType::PED_DRIVER_UPDATE, Packets::Peds::PedDriverUpdate* pPedDriverUpdate, CNetworkPlayer* pNetworkPlayer)
{
    if (!CNetworkPedManager::Authenticated(pNetworkPlayer) || !pPedDriverUpdate->Valid()) return;
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedDriverUpdate->pedid);
    if (pNetworkPed == nullptr)
    {
        return;
    }

    if (pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        logger::warn("%s tries to update (driver) someone else's ped", pNetworkPlayer->GetName().c_str());
        return;
    }

    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPedDriverUpdate->vehicleid);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }

    if (!VehicleAuthority::CanUpdateNpcDriver(pNetworkPlayer, pNetworkPed->m_pSyncer, pNetworkVehicle->m_pPlayers[0]))
        return;

    if (!pNetworkPed->AcceptState(pPedDriverUpdate->stamp)) return;
    pNetworkPed->m_lastState.mode = 2; pNetworkPed->m_lastState.driver = *pPedDriverUpdate; pNetworkPed->m_hasState = true;
    pNetworkPed->m_vecPos = pPedDriverUpdate->pos;
    pNetworkVehicle->m_bUsedByPed = true;
    pNetworkVehicle->m_vecPosition = pPedDriverUpdate->pos;
    pNetworkVehicle->m_vecRotation = pPedDriverUpdate->rot;
    CTrailerSync::NpcDriver(pNetworkVehicle,pNetworkPed);

    GetPacketFactory().SendToAll(*pPedDriverUpdate, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_PASSENGER_UPDATE, Packets::Peds::PedPassengerSync* pPedPassengerSync,
    CNetworkPlayer* pNetworkPlayer)
{
    if (!CNetworkPedManager::Authenticated(pNetworkPlayer) || !pPedPassengerSync->Valid()) return;
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedPassengerSync->pedid);
    if (pNetworkPed == nullptr)
    {
        return;
    }

    if (pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        logger::warn("%s tries to update (passenger) someone else's ped", pNetworkPlayer->GetName().c_str());
        return;
    }

    auto* vehicle = CNetworkVehicleManager::GetVehicle(pPedPassengerSync->vehicleid);
    if (!vehicle || (pPedPassengerSync->seatid + 1 < ARRAY_SIZE(vehicle->m_pPlayers) &&
        vehicle->m_pPlayers[pPedPassengerSync->seatid + 1]) || !pNetworkPed->AcceptState(pPedPassengerSync->stamp)) return;
    pNetworkPed->m_vecPos = vehicle->m_vecPosition;
    pNetworkPed->m_lastState.mode = 3; pNetworkPed->m_lastState.passenger = *pPedPassengerSync; pNetworkPed->m_hasState = true;
    GetPacketFactory().SendToAll(*pPedPassengerSync, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_SHOT_SYNC, Packets::Peds::PedShotSync* pPedShotSync, CNetworkPlayer* pNetworkPlayer)
{
    if (!CNetworkPedManager::Authenticated(pNetworkPlayer) || !pPedShotSync->Valid()) return;
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedShotSync->pedid);
    if (pNetworkPed == nullptr)
    {
        return;
    }

    if (pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        logger::warn("%s tries to update (shoot) someone else's ped", pNetworkPlayer->GetName().c_str());
        return;
    }

    if (!pPedShotSync->stamp.SameOwner(pNetworkPed->GetStamp())) return;
    pPedShotSync->serverTime = g_serverTime;
    GetPacketFactory().SendToAll(*pPedShotSync, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_SAY, Packets::Peds::PedSay* pPedSay, CNetworkPlayer* pNetworkPlayer)
{
    if (!CNetworkPedManager::Authenticated(pNetworkPlayer)) return;
    if (pPedSay->entity.entityType == NETWORK_ENTITY_TYPE_PLAYER)
    {
        pPedSay->entity.entityId = pNetworkPlayer->m_iPlayerId;
    }
    else if (pPedSay->entity.entityType == NETWORK_ENTITY_TYPE_PED)
    {
        CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedSay->entity.entityId);
        if (pNetworkPed == nullptr || !pPedSay->entity.Valid() ||
            pNetworkPed->m_generation != pPedSay->entity.entityGeneration || pNetworkPed->m_pSyncer != pNetworkPlayer)
        {
            return;
        }
    }

    GetPacketFactory().SendToAll(*pPedSay, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_CLAIM_ON_RELEASE, Packets::Peds::PedClaimOnRelease* packet, CNetworkPlayer* sender)
{
    auto* ped = CNetworkPedManager::GetPed(packet->pedid);
    if (!CNetworkPedManager::Authenticated(sender) || !ped || ped->m_bPinned || ped->m_pSyncer == sender ||
        !packet->stamp.SameOwner(ped->GetStamp())) return;
    if (std::find(sender->m_vPedClaims.begin(), sender->m_vPedClaims.end(), ped) == sender->m_vPedClaims.end()) sender->m_vPedClaims.push_back(ped);
}
PACKET_HANDLER(ePacketType::PED_CANCEL_CLAIM, Packets::Peds::PedCancelClaim* packet, CNetworkPlayer* sender)
{
    auto* ped = CNetworkPedManager::GetPed(packet->pedid);
    if (!CNetworkPedManager::Authenticated(sender) || !ped || !packet->stamp.SameOwner(ped->GetStamp())) return;
    sender->m_vPedClaims.erase(std::remove(sender->m_vPedClaims.begin(), sender->m_vPedClaims.end(), ped), sender->m_vPedClaims.end());
}
PACKET_HANDLER(ePacketType::PED_RESET_ALL_CLAIMS, Packets::Peds::PedResetAllClaims* packet, CNetworkPlayer* sender)
{
    auto* ped = CNetworkPedManager::GetPed(packet->pedid);
    if (!CNetworkPedManager::Authenticated(sender) || !ped || !packet->stamp.SameOwner(ped->GetStamp()) ||
        (ped->m_pSyncer != sender && !sender->m_bIsHost)) return;
    if (sender->m_bIsHost && !CNetworkPedManager::AssignOwner(ped, sender)) return;
    CNetworkPedManager::ClearClaims(ped);
    packet->stamp = ped->GetStamp(); packet->serverTime = g_serverTime; GetPacketFactory().SendToAll(*packet);
}
PACKET_HANDLER(ePacketType::PED_TAKE_HOST, Packets::Peds::PedTakeHost* packet, CNetworkPlayer* sender)
{
    auto* ped = CNetworkPedManager::GetPed(packet->pedid);
    if (!CNetworkPedManager::Authenticated(sender) || !sender->m_bIsHost || !ped || ped->m_bPinned ||
        !packet->stamp.SameOwner(ped->GetStamp())) return;
    auto* previous = ped->m_pSyncer;
    if (!CNetworkPedManager::AssignOwner(ped, sender)) return;
    if (packet->allowReturnToPreviousHost && previous != sender && CNetworkPedManager::Authenticated(previous)) previous->m_vPedClaims.push_back(ped);
}
PACKET_HANDLER(ePacketType::PED_PIN, Packets::Peds::PedPin* packet, CNetworkPlayer* sender)
{
    if (!CNetworkPedManager::Authenticated(sender) || !sender->m_bIsHost) return;
    auto* ped = CNetworkPedManager::GetPed(packet->pedid);
    if (!packet->stamp.generation && !packet->stamp.epoch && !packet->stamp.sequence && packet->requestToken) {
        ped = nullptr;
        for (auto* candidate : CNetworkPedManager::m_pPeds)
            if (candidate->m_pSyncer == sender && candidate->m_requestToken == packet->requestToken) { ped = candidate; break; }
        if (!ped) return;
        packet->pedid = ped->m_nPedId;
    } else if (!ped || packet->requestToken || !packet->stamp.SameOwner(ped->GetStamp())) return;
    // Only the host may pin its real native wave. A queued claim cannot move it;
    // a stale epoch cannot unpin a newer owner lifetime.
    if (!CNetworkPedManager::AssignOwner(ped, sender)) return;
    ped->m_bPinned = packet->pinned; CNetworkPedManager::ClearClaims(ped);
    packet->stamp = ped->GetStamp(); packet->requestToken = 0; packet->serverTime = g_serverTime; GetPacketFactory().SendToAll(*packet);
}
