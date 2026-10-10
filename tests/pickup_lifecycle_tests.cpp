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
static void TerminalReceiptCases(){
 for(auto reason:{Reason::Cleanup,Reason::Expired,Reason::OwnerLeft,Reason::CollectorLeft,Reason::Ambiguous,Reason::SceneEnded})
 for(auto outcome:{Outcome::Consumed,Outcome::DeclinedBeforeApply,Outcome::UnknownAfterApply}){
  Room room;room.ChangeHost(0,Life());auto*row=room.Create(0,room.epoch,Life(),Metadata());const auto id=row->item.id;
  room.Reserve(1,room.epoch,id,Life(),{},false,false);const auto token=row->grant;
  expect(room.Remove(0,room.epoch,id,reason)&&row->awaitingOutcome,"Every terminal removal retains exact outstanding accounting");
  auto*next=room.Create(0,room.epoch,Life(),Metadata(2));const auto nextId=next->item.id;
  expect(nextId!=id&&!room.Reserve(1,room.epoch,nextId,Life(),{},false,false),"Removed held grant blocks another grant until outcome");
  expect(!room.Reserve(2,room.epoch,id,Life(),{},false,false),"Removed pile is never reopened for a different collector");
  expect(!room.Complete(2,room.epoch,id,token,Life(),outcome),"Terminal receipt cannot change collector");
  expect(!room.Complete(1,room.epoch,id,token+1,Life(),outcome),"Terminal receipt cannot change grant token");
  expect(!room.Complete(1,room.epoch+1,id,token,Life(),outcome),"Terminal receipt cannot cross scene epoch");
  expect(!room.Complete(1,room.epoch,id,token,Life(2),outcome),"Terminal receipt cannot change actor birth");
  auto wrong=Life();++wrong.generation;expect(!room.Complete(1,room.epoch,id,token,wrong,outcome),"Terminal receipt cannot cross connection generation");
  wrong=Life();++wrong.model;expect(!room.Complete(1,room.epoch,id,token,wrong,outcome),"Terminal receipt cannot change actor model");
  wrong=Life();++wrong.area;expect(!room.Complete(1,room.epoch,id,token,wrong,outcome),"Terminal receipt cannot change actor area");
  expect(row->awaitingOutcome&&row->stage==Stage::Removed,"Invalid terminal receipts leave the held grant intact");
  expect(room.Complete(1,room.epoch,id,token,Life(),outcome)&&!row->awaitingOutcome,"Exact receipt settles every terminal removal reason");
  expect(row->stage==(outcome==Outcome::Consumed?Stage::Collected:Stage::Removed),"Terminal outcome accounts consumed or removed without reactivation");
  const auto revision=row->item.revision;expect(!room.Complete(1,room.epoch,id,token,Life(),outcome)&&row->item.revision==revision,"Core duplicate terminal receipt never performs a second transition");
  expect(!room.Reserve(1,room.epoch,id,Life(),{},false,false),"Settled old pile cannot be granted again");
  expect(room.Reserve(1,room.epoch,nextId,Life(),{},false,false)&&next->grant>token,"Settled terminal receipt releases collector for a distinct item and token");
 }
 Room room;room.ChangeHost(0,Life());auto*row=room.Create(0,room.epoch,Life(),Metadata());const auto id=row->item.id;
 room.Reserve(1,room.epoch,id,Life(),{},false,false);const auto token=row->grant;room.RetireCollector(1);
 expect(!row->awaitingOutcome&&!room.Complete(1,room.epoch,id,token,Life(),Outcome::Consumed),"Actual collector departure retires accounting rather than accepting a recycled collector receipt");
}
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
 expect(room.Complete(1,room.epoch,id,token,Life(),Outcome::DeclinedBeforeApply)&&row->stage==Stage::Removed,"Pre-apply decline is terminal in conservative ordinary slice");
 expect(!room.Complete(1,room.epoch,id,token,Life(),Outcome::Consumed),"Late old grant result cannot overwrite active row");
 row=room.Create(0,room.epoch,Life(),Metadata(2));id=row->item.id;
 room.Reserve(2,room.epoch,id,Life(),{},false,false);expect(row->grant>token,"A new reservation never reuses grant token");
 expect(room.Complete(2,room.epoch,id,row->grant,Life(),Outcome::UnknownAfterApply)&&row->stage==Stage::Removed,"Possible benefit after native apply retires ambiguity instead of reopening");
 row=room.Create(0,room.epoch,Life(),Metadata(3));expect(row&&row->item.id>id,"Retired storage slot receives a distinct network ID");
 id=row->item.id;room.Reserve(1,room.epoch,id,Life(),{},false,false);room.RetireCollector(1);
 expect(row->stage==Stage::Removed&&row->reason==Reason::CollectorLeft,"Lost collector cannot free ambiguous award for another claim");
 row=room.Create(0,room.epoch,Life(),Metadata(4));room.Reserve(1,room.epoch,row->item.id,Life(),{},false,false);
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
 RegistryCases();TerminalReceiptCases();Packets::Pickups::Hello hello;hello.actor=Life();Roundtrip(hello);
 Room room;room.ChangeHost(0,Life());auto*row=room.Create(0,room.epoch,Life(),Metadata());
 Packets::Pickups::State state;state.epoch=room.epoch;state.host=0;state.recipient=Life();Roundtrip(state);state.reset=false;state.row=*row;Roundtrip(state);
 room.Reserve(1,room.epoch,row->item.id,Life(),{},false,false);state.row=*row;Roundtrip(state);
 for(int mode=0;mode<=int(Packets::Pickups::Operation::Replay);++mode){Packets::Pickups::Action p;p.operation=Packets::Pickups::Operation(mode);p.epoch=1;p.sequence=1;p.actor=Life();p.item=Metadata();p.id=1;p.grant=1;Roundtrip(p);}
 auto cop=Metadata();cop.owner=1;cop.model=346;cop.type=4;cop.ammo=15;cop.cop={3,{9,2,6},1,1};
 expect(cop.ValidMetadata()&&!room.Create(0,room.epoch,Life(),cop),"Cop-origin item cannot enter ambient host creation");
 Room copRoom;copRoom.ChangeHost(0,Life());auto*copRow=copRoom.CreateCopDrop(1,copRoom.epoch,Life(),cop);
 expect(copRow&&copRow->item.owner==1&&copRow->item.ammo==15,"Verified cop allocation retains original producer and native quantity");
 state.epoch=copRoom.epoch;state.recipient=Life();state.row=*copRow;Roundtrip(state);
 Packets::Pickups::Action copCreate;copCreate.operation=Packets::Pickups::Operation::Create;copCreate.epoch=copRoom.epoch;copCreate.sequence=1;copCreate.actor=Life();copCreate.item=cop;Roundtrip(copCreate);
 for(auto mutate:{0,1,2,3,4,5}){auto bad=cop;
  if(mutate==0)bad.cop.producerGeneration=0;if(mutate==1)bad.cop.death.sequence=0;if(mutate==2)bad.cop.ped=255;
  if(mutate==3)bad.ammo=16;if(mutate==4)bad.type=22;if(mutate==5)bad.model=334;
  expect(!bad.ValidMetadata(),"Malformed seal, amount, persistent or melee cop metadata is rejected");
 }
 for(int ordinal=1;ordinal<=MaxDeathOutputs;++ordinal){
  auto item=cop;item.cop.ordinal=uint8_t(ordinal);item.creation=item.cop.sequence=uint32_t(ordinal);
  item.type=ordinal<=MaxDeathWeapons?4:8;item.model=ordinal<=MaxDeathWeapons?346:1212;item.ammo=ordinal<=MaxDeathWeapons?15:9365;
  expect(item.ValidMetadata(),"Every bounded native weapon/money ordinal is accepted by actual metadata validator");
  copCreate.item=item;copCreate.sequence=ordinal;Roundtrip(copCreate);
  std::array<uint32_t,512>words{};serialize::WriteStream write(reinterpret_cast<uint8_t*>(words.data()),sizeof(words));
  static_cast<Packet&>(copCreate).SerializeWrite(write);write.Flush();Packets::Pickups::Action restored;
  serialize::ReadStream stream(reinterpret_cast<uint8_t*>(words.data()),write.GetBytesProcessed());static_cast<Packet&>(restored).SerializeRead(stream);
  expect(restored.item.cop.ordinal==ordinal&&restored.item.ammo==item.ammo&&restored.item.cop.producerGeneration==item.cop.producerGeneration&&SameSeal(restored.item.cop.death,item.cop.death),"Real codec preserves exact ordinal, producer incarnation, seal and captured amount");
 }
 for(int model:{346,347,348,349,350,351,352,353,355,356,357,358}){
  auto item=cop;item.model=model;item.ammo=DeathWeaponLimit(model);expect(item.ValidMetadata(),"Verified supported firearm native limit accepted");
  ++item.ammo;expect(!item.ValidMetadata(),"Above-native firearm bound rejected by actual validator");
 }
 for(int kind=0;kind<7;++kind){auto item=cop;
  if(kind==0)item.cop.ordinal=0;if(kind==1)item.cop.ordinal=21;
  if(kind==2){item.cop.ordinal=14;item.type=8;item.model=1212;item.ammo=9366;}
  if(kind==3){item.cop.ordinal=1;item.type=8;item.model=1212;}
  if(kind==4){item.cop.ordinal=14;item.type=4;}
  if(kind==5)item.position.x=std::numeric_limits<float>::quiet_NaN();
  if(kind==6)item.cop.death.epoch=0;
  expect(!item.ValidMetadata(),"Malformed ordinal/kind/quantity/position/death rejected");
  Packets::Pickups::Action bad=copCreate;bad.item=item;serialize::MeasureStream measure;
  expect(!static_cast<Packet&>(bad).SerializeMeasure(measure),"Actual writer rejects malformed origin metadata");
 }
 hello.position.y=std::numeric_limits<float>::infinity();serialize::MeasureStream measure;
 expect(!static_cast<Packet&>(hello).SerializeMeasure(measure),"Invalid finite envelope fails actual writer before native calls");
 std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;
}
