#pragma once
#include "network/packets/vitals.h"
class CNetworkPlayer;
class CPlayerVitalsServer
{
public:
    static uint32_t AllocateGeneration();
    static void Announce(CNetworkPlayer* player);
    static void Replay(CNetworkPlayer* owner, CNetworkPlayer* recipient);
    static bool Receive(Packets::Players::Vitals packet, CNetworkPlayer* sender);
};
