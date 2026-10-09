#include "client_doubles.h"
#include "client/src/CTrailerSync.h"
#include "extracted_client.inc"
#include "vehicle_client_handlers.inc"
void CTrailerSync::NativeInit(){EnableNative();}
bool CVehicle::SetTowLink(CVehicle* p,bool){if(!CTrailerSync::AllowAttach(this,p))return false;++nativeAttach;m_pTractor=p;p->m_pTrailer=this;m_nStatus=7;return true;}
bool CVehicle::BreakTowLink(){if(!CTrailerSync::AllowDetach(this))return false;++nativeDetach;if(m_pTractor)m_pTractor->m_pTrailer=nullptr;m_pTractor=nullptr;m_nStatus=4;return true;}
bool CNetworkVehicle::HasValidVehicle()const{return CTrailerSync::NativeValid(this);}
#include "vehicle_manager_getters.inc"
#ifndef ACTUAL_CONSTRUCTOR_TEST
CNetworkVehicle::CNetworkVehicle(int id,int model,CVector pos,float,unsigned char,unsigned char,unsigned char,uint32_t birth){
 if(auto* old=CNetworkVehicleManager::FindVehicle(id))CNetworkVehicleManager::Remove(old);
 m_nVehicleId=id;m_nModelId=model;m_generation=birth;m_createdScene=CTrailerSync::Scene();m_pVehicle=new CVehicle;
 m_pVehicle->m_nModelIndex=model;m_pVehicle->m_matrix->pos=pos;m_nVehiclePoolRef=++nativeSpawn;CPools::pool.refs[m_pVehicle]=m_nVehiclePoolRef;
}
#else
#include "vehicle_constructor_extracted.inc"
#endif
void Handler::ProcessPacket(Packet* p){if(p->GetType()==ePacketType::VEHICLE_SPAWN)ReceiveVehicleSpawn(static_cast<Packets::Vehicles::VehicleSpawn*>(p));}
static void Map(CNetworkVehicle& m,CVehicle& v,int id,int model,uint32_t birth,int ref){m.m_nVehicleId=id;m.m_nModelId=model;m.m_generation=birth;m.m_pVehicle=&v;m.m_nVehiclePoolRef=ref;m.m_createdScene=scene;
 v.m_nModelIndex=model;CPools::pool.refs[&v]=ref;CNetworkVehicleManager::Add(&m);}
static void Start(){CTrailerSync::Reset();CNetworkVehicleManager::m_pVehicles.clear();CPools::pool.refs.clear();scene=1;scriptsReady=true;nativeEnabled=true;peerConnection=1;gGameState=9;nativeAttach=nativeDetach=0;
 Packets::Trailers::Lease receipt;receipt.room=1;receipt.scene=1;receipt.reset=true;CTrailerSync::Receive(receipt);}
static void Bind(const CNetworkVehicle& v,int owner){Packets::Trailers::Lease p;p.room=1;p.lease={{uint32_t(v.m_nVehicleId),v.m_generation,v.m_nModelId},1,owner,true};CTrailerSync::Receive(p);}
static TrailerSync::Link Link(const CNetworkVehicle& cab,const CNetworkVehicle& child){TrailerSync::Link l;l.room=l.ownerEpoch=l.sequence=l.revision=1;l.parent=Identity(const_cast<CNetworkVehicle*>(&cab));l.child=Identity(const_cast<CNetworkVehicle*>(&child));l.attached=true;return l;}
int main(){Start();CVehicle cab,child;CNetworkVehicle pc,cc;Map(pc,cab,0,403,1,100);Map(cc,child,1,435,2,101);Bind(pc,0);Bind(cc,0);
 expect(!child.SetTowLink(&cab,false),"Remote native auto-scan cannot create independent attachment");expect(nativeAttach==0&&!child.m_pTractor&&!cab.m_pTrailer,"Rejected native attach leaves both references untouched");
 Packets::Trailers::Link update;update.link=Link(pc,cc);CTrailerSync::Receive(update);CTrailerSync::Process();
 expect(child.m_pTractor==&cab&&cab.m_pTrailer==&child&&nativeAttach==1,"Actual service initializes reciprocal native articulation exactly once");
 CTrailerSync::Process();expect(nativeAttach==1,"Repeated canonical state does not duplicate native attach");
 expect(!child.BreakTowLink()&&nativeDetach==0,"Remote native physics cannot detach canonical link independently");
 Packets::Trailers::Pose pose;pose.link=update.link;pose.link.sequence=2;pose.link.frame.position={5,6,7};pose.link.frame.velocity={1,0,0};CTrailerSync::Receive(pose);CTrailerSync::Process();
 expect(child.matrix.pos.x==5&&child.m_vecMoveSpeed.x==1,"Actual articulated pose and native velocity apply");
 update.link.attached=false;update.link.revision=2;CTrailerSync::Receive(update);CTrailerSync::Process();
 expect(!child.m_pTractor&&!cab.m_pTrailer&&nativeDetach==1,"Canonical detach clears both native references");
 CTrailerSync::Receive(pose);CTrailerSync::Process();expect(!child.m_pTractor,"Delayed pose cannot revive detached linkage");
 update.link.attached=true;update.link.revision=3;CTrailerSync::Receive(update);CTrailerSync::Process();
 CTrailerSync::NativeRemoved(&child);CPools::pool.refs[&child]=101;child.m_pTractor=nullptr;cab.m_pTrailer=nullptr;
 CTrailerSync::Process();expect(!child.m_pTractor&&!CTrailerSync::NativeValid(&cc),"Native lifetime invalidation rejects repeated same full reference and model");
 expect(!CTrailerSync::LegacyAllowed(&cc),"Legacy child pose stream is suppressed while canonical link owns child");
 // Actual spawn/confirm/remove handlers: no SDK creation in the authenticated menu.
 Start();scene=0;gGameState=0;scriptsReady=false;Packets::Vehicles::VehicleSpawn spawn;spawn.vehicleid=8;spawn.generation=10;spawn.modelid=403;
 int count=nativeSpawn;ReceiveVehicleSpawn(&spawn);expect(nativeSpawn==count&&!CNetworkVehicleManager::FindVehicle(8),"Menu spawn is deferred before native construction");
 CTrailerSync::Init();Events::initScriptsEvent.Run();Events::processScriptsEvent.Run();gGameState=9;Events::processScriptsEvent.Run();CTrailerSync::Process();
 spawn.vehicleid=8;ReceiveVehicleSpawn(&spawn);auto* replayed=CNetworkVehicleManager::GetVehicle(8);expect(replayed&&replayed->m_generation==10&&replayed->m_createdScene==scene,"Matching first-game replay restores unlinked native vehicle mapping");
 Packets::Vehicles::AssignVehicleSyncer assign;assign.vehicleid=8;assign.generation=10;assign.syncerId=1;ReceiveAssignVehicleSyncer(&assign);ReceiveAssignVehicleSyncer(&assign);
 expect(replayed->m_bSyncing,"Explicit syncer replay is idempotent rather than toggling control off");
 CNetworkVehicle pending;CVehicle pendingNative;Map(pending,pendingNative,9,403,0,111);pending.m_requestToken=22;CNetworkVehicleManager::Remove(&pending);CNetworkVehicleManager::m_apTempVehicles[1]=&pending;
 Packets::Vehicles::VehicleConfirm confirm;confirm.tempid=1;confirm.vehicleid=9;confirm.generation=20;confirm.requestToken=21;ReceiveVehicleConfirm(&confirm);
 expect(!pending.m_generation,"Old confirmation cannot bind new temporary vehicle with reused temp ID");confirm.requestToken=22;ReceiveVehicleConfirm(&confirm);expect(pending.m_generation==20,"Matching nonce confirms exact current native vehicle");
 Packets::Vehicles::VehicleRemove removal;removal.vehicleid=8;removal.generation=9;ReceiveVehicleRemove(&removal);expect(CNetworkVehicleManager::GetVehicle(8)==replayed,"Old removal cannot delete reused server slot");
 // Connected guest restart: old wrappers invalidate; current replay restores BOTH members.
 spawn.vehicleid=0;spawn.generation=1;spawn.modelid=403;ReceiveVehicleSpawn(&spawn);spawn.vehicleid=1;spawn.generation=2;spawn.modelid=435;ReceiveVehicleSpawn(&spawn);
 auto* cp=CNetworkVehicleManager::GetVehicle(0);auto* ct=CNetworkVehicleManager::GetVehicle(1);Packets::Trailers::Lease receipt;receipt.room=1;receipt.scene=scene;receipt.reset=true;CTrailerSync::Receive(receipt);Bind(*cp,0);Bind(*ct,0);
 update.link=Link(*cp,*ct);CTrailerSync::Receive(update);CTrailerSync::Process();expect(ct->m_pVehicle->m_pTractor==cp->m_pVehicle,"Initialized pair native linkage is live before restart");
 Events::initScriptsEvent.Run();expect(!CNetworkVehicleManager::GetVehicle(0)&&!CNetworkVehicleManager::GetVehicle(1),"Pre-init wrappers become unresolved after script incarnation changes");
 gGameState=9;Events::processScriptsEvent.Run();receipt.scene=scene;CTrailerSync::Receive(receipt);CTrailerSync::Receive(update);CTrailerSync::Process();expect(!CNetworkVehicleManager::GetVehicle(0),"Scene receipt alone cannot adopt repeated old native references");
 spawn.vehicleid=0;spawn.modelid=403;spawn.generation=1;ReceiveVehicleSpawn(&spawn);spawn.vehicleid=1;spawn.modelid=435;spawn.generation=2;ReceiveVehicleSpawn(&spawn);
 cp=CNetworkVehicleManager::GetVehicle(0);ct=CNetworkVehicleManager::GetVehicle(1);Bind(*cp,0);Bind(*ct,0);CTrailerSync::Process();
 expect(cp&&ct&&cp->m_generation==1&&ct->m_generation==2&&ct->m_pVehicle->m_pTractor==cp->m_pVehicle,"Matching pair replay restores both identities and real native link after guest restart");
 Start();CVehicle takeoverCab,takeoverChild;CNetworkVehicle ownerParent,ownerChild;Map(ownerParent,takeoverCab,0,403,31,701);Map(ownerChild,takeoverChild,1,435,32,702);Bind(ownerParent,0);Bind(ownerChild,0);
 update.link=Link(ownerParent,ownerChild);CTrailerSync::Receive(update);CTrailerSync::Process();
 Packets::Trailers::Lease takeover;takeover.room=1;takeover.lease={Identity(&ownerParent),2,1,true};CTrailerSync::Receive(takeover);CTrailerSync::Process();
 update.link.ownerEpoch=2;update.link.revision=2;CTrailerSync::Receive(update);CTrailerSync::Process();
 expect(takeoverChild.m_pTractor==&takeoverCab&&takeoverCab.m_pTrailer==&takeoverChild,"New actual owner adopts canonical native attachment after lease-before-link transfer");
 expect(takeoverChild.BreakTowLink(),"New owner can detach through native input path");CTrailerSync::Process();
 expect(!takeoverChild.m_pTractor,"Canonical pending state cannot undo local owner detach before server reply");
 CTrailerSync::Reset();expect(!takeoverCab.m_pTrailer,"Disconnect/reset clears managed native references without replay echo");
 // Root menu spawn/remove repro executes the actual handlers, then actual Process.
 Start();gGameState=0;scriptsReady=false;spawn.vehicleid=8;spawn.generation=10;spawn.modelid=403;
 count=nativeSpawn;ReceiveVehicleSpawn(&spawn);removal.vehicleid=8;removal.generation=10;ReceiveVehicleRemove(&removal);
 gGameState=9;scriptsReady=true;CTrailerSync::Process();
 expect(!CNetworkVehicleManager::FindVehicle(8),"Menu removal cancels same-birth deferred spawn before native readiness");
 expect(nativeSpawn==count,"Removed menu birth never constructs native vehicle while awaiting HELLO replay");
 Start();gGameState=0;scriptsReady=false;spawn.vehicleid=8;spawn.generation=20;ReceiveVehicleSpawn(&spawn);
 removal.vehicleid=8;removal.generation=10;ReceiveVehicleRemove(&removal);gGameState=9;scriptsReady=true;CTrailerSync::Process();
 expect(CNetworkVehicleManager::GetVehicle(8)&&CNetworkVehicleManager::GetVehicle(8)->m_generation==20,
        "Older removal preserves newer queued birth for reused network slot");
 Start();CVehicle reused;CNetworkVehicle invalidOld,validNew;Map(invalidOld,reused,3,403,30,777);CTrailerSync::NativeRemoved(&reused);Map(validNew,reused,4,403,31,777);
 expect(CNetworkVehicleManager::GetVehicle(static_cast<CEntity*>(&reused))==&validNew,
        "Invalid old first address match cannot mask valid new mapping at same native full reference");
 CNetworkVehicleManager::m_pVehicles={&validNew,&invalidOld};
 expect(CNetworkVehicleManager::GetVehicle(static_cast<CEntity*>(&reused))==&validNew,
        "Valid current mapping resolves independently of registry ordering");
 // Fresh initialized connection: handshake reset precedes ordinary EVENT spawn.
 Start();peerConnection=0;CTrailerSync::Reset();spawn.vehicleid=8;spawn.generation=10;spawn.modelid=403;ReceiveVehicleSpawn(&spawn);
 auto* first=CNetworkVehicleManager::GetVehicle(8);count=nativeSpawn;CTrailerSync::Process();
 expect(CNetworkVehicleManager::GetVehicle(8)==first && first && first->m_generation==10,
        "First Process preserves current-peer vehicle created after handshake Reset");
 ReceiveVehicleSpawn(&spawn);expect(nativeSpawn==count,"First HELLO replay cannot duplicate the already live current-peer native vehicle");
 std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;}
