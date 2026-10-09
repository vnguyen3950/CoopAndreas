#include "stdafx.h"
#include "CFireSync.h"
PACKET_HANDLER(ePacketType::FIRE_HELLO,Packets::Fires::Hello* p,CNetworkPlayer* s) { CFireSync::Hello(*p,s); }
PACKET_HANDLER(ePacketType::FIRE_STATE,Packets::Fires::Update* p,CNetworkPlayer* s) { CFireSync::Update(*p,s); }
PACKET_HANDLER(ePacketType::FIRE_REMOVE,Packets::Fires::Remove* p,CNetworkPlayer* s) { CFireSync::Remove(*p,s); }
PACKET_HANDLER(ePacketType::FIRE_REQUEST,Packets::Fires::Request* p,CNetworkPlayer* s) { CFireSync::Request(*p,s); }
PACKET_HANDLER(ePacketType::FIRE_RESET,Packets::Fires::Reset*,CNetworkPlayer*) {}
PACKET_HANDLER(ePacketType::FIRE_BIND,Packets::Fires::Bind*,CNetworkPlayer*) {}
