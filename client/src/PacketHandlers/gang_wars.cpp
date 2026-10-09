#include "stdafx.h"
#include "CGangWarSync.h"
PACKET_HANDLER(ePacketType::GANG_WAR_STATE,Packets::Gangs::State* packet)
{CGangWarSync::Receive(*packet);}
PACKET_HANDLER(ePacketType::GANG_TERRITORY,Packets::Gangs::Territory* packet)
{CGangWarSync::Receive(*packet);}
