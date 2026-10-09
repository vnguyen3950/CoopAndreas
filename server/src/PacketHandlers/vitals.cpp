#include "stdafx.h"
#include "CPlayerVitalsSync.h"
PACKET_HANDLER(ePacketType::PLAYER_VITALS, Packets::Players::Vitals* packet, CNetworkPlayer* sender)
{ CPlayerVitalsServer::Receive(*packet, sender); }
