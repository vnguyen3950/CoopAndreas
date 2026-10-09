#pragma once
#include <algorithm>
#include <functional>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>
class CNetworkPlayer;
struct Factory
{
    template<class T>void RegisterPacket(T* packet){delete packet;}
    template<class T>void Send(const T& packet,CNetworkPlayer* player=nullptr);
};
inline Factory& GetPacketFactory(){static Factory factory;return factory;}
#include "network/packets/gang_wars.h"
inline std::vector<Packets::Gangs::State> states;
inline std::vector<Packets::Gangs::Territory> worlds;
inline std::vector<CNetworkPlayer*> targets;
template<class T>void Factory::Send(const T& packet,CNetworkPlayer* player)
{
    if constexpr(std::is_same_v<T,Packets::Gangs::State>)states.push_back(packet);
    else worlds.push_back(packet);
    targets.push_back(player);
}
struct Event
{
    std::vector<std::function<void()>> handlers;
    template<class F>void operator+=(F f){handlers.push_back(f);}
    void Fire(){for(auto& f:handlers)f();}
};
struct TimedEvent{Event before,after;};
namespace Events{inline TimedEvent initScriptsEvent,processScriptsEvent;}
inline TimedEvent gameShutdownEvent;
struct Peer{uint32_t connectID=1;};
struct CNetwork{inline static bool m_bAuthenticated=false;inline static Peer* m_pPeer=nullptr;};
struct CLocalPlayer{inline static bool m_bIsHost=false;};
class CNetworkPlayer{public:Peer* m_pPeer=nullptr;int m_iPlayerId=0;bool m_bIsHost=false;struct{uint32_t generation=1;}m_vitals;};
struct CNetworkPlayerManager
{
    inline static int m_nMyId=0;
    inline static std::vector<CNetworkPlayer*> m_pPlayers;
    static CNetworkPlayer* GetPlayer(int id){for(auto* p:m_pPlayers)if(p->m_iPlayerId==id)return p;return nullptr;}
    static CNetworkPlayer* GetPlayer(Peer* peer){for(auto* p:m_pPlayers)if(p->m_pPeer==peer)return p;return nullptr;}
    static CNetworkPlayer* GetHost(){for(auto* p:m_pPlayers)if(p->m_bIsHost)return p;return nullptr;}
};
struct CVector{float x=0,y=0,z=0;CVector()=default;CVector(float a,float b,float c):x(a),y(b),z(c){}};
struct CPed
{
    int reference=1,m_nPedType=7;
    void* m_pPlayerData=reinterpret_cast<void*>(1);
    void* m_matrix=reinterpret_cast<void*>(1);
    struct{bool bPartOfAttackWave=false;}m_nPedFlags;
};
struct Pool
{
    int m_nSize=0;std::vector<CPed*> entries;
    CPed* GetAt(int i){return i<int(entries.size())?entries[i]:nullptr;}
    bool IsObjectValid(CPed* p){return std::find(entries.begin(),entries.end(),p)!=entries.end();}
};
struct CPools
{
    inline static Pool* ms_pPedPool=nullptr;
    static CPed* GetPed(int ref){if(ms_pPedPool)for(auto* p:ms_pPedPool->entries)if(p&&p->reference==ref)return p;return nullptr;}
    static int GetPedRef(CPed* p){return p->reference;}
};
struct CWorld{inline static int PlayerInFocus=0;struct Info{CPed* m_pPed=nullptr;};inline static Info Players[1];};
inline CPed* FindPlayerPed(int){return CWorld::Players[0].m_pPed;}
inline int gGameState=0;
inline uint32_t tick=1000;
inline uint32_t GetTickCount(){return tick;}
struct CTimer{inline static uint32_t m_snTimeInMilliseconds=1000;};
enum eGangWarState{NOT_IN_WAR=0,PREFIRST_WAVE=1};
enum eGangAttackState{NO_ATTACK=0,WAR_NOTIFIED=1};
struct CZone{};
#pragma pack(push,1)
struct CZoneInfo{char m_nGangDensity[10]{};char dealer=0;struct{uint8_t r=0,g=0,b=0,a=0;}m_ZoneColor;char m_nFlags=0,races=0;};
#pragma pack(pop)
struct CGangWars
{
    inline static bool bGangWarsActive=false,bTrainingMission=false,bCanTriggerGangWarWhenOnAMission=false,bIsPlayerOnAMission=false;
    inline static eGangWarState State=NOT_IN_WAR;
    inline static eGangAttackState State2=NO_ATTACK;
    inline static CZone* pZoneToFightOver=nullptr;
    inline static CZoneInfo* pZoneInfoToFightOver=nullptr;
    inline static int Gang1=-1,Gang2=-1,FightTimer=0;
    inline static uint32_t TimeStarted=0;
    inline static float Provocation=0;
    inline static int NumSpecificZones=0,ZoneInfoForTraining=-1,WarFerocity=0,aSpecificZones[6]{};
    inline static float Difficulty=0,TerritoryUnderControlPercentage=0,TimeTillNextAttack=0;
    inline static bool bPlayerIsCloseby=false;
    inline static CVector PointOfAttack,CoorsOfPlayerAtStartOfWar;
    inline static int updates=0,cancels=0;
    inline static std::function<void()> onUpdate;
    static void Update(){++updates;if(onUpdate)onUpdate();}
    static void EndGangWar(bool){++cancels;State=NOT_IN_WAR;State2=NO_ATTACK;}
    static int ReleasePedsInAttackWave(bool,bool){++cancels;return 0;}
    static void ReleaseCarsInAttackWave(){}
};
struct CNetworkPedManager
{
    inline static std::vector<CPed*> tracked;
    inline static std::vector<std::pair<CPed*,bool>> operations;
    static bool IsPedTracked(CPed* ped){return std::find(tracked.begin(),tracked.end(),ped)!=tracked.end();}
    static bool PinGangWarPedToHost(CPed* ped,bool pin){if(!IsPedTracked(ped))return false;operations.emplace_back(ped,pin);return true;}
};
struct CNetworkPed{static void CreateHosted(CPed* p){CNetworkPedManager::tracked.push_back(p);}};
#include "gang_radar_enums.inc"
struct CRadar{inline static int created=0,cleared=0;static void ClearBlip(int){++cleared;}static int SetCoordBlip(int,CVector,int,int,void*){return ++created;}};
struct CChat{inline static int messages=0;static void AddMessage(const char*){++messages;}};
namespace patch{inline void RedirectCall(uintptr_t,void(*)()){};}
#include "source/client/src/CGangWarSync.h"
#include "source/server/src/CGangWarSync.h"
