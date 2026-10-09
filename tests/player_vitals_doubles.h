#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
class Packet;
class CNetworkPlayer;
struct Transport
{
    std::vector<std::unique_ptr<Packet>> sent;
    template<class T> void RegisterPacket(T* p) { delete p; }
    template<class T> void Send(const T& p, CNetworkPlayer* = nullptr);
    template<class T> void SendToAll(const T& p, CNetworkPlayer* = nullptr) { Send(p); }
};
Transport& GetPacketFactory();
#include "network/packets/vitals.h"
#include "client/src/CPlayerVitalsSync.h"
#include "server/src/CPlayerVitalsSync.h"
template<class T> void Transport::Send(const T& p, CNetworkPlayer*) { sent.emplace_back(static_cast<const Packet&>(p).Clone()); }
inline Transport& GetPacketFactory() { static Transport transport; return transport; }

using byte = uint8_t;
struct CPlayerData { float m_fBreath = 1000; };
class CPlayerPed
{
public:
    CPlayerData* m_pPlayerData = nullptr;
    float m_fMaxHealth = 100, m_fHealth = 100, m_fArmour = 0;
    struct { bool bSubmergedInWater = false; } m_nPhysicalFlags;
};
struct PlayerInfo { CPlayerPed* m_pPed = nullptr; uint8_t m_nMaxHealth = 100; };
struct CWorld
{
    static inline int PlayerInFocus = 0;
    static inline std::array<PlayerInfo,10> Players{};
};
struct Peer { uint32_t connectID = 10; };
struct CNetwork { static inline bool m_bAuthenticated = true; static inline Peer* m_pPeer = nullptr; };
class CNetworkPlayer
{
public:
    int m_iPlayerId = 0, m_nPedRef = -1;
    Peer* m_pPeer = nullptr;
    CPlayerPed* m_pPed = nullptr;
    PlayerVitals::Cache m_vitals;
    int GetInternalId(); // Definition extracted from actual CNetworkPlayer.cpp.
};
struct CNetworkPlayerManager
{
    static inline int m_nMyId = 0;
    static inline std::vector<CNetworkPlayer*> m_pPlayers;
    static void RemoveById(int id);
    static CNetworkPlayer* GetPlayer(int id)
    { for (auto* p : m_pPlayers) if (p && p->m_iPlayerId == id) return p; return nullptr; }
    static CNetworkPlayer* GetPlayer(CPlayerPed* ped)
    { for (auto* p : m_pPlayers) if (p && p->m_pPed == ped) return p; return nullptr; }
    static CNetworkPlayer* GetPlayer(Peer* peer)
    { for (auto* p : m_pPlayers) if (p && p->m_pPeer == peer) return p; return nullptr; }
};
static unsigned nativeReads = 0, statReads = 0;
struct Pool
{
    std::set<CPlayerPed*> live;
    bool IsObjectValid(CPlayerPed* ped) { ++nativeReads; return live.count(ped) != 0; }
};
static Pool nativePool;
struct CPools
{
    static inline Pool* ms_pPedPool = &nativePool;
    static inline std::map<int,CPlayerPed*> references;
    static CPlayerPed* GetPed(int ref) { ++nativeReads; auto p = references.find(ref); return p == references.end() ? nullptr : p->second; }
    static int GetPedRef(CPlayerPed* ped) { ++nativeReads; for (auto p : references) if (p.second == ped) return p.first; return -1; }
};
inline CPlayerPed* FindPlayerPed(int id) { ++nativeReads; return CWorld::Players[id].m_pPed; }
static float localCapacity = 1150;
static std::array<float,83> globalFloatStats{};
static std::array<int,224> globalIntStats{};
constexpr int STAT_MOD_AIR_IN_LUNG = 8;
struct CStats
{
    static float GetFatAndMuscleModifier(int)
    { ++statReads; return localCapacity; } // Recorded native getter result; no replacement stat algorithm.
};
static int gGameState = 9;
static uint32_t tick = 100;
inline uint32_t GetTickCount() { return tick; }
struct Phase
{
    std::vector<std::function<void()>> callbacks;
    template<class T> void operator+=(T callback) { callbacks.emplace_back(callback); }
    void Fire() { for (auto& callback : callbacks) callback(); }
};
struct Event { Phase before, after; };
namespace Events { static Event initScriptsEvent, processScriptsEvent; }
static Event gameShutdownEvent;

#include "client/src/UI/CNetworkPlayerList.h"
struct CRGBA { uint8_t r,g,b,a; CRGBA(uint8_t x,uint8_t y,uint8_t z,uint8_t alpha) : r(x),g(y),b(z),a(alpha) {} };
#include "vitals_hud_colors.inc"
struct ColorTable { CRGBA GetRGBA(int id) { return CRGBA(uint8_t(id),0,0,255); } };
static ColorTable HudColour;
struct CUtil { static float HUD_X(float x) { return x; } static float HUD_Y(float y) { return y; } };
struct Bar { float progress; uint8_t color; };
static std::vector<Bar> bars;
struct CSprite2d
{
    static void DrawBarChart(float,float,uint16_t,uint8_t,float progress,int,int,int,CRGBA color,CRGBA)
    { bars.push_back({progress,color.r}); }
};
