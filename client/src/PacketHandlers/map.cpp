#include "stdafx.h"
#include "CMapSync.h"
PACKET_HANDLER(ePacketType::MAP_DISCOVERY, Packets::Map::Discovery* packet)
{ CMapSync::Receive(*packet); }
