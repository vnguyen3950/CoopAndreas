#include "stdafx.h"
#include "CCutsceneVotes.h"
PACKET_HANDLER(ePacketType::CUTSCENE_VOTE_BEGIN, Packets::Cutscene::Begin* packet, CNetworkPlayer* sender)
{ CCutsceneVotes::Begin(*packet, sender); }
PACKET_HANDLER(ePacketType::CUTSCENE_VOTE, Packets::Cutscene::Vote* packet, CNetworkPlayer* sender)
{ CCutsceneVotes::Vote(*packet, sender); }
// Clients cannot announce totals or manufacture a skip commit.
PACKET_HANDLER(ePacketType::CUTSCENE_VOTE_STATE, Packets::Cutscene::Update*, CNetworkPlayer*) {}
PACKET_HANDLER(ePacketType::CUTSCENE_VOTE_COMMIT, Packets::Cutscene::Commit*, CNetworkPlayer*) {}
