#include "stdafx.h"
#include "CTrailerSync.h"
#include "CPacketBuffer.h"
namespace {
TrailerSync::Cache& Room(){static TrailerSync::Cache c;return c;}
struct Applied {TrailerSync::Ref parent{},child{};CVehicle* parentPtr=nullptr;CVehicle* childPtr=nullptr;int parentRef=-1,childRef=-1;uint32_t scene=0;};
struct Observation {TrailerSync::Link desired{};uint32_t time=0;bool nativeDetach=false;};
std::array<Applied,TrailerSync::MaxVehicles> applied{};
std::array<Observation,TrailerSync::MaxVehicles> observations{};
std::array<uint32_t,TrailerSync::MaxVehicles> intents{};
std::array<std::unique_ptr<Packets::Vehicles::VehicleSpawn>,TrailerSync::MaxVehicles> pendingSpawns{};
std::array<uint32_t,TrailerSync::MaxVehicles> retired{},pendingBirth{},attemptTime{};
uint32_t scene=0,acknowledgedScene=0,lastHello=0,peerConnection=0;
bool scriptsReady=false,nativeEnabled=false,controllingRestart=false;int replay=0;
bool Pool(CVehicle* v,int ref=-1){return v && CPools::ms_pVehiclePool && CPools::ms_pVehiclePool->IsObjectValid(v) &&
 (ref<0 || CPools::GetVehicleRef(v)==ref);}
bool Ready(){return nativeEnabled && CNetwork::m_bAuthenticated && scriptsReady && gGameState==9 && CWorld::PlayerInFocus==0 && FindPlayerPed(0);}
TrailerSync::Ref Identity(CNetworkVehicle* v){return v?TrailerSync::Ref{uint32_t(v->m_nVehicleId),v->m_generation,v->m_nModelId}:TrailerSync::Ref{};}
CVehicle* Resolve(const TrailerSync::Ref& ref){
 auto* v=ref.Valid()?CNetworkVehicleManager::GetVehicle(int(ref.id)):nullptr;
 return v && TrailerSync::Same(ref,Identity(v)) && CTrailerSync::NativeValid(v)?v->m_pVehicle:nullptr;
}
TrailerSync::Frame Capture(CVehicle* v){TrailerSync::Frame f;const auto& m=*v->m_matrix;
 f.position={m.pos.x,m.pos.y,m.pos.z};f.right={m.right.x,m.right.y,m.right.z};f.forward={m.up.x,m.up.y,m.up.z};
 f.velocity={v->m_vecMoveSpeed.x,v->m_vecMoveSpeed.y,v->m_vecMoveSpeed.z};f.turn={v->m_vecTurnSpeed.x,v->m_vecTurnSpeed.y,v->m_vecTurnSpeed.z};
 f.health=v->m_fHealth;return f;
}
bool Owned(const TrailerSync::Ref& ref){return Room().Current(ref) && Room().leases[ref.id].owner==CNetworkPlayerManager::m_nMyId;}
void Clear(Applied& a){
 if(a.scene==scene && Pool(a.childPtr,a.childRef) && Pool(a.parentPtr,a.parentRef) &&
  a.childPtr->m_pTractor==a.parentPtr && a.parentPtr->m_pTrailer==a.childPtr){++replay;a.childPtr->BreakTowLink();--replay;}
 a={};
}
void Write(CVehicle* v,const TrailerSync::Frame& f){
 v->m_matrix->pos=CVector(f.position.x,f.position.y,f.position.z);v->m_matrix->right=CVector(f.right.x,f.right.y,f.right.z);
 v->m_matrix->up=CVector(f.forward.x,f.forward.y,f.forward.z);
 v->m_matrix->at=CVector(f.right.y*f.forward.z-f.right.z*f.forward.y,f.right.z*f.forward.x-f.right.x*f.forward.z,f.right.x*f.forward.y-f.right.y*f.forward.x);
 v->m_vecMoveSpeed=CVector(f.velocity.x,f.velocity.y,f.velocity.z);v->m_vecTurnSpeed=CVector(f.turn.x,f.turn.y,f.turn.z);v->m_fHealth=f.health;
 v->UpdateRwFrame();
}
void Apply(){
 for(uint32_t id=0;id<TrailerSync::MaxVehicles;++id){const auto& l=Room().links[id];auto& a=applied[id];
  const bool current=Room().Linked(id,l.child.generation);auto* child=current?Resolve(l.child):nullptr;auto* parent=current?Resolve(l.parent):nullptr;
  if(!child||!parent){Clear(a);if(!l.attached)observations[id].nativeDetach=false;continue;}
  if(a.childPtr && (!TrailerSync::Same(a.child,l.child)||!TrailerSync::Same(a.parent,l.parent)||!Pool(a.childPtr,a.childRef)||!Pool(a.parentPtr,a.parentRef)))Clear(a);
  const bool owner=Owned(l.parent);
  if(!owner || (!observations[id].nativeDetach && (child->m_pTractor!=parent || parent->m_pTrailer!=child))){
   ++replay;
   if(child->m_pTractor && child->m_pTractor!=parent && Pool(child->m_pTractor))child->BreakTowLink();
   if(parent->m_pTrailer && parent->m_pTrailer!=child && Pool(parent->m_pTrailer))parent->m_pTrailer->BreakTowLink();
   if(child->m_pTractor!=parent || parent->m_pTrailer!=child){child->m_nStatus=4;Write(child,l.frame);child->SetTowLink(parent,false);}
   --replay;
   if(child->m_pTractor==parent && parent->m_pTrailer==child)Write(child,l.frame);
  }
  if(child->m_pTractor==parent && parent->m_pTrailer==child)a={l.parent,l.child,parent,child,CPools::GetVehicleRef(parent),CPools::GetVehicleRef(child),scene};
 }
}
void Publish(){
 const uint32_t now=GetTickCount();
 for(auto* v:CNetworkVehicleManager::m_pVehicles){if(!CTrailerSync::NativeValid(v)||!Identity(v).Valid())continue;
  auto* child=v->m_pVehicle;auto childRef=Identity(v);auto* nativeParent=Pool(child->m_pTractor)?child->m_pTractor:nullptr;
  auto* parentMap=nativeParent?CNetworkVehicleManager::GetVehicle(nativeParent):nullptr;auto parentRef=Identity(parentMap);
  auto& seen=observations[childRef.id];const auto& canonical=Room().links[childRef.id];
  if(!parentRef.Valid() && canonical.attached)parentRef=canonical.parent;
  if(!parentRef.Valid() && seen.desired.parent.Valid())parentRef=seen.desired.parent;
  if(!Room().Current(childRef)||!Owned(parentRef)||!Resolve(parentRef)||!TrailerSync::Pair(parentRef.model,childRef.model))continue;
  const bool attached=nativeParent && parentMap && TrailerSync::Same(parentRef,Identity(parentMap)) && nativeParent->m_pTrailer==child;
  const auto frame=Capture(child);if(!frame.Valid())continue;
  const auto lease=Room().leases[parentRef.id].epoch;
  if(!TrailerSync::Same(seen.desired.parent,parentRef)||!TrailerSync::Same(seen.desired.child,childRef)||seen.desired.ownerEpoch!=lease||seen.desired.attached!=attached){
   if(intents[parentRef.id]==TrailerSync::MaxCounter)continue;
   Packets::Trailers::Link p;p.scene=scene;p.link.room=Room().room;p.link.parent=parentRef;p.link.child=childRef;p.link.ownerEpoch=lease;
   p.link.sequence=++intents[parentRef.id];p.link.attached=attached;p.link.frame=frame;
   if(attached||canonical.attached)GetPacketFactory().Send(p);seen.desired=p.link;seen.time=now;
  }
  if(attached && canonical.attached && TrailerSync::Same(canonical.parent,parentRef) && canonical.ownerEpoch==lease && now-seen.time>=50 && canonical.sequence<TrailerSync::MaxCounter){
   Packets::Trailers::Pose p;p.scene=scene;p.link=canonical;p.link.sequence++;p.link.frame=frame;
   Room().Position(p.link);GetPacketFactory().Send(p);seen.time=now;
  }
 }
}
}
uint32_t CTrailerSync::Scene(){return scene;}
bool CTrailerSync::NativeValid(const CNetworkVehicle* v){return v && scene && v->m_createdScene==scene &&
 Pool(v->m_pVehicle,v->m_nVehiclePoolRef) && v->m_pVehicle->m_matrix && v->m_pVehicle->m_nModelIndex==v->m_nModelId;}
bool CTrailerSync::ConfirmValid(CNetworkVehicle* v,uint32_t request){return request && v && !v->m_generation &&
 v->m_requestToken==request && NativeValid(v);}
bool CTrailerSync::Replaying(){return replay!=0;}
void CTrailerSync::EnableNative(){nativeEnabled=true;}
void CTrailerSync::Init(){NativeInit();
 Events::initScriptsEvent.before+=[]{controllingRestart=scene && CNetwork::m_bAuthenticated && CLocalPlayer::m_bIsHost;Reset(true);if(scene<TrailerSync::MaxCounter)++scene;scriptsReady=false;};
 Events::processScriptsEvent.after+=[]{if(gGameState==9)scriptsReady=true;};
 gameShutdownEvent.before+=[]{Reset();scriptsReady=false;};
}
void CTrailerSync::Reset(bool preserveBirths){
 for(auto& temp:CNetworkVehicleManager::m_apTempVehicles)if(temp){auto* expired=temp;temp=nullptr;expired->m_pVehicle=nullptr;delete expired;}
 for(auto& p:pendingSpawns)p.reset();for(auto& a:applied)Clear(a);Room()={};observations={};intents={};pendingBirth={};attemptTime={};acknowledgedScene=lastHello=0;
 if(!preserveBirths){peerConnection=CNetwork::m_pPeer?CNetwork::m_pPeer->connectID:0;retired={};for(auto* v:CNetworkVehicleManager::m_pVehicles)if(v){v->m_createdScene=0;v->m_generation=0;}}
}
void CTrailerSync::NativeRemoved(CVehicle* native){
 for(auto& temp:CNetworkVehicleManager::m_apTempVehicles)if(temp && temp->m_pVehicle==native && temp->m_requestToken && NativeValid(temp)){
  auto* expired=temp;temp=nullptr;expired->m_pVehicle=nullptr;delete expired;
 }
 auto* v=CNetworkVehicleManager::GetVehicle(native);if(!v)return;
 VehicleRemoved(v->m_nVehicleId,v->m_generation);v->m_createdScene=0;v->m_nVehiclePoolRef=-1;}
void CTrailerSync::VehicleRemoved(int id,uint32_t birth){if(id<0||id>=TrailerSync::MaxVehicles)return;
 for(uint32_t i=0;i<TrailerSync::MaxVehicles;++i){auto& a=applied[i];if((a.child.id==uint32_t(id)&&a.child.generation==birth)||(a.parent.id==uint32_t(id)&&a.parent.generation==birth))Clear(a);}
}
bool CTrailerSync::LegacyAllowed(CNetworkVehicle* v){return !nativeEnabled || !v || !Room().Linked(uint32_t(v->m_nVehicleId),v->m_generation);}
bool CTrailerSync::AllowAttach(CVehicle* child,CVehicle* parent){
 if(!Ready()||Replaying())return true;
 auto* c=CNetworkVehicleManager::GetVehicle(child);auto* p=CNetworkVehicleManager::GetVehicle(parent);
 if(!c&&!p)return true;
 if(!NativeValid(c)||!NativeValid(p)||acknowledgedScene!=scene||!Room().Current(Identity(c))||!Owned(Identity(p))||
  !TrailerSync::Pair(p->m_nModelId,c->m_nModelId))return false;
 if(parent->m_pTrailer && parent->m_pTrailer!=child)return false;
 if(child->m_pTractor && child->m_pTractor!=parent)return false;
 observations[c->m_nVehicleId].nativeDetach=false;return true;
}
bool CTrailerSync::AllowDetach(CVehicle* child){if(!Ready()||Replaying())return true;
 auto* c=CNetworkVehicleManager::GetVehicle(child);if(!c)return true;
 if(!NativeValid(c))return false;const auto id=c->m_nVehicleId;
 if(id<0||id>=TrailerSync::MaxVehicles||!Room().links[id].attached)return true;
 if(!Owned(Room().links[id].parent))return false;observations[id].nativeDetach=true;return true;
}
void CTrailerSync::Queue(Packet& p){if(p.GetType()!=ePacketType::TRAILER_LEASE && p.GetType()!=ePacketType::TRAILER_LINK)return;
 for(auto it=GetPacketBuffer().m_packets.rbegin();it!=GetPacketBuffer().m_packets.rend();++it)if((*it)->GetChannel()==ePacketChannel::EVENT){p.serverTime=(std::max)(p.serverTime,(*it)->serverTime);break;}}
void CTrailerSync::Receive(const Packets::Trailers::Lease& p){if(!CNetwork::m_bAuthenticated||!TrailerSync::Counter(p.room))return;
 if(p.reset){if(p.scene!=scene || p.room<Room().room)return;if(p.room>Room().room){for(auto& a:applied)Clear(a);Room().Reset(p.room);observations={};intents={};}acknowledgedScene=p.scene;return;}
 if(p.room==Room().room && Room().Bind(p.lease) && !p.lease.live)RetireVehicle(int(p.lease.vehicle.id),p.lease.vehicle.generation);
}
void CTrailerSync::Receive(const Packets::Trailers::Link& p){if(CNetwork::m_bAuthenticated)Room().State(p.link);}
void CTrailerSync::Receive(const Packets::Trailers::Pose& p){if(CNetwork::m_bAuthenticated)Room().Position(p.link);}
void CTrailerSync::Process(){if(!CNetwork::m_bAuthenticated){Reset();return;}if(!GameplayReady())return;
 const uint32_t conn=CNetwork::m_pPeer?CNetwork::m_pPeer->connectID:0;
 if(peerConnection!=conn){Reset();peerConnection=conn;controllingRestart=false;}
 const uint32_t retryClock=GetTickCount();
 for(uint32_t id=0;id<TrailerSync::MaxVehicles;++id)if(pendingSpawns[id] && (!attemptTime[id]||retryClock-attemptTime[id]>=500)){
  auto packet=std::move(pendingSpawns[id]);attemptTime[id]=retryClock;GetPacketHandler().ProcessPacket(packet.get());
 }
 const uint32_t now=GetTickCount();if(acknowledgedScene!=scene || now-lastHello>2000){Packets::Trailers::Hello p;p.scene=scene;p.controllingRestart=controllingRestart;
  if(now-lastHello>=1000||!lastHello){GetPacketFactory().Send(p);lastHello=now;}}
 if(acknowledgedScene!=scene||!Room().room||!Ready())return;Apply();Publish();
}

bool CTrailerSync::GameplayReady(){return CNetwork::m_bAuthenticated && scene && scriptsReady && gGameState==9 && CPools::ms_pVehiclePool && FindPlayerPed(0);}
void CTrailerSync::QueueSpawn(const Packets::Vehicles::VehicleSpawn& p){
 if(!CanSpawn(p.vehicleid,p.generation))return;
 auto& old=pendingSpawns[p.vehicleid];if(!old || old->generation<=p.generation){if(pendingBirth[p.vehicleid]!=p.generation)attemptTime[p.vehicleid]=0;pendingBirth[p.vehicleid]=p.generation;old=std::make_unique<Packets::Vehicles::VehicleSpawn>(p);}
}

bool CTrailerSync::CanSpawn(int id,uint32_t birth){return id>=0 && id<TrailerSync::MaxVehicles && TrailerSync::Counter(birth) && birth>retired[id];}
void CTrailerSync::RetireVehicle(int id,uint32_t birth){if(id<0||id>=TrailerSync::MaxVehicles||!TrailerSync::Counter(birth))return;
 retired[id]=(std::max)(retired[id],birth);if(pendingSpawns[id]&&pendingSpawns[id]->generation<=birth)pendingSpawns[id].reset();VehicleRemoved(id,birth);
}
