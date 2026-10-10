#include "pickup_server_doubles.h"
#include <limits>
#include "pickup_server.inc"
static unsigned checks=0,failures=0;
static void expect(bool value,const char*text){++checks;if(!value){++failures;std::cout<<"FAIL: "<<text<<'\n';}}
static unsigned Grants(){unsigned count=0;for(const auto&packet:GetPacketFactory().sent){const auto*p=dynamic_cast<const Packets::Pickups::Action*>(packet.get());
 if(p&&p->operation==Packets::Pickups::Operation::Grant)++count;}return count;}
static void CopCases(){
 using PickupSync::Stage;
 PeerHandle hp,gp,xp;CNetworkPlayer host,guest,other;host.m_pPeer=&hp;host.m_bIsHost=true;
 guest.m_pPeer=&gp;guest.m_iPlayerId=1;guest.m_vitals.generation=guest.life.generation=11;
 other.m_pPeer=&xp;other.m_iPlayerId=2;other.m_vitals.generation=other.life.generation=12;
 CNetworkPed cop;
 auto prepare=[&](CNetworkPlayer*producer){Room()={};peers={};mission=false;copManifests={};serverTime=100;sealAvailable=false;cop={};cop.m_pSyncer=producer;
  nativePeds={{3,&cop}};CNetworkPlayerManager::m_pPlayers={&host,&guest,&other};CPickupServer::Join(&host);CPickupServer::Join(&guest);CPickupServer::Join(&other);CPickupServer::HostChanged(&host);};
 auto packet=[&](CNetworkPlayer*producer){Packets::Pickups::Action p;p.operation=Packets::Pickups::Operation::Create;p.epoch=Room().epoch;p.sequence=1;
  p.actor={producer->m_vitals.generation,2,7,0,0};p.item.owner=producer->m_iPlayerId;p.item.creation=1;p.item.model=346;p.item.type=4;p.item.ammo=15;
  p.item.cop={3,{9,2,6},1,producer->m_vitals.generation};return p;};
 auto seal=[&](CNetworkPlayer*producer){sealAvailable=true;cop.m_deathStamp={9,2,6};cop.m_deathProducer=producer;cop.m_deathProducerGeneration=producer->m_vitals.generation;};
 for(auto*producer:{&host,&guest}){
  prepare(producer);auto p=packet(producer);expect(CPickupServer::Action(p,producer)&&!Room().Find(1)&&copManifests[3].pending,"Actual SYSTEM Create waits inert before reliable EVENT death proof");
  seal(producer);cop.m_vecPos={1000,1000,1000};CPickupServer::ProcessPending();auto*row=Room().Find(1);
  expect(row&&row->stage==Stage::Active&&row->item.owner==producer->m_iPlayerId&&row->item.ammo==15&&copManifests[3].spent,"Actual sealed producer publishes one unchanged native amount for host or guest");
  cop.m_pSyncer=&other;cop.m_ownerEpoch=3;p.sequence=2;expect(CPickupServer::Action(p,producer)&&Room().Find(1)==row&&!Room().Find(2),"Original producer duplicate after transfer acknowledges one retained item only");
  auto replacement=p;replacement.sequence=1;replacement.actor={12,2,7,0,0};replacement.item.owner=2;replacement.item.cop.producerGeneration=12;
  expect(!CPickupServer::Action(replacement,&other),"New owner cannot obtain the original sealed manifest allowance");
  p.sequence=3;p.item.ammo=14;expect(!CPickupServer::Action(p,producer),"Duplicate seal cannot change captured native quantity");
  p.item.ammo=15;p.item.creation=p.item.cop.sequence=2;expect(!CPickupServer::Action(p,producer),"New producer nonce cannot reopen an already spent death");
 }
 prepare(&guest);seal(&guest);auto p=packet(&guest);p.item.position.x=6;expect(CPickupServer::Action(p,&guest)&&!Room().Find(1)&&!copManifests[3].spent,"Out-of-range sealed metadata cannot mint an item or spend another allowance");
 prepare(&guest);seal(&guest);p=packet(&guest);cop.m_deathArea=1;expect(CPickupServer::Action(p,&guest)&&!Room().Find(1),"Retained death interior rejects exterior publication");
 prepare(&guest);seal(&guest);p=packet(&guest);p.item.ammo=16;expect(!CPickupServer::Action(p,&guest),"Stock native ammo envelope rejects quantity beyond verified native limit");
 p.item.ammo=15;p.item.model=334;expect(!CPickupServer::Action(p,&guest),"Nightstick remains unsupported rather than pretending full cop-drop parity");
 p.item.model=346;p.item.type=22;expect(!CPickupServer::Action(p,&guest),"Persistent mission type remains excluded");
 p.item.type=4;p.item.model=352;expect(!CPickupServer::Action(p,&guest),"Retained city-cop model cannot authorize a SWAT stock weapon");
 prepare(&guest);p=packet(&guest);expect(CPickupServer::Action(p,&guest),"Provisional manifest fixture accepted into bounded cache");
 cop.m_generation=10;seal(&guest);CPickupServer::ProcessPending();expect(!Room().Find(1)&&!copManifests[3].spent,"Recycled NPC generation expires provisional request without spending a death allowance");
 prepare(&guest);p=packet(&guest);CPickupServer::Action(p,&guest);serverTime+=15001;CPickupServer::ProcessPending();
 expect(!Room().Find(1)&&copManifests[3].expired&&!copManifests[3].spent,"Expired unknown proof never mints resources or consumes another producer allowance");
 prepare(&guest);p=packet(&guest);CPickupServer::Action(p,&guest);guest.m_vitals.generation=guest.life.generation=21;seal(&guest);CPickupServer::ProcessPending();
 expect(!Room().Find(1),"Reconnected player slot cannot finish an old producer incarnation request");guest.m_vitals.generation=guest.life.generation=11;
 prepare(&guest);p=packet(&guest);p.item.cop.death.sequence=999;CPickupServer::Action(p,&guest);
 cop.m_pSyncer=&other;cop.m_ownerEpoch=3;seal(&other);cop.m_deathStamp={9,3,8};auto actual=packet(&other);actual.item.cop.death={9,3,8};
 expect(CPickupServer::Action(actual,&other)&&Room().Find(1)&&Room().Find(1)->item.owner==2,"Unknown earlier manifest cannot poison another authenticated original death producer allowance");
 prepare(&guest);p=packet(&guest);CPickupServer::Action(p,&guest);guest.life.birth=3;seal(&guest);CPickupServer::ProcessPending();
 expect(!Room().Find(1),"Queued request cannot silently adopt a different current actor life");
 p.sequence=2;p.actor.birth=3;expect(CPickupServer::Action(p,&guest)&&Room().Find(1),"Exact current-life retry can publish the same immutable original manifest");guest.life.birth=2;
 prepare(&guest);seal(&guest);p=packet(&guest);CPickupServer::Action(p,&guest);
 Packets::Pickups::Action remove;remove.operation=Packets::Pickups::Operation::Remove;remove.epoch=Room().epoch;remove.sequence=2;remove.id=1;remove.actor=p.actor;
 expect(CPickupServer::Action(remove,&guest)&&Room().Find(1)->stage==Stage::Removed,"Original guest producer owns cleanup of its canonical loot");
 prepare(&guest);seal(&guest);p=packet(&guest);CPickupServer::Action(p,&guest);guest.m_vitals.generation=guest.life.generation=21;remove.epoch=Room().epoch;remove.actor={21,2,7,0,0};
 expect(!CPickupServer::Action(remove,&guest)&&Room().Find(1)->stage==Stage::Active,"New player occupant cannot remove the old producer's item");guest.m_vitals.generation=guest.life.generation=11;
 prepare(&guest);seal(&guest);p=packet(&guest);CPickupServer::Action(p,&guest);CPickupServer::Leave(&guest);
 expect(Room().Find(1)->stage==Stage::Removed&&Room().Find(1)->reason==PickupSync::Reason::OwnerLeft,"Producer departure retires its loot without a collection reward");
 Room()={};peers={};mission=false;copManifests={};nativePeds.clear();sealAvailable=false;CNetworkPlayerManager::m_pPlayers.clear();GetPacketFactory().sent.clear();
}
static void TerminalReceiptCases(){
 using PickupSync::Reason;using PickupSync::Outcome;using PickupSync::Stage;
 for(auto reason:{Reason::Cleanup,Reason::Expired,Reason::OwnerLeft,Reason::CollectorLeft,Reason::Ambiguous,Reason::SceneEnded})
 for(auto outcome:{Outcome::Consumed,Outcome::DeclinedBeforeApply,Outcome::UnknownAfterApply}){
  Room()={};peers={};mission=false;GetPacketFactory().sent.clear();
  PeerHandle h,g;CNetworkPlayer host,guest;host.m_pPeer=&h;host.m_bIsHost=true;guest.m_pPeer=&g;guest.m_iPlayerId=1;guest.m_vitals.generation=11;guest.life.generation=11;
  CNetworkPlayerManager::m_pPlayers={&host,&guest};CPickupServer::Join(&host);CPickupServer::Join(&guest);CPickupServer::HostChanged(&host);
  Packets::Pickups::Hello hello;hello.actor={11,2,7,0,0};CPickupServer::Hello(hello,&guest);
  Packets::Pickups::Action create;create.operation=Packets::Pickups::Operation::Create;create.epoch=Room().epoch;create.sequence=1;create.actor={10,2,7,0,0};
  create.item.creation=1;create.item.owner=0;create.item.model=346;create.item.type=4;create.item.ammo=10;
  expect(CPickupServer::Action(create,&host),"Terminal fixture registers original item through actual host handler");
  auto*row=Room().Find(1);Packets::Pickups::Action claim;claim.operation=Packets::Pickups::Operation::Claim;claim.epoch=Room().epoch;claim.sequence=1;claim.id=row->item.id;claim.actor={11,2,7,0,0};
  expect(CPickupServer::Action(claim,&guest)&&Grants()==1,"Actual service issues one original grant");
  const auto token=row->grant;Packets::Pickups::Action remove;remove.operation=Packets::Pickups::Operation::Remove;remove.epoch=Room().epoch;remove.sequence=2;remove.id=row->item.id;remove.actor=create.actor;remove.reason=reason;
  expect(CPickupServer::Action(remove,&host)&&row->awaitingOutcome,"Actual host handler preserves held token for all terminal reasons");
  create.sequence=3;create.item.creation=2;expect(CPickupServer::Action(create,&host),"Different pickup can exist while old grant is held");
  auto next=claim;next.id=2;next.sequence=2;expect(!CPickupServer::Action(next,&guest),"Actual service prevents another outstanding reservation");
  GetPacketFactory().sent.clear();CPickupServer::Hello(hello,&guest);expect(Grants()==0,"Removed held identity is never regranted by replay");
  Packets::Pickups::Action result;result.operation=Packets::Pickups::Operation::Result;result.epoch=Room().epoch;result.sequence=3;result.id=row->item.id;result.grant=token;result.actor=claim.actor;result.outcome=outcome;
  auto wrong=result;++wrong.grant;expect(!CPickupServer::Action(wrong,&guest),"Actual service rejects mismatched terminal token");
  wrong=result;++wrong.actor.birth;expect(!CPickupServer::Action(wrong,&guest),"Actual service rejects mismatched terminal actor life");
  wrong=result;++wrong.actor.generation;expect(!CPickupServer::Action(wrong,&guest),"Actual service rejects recycled connection generation");
  guest.life.birth=3;expect(CPickupServer::Action(result,&guest)&&!row->awaitingOutcome,"Exact issued old-life receipt can settle after same-connection actor boundary");
  expect(row->stage==(outcome==Outcome::Consumed?Stage::Collected:Stage::Removed),"Actual receipt never reactivates removed resource");
  const auto revision=row->item.revision;expect(CPickupServer::Action(result,&guest)&&row->item.revision==revision&&Grants()==0,"Exact terminal duplicate replies state without accounting or another grant");
  wrong=result;++wrong.actor.birth;expect(!CPickupServer::Action(wrong,&guest),"Settled duplicate still requires the original actor life");
  next.actor.birth=3;next.sequence=4;expect(CPickupServer::Action(next,&guest)&&Grants()==1,"Terminal settlement unblocks a distinct item without regranting the old pile");
  auto oldClaim=claim;oldClaim.actor.birth=3;oldClaim.sequence=5;expect(!CPickupServer::Action(oldClaim,&guest),"Actual service rejects any new claim on the old terminal identity");
 }
 Room()={};peers={};mission=false;GetPacketFactory().sent.clear();CNetworkPlayerManager::m_pPlayers.clear();
}
int main(){
 CopCases();
 TerminalReceiptCases();
 PeerHandle h,g;CNetworkPlayer host,guest;host.m_pPeer=&h;host.m_bIsHost=true;guest.m_pPeer=&g;guest.m_iPlayerId=1;guest.m_vitals.generation=11;guest.life.generation=11;
 CNetworkPlayerManager::m_pPlayers={&host,&guest};CPickupServer::Join(&host);CPickupServer::Join(&guest);CPickupServer::HostChanged(&host);
 Packets::Pickups::Hello hello;hello.actor={10,2,7,0,0};CPickupServer::Hello(hello,&host);hello.actor.generation=11;CPickupServer::Hello(hello,&guest);
 Packets::Pickups::Action create;create.operation=Packets::Pickups::Operation::Create;create.epoch=Room().epoch;create.sequence=1;create.actor={10,2,7,0,0};
 create.item.creation=1;create.item.owner=0;create.item.model=346;create.item.type=4;create.item.ammo=10;
 expect(!CPickupServer::Action(create,&guest),"Wrong creator peer/life cannot register");expect(CPickupServer::Action(create,&host),"Authenticated initialized host registers native lifetime");
 auto*row=Room().Find(1);Packets::Pickups::Action claim;claim.operation=Packets::Pickups::Operation::Claim;claim.epoch=Room().epoch;claim.sequence=1;claim.id=1;claim.actor={11,2,7,0,0};
 auto wrong=claim;wrong.actor.birth=1;expect(!CPickupServer::Action(wrong,&guest),"Old actor birth rejected before reservation");
 wrong=claim;wrong.actor.sequence=8;expect(!CPickupServer::Action(wrong,&guest),"Future unacknowledged actor sequence rejected");
 guest.known=false;expect(!CPickupServer::Action(claim,&guest),"Unknown/not-ready actor cannot claim");guest.known=true;
 guest.life.ready=false;expect(!CPickupServer::Action(claim,&guest),"Published not-ready actor boundary cannot collect");guest.life.ready=true;
 guest.life.area=1;wrong=claim;wrong.actor.area=1;expect(!CPickupServer::Action(wrong,&guest),"Acknowledged interior actor cannot collect exterior item");guest.life.area=0;
 wrong=claim;wrong.position.x=std::numeric_limits<float>::infinity();expect(!CPickupServer::Action(wrong,&guest),"Nonfinite collector intent is rejected by actual service");
 guest.m_nVehicleId=4;expect(!CPickupServer::Action(claim,&guest),"Canonical vehicle membership rejects optimistic on-foot claim");guest.m_nVehicleId=-1;
 CPickupServer::Mission(&host,true);claim.epoch=Room().epoch;expect(!CPickupServer::Action(claim,&guest),"Host mission gate rejects native resource claim");
 CPickupServer::Mission(&host,false);create.epoch=Room().epoch;create.sequence=2;create.item.creation=2;expect(CPickupServer::Action(create,&host),"Host returns to exterior resource scene");
 row=nullptr;for(auto&r:Room().rows)if(r.item.id&&r.stage==PickupSync::Stage::Active)row=&r;
 claim.epoch=Room().epoch;claim.id=row->item.id;expect(CPickupServer::Action(claim,&guest),"Valid current collector reserves once");
 const auto token=row->grant;expect(!CPickupServer::Action(claim,&guest),"Duplicate claim does not issue another native grant");
 GetPacketFactory().sent.clear();hello.actor={11,2,7,0,0};CPickupServer::Hello(hello,&guest);
 const auto&replay=GetPacketFactory().sent;const auto*reset=replay.empty()?nullptr:dynamic_cast<const Packets::Pickups::State*>(replay[0].get());
 const auto*state=replay.size()<2?nullptr:dynamic_cast<const Packets::Pickups::State*>(replay[1].get());const auto*grantPacket=replay.size()<3?nullptr:dynamic_cast<const Packets::Pickups::Action*>(replay[2].get());
 expect(reset&&reset->reset&&state&&!state->reset&&state->row.grant==token&&grantPacket&&grantPacket->grant==token,"Actual late-join/retry replay orders recipient reset, retained reservation and same grant token");
 // A real host-side native merge quarantines the old active/reserved identity.
 Packets::Pickups::Action remove;remove.operation=Packets::Pickups::Operation::Remove;remove.epoch=Room().epoch;remove.sequence=3;remove.id=row->item.id;remove.actor={10,2,7,0,0};remove.reason=PickupSync::Reason::Ambiguous;
 expect(CPickupServer::Action(remove,&host)&&row->awaitingOutcome,"Retired merged pile retains outstanding original token accounting");
 auto later=claim;later.sequence=2;expect(!CPickupServer::Action(later,&guest),"Retired pile cannot be reserved again or loop declined grants");
 Packets::Pickups::Action result;result.operation=Packets::Pickups::Operation::Result;result.epoch=Room().epoch;result.sequence=4;result.id=row->item.id;result.grant=token;result.actor={11,2,7,0,0};result.outcome=PickupSync::Outcome::Consumed;
 expect(CPickupServer::Action(result,&guest)&&row->stage==PickupSync::Stage::Collected&&!row->awaitingOutcome,"Exact late native-consumed outcome accounts one old grant without full-pile republish");
 expect(CPickupServer::Action(result,&guest)&&row->stage==PickupSync::Stage::Collected,"Duplicate consumed receipt repeats state only");
 result.actor.generation=10;expect(!CPickupServer::Action(result,&guest),"Wrong connection cannot acknowledge another collector token");
 const auto epoch=Room().epoch;const auto oldId=row->item.id;CPickupServer::Leave(&host);expect(Room().epoch>epoch&&!Room().Find(oldId),"Owner departure removes old scene lifetimes");
 std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;
}
