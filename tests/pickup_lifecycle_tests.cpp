#include <array>
#include <iostream>
#include <limits>
struct Factory{template<class T>void RegisterPacket(T*p){delete p;}};
Factory&GetPacketFactory(){static Factory f;return f;}
#include "network/packets/pickups.h"
using namespace PickupSync;
static unsigned checks=0,failures=0;
static void expect(bool value,const char*text){++checks;if(!value){++failures;std::cout<<"FAIL: "<<text<<'\n';}}
static Actor Life(uint32_t birth=1){return {1,birth,1,0,0};}
static Item Metadata(uint32_t token=1){Item i;i.owner=0;i.creation=token;i.type=3;i.model=1240;return i;}
static void RegistryCases(){
 Room room;expect(room.ChangeHost(0,Life()),"Known initialized host establishes epoch");
 auto metadata=Metadata();expect(!room.Create(1,room.epoch,Life(),metadata),"Guest cannot register host resource");
 auto*row=room.Create(0,room.epoch,Life(),metadata);expect(row&&row->item.id==1,"Successful native lifetime registration allocates nonzero server ID");
 auto id=row->item.id;expect(!room.Create(0,room.epoch,Life(),metadata),"Creation capability replay is rejected");
 expect(!room.Reserve(1,room.epoch,id,{},Position{},false,false),"Unknown actor cannot reserve");
 auto interior=Life();interior.area=1;expect(!room.Reserve(1,room.epoch,id,interior,{},false,false),"Interior collector cannot reserve");
 expect(!room.Reserve(1,room.epoch,id,Life(),{},true,false)&&!room.Reserve(1,room.epoch,id,Life(),{},false,true),"Mission and vehicle gates precede reservation");
 Position far;far.x=2;expect(!room.Reserve(1,room.epoch,id,Life(),far,false,false),"Native touching envelope excludes distant collector");
 expect(room.Reserve(1,room.epoch,id,Life(),{},false,false)==row,"Authenticated eligible actor reserves actual ID");
 const auto token=row->grant;expect(!room.Reserve(2,room.epoch,id,Life(),{},false,false)&&row->collector==1&&row->grant==token,"Simultaneous other collector cannot duplicate reservation");
 expect(!room.Complete(2,room.epoch,id,token,Life(),Outcome::Consumed),"Wrong collector cannot acknowledge benefit");
 expect(!room.Complete(1,room.epoch,id,token,Life(2),Outcome::Consumed),"Grant cannot cross actor birth");
 expect(room.Complete(1,room.epoch,id,token,Life(),Outcome::DeclinedBeforeApply)&&row->stage==Stage::Active,"Verified pre-apply decline can reopen unmutated resource");
 expect(!room.Complete(1,room.epoch,id,token,Life(),Outcome::Consumed),"Late old grant result cannot overwrite active row");
 room.Reserve(2,room.epoch,id,Life(),{},false,false);expect(row->grant>token,"A new reservation never reuses grant token");
 expect(room.Complete(2,room.epoch,id,row->grant,Life(),Outcome::UnknownAfterApply)&&row->stage==Stage::Removed,"Possible benefit after native apply retires ambiguity instead of reopening");
 row=room.Create(0,room.epoch,Life(),Metadata(2));expect(row&&row->item.id>id,"Retired storage slot receives a distinct network ID");
 id=row->item.id;room.Reserve(1,room.epoch,id,Life(),{},false,false);room.RetireCollector(1);
 expect(row->stage==Stage::Removed&&row->reason==Reason::CollectorLeft,"Lost collector cannot free ambiguous award for another claim");
 row=room.Create(0,room.epoch,Life(),Metadata(3));room.Reserve(1,room.epoch,row->item.id,Life(),{},false,false);
 const auto consumedId=row->item.id,grant=row->grant;
 expect(room.Complete(1,room.epoch,consumedId,grant,Life(),Outcome::Consumed)&&row->stage==Stage::Collected,"Actual consumed result marks collection separately from removal");
 expect(!room.Complete(1,room.epoch,consumedId,grant,Life(),Outcome::Consumed),"Duplicate outcome has no second accounting transition");
 auto oldEpoch=room.epoch;room.ChangeHost(1,Life(2));expect(room.epoch>oldEpoch&&!room.Find(consumedId),"Owner departure/new scene invalidates old IDs");
 metadata=Metadata(1);metadata.owner=1;row=room.Create(1,room.epoch,Life(2),metadata);expect(row&&row->item.id>consumedId,"Epoch reset does not reset global pickup allocator");
 View view;view.Reset(room.epoch,1);expect(view.Accept(*row)&&!view.Accept(*row),"Late join receives one current active row and rejects duplicate revision");
 auto tombstone=*row;tombstone.stage=Stage::Removed;++tombstone.item.revision;expect(view.Accept(tombstone)&&!view.Accept(*row),"Old active replay cannot undo current removal");
 for(int type:{2,15,16,22,6,7,17,18}){auto x=Metadata();x.type=type;expect(!x.ValidMetadata(),"Unsupported respawn/mission/shop/collectible behavior is outside ordinary slice");}
 auto x=Metadata();x.position.x=std::numeric_limits<float>::quiet_NaN();expect(!x.ValidMetadata(),"Nonfinite pickup placement rejected");
 Receipt receipt;receipt.applied=true;receipt.epoch=1;receipt.id=7;receipt.grant=8;receipt.actor=Life();receipt.outcome=Outcome::Consumed;
 expect(receipt.Matches(1,7,8,Life())&&!receipt.Matches(1,7,8,Life(2)),"Applied receipt replay is bound to exact collector lifetime");
}
template<class T>void Roundtrip(T packet){
 std::array<uint32_t,512>words{};serialize::WriteStream write(reinterpret_cast<uint8_t*>(words.data()),sizeof(words));
 expect(static_cast<Packet&>(packet).SerializeWrite(write),"Actual supported packet serializes");write.Flush();const int size=write.GetBytesProcessed();
 T result;serialize::ReadStream read(reinterpret_cast<uint8_t*>(words.data()),size);expect(static_cast<Packet&>(result).SerializeRead(read),"Actual packet roundtrip succeeds");
 for(int n=0;n<size;++n){T cut;serialize::ReadStream shorter(reinterpret_cast<uint8_t*>(words.data()),n);expect(!static_cast<Packet&>(cut).SerializeRead(shorter),"Each truncated payload is rejected");}
 expect(static_cast<Packet&>(packet).GetChannel()==ePacketChannel::SYSTEM,"Pickup operations share reliable identity/lifecycle ordering");
}
int main(){
 RegistryCases();Packets::Pickups::Hello hello;hello.actor=Life();Roundtrip(hello);
 Room room;room.ChangeHost(0,Life());auto*row=room.Create(0,room.epoch,Life(),Metadata());
 Packets::Pickups::State state;state.epoch=room.epoch;state.host=0;state.recipient=Life();Roundtrip(state);state.reset=false;state.row=*row;Roundtrip(state);
 room.Reserve(1,room.epoch,row->item.id,Life(),{},false,false);state.row=*row;Roundtrip(state);
 for(int mode=0;mode<=int(Packets::Pickups::Operation::Replay);++mode){Packets::Pickups::Action p;p.operation=Packets::Pickups::Operation(mode);p.epoch=1;p.sequence=1;p.actor=Life();p.item=Metadata();p.id=1;p.grant=1;Roundtrip(p);}
 hello.position.y=std::numeric_limits<float>::infinity();serialize::MeasureStream measure;
 expect(!static_cast<Packet&>(hello).SerializeMeasure(measure),"Invalid finite envelope fails actual writer before native calls");
 std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;
}
