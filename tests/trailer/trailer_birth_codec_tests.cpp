#include "client_doubles.h"
template<class T> void Roundtrip(T value){alignas(4) std::array<uint8_t,128> bytes{},again{};serialize::MeasureStream measure;
 expect(static_cast<Packet&>(value).SerializeMeasure(measure),"Actual birth codec measures");serialize::WriteStream write(bytes.data(),int(bytes.size()));
 expect(static_cast<Packet&>(value).SerializeWrite(write),"Actual birth codec writes");write.Flush();expect(write.GetBitsProcessed()==measure.GetBitsProcessed(),"Measured birth bits match real serialized bits");
 T decoded;serialize::ReadStream read(bytes.data(),write.GetBytesProcessed());expect(static_cast<Packet&>(decoded).SerializeRead(read),"Actual birth codec reads");
 serialize::WriteStream second(again.data(),int(again.size()));expect(static_cast<Packet&>(decoded).SerializeWrite(second),"Actual decoded birth writes");second.Flush();
 expect(second.GetBitsProcessed()==write.GetBitsProcessed()&&std::memcmp(bytes.data(),again.data(),size_t(write.GetBytesProcessed()))==0,"All birth and authority fields round trip exactly");
 for(int n=0;n<write.GetBytesProcessed();++n){T shortPacket;serialize::ReadStream s(bytes.data(),n);expect(!static_cast<Packet&>(shortPacket).SerializeRead(s),"Every truncated birth payload rejected");}}
template<class T> bool Valid(T p){serialize::MeasureStream m;return static_cast<Packet&>(p).SerializeMeasure(m);}
int main(){for(int id:{0,254})for(uint32_t birth:{1u,TrailerSync::MaxCounter}){
 Packets::Vehicles::VehicleSpawn spawn;spawn.vehicleid=id;spawn.generation=birth;spawn.requestToken=TrailerSync::MaxCounter;Roundtrip(spawn);spawn.generation=0;spawn.requestToken=1;Roundtrip(spawn);
 Packets::Vehicles::VehicleConfirm confirm;confirm.vehicleid=id;confirm.generation=birth;confirm.requestToken=TrailerSync::MaxCounter;Roundtrip(confirm);
 Packets::Vehicles::VehicleRemove remove;remove.vehicleid=id;remove.generation=birth;Roundtrip(remove);
 for(int owner=-1;owner<8;++owner){Packets::Vehicles::AssignVehicleSyncer assign;assign.vehicleid=id;assign.generation=birth;assign.syncerId=owner;Roundtrip(assign);}}
 Packets::Vehicles::VehicleSpawn bad;bad.generation=TrailerSync::MaxCounter+1;expect(!Valid(bad),"Oversized birth rejected before serializer assertion");bad.generation=1;bad.vehicleid=255;expect(!Valid(bad),"Out-of-range vehicle slot rejected");
 Packets::Vehicles::VehicleConfirm missing;missing.generation=1;expect(!Valid(missing),"Missing confirmation nonce rejected");missing.requestToken=1;missing.generation=0;expect(!Valid(missing),"Zero confirmed birth rejected");
 Packets::Vehicles::AssignVehicleSyncer owner;owner.generation=1;owner.syncerId=8;expect(!Valid(owner),"Out-of-range syncer rejected");
 std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;}
