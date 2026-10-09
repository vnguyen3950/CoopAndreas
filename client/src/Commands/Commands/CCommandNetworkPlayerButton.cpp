#include "stdafx.h"
#include "CCommandNetworkPlayerButton.h"
#include "CNetworkPlayerInput.h"

void CCommandNetworkPlayerButton::Process(CRunningScript* script)
{
    script->CollectParameters(2);
    const int actor = ScriptParams[0];
    const int button = ScriptParams[1];
    bool pressed = false;
    if (CNetwork::m_bAuthenticated && CLocalPlayer::m_bIsHost && actor > 0 && button >= 0 && button < 20)
    {
        if (auto* ped = CPools::GetPed(actor))
        {
            if (ped->IsVTableValid() && ped->m_fHealth > 0.0f)
            {
                if (auto* player = CNetworkPlayerManager::GetPlayer(ped))
                {
                    pressed = GetNetworkScriptButtonState(player->m_newControllerState, button) != 0;
                    if (m_justPressed)
                        pressed = pressed && GetNetworkScriptButtonState(player->m_oldControllerState, button) == 0;
                }
            }
        }
    }
    script->UpdateCompareFlag(pressed);
}
