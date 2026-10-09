#pragma once
#include "network/packet.h"
#include "network/trailer_sync.h"
namespace Packets::Trailers {
template<class Stream> bool Ref(Stream& s,TrailerSync::Ref& r) {
    if(Stream::IsWriting && !r.Valid())return false;
    serialize_int(s,r.id,0,254);serialize_int(s,r.generation,1,TrailerSync::MaxCounter);serialize_int(s,r.model,400,611);return r.Valid();
}
template<class Stream> bool Vec(Stream& s,TrailerSync::Vec& v) { serialize_float(s,v.x);serialize_float(s,v.y);serialize_float(s,v.z);return true; }
template<class Stream> bool Frame(Stream& s,TrailerSync::Frame& f) {
    if(Stream::IsWriting && !f.Valid())return false;
    if(!Vec(s,f.position)||!Vec(s,f.right)||!Vec(s,f.forward)||!Vec(s,f.velocity)||!Vec(s,f.turn))return false;
    serialize_float(s,f.health);return f.Valid();
}
template<class Stream> bool EncodeLink(Stream& s,TrailerSync::Link& l) {
    if(Stream::IsWriting && !l.Valid())return false;
    serialize_int(s,l.room,1,TrailerSync::MaxCounter);serialize_int(s,l.ownerEpoch,1,TrailerSync::MaxCounter);
    serialize_int(s,l.sequence,1,TrailerSync::MaxCounter);serialize_int(s,l.revision,0,TrailerSync::MaxCounter);
    if(!Ref(s,l.parent)||!Ref(s,l.child))return false;serialize_bool(s,l.attached);return Frame(s,l.frame)&&l.Valid();
}
class Hello:public Packet {
    DEFINE_PACKET_TYPE(Hello,ePacketType::TRAILER_HELLO,ePacketChannel::EVENT);
public:uint32_t scene=0;bool controllingRestart=false;
private:template<class Stream> bool Serialize(Stream& s){if(Stream::IsWriting&&!TrailerSync::Counter(scene))return false;
    serialize_int(s,scene,1,TrailerSync::MaxCounter);serialize_bool(s,controllingRestart);return true;}
};
class Lease:public Packet {
    DEFINE_PACKET_TYPE(Lease,ePacketType::TRAILER_LEASE,ePacketChannel::EVENT);
public:uint32_t room=0,scene=0;bool reset=false;TrailerSync::Lease lease{};
private:template<class Stream> bool Serialize(Stream& s){
    if(Stream::IsWriting&&(!TrailerSync::Counter(room)||(reset?!TrailerSync::Counter(scene):!lease.Valid())))return false;
    serialize_int(s,room,1,TrailerSync::MaxCounter);serialize_bool(s,reset);
    if(reset){serialize_int(s,scene,1,TrailerSync::MaxCounter);return true;}
    if(!Ref(s,lease.vehicle))return false;serialize_int(s,lease.epoch,1,TrailerSync::MaxCounter);
    serialize_int(s,lease.owner,-1,7);serialize_bool(s,lease.live);return lease.Valid();
}
};
class Link:public Packet {
    DEFINE_PACKET_TYPE(Link,ePacketType::TRAILER_LINK,ePacketChannel::EVENT);
public:TrailerSync::Link link{};uint32_t scene=0;
private:template<class Stream> bool Serialize(Stream& s){if(Stream::IsWriting && scene>TrailerSync::MaxCounter)return false;serialize_int(s,scene,0,TrailerSync::MaxCounter);return Trailers::EncodeLink(s,link);}
};
class Pose:public Packet {
    DEFINE_PACKET_TYPE(Pose,ePacketType::TRAILER_POSE,ePacketChannel::SYNC);
public:TrailerSync::Link link{};uint32_t scene=0;
private:template<class Stream> bool Serialize(Stream& s){if(Stream::IsWriting&&(scene>TrailerSync::MaxCounter||!link.attached||!TrailerSync::Counter(link.revision)))return false;
    serialize_int(s,scene,0,TrailerSync::MaxCounter);return Trailers::EncodeLink(s,link)&&link.attached&&TrailerSync::Counter(link.revision);}
};
}
