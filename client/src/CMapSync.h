#pragma once
#include "network/packets/map.h"
namespace Packets::Players { class PlayerPlaceWaypoint; }
class CMapSync
{
public:
    static void Init();
    static void Reset();
    static void Process();
    static void Receive(const Packets::Map::Discovery& packet);
    static void HostChanged(int host);
    static void SetWaypoint(bool place, float x = 0, float y = 0);
    static void ReceiveWaypoint(const Packets::Players::PlayerPlaceWaypoint& packet);
};
