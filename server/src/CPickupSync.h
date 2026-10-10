#pragma once
#include "network/packets/pickups.h"
class CNetworkPlayer;
class CPickupServer {
public:
    static void Join(CNetworkPlayer* player);
    static void Leave(CNetworkPlayer* player);
    static void HostChanged(CNetworkPlayer* player);
    static void Mission(CNetworkPlayer* player,bool active);
    static void Hello(const Packets::Pickups::Hello& packet,CNetworkPlayer* sender);
    static bool Action(const Packets::Pickups::Action& packet,CNetworkPlayer* sender);
    static void ProcessPending();
};
