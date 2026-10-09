#pragma once
#include <atomic>
class CNetworkPlayerManager
{
public:
    static std::vector<CNetworkPlayer*> m_pPlayers;
    static CPad m_pPads[Config::MAX_SERVER_PLAYERS + 2];
    static int m_nMyId;

    static void Add(CNetworkPlayer* player);
    static void Remove(CNetworkPlayer* player);
    static void RemoveById(int playerid); // Main/game receive path only.
    static void Reset(); // Main/game receive path only; guarded native teardown.
    static void RequestReset(); // Metadata only, safe from networking callbacks.
    static void ProcessPendingReset();
    static CNetworkPlayer* GetPlayer(int playerid);
    static CNetworkPlayer* GetPlayer(SenderPlayerId playerid);
    static CNetworkPlayer* GetPlayer(CEntity* entity);
private:
    static std::atomic_bool m_bResetPending;
};
