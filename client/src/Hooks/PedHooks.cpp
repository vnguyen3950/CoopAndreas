#include "stdafx.h"
#include "PedHooks.h"
#include "CNetworkPed.h"
#include <CPedGroups.h>
#include <CCopPed.h>

void PedHooks::ProcessCopControl(CCopPed* ped)
{
    auto* networkPed = CNetwork::m_bAuthenticated ? CNetworkPedManager::GetPed(ped) : nullptr;
    if (networkPed && networkPed->HasValidPed() && networkPed->GetStamp().Lifetime() && !networkPed->m_bSyncing)
    {
        // Keep native physics/task processing, without this replica reacting to
        // the observer's wanted state through CCopPed's local control overlay.
        plugin::CallMethod<0x5E8CD0>(ped);
        return;
    }
    plugin::CallMethod<0x5DE160>(ped);
}
static void __fastcall CopControl_Hook(CCopPed* ped, void*) { PedHooks::ProcessCopControl(ped); }

static void __cdecl CPopulation__Update_Hook(bool generate)
{
    if (CNetwork::m_bAuthenticated)
        CPopulation::Update(generate);
}

CPed* pPed = nullptr;
CNetworkPed* _pNetworkPed = nullptr;
eMoveState nMoveState = (eMoveState)0;
static void __declspec(naked) CPed__SetMoveState_Hook()
{
    __asm
    {
        mov pPed, ecx
        mov eax, [esp+4]
        mov nMoveState, eax
        pushad
    }
 
    if (CNetwork::m_bAuthenticated && !pPed->IsPlayer())
    {
        _pNetworkPed = CNetworkPedManager::GetPed(pPed);
        if (_pNetworkPed && !_pNetworkPed->m_bSyncing)
        {

            pPed->m_nMoveState = _pNetworkPed->m_nMoveState;
            __asm
            {
                popad
                mov eax, 0x5DEC0A
                jmp eax
            }
        }
    }

    pPed->m_nMoveState = nMoveState;

    __asm
    {
        popad
        mov eax, 0x5DEC0A
        jmp eax
    }
}

bool __fastcall CWeapon__Fire_Hook(CWeapon* This, SKIP_EDX, CPed* owner, CVector* vecOrigin, CVector* vecEffectPosn, CEntity* targetEntity, CVector* vecTarget, CVector* arg_14)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(owner);

    if (pNetworkPed)
    {
        if (pNetworkPed->m_bSyncing && pNetworkPed->GetStamp().Lifetime())
        {
            Packets::Peds::PedShotSync packet{};
            packet.pedid = pNetworkPed->m_nPedId;
            packet.stamp = pNetworkPed->GetStamp();
            packet.weaponType = This->m_eWeaponType;
            if (!vecOrigin || !vecEffectPosn) return This->Fire(owner, vecOrigin, vecEffectPosn, targetEntity, vecTarget, arg_14);
            packet.origin = *vecOrigin;
            packet.effect = *vecEffectPosn;
            if (vecTarget)
                packet.target = *vecTarget;
            else if(targetEntity)
                packet.target = targetEntity->GetPosition();

            GetPacketFactory().Send(packet);

            return This->Fire(owner, vecOrigin, vecEffectPosn, targetEntity, vecTarget, arg_14);
        }
    }
    else
    {
        return This->Fire(owner, vecOrigin, vecEffectPosn, targetEntity, vecTarget, arg_14);
    }

    return false;
}

void CStreaming__RequestSpecialModel_Hook(int modelid, const char* txdName, int flags)
{
    CStreaming::RequestSpecialModel(modelid, txdName, flags);

    if (modelid >= 290 && modelid <= 299)
    {
        char* specialModel = PedHooks::ms_aszLoadedSpecialModels[modelid - 290];

        // copy characters and convert to uppercase
        int i = 0;
        for (; txdName[i] != '\0' && i < 7; i++)
        {
            specialModel[i] = std::toupper(txdName[i]);
        }

        // null-terminate the string
        specialModel[i] = '\0';

        // fill remaining elements with null characters
        for (int j = i + 1; j < 8; j++)
        {
            specialModel[j] = '\0';
        }
    }
}

int16_t __fastcall CAEPedSpeechAudioEntity__AddSayEvent_Hook(CAEPedSpeechAudioEntity* This, SKIP_EDX, eAudioEvents audioEvent, int16_t gCtx, uint32_t startTimeDelay, float probability, bool overideSilence, bool isForceAudible, bool isFrontEnd)
{
    CPed* pPed = (CPed*)((uintptr_t)This - offsetof(CPed, m_pedSpeech));

    if (!pPed->IsPlayer())
    {
        if (auto pNetworkPed = CNetworkPedManager::GetPed(pPed))
        {
            if (!pNetworkPed->m_bSyncing)
            {
                return -1;
            }
        }
    }

    auto result = plugin::CallMethodAndReturn<int16_t, 0x4E6550>(This, audioEvent, gCtx, startTimeDelay, probability, overideSilence, isForceAudible, isFrontEnd);
    
    if (result == -1)
    {
        return result;
    }

    Packets::Peds::PedSay packet{};
    packet.phraseId = static_cast<eGlobalSpeechContexts>(gCtx);
    packet.startTimeDelay = startTimeDelay;
    packet.overrideSilence = overideSilence;
    packet.isForceAudible = isForceAudible;
    packet.isFrontEnd = isFrontEnd;
    packet.entity.SetEntity(pPed);
    GetPacketFactory().Send(packet);

    return result;
}

void PedHooks::InjectHooks()
{
    // Supported executable disk identity; verify all relevant native boundaries
    // before changing the one cop vtable slot. This is not runtime validation.
    const uint8_t copPrefix[] = {0x83,0xEC,0x48,0x56};
    const uint8_t basePrefix[] = {0x83,0xEC,0x14,0x53};
    if (*reinterpret_cast<uintptr_t*>(0x86C148) == 0x5DE160
        && !std::memcmp(reinterpret_cast<const void*>(0x5DE160), copPrefix, sizeof copPrefix)
        && !std::memcmp(reinterpret_cast<const void*>(0x5E8CD0), basePrefix, sizeof basePrefix))
        patch::SetPointer(0x86C148, CopControl_Hook);
    else logger::warn("Cop replica control hook disabled: native identity mismatch");
    // ped hooks
    patch::RedirectCall(0x53C030, CPopulation__Update_Hook);
    patch::RedirectCall(0x53C054, CPopulation__Update_Hook);
    
    patch::RedirectJump(0x5DEC00, CPed__SetMoveState_Hook);

    patch::RedirectCall(0x61ECCD, CWeapon__Fire_Hook);
    patch::RedirectCall(0x628328, CWeapon__Fire_Hook);
    patch::RedirectCall(0x62B109, CWeapon__Fire_Hook);
    patch::RedirectCall(0x62B12A, CWeapon__Fire_Hook);

    patch::RedirectJump(0x40B45E, CStreaming__RequestSpecialModel_Hook);

    patch::RedirectCall(0x5F000B, CAEPedSpeechAudioEntity__AddSayEvent_Hook);
}
