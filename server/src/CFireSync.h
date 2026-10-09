#pragma once
#include "network/packets/fires.h"
class CNetworkPlayer;
class CNetworkVehicle;
class CFireSync {
public:
    static void Join(CNetworkPlayer* player);
    static void Leave(CNetworkPlayer* player);
    static void HostChanged(CNetworkPlayer* player);
    static void Pose(CNetworkPlayer* player, const CVector& position);
    static void VehicleChanged(CNetworkVehicle* vehicle, bool removed = false);
    static void Hello(const Packets::Fires::Hello& packet, CNetworkPlayer* sender);
    static void Update(Packets::Fires::Update& packet, CNetworkPlayer* sender);
    static void Remove(const Packets::Fires::Remove& packet, CNetworkPlayer* sender);
    static void Request(Packets::Fires::Request& packet, CNetworkPlayer* sender);
};
