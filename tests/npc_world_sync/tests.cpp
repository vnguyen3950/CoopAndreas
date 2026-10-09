#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include "config.h"
struct Factory { template<class T> void RegisterPacket(T* p) { delete p; } };
Factory& GetPacketFactory() { static Factory f; return f; }
#include "network/packet.h"
#include "network/npc_sync.h"
#include "extracted_math.h"
struct CVector { float x=0,y=0,z=0; CVector()=default; CVector(float a,float b,float c):x(a),y(b),z(c){} };
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#include "extracted_packet.h"
static unsigned checks=0,failures=0;
void expect(bool ok,const char* msg){++checks;if(!ok){++failures;std::cout<<"FAIL: "<<msg<<'\n';}}
struct Wire { std::array<uint32_t,4096> words{}; int bytes=0;uint8_t* data(){return reinterpret_cast<uint8_t*>(words.data());} };
template<class T> Wire encode(T p){Wire w;serialize::WriteStream s(w.data(),sizeof(w.words));expect(static_cast<Packet&>(p).SerializeWrite(s),"Actual packet writes");s.Flush();w.bytes=s.GetBytesProcessed();return w;}
template<class T> bool decode(Wire& w,T& p,int n=-1){serialize::ReadStream s(w.data(),n<0?w.bytes:n);return static_cast<Packet&>(p).SerializeRead(s);}
template<class T> void roundtrip(T p){auto w=encode(p);T q;expect(decode(w,q),"Actual packet reads");expect(q.stamp.generation==p.stamp.generation && q.stamp.epoch==p.stamp.epoch && q.stamp.sequence==p.stamp.sequence,"Full identity survives codec");for(int n=0;n<w.bytes;++n){T shortPacket;expect(!decode(w,shortPacket,n),"Every truncated packet rejected");}}
int main(){
 using namespace Packets::Peds;using NPCSync::Stamp;
 Stamp old{9,2,5},fresh{10,1,1},transferred{9,3,1},next{9,2,6};
 expect(next.Newer(old),"Later same-owner state accepted");expect(!old.Newer(old),"Duplicate state rejected");expect(!fresh.Newer(old),"Reused slot is not same lifetime");expect(!transferred.Newer(old),"Old owner epoch cannot cross ownership");
 expect(NPCSync::Assignable(old,transferred,1,2),"Explicit higher-epoch transfer allowed");expect(!NPCSync::Assignable(old,old,1,2),"Same epoch cannot silently switch owner");
 for(int i=0;i<255;++i){PedOnFoot p;p.pedid=i;p.stamp={uint32_t(i+1),2,5};p.area=18;p.pos={100,200,30};p.velocity={.01f,.02f,.03f};p.healthSnapshot.iHealth=0;p.healthSnapshot.iArmour=50;p.weaponSnapshot.iWeaponType=WEAPON_AK47;p.weaponSnapshot.nAmmo=30;p.bAiming=true;p.weaponAim={10,20,30};roundtrip(p);}
 PedSpawn spawn;spawn.stamp={12,1,0};spawn.ownerid=1;roundtrip(spawn);
 PedConfirm confirm;confirm.tempid=2;confirm.stamp={12,1,0};confirm.ownerid=1;confirm.requestToken=45;roundtrip(confirm);
 AssignPedSyncer assign;assign.pedid=2;assign.stamp={12,2,100};assign.ownerid=3;roundtrip(assign);
 PedRemove remove;remove.stamp={12,2,0};roundtrip(remove);
 PedPassengerSync passenger;passenger.stamp={12,2,1};passenger.seatid=3;roundtrip(passenger);
 PedDriverUpdate driver;driver.stamp={12,2,1};driver.engineState=true;driver.dirtLevel=15;roundtrip(driver);
 PedReplay replay;replay.onFoot.stamp={12,2,1};auto wire=encode(replay);PedReplay copy;expect(decode(wire,copy)&&copy.onFoot.stamp.generation==12,"Reliable replay embeds actual state codec");expect(GetChannelReliability(static_cast<Packet&>(replay).GetChannel())==ePacketReliability::RELIABLE,"Replay uses reliable lifecycle domain");
 for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),20001.f}){PedOnFoot p;p.stamp={1,1,1};p.pos.x=bad;Wire w;serialize::WriteStream s(w.data(),sizeof(w.words));expect(!static_cast<Packet&>(p).SerializeWrite(s),"Nonfinite/extreme coordinates rejected before native/compression");}
 CNetworkEntitySerializer ped;ped.entityType=NETWORK_ENTITY_TYPE_PED;ped.entityId=254;ped.entityGeneration=32;Wire e;serialize::WriteStream es(e.data(),sizeof(e.words));expect(ped.Serialize(es),"Generation-aware PED operand writes");es.Flush();e.bytes=es.GetBytesProcessed();CNetworkEntitySerializer read;serialize::ReadStream ers(e.data(),e.bytes);expect(read.Serialize(ers)&&read.entityGeneration==32,"PED generation roundtrip");
 for(int type=NETWORK_ENTITY_TYPE_PLAYER;type<=NETWORK_ENTITY_TYPE_NOTINPOOLS;++type){
  if(type==NETWORK_ENTITY_TYPE_PED)continue;
  CNetworkEntitySerializer a;a.entityType=eNetworkEntityType(type);a.entityId=1;a.entityGeneration=123;
  Wire w;serialize::WriteStream s(w.data(),sizeof(w.words));expect(a.Serialize(s)&&a.entityGeneration==0,"Legacy non-NPC operand removes stale generation without adding a field");
  int bits=s.GetBitsProcessed();s.Flush();w.bytes=s.GetBytesProcessed();CNetworkEntitySerializer b;serialize::ReadStream rs(w.data(),w.bytes);expect(b.Serialize(rs)&&b.entityGeneration==0,"Every non-NPC operand reads zero generation");
  Wire plain;serialize::WriteStream baseline(plain.data(),sizeof(plain.words));b.Serialize(baseline);expect(baseline.GetBitsProcessed()==bits,"Non-NPC zero-generation bit length stays unchanged");
 }
 for(uint32_t invalid:{0u,NPCSync::MaxCounter+1}){CNetworkEntitySerializer bad;bad.entityType=NETWORK_ENTITY_TYPE_PED;bad.entityGeneration=invalid;Wire w;serialize::WriteStream s(w.data(),sizeof(w.words));expect(!bad.Serialize(s),"Invalid NPC generation fails writer before integer assertions");}
 // Forge the unused integer encodings in actual wire data, rather than asking
 // the writer to produce out-of-range primitives.
 for(int field=0;field<2;++field){auto bad=e;int start=field==0?3:11;int width=field==0?8:31;for(int bit=0;bit<width;++bit)bad.data()[(start+bit)/8]|=uint8_t(1u<<((start+bit)%8));CNetworkEntitySerializer value;serialize::ReadStream s(bad.data(),bad.bytes);expect(!value.Serialize(s),"Malformed NPC slot/generation encodings rejected on read");}
 PedPin pendingPin;pendingPin.requestToken=42;pendingPin.pinned=true;roundtrip(pendingPin);
 for(uint32_t seed=1;seed<=5000;++seed){Wire bad;uint32_t value=seed;for(auto&word:bad.words){value=value*1664525u+1013904223u;word=value;}bad.bytes=1+int(seed%128);PedOnFoot p;bool accepted=decode(bad,p);expect(!accepted||p.Valid(),"Malformed actual NPC envelope never yields invalid replay state");}
 std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;
}
