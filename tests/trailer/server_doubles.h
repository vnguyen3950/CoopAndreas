#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <limits>
#include <string>
#include <vector>
class Packet; class CNetworkPlayer;
struct Factory { std::vector<std::pair<int,std::unique_ptr<Packet>>> sent; void RegisterPacket(Packet*); void Send(Packet&,CNetworkPlayer*); };
inline Factory& GetPacketFactory(){static Factory f;return f;}
#include "network/packets/trailers.h"
#include "network/object_sync.h"
struct CVector {float x=0,y=0,z=0;CVector()=default;CVector(float a,float b,float c):x(a),y(b),z(c){}};
constexpr int ENET_PEER_STATE_CONNECTED=5;
struct ENetPeer {int state=5;};
class CNetworkPlayer {public:ENetPeer* m_pPeer=nullptr;int m_iPlayerId=0;bool m_bIsHost=false;int m_nVehicleId=-1,m_nSeatId=-1;};
struct CNetworkPlayerManager {
    static inline std::vector<CNetworkPlayer*> m_pPlayers;
    static CNetworkPlayer* GetPlayer(int id){for(auto* p:m_pPlayers)if(p->m_iPlayerId==id)return p;return nullptr;}
    static CNetworkPlayer* GetPlayer(ENetPeer* peer){for(auto* p:m_pPlayers)if(p->m_pPeer==peer)return p;return nullptr;}
};
enum eVehicleCreatedBy {RANDOM_VEHICLE=1};
namespace Packets::Vehicles {
class VehicleSpawn:public Packet {DEFINE_PACKET_TYPE(VehicleSpawn,ePacketType::VEHICLE_SPAWN,ePacketChannel::EVENT);
public:int vehicleid=0;uint32_t generation=0;int modelid=0;CVector pos{};float rot=0;int color1=0,color2=0;eVehicleCreatedBy createdBy=RANDOM_VEHICLE;
private:template<class Stream> bool Serialize(Stream&){return true;}};
class AssignVehicleSyncer:public Packet {DEFINE_PACKET_TYPE(AssignVehicleSyncer,ePacketType::ASSIGN_VEHICLE,ePacketChannel::EVENT);
public:int vehicleid=0,syncerId=-1;uint32_t generation=0;private:template<class Stream>bool Serialize(Stream&){return true;}};
}
class CNetworkVehicle {public:int m_nVehicleId=0,m_nModelId=403;uint32_t m_generation=1;CNetworkPlayer* m_pSyncer=nullptr;CNetworkPlayer* m_pPlayers[8]{};
 bool m_bUsedByPed=false;CVector m_vecPosition{},m_vecVelocity{};int m_nPrimaryColor=0,m_nSecondaryColor=0,m_nCreatedBy=1;void ReassignSyncer(CNetworkPlayer*);
};
struct CNetworkVehicleManager {static inline std::vector<CNetworkVehicle*> m_pVehicles;static CNetworkVehicle* GetVehicle(int id){for(auto* p:m_pVehicles)if(p->m_nVehicleId==id)return p;return nullptr;}};
class CNetworkPed {public:uint32_t m_generation=1,m_ownerEpoch=1,m_stateSequence=1;int m_nPedId=0,m_nModelId=105;CNetworkPlayer* m_pSyncer=nullptr;bool m_hasState=true;
 struct {int mode=2;struct {int vehicleid=0;struct {uint32_t generation=1,epoch=1,sequence=1;} stamp;} driver;} m_lastState;};
struct CNetworkPedManager {static inline std::vector<CNetworkPed*> peds;static CNetworkPed* GetPed(int id){for(auto* p:peds)if(p->m_nPedId==id)return p;return nullptr;}};
struct CNetworkObjectManager {static ObjectSync::Registry& Registry(){static ObjectSync::Registry r;return r;}};
inline void Factory::RegisterPacket(Packet* p){delete p;}
inline void Factory::Send(Packet& p,CNetworkPlayer* target){sent.emplace_back(target->m_iPlayerId,std::unique_ptr<Packet>(p.Clone()));}
static uint32_t g_serverTime=1000;
static int checks=0,failures=0;
inline void expect(bool ok,const char* msg){++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<msg<<'\n';}}
