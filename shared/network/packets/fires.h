#pragma once
#include "network/packet.h"
#include "network/fire_sync.h"
namespace Packets::Fires {
template<class Stream> bool Key(Stream& stream, FireSync::Key& k) {
    if (Stream::IsWriting && !k.Valid()) return false;
    serialize_int(stream,k.epoch,1,FireSync::MaxCounter); serialize_int(stream,k.id,1,FireSync::MaxFires);
    serialize_int(stream,k.generation,1,FireSync::MaxCounter); serialize_int(stream,k.sequence,1,FireSync::MaxCounter);
    return k.Valid();
}
template<class Stream> bool Entity(Stream& stream, FireSync::Entity& e) {
    if (Stream::IsWriting && !e.Valid()) return false;
    int kind = int(e.kind); serialize_int(stream,kind,0,int(FireSync::Kind::Object)); e.kind = FireSync::Kind(kind);
    if (e.kind == FireSync::Kind::World) { e = {}; return true; }
    const int minimum = e.kind == FireSync::Kind::Object ? 1 : 0;
    const int maximum = e.kind == FireSync::Kind::Object ? 0x07ffffff : e.kind == FireSync::Kind::Player ? 7 : 254;
    serialize_int(stream,e.id,minimum,maximum);
    serialize_int(stream,e.generation,1,FireSync::MaxCounter); serialize_int(stream,e.ownerEpoch,1,FireSync::MaxCounter);
    serialize_int(stream,e.owner,0,7); serialize_int(stream,e.model,0,19999); return e.Valid();
}
template<class Stream> bool Vec(Stream& stream, FireSync::Vec& p) {
    if (Stream::IsWriting && !p.Valid()) return false;
    serialize_float(stream,p.x); serialize_float(stream,p.y); serialize_float(stream,p.z); return p.Valid();
}
class Hello : public Packet {
    DEFINE_PACKET_TYPE(Hello,ePacketType::FIRE_HELLO,ePacketChannel::EVENT);
public: uint32_t nativeReference = 0, gameGeneration = 0; bool controllingRestart = false; int model = 0; FireSync::Vec position{};
private: template<class Stream> bool Serialize(Stream& stream) {
    if (Stream::IsWriting && (nativeReference > FireSync::MaxCounter || !gameGeneration || gameGeneration > FireSync::MaxCounter || model < 0 || model > 19999)) return false;
    serialize_int(stream,nativeReference,0,FireSync::MaxCounter); serialize_int(stream,gameGeneration,1,FireSync::MaxCounter);
    serialize_bool(stream,controllingRestart); serialize_int(stream,model,0,19999); return Vec(stream,position);
} };
class Reset : public Packet {
    DEFINE_PACKET_TYPE(Reset,ePacketType::FIRE_RESET,ePacketChannel::SYSTEM);
public: uint32_t epoch = 0, connection = 0; int host = -1;
private: template<class Stream> bool Serialize(Stream& stream) {
    if (Stream::IsWriting && (!epoch || epoch > FireSync::MaxCounter || !connection || connection > FireSync::MaxCounter || host < -1 || host > 7)) return false;
    serialize_int(stream,epoch,1,FireSync::MaxCounter); serialize_int(stream,connection,1,FireSync::MaxCounter);
    serialize_int(stream,host,-1,7); return true;
} };
class Update : public Packet {
    DEFINE_PACKET_TYPE(Update,ePacketType::FIRE_STATE,ePacketChannel::EVENT);
public: FireSync::State state{};
private: template<class Stream> bool Serialize(Stream& stream) {
    if (Stream::IsWriting && !state.Valid()) return false;
    if (!Key(stream,state.key) || !Vec(stream,state.position) || !Entity(stream,state.target) || !Entity(stream,state.creator)) return false;
    serialize_float(stream,state.strength); serialize_int(stream,state.remaining,0,120000); serialize_uint8(stream,state.generations);
    serialize_bool(stream,state.script); serialize_bool(stream,state.noise); return state.Valid();
} };
class Remove : public Packet {
    DEFINE_PACKET_TYPE(Remove,ePacketType::FIRE_REMOVE,ePacketChannel::EVENT);
public: FireSync::Key key{};
private: template<class Stream> bool Serialize(Stream& stream) { return Key(stream,key); }
};
class Bind : public Packet {
    DEFINE_PACKET_TYPE(Bind,ePacketType::FIRE_BIND,ePacketChannel::EVENT);
public: uint32_t epoch = 0; FireSync::Entity entity{}; bool live = true;
private: template<class Stream> bool Serialize(Stream& stream) {
    if (Stream::IsWriting && (!epoch || epoch > FireSync::MaxCounter || !entity.Valid() || entity.kind == FireSync::Kind::World)) return false;
    serialize_int(stream,epoch,1,FireSync::MaxCounter); serialize_bool(stream,live); return Entity(stream,entity) && entity.kind != FireSync::Kind::World;
} };
class Request : public Packet {
    DEFINE_PACKET_TYPE(Request,ePacketType::FIRE_REQUEST,ePacketChannel::EVENT);
public: int sender = 0; FireSync::Request request{};
private: template<class Stream> bool Serialize(Stream& stream) {
    if (Stream::IsWriting && !request.Valid()) return false;
    serialize_int(stream,sender,0,7); serialize_int(stream,request.epoch,1,FireSync::MaxCounter);
    serialize_int(stream,request.connection,1,FireSync::MaxCounter); serialize_int(stream,request.sequence,1,FireSync::MaxCounter);
    serialize_int(stream,request.gameGeneration,1,FireSync::MaxCounter);
    int intent = int(request.intent); serialize_int(stream,intent,0,int(FireSync::Intent::Heat)); request.intent = FireSync::Intent(intent);
    if (!Vec(stream,request.position) || !Entity(stream,request.creator) || !Entity(stream,request.target) || !Entity(stream,request.issuer)) return false;
    if (request.intent != FireSync::Intent::Ground && request.intent != FireSync::Intent::Attached && !Key(stream,request.fire)) return false;
    serialize_float(stream,request.radius); serialize_float(stream,request.water); return request.Valid();
} };
}
