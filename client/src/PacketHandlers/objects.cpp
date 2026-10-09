#include "stdafx.h"
#include "CNetworkObjectManager.h"

PACKET_HANDLER(ePacketType::OBJECT_CREATE, Packets::Objects::Create* packet)
{ CNetworkObjectManager::ReceiveCreate(*packet); }
PACKET_HANDLER(ePacketType::OBJECT_UPDATE, Packets::Objects::Update* packet)
{ CNetworkObjectManager::ReceiveUpdate(*packet); }
PACKET_HANDLER(ePacketType::OBJECT_REMOVE, Packets::Objects::Remove* packet)
{ CNetworkObjectManager::ReceiveRemove(packet->id); }
PACKET_HANDLER(ePacketType::OBJECT_CONFIRM, Packets::Objects::Confirm* packet)
{ CNetworkObjectManager::Confirm(packet->token, packet->id); }
PACKET_HANDLER(ePacketType::OBJECT_HIT, Packets::Objects::Hit* packet)
{ CNetworkObjectManager::ReceiveHit(*packet); }
