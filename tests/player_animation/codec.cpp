#include "doubles.h"
#include <limits>
#include <cstring>
#include <fstream>
static unsigned checks=0,failures=0;
void expect(bool b,const char* text){++checks;if(!b){++failures;std::cout<<"FAIL: "<<text<<'\n';}}
struct Wire { std::array<uint32_t,512> data{}; int bytes=0; };
template<class T> bool encode(T p,Wire& w)
{ serialize::WriteStream stream(reinterpret_cast<uint8_t*>(w.data.data()),sizeof(w.data));if(!static_cast<Packet&>(p).SerializeWrite(stream))return false;stream.Flush();w.bytes=stream.GetBytesProcessed();return true; }
template<class T> bool decode(Wire& w,T& p,int n)
{ serialize::ReadStream stream(reinterpret_cast<uint8_t*>(w.data.data()),n);return static_cast<Packet&>(p).SerializeRead(stream); }
// An untrusted sender is not required to use our validating encoder.
bool malformedWire(Packets::Players::PlayerAnimationState p,Wire& w)
{
    using Stream=serialize::WriteStream;
    Stream stream(reinterpret_cast<uint8_t*>(w.data.data()),sizeof(w.data));
    serialize_int(stream,p.playerid,0,7);
    if(!Packets::Players::SerializeActorLife(stream,p.life))return false;
    serialize_int(stream,p.state.pose,0,5);serialize_uint32(stream,p.sampledAt);
    serialize_int(stream,p.state.instance,1,PlayerAnimation::MAX_COUNTER);
    serialize_float(stream,p.state.phase);serialize_float(stream,p.state.duration);
    serialize_float(stream,p.state.speed);serialize_float(stream,p.state.blend);
    serialize_bool(stream,p.state.loop);stream.Flush();w.bytes=stream.GetBytesProcessed();return true;
}
int main(int argc,char**)

{
    using namespace Packets::Players;
    if(argc>1)
    {
        Wire w;
#ifdef COOP_CLIENT
        std::ifstream in("respawn-server.bin",std::ios::binary);
#else
        std::ifstream in("respawn-client.bin",std::ios::binary);
#endif
        in.read(reinterpret_cast<char*>(w.data.data()),sizeof(w.data));w.bytes=int(in.gcount());
        RespawnPlayer q;expect(w.bytes>0&&decode(w,q,w.bytes)&&q.life.generation==9&&q.life.birth==4&&q.life.sequence==99,"Opposite-role respawn reads exact life");
        for(int n=0;n<w.bytes;n++){RespawnPlayer shortPacket;expect(!decode(w,shortPacket,n),"Every opposite-role respawn truncation rejected");}
#ifdef COOP_CLIENT
        RespawnPlayer reply;reply.life={9,4,99,0,0,0,false};Wire output;expect(encode(reply,output),"Client respawn writes");
        std::ofstream out("respawn-client.bin",std::ios::binary);out.write(reinterpret_cast<char*>(output.data.data()),output.bytes);
#endif
        std::cout<<checks<<" opposite-role assertions, "<<failures<<" failures\n";return failures?1:0;
    }
    for(int id=0;id<8;id++)for(int pose=0;pose<6;pose++)for(int loop=0;loop<2;loop++)
    {
        PlayerAnimationState p;p.playerid=id;p.life={PlayerAnimation::MAX_COUNTER,3,42,299,255,12345};
        if(pose)p.state={pose,1,3,1,1,bool(loop),4};p.sampledAt=0xfffffff0;
        Wire w;expect(encode(p,w),"Actual packet encodes");PlayerAnimationState q;
        expect(decode(w,q,w.bytes),"Actual packet roundtrip");
        expect(q.life.generation==p.life.generation&&q.life.birth==3&&q.life.sequence==42&&q.life.model==299
            &&q.life.area==255&&q.life.nativeReference==12345&&q.state.instance==p.state.instance
            &&q.state.pose==pose&&q.sampledAt==p.sampledAt,"Every identity/pose operand survives actual codec");
        expect(static_cast<Packet&>(p).GetChannel()==ePacketChannel::EVENT
            &&GetChannelReliability(ePacketChannel::EVENT)==ePacketReliability::RELIABLE,"Ordered reliable lifecycle channel");
        for(int n=0;n<w.bytes;n++){PlayerAnimationState shortPacket;expect(!decode(w,shortPacket,n),"Every shorter payload rejected");}
    }
    PlayerAnimationState p;p.life={0,1,1,0,0,10};p.state={1,1,3,1,1,false,1};
    const float malformed[]={std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()};
    for(float value:malformed)for(int member=0;member<4;member++)
    {
        auto q=p;float* fields[]={&q.state.phase,&q.state.duration,&q.state.speed,&q.state.blend};*fields[member]=value;Wire w;
        expect(!encode(q,w),"Nonfinite native operands rejected before write");
        expect(malformedWire(q,w),"Construct nonfinite hostile frame");PlayerAnimationState output;
        expect(!decode(w,output,w.bytes),"Actual decoder rejects nonfinite native operands");
    }
    for(int member=0;member<4;member++)
    {
        auto q=p;float* fields[]={&q.state.phase,&q.state.duration,&q.state.speed,&q.state.blend};
        *fields[member]=member==0?-1.f:member==1?61.f:member==2?3.01f:1.01f;Wire w;
        expect(malformedWire(q,w),"Construct hostile finite-range frame");PlayerAnimationState output;
        expect(!decode(w,output,w.bytes),"Actual decoder rejects finite out-of-range native operands");
    }
    for(int member=0;member<6;member++)
    { auto q=p;switch(member){case 0:q.life.birth=0;break;case 1:q.life.sequence=0;break;case 2:q.life.model=300;break;case 3:q.life.area=256;break;case 4:q.state.pose=6;break;case 5:q.state.instance=0;break;}Wire w;expect(!encode(q,w),"Malformed bounded operand rejected"); }
    RespawnPlayer reset;reset.playerid.value=7;reset.life={9,4,99,0,0,0,false};Wire w;expect(encode(reset,w),"Actual respawn payload encodes");
    std::ofstream out("respawn-server.bin",std::ios::binary);out.write(reinterpret_cast<char*>(w.data.data()),w.bytes);out.close();
    std::cout<<checks<<" actual codec assertions, "<<failures<<" failures\n";return failures?1:0;
}
