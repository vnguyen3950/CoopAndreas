#include "stdafx.h"
#include "CMapSync.h"

namespace
{
MapSync::Room room;
bool Authenticated(CNetworkPlayer* player)
{
    return player && player->m_pPeer && player->m_vitals.generation
        && CNetworkPlayerManager::GetPlayer(player->m_pPeer) == player
        && CNetworkPlayerManager::GetPlayer(player->m_iPlayerId) == player;
}
Packets::Map::Discovery Snapshot(CNetworkPlayer* host)
{
    Packets::Map::Discovery packet;
    packet.playerid = host->m_iPlayerId; packet.generation = host->m_vitals.generation;
    packet.epoch = room.epoch; packet.revision = room.revision; packet.cells = room.discovery;
    return packet;
}
}
bool CMapSyncServer::Receive(const Packets::Map::Discovery& packet, CNetworkPlayer* sender)
{
    auto* host = CNetworkPlayerManager::GetHost();
    if (!Authenticated(sender) || !Authenticated(host) || !packet.Valid()
        || packet.playerid != sender->m_iPlayerId || packet.sequence <= sender->m_mapSequence
        || packet.mode == Packets::Map::Mode::State) return false;
    bool changed = false;
    if (packet.mode == Packets::Map::Mode::Seed)
    {
        if (sender != host || !room.Seed(packet.epoch, packet.cells)) return false;
        changed = true;
    }
    else
    {
        if (!room.epoch || packet.epoch != room.epoch) return false;
        // An already revealed cell consumes the owner's sequence idempotently.
        changed = room.Reveal(packet.epoch, packet.cells);
        if (!changed && room.revision == MapSync::MAX_COUNTER) return false;
    }
    sender->m_mapSequence = packet.sequence;
    auto state = Snapshot(host);
    if (changed) GetPacketFactory().SendToAll(state);
    else GetPacketFactory().Send(state, sender);
    return true;
}
void CMapSyncServer::Replay(CNetworkPlayer* recipient)
{
    auto* host = CNetworkPlayerManager::GetHost();
    if (Authenticated(recipient) && Authenticated(host))
    { auto packet = Snapshot(host); GetPacketFactory().Send(packet, recipient); }
}
void CMapSyncServer::HostChanged()
{
    auto* host = CNetworkPlayerManager::GetHost();
    if (Authenticated(host)) { auto packet = Snapshot(host); GetPacketFactory().SendToAll(packet); }
}
void CMapSyncServer::Leave(CNetworkPlayer* departing)
{
    if (CNetworkPlayerManager::m_pPlayers.size() == 1
        && CNetworkPlayerManager::m_pPlayers.front() == departing) room.Clear();
}
bool CMapSyncServer::Waypoint(Packets::Players::PlayerPlaceWaypoint packet, CNetworkPlayer* sender)
{
    // SenderPlayerId has no C2S bytes. Assign it from the authenticated peer.
    if (!Authenticated(sender)) return false;
    packet.playerid.value = sender->m_iPlayerId;
    if (packet.generation != 0
        || !packet.Valid() || packet.sequence <= sender->m_waypointState.sequence) return false;
    packet.generation = sender->m_vitals.generation;
    sender->m_waypointState = packet;
    GetPacketFactory().SendToAll(packet, sender);
    return true;
}
