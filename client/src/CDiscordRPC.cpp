#include "stdafx.h"
#include "CDiscordRPC.h"

void CDiscordRPC::Init()
{
	if (ms_bInitialized)
		return;

	DiscordEventHandlers handlers;

	memset(&handlers, 0, sizeof(handlers));
	memset(&presence, 0, sizeof(presence));

	presence.state = state.c_str();
	presence.details = details.c_str();
	presence.startTimestamp = std::time(nullptr);
	presence.largeImageKey = "icon";
	presence.largeImageText = "CoopAndreas";

	Discord_Initialize("1324128258672951317", &handlers, 1, NULL);
	Discord_UpdatePresence(&presence);
	ms_bInitialized = true;
}

void CDiscordRPC::Destroy()
{
	if (!ms_bInitialized)
		return;

	Discord_Shutdown();
	ms_bInitialized = false;
}

void CDiscordRPC::SetDetailsAndState(std::string details, std::string state)
{
	CDiscordRPC::details = details;
	presence.details = details.c_str();
	CDiscordRPC::state = state;
	presence.state = state.c_str();
	Discord_UpdatePresence(&presence);
}