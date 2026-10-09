#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>
class Packet;
struct Factory {std::vector<std::unique_ptr<Packet>> sent;void RegisterPacket(Packet*);void Send(Packet&);};
inline Factory& GetPacketFactory(){static Factory f;return f;}
#include "network/packets/trailers.h"
inline void Factory::RegisterPacket(Packet* p){delete p;}inline void Factory::Send(Packet& p){sent.emplace_back(p.Clone());}
struct CVector {float x=0,y=0,z=0;CVector()=default;CVector(float a,float b,float c):x(a),y(b),z(c){}};
struct Rotation {float m_angle=0;Rotation& operator=(float n){m_angle=n;return *this;}};
using WorldPositionCompressed=CVector;using RadianAngleCompressed=Rotation;
enum eVehicleCreatedBy {RANDOM_VEHICLE=1,MISSION_VEHICLE=2};
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
struct Queue {std::deque<Packet*> m_packets;};inline Queue& GetPacketBuffer(){static Queue q;return q;}
struct Handler {void ProcessPacket(Packet*);};inline Handler& GetPacketHandler(){static Handler h;return h;}
struct Matrix {CVector pos{},right{1,0,0},up{0,1,0},at{0,0,1};};
struct CPed {};struct CEntity {};
class CVehicle:public CEntity {public:int m_nModelIndex=0,m_nStatus=4;Matrix matrix{};Matrix* m_matrix=&matrix;
 CVehicle* m_pTractor=nullptr;CVehicle* m_pTrailer=nullptr;CPed* m_pDriver=nullptr;CVector m_vecMoveSpeed{},m_vecTurnSpeed{};float m_fHealth=1000;
 float m_fDirtLevel=0;int m_eDoorLock=0,m_nPrimaryColor=0,m_nSecondaryColor=0,m_nDamageFlags=0;
 void SetPosn(CVector v){matrix.pos=v;}void SetOrientation(float,float,float){};
 static void* operator new(size_t)noexcept;static void operator delete(void*);
 int m_nTimeTillWeNeedThisCar=0,m_nCreatedBy=1;int GetRemapIndex(){return 0;}float GetHeading(){return 0;}
 bool SetTowLink(CVehicle*,bool);bool BreakTowLink();void UpdateRwFrame(){};void SetVehicleCreatedBy(int){};
};
struct NativePool {unsigned int freeSpaces=255;unsigned int GetNoOfFreeSpaces(){return freeSpaces;}std::map<CVehicle*,int> refs;bool IsObjectValid(CVehicle* p){return refs.count(p)!=0;}};
struct CPools {static inline NativePool pool;static inline NativePool* ms_pVehiclePool=&pool;static int GetVehicleRef(CVehicle* v){return pool.refs.at(v);}};
static CPed local;inline CPed* FindPlayerPed(int){return &local;}
struct CNetwork {static inline bool m_bAuthenticated=true;struct Peer{uint32_t connectID=1;};static inline Peer peer;static inline Peer* m_pPeer=&peer;};
struct CLocalPlayer {static inline bool m_bIsHost=false;};
struct CNetworkPlayerManager {static inline int m_nMyId=1;};
struct CWorld {static inline int PlayerInFocus=0;static void Add(CVehicle*){}static void Remove(CVehicle*){}};static int gGameState=9;
struct CTimer {static inline uint32_t m_snTimeInMilliseconds=1000;};inline uint32_t GetTickCount(){return CTimer::m_snTimeInMilliseconds;}
class CNetworkVehicle {public:int m_nVehicleId=0,m_nModelId=403,m_nVehiclePoolRef=-1,m_nCreatedBy=1;uint32_t m_generation=0,m_createdScene=0,m_requestToken=0;
 CVehicle* m_pVehicle=nullptr;bool m_bSyncing=false,m_bPreserveBirth=false;unsigned char m_nTempId=255;char m_nPaintJob=0;CNetworkVehicle()=default;CNetworkVehicle(int,int,CVector,float,unsigned char,unsigned char,unsigned char,uint32_t);bool CreateVehicle(int,int,CVector,float,unsigned char,unsigned char);static CNetworkVehicle* CreateHosted(CVehicle*);bool HasValidVehicle()const;};
struct CNetworkVehicleManager {static inline std::vector<CNetworkVehicle*> m_pVehicles;static inline CNetworkVehicle* m_apTempVehicles[255]{};
 static CNetworkVehicle* FindVehicle(int id){for(auto* p:m_pVehicles)if(p->m_nVehicleId==id)return p;return nullptr;}
 static CNetworkVehicle* FindVehicle(CEntity* n){for(auto* p:m_pVehicles)if(p->m_pVehicle==n)return p;return nullptr;}
 static CNetworkVehicle* GetVehicle(int);static CNetworkVehicle* GetVehicle(CEntity*);
 static uint8_t AddToTempList(CNetworkVehicle*);
 static void Add(CNetworkVehicle* v){m_pVehicles.push_back(v);}static void Remove(CNetworkVehicle* v){m_pVehicles.erase(std::remove(m_pVehicles.begin(),m_pVehicles.end(),v),m_pVehicles.end());}
};
struct Event {std::vector<void(*)()> calls;Event& before=*this;Event& after=*this;template<class T>void operator+=(T fn){calls.push_back(fn);}void Run(){for(auto fn:calls)fn();}};
namespace Events {static Event initScriptsEvent,processScriptsEvent;}static Event gameShutdownEvent;
struct CFireSync {static void VehicleRemoved(int){}};
static int checks=0,failures=0,nativeAttach=0,nativeDetach=0,nativeSpawn=0;
inline void expect(bool ok,const char* msg){++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<msg<<'\n';}}
#include "vehicle_birth_extracted.h"

constexpr int MODEL_LANDSTAL=400,MODEL_UTILTR1=611,GAME_REQUIRED=2,LOADSTATE_LOADED=1,DOORLOCK_UNLOCKED=1;
enum {VEHICLE_AUTOMOBILE,VEHICLE_MTRUCK,VEHICLE_QUAD,VEHICLE_HELI,VEHICLE_PLANE,VEHICLE_BIKE,VEHICLE_BMX,VEHICLE_TRAILER,VEHICLE_BOAT,VEHICLE_TRAIN};
struct CVehicleModelInfo {int m_nVehicleType=VEHICLE_AUTOMOBILE;};
struct CModelInfo {static inline CVehicleModelInfo* ms_modelInfoPtrs[20000]{};};
struct CStreaming {struct Info{unsigned char m_nFlags=0,m_nLoadState=0;};static inline Info ms_aInfoForModel[20000]{};static inline bool loadSucceeds=true;
 static void RequestModel(int model,int){if(loadSucceeds)ms_aInfoForModel[model].m_nLoadState=LOADSTATE_LOADED;}
 static void LoadAllRequestedModels(bool){}static void SetModelIsDeletable(int){}static void SetModelTxdIsDeletable(int){};
};
inline bool allocationFails=false;inline int allocationCalls=0,sdkConstructors=0;
inline void* CVehicle::operator new(size_t n)noexcept {++allocationCalls;return allocationFails?nullptr:std::malloc(n);}
inline void CVehicle::operator delete(void* p){CPools::pool.refs.erase(static_cast<CVehicle*>(p));std::free(p);}
inline void RecordSdkConstruction(CVehicle* v,int model){++sdkConstructors;v->m_nModelIndex=model;CPools::pool.refs[v]=++nativeSpawn;}
#define SDK_VEHICLE(name) class name:public CVehicle {public:name(int model,int,bool=false){RecordSdkConstruction(this,model);}};
SDK_VEHICLE(CMonsterTruck) SDK_VEHICLE(CQuadBike) SDK_VEHICLE(CHeli) SDK_VEHICLE(CPlane) SDK_VEHICLE(CBike)
SDK_VEHICLE(CBmx) SDK_VEHICLE(CTrailer) SDK_VEHICLE(CBoat) SDK_VEHICLE(CAutomobile)
#undef SDK_VEHICLE
