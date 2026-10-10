#pragma once
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <map>
#include <vector>
#include "network/player_animation_sync.h"
class CNetworkPlayer;
class Packet;
struct Factory{std::vector<std::unique_ptr<Packet>>sent;template<class T>void RegisterPacket(T*p){delete p;}void Send(Packet&,CNetworkPlayer*);};
inline Factory&GetPacketFactory(){static Factory f;return f;}
#include "network/packets/pickups.h"
inline void Factory::Send(Packet&p,CNetworkPlayer*){sent.emplace_back(p.Clone());}
constexpr int ENET_PEER_STATE_CONNECTED=5;struct PeerHandle{int state=5;};
class CNetworkPlayer{public:PeerHandle*m_pPeer=nullptr;int m_iPlayerId=0,m_nVehicleId=-1;bool m_bIsHost=false;
    struct{uint32_t generation=10;}m_vitals;PlayerAnimation::Life life{10,2,7,0,0,10,true};bool known=true;};
struct CNetworkPlayerManager{inline static std::vector<CNetworkPlayer*>m_pPlayers;
    static CNetworkPlayer*GetPlayer(PeerHandle*p){for(auto*player:m_pPlayers)if(player->m_pPeer==p)return player;return nullptr;}
    static CNetworkPlayer*GetPlayer(int id){for(auto*player:m_pPlayers)if(player->m_iPlayerId==id)return player;return nullptr;}
    static CNetworkPlayer*GetHost(){for(auto*player:m_pPlayers)if(player->m_bIsHost)return player;return nullptr;}};
struct CPlayerAnimationServer{static bool GetActorLife(const CNetworkPlayer*p,PlayerAnimation::Life&out){out=p->life;return p->known&&p->life.ready&&p->life.generation==p->m_vitals.generation;}};
constexpr int PED_TYPE_COP=6;
struct CVector{float x=0,y=0,z=0;};
struct CNetworkPed{int m_nModelId=280,m_nPedType=PED_TYPE_COP;uint32_t m_generation=9,m_ownerEpoch=2;
    CNetworkPlayer*m_pSyncer=nullptr;NPCSync::Stamp m_deathStamp;CVector m_deathPosition,m_vecPos;
    uint8_t m_deathArea=0;uint32_t m_deathProducerGeneration=0;CNetworkPlayer*m_deathProducer=nullptr;};
inline std::map<int,CNetworkPed*>nativePeds;inline bool sealAvailable=false;inline uint32_t serverTime=100;
inline uint32_t enet_time_get(){return serverTime;}
struct CNetworkPedManager{static CNetworkPed*GetPed(int id){auto it=nativePeds.find(id);return it==nativePeds.end()?nullptr:it->second;}
    static bool GetDeathProducer(CNetworkPlayer*p,int id,const NPCSync::Stamp&s){auto*ped=GetPed(id);return sealAvailable&&ped
        &&ped->m_deathProducer==p&&ped->m_deathProducerGeneration==p->m_vitals.generation&&PickupSync::SameSeal(ped->m_deathStamp,s);}};
#include "server/src/CPickupSync.h"
