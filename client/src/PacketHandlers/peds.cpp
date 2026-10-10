#include "network/packets/peds.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include <CCarEnterExit.h>
#include <CTaskSimpleCarSetPedInAsPassenger.h>

PACKET_HANDLER(ePacketType::PED_SPAWN, Packets::Peds::PedSpawn* pPedSpawn)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("PED SPAWN %d %d %.1f %.1f %.1f %d %d", packet->pedid, packet->modelId, packet->pos.x,
        packet->pos.y, packet->pos.z, packet->pedType, packet->createdBy);
#endif

    if (!CNetwork::m_bAuthenticated || !pPedSpawn->Valid() || !pPedSpawn->stamp.Lifetime()) return;
    if (CNetworkPedManager::Defer(*pPedSpawn, pPedSpawn->pedid, pPedSpawn->stamp) ||
        !CNetworkPedManager::AcceptSpawn(pPedSpawn->pedid, pPedSpawn->stamp)) return;
    CNetworkPed* pNetworkPed = new CNetworkPed(pPedSpawn->pedid, pPedSpawn->modelId, pPedSpawn->pedType, pPedSpawn->pos,
        pPedSpawn->createdBy, pPedSpawn->specialModelName);

    pNetworkPed->m_generation = pPedSpawn->stamp.generation;
    pNetworkPed->m_ownerEpoch = pPedSpawn->stamp.epoch;
    pNetworkPed->m_ownerId = pPedSpawn->ownerid;
    pNetworkPed->m_bSyncing = pPedSpawn->ownerid == CNetworkPlayerManager::m_nMyId;
    pNetworkPed->m_stateSequence = pPedSpawn->stamp.sequence;
    if (!pNetworkPed->HasValidPed()) {
        pNetworkPed->m_bSyncing = false; delete pNetworkPed;
        CNetworkPedManager::Defer(*pPedSpawn, pPedSpawn->pedid, pPedSpawn->stamp, true); return;
    }
    CNetworkPedManager::Add(pNetworkPed);
}

PACKET_HANDLER(ePacketType::PED_CONFIRM, Packets::Peds::PedConfirm* pPedConfirm)
{
    if (!CNetwork::m_bAuthenticated || !pPedConfirm->stamp.Lifetime() ||
        pPedConfirm->ownerid != CNetworkPlayerManager::m_nMyId || !CNetworkPed::WasRequested(pPedConfirm->requestToken)) return;
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("PED CONFIRM %d %d", packet->pedid, packet->tempid);
#endif

    if (pPedConfirm->tempid < ARRAY_SIZE(CNetworkPedManager::m_apTempPeds))
    {
        CNetworkPed* pTempPed = CNetworkPedManager::m_apTempPeds[pPedConfirm->tempid];
        if (!pTempPed || pTempPed->m_requestToken != pPedConfirm->requestToken) {
            // The original native actor disappeared before confirmation. Free
            // only that server lifetime, never a newer occupant of this temp slot.
            Packets::Peds::PedRemove remove; remove.pedid = pPedConfirm->pedid; remove.stamp = pPedConfirm->stamp;
            GetPacketFactory().Send(remove); return;
        }
        if (pTempPed && pTempPed->m_requestToken == pPedConfirm->requestToken &&
            pPedConfirm->stamp.Lifetime() && pPedConfirm->ownerid == CNetworkPlayerManager::m_nMyId)
        {
            pTempPed->m_nPedId = pPedConfirm->pedid;
            pTempPed->m_generation = pPedConfirm->stamp.generation;
            pTempPed->m_ownerEpoch = pPedConfirm->stamp.epoch;
            pTempPed->m_ownerId = pPedConfirm->ownerid;
            CNetworkPedManager::m_apTempPeds[pPedConfirm->tempid] = nullptr;

            if (!pTempPed->HasValidPed())
            {
                delete pTempPed;
            }
            else
            {
                if (!CNetworkPedManager::AcceptSpawn(pTempPed->m_nPedId, pTempPed->GetStamp())) {
                    pTempPed->m_nPedId = -1; delete pTempPed; return;
                }
                CNetworkPedManager::Add(pTempPed);
                if (pTempPed->m_bPinned) CNetworkPedManager::PinGangWarPedToHost(pTempPed->m_pPed, true);
            }
        }
    }
}

PACKET_HANDLER(ePacketType::PED_REMOVE, Packets::Peds::PedRemove* pPedRemove)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("PED REMOVE %d", pPedRemove->pedid);
#endif

    if (CNetworkPedManager::Defer(*pPedRemove, pPedRemove->pedid, pPedRemove->stamp) ||
        !CNetworkPedManager::AcceptRemoval(pPedRemove->pedid, pPedRemove->stamp)) return;
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedRemove->pedid);
    if (pNetworkPed)
    {
        CNetworkPedManager::Remove(pNetworkPed);
        pNetworkPed->m_bSyncing = false;
        delete pNetworkPed;
    }
}

PACKET_HANDLER(ePacketType::ASSIGN_PED, Packets::Peds::AssignPedSyncer* packet)
{
    auto* ped = CNetworkPedManager::GetPed(packet->pedid);
    if (CNetworkPedManager::Defer(*packet, packet->pedid, packet->stamp, !ped)) return;
    if (!NPCSync::Assignable(ped->GetStamp(), packet->stamp, ped->m_ownerId, packet->ownerid)) return;
    if (packet->stamp.epoch == ped->m_ownerEpoch) return; // Explicit repeated assignment is idempotent.
    ped->m_ownerEpoch = packet->stamp.epoch; ped->m_ownerId = packet->ownerid;
    ped->m_stateSequence = packet->stamp.sequence;
    ped->m_bSyncing = packet->ownerid == CNetworkPlayerManager::m_nMyId;
    ped->m_bClaimOnRelease = false;
    ped->m_pPed->SetCharCreatedBy(ped->m_bSyncing ? ped->m_nCreatedBy : MISSION_CHAR);
}

PACKET_HANDLER(ePacketType::PED_ONFOOT, Packets::Peds::PedOnFoot* pPedOnFoot)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedOnFoot->pedid);

    if (!pNetworkPed || !pPedOnFoot->Valid() || (pNetworkPed->m_bSyncing && !pNetworkPed->m_bAllowReplay) ||
        !pNetworkPed->AcceptState(pPedOnFoot->stamp)) return;

    CPed* pPed = pNetworkPed->m_pPed;
    if (!pPed)
    {
        return;
    }

    if (pNetworkPed->m_replicaDeath) {
        pPed->SetPosn(pPedOnFoot->pos); pPed->m_nAreaCode = pPedOnFoot->area;
        pNetworkPed->ApplyReplicaHealth(0); return; // Keep the existing death task intact.
    }

    CVehicle* pVehicle = pPed->m_pVehicle;
    if (pVehicle && pPed->m_nPedFlags.bInVehicle && pVehicle->IsVTableValid())
    {
        // plugin::Command<Commands::TASK_LEAVE_CAR>(CPools::GetPedRef(ped->m_pPed),
        // CPools::GetVehicleRef(ped->m_pPed->m_pVehicle));
        // plugin::Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(CPools::GetPedRef(ped->m_pPed), packet->pos.x,
        // packet->pos.y, packet->pos.z);
        pNetworkPed->RemoveFromVehicle(pVehicle);
    }

    pNetworkPed->ApplyWeaponSnapshot(pPedOnFoot->weaponSnapshot);
    pPed->SetPosn(pPedOnFoot->pos);
    pPed->m_nAreaCode = pPedOnFoot->area;
    pPed->m_vecMoveSpeed = pPedOnFoot->velocity;

    pNetworkPed->m_fCurrentRotation = pPed->m_fCurrentRotation = pPedOnFoot->currentRotation.m_angle;
    pNetworkPed->m_fAimingRotation = pPed->m_fAimingRotation = pPedOnFoot->aimingRotation.m_angle;
    pNetworkPed->m_fLookDirection = pPed->m_fLookDirection = pPedOnFoot->lookDirection.m_angle;

    pNetworkPed->ApplyReplicaHealth(pPedOnFoot->healthSnapshot.iHealth);
    pPed->m_fArmour = pPedOnFoot->healthSnapshot.iArmour;

    pNetworkPed->m_vecVelocity = pPedOnFoot->velocity;
    pNetworkPed->m_nMoveState = pNetworkPed->m_replicaDeath ? PEDMOVE_STILL : pPedOnFoot->moveState;
    if (!pNetworkPed->m_replicaDeath) {
        pPed->SetMoveState(pPedOnFoot->moveState); pPed->SetMoveAnim();
    }

    if (CUtil::IsDucked(pPed) != pPedOnFoot->bDucked)
    {
        CTaskSimpleDuckToggle task = CTaskSimpleDuckToggle(pPedOnFoot->bDucked);
        task.ProcessPed(pPed);
    }

    // TODO reimplement ped aim sync
    if (pPedOnFoot->bAiming && !pNetworkPed->m_replicaDeath)
    {
        CTaskSimpleUseGun* useGun = pPed->m_pIntelligence->GetTaskUseGun();
        if (!useGun)
        {
            auto* taskUseGun = new CTaskSimpleUseGun(pPed->m_pTargetedObject, CVector(0.0f, 0.0f, 0.f), 1, 1, false);
            pPed->m_pIntelligence->m_TaskMgr.SetTaskSecondary(taskUseGun, TASK_SECONDARY_ATTACK);
        }

        useGun = pPed->m_pIntelligence->GetTaskUseGun();

        if (useGun)
        {
            useGun->m_vecTarget = pPedOnFoot->weaponAim;
        }
    }
    else
    {
        if (auto useGun = pPed->m_pIntelligence->GetTaskUseGun())
        {
            useGun->MakeAbortable(pPed, ABORT_PRIORITY_URGENT, nullptr);
        }
    }

    pPed->m_nFightingStyle = pPedOnFoot->fightingStyle;
}

PACKET_HANDLER(ePacketType::PED_DRIVER_UPDATE, Packets::Peds::PedDriverUpdate* pPedDriverUpdate)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPedDriverUpdate->vehicleid);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }

    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
    if (pVehicle == nullptr || !pVehicle->IsVTableValid() || !pVehicle->m_matrix)
    {
        return;
    }

    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedDriverUpdate->pedid);
    if (pNetworkPed == nullptr)
    {
        return;
    }

    CPed* pPed = pNetworkPed->m_pPed;
    if (pPed == nullptr || !pPed->IsVTableValid())
    {
        return;
    }

    // Ped ownership drives this stream; the idle vehicle syncer may be local.
    // Neither a newly local NPC nor a player already in the driver seat may be
    // displaced by an older unreliable NPC snapshot.
    if ((pNetworkPed->m_bSyncing && !pNetworkPed->m_bAllowReplay) ||
        !pPedDriverUpdate->Valid() || (pVehicle->m_pDriver && pVehicle->m_pDriver->IsPlayer()) ||
        !pNetworkPed->AcceptState(pPedDriverUpdate->stamp))
        return;

    if (!pNetworkPed->m_replicaDeath && (pPed->m_pVehicle != pVehicle || !pPed->m_nPedFlags.bInVehicle))
    {
        pNetworkPed->WarpIntoVehicleDriver(pVehicle);
    }
    pPed->m_nAreaCode = pPedDriverUpdate->area;
    pVehicle->m_nAreaCode = pPedDriverUpdate->area;
    pVehicle->m_matrix->pos = pPedDriverUpdate->pos;
    pVehicle->m_matrix->right = pPedDriverUpdate->roll;
    pVehicle->m_matrix->up = pPedDriverUpdate->rot;
    pNetworkPed->m_vecVelocity = pPedDriverUpdate->velocity;
    pVehicle->m_vecMoveSpeed = pPedDriverUpdate->velocity;
    pVehicle->m_vecTurnSpeed = pPedDriverUpdate->turnSpeed;

    pNetworkPed->ApplyWeaponSnapshot(pPedDriverUpdate->pedWeapon);

    pNetworkPed->ApplyReplicaHealth(pPedDriverUpdate->pedHealth.iHealth);
    pPed->m_fArmour = pPedDriverUpdate->pedHealth.iArmour;

    pVehicle->m_nPrimaryColor = pPedDriverUpdate->color1;
    pVehicle->m_nSecondaryColor = pPedDriverUpdate->color2;

    pVehicle->m_fHealth = pPedDriverUpdate->health;

    if (pNetworkVehicle->m_nPaintJob != pPedDriverUpdate->paintjob)
    {
        pVehicle->SetRemap(pPedDriverUpdate->paintjob);
    }

    if (pVehicle->m_nVehicleType == VEHICLE_BIKE)
    {
        CBike* pBike = (CBike*)pVehicle;
        pBike->m_rideAnimData.m_fDesiredLeanAngle = pPedDriverUpdate->bikeLean;
    }
    if (pVehicle->m_nVehicleSubType == VEHICLE_PLANE)
    {
        CPlane* pPlane = (CPlane*)pVehicle;
        pPlane->m_fLandingGearStatus = pPedDriverUpdate->planeGearState;
    }
    if (pVehicle->m_nVehicleSubType == VEHICLE_BMX)
    {
        CBmx* pBmx = (CBmx*)pVehicle;
        pBmx->m_fControlPedaling = pPedDriverUpdate->controlPedaling;
    }
    if (pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
    {
        ((CAutomobile*)pVehicle)->m_wMiscComponentAngle = pPedDriverUpdate->miscComponentAngle;
    }
    pVehicle->m_eDoorLock = pPedDriverUpdate->locked;

    pVehicle->m_fGasPedal = pNetworkPed->m_fGasPedal = pPedDriverUpdate->gasPedal;
    pVehicle->m_fBreakPedal = pNetworkPed->m_fBreakPedal = pPedDriverUpdate->breakPedal;
    pVehicle->m_fSteerAngle = pNetworkPed->m_fSteerAngle = pPedDriverUpdate->steerAngle;
    pVehicle->m_nVehicleFlags.bEngineOn = pPedDriverUpdate->engineState;
    pVehicle->m_nVehicleFlags.bLightsOn = pPedDriverUpdate->lightState;
    pVehicle->m_nVehicleFlags.bEngineBroken = pPedDriverUpdate->engineBroken;
    pVehicle->m_nVehicleFlags.bSirenOrAlarm = pPedDriverUpdate->sirenOrAlarm;
    pVehicle->m_nAlarmState = pPedDriverUpdate->alarmState;
    pVehicle->m_fDirtLevel = pPedDriverUpdate->dirtLevel;
}

PACKET_HANDLER(ePacketType::PED_PASSENGER_UPDATE, Packets::Peds::PedPassengerSync* pPedPassengerSync)
{
    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPedPassengerSync->vehicleid);
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedPassengerSync->pedid);

    if (pNetworkVehicle == nullptr || pNetworkPed == nullptr)
        return;

    if (pNetworkVehicle->m_pVehicle == nullptr)
        return;

    if (pNetworkPed->m_pPed == nullptr)
        return;

    if (!pNetworkVehicle->m_pVehicle->IsVTableValid() || !pNetworkPed->m_pPed->IsVTableValid())
        return;

    if (!pPedPassengerSync->Valid() || (pNetworkPed->m_bSyncing && !pNetworkPed->m_bAllowReplay) ||
        pPedPassengerSync->seatid >= pNetworkVehicle->m_pVehicle->m_nMaxPassengers ||
        (pNetworkVehicle->m_pVehicle->m_apPassengers[pPedPassengerSync->seatid] &&
         pNetworkVehicle->m_pVehicle->m_apPassengers[pPedPassengerSync->seatid] != pNetworkPed->m_pPed) ||
        !pNetworkPed->CanAcceptState(pPedPassengerSync->stamp)) return;
    if (pNetworkPed->m_replicaDeath) {
        if (pNetworkPed->AcceptState(pPedPassengerSync->stamp)) pNetworkPed->ApplyReplicaHealth(0);
        return;
    }

    if (!pNetworkPed->m_pPed->m_nPedFlags.bInVehicle ||
        pNetworkPed->m_pPed->m_pVehicle != pNetworkVehicle->m_pVehicle ||
        pNetworkVehicle->m_pVehicle->m_apPassengers[pPedPassengerSync->seatid] != pNetworkPed->m_pPed)
    {
        pNetworkPed->WarpIntoVehiclePassenger(pNetworkVehicle->m_pVehicle, pPedPassengerSync->seatid);
    }

    // Native task changes can fail or invalidate a binding. Consume the state
    // sequence only after the authoritative vehicle and seat actually match.
    pNetworkPed = CNetworkPedManager::GetPed(pPedPassengerSync->pedid);
    pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPedPassengerSync->vehicleid);
    if (!pNetworkPed || !pNetworkVehicle || !pNetworkVehicle->m_pVehicle ||
        !pNetworkPed->m_pPed->m_nPedFlags.bInVehicle ||
        pNetworkPed->m_pPed->m_pVehicle != pNetworkVehicle->m_pVehicle ||
        pNetworkVehicle->m_pVehicle->m_apPassengers[pPedPassengerSync->seatid] != pNetworkPed->m_pPed ||
        !pNetworkPed->AcceptState(pPedPassengerSync->stamp)) return;
    pNetworkPed->m_pPed->m_nAreaCode = pPedPassengerSync->area;

    pNetworkPed->ApplyWeaponSnapshot(pPedPassengerSync->weaponSnapshot);

    pNetworkPed->ApplyReplicaHealth(pPedPassengerSync->healthSnapshot.iHealth);
    pNetworkPed->m_pPed->m_fArmour = pPedPassengerSync->healthSnapshot.iArmour;
}

PACKET_HANDLER(ePacketType::PED_SHOT_SYNC, Packets::Peds::PedShotSync* pPedShotSync)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedShotSync->pedid);

    if (pNetworkPed && pNetworkPed->HasValidPed() && !pNetworkPed->m_bSyncing && !pNetworkPed->m_replicaDeath && !pNetworkPed->m_deathStamp.State() &&
        pPedShotSync->Valid() && pPedShotSync->stamp.SameOwner(pNetworkPed->GetStamp()))
    {
        if (pNetworkPed->m_pPed->GetWeapon().m_eWeaponType != pPedShotSync->weaponType)
        {
            pNetworkPed->m_pPed->SetCurrentWeapon(pPedShotSync->weaponType);
        }
        pNetworkPed->m_pPed->GetWeapon().Fire(
            pNetworkPed->m_pPed, &pPedShotSync->origin, &pPedShotSync->effect, nullptr, &pPedShotSync->target, nullptr);
    }
}

PACKET_HANDLER(ePacketType::PED_SAY, Packets::Peds::PedSay* pPedSay)
{
    // CChat::AddMessage("PedSay %d %d %d %d %d", pPedSay->phraseId, pPedSay->startTimeDelay, pPedSay->overrideSilence,
    // pPedSay->isForceAudible, pPedSay->isFrontEnd);

    CPed* pPed = (CPed*)pPedSay->entity.GetEntity();
    if (pPed)
    {
        // CAEPedSpeechAudioEntity::AddSayEvent
        plugin::CallMethodAndReturn<int16_t, 0x4E6550, CAEPedSpeechAudioEntity*, int, int16_t, uint32_t, float, bool,
            bool, bool>(&pPed->m_pedSpeech, AE_SPEECH_PED, pPedSay->phraseId, pPedSay->startTimeDelay, 1.0f,
            pPedSay->overrideSilence, pPedSay->isForceAudible, pPedSay->isFrontEnd);
    }
}


PACKET_HANDLER(ePacketType::PED_RESET_ALL_CLAIMS, Packets::Peds::PedResetAllClaims* pPedResetAllClaims)
{
    if (auto pNetworkPed = CNetworkPedManager::GetPed(pPedResetAllClaims->pedid))
    {
        if (!pNetworkPed->m_bSyncing && pPedResetAllClaims->stamp.SameOwner(pNetworkPed->GetStamp()))
        {
            pNetworkPed->m_bClaimOnRelease = false;
        }
    }
}

PACKET_HANDLER(ePacketType::PED_REPLAY, Packets::Peds::PedReplay* packet)
{
    if (packet->mode < 1 || packet->mode > 3) return;
    auto& state = packet->StatePacket();
    int id = packet->mode == 1 ? packet->onFoot.pedid : packet->mode == 2 ? packet->driver.pedid : packet->passenger.pedid;
    const auto stamp = packet->mode == 1 ? packet->onFoot.stamp : packet->mode == 2 ? packet->driver.stamp : packet->passenger.stamp;
    auto* ped = CNetworkPedManager::GetPed(id);
    const int vehicle = packet->mode == 2 ? packet->driver.vehicleid : packet->mode == 3 ? packet->passenger.vehicleid : -1;
    auto* nativeVehicle = vehicle >= 0 ? CNetworkVehicleManager::GetVehicle(vehicle) : nullptr;
    const bool missingVehicle = vehicle >= 0 && (!nativeVehicle || !nativeVehicle->m_pVehicle || !nativeVehicle->m_pVehicle->m_matrix);
    if (CNetworkPedManager::Defer(*packet, id, stamp, !ped || missingVehicle)) return;
    ped->m_bAllowReplay = true;
    GetPacketHandler().ProcessPacket(&state);
    // Native state operations can destroy/recreate a pool entry; do not retain the wrapper across them.
    if (auto* current = CNetworkPedManager::GetPed(id)) {
        auto* currentVehicle = vehicle >= 0 ? CNetworkVehicleManager::GetVehicle(vehicle) : nullptr;
        const bool retryPassenger = packet->mode == 3 && packet->passenger.Valid() && current->CanAcceptState(stamp) &&
            (!currentVehicle || !currentVehicle->m_pVehicle || !current->m_pPed->m_nPedFlags.bInVehicle ||
                current->m_pPed->m_pVehicle != currentVehicle->m_pVehicle ||
                currentVehicle->m_pVehicle->m_apPassengers[packet->passenger.seatid] != current->m_pPed);
        current->m_bAllowReplay = false;
        if (retryPassenger) CNetworkPedManager::Defer(*packet, id, stamp, true);
    }
}
PACKET_HANDLER(ePacketType::PED_PIN, Packets::Peds::PedPin* packet)
{
    if (CNetworkPedManager::Defer(*packet, packet->pedid, packet->stamp, !CNetworkPedManager::GetPed(packet->pedid))) return;
    if (auto* ped = CNetworkPedManager::GetPed(packet->pedid))
        if (packet->stamp.SameOwner(ped->GetStamp())) { ped->m_bPinned = packet->pinned; ped->m_bClaimOnRelease = false; }
}

PACKET_HANDLER(ePacketType::PED_DEATH, Packets::Peds::PedDeath* packet)
{
    auto* ped = CNetworkPedManager::GetPed(packet->pedid);
    if (!packet->Valid() || CNetworkPedManager::Defer(*packet, packet->pedid, packet->stamp, !ped)) return;
    if (!ped || packet->stamp.generation != ped->m_generation) return;
    if (packet->stamp.epoch > ped->m_ownerEpoch) { CNetworkPedManager::Defer(*packet, packet->pedid, packet->stamp, true); return; }
    if (ped->m_deathStamp.State()) return;
    ped->m_deathStamp = packet->stamp;
    ped->m_stateSequence = (std::max)(ped->m_stateSequence, packet->stamp.sequence);
    if (!ped->HasValidPed()) return;
    ped->m_pPed->SetPosn(packet->position); ped->m_pPed->m_nAreaCode = packet->area;
    ped->ApplyReplicaHealth(0);
}
