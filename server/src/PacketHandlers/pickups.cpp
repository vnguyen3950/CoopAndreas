#include "stdafx.h"
#include "CPickupSync.h"
PACKET_HANDLER(ePacketType::PICKUP_HELLO,Packets::Pickups::Hello* packet,CNetworkPlayer* sender){CPickupServer::Hello(*packet,sender);}
PACKET_HANDLER(ePacketType::PICKUP_ACTION,Packets::Pickups::Action* packet,CNetworkPlayer* sender){CPickupServer::Action(*packet,sender);}
