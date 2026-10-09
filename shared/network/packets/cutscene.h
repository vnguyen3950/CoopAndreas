#pragma once
#include "network/packet.h"
#include "network/cutscene_votes.h"

namespace Packets::Cutscene {
template<class Stream> bool State(Stream& stream, CutsceneVotes::Snapshot& s) {
    serialize_uint64(stream, s.generation); serialize_uint64(stream, s.serial);
    serialize_uint64(stream, s.identity);
    int host = s.host; serialize_int(stream, host, -1, CutsceneVotes::MaxPeers - 1); s.host = int8_t(host);
    for (char& c : s.name) { uint8_t byte = uint8_t(c); serialize_uint8(stream, byte); c = char(byte); }
    serialize_uint8(stream, s.total); serialize_uint8(stream, s.votes);
    uint8_t phase = uint8_t(s.phase); serialize_uint8(stream, phase); s.phase = CutsceneVotes::Phase(phase);
    serialize_bool(stream, s.eligible); serialize_bool(stream, s.voted);
    return s.Valid();
}
class Begin : public Packet {
    DEFINE_PACKET_TYPE(Begin, ePacketType::CUTSCENE_VOTE_BEGIN, ePacketChannel::SCRIPT);
public:
    // Host requests have generation/identity zero. Server announcements contain a personalized snapshot.
    bool active = true;
    CutsceneVotes::Snapshot state{};
private:
    template<class Stream> bool Serialize(Stream& stream) { serialize_bool(stream, active); return State(stream, state); }
};
class Vote : public Packet {
    DEFINE_PACKET_TYPE(Vote, ePacketType::CUTSCENE_VOTE, ePacketChannel::SCRIPT);
public:
    CutsceneVotes::Token generation = 0, identity = 0;
private:
    template<class Stream> bool Serialize(Stream& stream) {
        serialize_uint64(stream, generation); serialize_uint64(stream, identity);
        return generation && identity;
    }
};
class Update : public Packet {
    DEFINE_PACKET_TYPE(Update, ePacketType::CUTSCENE_VOTE_STATE, ePacketChannel::SCRIPT);
public: CutsceneVotes::Snapshot state{};
private: template<class Stream> bool Serialize(Stream& stream) { return State(stream, state); }
};
class Commit : public Packet {
    DEFINE_PACKET_TYPE(Commit, ePacketType::CUTSCENE_VOTE_COMMIT, ePacketChannel::SCRIPT);
public: CutsceneVotes::Snapshot state{};
private: template<class Stream> bool Serialize(Stream& stream) {
        return State(stream, state) && state.phase == CutsceneVotes::Phase::Committed;
    }
};
}
