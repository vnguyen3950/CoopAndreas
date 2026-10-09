#include "pickup_server_doubles.h"
#include <limits>
#include "pickup_server.inc"
static unsigned checks=0,failures=0;
static void expect(bool value,const char*text){++checks;if(!value){++failures;std::cout<<"FAIL: "<<text<<'\n';}}
int main(){
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
