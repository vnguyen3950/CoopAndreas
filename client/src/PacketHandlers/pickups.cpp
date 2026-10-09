#include "stdafx.h"
#include "CPickupSync.h"
PACKET_HANDLER(ePacketType::PICKUP_STATE,Packets::Pickups::State* packet){CPickupSync::Receive(*packet);}
PACKET_HANDLER(ePacketType::PICKUP_ACTION,Packets::Pickups::Action* packet){CPickupSync::Receive(*packet);}
