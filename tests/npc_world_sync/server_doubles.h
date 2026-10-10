#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "config.h"
class Packet;class CNetworkPlayer;
struct Factory {
    struct Record { int recipient; std::unique_ptr<Packet> packet; };
    std::vector<Record> sent;
    template<class T>void RegisterPacket(T* p){delete p;}
    void Send(Packet&,CNetworkPlayer* = nullptr);
    void SendToAll(Packet&,CNetworkPlayer* = nullptr);
};
Factory& GetPacketFactory(){static Factory f;return f;}
#include "network/packet.h"
#include "network/npc_sync.h"
#include "extracted_math.h"
struct CVector { float x=0,y=0,z=0; CVector()=default;CVector(float a,float b,float c):x(a),y(b),z(c){} };
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#include "extracted_packet.h"
struct ENetPeer {int state=5;};constexpr int ENET_PEER_STATE_CONNECTED=5;
class CNetworkPed;
class CNetworkPlayer {public:ENetPeer* m_pPeer=nullptr;int m_iPlayerId=0;bool m_bIsHost=false;struct{uint32_t generation=10;}m_vitals;std::vector<CNetworkPed*> m_vPedClaims;std::string GetName(){return "recorded";}};
struct CNetworkPlayerManager {static inline std::vector<CNetworkPlayer*>m_pPlayers;static CNetworkPlayer*GetPlayer(ENetPeer* p){for(auto* player:m_pPlayers)if(player->m_pPeer==p)return player;return nullptr;}};
struct CNetworkVehicle {int m_nVehicleId=0;bool m_bUsedByPed=false;CVector m_vecPosition{},m_vecRotation{};CNetworkPlayer*m_pPlayers[8]{};};
struct CTrailerSync {static void Queue(Packet&) {} static void NpcDriver(CNetworkVehicle*,CNetworkPed*) {}};
struct CNetworkVehicleManager {static inline CNetworkVehicle* current=nullptr;static CNetworkVehicle*GetVehicle(int id){return current&&current->m_nVehicleId==id?current:nullptr;}};
namespace logger {template<class...T>void warn(const char*,T...) {}}
static uint32_t g_serverTime=1000;
#include "server_factory.inc"
inline void Factory::Send(Packet&p,CNetworkPlayer*to){serialize::WriteStream stream;ePacketChannel channel{};ePacketReliability reliability{};BuildPacketStream(p,stream,channel,reliability);sent.push_back({to?to->m_iPlayerId:-1,std::unique_ptr<Packet>(p.Clone())});}
inline void Factory::SendToAll(Packet&p,CNetworkPlayer*){Send(p);}
#include "network/vehicle_authority.h"
#include "network/packet_handler.h"
#include "server_ped_decl.inc"
#include "server_manager_decl.inc"
#include "server_ped.inc"
#include "server_manager.inc"
#include "server_handlers.inc"
// Actual client timestamp sorting with only the unrelated SYSTEM/cutscene dependencies doubled.
struct CCutsceneVotes {static void Queue(Packet&) {}};
// Fire queue behavior is covered by its actual-service suite; NPC replay uses
// the unchanged buffer body with a non-fire packet here.
struct CFireSync {static void Queue(Packet&) {}};
struct BufferHandler {void ProcessPacket(Packet*){}};
BufferHandler&GetBufferHandler(){static BufferHandler h;return h;}
struct CPacketBuffer {std::deque<Packet*>m_packets;void Receive(Packet*);~CPacketBuffer(){for(auto*p:m_packets)delete p;}};
#define GetPacketHandler GetBufferHandler
#include "client_buffer.inc"
#undef GetPacketHandler
static unsigned checks=0,failures=0;
inline void expect(bool ok,const char*message){++checks;if(!ok){++failures;std::cout<<"FAIL: "<<message<<'\n';}}
template<class T>void receive(T&p,CNetworkPlayer*sender){GetPacketHandler().ProcessPacket(static_cast<Packet*>(&p),sender);}
