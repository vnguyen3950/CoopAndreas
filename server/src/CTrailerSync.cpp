#include "stdafx.h"
#include "CTrailerSync.h"
#include "CFireSync.h"
#include "network/packets/vehicles.h"
#include "network/vehicle_authority.h"
namespace {
TrailerSync::Cache& Room(){static TrailerSync::Cache c;c.Reset(c.room?c.room:1);return c;}
struct Peer {uint32_t scene=0,window=0,count=0,incarnation=0;};
struct Npc {int id=-1;uint32_t birth=0,epoch=0;};
std::array<Peer,TrailerSync::MaxPlayers> peers{};
std::array<Npc,TrailerSync::MaxVehicles> npcDrivers{};
std::array<uint32_t,TrailerSync::MaxVehicles> intents{};
std::array<uint32_t,TrailerSync::MaxVehicles> ownerIncarnations{};
uint32_t nextBirth=0,nextRevision=0,nextConnection=0;
bool Auth(CNetworkPlayer* p){return p && std::find(CNetworkPlayerManager::m_pPlayers.begin(),CNetworkPlayerManager::m_pPlayers.end(),p)!=CNetworkPlayerManager::m_pPlayers.end() && p->m_iPlayerId>=0 && p->m_iPlayerId<8 && p->m_pPeer &&
 p->m_pPeer->state==ENET_PEER_STATE_CONNECTED && CNetworkPlayerManager::GetPlayer(p->m_pPeer)==p;}
TrailerSync::Ref Identity(CNetworkVehicle* v){return v?TrailerSync::Ref{uint32_t(v->m_nVehicleId),v->m_generation,int(v->m_nModelId)}:TrailerSync::Ref{};}
bool Mapped(CNetworkVehicle* v){return v && Identity(v).Valid() && CNetworkVehicleManager::GetVehicle(v->m_nVehicleId)==v;}
CNetworkPlayer* Owner(CNetworkVehicle* v){
 if(!Mapped(v))return nullptr;
 if(auto* driver=v->m_pPlayers[0])return Auth(driver) && driver->m_nVehicleId==v->m_nVehicleId && driver->m_nSeatId==0?driver:nullptr;
 if(v->m_bUsedByPed){
  const auto& n=npcDrivers[v->m_nVehicleId];auto* p=n.id>=0?CNetworkPedManager::GetPed(n.id):nullptr;
  if(!p || p->m_generation!=n.birth || p->m_ownerEpoch!=n.epoch || !p->m_hasState || p->m_lastState.mode!=2 ||
    p->m_lastState.driver.vehicleid!=v->m_nVehicleId || p->m_lastState.driver.stamp.generation!=n.birth ||
    p->m_lastState.driver.stamp.epoch!=n.epoch || p->m_lastState.driver.stamp.sequence!=p->m_stateSequence || !Auth(p->m_pSyncer))return nullptr;
  return p->m_pSyncer;
 }
 npcDrivers[v->m_nVehicleId]={};return Auth(v->m_pSyncer)?v->m_pSyncer:nullptr;
}
void Send(Packet& p,CNetworkPlayer* recipient=nullptr){p.serverTime=g_serverTime;
 if(recipient){if(Auth(recipient)&&peers[recipient->m_iPlayerId].scene)GetPacketFactory().Send(p,recipient);return;}
 for(auto* player:CNetworkPlayerManager::m_pPlayers)if(Auth(player)&&peers[player->m_iPlayerId].scene)GetPacketFactory().Send(p,player);
}
void Receipt(CNetworkPlayer* p){Packets::Trailers::Lease packet;packet.room=Room().room;packet.reset=true;
 packet.scene=peers[p->m_iPlayerId].scene;Send(packet,p);}
void Lease(const TrailerSync::Lease& lease,CNetworkPlayer* recipient=nullptr){Packets::Trailers::Lease packet;packet.room=Room().room;packet.lease=lease;Send(packet,recipient);}
void State(TrailerSync::Link next){if(nextRevision==TrailerSync::MaxCounter)return;
 next.revision=++nextRevision;next.sequence=1;Room().State(next);Packets::Trailers::Link packet;packet.link=next;Send(packet);}
void Detach(uint32_t id){auto l=Room().links[id];if(!l.attached)return;l.attached=false;State(l);}
void ReplayVehicle(CNetworkVehicle* v,CNetworkPlayer* p){
 if(!Mapped(v))return;
 Packets::Vehicles::VehicleSpawn spawn;spawn.vehicleid=v->m_nVehicleId;spawn.generation=v->m_generation;
 spawn.modelid=v->m_nModelId;spawn.pos=v->m_vecPosition;spawn.rot=0.0f;spawn.color1=v->m_nPrimaryColor;spawn.color2=v->m_nSecondaryColor;
 spawn.createdBy=static_cast<eVehicleCreatedBy>(v->m_nCreatedBy);Send(spawn,p);
 Packets::Vehicles::AssignVehicleSyncer assign;assign.vehicleid=v->m_nVehicleId;assign.generation=v->m_generation;assign.syncerId=v->m_pSyncer?v->m_pSyncer->m_iPlayerId:-1;Send(assign,p);
}
bool ReadyRequest(CNetworkPlayer* sender,uint32_t scene){return Auth(sender)&&peers[sender->m_iPlayerId].incarnation && scene && peers[sender->m_iPlayerId].scene==scene;}
}
uint32_t CTrailerSync::AllocateGeneration(){return nextBirth<TrailerSync::MaxCounter?++nextBirth:0;}
void CTrailerSync::Join(CNetworkPlayer* p){if(Auth(p)){peers[p->m_iPlayerId]={};if(nextConnection<TrailerSync::MaxCounter)peers[p->m_iPlayerId].incarnation=++nextConnection;}}
void CTrailerSync::Leave(CNetworkPlayer* p){if(!p||p->m_iPlayerId<0||p->m_iPlayerId>=8)return;peers[p->m_iPlayerId]={};
 for(uint32_t i=0;i<TrailerSync::MaxVehicles;++i)if(Room().links[i].attached && Room().leases[Room().links[i].parent.id].owner==p->m_iPlayerId)Detach(i);
}
void CTrailerSync::NpcDriver(CNetworkVehicle* v,CNetworkPed* p){if(!Mapped(v)||!p)return;
 npcDrivers[v->m_nVehicleId]={p->m_nPedId,p->m_generation,p->m_ownerEpoch};Changed(v);}
void CTrailerSync::Changed(CNetworkVehicle* v,bool removed){
 if(!Mapped(v))return;const auto ref=Identity(v);auto previous=Room().leases[ref.id];
 TrailerSync::Lease next;next.vehicle=ref;next.epoch=1;next.live=!removed;
 auto* owner=removed?nullptr:Owner(v);next.owner=owner?owner->m_iPlayerId:-1;
 if(TrailerSync::Same(previous.vehicle,ref)){
  if(previous.owner==next.owner && previous.live==next.live && ownerIncarnations[ref.id]==(owner?peers[owner->m_iPlayerId].incarnation:0))return;
  if(previous.epoch==TrailerSync::MaxCounter)return;next.epoch=previous.epoch+1;
 }
 if(!Room().Bind(next))return;ownerIncarnations[ref.id]=owner?peers[owner->m_iPlayerId].incarnation:0;intents[ref.id]=0;Lease(next);
 for(uint32_t id=0;id<TrailerSync::MaxVehicles;++id){auto l=Room().links[id];if(!l.attached)continue;
  if(l.parent.id==ref.id){
   if(removed||!TrailerSync::Same(l.parent,ref)||!owner){Detach(id);continue;}
   l.ownerEpoch=next.epoch;if(auto* child=CNetworkVehicleManager::GetVehicle(int(l.child.id))){child->ReassignSyncer(owner);CFireSync::VehicleChanged(child);}
   State(l);
  }else if(l.child.id==ref.id && (removed||!TrailerSync::Same(l.child,ref)))Detach(id);
 }
}
bool CTrailerSync::LegacyAllowed(CNetworkVehicle* v){return !Mapped(v)||!Room().Linked(uint32_t(v->m_nVehicleId),v->m_generation);}
void CTrailerSync::Hello(const Packets::Trailers::Hello& packet,CNetworkPlayer* sender){
 if(!Auth(sender)||!TrailerSync::Counter(packet.scene))return;auto& peer=peers[sender->m_iPlayerId];if(packet.scene<peer.scene)return;
 if(peer.scene && packet.scene>peer.scene && packet.controllingRestart && sender->m_bIsHost){
  if(Room().room==TrailerSync::MaxCounter)return;Room().Reset(Room().room+1);intents={};
  for(auto* p:CNetworkPlayerManager::m_pPlayers)if(Auth(p)&&peers[p->m_iPlayerId].scene)Receipt(p);
 }
 peer.scene=packet.scene;Receipt(sender);
 for(auto* v:CNetworkVehicleManager::m_pVehicles){Changed(v);ReplayVehicle(v,sender);}
 for(const auto& lease:Room().leases)if(lease.Valid())Lease(lease,sender);
 for(const auto& link:Room().links)if(link.revision){
  Packets::Trailers::Link p;p.link=link;Send(p,sender);
 }
}
void CTrailerSync::Link(Packets::Trailers::Link& packet,CNetworkPlayer* sender){
 auto& l=packet.link;
 if(!ReadyRequest(sender,packet.scene)||!l.Valid()||l.revision||l.room!=Room().room)return;
 auto* parent=CNetworkVehicleManager::GetVehicle(int(l.parent.id));auto* child=CNetworkVehicleManager::GetVehicle(int(l.child.id));
 if(!Mapped(parent)||!Mapped(child))return;Changed(parent);Changed(child);
 if(!Room().Authorized(l,sender->m_iPlayerId)||l.sequence<=intents[l.parent.id])return;
 auto& peer=peers[sender->m_iPlayerId];if(g_serverTime-peer.window>=1000){peer.window=g_serverTime;peer.count=0;}
 if(peer.count>=32)return;
 auto previous=Room().links[l.child.id];
 if(l.attached){
  if(child->m_pPlayers[0]||child->m_bUsedByPed)return;
  if(previous.attached && !TrailerSync::Same(previous.parent,l.parent) && !Room().Authorized(previous,sender->m_iPlayerId))return;
  for(const auto& link:Room().links)if(link.attached && TrailerSync::Same(link.parent,l.parent) && !TrailerSync::Same(link.child,l.child))return;
  const auto delta=parent->m_vecPosition;TrailerSync::Vec pos{delta.x,delta.y,delta.z};
  const auto d=TrailerSync::Vec{l.frame.position.x-pos.x,l.frame.position.y-pos.y,l.frame.position.z-pos.z};
  if(!pos.Valid(20000)||d.Dot(d)>1600)return;
 }else if(!previous.attached||!TrailerSync::Same(previous.parent,l.parent)||!TrailerSync::Same(previous.child,l.child))return;
 intents[l.parent.id]=l.sequence;++peer.count;
 if(l.attached){child->ReassignSyncer(sender);CFireSync::VehicleChanged(child);}
 State(l);
}
void CTrailerSync::Pose(Packets::Trailers::Pose& packet,CNetworkPlayer* sender){
 auto& l=packet.link;if(!ReadyRequest(sender,packet.scene)||!l.Valid()||!l.attached||!TrailerSync::Counter(l.revision))return;
 auto* parent=CNetworkVehicleManager::GetVehicle(int(l.parent.id));auto* child=CNetworkVehicleManager::GetVehicle(int(l.child.id));
 if(!Mapped(parent)||!Mapped(child))return;Changed(parent);Changed(child);
 const auto& old=Room().links[l.child.id];
 if(!Room().Authorized(l,sender->m_iPlayerId)||!old.attached||old.revision!=l.revision||!TrailerSync::Same(old.parent,l.parent)||
  !TrailerSync::Same(old.child,l.child)||old.ownerEpoch!=l.ownerEpoch||l.sequence<=old.sequence)return;
 if(!Room().Position(l))return;
 child->m_vecPosition=CVector(l.frame.position.x,l.frame.position.y,l.frame.position.z);
 child->m_vecVelocity=CVector(l.frame.velocity.x,l.frame.velocity.y,l.frame.velocity.z);Send(packet);
}
