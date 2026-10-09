#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
class Packet; class CNetworkPlayer;
struct Factory { std::vector<std::pair<int,std::unique_ptr<Packet>>> sent; void RegisterPacket(Packet*); void Send(Packet&,CNetworkPlayer*); };
inline Factory& GetPacketFactory(){static Factory f;return f;}
#include "network/packets/fires.h"
#include "network/object_sync.h"
struct CVector {float x=0,y=0,z=0;};
constexpr int ENET_PEER_STATE_CONNECTED=5;
struct ENetPeer {int state=5;};
class CNetworkPlayer {public:ENetPeer* m_pPeer=nullptr;int m_iPlayerId=0;bool m_bIsHost=false;};
struct CNetworkPlayerManager {
    static inline std::vector<CNetworkPlayer*> m_pPlayers;
    static CNetworkPlayer* GetPlayer(int id){for(auto* p:m_pPlayers)if(p->m_iPlayerId==id)return p;return nullptr;}
    static CNetworkPlayer* GetPlayer(ENetPeer* peer){for(auto* p:m_pPlayers)if(p->m_pPeer==peer)return p;return nullptr;}
};
class CNetworkVehicle {public:int m_nVehicleId=0,m_nModelId=400;CNetworkPlayer* m_pSyncer=nullptr;CNetworkPlayer* m_pPlayers[8]{};};
struct CNetworkVehicleManager {static inline std::vector<CNetworkVehicle*> m_pVehicles;static CNetworkVehicle* GetVehicle(int id){for(auto* p:m_pVehicles)if(p->m_nVehicleId==id)return p;return nullptr;}};
class CNetworkPed {public:uint32_t m_generation=1,m_ownerEpoch=1;int m_nPedId=0,m_nModelId=105;CNetworkPlayer* m_pSyncer=nullptr;};
struct CNetworkPedManager {static inline std::vector<CNetworkPed*> peds;static CNetworkPed* GetPed(int id){for(auto* p:peds)if(p->m_nPedId==id)return p;return nullptr;}};
struct CNetworkObjectManager {static ObjectSync::Registry& Registry(){static ObjectSync::Registry r;return r;}};
inline void Factory::RegisterPacket(Packet* p){delete p;}
inline void Factory::Send(Packet& p,CNetworkPlayer* target){sent.emplace_back(target->m_iPlayerId,std::unique_ptr<Packet>(p.Clone()));}
static uint32_t g_serverTime=1000;
static int checks=0,failures=0;
inline void expect(bool ok,const char* msg){++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<msg<<'\n';}}
