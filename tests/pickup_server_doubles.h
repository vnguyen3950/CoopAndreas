#pragma once
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
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
#include "server/src/CPickupSync.h"
