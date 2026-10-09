#include "stdafx.h"
#include "CTrailerSync.h"
PACKET_HANDLER(ePacketType::TRAILER_HELLO,Packets::Trailers::Hello* p,CNetworkPlayer* sender){CTrailerSync::Hello(*p,sender);}
PACKET_HANDLER(ePacketType::TRAILER_LINK,Packets::Trailers::Link* p,CNetworkPlayer* sender){CTrailerSync::Link(*p,sender);}
PACKET_HANDLER(ePacketType::TRAILER_POSE,Packets::Trailers::Pose* p,CNetworkPlayer* sender){CTrailerSync::Pose(*p,sender);}
