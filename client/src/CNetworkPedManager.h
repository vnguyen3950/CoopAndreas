#pragma once
#include <array>
#include <atomic>
#include "network/npc_sync.h"
class CNetworkPedManager
{
public:
	static std::vector<CNetworkPed*> m_pPeds;
	static CNetworkPed* m_apTempPeds[255];

	static CNetworkPed* GetPed(int pedid);
	static CNetworkPed* GetPed(CEntity* entity);
	static bool IsPedTracked(CPed* pPed);
	static void Add(CNetworkPed* ped);
	static void Remove(CNetworkPed* ped);
	static void HandlePedDestruction(CPed* pPed);
	static void Update();
	static void Process();
	static void AssignHost();
	static unsigned char AddToTempList(CNetworkPed* networkPed);
	static void RemoveInvalidPeds();
	static bool AcceptSpawn(int id, const NPCSync::Stamp& stamp);
	static bool AcceptRemoval(int id, const NPCSync::Stamp& stamp);
	static bool PinGangWarPedToHost(CPed* ped, bool pinned);
	static void RequestReset();
	static void ProcessPendingReset();
	static void Clear();
	static void Init();
	static bool NativeReady();
	static bool Defer(Packet& packet, int id, const NPCSync::Stamp& stamp, bool force = false);
	static void ProcessPendingNative();
private:
	static std::array<uint32_t, 255> m_generations;
	static std::array<bool, 255> m_removed;
	static std::atomic_bool m_resetPending;
};

