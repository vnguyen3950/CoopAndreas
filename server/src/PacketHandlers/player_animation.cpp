#include "stdafx.h"
#include "CPlayerAnimationSync.h"
PACKET_HANDLER(ePacketType::PLAYER_ANIMATION, Packets::Players::PlayerAnimationState* packet, CNetworkPlayer* sender)
{ CPlayerAnimationServer::Receive(*packet, sender); }
