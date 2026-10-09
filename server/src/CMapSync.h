#pragma once
#include "network/packets/map.h"
namespace Packets::Players { class PlayerPlaceWaypoint; }
class CNetworkPlayer;
class CMapSyncServer
{
public:
    static bool Receive(const Packets::Map::Discovery& packet, CNetworkPlayer* sender);
    static void Replay(CNetworkPlayer* recipient);
    static void HostChanged();
    static void Leave(CNetworkPlayer* departing);
    static bool Waypoint(Packets::Players::PlayerPlaceWaypoint packet, CNetworkPlayer* sender);
};
