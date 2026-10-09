#pragma once
#include "network/object_sync.h"
#include "network/packets/objects.h"

class CNetworkObjectManager
{
public:
    static ObjectSync::Registry& Registry();
    static void RemoveOwner(CNetworkPlayer* player);
    static bool RemapOpcode(uint8_t* buffer, int size, CNetworkPlayer* sender);
    static void RouteHit(const Packets::Players::PlayerBulletShot& packet, CNetworkPlayer* sender);
};
