#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <vector>
#include "network/session_sync.h"
using uint32 = uint32_t; using int32 = int32_t;
#include "source/third_party/plugin-sdk/plugin_sa/game_sa/eScriptCommands.h"
#include "policy_defs.inc"
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define strnicmp _strnicmp
#define SKIP_EDX void* unusedEdx
struct CRunningScript {
    CRunningScript* m_pNext = nullptr;
    char m_szName[8]{};
    bool m_bIsActive = true, m_bIsExternal = true, m_bIsMission = false;
};
struct Clothes {
    uint32 m_anTextureKeys[17]{}; uint32 model = 0; int writes = 0;
    void SetTextureAndModel(uint32 key, uint32 value, int part) { m_anTextureKeys[part] = key; model = value; ++writes; }
};
struct CPlayerPed {
    void* m_pPlayerData = this; bool nativeEligible = true; int nativeCalls = 0;
    Clothes clothes; int weapon = -1, ammo = 0; float m_fArmour = 0;
    bool IsPlayer(){return true;}
    bool CanPlayerStartMission() { ++nativeCalls; return nativeEligible; }
    Clothes* GetClothesDesc() { return &clothes; }
    void GiveWeapon(int type, int quantity, bool) { weapon = type; ammo += quantity; }
    void SetCurrentWeapon(int type) { weapon = type; }
};
static CPlayerPed localPed, otherPed;
struct PlayerInfo { CPlayerPed* m_pPed = &localPed; int m_nMoney = 1000, m_nDisplayMoney = 1000, m_nMaxArmour = 100; };
struct CWorld { static inline int PlayerInFocus = 0; static inline std::array<PlayerInfo, 2> Players{}; };
inline CPlayerPed* FindPlayerPed(int id = -1) { return CWorld::Players[id < 0 ? CWorld::PlayerInFocus : id].m_pPed; }
inline PlayerInfo& FindPlayerInfo() { return CWorld::Players[CWorld::PlayerInFocus]; }
struct Pool { bool valid = true; bool IsObjectValid(CPlayerPed* ped) { return valid && (ped == &localPed || ped == &otherPed); } };
struct CPools {
    static inline Pool pool; static inline Pool* ms_pPedPool = &pool; static inline bool recycled = false;
    static int GetPedRef(CPlayerPed*) { return 77; }
    static CPlayerPed* GetPed(int) { return recycled ? &otherPed : &localPed; }
};
struct CTheScripts {
    static inline std::array<CRunningScript, 96> storage;
    static inline CRunningScript* ScriptsArray = storage.data();
    static inline CRunningScript* pActiveScripts = nullptr;
    static int GetActualScriptThingIndex(int,int){return 0;}
    static inline unsigned OnAMissionFlag = 1;
    static inline std::array<uint8_t, 200000> ScriptSpace{};
};
struct Peer{uint32_t connectID=1;};static Peer peer;
struct CNetwork {static inline Peer*m_pPeer=&peer; static inline bool m_bAuthenticated = true,m_bConnected=true; };
struct CNetworkPlayerManager{static inline int m_nMyId=1;};
struct CLocalPlayer { static inline bool m_bIsHost = false; };
namespace logger { template<class... T> void warn(const char*, T...) {} }
struct CAudioEngine {};
struct Stats { static inline int nativeAudioLoads = 0, nativeAudioPlays = 0, taskCapture = 0, opcodeSends = 0, audioSends = 0; };
namespace plugin { template<int Address, class... T> void CallMethod(T...) { if (Address == 0x507290) ++Stats::nativeAudioLoads; if (Address == 0x5072B0) ++Stats::nativeAudioPlays; } }
struct OpParam { int value = 0; };
struct COpCodeSync {
    static inline OpParam scriptParamsBuffer[15]; static inline bool bProcessingNetworkOpcode = false;
    static inline bool ms_bSyncingEnabled = true;
    static inline uint32_t ms_iFreeSyncedScript = 0;static inline char ms_aszSyncedScripts[256][9]{};
    static bool IsOpcodeSyncable(int,int* idx = nullptr,bool ignoreOpCodeSync = false);
    static std::vector<uint8_t> SerializeOpcode(int, int& size) { size = 4; return {1,2,3,4}; }
    static CRunningScript* GetActiveScript();
};
struct CTaskSequenceSync {static bool IsOpCodeTaskSynced(eScriptCommands); static bool OnOpCodeExecuted(eScriptCommands);};
struct CTaskSequences {static inline int ms_iActiveSequence=-1;static constexpr int NUM_SEQUENCES=64,NUM_TASKS=8;};
static std::vector<uint8_t> m_serializedSequences[64][8];
constexpr int SCRIPT_THING_SEQUENCE_TASK=1;
struct CNetworkPedManager{struct Row{int m_nPedId=1;};static Row*GetPed(CPlayerPed*){static Row row;return &row;}};
static bool m_bSequenceOpened=false;
inline void OpenSequence(){++Stats::taskCapture;}inline void CloseSequence(){++Stats::taskCapture;}inline void ClearSequence(){++Stats::taskCapture;}void PerformSequence();inline void AddNewTask(eScriptCommands){++Stats::taskCapture;}
struct CEntryExitMarkerSync { static inline bool ms_bUpdateAfterProcessingScripts = false; };
struct CCutsceneVotes { static void ObserveOpcode(uint16_t, bool) {} };
struct CNetworkObjectManager { static void ObserveOpcode(uint16_t, const int*, int, int) {} };
namespace ObjectSync { inline bool IsObjectOpcode(uint16_t) { return false; } inline bool IsSnapshotOnlyOpcode(uint16_t) { return false; } inline bool ValidOpcode(const uint8_t*, size_t) { return true; } }
static int ScriptParams[15]{};
constexpr int NUM_SYNCED_PARAMS = 15;
static uint8_t textLengthBuffer[15]; static char textParamBuffer[15][256];
static uint16_t scriptParamCount = 0, textParamCount = 0;
static uint32_t lastOpCodeProcessed = 0; static CRunningScript* lastProcessedScript = nullptr; static bool activeOpcodeScope = false;
namespace Packets { namespace Scripts {
struct OpCodeSync { int size = 0; uint8_t buffer[4096]{}; };
struct PerformTaskSequence{int size=0;uint8_t buffer[4096]{};};
struct PlayMissionAudio { int audioid = 0; uint8_t slotid = 0; };
} namespace Session { struct Transaction { SessionSync::Operation op; }; } }
struct Transport {
    std::vector<SessionSync::Operation> money;int sequenceSends=0;
    void Send(const Packets::Scripts::PerformTaskSequence&){++sequenceSends;}
    void Send(const Packets::Session::Transaction& packet) { money.push_back(packet.op); }
    void Send(const Packets::Scripts::OpCodeSync&) { ++Stats::opcodeSends; }
    void Send(const Packets::Scripts::PlayMissionAudio&) { ++Stats::audioSends; }
};
inline Transport& GetPacketFactory() { static Transport transport; return transport; }
static SessionSync::Client g_client;
static SessionSync::MoneyObservation g_money;
static int64_t g_unsentMoney = 0; static int g_suppress = 0; static bool g_seedSent = false;
static bool g_authenticated=true;static uint32_t g_connection=1;
static bool g_scriptsCompleted = true; static uint32_t g_gameGeneration = 1; static int gGameState = 9;
inline bool EnsureSession() { return CNetwork::m_bAuthenticated; }
#include "wallet.inc"
struct CSessionSync {static bool IsWalletReadyForLocalService(); static bool NeedsOpcodeCapture(uint16_t);static bool ConsumeOpcode(uint16_t,const int*,int); };
#include "host_gate.inc"
#include "service_decl.h"
#if __has_include("service.inc")
#include "service.inc"
#endif
#include "script_gate.inc"
#include "live_scope.inc"
#include "perform_sequence.inc"
#include "sync_policy.inc"
#include "send.inc"
#include "audio.inc"

// Native Buy is compiled unchanged; these are only its dependent engine resources.
using eClothesTexturePart = int; using eWeaponType = int;
enum { WEAPON_FLOWERS = 14, WEAPON_ARMOUR = 48 };
enum { CLOTHES_MODEL_TORSO, CLOTHES_MODEL_LEGS, CLOTHES_MODEL_HEAD, CLOTHES_MODEL_GLASSES, CLOTHES_MODEL_HATS };
enum { DOOR_BONNET, DOOR_BOOT, CAR_WHEEL_RF, CAR_BUMP_FRONT, CAR_BUMP_REAR, CAR_BONNET, CAR_BOOT, CAR_WHEEL_FRONT_LEFT, CAR_WHEEL_FRONT_RIGHT, CAR_WHEEL_REAR_LEFT, CAR_WHEEL_REAR_RIGHT, FRONT_BUMPER, REAR_BUMPER };
enum { STAT_TOTAL_SHOPPING_BUDGET, STAT_CAR_MODIFICATION_BUDGET, STAT_FASHION_BUDGET, STAT_HAIRDRESSING_BUDGET, STAT_TATTOO_BUDGET, STAT_FOOD_BUDGET, STAT_NUMBER_OF_MEALS_EATEN, STAT_WEAPON_BUDGET };
struct CStats { static inline std::array<float, 8> values{}; static void ModifyStat(int index, float value) { values[index] += value; } };
struct CClothes { static int GetTextureDependency(int) { return CLOTHES_MODEL_HEAD; } };
struct CWanted {static inline unsigned MaximumWantedLevel=6;unsigned m_nWantedLevel=0;bool m_bPoliceBackOff=false,m_bEverybodyBackOff=false;void ClearWantedLevelAndGoOnParole(){m_nWantedLevel=0;} };
static CWanted wanted;inline CWanted* FindPlayerWanted(int=-1){return &wanted;}
static int g_lastWanted=-1;
#include "consume_money.inc"
struct Model { int CarMod = 0; bool bUsesVehDummy = false; struct Component { int m_nParentComponentId = 0; }; struct Structure { Component m_aUpgrades[10]; } structure; Structure* m_pVehicleStruct = &structure; Model* AsVehicleModelInfoPtr() { return this; } };
struct CModelInfo { static Model* GetModelInfo(uint32) { static Model model; return &model; } };
struct Vehicle { void AddVehicleUpgrade(uint32) {} Model* GetModelInfo() { return CModelInfo::GetModelInfo(0); } bool IsAutomobile() { return true; } Vehicle* AsAutomobile() { return this; } void FixTyre(int) {} void FixPanel(int,int) {} void FixDoor(int,int) {} };
inline Vehicle* FindPlayerVehicle() { static Vehicle vehicle; return &vehicle; }
static bool gClothesHaveBeenStored = false; static Clothes gStoredClothesState;
#include "price_sections.h"
struct CShopping {
    struct Price { int price = 100; struct { uint32 modelKey = 5, type = 0; } clothes; struct { uint32 type1 = 2; } tattoos; struct { uint32 ammo = 30; } weapon; };
    struct Modifiers { struct { int statIndex = 0, change = 0; } modifiers[2]; };
    static inline std::array<Price, 1> ms_prices;
    static inline std::array<Modifiers, 1> ms_statModifiers;
    static inline std::array<bool, 1> ms_bHasBought{};
    static inline ePriceSection ms_priceSectionLoaded = PRICE_SECTION_HAIRCUTS;
    static int GetItemIndex(uint32) { return 0; } static int FindItem(uint32) { return 0; }
    static int GetPrice(uint32) { return ms_prices[0].price; }
    static void IncrementStat(int,int) {} static void UpdateStats(int,bool) {}
    static void Buy(uint32, int32);
};
#include "shopping.inc"
static int checks = 0, failures = 0;
inline void expect(bool ok, const char* message) { ++checks; if (!ok) { ++failures; std::cout << "FAIL: " << message << '\n'; } }
