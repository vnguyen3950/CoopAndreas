#include "stdafx.h"
#include "CCutsceneVotes.h"
PACKET_HANDLER(ePacketType::CUTSCENE_VOTE_BEGIN, Packets::Cutscene::Begin* packet)
{ CCutsceneVotes::ReceiveBegin(*packet); }
PACKET_HANDLER(ePacketType::CUTSCENE_VOTE_STATE, Packets::Cutscene::Update* packet)
{ CCutsceneVotes::ReceiveState(*packet); }
PACKET_HANDLER(ePacketType::CUTSCENE_VOTE_COMMIT, Packets::Cutscene::Commit* packet)
{ CCutsceneVotes::ReceiveCommit(*packet); }
