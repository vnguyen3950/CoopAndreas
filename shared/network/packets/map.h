#pragma once
#include "config.h"
#include "network/packet.h"
#include "network/map_sync.h"
namespace Packets::Map
{
enum class Mode : uint8_t { Seed, Reveal, State };
class Discovery : public Packet
{
    DEFINE_PACKET_TYPE(Discovery, ePacketType::MAP_DISCOVERY, ePacketChannel::SYSTEM);
public:
    Mode mode = Mode::State;
    int playerid = 0;
    uint32_t generation = 0, sequence = 0, epoch = 0, revision = 0;
    MapSync::Discovery cells;
    bool Valid() const
    {
        if (playerid < 0 || playerid >= Config::MAX_SERVER_PLAYERS || generation > MapSync::MAX_COUNTER
            || sequence > MapSync::MAX_COUNTER || epoch > MapSync::MAX_COUNTER || revision > MapSync::MAX_COUNTER
            || !cells.Valid()) return false;
        if (mode == Mode::State) return generation && sequence == 0
            && (epoch ? revision != 0 : revision == 0 && cells.Count() == 0);
        if (mode == Mode::Seed) return generation == 0 && sequence && revision == 0;
        if (mode == Mode::Reveal) return generation == 0 && sequence && epoch && revision == 0 && cells.Count() == 1;
        return false;
    }
private:
    template<class Stream> bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && !Valid()) return false;
        uint8_t wireMode = static_cast<uint8_t>(mode);
        serialize_int(stream, wireMode, 0, 2);
        if (Stream::IsReading) mode = static_cast<Mode>(wireMode);
        serialize_int(stream, playerid, 0, Config::MAX_SERVER_PLAYERS - 1);
        serialize_int(stream, generation, 0, MapSync::MAX_COUNTER);
        serialize_int(stream, sequence, 0, MapSync::MAX_COUNTER);
        serialize_int(stream, epoch, 0, MapSync::MAX_COUNTER);
        serialize_int(stream, revision, 0, MapSync::MAX_COUNTER);
        for (auto& byte : cells.bits) serialize_uint8(stream, byte);
        return Valid();
    }
};
}
