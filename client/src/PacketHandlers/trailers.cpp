#include "stdafx.h"
#include "CTrailerSync.h"
PACKET_HANDLER(ePacketType::TRAILER_LEASE,Packets::Trailers::Lease* p){CTrailerSync::Receive(*p);}
PACKET_HANDLER(ePacketType::TRAILER_LINK,Packets::Trailers::Link* p){CTrailerSync::Receive(*p);}
PACKET_HANDLER(ePacketType::TRAILER_POSE,Packets::Trailers::Pose* p){CTrailerSync::Receive(*p);}
