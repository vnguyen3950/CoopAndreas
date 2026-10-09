#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
class Packet;
struct RegistryStub { void RegisterPacket(Packet* p); };
static RegistryStub& GetPacketFactory() { static RegistryStub r; return r; }
#include "network/packets/fires.h"
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
int main() {
    using namespace FireSync;
    for (uint32_t slot=1;slot<=MaxFires;++slot) for (int flags=0;flags<4;++flags) for (int kind=0;kind<=4;++kind) {
        Packets::Fires::Update p; p.state.key={MaxCounter,slot,MaxCounter,MaxCounter};
        p.state.position={20000,-20000,10000}; p.state.strength=flags?3.0f:0.0f;
        p.state.remaining=120000; p.state.generations=255; p.state.script=bool(flags&1); p.state.noise=bool(flags&2);
        if (kind) { p.state.target={Kind(kind),kind==4?uint32_t(0x07ffffff):kind==1?7u:254u,MaxCounter,MaxCounter,7,19999}; p.state.creator=p.state.target; }
        Roundtrip(p);
    }
    Packets::Fires::Hello hello; hello.gameGeneration=MaxCounter; hello.nativeReference=MaxCounter; hello.controllingRestart=true; Roundtrip(hello);
    Packets::Fires::Reset reset; reset.epoch=MaxCounter; reset.connection=MaxCounter; reset.gameGeneration=MaxCounter; reset.recipientBirth=MaxCounter; reset.host=7; Roundtrip(reset);
    expect(static_cast<Packet&>(reset).GetChannel()==ePacketChannel::SYSTEM,"Room reset ordered on reliable SYSTEM");
    reset.recipientBirth=0;expect(!Measures(reset),"Missing exact recipient birth rejected before writing");
    reset.recipientBirth=MaxCounter+1;expect(!Measures(reset),"Out-of-range recipient birth rejected");
    Packets::Fires::Bind bind; bind.epoch=1; bind.entity={Kind::Vehicle,254,1,1,0,400}; Roundtrip(bind); bind.live=false; Roundtrip(bind);
    Packets::Fires::Remove remove; remove.key={1,60,1,2}; Roundtrip(remove);
    for (int kind=0;kind<=4;++kind) {
        Packets::Fires::Request p; p.request.epoch=p.request.connection=p.request.sequence=p.request.gameGeneration=1;
        p.request.intent=Intent(kind); p.request.issuer={Kind::Player,0,1,1,0,0}; p.request.fire={1,1,1,1};
        p.request.radius=8; p.request.water=2; Roundtrip(p);
    }
    Packets::Fires::Update bad; bad.state.key={1,1,1,1}; bad.state.strength=std::numeric_limits<float>::quiet_NaN();
    expect(!Measures(bad),"NaN strength rejected before writing"); bad.state.strength=1;
    bad.state.position.x=std::numeric_limits<float>::infinity(); expect(!Measures(bad),"Infinite position rejected");
    bad.state.position={}; bad.state.key.id=61; expect(!Measures(bad),"Out-of-range fire slot rejected");
    bad.state.key.id=1; bad.state.target={Kind::Ped,255,1,1,0,105}; expect(!Measures(bad),"NPC slot 255 rejected");
    bad.state.target.id=0; bad.state.target.generation=0; expect(!Measures(bad),"Zero attached entity generation rejected");
    Packets::Fires::Request q; q.request.epoch=q.request.connection=q.request.sequence=q.request.gameGeneration=1;
    q.request.issuer={Kind::Player,0,1,1,0,0}; q.request.radius=9; expect(!Measures(q),"Malformed water radius rejected");
    q.request.radius=0; q.request.water=-1; expect(!Measures(q),"Negative water strength rejected");
    Packets::Fires::Update valid;valid.state.key={1,1,1,1};
    alignas(4) std::array<uint8_t,512> corrupt{};serialize::WriteStream writer(corrupt.data(),int(corrupt.size()));
    expect(static_cast<Packet&>(valid).SerializeWrite(writer),"Valid mutation payload writes");writer.Flush();
    serialize::MeasureStream prefix;auto key=valid.state.key;expect(Packets::Fires::Key(prefix,key),"Actual key codec measures vector offset");
    const int bitOffset=prefix.GetBitsProcessed();const uint32_t nanBits=0x7fc00000;
    for(int bit=0;bit<32;++bit){const int at=bitOffset+bit;const uint8_t mask=uint8_t(1u<<(at%8));
        if(nanBits&(1u<<bit))corrupt[size_t(at/8)]|=mask;else corrupt[size_t(at/8)]&=uint8_t(~mask);}
    Packets::Fires::Update malformed;serialize::ReadStream reader(corrupt.data(),writer.GetBytesProcessed());
    expect(!static_cast<Packet&>(malformed).SerializeRead(reader),"Wire NaN vector rejected by actual production read codec");
    std::cout<<checks<<" assertions, "<<failures<<" failures\n"; return failures?1:0;
}
