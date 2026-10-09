#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
class Packet;
struct RegistryStub { void RegisterPacket(Packet* p); };
static RegistryStub& GetPacketFactory() { static RegistryStub r; return r; }
#include "network/packets/trailers.h"
void RegistryStub::RegisterPacket(Packet* p) { delete p; }
static int checks = 0, failures = 0;
static void expect(bool ok,const char* msg) { ++checks; if (!ok) { ++failures; std::cerr << "FAIL: " << msg << '\n'; } }
template<class T> void Roundtrip(T value) {
    alignas(4) std::array<uint8_t,512> bytes{};
    serialize::MeasureStream measure; Packet& input = value;
    expect(input.SerializeMeasure(measure),"Real codec measure");
    serialize::WriteStream write(bytes.data(),int(bytes.size()));
    expect(input.SerializeWrite(write),"Real codec write"); write.Flush();
    expect(write.GetBitsProcessed()==measure.GetBitsProcessed(),"Exact measure/write bit count");
    T output; Packet& decoded = output; serialize::ReadStream read(bytes.data(),write.GetBytesProcessed());
    expect(decoded.SerializeRead(read),"Real codec full read");
    alignas(4) std::array<uint8_t,512> round{}; serialize::WriteStream again(round.data(),int(round.size()));
    expect(decoded.SerializeWrite(again),"Decoded codec write"); again.Flush();
    expect(again.GetBitsProcessed()==write.GetBitsProcessed() && std::memcmp(bytes.data(),round.data(),size_t(write.GetBytesProcessed()))==0,
        "All actual serialized fields round trip unchanged");
    for (int n=0;n<write.GetBytesProcessed();++n) {
        T truncated; serialize::ReadStream stream(bytes.data(),n);
        expect(!static_cast<Packet&>(truncated).SerializeRead(stream),"Every truncated payload rejected");
    }
}
template<class T> bool Measures(T value) { serialize::MeasureStream stream; return static_cast<Packet&>(value).SerializeMeasure(stream); }
int main(){
 for(int cab:{403,514,515})for(int trailer:{435,450,584,591})for(int attached=0;attached<2;++attached){
  Packets::Trailers::Link p;p.link.room=p.link.ownerEpoch=p.link.sequence=TrailerSync::MaxCounter;p.link.revision=TrailerSync::MaxCounter;
  p.link.parent={254,TrailerSync::MaxCounter,cab};p.link.child={0,TrailerSync::MaxCounter,trailer};p.link.attached=bool(attached);Roundtrip(p);
  if(attached){Packets::Trailers::Pose pose;pose.link=p.link;Roundtrip(pose);}
 }
 Packets::Trailers::Hello h;h.scene=1;Roundtrip(h);h.controllingRestart=true;Roundtrip(h);
 Packets::Trailers::Lease lease;lease.room=1;lease.reset=true;lease.scene=1;Roundtrip(lease);
 lease.reset=false;lease.lease={{1,1,435},1,0,true};Roundtrip(lease);lease.lease.live=false;Roundtrip(lease);
 Packets::Trailers::Link bad;bad.link.room=bad.link.ownerEpoch=bad.link.sequence=1;bad.link.parent={0,1,403};bad.link.child={1,2,435};
 bad.link.frame.position.x=std::numeric_limits<float>::quiet_NaN();expect(!Measures(bad),"Actual write rejects NaN");bad.link.frame.position={};
 bad.link.frame.right={0,0,0};expect(!Measures(bad),"Degenerate orientation rejected");bad.link.frame.right={1,0,0};bad.link.parent.generation=0;
 expect(!Measures(bad),"Missing vehicle birth rejected");bad.link.parent.generation=1;bad.link.child.id=0;expect(!Measures(bad),"Self link rejected");
 bad.link.child.id=1;bad.link.parent.model=531;expect(!Measures(bad),"Unsupported trailer model pair rejected");
 std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;}
