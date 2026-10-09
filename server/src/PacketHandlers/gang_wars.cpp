#include "stdafx.h"
#include "CGangWarSync.h"
PACKET_HANDLER(ePacketType::GANG_WAR_STATE,Packets::Gangs::State* packet,CNetworkPlayer* sender)
{CGangWarServer::Receive(*packet,sender);}
PACKET_HANDLER(ePacketType::GANG_TERRITORY,Packets::Gangs::Territory* packet,CNetworkPlayer* sender)
{CGangWarServer::Receive(*packet,sender);}
