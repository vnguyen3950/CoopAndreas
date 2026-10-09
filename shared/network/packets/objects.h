#pragma once
#include "network/object_sync.h"

namespace Packets::Objects
{
template <typename Stream> bool SerializeState(Stream& stream, ObjectSync::State& s)
{
    serialize_int(stream, s.model, 0, ObjectSync::MAX_MODEL);
    serialize_int(stream, s.area, 0, 18);
    serialize_int(stream, s.lastWeaponDamage, 0, 255);
    for (auto* v : {&s.position, &s.rotation, &s.velocity, &s.turnSpeed})
    {
        serialize_float(stream, v->x); serialize_float(stream, v->y); serialize_float(stream, v->z);
    }
    serialize_float(stream, s.health); serialize_float(stream, s.scale);
    serialize_bool(stream, s.collision); serialize_bool(stream, s.visible);
    serialize_bool(stream, s.dynamic); serialize_bool(stream, s.targetable);
    serialize_bool(stream, s.bulletProof); serialize_bool(stream, s.fireProof);
    serialize_bool(stream, s.collisionProof); serialize_bool(stream, s.meleeProof);
    serialize_bool(stream, s.explosionProof); serialize_bool(stream, s.playerOnlyDamage);
    return s.Valid();
}
class Create : public Packet
{
    DEFINE_PACKET_TYPE(Create, ePacketType::OBJECT_CREATE, ePacketChannel::SCRIPT);
public:
    uint32_t id = 0, revision = 1;
    ObjectSync::State state;
private:
    template <typename Stream> bool Serialize(Stream& stream)
    {
        serialize_int(stream, id, 1, ObjectSync::MAX_ID);
        serialize_int(stream, revision, 1, 0x7fffffff);
        return SerializeState(stream, state);
    }
};
class Update : public Packet
{
    DEFINE_PACKET_TYPE(Update, ePacketType::OBJECT_UPDATE, ePacketChannel::SCRIPT);
public:
    uint32_t id = 0, revision = 1;
    ObjectSync::State state;
private:
    template <typename Stream> bool Serialize(Stream& stream)
    {
        serialize_int(stream, id, 1, ObjectSync::MAX_ID);
        serialize_int(stream, revision, 1, 0x7fffffff);
        return SerializeState(stream, state);
    }
};
class Remove : public Packet
{
    DEFINE_PACKET_TYPE(Remove, ePacketType::OBJECT_REMOVE, ePacketChannel::SCRIPT);
public: uint32_t id = 0;
private:
    template <typename Stream> bool Serialize(Stream& stream)
    { serialize_int(stream, id, 1, ObjectSync::MAX_ID); return true; }
};
class Confirm : public Packet
{
    DEFINE_PACKET_TYPE(Confirm, ePacketType::OBJECT_CONFIRM, ePacketChannel::SCRIPT);
public: uint32_t token = 0, id = 0;
private:
    template <typename Stream> bool Serialize(Stream& stream)
    {
        serialize_int(stream, token, 1, ObjectSync::MAX_ID);
        serialize_int(stream, id, 1, ObjectSync::MAX_ID); return true;
    }
};
class Resync : public Packet
{
    DEFINE_PACKET_TYPE(Resync, ePacketType::OBJECT_RESYNC, ePacketChannel::SCRIPT);
    template <typename Stream> bool Serialize(Stream&) { return true; }
};
// The server converts an authenticated PlayerBulletShot into this SCRIPT
// packet, ordering the hit after the host's object ID confirmation.
class Hit : public Packet
{
    DEFINE_PACKET_TYPE(Hit, ePacketType::OBJECT_HIT, ePacketChannel::SCRIPT);
public:
    uint32_t id = 0;
    int playerId = 0, weapon = 22;
    ObjectSync::Vec3 origin, impact;
private:
    template <typename Stream> bool Serialize(Stream& stream)
    {
        serialize_int(stream, id, 1, ObjectSync::MAX_ID);
        serialize_int(stream, playerId, 0, Config::MAX_SERVER_PLAYERS - 1);
        serialize_int(stream, weapon, 22, 34);
        serialize_float(stream, origin.x); serialize_float(stream, origin.y); serialize_float(stream, origin.z);
        serialize_float(stream, impact.x); serialize_float(stream, impact.y); serialize_float(stream, impact.z);
        return origin.Valid(20000) && impact.Valid(20000);
    }
};
}
