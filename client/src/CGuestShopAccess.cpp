#include "stdafx.h"
#include "CGuestShopAccess.h"
#include "CSessionSync.h"

namespace {
bool ScriptInPool(const CRunningScript* script)
{
    if (!script || !CTheScripts::ScriptsArray) return false;
    const auto address = reinterpret_cast<uintptr_t>(script);
    const auto first = reinterpret_cast<uintptr_t>(CTheScripts::ScriptsArray);
    return address >= first && address - first < 96u * sizeof(CRunningScript)
        && (address - first) % sizeof(CRunningScript) == 0;
}
}

bool CGuestShopAccess::IsLocalServiceScript(const CRunningScript* script)
{
    if (!ScriptInPool(script) || !script->m_bIsActive
        || !script->m_bIsExternal || script->m_bIsMission) return false;
    static const char names[][8] = {"BARB", "CLOTH", "TATTO", "AMUNAT", "JFUD"};
    bool retail = false;
    for (const auto& name : names) if (!strnicmp(script->m_szName, name, 8)) { retail = true; break; }
    if (!retail) return false;
    // Captured ESI must belong to the live native chain, not a stale name or
    // the synthetic CRunningScript used by Command<>/network opcode replay.
    auto* current = CTheScripts::pActiveScripts;
    for (unsigned count = 0; current && count < 96; ++count) {
        if (!ScriptInPool(current)) return false;
        if (current == script) return true;
        current = current->m_pNext;
    }
    return false;
}

bool __cdecl CGuestShopAccess::CanStartFromScript(CPlayerPed* player, CRunningScript* script)
{
    if (!player) return false;
    // Offline SCM and non-retail host story predicates retain native behavior.
    if (!CNetwork::m_bAuthenticated)
        return !CNetwork::m_bConnected && player->CanPlayerStartMission();
    const bool retail = IsLocalServiceScript(script);
    if (CLocalPlayer::m_bIsHost && !retail) return player->CanPlayerStartMission();
    if (!retail) return false;
    if (CWorld::PlayerInFocus != 0 || player != FindPlayerPed(0) || !CPools::ms_pPedPool
        || !CPools::ms_pPedPool->IsObjectValid(player)) return false;
    const int reference = CPools::GetPedRef(player);
    if (reference < 0 || CPools::GetPed(reference) != player || !player->m_pPlayerData) return false;
    if (!CSessionSync::IsWalletReadyForLocalService()) return false;
    if (!CLocalPlayer::m_bIsHost) {
        const int missionFlag = CTheScripts::OnAMissionFlag;
        if (missionFlag <= 0 || missionFlag >= 200000 || CTheScripts::ScriptSpace[missionFlag]) return false;
    }
    // SDK uses a direct verified bool __thiscall at 0x609590, not a vtable slot.
    return player->CanPlayerStartMission();
}

bool CGuestShopAccess::NativeRetailBindingMatches()
{
    // Supported A559 executable: command 03EE keeps the current script in ESI,
    // obtains the player in ECX, then tests AL after this zero-argument call.
    const uint8_t scriptContext[] = {0x6A,0x01,0x8B,0xCE,0xE8,0xE2,0xAA,0xFD,0xFF};
    const uint8_t resultTest[] = {0x84,0xC0,0x0F,0x95,0xC2};
    const uint8_t call[] = {0xE8,0xDB,0xFF,0x17,0x00};
    const uint8_t predicate[] = {0xA0,0xB0,0xA8,0x96,0x00,0x84,0xC0,0x56,0x8B,0xF1};
    return !std::memcmp(reinterpret_cast<const void*>(0x489595), scriptContext, sizeof scriptContext)
        && !std::memcmp(reinterpret_cast<const void*>(0x4895B5), resultTest, sizeof resultTest)
        && !std::memcmp(reinterpret_cast<const void*>(0x4895B0), call, sizeof call)
        && !std::memcmp(reinterpret_cast<const void*>(0x609590), predicate, sizeof predicate);
}
