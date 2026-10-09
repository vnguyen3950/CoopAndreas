#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include "config.h"
using byte = uint8_t;
class CEntity {};
class CPlayerPed;
static std::map<int,CPlayerPed*> references;
static std::set<CPlayerPed*> live;
static unsigned destroyed = 0, removed = 0, nativeReads = 0, creates = 0, warps = 0;
static unsigned wrapperDeletes = 0;
static std::function<void()> duringDestruction;
static std::thread::id destructionThread;
struct CVector { float x=0,y=0,z=0; CVector(float a=0,float b=0,float c=0):x(a),y(b),z(c){} };
struct CPedClothesDesc { void SetTextureAndModel(const char*,const char*,int) {} };
struct CPlayerData { CPedClothesDesc* m_pPedClothesDesc = nullptr; };
class CPlayerPed : public CEntity
{
public:
    CPlayerData* m_pPlayerData = nullptr;
    void* m_pVehicle = nullptr;
    bool vtableValid = true;
    CPlayerPed() { m_pPlayerData = new CPlayerData; m_pPlayerData->m_pPedClothesDesc = new CPedClothesDesc; }
    virtual ~CPlayerPed()
    {
        ++destroyed; destructionThread = std::this_thread::get_id();
        live.erase(this);
        for(auto it=references.begin();it!=references.end();) { if(it->second==this) it=references.erase(it); else ++it; }
        if(duringDestruction) duringDestruction();
        if(m_pPlayerData) { delete m_pPlayerData->m_pPedClothesDesc; delete m_pPlayerData; }
    }
    bool IsVTableValid() { ++nativeReads; return vtableValid; }
    void SetOrientation(float,float,float) { ++nativeReads; }
};
struct Info { CPlayerPed* m_pPed = nullptr; };
struct CWorld
{
    static inline std::array<Info,10> Players{};
    static void Remove(CPlayerPed*) { ++removed; }
};
struct Pool { bool IsObjectValid(CPlayerPed* p) { return live.count(p)!=0; } };
static Pool pool;
struct CPools
{
    static inline Pool* ms_pPedPool = &pool;
    static CPlayerPed* GetPed(int ref) { auto p=references.find(ref); return p==references.end()?nullptr:p->second; }
    static int GetPedRef(CPlayerPed* p) { for(auto entry:references)if(entry.second==p)return entry.first; return -1; }
};
enum NativeFailure { NONE, CREATE_FAILURE, GET_FAILURE, GET_NULL };
static NativeFailure failure = NONE;
static int nextReference = 100;
namespace Commands { enum { CREATE_PLAYER,GET_PLAYER_CHAR,SET_CHAR_PROOFS,WARP_CHAR_FROM_CAR_TO_COORD }; }
template<int> struct RecordedCommand;
template<> struct RecordedCommand<Commands::CREATE_PLAYER>
{
    static void Run(int slot,float,float,float,unsigned* output)
    {
        ++creates;
        if(failure==CREATE_FAILURE)return;
        auto* ped=new CPlayerPed; references[++nextReference]=ped; live.insert(ped); CWorld::Players[slot].m_pPed=ped;
        *output=unsigned(slot);
    }
};
template<> struct RecordedCommand<Commands::GET_PLAYER_CHAR>
{
    static void Run(int slot,unsigned* output)
    {
        if(failure==GET_FAILURE)return;
        *output=failure==GET_NULL?999999:unsigned(CPools::GetPedRef(CWorld::Players[slot].m_pPed));
    }
};
template<> struct RecordedCommand<Commands::SET_CHAR_PROOFS> { template<class... T>static void Run(T...){} };
template<> struct RecordedCommand<Commands::WARP_CHAR_FROM_CAR_TO_COORD> { template<class... T>static void Run(T...){++warps;} };
namespace plugin { template<int op,class... T>void Command(T... args){RecordedCommand<op>::Run(args...);} }
using plugin::Command;
struct CClothes { static void RebuildPlayer(CPlayerPed*,bool) {} };
struct CPad {};
struct SenderPlayerId { int value=0; };
class CNetworkPlayer
{
public:
    CPlayerPed* m_pPed=nullptr;
    int m_nPedRef=-1,m_iPlayerId=0;
    char m_Name[Config::MAX_NICKNAME_LENGTH+1]{};
    bool m_bHasBeenConnectedBeforeMe=false;
    CPedClothesDesc m_pPedClothesDesc;
    struct { CVector vecPos; } m_onFootSnapshotInterpolated;
    CNetworkPlayer()=default;
    CNetworkPlayer(int,CVector);
    ~CNetworkPlayer();
    void CreatePed(int,CVector);
    void DestroyPed();
    void Respawn();
    int GetInternalId();
    std::string GetName(){return m_Name;}
};
#include "client/src/CNetworkPlayerManager.h"
struct CNetwork { static inline bool m_bAuthenticated=true; static void Disconnect(){m_bAuthenticated=false;CNetworkPlayerManager::RequestReset();} };
struct CLocalPlayer { static inline bool m_bIsHost=false; static inline char m_Name[Config::MAX_NICKNAME_LENGTH+1]{}; };
struct CNetworkStaticBlip { static inline bool ms_bNeedToSendAfterThisFrame=false; };
struct CEntryExitMarkerSync { static inline bool ms_bUpdateAfterProcessingThisFrame=false; };
struct CTagSync { static void SyncCurrentState(){} };
struct CMoonSync { static void SyncCurrentState(){} };
struct CChat { template<class... T>static void AddMessage(T...){} };
struct CPatch { static void RevertTemporaryPatches(){} };
namespace logger { template<class... T>void info(T...){} }
struct semver_t {};
inline void semver_unpack(uint32_t,semver_t*){}
inline void semver_to_string(semver_t*,char* out,size_t){*out=0;}
namespace Packets::System
{
struct PlayerConnected
{ struct { int playerid=0; bool isAlreadyConnected=false; char name[Config::MAX_NICKNAME_LENGTH+1]{}; uint32_t version=0; } payload; };
struct PlayerHandshake { int yourid=0; };
struct PlayerDisconnected
{
    enum { DISCONNECTION_REASON_VERSION_MISMATCH=1,DISCONNECTION_REASON_NAME_TAKEN=2 };
    struct { int playerid=0; uint8_t reason=0; uint32_t version=0; } payload;
};
}
