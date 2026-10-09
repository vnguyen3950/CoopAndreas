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
struct Factory { std::vector<std::unique_ptr<Packet>> sent; void RegisterPacket(Packet*); void Send(Packet&); };
inline Factory& GetPacketFactory() { static Factory f; return f; }
#include "network/packets/fires.h"
inline void Factory::RegisterPacket(Packet* p) { delete p; }
inline void Factory::Send(Packet& p) { sent.emplace_back(p.Clone()); }
struct Queue { std::deque<Packet*> m_packets; };
inline Queue& GetPacketBuffer() { static Queue q; return q; }
struct CVector { float x=0,y=0,z=0; };
using RwV3d=CVector;
class CFire;
struct Scanner { int requests=0; };
struct CPedIntelligence { Scanner m_eventScanner; };
constexpr int ENTITY_TYPE_PED=3, ENTITY_TYPE_VEHICLE=2, ENTITY_TYPE_OBJECT=4, WEAPON_FTHROWER=37, WEAPON_EXTINGUISHER=42;
struct Weapon { int m_eWeaponType=0; };
class CEntity { public:
    int m_nType=0,m_nModelIndex=0; CVector position{};
    CVector GetPosition() const { return position; }
    void RegisterReference(CEntity**) {}
};
struct CVehicle;
class CPed: public CEntity { public:
    CFire* m_pFire=nullptr; float health=100; bool burningTask=false;
    CPedIntelligence intelligence{}; CPedIntelligence* m_pIntelligence=&intelligence;
    struct { bool bInVehicle=false; } m_nPedFlags;
    struct { bool bFireProof=false; } m_nPhysicalFlags;
    CEntity* m_pAttachedTo=nullptr; CVehicle* m_pVehicle=nullptr; Weapon weapon{}; Weapon m_aWeapons[13]{}; uint8_t m_nActiveWeaponSlot=0;
    CPed() { m_nType=ENTITY_TYPE_PED; }
    Weapon& GetActiveWeapon() { return weapon; }
};
using CPlayerPed=CPed;
struct CVehicle: CEntity {
    CPed* m_pDriver=nullptr; CFire* m_pFire=nullptr; float m_fHealth=1000;
    struct { bool bFireProof=false; } m_nPhysicalFlags;
    CVehicle() { m_nType=ENTITY_TYPE_VEHICLE; }
    void InflictDamage(CEntity*,int,float damage,CVector) { if (!m_nPhysicalFlags.bFireProof) m_fHealth-=damage; }
};
struct CObject:CEntity { CObject() { m_nType=ENTITY_TYPE_OBJECT; } };
template<class T> struct Pool {
    std::map<T*,int> refs;
    bool IsObjectValid(T* p) const { return refs.count(p)!=0; }
    T* GetAtRef(int r) { for(auto item:refs) if(item.second==r)return item.first; return nullptr; }
};
struct CPools {
    static inline Pool<CPed> ped; static inline Pool<CVehicle> car; static inline Pool<CObject> object;
    static inline Pool<CPed>* ms_pPedPool=&ped; static inline Pool<CVehicle>* ms_pVehiclePool=&car; static inline Pool<CObject>* ms_pObjectPool=&object;
    static int GetPedRef(CPed* p) { return ped.refs.at(p); }
    static int GetVehicleRef(CVehicle* p) { return car.refs.at(p); }
    static int GetObjectRef(CObject* p) { return object.refs.at(p); }
};
static CPlayerPed* localPed=nullptr;
inline CPlayerPed* FindPlayerPed(int) { return localPed; }
struct CTimer { static inline uint32_t m_snTimeInMilliseconds=1000; static inline float ms_fTimeStep=1; };
inline uint32_t GetTickCount() { return CTimer::m_snTimeInMilliseconds; }
struct FxSystem { void SetOffsetPos(RwV3d*) {} };
class CFire { public:
    struct { bool bActive=false,bCreatedByScript=false,bMakesNoise=true,bBeingExtinguished=false,bFirstGeneration=true; } m_nFlags;
    short m_nScriptReferenceIndex=1; CVector m_vecPosition{};
    CEntity* m_pEntityTarget=nullptr; CEntity* m_pEntityCreator=nullptr;
    uint32_t m_nTimeToBurn=0; float m_fStrength=1; char m_nNumGenerationsAllowed=0; uint8_t m_nRemovalDist=60;
    FxSystem* m_pFxSystem=nullptr;
    void Initialise() { *this={}; }
    void CreateFxSysForStrength(RwV3d*,void*) { static FxSystem fx; m_pFxSystem=&fx; }
    void Extinguish();
};
struct CFireManager {
    CFire m_aFires[60];
    CFire* StartFire(CVector,float,uint8_t,CEntity*,uint32_t,int8_t,uint8_t) { return nullptr; }
    CFire* StartFire(CEntity*,CEntity*,float,uint8_t,uint32_t,int8_t) { return nullptr; }
    bool ExtinguishPointWithWater(CVector,float,float) { return true; }
};
static CFireManager gFireManager;
struct CNetwork { static inline bool m_bAuthenticated=true; struct Peer { uint32_t connectID=1; }; static inline Peer peer; static inline Peer* m_pPeer=&peer; };
struct CLocalPlayer { static inline bool m_bIsHost=false; };
struct CNetworkPed { int m_nPedId=0,m_nPedPoolRef=-1; CPed* m_pPed=nullptr; bool m_bSyncing=false;
    uint32_t m_generation=1,m_ownerEpoch=1,m_stateSequence=1;
    bool HasValidPed() const { return m_pPed && CPools::ped.IsObjectValid(m_pPed) && CPools::GetPedRef(m_pPed)==m_nPedPoolRef; }
};
struct CNetworkPedManager {
    static inline std::vector<CNetworkPed*> m_pPeds;
    static CNetworkPed* GetPed(int id) { for(auto* p:m_pPeds)if(p->m_nPedId==id && p->HasValidPed())return p;return nullptr; }
    static CNetworkPed* GetPed(CEntity* e) { for(auto* p:m_pPeds)if(p->m_pPed==e && p->HasValidPed())return p;return nullptr; }
};
struct CNetworkPlayer { int m_iPlayerId=0; CPlayerPed* m_pPed=nullptr; };
struct CNetworkPlayerManager {
    static inline int m_nMyId=1; static inline std::vector<CNetworkPlayer*> players;
    static CNetworkPlayer* GetPlayer(int id) { for(auto* p:players)if(p->m_iPlayerId==id)return p;return nullptr; }
    static CNetworkPlayer* GetPlayer(CEntity* e) { for(auto* p:players)if(p->m_pPed==e)return p;return nullptr; }
};
struct CNetworkVehicle { int m_nVehicleId=0; CVehicle* m_pVehicle=nullptr; bool m_bSyncing=false,nativeValid=true;
    // Vehicle scene/ref policy is exercised by the actual trailer suite. This
    // collaborator tests fire's rejection of an invalid registered binding.
    bool HasValidVehicle() const { return nativeValid && m_pVehicle && CPools::car.IsObjectValid(m_pVehicle); }
};
struct CNetworkVehicleManager {
    static inline std::vector<CNetworkVehicle*> m_pVehicles;
    static CNetworkVehicle* FindVehicle(CEntity* e) { for(auto* p:m_pVehicles)if(p->m_pVehicle==e)return p;return nullptr; }
    static CNetworkVehicle* GetVehicle(int id) { for(auto* p:m_pVehicles)if(p->m_nVehicleId==id&&p->HasValidVehicle())return p;return nullptr; }
    static CNetworkVehicle* GetVehicle(CEntity* e) { for(auto* p:m_pVehicles)if(p->m_pVehicle==e&&p->HasValidVehicle())return p;return nullptr; }
};
struct CNetworkObjectManager { static int GetNetworkId(CEntity*) { return -1; } static int GetHandle(uint32_t) { return -1; } };
struct CWorld { static inline int PlayerInFocus=0; struct Info { CPed* m_pPed=nullptr; }; static inline Info Players[8]; };
static int gGameState=9;
struct CPad { struct { int ButtonCircle=0; } NewState; static CPad* GetPad(int) { static CPad p; return &p; } };
struct Event {
    std::vector<void(*)()> calls; Event& before=*this; Event& after=*this;
    template<class Fn> void operator+=(Fn fn) { calls.push_back(fn); }
    void Run() { for(auto fn:calls)fn(); }
};
namespace Events { static Event initScriptsEvent,processScriptsEvent; }
static Event gameShutdownEvent;
namespace plugin {
template<uintptr_t Address,class A,class B> void CallMethod(A*,B* ped) {
    static_assert(Address==0x607E30,"Only verified event scanner is doubled");
    ++ped->m_pIntelligence->m_eventScanner.requests;
    if (ped->m_pFire && !ped->burningTask) ped->burningTask=true;
}
}
static int checks=0,failures=0,originalDamage=0,originalProcess=0;
inline void expect(bool ok,const char* message) { ++checks; if(!ok){++failures;std::cerr<<"FAIL: "<<message<<'\n';} }
