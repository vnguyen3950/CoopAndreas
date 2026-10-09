#include "stdafx.h"
#include "CNetworkObjectManager.h"
#include "network/eNetworkEntityType.h"

ObjectSync::Registry& CNetworkObjectManager::Registry()
{
    static ObjectSync::Registry registry;
    return registry;
}

void CNetworkObjectManager::RemoveOwner(CNetworkPlayer* player)
{
    if (!player) return;
    for (auto id : Registry().RemoveOwner(player->m_iPlayerId))
    {
        Packets::Objects::Remove packet; packet.id = id;
        GetPacketFactory().SendToAll(packet, player);
    }
    Registry().Disconnect(player->m_iPlayerId);
}

bool CNetworkObjectManager::RemapOpcode(uint8_t* buffer, int size, CNetworkPlayer* sender)
{
    if (!sender || !sender->m_bIsHost || size < 0 || !ObjectSync::ValidOpcode(buffer, size)) return false;
    uint32_t parameter; std::memcpy(&parameter, buffer + 4, sizeof parameter);
    if ((parameter & 15) != 4 || (parameter >> 4) > ObjectSync::MAX_ID) return false;
    auto* record = Registry().ByToken(sender->m_iPlayerId, parameter >> 4);
    if (!record) return false;
    parameter = (record->id << 4) | 4;
    std::memcpy(buffer + 4, &parameter, sizeof parameter);
    return true;
}

void CNetworkObjectManager::RouteHit(const Packets::Players::PlayerBulletShot& shot, CNetworkPlayer* sender)
{
    if (!sender || shot.hitEntity.entityType != NETWORK_ENTITY_TYPE_OBJECT) return;
    auto* record = Registry().ById(shot.hitEntity.entityId);
    if (!record || shot.iWeaponType < 22 || shot.iWeaponType > 34) return;
    auto* host = CNetworkPlayerManager::GetPlayer(record->owner);
    if (!host || !host->m_bIsHost || host == sender) return;
    Packets::Objects::Hit hit;
    hit.id = record->id; hit.playerId = sender->m_iPlayerId; hit.weapon = shot.iWeaponType;
    hit.origin = {shot.startPos.x, shot.startPos.y, shot.startPos.z};
    hit.impact = {shot.endPos.x, shot.endPos.y, shot.endPos.z};
    if (!hit.origin.Valid(20000) || !hit.impact.Valid(20000)) return;
    // Native collision/weapon validation is performed by the authority client.
    // SCRIPT delivery orders this after the server's creation confirmation.
    GetPacketFactory().Send(hit, host);
}
