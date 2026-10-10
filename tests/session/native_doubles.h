#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <cstring>
using DWORD = uint32_t;
using int32 = int32_t;
static DWORD tickCount = 100;
inline DWORD GetTickCount() { return tickCount; }

class Packet;
struct Transport
{
    std::vector<std::unique_ptr<Packet>> sent;
    template<class T> void RegisterPacket(T* p) { delete p; }
    template<class T> void Send(const T& p);
};
Transport& GetPacketFactory();
#include "network/packet.h"
#include "network/packets/session.h"
#include "CSessionSync.h"
template<class T> void Transport::Send(const T& p) { sent.emplace_back(static_cast<const Packet&>(p).Clone()); }
inline Transport& GetPacketFactory() { static Transport t; return t; }

struct CWanted
{
    unsigned m_nWantedLevel = 0;
    bool m_bPoliceBackOff = false, m_bEverybodyBackOff = false;
    static inline unsigned MaximumWantedLevel = 6, nMaximumWantedLevel = 4620;
    static inline unsigned setterCalls = 0;
    void SetWantedLevel(int level);
    void ClearWantedLevelAndGoOnParole() { m_nWantedLevel = 0; }
    void Reset() { m_nWantedLevel = 0; m_bPoliceBackOff = m_bEverybodyBackOff = false; }
    static void SetMaximumWantedLevel(unsigned value) { MaximumWantedLevel = value; }
};
struct CPed
{
    void* m_pPlayerData = reinterpret_cast<void*>(1);
    void* m_pRwObject = reinterpret_cast<void*>(1);
    float m_fHealth = 100, armor = 0;
    unsigned weaponsCleared = 0;
    void ClearWeapons() { ++weaponsCleared; }
};
static CPed localPed;
static CWanted localWanted;
static bool playerExists = true;
static uint32_t nativePedRef = 1;
inline CPed* FindPlayerPed(int) { return playerExists ? &localPed : nullptr; }
inline CWanted* FindPlayerWanted(int) { return playerExists ? &localWanted : nullptr; }
inline bool IsPedPointerValid(CPed* ped) { return ped != nullptr; }
struct Pool { bool valid = true; bool IsObjectValid(CPed* p) { return valid && p == &localPed; } };
static Pool pedPool;
struct CPools
{
    static inline Pool* ms_pPedPool = &pedPool;
    static uint32_t GetPedRef(CPed*) { return nativePedRef; }
};
struct PlayerInfo { int32_t m_nMoney = 0, m_nDisplayMoney = 0; int m_nPlayerState = 0; };
struct CWorld { static inline std::array<PlayerInfo,1> Players{}; static inline int PlayerInFocus=0; };
struct CCheat
{
    static inline std::array<void(*)(),92> m_aCheatFunctions{};
    static inline std::array<bool,92> m_aCheatsActive{};
    static inline bool m_bHasPlayerCheated = false;
};
inline void CWanted::SetWantedLevel(int level)
{
    ++setterCalls;
    if (CCheat::m_aCheatsActive[65]) return;
    m_nWantedLevel = unsigned(std::min(level, int(MaximumWantedLevel)));
}
struct Peer { uint32_t connectID = 1; };
static Peer peer;
struct CNetwork {
    static inline bool m_bAuthenticated = true;
    static inline Peer* m_pPeer = &peer;
    static void Disconnect() { m_bAuthenticated = false; }
};
struct CLocalPlayer { static inline bool m_bIsHost = true; };
struct RemotePlayer { CPed* m_pPed = nullptr; int GetInternalId() { return 1; } };
struct CNetworkPlayerManager
{
    static inline int m_nMyId = 0;
    static inline std::vector<RemotePlayer*> m_pPlayers;
};
static int gGameState = 9;
static constexpr int MODEL_BRIBE = 1247;
struct CPickups
{
    static bool GivePlayerGoodiesWithPickUpMI(unsigned short model, int player)
    { if (model == MODEL_BRIBE && player == 0 && localWanted.m_nWantedLevel) --localWanted.m_nWantedLevel; return true; }
};
namespace logger { inline void warn(const char*) {} }
namespace patch
{
    static std::vector<std::pair<uintptr_t,std::function<void(CWanted*)>>> wantedHooks;
    template<class T> void SetPointer(uintptr_t, T) {}
    template<class T> void RedirectCall(uintptr_t, T) {}
    inline void RedirectCall(uintptr_t address, void (__fastcall *hook)(CWanted*,void*))
    { wantedHooks.emplace_back(address, [hook](CWanted* wanted) { hook(wanted,nullptr); }); }
}
struct EventPhase
{
    std::vector<std::function<void()>> callbacks;
    template<class T> void operator+=(T callback) { callbacks.emplace_back(callback); }
    void Fire() { for (auto& callback : callbacks) callback(); }
};
struct Event { EventPhase before, after; };
namespace Events { static Event initScriptsEvent, processScriptsEvent; }
static Event gameShutdownEvent;
