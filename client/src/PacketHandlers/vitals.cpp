#include "stdafx.h"
#include "CPlayerVitalsSync.h"
PACKET_HANDLER(ePacketType::PLAYER_VITALS, Packets::Players::Vitals* packet)
{ CPlayerVitalsSync::Receive(*packet); }
