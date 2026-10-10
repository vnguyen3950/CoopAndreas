#pragma once
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include <cstddef>

// SDK field definitions are extracted verbatim into native_fields.inc.
// Methods below are explicit non-native doubles; no fixed-address dispatch exists.
template<class... T> bool serialize_bool(T&&...);
template<class... T> bool serialize_int(T&&...);
template<class... T> bool serialize_uint8(T&&...);
template<class... T> bool serialize_object(T&&...);
#include "native_fields.inc"
static_assert(sizeof(SEntryExitFlags) == 2, "Recorded SDK flags occupy a WORD");
struct Packet { uint32_t serverTime = 0; };
#define DEFINE_PACKET_TYPE(...)
#include "packet_fields.inc"

struct CVector { float x = 0, y = 0, z = 0; };
struct CRect { float left = 0, bottom = 0, right = 0, top = 0; };
struct CEntryExit { CRect m_recEntrance; SEntryExitFlags m_nFlags; uint8_t m_nArea = 0; };
struct CPlayerPed {
    void* m_pPlayerData = this;
    uint8_t m_nAreaCode = 0;
    CVector position{};
    bool eligible = true;
    int positionCalls = 0, eligibilityCalls = 0;
    CVector GetPosition() { ++positionCalls; return position; }
    bool CanPlayerStartMission() { ++eligibilityCalls; return eligible; }
};
inline CPlayerPed localPed, otherPed;
struct CWorld {
    struct Player { CPlayerPed* m_pPed = nullptr; };
    static inline std::array<Player, 2> Players{};
    static inline unsigned char PlayerInFocus = 0;
};
inline CPlayerPed* FindPlayerPed(int slot = 0) { return CWorld::Players[slot].m_pPed; }
struct PedPool { bool valid = true; bool IsObjectValid(CPlayerPed* p) { return valid && p == &localPed; } };
struct CPools {
    static inline PedPool pool;
    static inline PedPool* ms_pPedPool = &pool;
    static inline int reference = 100;
    static inline CPlayerPed* mapped = &localPed;
    static int GetPedRef(CPlayerPed*) { return reference; }
    static CPlayerPed* GetPed(int) { return mapped; }
};
struct CNetwork { static inline bool m_bConnected = true, m_bAuthenticated = true; };
inline int gGameState = 9;
struct CLocalPlayer { static inline bool m_bIsHost = false; };
struct CNetworkPlayerManager { static inline int m_nMyId = 1; };
struct CGame { static inline unsigned char currArea = 0; };
struct CCutsceneMgr { static inline bool ms_cutsceneProcessing = false; };
struct CGameLogic {
    static bool IsCoopGameGoingOn() { return CWorld::Players[0].m_pPed && CWorld::Players[1].m_pPed; }
};
struct CReplay { static inline unsigned char Mode = 0; };
struct CSessionSync { static inline bool ready = true; static bool IsWalletReadyForLocalService() { return ready; } };
struct CTheScripts { static inline int OnAMissionFlag = 1; static inline std::array<char, 200000> ScriptSpace{}; };
struct CPad {
    char prefix[0x10e]{};
    unsigned short DisablePlayerControls = 0;
    static CPad* GetPad(int) { return pad; }
    static CPad* pad;
};
static_assert(offsetof(CPad, DisablePlayerControls) == 0x10e, "Verified disk native WORD control cell");
inline CPad localPad;
inline CPad* CPad::pad = &localPad;
struct CEntryExitManager {
    struct EntryPool : std::vector<CEntryExit*> {
        using std::vector<CEntryExit*>::operator=;
        bool present = true;
        explicit operator bool() const { return present; }
    };
    static inline EntryPool mp_poolEntryExits;
    static inline bool ms_bDisabled = false, ms_bBurglaryHousesEnabled = false;
    static inline int ms_exitEnterState = 0;
};
struct CPacketBuffer { std::deque<Packet*> m_packets; };
inline CPacketBuffer packetBuffer;
inline CPacketBuffer& GetPacketBuffer() { return packetBuffer; }
inline uint32_t g_serverTime = 0;
struct CEntryExitDiagnostics { static void Process(); };
struct CEntryExitMarkerSync {
    static inline Packets::Scripts::EnExSync ms_lastData;
    static void Receive(const Packets::Scripts::EnExSync&);
};

struct GateSnapshot {
    bool disabled, burglary, cutscene, eligible, wallet, auth, connected;
    unsigned short controls;
    unsigned char replay, focus, area, pedArea;
    int transition, reference, mission, game;
    CPlayerPed* player0; CPlayerPed* player1; CPlayerPed* mapping;
    std::vector<unsigned short> flags;
    bool operator==(const GateSnapshot& b) const {
        return disabled==b.disabled && burglary==b.burglary && cutscene==b.cutscene && eligible==b.eligible
            && wallet==b.wallet && auth==b.auth && connected==b.connected && controls==b.controls
            && replay==b.replay && focus==b.focus && area==b.area && pedArea==b.pedArea
            && transition==b.transition && reference==b.reference && mission==b.mission && game==b.game
            && player0==b.player0 && player1==b.player1 && mapping==b.mapping && flags==b.flags;
    }
};
inline GateSnapshot Snapshot() {
    GateSnapshot s{CEntryExitManager::ms_bDisabled, CEntryExitManager::ms_bBurglaryHousesEnabled,
        CCutsceneMgr::ms_cutsceneProcessing, localPed.eligible, CSessionSync::ready,
        CNetwork::m_bAuthenticated, CNetwork::m_bConnected, localPad.DisablePlayerControls,
        CReplay::Mode, CWorld::PlayerInFocus, CGame::currArea, localPed.m_nAreaCode,
        CEntryExitManager::ms_exitEnterState, CPools::reference, CTheScripts::ScriptSpace[1], gGameState,
        CWorld::Players[0].m_pPed, CWorld::Players[1].m_pPed, CPools::mapped, {}};
    for (auto* e : CEntryExitManager::mp_poolEntryExits) { unsigned short f; std::memcpy(&f, &e->m_nFlags, 2); s.flags.push_back(f); }
    return s;
}
