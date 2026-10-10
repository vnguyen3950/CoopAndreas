#pragma once
class CPlayerPed;
class CRunningScript;

class CGuestShopAccess
{
public:
    static bool IsLocalServiceScript(const CRunningScript* script);
    static bool __cdecl CanStartFromScript(CPlayerPed* player, CRunningScript* script);
    static bool NativeRetailBindingMatches();
};
