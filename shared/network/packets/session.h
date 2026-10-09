#pragma once
#include "network/session_sync.h"

namespace Packets::Session
{
template<class Stream> bool State(Stream& stream, SessionSync::Snapshot& s)
{
    serialize_int(stream, s.epoch, 1, SessionSync::MAX_COUNTER);
    serialize_int(stream, s.revision, 1, SessionSync::MAX_COUNTER);
    serialize_int(stream, s.incarnation, 1, SessionSync::MAX_COUNTER);
    serialize_int(stream, s.acknowledged, 0, SessionSync::MAX_COUNTER);
    serialize_int(stream, s.recipient, 0, SessionSync::MAX_PEERS - 1);
    serialize_int(stream, s.host, -1, SessionSync::MAX_PEERS - 1);
    serialize_bool(stream, s.ready);
    serialize_int(stream, s.money, -SessionSync::MONEY_LIMIT, SessionSync::MONEY_LIMIT);
    serialize_int(stream, s.wanted, 0, 6);
    serialize_int(stream, s.maximumWanted, 0, 6);
    serialize_bool(stream, s.policeIgnore); serialize_bool(stream, s.everyoneIgnore);
    for (auto& flag : s.toggles) { serialize_bool(stream, flag); }
    return s.Valid();
}
class Hello : public Packet
{
    DEFINE_PACKET_TYPE(Hello, ePacketType::SESSION_HELLO, ePacketChannel::SCRIPT);
    template<class Stream> bool Serialize(Stream&) { return true; }
};
class Seed : public Packet
{
    DEFINE_PACKET_TYPE(Seed, ePacketType::SESSION_SEED, ePacketChannel::SCRIPT);
public: SessionSync::Snapshot state;
private: template<class Stream> bool Serialize(Stream& stream) { return State(stream, state); }
};
class Update : public Packet
{
    DEFINE_PACKET_TYPE(Update, ePacketType::SESSION_STATE, ePacketChannel::SCRIPT);
public: SessionSync::Snapshot state;
private: template<class Stream> bool Serialize(Stream& stream) { return State(stream, state); }
};
class Transaction : public Packet
{
    DEFINE_PACKET_TYPE(Transaction, ePacketType::SESSION_OPERATION, ePacketChannel::SCRIPT);
public: SessionSync::Operation op;
private:
    template<class Stream> bool Serialize(Stream& stream)
    {
        serialize_int(stream, op.epoch, 1, SessionSync::MAX_COUNTER);
        serialize_int(stream, op.incarnation, 1, SessionSync::MAX_COUNTER);
        serialize_int(stream, op.sequence, 1, SessionSync::MAX_COUNTER);
        int kind = int(op.kind), reason = int(op.reason);
        serialize_int(stream, kind, 0, int(SessionSync::Kind::CheatToggle)); op.kind = SessionSync::Kind(kind);
        serialize_int(stream, reason, 0, int(SessionSync::Reason::Respray)); op.reason = SessionSync::Reason(reason);
        // The full signed delta domain exceeds a signed serialize_int range.
        serialize_bytes(stream, reinterpret_cast<uint8_t*>(&op.delta), sizeof op.delta);
        serialize_int(stream, op.level, 0, 6); serialize_int(stream, op.cheat, 0, SessionSync::CHEATS - 1);
        serialize_bool(stream, op.active); serialize_bool(stream, op.policeIgnore); serialize_bool(stream, op.everyoneIgnore);
        return op.Valid();
    }
};
class CheatAction : public Packet
{
    DEFINE_PACKET_TYPE(CheatAction, ePacketType::SESSION_CHEAT_ACTION, ePacketChannel::SCRIPT);
public:
    uint32_t epoch = 0, revision = 0, incarnation = 0, sequence = 0;
    int sender = 0;
    uint8_t cheat = 0;
private:
    template<class Stream> bool Serialize(Stream& stream)
    {
        serialize_int(stream, epoch, 1, SessionSync::MAX_COUNTER); serialize_int(stream, revision, 1, SessionSync::MAX_COUNTER);
        serialize_int(stream, incarnation, 1, SessionSync::MAX_COUNTER); serialize_int(stream, sequence, 1, SessionSync::MAX_COUNTER);
        serialize_int(stream, sender, 0, SessionSync::MAX_PEERS - 1); serialize_int(stream, cheat, 0, SessionSync::CHEATS - 1);
        return SessionSync::Mode(cheat) == SessionSync::CheatMode::Action;
    }
};
}
