#include "stdafx.h"
#include "CMapSync.h"
PACKET_HANDLER(ePacketType::MAP_DISCOVERY, Packets::Map::Discovery* packet, CNetworkPlayer* sender)
{ CMapSyncServer::Receive(*packet, sender); }
