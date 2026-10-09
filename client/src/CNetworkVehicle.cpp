#include "stdafx.h"
#include "CFireSync.h"
#include "CTrailerSync.h"
#include "CNetworkVehicle.h"
#include <new>

CNetworkVehicle::CNetworkVehicle(int vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2, unsigned char createdBy,uint32_t generation)
{
    if (auto vehicle = CNetworkVehicleManager::FindVehicle(vehicleid))
    {
        const bool current=vehicle->HasValidVehicle();
        vehicle->m_bPreserveBirth=vehicle->m_generation==generation || !current;
        if (current)
        {
            CTrailerSync::NativeRemoved(vehicle->m_pVehicle);
            CWorld::Remove(vehicle->m_pVehicle);
            delete vehicle->m_pVehicle;
        }
        vehicle->m_bSyncing=false;vehicle->m_pVehicle=nullptr;
        CNetworkVehicleManager::Remove(vehicle);delete vehicle;
    }

    m_nVehicleId = vehicleid;
    m_generation=generation;
    m_bSyncing = false;
    m_nTempId = 255;
    m_nModelId = modelid;
    m_nCreatedBy = createdBy;

    if (!CreateVehicle(vehicleid, modelid, pos, rotation, color1, color2))
        return;

}

bool CNetworkVehicle::CreateVehicle(int /*vehicleid*/, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2)
{
    if(modelid<MODEL_LANDSTAL || modelid>MODEL_UTILTR1 || !CPools::ms_pVehiclePool ||
       !CPools::ms_pVehiclePool->GetNoOfFreeSpaces())return false;
    unsigned char oldFlags = CStreaming::ms_aInfoForModel[modelid].m_nFlags;
    CStreaming::RequestModel(modelid, GAME_REQUIRED);
    CStreaming::LoadAllRequestedModels(false);

    if (!(oldFlags & GAME_REQUIRED))
    {
        CStreaming::SetModelIsDeletable(modelid);
        CStreaming::SetModelTxdIsDeletable(modelid);
    }

    auto* info=static_cast<CVehicleModelInfo*>(CModelInfo::ms_modelInfoPtrs[modelid]);
    if(!info || CStreaming::ms_aInfoForModel[modelid].m_nLoadState!=LOADSTATE_LOADED || info->m_nVehicleType==VEHICLE_TRAIN)return false;
    void* memory=CVehicle::operator new(sizeof(CHeli));
    if(!memory)return false;
    switch (info->m_nVehicleType)
    {
    case VEHICLE_MTRUCK:
        m_pVehicle = ::new(memory) CMonsterTruck(modelid, MISSION_VEHICLE); break;

    case VEHICLE_QUAD:
        m_pVehicle = ::new(memory) CQuadBike(modelid, MISSION_VEHICLE); break;

    case VEHICLE_HELI:
        m_pVehicle = ::new(memory) CHeli(modelid, MISSION_VEHICLE); break;

    case VEHICLE_PLANE:
        m_pVehicle = ::new(memory) CPlane(modelid, MISSION_VEHICLE); break;

    case VEHICLE_BIKE:
        m_pVehicle = ::new(memory) CBike(modelid, MISSION_VEHICLE);
        ((CBike*)m_pVehicle)->m_nDamageFlags |= 0x10; break;

    case VEHICLE_BMX:
        m_pVehicle = ::new(memory) CBmx(modelid, MISSION_VEHICLE);
        ((CBmx*)m_pVehicle)->m_nDamageFlags |= 0x10; break;

    case VEHICLE_TRAILER:
        m_pVehicle = ::new(memory) CTrailer(modelid, MISSION_VEHICLE); break;

    case VEHICLE_BOAT:
        m_pVehicle = ::new(memory) CBoat(modelid, MISSION_VEHICLE); break;

    case VEHICLE_TRAIN:
        CVehicle::operator delete(memory);return false;

    default:
        m_pVehicle = ::new(memory) CAutomobile(modelid, MISSION_VEHICLE, true); break;
    }

    if (!m_pVehicle)
        return false;

    m_pVehicle->SetPosn(pos);
    m_pVehicle->SetOrientation(0.0f, 0.0f, rotation);
    m_pVehicle->m_nStatus = 4;
    m_pVehicle->m_fDirtLevel = 0.0f;
    m_pVehicle->m_eDoorLock = DOORLOCK_UNLOCKED;
    m_pVehicle->m_nPrimaryColor = color1;
    m_pVehicle->m_nSecondaryColor = color2;
    m_nVehiclePoolRef=CPools::GetVehicleRef(m_pVehicle);m_createdScene=CTrailerSync::Scene();
    CWorld::Add(m_pVehicle);

    return true;
}

CNetworkVehicle::~CNetworkVehicle()
{
    CTrailerSync::VehicleRemoved(m_nVehicleId,m_generation);
    if(!m_bPreserveBirth)CFireSync::VehicleRemoved(m_nVehicleId);
    if (m_bSyncing)
    {
        if (!m_generation) return;
        Packets::Vehicles::VehicleRemove vehicleRemovePacket{};
        vehicleRemovePacket.vehicleid = m_nVehicleId;
        vehicleRemovePacket.generation=m_generation;
        GetPacketFactory().Send(vehicleRemovePacket);
    }
    else
    {
        if (CTrailerSync::NativeValid(this))
        {
            if (m_nBlipHandle != -1)
            {
                CRadar::ClearBlipForEntity(eBlipType::BLIP_CAR, CPools::GetVehicleRef(m_pVehicle));
            }
            CTrailerSync::NativeRemoved(m_pVehicle);
            CWorld::Remove(m_pVehicle);
            CWorld::RemoveReferencesToDeletedObject(m_pVehicle); // ?
            delete m_pVehicle;
        }
    }
}

bool CNetworkVehicle::HasDriver()
{
    if (!HasValidVehicle())
        return false;

    return m_pVehicle->m_pDriver != nullptr;
}

CNetworkVehicle* CNetworkVehicle::CreateHosted(CVehicle* vehicle)
{
    static uint32_t nextRequest=0;
    if(nextRequest==TrailerSync::MaxCounter)return nullptr;
    vehicle->m_nTimeTillWeNeedThisCar += 5000;

    CNetworkVehicle* networkVehicle = new CNetworkVehicle();
    networkVehicle->m_pVehicle = vehicle;
    networkVehicle->m_requestToken=++nextRequest;networkVehicle->m_createdScene=CTrailerSync::Scene();
    networkVehicle->m_nVehiclePoolRef=CPools::GetVehicleRef(vehicle);
    networkVehicle->m_nVehicleId = -1;
    networkVehicle->m_bSyncing = true;
    networkVehicle->m_nModelId = vehicle->m_nModelIndex;
    networkVehicle->m_nPaintJob = (char)vehicle->GetRemapIndex();
    networkVehicle->m_nTempId = CNetworkVehicleManager::AddToTempList(networkVehicle);
    if(networkVehicle->m_nTempId==255){networkVehicle->m_pVehicle=nullptr;delete networkVehicle;return nullptr;}
    networkVehicle->m_nCreatedBy = vehicle->m_nCreatedBy;

    Packets::Vehicles::VehicleSpawn vehicleSpawnPacket{};
    vehicleSpawnPacket.vehicleid = 0;
    vehicleSpawnPacket.requestToken=networkVehicle->m_requestToken;
    vehicleSpawnPacket.tempid = networkVehicle->m_nTempId;
    vehicleSpawnPacket.modelid = vehicle->m_nModelIndex;
    vehicleSpawnPacket.pos = vehicle->m_matrix->pos;
    vehicleSpawnPacket.rot = vehicle->GetHeading();
    vehicleSpawnPacket.color1 = vehicle->m_nPrimaryColor;
    vehicleSpawnPacket.color2 = vehicle->m_nSecondaryColor;
    vehicleSpawnPacket.createdBy = (eVehicleCreatedBy)vehicle->m_nCreatedBy;
    GetPacketFactory().Send(vehicleSpawnPacket);

    return networkVehicle;
}
bool CNetworkVehicle::HasValidVehicle() const {return CTrailerSync::NativeValid(this);}
