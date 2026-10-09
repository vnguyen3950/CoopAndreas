#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "config.h"
class Packet;
class CEntity;
struct Factory { std::vector<std::unique_ptr<Packet>> sent;template<class T>void RegisterPacket(T*p){delete p;}void Send(Packet&); };
Factory& GetPacketFactory(){static Factory factory;return factory;}
#include "network/packet.h"
#include "network/npc_sync.h"
#include "extracted_math.h"
struct CVector {float x=0,y=0,z=0;CVector()=default;CVector(float a,float b,float c):x(a),y(b),z(c){}};
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#include "extracted_packet.h"
inline void Factory::Send(Packet&p){sent.emplace_back(p.Clone());}
namespace logger {template<class...T>void warn(const char*,T...) {}}
static int nativeCreates=0,nativeDeletes=0,modelRequests=0,warps=0;
static bool poolFull=false,modelsAvailable=true;
struct CEntity {virtual ~CEntity()=default;};class CPed;class CVehicle;
struct CTaskSimpleUseGun {CVector m_vecTarget{};CTaskSimpleUseGun(CEntity*,CVector,int,int,bool){}void MakeAbortable(CPed*,int,void*){}};
constexpr int TASK_SECONDARY_ATTACK=0,ABORT_PRIORITY_URGENT=0;
struct TaskManager {CTaskSimpleUseGun*gun=nullptr;void SetTaskSecondary(CTaskSimpleUseGun* task,int){gun=task;}};
struct Intelligence {TaskManager m_TaskMgr;float m_fDmRadius=0;int m_nDmNumPedsToScan=0;void SetPedDecisionMakerType(int){}void SetSeeingRange(float){}void SetHearingRange(float){}CTaskSimpleUseGun*GetTaskUseGun(){return m_TaskMgr.gun;}};
struct Matrix {CVector pos{},right{},up{};};
struct Weapon {eWeaponType m_eWeaponType=WEAPON_UNARMED;eWeaponState m_nState=WEAPONSTATE_READY;};
struct PlayerData {};
class CPed:public CEntity {
public:
    Matrix matrix;Matrix*m_matrix=&matrix;Intelligence intelligence;Intelligence*m_pIntelligence=&intelligence;
    PlayerData playerData;PlayerData*m_pPlayerData=&playerData;CVehicle*m_pVehicle=nullptr;CEntity*m_pTargetedObject=nullptr;
    struct {bool bInVehicle=false;int CantBeKnockedOffBike=2;}m_nPedFlags;
    int m_nPedType=PED_TYPE_CIVMALE,m_nModelIndex=MODEL_MALE01,m_nCreatedBy=RANDOM_CHAR,m_nAreaCode=0,field_54C=0;
    float m_fHealth=100,m_fArmour=0,m_fAimingRotation=0,m_fCurrentRotation=0,m_fLookDirection=0;
    CVector m_vecMoveSpeed{};uint8_t m_nFightingStyle=4,m_nActiveWeaponSlot=0;bool ducked=false;
    std::array<Weapon,13>m_aWeapons;
    CPed(int type=PED_TYPE_CIVMALE,int model=MODEL_MALE01):m_nPedType(type),m_nModelIndex(model){++nativeCreates;}
    ~CPed();bool IsPlayer(){return m_nPedType<4;}bool IsVTableValid(){return true;}
    CVector&GetPosition(){return matrix.pos;}void SetPosn(CVector p){matrix.pos=p;}void SetOrientation(float,float,float){}void SetCharCreatedBy(int c){m_nCreatedBy=c;}
    void Remove(){}Weapon&GetWeapon(){return m_aWeapons[m_nActiveWeaponSlot];}
};
class CCopPed:public CPed {public:CCopPed(eCopType):CPed(PED_TYPE_COP,MODEL_LAPDM1){}};
class CEmergencyPed:public CPed {public:CEmergencyPed(ePedType type,int model):CPed(type,model){}};
class CCivilianPed:public CPed {public:CCivilianPed(ePedType type,int model):CPed(type,model){}};
struct Pool {std::map<int,CPed*>refs;int next=100;unsigned GetNoOfFreeSpaces(){return poolFull?0:255;}bool IsObjectValid(CPed*p){for(auto r:refs)if(r.second==p)return true;return false;}};
struct CPools {static inline Pool pool;static inline Pool*ms_pPedPool=&pool;static int GetPedRef(CPed*p){for(auto r:pool.refs)if(r.second==p)return r.first;int ref=++pool.next;pool.refs[ref]=p;return ref;}static CPed*GetPed(int ref){auto p=pool.refs.find(ref);return p==pool.refs.end()?nullptr:p->second;}};
inline CPed::~CPed(){++nativeDeletes;for(auto i=CPools::pool.refs.begin();i!=CPools::pool.refs.end();)if(i->second==this)i=CPools::pool.refs.erase(i);else++i;}
constexpr int MODEL_INFO_PED=7,LOADSTATE_LOADED=1;
struct ModelInfo {int GetModelType(){return MODEL_INFO_PED;}};
struct CModelInfo {static inline std::array<ModelInfo*,300>ms_modelInfoPtrs{};};
struct CStreaming {struct Info {int m_nLoadState=0;};static inline std::array<Info,300>ms_aInfoForModel{};static void RequestModel(int,int){++modelRequests;}static void RequestSpecialModel(int,const char*,int){++modelRequests;}static void LoadAllRequestedModels(bool){if(modelsAvailable)for(auto&i:ms_aInfoForModel)i.m_nLoadState=1;}};
struct CWorld {struct Info {CPed*m_pPed=nullptr;};static inline std::array<Info,10>Players{};static inline int PlayerInFocus=0;static void Add(CPed*){}static void Remove(CPed*){}};
struct CVehicle:public CEntity {
    Matrix matrix;Matrix*m_matrix=&matrix;CPed*m_pDriver=nullptr;CPed*m_apPassengers[8]{};int m_nMaxPassengers=8,m_nAreaCode=0;
    CVector m_vecMoveSpeed{},m_vecTurnSpeed{};uint8_t m_nPrimaryColor=0,m_nSecondaryColor=0;float m_fHealth=1000,m_fGasPedal=0,m_fBreakPedal=0,m_fSteerAngle=0;
    int m_nVehicleType=VEHICLE_AUTOMOBILE,m_nVehicleSubType=VEHICLE_AUTOMOBILE;uint16_t m_nAlarmState=0;float m_fDirtLevel=0;int m_eDoorLock=0;
    struct {bool bEngineOn=false,bLightsOn=false,bEngineBroken=false,bSirenOrAlarm=false;}m_nVehicleFlags;
    bool IsVTableValid(){return true;}void SetRemap(int){}
};
struct CBike:CVehicle {struct {float m_fDesiredLeanAngle=0;}m_rideAnimData;};
struct CBmx:CVehicle {float m_fControlPedaling=0;};struct CPlane:CVehicle {float m_fLandingGearStatus=0;};struct CAutomobile:CVehicle {uint16_t m_wMiscComponentAngle=0;};
struct CNetworkVehicle {int m_nVehicleId=0,m_nPaintJob=0;CVehicle*m_pVehicle=nullptr;};
struct CNetworkVehicleManager {static inline CNetworkVehicle*vehicle=nullptr;static CNetworkVehicle*GetVehicle(int id){return vehicle&&vehicle->m_nVehicleId==id?vehicle:nullptr;}};
struct CNetwork {static inline bool m_bAuthenticated=true;};struct CLocalPlayer {static inline bool m_bIsHost=false;};
struct CNetworkPlayerManager {static inline int m_nMyId=0;};
struct CTaskSimpleDuckToggle {bool value;CTaskSimpleDuckToggle(bool b):value(b){}void ProcessPed(CPed*p){p->ducked=value;}};
struct CUtil {static bool IsDucked(CPed*p){return p->ducked;}template<class T>static void GiveWeaponByPacket(T*p,int type,int){p->m_pPed->GetWeapon().m_eWeaponType=eWeaponType(type);}};
struct CAutoPilot{};struct CRadar {static void ClearBlipForEntity(int,int){}};struct eBlipType {static constexpr int BLIP_CHAR=0;};
namespace Commands {constexpr int WARP_CHAR_FROM_CAR_TO_COORD=1;}
namespace plugin {template<int Op,class...A>void Command(A...){}}
struct PedHooks {static inline char ms_aszLoadedSpecialModels[10][8]{};};
struct Phase {std::vector<std::function<void()>>callbacks;template<class T>void operator+=(T callback){callbacks.emplace_back(callback);}void Fire(){for(auto&callback:callbacks)callback();}};
struct Event {Phase before,after;};namespace Events {static Event initScriptsEvent,processScriptsEvent;}static Event gameShutdownEvent;
static int gGameState=0;static uint32_t tick=1000;inline uint32_t GetTickCount(){return tick;}
#include "network/packet_handler.h"
#include "client_ped_decl.inc"
#include "client_manager_decl.inc"
// Native task effects are recorded doubles; their callers and validation remain actual source.
inline void CNetworkPed::WarpIntoVehicleDriver(CVehicle*v){++warps;m_pPed->m_pVehicle=v;m_pPed->m_nPedFlags.bInVehicle=true;v->m_pDriver=m_pPed;}
inline void CNetworkPed::WarpIntoVehiclePassenger(CVehicle*v,int seat){++warps;m_pPed->m_pVehicle=v;m_pPed->m_nPedFlags.bInVehicle=true;v->m_apPassengers[seat]=m_pPed;}
inline void CNetworkPed::RemoveFromVehicle(CVehicle*){m_pPed->m_pVehicle=nullptr;m_pPed->m_nPedFlags.bInVehicle=false;}
#include "client_functions.inc"
static unsigned checks=0,failures=0;
inline void expect(bool ok,const char*m){++checks;if(!ok){++failures;std::cout<<"FAIL: "<<m<<'\n';}}
template<class T>void receive(T&p){GetPacketHandler().ProcessPacket(static_cast<Packet*>(&p));}
