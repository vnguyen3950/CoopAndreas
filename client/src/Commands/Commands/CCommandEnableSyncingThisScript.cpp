#include "stdafx.h"
#include "CCommandEnableSyncingThisScript.h"
#include "COpCodeSync.h"

void CCommandEnableSyncingThisScript::Process(CRunningScript* pScript)
{
    // CChat::AddMessage("CCommandEnableSyncingThisScript \"%s\"", pScript->m_szName);

    for (size_t i = 0; i < COpCodeSync::ms_iFreeSyncedScript; i++)
    {
        if (strnicmp(COpCodeSync::ms_aszSyncedScripts[i], pScript->m_szName, 8) == 0)
        {
            return;
        }
    }

    if (COpCodeSync::ms_iFreeSyncedScript >= ARRAY_SIZE(COpCodeSync::ms_aszSyncedScripts))
    {
        CChat::AddMessage("[Mission] Script synchronization registry is full.");
        return;
    }

    memcpy(COpCodeSync::ms_aszSyncedScripts[COpCodeSync::ms_iFreeSyncedScript], pScript->m_szName, 8);
    COpCodeSync::ms_aszSyncedScripts[COpCodeSync::ms_iFreeSyncedScript][8] = '\0';
    ++COpCodeSync::ms_iFreeSyncedScript;
}
