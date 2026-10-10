#include "stdafx.h"
#include "CEntryExitDiagnostics.h"
#include "CSessionSync.h"
#include "CPacketBuffer.h"
#include "CServerTime.h"
#include "runtime_diagnostics.h"
#include <network/packets/scripts.h>
#include <CGame.h>
#include <CEntryExitManager.h>
#include <CGameLogic.h>
#include <CReplay.h>
#include <CCutsceneMgr.h>
#include <cmath>

void CEntryExitDiagnostics::Process() {
    if (!CNetwork::m_bConnected) return;
    static bool sampled = false;
    static DWORD lastSample = 0;
    const DWORD now = GetTickCount();
    if (sampled && now - lastSample < 5000) return;
    sampled = true; lastSample = now;
    if (!CNetwork::m_bAuthenticated || gGameState != 9 || CWorld::PlayerInFocus != 0 || !CPools::ms_pPedPool) {
        RuntimeDiagnostics::Write("entry", "\"event\":\"unready\",\"auth\":%d,\"game_state\":%d,\"focus\":%d,\"ped_pool\":%d",
            int(CNetwork::m_bAuthenticated), int(gGameState), int(CWorld::PlayerInFocus), int(CPools::ms_pPedPool != nullptr));
        return;
    }
    auto* player = FindPlayerPed(0);
    if (!player || !CPools::ms_pPedPool->IsObjectValid(player)) {
        RuntimeDiagnostics::Write("entry", "\"event\":\"local-player-unbound\",\"present\":%d", int(player != nullptr));
        return;
    }
    const int reference = CPools::GetPedRef(player);
    if (reference < 0 || CPools::GetPed(reference) != player || !player->m_pPlayerData) {
        RuntimeDiagnostics::Write("entry", "\"event\":\"local-reference-unbound\",\"ref\":%d", reference);
        return;
    }
    const auto position = player->GetPosition();
    const bool finite = std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
    int poolCount = 0, enabledCount = 0, nearestFlags = -1, nearestArea = -1;
    float nearestDistance = 1.0e30f;
    if (CEntryExitManager::mp_poolEntryExits) {
        for (auto* entry : CEntryExitManager::mp_poolEntryExits) {
            if (++poolCount > int(Packets::Scripts::EnExSync::MAX_ENEX_COUNT)) break;
            if (entry->m_nFlags.bEnableAccess) ++enabledCount;
            if (!finite) continue;
            const float dx = position.x - (entry->m_recEntrance.left + entry->m_recEntrance.right) * 0.5f;
            const float dy = position.y - (entry->m_recEntrance.bottom + entry->m_recEntrance.top) * 0.5f;
            const float distance = dx * dx + dy * dy;
            if (!std::isfinite(distance) || distance >= nearestDistance) continue;
            nearestDistance = distance; nearestArea = entry->m_nArea;
            unsigned short flags = 0;
            std::memcpy(&flags, &entry->m_nFlags, sizeof flags);
            nearestFlags = flags;
        }
    }
    const int missionIndex = CTheScripts::OnAMissionFlag;
    const int mission = missionIndex > 0 && missionIndex < 200000 ? int(CTheScripts::ScriptSpace[missionIndex] != 0) : -1;
    auto* pad = CPad::GetPad(0);
    const unsigned controls = pad ? unsigned(pad->DisablePlayerControls) : 65535u;
    const auto& packets = GetPacketBuffer().m_packets;
    RuntimeDiagnostics::Write("entry", "\"event\":\"state\",\"host\":%d,\"player\":%d,\"ref\":%d,\"area\":%d,\"ped_area\":%d,"
        "\"cutscene\":%d,\"controls\":%u,\"safe\":%d,\"debug_ui\":%d,\"native_coop\":%d,\"replay\":%d,\"disabled\":%d,\"transition\":%d,"
        "\"native_player0\":%d,\"native_player1\":%d,\"can_start_native\":%d,\"wallet_ready\":%d,\"mission\":%d,"
        "\"pool\":%d,\"access_enabled\":%d,\"nearest_flags\":%d,\"nearest_area\":%d,"
        "\"buffered\":%u,\"next_time\":%u,\"server_time\":%u",
        int(CLocalPlayer::m_bIsHost), CNetworkPlayerManager::m_nMyId, reference, int(CGame::currArea), int(player->m_nAreaCode),
        int(CCutsceneMgr::ms_cutsceneProcessing), controls, int((controls & 0x20u) != 0), int((controls & 0x200u) != 0),
        int(CGameLogic::IsCoopGameGoingOn()), int(CReplay::Mode), int(CEntryExitManager::ms_bDisabled), CEntryExitManager::ms_exitEnterState,
        int(CWorld::Players[0].m_pPed != nullptr), int(CWorld::Players[1].m_pPed != nullptr), int(player->CanPlayerStartMission()),
        int(CSessionSync::IsWalletReadyForLocalService()), mission, poolCount, enabledCount, nearestFlags, nearestArea,
        unsigned(packets.size()), packets.empty() ? 0u : unsigned(packets.front()->serverTime), unsigned(g_serverTime));
}
