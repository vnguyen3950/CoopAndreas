#pragma once
#include "network/packets/trailers.h"
class CNetworkPlayer;class CNetworkVehicle;class CNetworkPed;
class CTrailerSync {
public:
 static uint32_t AllocateGeneration();
 static void Join(CNetworkPlayer*);static void Leave(CNetworkPlayer*);
 static void Changed(CNetworkVehicle*,bool removed=false);static void NpcDriver(CNetworkVehicle*,CNetworkPed*);
 static bool LegacyAllowed(CNetworkVehicle*);
 static void Hello(const Packets::Trailers::Hello&,CNetworkPlayer*);
 static void Link(Packets::Trailers::Link&,CNetworkPlayer*);
 static void Pose(Packets::Trailers::Pose&,CNetworkPlayer*);
};
