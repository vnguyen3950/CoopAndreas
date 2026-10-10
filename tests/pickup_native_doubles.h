#pragma once
#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <memory>
#include <map>
#include <string>
#include <vector>
#define PLUGIN_API
#include "pickup_sdk_enums.inc"
using uint32=uint32_t;
class Packet;
struct Factory{std::vector<std::unique_ptr<Packet>>sent;template<class T>void RegisterPacket(T*p){delete p;}template<class T>void Send(const T&p);};
inline Factory&GetPacketFactory(){static Factory f;return f;}
#include "network/packets/pickups.h"
template<class T>void Factory::Send(const T&p){sent.emplace_back(static_cast<const Packet&>(p).Clone());}
struct CVector{float x=0,y=0,z=0;};
#include "client/src/CPickupSync.h"
#include "network/player_animation_sync.h"
inline PlayerAnimation::Life localLife{10,2,7,0,0,10,true};
inline bool lifeKnown=true;
struct CPlayerAnimationSync{static bool GetLocalLife(PlayerAnimation::Life&out){out=localLife;return lifeKnown&&out.ready;}};
struct CObject{int m_nAreaCode=0,m_nModelIndex=346;struct{bool bDoNotRender=false,b01=true;}m_nObjectFlags;};
struct CWeapon{eWeaponType m_eWeaponType=WEAPON_UNARMED;uint32_t m_nTotalAmmo=0,m_nAmmoInClip=0;};
struct CPlayerData{uint32_t m_nLastHSMissileLOSTime=0;};
class CPed;inline std::function<void(CPed*)>recordedWeaponDrops;inline unsigned nativeWeaponDropCalls=0;
inline std::function<void(CPed*)>recordedMoneyDrops;inline unsigned nativeMoneyDropCalls=0;
class CPed{public:int m_nPedType=PED_TYPE_CIVMALE,m_nModelIndex=280,m_nAreaCode=0,poolRef=20,m_nCreatedBy=1;bool poolValid=true;
    void RecordedMoneyDrops(){++nativeMoneyDropCalls;if(recordedMoneyDrops)recordedMoneyDrops(this);}
    void RecordedWeaponDrops(){++nativeWeaponDropCalls;if(recordedWeaponDrops)recordedWeaponDrops(this);}};
class CPlayerPed:public CPed{public:float m_fHealth=50,m_fArmour=0;bool alive=true,wants=true;unsigned missionChecks=0;bool CanPlayerStartMission(){++missionChecks;return CLocalPlayerHostForGate;}
    inline static bool CLocalPlayerHostForGate=false;CVector position;
    CPlayerData data;CPlayerData*m_pPlayerData=&data;eWeaponType m_nDelayedWeapon=eWeaponType(55);uint32_t m_nDelayedWeaponAmmo=0;
    std::array<CWeapon,13>m_aWeapons;struct{bool bInVehicle=false;}m_nPedFlags;
    bool IsAlive(){return alive;}CVector GetPosition(){return position;}bool DoesPlayerWantNewWeapon(eWeaponType,bool){return wants;}};
class CVehicle{};inline CPlayerPed testPlayer;
struct CWorld{inline static int PlayerInFocus=0;struct Info{CPlayerPed*m_pPed=&testPlayer;int m_nMoney=0,m_nMaxHealth=100,m_nMaxArmour=100;};inline static std::array<Info,10>Players;};
struct ObjectPool{std::map<CObject*,int>refs;bool IsObjectValid(CObject*p){return refs.count(p)!=0;}CObject*GetAtRef(int ref){for(auto item:refs)if(item.second==ref)return item.first;return nullptr;}};
struct CPools{inline static int pool=1;inline static int*ms_pPedPool=&pool;inline static ObjectPool objects;inline static ObjectPool*ms_pObjectPool=&objects;
    inline static std::map<int,CPed*>pedRefs;
    static CPed*GetPed(int ref){if(ref==localLife.nativeReference)return &testPlayer;const auto found=pedRefs.find(ref);return found==pedRefs.end()?nullptr:found->second;}
    static int GetPedRef(CPed*p){return p&&p->poolValid?p->poolRef:-1;}static int GetObjectRef(CObject*p){return objects.refs.at(p);}};
inline bool deathIdentityAvailable=true;inline int deathPedId=3;inline NPCSync::Stamp deathIdentity{9,2,6};
struct CNetworkPedManager{static bool GetOwnerDeathIdentity(CPed*,int&pid,NPCSync::Stamp&out){pid=deathPedId;out=deathIdentity;return deathIdentityAvailable;}};
inline CPlayerPed*FindPlayerPed(int){return &testPlayer;}
struct CWeaponInfo{int m_nSlot=2,m_nModelId1=346,m_nModelId2=-1;static CWeaponInfo*GetWeaponInfo(eWeaponType,int=WEAPSKILL_STD){static CWeaponInfo i;return &i;}};
inline bool frenzy=false,weaponAllowed=true;
struct CDarkel{static bool FrenzyOnGoing(){return frenzy;}};
struct CModelInfo{inline static int info=1;inline static std::array<int*,20000>ms_modelInfoPtrs{};};
constexpr int LOADSTATE_LOADED=1;
struct CStreaming{struct Info{int m_nLoadState=1;};inline static std::array<Info,20000>ms_aInfoForModel;
    inline static std::vector<int>requests;static void RequestModel(int model,int){requests.push_back(model);}};
struct CTimer{inline static uint32_t m_snTimeInMilliseconds=10000;};
inline unsigned nativeCalls=0,nativeRemoves=0,nativeQueryFlags=0;inline uint32_t observedAmmo=0;
class CPickup;
inline std::function<bool(CPickup*,CPlayerPed*)>nativeUpdate;
class CPickup{public:int16_t m_nReferenceIndex=1,m_nModelIndex=346;uint8_t m_nPickupType=PICKUP_ONCE;uint32_t m_nAmmo=10,m_nRegenerationTime=20000;
    CObject*m_pObject=nullptr;CVector position;struct{bool bDisabled=false;}m_nFlags;
    CVector GetPosn(){return position;}bool Update(CPlayerPed*p,CVehicle*,int){++nativeCalls;observedAmmo=m_nAmmo;return nativeUpdate?nativeUpdate(this,p):false;}};
struct CPickups{inline static CPickup aPickUps[620];static int WeaponForModel(int){return WEAPON_PISTOL;}
    static bool PlayerCanPickUpThisWeaponTypeAtThisMoment(eWeaponType){return weaponAllowed;}static void AddToCollectedPickupsArray(int){++nativeQueryFlags;}
    static bool TryToMerge_WeaponType(CVector,eWeaponType,ePickupType,uint32_t,bool);};
inline bool IsPointInSphere(CVector a,CVector b,float r){return(a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z)<r*r;}
struct CGame{inline static int currArea=0;};
struct CTheScripts{inline static int OnAMissionFlag=1;inline static std::array<int,2>ScriptSpace{};};
struct Peer{uint32_t connectID=10;};inline Peer peer;
struct CNetwork{inline static bool m_bAuthenticated=true;inline static Peer*m_pPeer=&peer;};
struct CLocalPlayer{inline static bool m_bIsHost=true;};struct CNetworkPlayerManager{inline static int m_nMyId=0;};
inline int gGameState=9;inline uint32_t tick=10000;inline uint32_t GetTickCount(){return tick;}
struct Phase{std::vector<std::function<void()>>f;template<class T>void operator+=(T fn){f.emplace_back(fn);}void Fire(){for(auto&fn:f)fn();}};
struct Event{Phase before,after;};namespace Events{inline Event initScriptsEvent,processScriptsEvent;}inline Event gameShutdownEvent;
inline void CPickupSync::NativeInit(){EnableNative();}
inline void CPickupSync::NativeRemove(CPickup*p){++nativeRemoves;p->m_nPickupType=0;}
inline std::function<int(CVector,uint32_t,uint8_t,uint32_t)>replicaGenerate;
inline int CPickupSync::Generate(CVector pos,uint32_t model,uint8_t type,uint32_t ammo,uint32_t,bool,char*){
    return replicaGenerate?replicaGenerate(pos,model,type,ammo):-1;
}
