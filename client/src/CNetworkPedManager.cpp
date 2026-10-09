#include "stdafx.h"
#include "CNetworkVehicle.h"
#include "CNetworkPed.h"
#include <memory>
namespace {
bool initializedScripts = false;
struct DeferredNPC { int id; NPCSync::Stamp stamp; std::unique_ptr<Packet> packet; };
std::vector<DeferredNPC>& Deferred() { static std::vector<DeferredNPC> packets; return packets; }
}

std::vector<CNetworkPed*> CNetworkPedManager::m_pPeds;
CNetworkPed* CNetworkPedManager::m_apTempPeds[255];
std::array<uint32_t, 255> CNetworkPedManager::m_generations{};
std::array<bool, 255> CNetworkPedManager::m_removed{};
std::atomic_bool CNetworkPedManager::m_resetPending{false};

CNetworkPed* CNetworkPedManager::GetPed(int pedid)
{
    for (int i = 0; i != m_pPeds.size(); i++)
    {
        if (m_pPeds[i]->m_nPedId == pedid && m_pPeds[i]->HasValidPed())
        {
            return m_pPeds[i];
        }
    }
    return nullptr;
}

CNetworkPed* CNetworkPedManager::GetPed(CEntity* entity)
{
    if (entity == nullptr)
        return nullptr;

    for (int i = 0; i != m_pPeds.size(); i++)
    {
        if (m_pPeds[i]->m_pPed == entity && m_pPeds[i]->HasValidPed())
        {
            return m_pPeds[i];
        }
    }

    return nullptr;
}

bool CNetworkPedManager::IsPedTracked(CPed* pPed)
{
    if (!pPed)
        return false;

    for (CNetworkPed* pNetworkPed : m_pPeds)
    {
        if (pNetworkPed && pNetworkPed->m_pPed == pPed && pNetworkPed->HasValidPed())
            return true;
    }

    for (CNetworkPed* pNetworkPed : m_apTempPeds)
    {
        if (pNetworkPed && pNetworkPed->m_pPed == pPed && pNetworkPed->HasValidPed())
            return true;
    }

    return false;
}

void CNetworkPedManager::Add(CNetworkPed* ped)
{
    CNetworkPedManager::m_pPeds.push_back(ped);
}

void CNetworkPedManager::Remove(CNetworkPed* ped)
{
    auto it = std::find(m_pPeds.begin(), m_pPeds.end(), ped);
    if (it != m_pPeds.end())
    {
        m_pPeds.erase(it);
    }
}

void CNetworkPedManager::HandlePedDestruction(CPed* pPed)
{
    if (!pPed)
        return;

    // At the native destructor boundary, every wrapper retaining this address is stale,
    // including wrappers from earlier pool generations.
    for (auto it = m_pPeds.begin(); it != m_pPeds.end();)
    {
        CNetworkPed* pNetworkPed = *it;
        if (!pNetworkPed || pNetworkPed->m_pPed != pPed)
        {
            ++it;
            continue;
        }

        pNetworkPed->CancelClaim();
        pNetworkPed->DetachPed();
        it = m_pPeds.erase(it);
        delete pNetworkPed;
    }

    for (auto*& pNetworkPed : m_apTempPeds)
    {
        if (!pNetworkPed || pNetworkPed->m_pPed != pPed)
            continue;

        auto* old = pNetworkPed; pNetworkPed = nullptr;
        old->DetachPed(); old->m_nPedId = -1; delete old;
    }
}

void CNetworkPedManager::Update()
{
    CNetworkPedManager::RemoveInvalidPeds();

    for (CNetworkPed* pNetworkPed : m_pPeds)
    {
        if (!pNetworkPed->m_bSyncing || !pNetworkPed->GetStamp().Lifetime() || !pNetworkPed->HasValidPed())
            continue;

        CPed* pPed = pNetworkPed->m_pPed;
        if (!pPed)
            continue;

        CVehicle* pVehicle = pPed->m_pVehicle;
        CNetworkVehicle* pNetworkVehicle = pVehicle ? CNetworkVehicleManager::GetVehicle(pVehicle) : nullptr;

        if (pNetworkVehicle && pPed->m_nPedFlags.bInVehicle)
        {
            if (!pVehicle->IsVTableValid() || !pVehicle->m_matrix
                || pNetworkPed->m_nPedId < 0 || pNetworkVehicle->m_nVehicleId < 0)
                continue;
            bool isDriver = (pVehicle->m_pDriver == pPed);

            if (isDriver)
            {
                Packets::Peds::PedDriverUpdate packet{};

                packet.vehicleSubType = static_cast<eVehicleType>(pVehicle->m_nVehicleSubType);

                packet.pedid = pNetworkPed->m_nPedId;
                if (!pNetworkPed->NextState(packet.stamp)) continue;
                packet.area = pPed->m_nAreaCode;
                packet.vehicleid = pNetworkVehicle->m_nVehicleId;

                packet.pos = pVehicle->m_matrix->pos;
                packet.roll = pVehicle->m_matrix->right;
                packet.rot = pVehicle->m_matrix->up;
                packet.velocity = pVehicle->m_vecMoveSpeed;
                packet.turnSpeed = pVehicle->m_vecTurnSpeed;

                packet.pedHealth.iHealth = static_cast<uint8_t>(std::clamp(pPed->m_fHealth, 0.0f, 255.0f));
                packet.pedHealth.iArmour = static_cast<uint8_t>(std::clamp(pPed->m_fArmour, 0.0f, 255.0f));

                CWeapon& weapon = pPed->GetWeapon();
                packet.pedWeapon.iWeaponType = weapon.m_eWeaponType;
                packet.pedWeapon.iWeaponState = weapon.m_nState;
                packet.pedWeapon.nAmmo = weapon.m_nAmmoInClip;

                packet.color1 = pVehicle->m_nPrimaryColor;
                packet.color2 = pVehicle->m_nSecondaryColor;

                packet.health = pVehicle->m_fHealth;
                packet.engineState = pVehicle->m_nVehicleFlags.bEngineOn;
                packet.lightState = pVehicle->m_nVehicleFlags.bLightsOn;
                packet.engineBroken = pVehicle->m_nVehicleFlags.bEngineBroken;
                packet.sirenOrAlarm = pVehicle->m_nVehicleFlags.bSirenOrAlarm;
                packet.alarmState = pVehicle->m_nAlarmState;
                packet.dirtLevel = pVehicle->m_fDirtLevel;

                packet.paintjob = pVehicle->GetRemapIndex();

                if (pVehicle->m_nVehicleType == VEHICLE_BIKE)
                {
                    CBike* pBike = (CBike*)pVehicle;
                    packet.bikeLean = pBike->m_rideAnimData.m_fDesiredLeanAngle;
                }
                if (pVehicle->m_nVehicleSubType == VEHICLE_PLANE)
                {
                    CPlane* pPlane = (CPlane*)pVehicle;
                    packet.planeGearState = pPlane->m_fLandingGearStatus;
                }
                if (pVehicle->m_nVehicleSubType == eVehicleType::VEHICLE_BMX)
                {
                    CBmx* pBmx = (CBmx*)pVehicle;
                    packet.controlPedaling = pBmx->m_fControlPedaling;
                }
                if (pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
                {
                    CAutomobile* pAutomobile = (CAutomobile*)pVehicle;
                    packet.bikeLean = pAutomobile->m_wMiscComponentAngle;
                }
                packet.locked = pVehicle->m_eDoorLock;
                packet.gasPedal = pVehicle->m_fGasPedal;
                packet.breakPedal = pVehicle->m_fBreakPedal;
                packet.steerAngle = pVehicle->m_fSteerAngle;

                GetPacketFactory().Send(packet);
            }
            else
            {
                Packets::Peds::PedPassengerSync packet{};
                packet.pedid = pNetworkPed->m_nPedId;
                if (!pNetworkPed->NextState(packet.stamp)) continue;
                packet.area = pPed->m_nAreaCode;
                packet.vehicleid = pNetworkVehicle->m_nVehicleId;

                packet.healthSnapshot.iHealth = static_cast<uint8_t>(std::clamp(pPed->m_fHealth, 0.0f, 255.0f));
                packet.healthSnapshot.iArmour = static_cast<uint8_t>(std::clamp(pPed->m_fArmour, 0.0f, 255.0f));

                CWeapon& weapon = pPed->GetWeapon();
                packet.weaponSnapshot.iWeaponType = weapon.m_eWeaponType;
                packet.weaponSnapshot.iWeaponState = weapon.m_nState;
                packet.weaponSnapshot.nAmmo = weapon.m_nAmmoInClip;

                for (int i = 0; i < pNetworkVehicle->m_pVehicle->m_nMaxPassengers; i++)
                {
                    if (pNetworkVehicle->m_pVehicle->m_apPassengers[i] == pNetworkPed->m_pPed)
                    {
                        packet.seatid = i;
                        break;
                    }
                }
                GetPacketFactory().Send(packet);
            }
        }
        else
        {
            Packets::Peds::PedOnFoot packet{};

            packet.pedid = pNetworkPed->m_nPedId;
                if (!pNetworkPed->NextState(packet.stamp)) continue;
                packet.area = pPed->m_nAreaCode;
            packet.pos = pPed->GetPosition();
            packet.velocity = pPed->m_vecMoveSpeed;

            packet.healthSnapshot.iHealth = static_cast<uint8_t>(std::clamp(pPed->m_fHealth, 0.0f, 255.0f));
            packet.healthSnapshot.iArmour = static_cast<uint8_t>(std::clamp(pPed->m_fArmour, 0.0f, 255.0f));

            CWeapon& weapon = pPed->GetWeapon();
            packet.weaponSnapshot.iWeaponType = weapon.m_eWeaponType;
            packet.weaponSnapshot.iWeaponState = weapon.m_nState;
            packet.weaponSnapshot.nAmmo = weapon.m_nAmmoInClip;

            packet.aimingRotation = pPed->m_fAimingRotation;
            packet.currentRotation = pPed->m_fCurrentRotation;
            packet.lookDirection = pPed->m_fLookDirection;
            packet.moveState = (eMoveState)pPed->m_nMoveState;

            packet.bDucked = CUtil::IsDucked(pPed);

            CTaskSimpleUseGun* useGun = pPed->m_pIntelligence->GetTaskUseGun();

            if (useGun)
            {
                packet.bAiming = true;
                packet.weaponAim = useGun->m_pTarget && (useGun->m_vecTarget.x == 0.f || useGun->m_vecTarget.y == 0.f)
                                       ? useGun->m_pTarget->GetPosition()
                                       : useGun->m_vecTarget;
            }

            packet.fightingStyle = pPed->m_nFightingStyle;

            GetPacketFactory().Send(packet);
        }
    }
}

void CNetworkPedManager::Process()
{
    ProcessPendingReset();
    ProcessPendingNative();
    for (auto networkPed : m_pPeds)
    {
        if (!networkPed->HasValidPed())
            continue;

        if (networkPed->m_bSyncing)
            continue;

        CPed* ped = networkPed->m_pPed;

        if (ped == nullptr)
            continue;

        if (!ped->m_nPedFlags.bInVehicle)
        {
            ped->m_fAimingRotation = networkPed->m_fAimingRotation;
            ped->m_fCurrentRotation = networkPed->m_fCurrentRotation;
            ped->m_fLookDirection = networkPed->m_fLookDirection;
        }
    }
}

void CNetworkPedManager::AssignHost()
{
    /*for (auto networkPed : m_pPeds)
    {
        CPed* ped = networkPed->m_pPed;

        if (ped)
        {
            ped->SetCharCreatedBy(networkPed->m_nCreatedBy);

            CTaskComplexWander* task = plugin::CallAndReturn<CTaskComplexWander*, 0x673D00>(ped); //
    GetWanderTaskByPedType ped->m_pIntelligence->m_TaskMgr.SetTask(task, 0, false);
        }
    }*/
}

unsigned char CNetworkPedManager::AddToTempList(CNetworkPed* networkPed)
{
    for (unsigned char i = 0; i < 255; i++)
    {
        if (m_apTempPeds[i] == nullptr)
        {
            m_apTempPeds[i] = networkPed;
            return i;
        }
    }

    return 255;
}

void CNetworkPedManager::RemoveInvalidPeds()
{
    for (auto it = CNetworkPedManager::m_pPeds.begin(); it != CNetworkPedManager::m_pPeds.end();)
    {
        CNetworkPed* pNetworkPed = *it;
        if (!pNetworkPed->HasValidPed())
        {
            pNetworkPed->CancelClaim();
            pNetworkPed->DetachPed();
            it = m_pPeds.erase(it);
            delete pNetworkPed;
            continue;
        }
        ++it;
    }

    for (auto*& pNetworkPed : m_apTempPeds)
    {
        if (!pNetworkPed || pNetworkPed->HasValidPed())
            continue;

        auto* old = pNetworkPed; pNetworkPed = nullptr;
        old->DetachPed(); old->m_nPedId = -1; delete old;
    }
}

bool CNetworkPedManager::AcceptSpawn(int id, const NPCSync::Stamp& stamp)
{
    if (id < 0 || id >= Config::MAX_SERVER_PEDS || !stamp.Lifetime()) return false;
    if (stamp.generation < m_generations[id] || (stamp.generation == m_generations[id] && m_removed[id])) return false;
    if (auto* existing = GetPed(id)) {
        if (existing->m_generation >= stamp.generation) return false;
        Remove(existing); existing->m_bSyncing = false; delete existing;
    }
    m_generations[id] = stamp.generation; m_removed[id] = false;
    return true;
}
bool CNetworkPedManager::AcceptRemoval(int id, const NPCSync::Stamp& stamp)
{
    if (id < 0 || id >= Config::MAX_SERVER_PEDS || !stamp.Lifetime() || stamp.generation < m_generations[id]) return false;
    if (auto* existing = GetPed(id))
        if (stamp.generation == existing->m_generation && !stamp.SameOwner(existing->GetStamp())) return false;
    m_generations[id] = stamp.generation; m_removed[id] = true;
    return true;
}
bool CNetworkPedManager::PinGangWarPedToHost(CPed* ped, bool pinned)
{
    if (!CNetwork::m_bAuthenticated || !CLocalPlayer::m_bIsHost || !ped) return false;
    auto* networkPed = GetPed(ped);
    if (!networkPed) for (auto* pending : m_apTempPeds)
        if (pending && pending->m_pPed == ped && pending->HasValidPed()) { networkPed = pending; break; }
    if (!networkPed) return false;
    networkPed->m_bPinned = pinned;
    Packets::Peds::PedPin packet;
    packet.pedid = networkPed->m_generation ? networkPed->m_nPedId : 0;
    if (!networkPed->m_generation) packet.requestToken = networkPed->m_requestToken;
    packet.stamp = networkPed->GetStamp(); packet.pinned = pinned;
    GetPacketFactory().Send(packet);
    return true;
}
void CNetworkPedManager::RequestReset() { m_resetPending.store(true, std::memory_order_release); }
void CNetworkPedManager::ProcessPendingReset() { if (m_resetPending.load(std::memory_order_acquire)) Clear(); }
void CNetworkPedManager::Clear()
{
    m_resetPending.exchange(false, std::memory_order_acq_rel);
    auto previous = std::move(m_pPeds); m_pPeds.clear();
    for (auto* ped : previous) { ped->m_nPedId = -1; delete ped; }
    for (auto*& ped : m_apTempPeds) { auto* old = ped; ped = nullptr; if (old) { old->m_nPedId = -1; delete old; } }
    m_generations = {}; m_removed = {};
    Deferred().clear();
}

void CNetworkPedManager::Init()
{
    Events::initScriptsEvent.before += [] { initializedScripts = false; };
    Events::processScriptsEvent.after += [] { if (gGameState == 9) initializedScripts = true; };
    gameShutdownEvent.before += [] { initializedScripts = false; RequestReset(); };
}
bool CNetworkPedManager::NativeReady()
{
    auto* local = CWorld::Players[0].m_pPed;
    return initializedScripts && gGameState == 9 && CWorld::PlayerInFocus == 0 && CPools::ms_pPedPool &&
        local && CPools::ms_pPedPool->IsObjectValid(local) && local->m_pPlayerData;
}
bool CNetworkPedManager::Defer(Packet& packet, int id, const NPCSync::Stamp& stamp, bool force)
{
    if (!force && NativeReady()) return false;
    if (id < 0 || id >= Config::MAX_SERVER_PEDS || !stamp.Lifetime()) return true;
    const auto type = packet.GetType();
    if (type != ePacketType::PED_REMOVE && (stamp.generation < m_generations[id] ||
        (stamp.generation == m_generations[id] && m_removed[id]))) return true;
    auto& queue = Deferred();
    for (const auto& old : queue) if (old.id == id && (old.stamp.generation > stamp.generation ||
        (old.stamp.generation == stamp.generation && (old.packet->GetType() == ePacketType::PED_REMOVE ||
            ((old.packet->GetType() == type || type == ePacketType::PED_REMOVE) &&
                (old.stamp.epoch > stamp.epoch || (old.stamp.epoch == stamp.epoch && old.stamp.sequence > stamp.sequence))))))) return true;
    if (type == ePacketType::PED_REMOVE && !AcceptRemoval(id, stamp)) return true;
    queue.erase(std::remove_if(queue.begin(), queue.end(), [&](const auto& old) {
        return old.id == id && (old.stamp.generation < stamp.generation ||
            (old.stamp.generation == stamp.generation && (type == ePacketType::PED_REMOVE || old.packet->GetType() == type)));
    }), queue.end());
    // At most one of five reliable lifecycle/replay types per slot. Dormant
    // menu/model/pool identities have no auth-time expiry and cannot grow unbounded.
    if (type != ePacketType::PED_REMOVE || GetPed(id)) queue.push_back({id, stamp, std::unique_ptr<Packet>(packet.Clone())});
    return true;
}
void CNetworkPedManager::ProcessPendingNative()
{
    if (!CNetwork::m_bAuthenticated || !NativeReady()) return;
    static uint32_t lastRetry = 0;
    const uint32_t now = GetTickCount();
    if (now - lastRetry < 250) return;
    lastRetry = now;
    const size_t budget = (std::min)(Deferred().size(), size_t(4));
    for (size_t i = 0; i < budget && !Deferred().empty(); ++i) {
        auto next = std::move(Deferred().front()); Deferred().erase(Deferred().begin());
        GetPacketHandler().ProcessPacket(next.packet.get());
    }
}
