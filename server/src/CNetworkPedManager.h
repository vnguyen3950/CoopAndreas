#pragma once

#include <vector>

class CNetworkPlayer;
class CNetworkPed;

class CNetworkPedManager
{
public:
    static std::vector<CNetworkPed*> m_pPeds;
    static void Add(CNetworkPed* ped);
    static void Remove(CNetworkPed* ped);
    static CNetworkPed* GetPed(int pedid);
    static int GetFreeId();
    static void RemoveAllHostedAndNotify(CNetworkPlayer* player);
    static bool Authenticated(CNetworkPlayer* player);
    static uint32_t AllocateGeneration();
    static bool AcceptRequest(CNetworkPlayer* player, uint32_t token);
    static bool AssignOwner(CNetworkPed* ped, CNetworkPlayer* player);
    static void Replay(CNetworkPed* ped, CNetworkPlayer* recipient);
    static void DeleteAndNotify(CNetworkPed* ped, CNetworkPlayer* ignore = nullptr);
    static void ClearClaims(CNetworkPed* ped);
};
