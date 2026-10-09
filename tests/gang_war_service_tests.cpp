#include "gang_war_doubles.h"
#include "gang_native.inc"
static GangWarsNative::Zone zones[2]{};
static CZoneInfo infos[2]{};
namespace GangWarsNative{inline View Live(){return {zones,infos,2,2};}inline void ClearNativeBlip(){++CRadar::cleared;}}
#include "gang_client.inc"
#include "gang_server.inc"
static unsigned checks=0,failures=0;
static void expect(bool value,const char* message){++checks;if(!value){++failures;std::cout<<"FAIL: "<<message<<'\n';}}
static Peer peer;
static CPed local;
static Pool pool;
static void Ready()
{
    local.m_nPedType=0;pool.entries={&local};pool.m_nSize=1;CPools::ms_pPedPool=&pool;
    CWorld::Players[0].m_pPed=&local;gGameState=9;Events::processScriptsEvent.after.Fire();
}
static GangWarSync::World World()
{GangWarSync::World value;expect(GangWarsNative::CaptureWorld(GangWarsNative::Live(),value),"Capture actual native view");return value;}
static void Replay(const GangWarSync::Room& room)
{
    Packets::Gangs::State state;state.kind=GangWarSync::Kind::Snapshot;state.authority=room.state;CGangWarSync::Receive(state);
    if(room.state.ready){Packets::Gangs::Territory world;world.kind=GangWarSync::Kind::Snapshot;world.epoch=room.state.epoch;
        world.campaign=room.state.campaign;world.revision=room.state.revision;world.world=room.world;CGangWarSync::Receive(world);}
}
static void ClientCase()
{
    CGangWarSync::Init();CNetwork::m_pPeer=&peer;CNetwork::m_bAuthenticated=true;CLocalPlayer::m_bIsHost=true;
    GangWarSync::Room room;room.SetHost(0,1);Replay(room);
    CGangWarSync::NativeUpdate();expect(CGangWars::updates==0&&worlds.empty(),"Menu authentication cannot seed or advance native war");
    zones[0].info=0;zones[1].info=1;infos[0].m_nGangDensity[0]=42;
    Ready();CGangWars::bGangWarsActive=true;
    CGangWarSync::NativeUpdate();expect(worlds.size()==1&&CGangWars::updates==0,"Initialized first host freezes native Update until seed acknowledgement");
    CGangWarSync::Process();expect(worlds.size()==1,"Outstanding seed is not republished before echo");
    const auto seed=worlds.back();expect(room.PublishWorld(0,seed.epoch,seed.campaign,seed.sequence,seed.reset,seed.world),"Server accepts actual service seed");
    Replay(room);CGangWarSync::NativeUpdate();expect(CGangWars::updates==1&&CGangWars::bGangWarsActive,"Own seed preserves native enabled flag before progression");
    const int updates=CGangWars::updates;CWorld::PlayerInFocus=2;CGangWarSync::NativeUpdate();
    expect(CGangWars::updates==updates,"Remote focus cannot advance or sample host native state");CWorld::PlayerInFocus=0;
    CPed wave;wave.reference=257;pool.entries.push_back(&wave);pool.m_nSize=2;CNetworkPedManager::tracked.push_back(&wave);
    CGangWars::onUpdate=[&]{wave.m_nPedFlags.bPartOfAttackWave=true;};CGangWarSync::NativeUpdate();
    expect(CNetworkPedManager::operations.size()==1&&CNetworkPedManager::operations.back().second,"Post-Update scanner pins real newly flagged wave actor");
    CGangWarSync::NativeUpdate();expect(CNetworkPedManager::operations.size()==1,"One pin operation per actor lifetime, no per-frame flood");
    CGangWars::onUpdate={};wave.m_nPedFlags.bPartOfAttackWave=false;CGangWarSync::NativeUpdate();
    expect(CNetworkPedManager::operations.size()==2&&!CNetworkPedManager::operations.back().second,"Native death/release clears pin without synthetic kill count");
    wave.m_nPedFlags.bPartOfAttackWave=true;CGangWarSync::NativeUpdate();const auto operations=CNetworkPedManager::operations.size();
    wave.reference=258;pool.entries.pop_back();pool.m_nSize=1;CGangWarSync::NativeUpdate();
    expect(CNetworkPedManager::operations.size()==operations,"Stale pool reference is forgotten without dereferencing recycled actor");
    CPed delayed;delayed.reference=513;delayed.m_matrix=nullptr;delayed.m_nPedFlags.bPartOfAttackWave=true;
    pool.entries.push_back(&delayed);pool.m_nSize=2;CGangWarSync::NativeUpdate();
    expect(!CNetworkPedManager::IsPedTracked(&delayed),"Unregistered wave with no matrix waits for safe registration");
    delayed.m_matrix=&delayed;CGangWarSync::NativeUpdate();
    expect(CNetworkPedManager::IsPedTracked(&delayed)&&CNetworkPedManager::operations.back().first==&delayed,
        "Ready original wave actor is registered and pinned without replacement");
    // Migration while loading must adopt the host campaign, not this save.
    CLocalPlayer::m_bIsHost=false;room.SetHost(1,2);Replay(room);Events::initScriptsEvent.before.Fire();gGameState=0;
    const size_t before=worlds.size();infos[0].m_nGangDensity[0]=99;
    CNetworkPlayerManager::m_nMyId=1;CLocalPlayer::m_bIsHost=true;CGangWarSync::NativeUpdate();
    expect(worlds.size()==before,"Promoted loading guest cannot reseed room from its local save");
    Ready();CGangWars::onUpdate=[&]{expect(uint8_t(infos[0].m_nGangDensity[0])==42,"Inherited density is installed before promoted host native Update");};
    CGangWarSync::NativeUpdate();expect(worlds.size()==before,"First promoted initialization adopts existing campaign without reset publication");
    CGangWars::onUpdate={};Events::initScriptsEvent.before.Fire();infos[0].m_nGangDensity[0]=55;Ready();
    CGangWarSync::NativeUpdate();expect(worlds.size()==before+1&&worlds.back().reset,"Established current host later load deliberately reseeds new campaign");
    const auto reset=worlds.back();expect(room.PublishWorld(1,reset.epoch,reset.campaign,reset.sequence,true,reset.world),"Reset has exact current epoch and campaign");
    Replay(room);CGangWarSync::NativeUpdate();expect(uint8_t(infos[0].m_nGangDensity[0])==55,"Reset echo adopts newly loaded host world");
    // Guests receive state before spawn; never invoke wave AI or rewards.
    CLocalPlayer::m_bIsHost=false;CNetworkPlayerManager::m_nMyId=0;room.SetHost(1,2);
    GangWarSync::War war;war.enabled=true;war.offense=2;war.zone=0;war.info=0;war.gang1=1;war.x=10;
    room.PublishWar(1,room.state.epoch,room.state.campaign,1,war);Replay(room);
    const int old=CGangWars::updates;CWorld::PlayerInFocus=2;CGangWars::State=NOT_IN_WAR;
    CGangWarSync::NativeUpdate();expect(CGangWars::State==NOT_IN_WAR&&CGangWars::updates==old,"Guest mirror is not applied while remote player focus is swapped");
    CWorld::PlayerInFocus=0;CGangWarSync::NativeUpdate();
    expect(CGangWars::updates==old&&int(CGangWars::State)==2&&CRadar::created==1,"Guest mirror and guidance do not call native progression");
    room.SetHost(0,3);CLocalPlayer::m_bIsHost=true;Replay(room);CGangWarSync::NativeUpdate();
    expect(int(CGangWars::State)==0&&CRadar::cleared>=1,"Migration cancels mirrored fight and clears guest marker without awarding victory");
    CNetwork::m_bAuthenticated=false;CGangWarSync::Process();peer.connectID=2;CNetwork::m_bAuthenticated=true;
    const auto count=worlds.size();CGangWarSync::NativeUpdate();expect(worlds.size()==count,"New connection cannot use cached old-session campaign");
    CLocalPlayer::m_bIsHost=false;CNetworkPlayerManager::m_nMyId=0;room.SetHost(1,4);
    room.PublishWar(1,room.state.epoch,room.state.campaign,1,war);Replay(room);CGangWarSync::Process();
    expect(CGangWars::bGangWarsActive&&int(CGangWars::State)==2,"New connection adopts only replayed current campaign");
    CNetwork::m_bAuthenticated=false;CGangWarSync::Process();
    expect(!CGangWars::bGangWarsActive&&int(CGangWars::State)==0,"Disconnect clears mirrored war before offline Update can run waves");
    CNetwork::m_bAuthenticated=false;CGangWarSync::NativeUpdate();expect(CGangWars::updates>old,"Offline native behavior remains callable");
}
static void NativeCase()
{
    zones[0].info=0;zones[1].info=1;uint8_t flag=0x9F,races=0xAB;
    std::memcpy(&infos[0].m_nFlags,&flag,1);infos[0].dealer=13;std::memcpy(&infos[0].races,&races,1);
    auto world=World();world.zones[0].density[9]=255;world.zones[0].color={1,2,3,4};world.zones[0].radar=2;
    expect(GangWarsNative::ApplyWorld(GangWarsNative::Live(),world),"Verified zone view applies valid world");
    expect(uint8_t(infos[0].m_nFlags)==0xDF&&infos[0].dealer==13&&uint8_t(infos[0].races)==0xAB,
        "Territory apply preserves population, no-cops, dealer and race fields");
    expect(uint8_t(infos[0].m_nGangDensity[9])==255&&infos[0].m_ZoneColor.a==4,"All ten unsigned densities and actual RGBA are applied");
    auto bad=world;bad.layout^=1;expect(!GangWarsNative::ApplyWorld(GangWarsNative::Live(),bad),"Different zone layout cannot corrupt native geometry");
    CGangWars::bGangWarsActive=true;CGangWars::State=eGangWarState(256);GangWarSync::War war;
    expect(!GangWarsNative::CaptureWar(GangWarsNative::Live(),10,war),"Native invalid enum rejected before narrowing");
    CGangWars::State=PREFIRST_WAVE;CGangWars::pZoneToFightOver=reinterpret_cast<CZone*>(reinterpret_cast<char*>(zones)+1);
    CGangWars::pZoneInfoToFightOver=infos;CGangWars::Gang1=1;
    expect(!GangWarsNative::CaptureWar(GangWarsNative::Live(),10,war),"Misaligned native fight pointer cannot alias a valid zone");
    CGangWars::State=NOT_IN_WAR;CGangWars::NumSpecificZones=1;CGangWars::aSpecificZones[0]=1;
    CGangWars::ZoneInfoForTraining=1;CGangWars::bTrainingMission=true;CGangWars::TimeTillNextAttack=-10;
    CGangWars::Difficulty=.4f;CGangWars::TerritoryUnderControlPercentage=.3f;
    expect(GangWarsNative::CaptureWar(GangWarsNative::Live(),10,war)&&war.specificCount==1&&war.specificZones[0]==1
        &&war.trainingInfo==1&&war.nextAttack==0,"Native restrictions captured and legitimate negative countdown clamped before send");
    war.specificZones[0]=2;expect(!GangWarsNative::ApplyWar(GangWarsNative::Live(),war,10),"Native adoption rejects restriction outside actual navigation count");
    war.specificZones[0]=0;war.nextAttack=1234;
    expect(GangWarsNative::ApplyWar(GangWarsNative::Live(),war,10)&&CGangWars::aSpecificZones[0]==0&&CGangWars::TimeTillNextAttack==1234,
        "Migration mirror replaces unrelated native trigger list and countdown");
}
static void ServerCase()
{
    Peer a,b;CNetworkPlayer host{&a,0,true},guest{&b,1,false};CNetworkPlayer forged{&a,0,true};
    CNetworkPlayerManager::m_pPlayers={&host,&guest};zones[0].info=0;zones[1].info=1;
    CGangWarServer::Join(&host);const auto epoch=CGangWarServer::Room().state.epoch;
    Packets::Gangs::Territory publish;publish.kind=GangWarSync::Kind::Publish;publish.epoch=epoch;publish.sequence=1;publish.world=World();
    expect(!CGangWarServer::Receive(publish,&guest)&&!CGangWarServer::Receive(publish,&forged),"Actual service rejects guest authority and forged wrapper despite valid packet");
    expect(CGangWarServer::Receive(publish,&host),"Actual authenticated host service seeds world");
    const auto count=worlds.size();CGangWarServer::Join(&guest);
    expect(worlds.size()==count+1&&targets.back()==&guest&&worlds.back().kind==GangWarSync::Kind::Snapshot,"Late join receives retained real territory on SYSTEM");
    expect(!CGangWarServer::Receive(publish,&host),"Duplicate host sequence does not apply another transaction");
    CGangWarServer::Leave(&host);host.m_bIsHost=false;guest.m_bIsHost=true;CNetworkPlayerManager::m_pPlayers={&guest};
    CGangWarServer::HostChanged(&guest);expect(CGangWarServer::Room().state.epoch>epoch&&CGangWarServer::Room().state.ready,
        "Actual host departure and reassignment retain campaign under new authority epoch");
    CGangWarServer::Leave(&guest);expect(!CGangWarServer::Room().state.ready,"Last peer ends room state without reusing lifetime epoch");
}
static void ShutdownCase()
{
    CGangWarSync::Init(); CNetwork::m_pPeer=&peer; CNetwork::m_bAuthenticated=true; CLocalPlayer::m_bIsHost=true;
    zones[0].info=0; zones[1].info=1;
    GangWarSync::Room room; room.SetHost(0,1); Replay(room); Ready();
    CGangWars::bGangWarsActive=true; CGangWarSync::NativeUpdate();
    const auto seed=worlds.back(); room.PublishWorld(0,seed.epoch,seed.campaign,seed.sequence,seed.reset,seed.world);
    Replay(room); CGangWarSync::NativeUpdate();
    expect(nativeAuthority&&initializedScripts,"Actual seeded owner is ready before shutdown callback");
    const auto old=CGangWars::cancels;
    gameShutdownEvent.before.Fire();
    expect(CGangWars::cancels==old+1,"Shutdown callback releases owner wave before disarming script readiness");
    expect(!initializedScripts&&!nativeAuthority,"Shutdown disarms native progression after cleanup");
}
int main(int argc,char** argv)
{
    if(argc!=2)return 2;std::string mode=argv[1];
    if(mode=="client")ClientCase();else if(mode=="native")NativeCase();else if(mode=="server")ServerCase();else if(mode=="shutdown")ShutdownCase();else return 2;
    std::cout<<checks<<" assertions, "<<failures<<" failures ("<<mode<<")\n";return failures?1:0;
}
