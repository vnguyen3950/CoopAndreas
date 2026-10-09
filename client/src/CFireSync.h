#pragma once
#include "network/packets/fires.h"
class CFire;
class CPed;
class CEntity;
class CFireSync {
public:
    static void Init();
    static void Process();
    static void Reset();
    static void HostChanged(int host);
    static void Queue(Packet& packet);
    static void VehicleRemoved(int id);
    static void Receive(const Packets::Fires::Reset& packet);
    static void Receive(const Packets::Fires::Update& packet);
    static void Receive(const Packets::Fires::Remove& packet);
    static void Receive(const Packets::Fires::Bind& packet);
    static void Receive(const Packets::Fires::Request& packet);
    static bool Ready();
    static bool IsHost();
    static bool Replaying();
    static void BeginReplay();
    static void EndReplay();
    static bool NativeStart(CEntity* creator, CEntity* target, const CVector& position);
    static bool NativeWater(const CVector& position, float radius, float strength);
    static bool NativeExtinguish(CFire* fire);
    static void Created();
    static void Tick(CFire* fire);
    static bool AllowPedDamage(CPed* ped);
    static void NativeInit();
    static void EnableNative();
    static void OriginalProcess(CFire* fire);
    static void OriginalExtinguish(CFire* fire);
    static void ExecuteRequest(const FireSync::Request& request);
};
