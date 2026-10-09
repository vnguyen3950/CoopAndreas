#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
struct RegistrationStub{template<class T>void RegisterPacket(T* p){delete p;}};
static RegistrationStub& GetPacketFactory(){static RegistrationStub f;return f;}
#include "network/packets/gang_wars.h"

using namespace GangWarSync;
static unsigned checks=0,failures=0;
static void expect(bool x,const char* why){++checks;if(!x){++failures;std::cout<<"FAIL: "<<why<<'\n';}}
static World native_world()
{
    World w;w.layout=123;w.count=380;
    for(int i=0;i<w.count;++i){for(int gang=0;gang<GANGS;++gang)w.zones[i].density[gang]=uint8_t((i+gang)%256);
        w.zones[i].color={uint8_t(i),200,100,120};w.zones[i].radar=uint8_t(i%4);}
    return w;
}
static War active()
{War w;w.enabled=true;w.offense=1;w.zone=20;w.info=10;w.gang1=0;w.gang2=2;w.x=100;w.y=-100;w.z=10;return w;}
static void authority_tests()
{
    Room room;expect(room.SetHost(0,1),"Initial registered host binds authority.");auto world=native_world();
    expect(!room.PublishWorld(1,room.state.epoch,0,1,false,world),"Guest cannot seed or alter host territory.");
    expect(room.PublishWorld(0,room.state.epoch,0,1,false,world)&&room.state.campaign==1,"First host seed creates campaign.");
    auto before=room.state;auto malformed=world;malformed.count=381;
    expect(!room.PublishWorld(0,room.state.epoch,1,2,false,malformed)&&room.state.revision==before.revision,"Malformed world does not consume sequence or revision.");
    expect(!room.PublishWorld(0,room.state.epoch,1,1,false,world),"Duplicate world publication is ignored.");
    expect(room.PublishWar(0,room.state.epoch,1,1,active())&&room.state.warGeneration==1,"Native active transition starts one war generation.");
    auto wave=active();wave.offense=2;
    expect(room.PublishWar(0,room.state.epoch,1,2,wave)&&room.state.warGeneration==1,"Wave progression does not fabricate a new war.");
    auto epoch=room.state.epoch;auto campaign=room.state.campaign;
    expect(room.SetHost(1,2)&&room.state.epoch>epoch&&room.state.campaign==campaign&&room.world==world&&!room.state.war.Active(),
        "Host migration cancels active fight and retains complete campaign territory.");
    expect(!room.PublishWar(0,epoch,campaign,3,wave),"Delayed old-host publication cannot revive its war.");
    expect(!room.PublishWorld(1,epoch,campaign,2,true,world),"Old authority epoch cannot reset migrated campaign.");
    expect(room.PublishWar(1,room.state.epoch,campaign,1,active())&&room.state.warGeneration==2,"New host later starts a distinct monotonic war.");
    auto changed=world;changed.zones[10].density[1]=55;
    expect(room.PublishWorld(1,room.state.epoch,campaign,1,false,changed),"New host publishes inherited-campaign native changes.");
    auto different=changed;different.layout=456;
    expect(!room.PublishWorld(1,room.state.epoch,campaign,2,false,different),"Unannounced layout replacement cannot alias zone indices.");
    expect(room.PublishWorld(1,room.state.epoch,campaign,2,true,different)&&room.state.campaign==campaign+1&&!room.state.war.Active(),
        "Current host deliberate load resets campaign without fake victory.");
    auto revision=room.state.revision;room.state.revision=MAX_COUNTER;auto saved=room.world;
    expect(!room.SetHost(2,3)&&!room.PublishWorld(1,room.state.epoch,room.state.campaign,3,true,world)&&room.world==saved,
        "Revision exhaustion fails before host or world mutation.");room.state.revision=revision;
    epoch=room.state.epoch;room.EmptyRoom();expect(room.SetHost(2,3)&&room.state.epoch>epoch&&!room.state.ready,"Empty room does not reuse authority epoch.");
    expect(!room.SetHost(8,3)&&!room.SetHost(1,0),"Host identities are bounded and require a connection incarnation.");
}
static void client_tests()
{
    Room room;room.SetHost(0,1);room.PublishWorld(0,room.state.epoch,0,1,false,native_world());
    Client client;expect(client.Accept(room.state)&&client.AcceptWorld(room.state.epoch,room.state.campaign,room.state.revision,room.world)&&client.HasWorld(),
        "Join caches host campaign and territory before native initialization.");
    expect(!client.Accept(room.state),"Duplicate state cannot overwrite a later snapshot.");
    auto old=room.state;room.SetHost(1,2);expect(client.Accept(room.state)&&client.HasWorld(),"Promoted menu guest retains inherited territory cache.");
    expect(!client.Accept(old)&&!client.AcceptWorld(old.epoch,old.campaign,old.revision,native_world()),"Stale epoch state and territory are rejected.");
    room.PublishWorld(1,room.state.epoch,1,1,true,native_world());
    expect(client.Accept(room.state)&&!client.HasWorld(),"New campaign requires matching new world before native adoption.");
    expect(client.AcceptWorld(room.state.epoch,room.state.campaign,room.state.revision,room.world)&&client.HasWorld(),"Matching new campaign atomically becomes available.");
    auto bad=room.state;bad.war=active();bad.war.x=std::numeric_limits<float>::quiet_NaN();
    expect(!client.Accept(bad),"Nonfinite gang-war coordinate rejected.");
    for(float value:{std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}){
        auto w=active();w.x=value;expect(!w.Valid(),"Infinite native location is invalid.");}
    auto w=active();w.offense=7;expect(!w.Valid(),"Invalid native offensive stage is rejected.");
    w=active();w.defense=3;expect(!w.Valid(),"Invalid native defensive stage is rejected.");
    w=active();w.info=380;expect(!w.Valid(),"Zone-info index is bounded.");
    w=active();w.fightRemaining=MAX_DURATION+1;expect(!w.Valid(),"Remote timer cannot overflow native presentation window.");
    w=active();w.specificCount=7;expect(!w.Valid(),"Trigger list cannot exceed native six-entry array.");
    w=active();w.specificCount=1;w.specificZones[0]=380;expect(!w.Valid(),"Trigger zone is bounded.");
    w=active();w.training=true;expect(!w.Valid(),"Training requires a real zone-info index.");
    w=active();w.nextAttack=std::numeric_limits<float>::infinity();expect(!w.Valid(),"Next-attack timer must be finite.");
    w=active();w.difficulty=1.01f;expect(!w.Valid(),"Difficulty cannot corrupt native wave size envelope.");
    w=active();w.territoryPercent=-1;expect(!w.Valid(),"Owner territory fraction cannot be negative.");
    auto badWorld=native_world();badWorld.zones[0].radar=4;expect(!badWorld.Valid(),"Radar mode cannot overwrite unrelated flag bits.");
}
struct Wire{std::array<uint32_t,2560> words{};int bytes=0;uint8_t* data(){return reinterpret_cast<uint8_t*>(words.data());}};
template<class T>static Wire encode(T p)
{
    Wire wire;serialize::MeasureStream size;Packet& packet=p;
    expect(packet.SerializeMeasure(size),"Actual gang packet measures.");
    serialize::WriteStream writer(wire.data(),int(wire.words.size()*4));expect(packet.SerializeWrite(writer),"Actual gang packet writes.");
    writer.Flush();wire.bytes=writer.GetBytesProcessed();expect(wire.bytes<=size.GetBytesProcessed(),"Measured buffer covers payload.");return wire;
}
template<class T>static bool decode(Wire& wire,T& p,int bytes=-1)
{serialize::ReadStream read(wire.data(),bytes<0?wire.bytes:bytes);return static_cast<Packet&>(p).SerializeRead(read);}
static void patch(Wire& wire,int offset,int bits,uint32_t value)
{
    for(int i=0;i<bits;++i){auto& b=wire.data()[(offset+i)/8];uint8_t mask=uint8_t(1u<<((offset+i)%8));
        b=uint8_t((b&uint8_t(~mask))|((value&(1u<<i))?mask:0));}
}
template<class T>static T roundtrip(T p)
{
    auto wire=encode(p);T result;expect(decode(wire,result),"Complete actual packet reads.");
    expect(static_cast<Packet&>(result).GetBytesRead()==size_t(wire.bytes),"Actual wrapper records consumed bytes.");
    for(int bytes=0;bytes<wire.bytes;++bytes){T truncated;expect(!decode(wire,truncated,bytes),"Every shorter byte payload is rejected.");}
    expect(static_cast<Packet&>(p).GetChannel()==ePacketChannel::SYSTEM&&GetChannelReliability(ePacketChannel::SYSTEM)==ePacketReliability::RELIABLE,
        "Gang lifetime/state packets use actual reliable SYSTEM assignment.");return result;
}
static void packet_tests()
{
    Packets::Gangs::State hello;roundtrip(hello);Packets::Gangs::Territory request;roundtrip(request);
    Room room;room.SetHost(0,1);room.PublishWorld(0,room.state.epoch,0,1,false,native_world());auto detailed=active();
    detailed.specificCount=6;detailed.specificZones={0,1,2,377,378,379};detailed.training=true;detailed.trainingInfo=379;
    detailed.ferocity=5;detailed.nextAttack=float(MAX_DURATION);detailed.difficulty=1;detailed.territoryPercent=.25f;detailed.closeby=true;
    room.PublishWar(0,room.state.epoch,1,1,detailed);
    Packets::Gangs::State state;state.kind=Kind::Snapshot;state.authority=room.state;
    auto copy=roundtrip(state);expect(copy.authority.war==state.authority.war&&copy.authority.warGeneration==state.authority.warGeneration,"War native state and generation round trip.");
    auto encodedState=encode(state);
    for(float invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}){
        auto malformed=encodedState;uint32_t bits=0;std::memcpy(&bits,&invalid,4);patch(malformed,208,32,bits);
        Packets::Gangs::State rejected;expect(!decode(malformed,rejected),"Raw nonfinite coordinate is rejected by actual reader.");}
    for(int offset:{373,405,437}){
        auto malformed=encodedState;patch(malformed,offset,32,0x7FC00000);
        Packets::Gangs::State rejected;expect(!decode(malformed,rejected),"Raw nonfinite timer/difficulty/territory fraction rejected by actual reader.");}
    auto malformed=encodedState;patch(malformed,131,3,7);Packets::Gangs::State rejected;
    expect(!decode(malformed,rejected),"Unused offensive-stage bit code is rejected by actual reader.");
    malformed=encodedState;patch(malformed,2,31,MAX_COUNTER);
    expect(!decode(malformed,rejected),"Out-of-envelope authority epoch is rejected by actual reader.");
    state.kind=Kind::Publish;state.sequence=MAX_COUNTER;
    copy=roundtrip(state);expect(copy.sequence==MAX_COUNTER&&copy.authority.war==state.authority.war,"Maximum host publication sequence round trips.");
    Packets::Gangs::Territory full;full.kind=Kind::Snapshot;full.epoch=room.state.epoch;full.campaign=room.state.campaign;full.revision=room.state.revision;full.world=room.world;
    auto world=roundtrip(full);expect(world.world==full.world,"All 380 zone densities, colors and radar modes round trip exactly.");
    auto wire=encode(full);expect(wire.bytes<10*1024,"Full territory fits actual packet factory capacity.");
    auto malformedWorld=wire;patch(malformedWorld,126,9,511);Packets::Gangs::Territory rejectedWorld;
    expect(!decode(malformedWorld,rejectedWorld),"Malformed zone count cannot overrun world array.");
    malformedWorld=wire;patch(malformedWorld,0,2,3);
    expect(!decode(malformedWorld,rejectedWorld),"Unused packet-kind representation is rejected.");
    full.kind=Kind::Publish;full.revision=0;full.sequence=MAX_COUNTER;full.reset=true;
    world=roundtrip(full);expect(world.reset&&world.sequence==MAX_COUNTER&&world.world==full.world,"Deliberate reset publication keeps full native territory.");
    state.authority.war.x=std::numeric_limits<float>::quiet_NaN();serialize::MeasureStream measure;
    expect(!static_cast<Packet&>(state).SerializeMeasure(measure),"Invalid outbound float fails actual codec before publication.");
    std::cout<<"Full territory payload: "<<wire.bytes<<" bytes\n";
}
int main(){authority_tests();client_tests();packet_tests();std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;}
