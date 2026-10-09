#include "server_doubles.h"
#include "server/src/CTrailerSync.h"
struct CFireSync {static void VehicleChanged(CNetworkVehicle*,bool=false){}};
#include "extracted_server.inc"
void CNetworkVehicle::ReassignSyncer(CNetworkPlayer* p){m_pSyncer=p;CTrailerSync::Changed(this);}
static void Ready(CNetworkPlayer& p,uint32_t scene=1,bool restart=false){Packets::Trailers::Hello h;h.scene=scene;h.controllingRestart=restart;CTrailerSync::Hello(h,&p);}
int main(){ENetPeer ap,bp,mp;CNetworkPlayer a,b,menu;a.m_pPeer=&ap;b.m_pPeer=&bp;menu.m_pPeer=&mp;a.m_iPlayerId=0;b.m_iPlayerId=1;menu.m_iPlayerId=2;a.m_bIsHost=true;
 CNetworkPlayerManager::m_pPlayers={&a,&b,&menu};CTrailerSync::Join(&a);CTrailerSync::Join(&b);CTrailerSync::Join(&menu);Ready(a);Ready(b);
 CNetworkVehicle cab,child;cab.m_nModelId=403;cab.m_generation=CTrailerSync::AllocateGeneration();cab.m_pSyncer=&a;
 child.m_nVehicleId=1;child.m_nModelId=435;child.m_generation=CTrailerSync::AllocateGeneration();child.m_pSyncer=&a;
 CNetworkVehicleManager::m_pVehicles={&cab,&child};CTrailerSync::Changed(&cab);CTrailerSync::Changed(&child);
 Packets::Trailers::Link req;req.scene=1;auto& l=req.link;l.room=Room().room;l.parent=Identity(&cab);l.child=Identity(&child);l.ownerEpoch=1;l.sequence=1;l.attached=true;
 CTrailerSync::Link(req,&b);expect(!Room().links[1].attached,"Unassigned peer cannot attach mapped vehicles");
 CTrailerSync::Link(req,&a);expect(Room().links[1].attached,"Actual parent owner attaches native trailer pair");
 CNetworkVehicle unlinked;unlinked.m_nVehicleId=7;unlinked.m_nModelId=400;unlinked.m_generation=CTrailerSync::AllocateGeneration();unlinked.m_pSyncer=&a;
 CNetworkVehicleManager::m_pVehicles.push_back(&unlinked);CTrailerSync::Changed(&unlinked);
 GetPacketFactory().sent.clear();Ready(menu);int replayed=0;for(const auto& sent:GetPacketFactory().sent)if(sent.first==2&&sent.second->GetType()==ePacketType::VEHICLE_SPAWN)++replayed;
 expect(replayed==3,"Gameplay-ready menu peer receives bounded replay of linked pair and unlinked mapping");
 const auto replayEpoch=Room().room;GetPacketFactory().sent.clear();Ready(menu,2);replayed=0;
 for(const auto& sent:GetPacketFactory().sent)if(sent.first==2&&sent.second->GetType()==ePacketType::VEHICLE_SPAWN)++replayed;
 expect(Room().room==replayEpoch&&replayed==3,"Guest restart replays all native mappings without resetting another owner's world");
 // Restore menu-only eligibility test below, independently of completed ready replays.
 CTrailerSync::Join(&menu);GetPacketFactory().sent.clear();
 auto rev=Room().links[1].revision;CTrailerSync::Link(req,&a);expect(Room().links[1].revision==rev,"Duplicate reliable attach is idempotent");
 Lease(Room().leases[7]);bool noMenu=true;for(const auto& sent:GetPacketFactory().sent)if(sent.first==2)noMenu=false;
 expect(noMenu&&GetPacketFactory().sent.size()==2,"Menu-only peer never receives lifecycle while both ready peers do");
 b.m_nVehicleId=0;b.m_nSeatId=0;cab.m_pPlayers[0]=&b;CTrailerSync::Changed(&cab);
 expect(Room().leases[0].owner==1 && Room().leases[0].epoch==2,"Current reserved driver supersedes old assigned syncer");
 expect(child.m_pSyncer==&b && Room().links[1].ownerEpoch==2,"Driver takeover transfers linked child lease and pose authority");
 Packets::Trailers::Pose pose;pose.scene=1;pose.link=Room().links[1];pose.link.sequence++;
 CTrailerSync::Pose(pose,&a);expect(Room().links[1].sequence==1,"Delayed old owner cannot move trailer after reliable driver takeover");
 CTrailerSync::Pose(pose,&b);expect(Room().links[1].sequence==2,"New driver publishes articulated pose");
 pose.link.frame.position.x=std::numeric_limits<float>::quiet_NaN();CTrailerSync::Pose(pose,&b);expect(Room().links[1].frame.position.x==0,"Actual service rejects nonfinite pose");
 pose.link=Room().links[1];pose.link.sequence++;pose.link.child.generation++;CTrailerSync::Pose(pose,&b);expect(Room().links[1].sequence==2,"Wrong child birth cannot move reused slot");
 req.link=Room().links[1];req.link.revision=0;req.link.sequence=2;req.link.attached=false;CTrailerSync::Link(req,&b);
 expect(!Room().links[1].attached,"Current driver detaches with canonical native lifecycle");
 req.link.attached=true;req.link.sequence++;CTrailerSync::Link(req,&b);expect(Room().links[1].attached,"Retry attaches with fresh intent sequence");
 CTrailerSync::Changed(&cab,true);expect(!Room().links[1].attached,"Parent removal tears down child link");
 cab.m_generation++;CTrailerSync::Changed(&cab);req.link.parent=Identity(&cab);req.link.ownerEpoch=1;req.link.sequence=1;CTrailerSync::Link(req,&b);
 expect(Room().links[1].attached,"Fresh parent birth can reuse slot without inheriting old tombstone");
 CTrailerSync::Leave(&b);expect(!Room().links[1].attached,"Owner disconnect detaches retained links before wrapper removal");
 cab.m_pPlayers[0]=nullptr;cab.m_bUsedByPed=true;CTrailerSync::Changed(&cab);expect(Room().leases[0].owner==-1,"Unstamped NPC flag is never an authority source");
 CNetworkPed npc;npc.m_pSyncer=&a;npc.m_nPedId=3;npc.m_generation=7;npc.m_ownerEpoch=4;npc.m_lastState.driver.stamp={7,4};CNetworkPedManager::peds={&npc};
 CTrailerSync::NpcDriver(&cab,&npc);expect(Room().leases[0].owner==0,"Stamped current NPC driver owner controls parent");
 npc.m_ownerEpoch++;CTrailerSync::Changed(&cab);expect(Room().leases[0].owner==-1,"Stale NPC driver incarnation cannot retain authority");
 npc.m_ownerEpoch=4;npc.m_stateSequence=2;CTrailerSync::NpcDriver(&cab,&npc);
 expect(Room().leases[0].owner==-1,"Stale NPC driver sequence cannot serve as a current driver lease");
 cab.m_bUsedByPed=false;cab.m_pPlayers[0]=reinterpret_cast<CNetworkPlayer*>(uintptr_t(1));CTrailerSync::Changed(&cab);
 expect(Room().leases[0].owner==-1,"Unregistered driver pointer rejected before dereferencing freed player state");
 cab.m_pPlayers[0]=&b;b.m_nVehicleId=0;b.m_nSeatId=0;CTrailerSync::Changed(&cab);auto formerEpoch=Room().leases[0].epoch;
 CTrailerSync::Join(&b);Ready(b);CTrailerSync::Changed(&cab);
 expect(Room().leases[0].epoch>formerEpoch,"Reused player slot and pointer receives a fresh connection-owned lease epoch");
 auto epoch=Room().room;Ready(a,2,true);expect(Room().room>epoch,"Controlling-host scene restart invalidates old room");
 std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;}
