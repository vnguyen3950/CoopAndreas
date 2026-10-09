#include "stdafx.h"
#include "CNetworkPed.h"
#include <CTaskSimpleCarSetPedInAsPassenger.h>
#include <CCarEnterExit.h>
#include <CTaskSimpleCarSetPedOut.h>
#include <Hooks/PedHooks.h>

CNetworkPed::CNetworkPed(int pedid, int modelId, ePedType pedType, CVector pos, unsigned char createdBy, char specialModelName[])
{
    m_nPedId = pedid; m_nPedType = pedType; m_nCreatedBy = createdBy;
    if (!CPools::ms_pPedPool || !CPools::ms_pPedPool->GetNoOfFreeSpaces()) return;
    if (pedType == PED_TYPE_COP && modelId != MODEL_LAPDM1 && modelId != MODEL_CSHER &&
        modelId != MODEL_SWAT && modelId != MODEL_FBI && modelId != MODEL_ARMY) return;
    if (modelId >= 290 && modelId <= 299)
        CStreaming::RequestSpecialModel(modelId, specialModelName, 0);
    else
        CStreaming::RequestModel(modelId, 0);

    CStreaming::LoadAllRequestedModels(false);
    if (!CPools::ms_pPedPool || !CModelInfo::ms_modelInfoPtrs[modelId] ||
        CModelInfo::ms_modelInfoPtrs[modelId]->GetModelType() != MODEL_INFO_PED ||
        CStreaming::ms_aInfoForModel[modelId].m_nLoadState != LOADSTATE_LOADED) return;

    if (pedType == PED_TYPE_COP)
    {
        switch (modelId) 
        {
        case MODEL_LAPDM1:
            modelId = COP_TYPE_LAPDM1;
            break;
        case MODEL_CSHER:
            modelId = COP_TYPE_CSHER;
            break;
        case MODEL_SWAT:
            modelId = COP_TYPE_SWAT1;
            break;
        case MODEL_FBI:
            modelId = COP_TYPE_FBI;
            break;
        case MODEL_ARMY:
            modelId = COP_TYPE_ARMY;
            break;
        }
    }

    if (pedType == PED_TYPE_COP)
    {
        m_pPed = new CCopPed((eCopType)modelId);
    }
    else if (pedType == PED_TYPE_MEDIC || pedType == PED_TYPE_FIREMAN)
    {
        m_pPed = new CEmergencyPed(pedType, modelId);
    }
    else
    {
        m_pPed = new CCivilianPed(pedType, modelId);
    }

    m_nPedPoolRef = CPools::GetPedRef(m_pPed);
    m_pPed->m_nCreatedBy = 2;
    m_pPed->m_pIntelligence->SetPedDecisionMakerType(-1);
    m_pPed->m_pIntelligence->SetSeeingRange(30.0);
    m_pPed->m_pIntelligence->SetHearingRange(30.0);
    m_pPed->m_pIntelligence->m_fDmRadius = 0.0f;
    m_pPed->m_pIntelligence->m_nDmNumPedsToScan = 0;
    
    m_pPed->SetPosn(pos);
    m_pPed->SetOrientation(0.f, 0.f, 0.f);
    CWorld::Add(m_pPed);

    m_nPedId = pedid;
    m_nPedType = pedType;
    m_bSyncing = false;
    m_nCreatedBy = createdBy;
}

CNetworkPed::~CNetworkPed()
{
    CPed* pPed = HasValidPed() ? m_pPed : nullptr;
    int nPedPoolRef = m_nPedPoolRef;
    DetachPed();

    if (m_bSyncing)
    {
        if (m_nPedId >= 0)
        {
            Packets::Peds::PedRemove packet{};
            packet.pedid = m_nPedId;
            packet.stamp = GetStamp();
            GetPacketFactory().Send(packet);
        }
    }
    else
    {
        // The full pool reference validates ownership; a live ped may have no
        // matrix yet, so matrix allocation must not decide whether it is deleted.
        if (pPed)
        {
            if (m_nBlipHandle != -1)
            {
                CRadar::ClearBlipForEntity(eBlipType::BLIP_CHAR, nPedPoolRef);
                //CChat::AddMessage("REMOVE THE FUCKING BLIP");
            }

            if (pPed->m_nPedFlags.bInVehicle)
            {
                plugin::Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(nPedPoolRef, 0.f, 0.f, 0.f);
            }

            CWorld::Remove(pPed);
            //CWorld::RemoveReferencesToDeletedObject(pPed);
            pPed->Remove();
            delete pPed;
        }
    }
}

bool CNetworkPed::HasValidPed() const
{
    return m_pPed && m_nPedPoolRef >= 0 && CPools::ms_pPedPool && CPools::GetPed(m_nPedPoolRef) == m_pPed;
}

void CNetworkPed::DetachPed()
{
    m_pPed = nullptr;
    m_nPedPoolRef = -1;
}

CNetworkPed* CNetworkPed::CreateHosted(CPed* pPed)
{
    // Never reuse a token, including across Clear/retry.
    if (!pPed || pPed->IsPlayer() || pPed->m_nPedType < PED_TYPE_CIVMALE || !CPools::ms_pPedPool || m_lastRequestToken == NPCSync::MaxCounter) return nullptr;
    CNetworkPed* pNetworkPed = new CNetworkPed();
    pNetworkPed->m_requestToken = ++m_lastRequestToken;

    pNetworkPed->m_pPed = pPed;
    pNetworkPed->m_nPedPoolRef = CPools::GetPedRef(pPed);
    pNetworkPed->m_nPedId = -1;
    pNetworkPed->m_nCreatedBy = pPed->m_nCreatedBy;
    pNetworkPed->m_nPedType = static_cast<ePedType>(pPed->m_nPedType);
    pNetworkPed->m_bSyncing = true;
    pNetworkPed->m_nTempId = CNetworkPedManager::AddToTempList(pNetworkPed);

    if (pNetworkPed->m_nTempId == 255)
    {
        pNetworkPed->DetachPed();
        delete pNetworkPed;
        return nullptr;
    }

    pPed->field_54C += 5000; // m_nTimeTillWeNeedThisPed

    Packets::Peds::PedSpawn packet{};
    packet.tempid = pNetworkPed->m_nTempId;
    packet.requestToken = pNetworkPed->m_requestToken;
    packet.pedid = 0; // the server assigns the real id before forwarding the spawn
    packet.modelId = static_cast<eModelID>(pPed->m_nModelIndex);
    packet.pos = pPed->GetPosition();
    packet.pedType = static_cast<ePedType>(pPed->m_nPedType);
    packet.createdBy = static_cast<eCharCreatedBy>(pPed->m_nCreatedBy);

    if (packet.modelId >= MODEL_SPECIAL01 && packet.modelId <= MODEL_SPECIAL10)
    {
        strcpy_s(packet.specialModelName, PedHooks::ms_aszLoadedSpecialModels[packet.modelId - MODEL_SPECIAL01]);
        packet.specialModelName[7] = '\0';
    }
    GetPacketFactory().Send(packet);

    return pNetworkPed;
}

void CNetworkPed::WarpIntoVehicleDriver(CVehicle* vehicle)
{
    assert(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    if (m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle)
    {
        RemoveFromVehicle(m_pPed->m_pVehicle);
    }

    m_pPed->m_pIntelligence->FlushImmediately(false);

    if (!m_bSyncing)
    {
        m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1; // 1 - never
    }

    auto task = CTaskSimpleCarSetPedInAsDriver(vehicle, nullptr);
    task.m_bWarpingInToCar = true;
    task.ProcessPed(m_pPed);
}

void CNetworkPed::WarpIntoVehiclePassenger(CVehicle* vehicle, int seatid)
{
    assert(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    if (m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle)
    {
        RemoveFromVehicle(m_pPed->m_pVehicle);
    }

    m_pPed->m_pIntelligence->FlushImmediately(false);

    if (!m_bSyncing)
    {
        m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1; // 1 - never
    }

    int doorId = CCarEnterExit::ComputeTargetDoorToEnterAsPassenger(vehicle, seatid);
    auto task = CTaskSimpleCarSetPedInAsPassenger(vehicle, doorId, nullptr);
    task.m_bWarpingInToCar = true;
    task.ProcessPed(m_pPed);
}

void CNetworkPed::RemoveFromVehicle(CVehicle* vehicle)
{
    assert(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    m_pPed->m_pIntelligence->m_TaskMgr.SetTask(nullptr, TASK_PRIMARY_PRIMARY, false);

    if (!m_bSyncing)
    {
        m_pPed->m_nPedFlags.CantBeKnockedOffBike = 2; // 2 - normal
    }

    auto task = CTaskSimpleCarSetPedOut(vehicle, 1, false);
    task.m_bWarpingOutOfCar = true;
    task.ProcessPed(m_pPed);

    m_pPed->m_pIntelligence->FlushImmediately(true); // create a default primary task (fix bug)
}

void CNetworkPed::ClaimOnRelease()
{
    if (m_bClaimOnRelease || m_bSyncing || m_bPinned || !m_generation)
        return;

    Packets::Peds::PedClaimOnRelease packet{};
    packet.pedid = m_nPedId;
    packet.stamp = GetStamp();
    GetPacketFactory().Send(packet);

    m_bClaimOnRelease = true;
}

void CNetworkPed::CancelClaim()
{
    if (!m_bClaimOnRelease || m_bSyncing)
        return;

    Packets::Peds::PedCancelClaim packet{};
    packet.pedid = m_nPedId;
    packet.stamp = GetStamp();
    GetPacketFactory().Send(packet);

    m_bClaimOnRelease = false;
}

void CNetworkPed::ApplyWeaponSnapshot(Packets::Players::SWeaponSnapshot& weaponSnapshot)
{
    if (m_pPed == nullptr)
    {
        return;
    }

    // TODO refactor CUtil
    CUtil::GiveWeaponByPacket(this, weaponSnapshot.iWeaponType, weaponSnapshot.nAmmo);
    m_pPed->m_aWeapons[m_pPed->m_nActiveWeaponSlot].m_nState = static_cast<eWeaponState>(weaponSnapshot.iWeaponState);
}

bool CNetworkPed::NextState(NPCSync::Stamp& stamp)
{
    if (!GetStamp().Lifetime() || !m_bSyncing || m_stateSequence == NPCSync::MaxCounter) return false;
    stamp = {m_generation, m_ownerEpoch, ++m_stateSequence};
    return true;
}
bool CNetworkPed::AcceptState(const NPCSync::Stamp& stamp)
{
    if (!CanAcceptState(stamp)) return false;
    m_stateSequence = stamp.sequence;
    return true;
}
bool CNetworkPed::CanAcceptState(const NPCSync::Stamp& stamp) const
{
    if (!HasValidPed() || !stamp.SameOwner(GetStamp()) || !stamp.State()) return false;
    if (stamp.sequence <= m_stateSequence && !(m_bAllowReplay && stamp.sequence == m_stateSequence)) return false;
    return true;
}
