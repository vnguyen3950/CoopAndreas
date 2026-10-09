#pragma once
#include "network/packets/vitals.h"
class CNetworkPlayer;
class CPlayerPed;
class CPlayerVitalsSync
{
public:
    static void Init();
    static void Process();
    static void Receive(const Packets::Players::Vitals& packet);
    static bool ApplyRemote(CNetworkPlayer* player);
    static bool HasBoundPed(CNetworkPlayer* player);
    static const PlayerVitals::State* GetState(const CNetworkPlayer* player);
    static float HealthPercent(CPlayerPed* ped, float health);
};
