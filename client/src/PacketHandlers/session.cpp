#include "stdafx.h"
#include "CSessionSync.h"
PACKET_HANDLER(ePacketType::SESSION_STATE, Packets::Session::Update* packet) { CSessionSync::HandleState(*packet); }
PACKET_HANDLER(ePacketType::SESSION_CHEAT_ACTION, Packets::Session::CheatAction* packet) { CSessionSync::HandleAction(*packet); }
