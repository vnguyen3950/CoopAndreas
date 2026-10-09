#include "stdafx.h"
#include "CNetworkObjectManager.h"

PACKET_HANDLER(ePacketType::OBJECT_CREATE, Packets::Objects::Create* packet, CNetworkPlayer* sender)
{
    if (!sender || !sender->m_bIsHost || !packet->state.Valid()) return;
    auto token = packet->id;
    auto id = CNetworkObjectManager::Registry().Create(sender->m_iPlayerId, token, packet->revision, packet->state);
    if (!id) return;
    Packets::Objects::Confirm confirm; confirm.token = token; confirm.id = id;
    GetPacketFactory().Send(confirm, sender);
    packet->id = id;
    packet->serverTime = 0;
    GetPacketFactory().SendToAll(*packet, sender);
}
PACKET_HANDLER(ePacketType::OBJECT_UPDATE, Packets::Objects::Update* packet, CNetworkPlayer* sender)
{
    if (!sender || !sender->m_bIsHost || !packet->state.Valid()) return;
    auto& registry = CNetworkObjectManager::Registry();
    if (!registry.Update(sender->m_iPlayerId, packet->id, packet->revision, packet->state)) return;
    packet->id = registry.ByToken(sender->m_iPlayerId, packet->id)->id;
    packet->serverTime = 0;
    GetPacketFactory().SendToAll(*packet, sender);
}
PACKET_HANDLER(ePacketType::OBJECT_REMOVE, Packets::Objects::Remove* packet, CNetworkPlayer* sender)
{
    if (!sender || !sender->m_bIsHost) return;
    auto id = CNetworkObjectManager::Registry().Remove(sender->m_iPlayerId, packet->id);
    if (!id) return;
    packet->id = id;
    packet->serverTime = 0;
    GetPacketFactory().SendToAll(*packet, sender);
}
PACKET_HANDLER(ePacketType::OBJECT_RESYNC, Packets::Objects::Resync*, CNetworkPlayer* sender)
{
    if (!sender) return;
    for (const auto& item : CNetworkObjectManager::Registry().records)
    {
        const auto& record = item.second;
        if (record.owner == sender->m_iPlayerId) continue;
        Packets::Objects::Create packet;
        packet.id = record.id; packet.revision = record.revision; packet.state = record.state;
        GetPacketFactory().Send(packet, sender);
    }
}
// A peer cannot submit server-only confirmation or authoritative hit packets.
PACKET_HANDLER(ePacketType::OBJECT_CONFIRM, Packets::Objects::Confirm*, CNetworkPlayer*) {}
PACKET_HANDLER(ePacketType::OBJECT_HIT, Packets::Objects::Hit*, CNetworkPlayer*) {}
