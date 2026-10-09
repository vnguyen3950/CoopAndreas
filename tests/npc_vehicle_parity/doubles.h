#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include "config.h"

struct Transport
{
    unsigned forwarded = 0;
    std::unique_ptr<class Packet> sent;
    template<class T> void RegisterPacket(T* p) { delete p; }
    template<class T> void Send(const T& p);
    template<class T, class Peer> void SendToAll(const T&, const Peer*) { ++forwarded; }
};
Transport& GetPacketFactory();
#include "network/packet.h"
#include "network/vehicle_authority.h"
#include "network/npc_sync.h"
inline Transport& GetPacketFactory() { static Transport transport; return transport; }
template<class T> void Transport::Send(const T& p) { sent.reset(static_cast<const Packet&>(p).Clone()); }
namespace logger { template<class... T> void warn(const char*, T...) {} }

struct CVector
{
    float x = 0, y = 0, z = 0;
    CVector() = default;
    CVector(float a, float b, float c) : x(a), y(b), z(c) {}
};
#include "extracted_packet.h"

struct Matrix { CVector pos{}, right{1,0,0}, up{0,1,0}; };
struct CVehicle;
struct CWeapon
{
    eWeaponType m_eWeaponType = WEAPON_UNARMED;
    eWeaponState m_nState = WEAPONSTATE_READY;
    uint16_t m_nAmmoInClip = 10;
};
struct CPed
{
    bool valid = true, player = false;
    struct { bool bInVehicle = false; } m_nPedFlags;
    CVehicle* m_pVehicle = nullptr;
    float m_fHealth = 100, m_fArmour = 0;
    uint8_t m_nAreaCode = 0;
    CWeapon weapon;
    bool IsVTableValid() const { return valid; }
    bool IsPlayer() const { return player; }
    CWeapon& GetWeapon() { return weapon; }
};
struct CVehicle
{
    uint8_t m_nAreaCode = 0;
    bool valid = true;
    Matrix storage;
    Matrix* m_matrix = &storage;
    CPed* m_pDriver = nullptr;
    eVehicleType m_nVehicleType = VEHICLE_AUTOMOBILE, m_nVehicleSubType = VEHICLE_AUTOMOBILE;
    CVector m_vecMoveSpeed{}, m_vecTurnSpeed{};
    uint8_t m_nPrimaryColor = 0, m_nSecondaryColor = 0;
    float m_fHealth = 1000, m_fDirtLevel = 0, m_fGasPedal = 0, m_fBreakPedal = 0, m_fSteerAngle = 0;
    struct { bool bEngineOn = false, bLightsOn = false, bEngineBroken = false, bSirenOrAlarm = false; } m_nVehicleFlags;
    uint16_t m_nAlarmState = 0;
    eDoorLock m_eDoorLock = DOORLOCK_UNLOCKED;
    bool IsVTableValid() const { return valid; }
    int GetRemapIndex() const { return 1; }
    void SetRemap(int) {}
};
struct CBike : CVehicle { struct { float m_fDesiredLeanAngle = 0; } m_rideAnimData; };
struct CBmx : CBike { float m_fControlPedaling = 0; };
struct CPlane : CVehicle { float m_fLandingGearStatus = 0; };
struct CAutomobile : CVehicle { uint16_t m_wMiscComponentAngle = 0; };
struct CNetworkPlayer { int id; std::string GetName() const { return "test peer"; } };
struct CNetworkPed
{
    uint32_t m_generation = 1, m_ownerEpoch = 1, m_stateSequence = 0;
    NPCSync::Stamp GetStamp() const { return {m_generation, m_ownerEpoch, m_stateSequence}; }
    bool m_bAllowReplay = false, m_hasState = false;
    CVector m_vecPos{};
    struct { int mode = 0; Packets::Peds::PedDriverUpdate driver; } m_lastState;
    bool HasValidPed() const { return !m_pPed || m_pPed->valid; } // Server records have no native actor; native clients are checked by caller.
    bool NextState(NPCSync::Stamp& stamp);
    bool AcceptState(const NPCSync::Stamp& stamp);
    bool CanAcceptState(const NPCSync::Stamp& stamp) const;
    int m_nPedId = 7;
    bool m_bSyncing = false;
    CPed* m_pPed = nullptr;
    CNetworkPlayer* m_pSyncer = nullptr;
    CVector m_vecVelocity{};
    float m_fHealth = 100, m_fGasPedal = 0, m_fBreakPedal = 0, m_fSteerAngle = 0;
    unsigned warps = 0;
    void ApplyWeaponSnapshot(Packets::Players::SWeaponSnapshot&) {}
    void WarpIntoVehicleDriver(CVehicle* vehicle)
    { ++warps; m_pPed->m_pVehicle = vehicle; m_pPed->m_nPedFlags.bInVehicle = true; vehicle->m_pDriver = m_pPed; }
};
struct CNetworkVehicle
{
    int m_nVehicleId = 9;
    CVehicle* m_pVehicle = nullptr;
    CNetworkPlayer* m_pSyncer = nullptr;
    CNetworkPlayer* m_pPlayers[8]{};
    bool m_bSyncing = false, m_bUsedByPed = false;
    int m_nPaintJob = 1;
    CVector m_vecPosition{}, m_vecRotation{};
};
struct CNetworkPedManager
{
    static bool Authenticated(CNetworkPlayer* player) { return player != nullptr; } // Recorded transport; real registry authentication is covered in npc_world_sync.
    static inline CNetworkPed* ped = nullptr;
    static CNetworkPed* GetPed(int id) { return ped && ped->m_nPedId == id ? ped : nullptr; }
};
struct CNetworkVehicleManager
{
    static inline CNetworkVehicle* vehicle = nullptr;
    static CNetworkVehicle* GetVehicle(int id) { return vehicle && vehicle->m_nVehicleId == id ? vehicle : nullptr; }
};
