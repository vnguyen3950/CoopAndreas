#pragma once
#include "network/packets/pickups.h"
class CPickup;
class CPlayerPed;
class CVehicle;
class CPickupSync {
public:
    static void Init();
    static void Process();
    static void Reset();
    static void HostChanged(int host);
    static void Receive(const Packets::Pickups::State& packet);
    static void Receive(const Packets::Pickups::Action& packet);
    static bool Update(CPickup* pickup,CPlayerPed* player,CVehicle* vehicle,int playerId);
    static void Created(int nativeHandle,bool freshCreation=false);
    static void Removed(CPickup* pickup);
    static void NativeInit();
    static int Generate(CVector position,uint32_t model,uint8_t type,uint32_t ammo,uint32_t money,bool empty,char*message);
    static void NativeRemove(CPickup* pickup);
    static bool Replay();
    static void EnableNative();
};
