#pragma once
#include <array>
#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
// Native positions, events, pools and ENet transport are doubles. Packet classes,
// SenderPlayerId, map contracts and both services are extracted unchanged.
struct CVector2D { float x = 0, y = 0; CVector2D(float a = 0, float b = 0) : x(a), y(b) {} };
struct CVector { float x = 0, y = 0, z = 0; };
struct RegistrationStub { template<class T> void RegisterPacket(T* p) { delete p; } };
RegistrationStub& GetPacketFactory();
#include "network/packets/map.h"
#include "sender.inc"
#include "waypoint.inc"
#include "network/player_vitals.h"
struct ENetPeer {};
struct Ped { CVector pos; void* m_pPlayerData = this; const CVector& GetPosition() { return pos; } };
class CNetworkPlayer
{
public:
    ENetPeer* m_pPeer = nullptr;
    int m_iPlayerId = 0;
    PlayerVitals::Cache m_vitals;
    uint32_t m_mapSequence = 0;
    Packets::Players::PlayerPlaceWaypoint m_waypointState;
};
struct CNetworkPlayerManager
{
    inline static std::vector<CNetworkPlayer*> m_pPlayers;
    inline static int m_nMyId = 0;
    inline static CNetworkPlayer* host = nullptr;
    static CNetworkPlayer* GetPlayer(int id) { for (auto* p : m_pPlayers) if (p->m_iPlayerId == id) return p; return nullptr; }
    static CNetworkPlayer* GetPlayer(ENetPeer* peer) { for (auto* p : m_pPlayers) if (p->m_pPeer == peer) return p; return nullptr; }
    static CNetworkPlayer* GetHost() { return host; }
};
struct Factory : RegistrationStub
{
    std::vector<Packets::Map::Discovery> maps;
    std::vector<Packets::Players::PlayerPlaceWaypoint> waypoints;
    void Send(Packets::Map::Discovery& p, CNetworkPlayer* = nullptr) { maps.push_back(p); }
    void SendToAll(Packets::Map::Discovery& p) { maps.push_back(p); }
    void Send(Packets::Players::PlayerPlaceWaypoint& p) { waypoints.push_back(p); }
    void SendToAll(Packets::Players::PlayerPlaceWaypoint& p, CNetworkPlayer*) { waypoints.push_back(p); }
};
struct CNetwork { inline static bool m_bAuthenticated = true; };
struct CLocalPlayer { inline static bool m_bIsHost = false; };
struct CWorld { inline static int PlayerInFocus = 0; struct Info { Ped* m_pPed = nullptr; }; inline static Info Players[1]; };
struct Pool { bool valid = true; bool IsObjectValid(Ped*) { return valid; } };
struct CPools { inline static Pool* ms_pPedPool = nullptr; };
struct CGame { inline static int currArea = 0; inline static bool outside = true; static bool CanSeeOutSideFromCurrArea() { return outside; } };
struct CTheScripts { inline static bool bPlayerIsOffTheMap = false; };
struct CTheZones { inline static char ExploredTerritoriesArray[100]{}; inline static int TotalNumberExploredTerritories = 0; };
constexpr int MAX_RADAR_TRACES = 175, RADAR_SPRITE_WAYPOINT = 41;
struct RadarTrace { bool m_bInUse = false; int m_nRadarSprite = 0; CVector m_vecPos; };
struct CRadar { inline static RadarTrace ms_RadarTrace[MAX_RADAR_TRACES]; static int GetActualBlipArrayIndex(int handle) { return handle - 1; } };
inline struct MenuManager { int m_nTargetBlipIndex = 0; } FrontEndMenuManager;
inline int gGameState = 9;
inline uint32_t testTick = 6000;
inline uint32_t GetTickCount() { return testTick; }
inline Ped* FindPlayerPed(int) { return CWorld::Players[0].m_pPed; }
struct Event
{
    std::vector<std::function<void()>> hooks;
    template<class F> void operator+=(F hook) { hooks.push_back(hook); }
    void Fire() { for (auto& hook : hooks) hook(); }
};
struct ScriptEvent { Event before, after; };
namespace Events { inline ScriptEvent initScriptsEvent, processScriptsEvent; }
inline ScriptEvent gameShutdownEvent;
#include "client/src/CMapSync.h"
#include "server/src/CMapSync.h"
