#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "config.h"
class Packet;
class CNetworkPlayer;
struct Transport
{
    std::vector<std::unique_ptr<Packet>> sent;
    std::vector<CNetworkPlayer*> recipients;
    template<class T> void RegisterPacket(T* p) { delete p; }
    template<class T> void Send(const T& p, CNetworkPlayer* to = nullptr);
    template<class T> void SendToAll(const T& p, CNetworkPlayer* = nullptr) { Send(p); }
};
Transport& GetPacketFactory();
#include "network/packets/player_animation.h"
#include "respawn.inc"
template<class T> void Transport::Send(const T& p, CNetworkPlayer* to)
{ sent.emplace_back(static_cast<const Packet&>(p).Clone()); recipients.push_back(to); }
inline Transport& GetPacketFactory() { static Transport t; return t; }
#include "network/player_vitals.h"
#include "client/src/CPlayerAnimationSync.h"
#include "server/src/CPlayerAnimationSync.h"
#define PLUGIN_API
#include "native_enums.inc"
struct CAnimBlendHierarchy { float m_fTotalTime = 3; };
struct CAnimBlendAssociation
{
    unsigned short m_nAnimGroup=49;
    short m_nAnimId=261;
    CAnimBlendHierarchy* m_pHierarchy=nullptr;
    float m_fBlendAmount=1,m_fBlendDelta=0,m_fCurrentTime=0,m_fSpeed=1;
    bool m_bLooped=false,m_bPlaying=true,m_bUnlockLastFrame=false,m_bEnableMovement=false;
    bool m_bTranslateX=false,m_bTranslateY=false,m_bIndestructible=false,m_bFreezeLastFrame=false;
    int m_nCallbackType=0;
    void(*m_pCallbackFunc)(CAnimBlendAssociation*,void*)=nullptr;
    void* m_pCallbackData=nullptr;
    void SetDeleteCallback(void(*fn)(CAnimBlendAssociation*,void*),void* data)
    { m_nCallbackType=ANIMBLENDCALLBACK_DELETE;m_pCallbackFunc=fn;m_pCallbackData=data; }
    void SetCurrentTime(float t) { m_fCurrentTime=t; }
    void SetBlend(float a,float d) { m_fBlendAmount=a;m_fBlendDelta=d; }
};
struct RpClump { std::vector<CAnimBlendAssociation*> list; };
inline std::vector<RpClump*> clumps;
inline CAnimBlendAssociation* RpAnimBlendClumpGetFirstAssociation(RpClump* c)
{ return c->list.empty()?nullptr:c->list.front(); }
inline CAnimBlendAssociation* RpAnimBlendGetNextAssociation(CAnimBlendAssociation* a)
{ for(auto* c:clumps) for(size_t i=0;i<c->list.size();++i) if(c->list[i]==a) return i+1<c->list.size()?c->list[i+1]:nullptr; return nullptr; }
struct CAnimBlock { char bLoaded=1; };
inline CAnimBlock block;
inline CAnimBlendHierarchy hierarchy;
inline unsigned adds=0,requests=0; inline int requestedFlags=-1;
struct CAnimManager
{
    static const char* GetAnimBlockName(int group) { return group==49?"playidles":"ped"; }
    static CAnimBlock* GetAnimationBlock(const char*) { return &block; }
    static int GetAnimationBlockIndex(const char*) { return 5; }
    static CAnimBlendAssociation* AddAnimation(RpClump* c,int group,int animation)
    { auto* a=new CAnimBlendAssociation; a->m_nAnimGroup=group;a->m_nAnimId=animation;a->m_pHierarchy=&hierarchy;c->list.push_back(a);++adds;return a; }
};
struct CStreaming { static void RequestModel(int,int flags) { ++requests;requestedFlags=flags; } };
struct CPlayerPed
{
    void* m_pPlayerData = reinterpret_cast<void*>(1);
    RpClump* m_pRwClump=nullptr;
    int m_nModelIndex=0,m_nAreaCode=0,m_nMoveState=PEDMOVE_STILL;
    ePedState m_ePedState=PEDSTATE_IDLE;
    float m_fHealth=100;
    bool duck=false;
    struct { bool bInVehicle=false; } m_nPedFlags;
    struct { bool bSubmergedInWater=false; } m_nPhysicalFlags;
};
struct PlayerInfo { CPlayerPed* m_pPed=nullptr; };
struct CWorld { static inline int PlayerInFocus=0; static inline std::array<PlayerInfo,10> Players{}; };
struct Peer { uint32_t connectID=10; };
struct CNetwork { static inline bool m_bAuthenticated=true; static inline Peer* m_pPeer=nullptr; };
struct Pool
{ std::set<CPlayerPed*> live; bool IsObjectValid(CPlayerPed* p) { return live.count(p)!=0; } };
inline Pool nativePool;
struct CPools
{
    static inline Pool* ms_pPedPool=&nativePool;
    static inline std::map<int,CPlayerPed*> references;
    static CPlayerPed* GetPed(int ref) { auto i=references.find(ref);return i==references.end()?nullptr:i->second; }
    static int GetPedRef(CPlayerPed* p) { for(auto i:references)if(i.second==p)return i.first;return -1; }
};
inline unsigned respawns=0; inline bool failRespawn=false;
class CNetworkPlayer
{
public:
    Peer* m_pPeer=nullptr;
    int m_iPlayerId=1,m_nPedRef=100;
    CPlayerPed* m_pPed=nullptr;
    PlayerVitals::Cache m_vitals;
    PlayerAnimation::Cache m_actorLife;
    int GetInternalId() { for(int i=2;i<10;i++)if(CWorld::Players[i].m_pPed==m_pPed)return i;return -1; }
    void Respawn() { ++respawns;if(failRespawn){m_pPed=nullptr;return;} m_pPed=CPools::GetPed(m_nPedRef); }
};
struct CNetworkPlayerManager
{
    static inline int m_nMyId=0;
    static inline std::vector<CNetworkPlayer*> m_pPlayers;
    static CNetworkPlayer* GetPlayer(int id) { for(auto* p:m_pPlayers)if(p&&p->m_iPlayerId==id)return p;return nullptr; }
    static CNetworkPlayer* GetPlayer(Peer* peer) { for(auto* p:m_pPlayers)if(p&&p->m_pPeer==peer)return p;return nullptr; }
};
class CPlayerVitalsSync { public: static bool HasBoundPed(CNetworkPlayer*); };
inline CPlayerPed* FindPlayerPed(int id) { return CWorld::Players[id].m_pPed; }
struct CUtil { static bool IsDucked(CPlayerPed* p) { return p->duck; } };
struct CTheScripts { static inline int OnAMissionFlag=1; static inline std::array<int,2> ScriptSpace{}; };
inline int gGameState=9; inline uint32_t tick=1000,g_serverTime=1000;
inline uint32_t GetTickCount() { return tick; }
struct Phase
{
    std::vector<std::function<void()>> callbacks;
    template<class T> void operator+=(T c) { callbacks.emplace_back(c); }
    void Fire() { for(auto& c:callbacks)c(); }
};
struct Event { Phase before,after; };
namespace Events { inline Event initScriptsEvent,processScriptsEvent,shutdownRwEvent; }
inline Event gameShutdownEvent;
