#pragma once
#include "network/packets/trailers.h"
class CNetworkVehicle;class CVehicle;
namespace Packets::Vehicles {class VehicleSpawn;}
class CTrailerSync {
public:
 static void Init();static void Process();static void Reset(bool preserveBirths=false);static uint32_t Scene();
 static bool NativeValid(const CNetworkVehicle*);
 static bool GameplayReady();static bool CanSpawn(int id,uint32_t birth);static void RetireVehicle(int id,uint32_t birth);static void QueueSpawn(const Packets::Vehicles::VehicleSpawn&);static bool ConfirmValid(CNetworkVehicle*,uint32_t request);
 static void NativeRemoved(CVehicle*);static void VehicleRemoved(int id,uint32_t birth);
 static bool LegacyAllowed(CNetworkVehicle*);static bool AllowAttach(CVehicle*,CVehicle*);static bool AllowDetach(CVehicle*);
 static bool Replaying();static void EnableNative();static void NativeInit();static void Queue(Packet&);
 static void Receive(const Packets::Trailers::Lease&);static void Receive(const Packets::Trailers::Link&);static void Receive(const Packets::Trailers::Pose&);
};
