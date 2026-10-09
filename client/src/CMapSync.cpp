#include "stdafx.h"
#include "CMapSync.h"
#include <CGame.h>
#include <CTheZones.h>

namespace
{
MapSync::View view;
bool scriptsReady = false, receivedState = false, seedRequested = true, seedSent = false;
bool authorityReady = false;
bool sentWaypoint = false, waypointPlaced = false;
float waypointX = 0, waypointY = 0;
int hostId = -1;
uint32_t hostGeneration = 0, mapSequence = 0, waypointSequence = 0, lastReveal = 0;
bool Ready()
{
    auto* ped = FindPlayerPed(0);
    return CNetwork::m_bAuthenticated && scriptsReady && gGameState == 9 && CWorld::PlayerInFocus == 0
        && ped && CPools::ms_pPedPool && CPools::ms_pPedPool->IsObjectValid(ped)
        && CWorld::Players[0].m_pPed == ped && ped->m_pPlayerData;
}
MapSync::Discovery Capture()
{
    MapSync::Discovery result;
    for (int i = 0; i < MapSync::CELLS; ++i) if (CTheZones::ExploredTerritoriesArray[i]) result.Add(i);
    return result;
}
void Apply()
{
    for (int i = 0; i < MapSync::CELLS; ++i) CTheZones::ExploredTerritoriesArray[i] = view.discovery.Has(i) ? 1 : 0;
    CTheZones::TotalNumberExploredTerritories = view.discovery.Count();
}
}
void CMapSync::Init()
{
    Events::initScriptsEvent.before += []
    {
        // First initialization of a promoted menu guest adopts the room. A
        // later load by an already initialized controlling host starts a campaign.
        seedRequested = !view.epoch || (CLocalPlayer::m_bIsHost && authorityReady);
        scriptsReady = false; seedSent = false; authorityReady = false;
    };
    Events::processScriptsEvent.after += [] { if (gGameState == 9) scriptsReady = true; };
    gameShutdownEvent.before += [] { scriptsReady = false; };
}
void CMapSync::Reset()
{
    view = {}; receivedState = false; seedRequested = true; seedSent = false; authorityReady = false; hostId = -1;
    hostGeneration = mapSequence = waypointSequence = lastReveal = 0;
    sentWaypoint = waypointPlaced = false; waypointX = waypointY = 0;
}
void CMapSync::HostChanged(int host)
{
    hostId = host; hostGeneration = 0; receivedState = false; authorityReady = false;
    // A promoted guest keeps the room's discovery, rather than seeding its own save.
    if (view.epoch) { seedRequested = false; seedSent = false; }
}
void CMapSync::Receive(const Packets::Map::Discovery& packet)
{
    if (!CNetwork::m_bAuthenticated || !packet.Valid() || packet.mode != Packets::Map::Mode::State
        || packet.playerid != hostId) return;
    if (hostId != CNetworkPlayerManager::m_nMyId)
    {
        auto* host = CNetworkPlayerManager::GetPlayer(hostId);
        if (!host || host->m_vitals.generation != packet.generation) return;
    }
    if (hostGeneration && hostGeneration != packet.generation) return;
    hostGeneration = packet.generation; receivedState = true;
    if (!packet.epoch) return;
    if (!view.Accept(packet.epoch, packet.revision, packet.cells)) return;
    if (seedSent) { seedSent = false; seedRequested = false; }
    if (Ready() && (!CLocalPlayer::m_bIsHost || !seedRequested)) Apply();
}
void CMapSync::Process()
{
    if (!Ready()) return;
    // Also observe the native target: covers markers set before connecting and
    // menu/controller paths that do not reach the old mouse-only hook.
    bool place = false; float x = 0, y = 0;
    const int handle = FrontEndMenuManager.m_nTargetBlipIndex;
    const int index = handle ? CRadar::GetActualBlipArrayIndex(handle) : -1;
    if (index >= 0 && index < MAX_RADAR_TRACES && CRadar::ms_RadarTrace[index].m_bInUse
        && CRadar::ms_RadarTrace[index].m_nRadarSprite == RADAR_SPRITE_WAYPOINT)
    {
        place = true; x = CRadar::ms_RadarTrace[index].m_vecPos.x; y = CRadar::ms_RadarTrace[index].m_vecPos.y;
    }
    if (!sentWaypoint || place != waypointPlaced || (place && (x != waypointX || y != waypointY))) SetWaypoint(place, x, y);
    if (!receivedState) return;
    if (CLocalPlayer::m_bIsHost && seedRequested)
    {
        if (!seedSent && mapSequence < MapSync::MAX_COUNTER)
        {
            Packets::Map::Discovery packet;
            packet.mode = Packets::Map::Mode::Seed; packet.playerid = CNetworkPlayerManager::m_nMyId;
            packet.sequence = ++mapSequence; packet.epoch = view.epoch; packet.cells = Capture();
            seedSent = true; GetPacketFactory().Send(packet);
        }
        return;
    }
    if (!view.epoch) return;
    Apply();
    if (CLocalPlayer::m_bIsHost) authorityReady = true;
    const uint32_t now = GetTickCount();
    if (now - lastReveal < 5000 || CGame::currArea != 0 || CTheScripts::bPlayerIsOffTheMap
        || !CGame::CanSeeOutSideFromCurrArea() || mapSequence == MapSync::MAX_COUNTER) return;
    lastReveal = now;
    const auto& position = FindPlayerPed(0)->GetPosition();
    const int cell = MapSync::Cell(position.x, position.y);
    if (cell < 0 || view.discovery.Has(cell)) return;
    Packets::Map::Discovery packet;
    packet.mode = Packets::Map::Mode::Reveal; packet.playerid = CNetworkPlayerManager::m_nMyId;
    packet.sequence = ++mapSequence; packet.epoch = view.epoch; packet.cells.Add(cell);
    GetPacketFactory().Send(packet);
}
void CMapSync::SetWaypoint(bool place, float x, float y)
{
    if (!CNetwork::m_bAuthenticated || waypointSequence == MapSync::MAX_COUNTER
        || !MapSync::ValidWaypoint(place, x, y)) return;
    Packets::Players::PlayerPlaceWaypoint packet;
    packet.playerid = CNetworkPlayerManager::m_nMyId; packet.sequence = ++waypointSequence;
    packet.place = place; packet.position = CVector2D(x, y);
    GetPacketFactory().Send(packet);
    sentWaypoint = true; waypointPlaced = place; waypointX = x; waypointY = y;
}
void CMapSync::ReceiveWaypoint(const Packets::Players::PlayerPlaceWaypoint& packet)
{
    if (!CNetwork::m_bAuthenticated || !packet.Valid()) return;
    auto* player = CNetworkPlayerManager::GetPlayer(packet.playerid.value);
    if (!player || !MapSync::AcceptWaypoint(player->m_vitals.generation, player->m_waypointState.sequence,
        packet.generation, packet.sequence, packet.place, packet.position.x, packet.position.y)) return;
    player->m_waypointState = packet;
}
