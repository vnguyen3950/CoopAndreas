#include "stdafx.h"
#include "CFireSync.h"
PACKET_HANDLER(ePacketType::FIRE_RESET,Packets::Fires::Reset* p) { CFireSync::Receive(*p); }
PACKET_HANDLER(ePacketType::FIRE_STATE,Packets::Fires::Update* p) { CFireSync::Receive(*p); }
PACKET_HANDLER(ePacketType::FIRE_REMOVE,Packets::Fires::Remove* p) { CFireSync::Receive(*p); }
PACKET_HANDLER(ePacketType::FIRE_BIND,Packets::Fires::Bind* p) { CFireSync::Receive(*p); }
PACKET_HANDLER(ePacketType::FIRE_REQUEST,Packets::Fires::Request* p) { CFireSync::Receive(*p); }
