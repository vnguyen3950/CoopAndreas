#pragma once
#include "config.h"
#include "network/packet.h"
#include "network/player_vitals.h"

namespace Packets::Players
{
class Vitals : public Packet
{
    DEFINE_PACKET_TYPE(Vitals, ePacketType::PLAYER_VITALS, ePacketChannel::SYSTEM);
public:
    int playerid = 0;
    uint32_t generation = 0, sequence = 0;
    bool hasState = false;
    PlayerVitals::State state;
    bool Valid() const
    {
        return playerid >= 0 && playerid < PlayerVitals::MAX_PLAYERS
            && generation <= PlayerVitals::MAX_COUNTER && sequence <= PlayerVitals::MAX_COUNTER
            && (hasState ? sequence != 0 && state.Valid() : generation != 0 && sequence == 0);
    }
private:
    template<class Stream> bool Serialize(Stream& stream)
    {
        static_assert(Config::MAX_SERVER_PLAYERS == PlayerVitals::MAX_PLAYERS, "Vitals player bound differs from room bound");
        if (Stream::IsWriting && !Valid()) return false;
        serialize_int(stream, playerid, 0, PlayerVitals::MAX_PLAYERS - 1);
        serialize_int(stream, generation, 0, PlayerVitals::MAX_COUNTER);
        serialize_int(stream, sequence, 0, PlayerVitals::MAX_COUNTER);
        serialize_bool(stream, hasState);
        if (hasState)
        {
            serialize_int(stream, state.maxHealth, 1, 255);
            serialize_float(stream, state.pedMaxHealth);
            serialize_float(stream, state.breath);
            serialize_float(stream, state.airCapacity);
            serialize_bool(stream, state.submerged);
        }
        return Valid();
    }
};
}
