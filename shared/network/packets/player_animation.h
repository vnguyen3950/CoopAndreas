#pragma once
#include "network/packet.h"
#include "network/player_animation_sync.h"

namespace Packets::Players
{
template<class Stream> bool SerializeActorLife(Stream& stream, PlayerAnimation::Life& life)
{
    if (Stream::IsWriting && !life.Valid()) return false;
    serialize_int(stream, life.generation, 0, PlayerAnimation::MAX_COUNTER);
    serialize_int(stream, life.birth, 1, PlayerAnimation::MAX_COUNTER);
    serialize_int(stream, life.sequence, 1, PlayerAnimation::MAX_COUNTER);
    serialize_int(stream, life.model, 0, 299);
    serialize_int(stream, life.area, 0, 255);
    serialize_int(stream, life.nativeReference, 0, PlayerAnimation::MAX_COUNTER);
    serialize_bool(stream, life.ready);
    return life.Valid();
}
class PlayerAnimationState : public Packet
{
    DEFINE_PACKET_TYPE(PlayerAnimationState, ePacketType::PLAYER_ANIMATION, ePacketChannel::EVENT);
public:
    int playerid = 0;
    PlayerAnimation::Life life;
    PlayerAnimation::State state;
    uint32_t sampledAt = 0;
    bool Valid() const
    { return playerid >= 0 && playerid < PlayerAnimation::MAX_PLAYERS && life.Valid() && state.Valid() && (life.ready || !state.pose); }
    template<class Stream> bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && !Valid()) return false;
        serialize_int(stream, playerid, 0, PlayerAnimation::MAX_PLAYERS - 1);
        if (!SerializeActorLife(stream, life)) return false;
        serialize_int(stream, state.pose, 0, 5);
        serialize_uint32(stream, sampledAt);
        if (state.pose)
        {
            serialize_int(stream, state.instance, 1, PlayerAnimation::MAX_COUNTER);
            serialize_float(stream, state.phase); serialize_float(stream, state.duration);
            serialize_float(stream, state.speed); serialize_float(stream, state.blend);
            serialize_bool(stream, state.loop);
        }
        else if (Stream::IsReading) state = {};
        return Valid();
    }
};
}
