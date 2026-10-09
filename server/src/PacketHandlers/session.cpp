#include "stdafx.h"
#include "CSessionSync.h"
PACKET_HANDLER(ePacketType::SESSION_HELLO, Packets::Session::Hello*, CNetworkPlayer* sender)
{ CSessionSync::Send(sender); }
PACKET_HANDLER(ePacketType::SESSION_SEED, Packets::Session::Seed* packet, CNetworkPlayer* sender)
{
    if (!sender || !sender->m_bIsHost) return;
    if (CSessionSync::Room().Seed(sender->m_iPlayerId, packet->state)) CSessionSync::Broadcast();
    else CSessionSync::Send(sender);
}
PACKET_HANDLER(ePacketType::SESSION_OPERATION, Packets::Session::Transaction* packet, CNetworkPlayer* sender)
{
    if (!sender) return;
    auto receipt = CSessionSync::Room().Apply(sender->m_iPlayerId, packet->op);
    if (receipt.status != SessionSync::Status::Accepted && receipt.status != SessionSync::Status::Rejected)
    { CSessionSync::Send(sender); return; }
    if (receipt.action)
    {
        Packets::Session::CheatAction action;
        action.epoch = packet->op.epoch; action.revision = CSessionSync::Room().state.revision;
        action.incarnation = packet->op.incarnation; action.sequence = packet->op.sequence;
        action.sender = sender->m_iPlayerId; action.cheat = receipt.cheat;
        GetPacketFactory().SendToAll(action);
    }
    CSessionSync::Broadcast();
}
PACKET_HANDLER(ePacketType::SESSION_STATE, Packets::Session::Update*, CNetworkPlayer*) {}
PACKET_HANDLER(ePacketType::SESSION_CHEAT_ACTION, Packets::Session::CheatAction*, CNetworkPlayer*) {}
