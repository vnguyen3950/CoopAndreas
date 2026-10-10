#pragma once
class CCopPed;
class PedHooks
{
public:
	static void InjectHooks();
    static void ProcessCopControl(CCopPed* ped);

	static inline char ms_aszLoadedSpecialModels[10][8];
};

