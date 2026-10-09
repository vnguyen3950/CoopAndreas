#include "doubles.h"
#include "vitals.inc"
#include "client.inc"
#include "server.inc"
static unsigned checks=0,failures=0;
void expect(bool b,const char* text) { ++checks; if(!b){++failures;std::cout<<"FAIL: "<<text<<'\n';} }
Peer connectionPeer,remotePeer;
RpClump recordedLocalClump,remoteClump;
CPlayerPed localPed,remotePed;
CNetworkPlayer remotePlayer;
void setup(bool identity=true)
{
    CPlayerAnimationSync::Reset();
    recordedLocalClump.list.clear();remoteClump.list.clear();clumps={&recordedLocalClump,&remoteClump};
    localPed={};remotePed={};localPed.m_pRwClump=&recordedLocalClump;remotePed.m_pRwClump=&remoteClump;
    nativePool.live={&localPed,&remotePed};CPools::ms_pPedPool=&nativePool;
    CPools::references={{10,&localPed},{100,&remotePed}};
    CWorld::Players={};CWorld::Players[0].m_pPed=&localPed;CWorld::Players[3].m_pPed=&remotePed;
    remotePlayer={};remotePlayer.m_iPlayerId=1;remotePlayer.m_pPeer=&remotePeer;remotePlayer.m_pPed=&remotePed;
    if(identity)remotePlayer.m_vitals.Bind(1,5);
    CNetworkPlayerManager::m_pPlayers={&remotePlayer};CNetworkPlayerManager::m_nMyId=0;
    CNetwork::m_pPeer=&connectionPeer;CNetwork::m_bAuthenticated=true;CWorld::PlayerInFocus=0;
    gGameState=9;tick=g_serverTime=1000;block.bLoaded=1;adds=requests=respawns=0;failRespawn=false;
    CTheScripts::ScriptSpace[1]=0;GetPacketFactory().sent.clear();GetPacketFactory().recipients.clear();
    Events::initScriptsEvent.before.Fire();Events::processScriptsEvent.after.Fire();
}
Packets::Players::PlayerAnimationState frame(uint32_t birth=1,uint32_t sequence=1,int pose=1,bool loop=false)
{
    Packets::Players::PlayerAnimationState p;p.playerid=1;p.life={5,birth,sequence,0,0,100};
    if(pose)p.state={pose,.2f,3,1,1,loop,sequence};
    p.sampledAt=p.serverTime=1000;return p;
}
void receive(Packets::Players::PlayerAnimationState p) { CPlayerAnimationSync::Receive(p); CPlayerAnimationSync::Process(); }
int main()
{
    CPlayerAnimationSync::Init();
    setup(false);auto p=frame();receive(p);expect(adds==0,"EVENT before SYSTEM does not apply");
    tick=g_serverTime=181000;remotePlayer.m_vitals.Bind(1,5);CPlayerAnimationSync::Process();
    expect(adds==0,"Minutes in menu expire finite one-shot");
    setup(false);p=frame(1,1,1,true);receive(p);tick=g_serverTime=181000;
    remotePlayer.m_vitals.Bind(1,5);CPlayerAnimationSync::Process();expect(adds==1,"Dormant loop binds after exact SYSTEM identity");
    setup();receive(frame());expect(adds==1,"SYSTEM before EVENT applies");
    auto* own=remoteClump.list.back();CAnimBlendAssociation foreign;foreign.m_pHierarchy=&hierarchy;remoteClump.list.push_back(&foreign);
    receive(frame(1,2,0));expect(own->m_fBlendDelta==-8&&foreign.m_fBlendDelta==0,"Stop fades only service-owned association");
    receive(frame(1,1));expect(adds==1,"Old pose after stop cannot restart");
    setup();block.bLoaded=0;receive(frame(1,1,1,true));expect(adds==0&&requests==1&&requestedFlags==0,"Missing block retries without permanent flags");
    block.bLoaded=1;CPlayerAnimationSync::Process();expect(adds==1,"Model-ready loop retry");
    setup();receive(frame(1,1,1,true));remoteClump.list.clear();CPlayerAnimationSync::Process();expect(adds==1,"Interrupted association is not recreated");
    p=frame(1,2,1,true);p.state.instance=1;receive(p);expect(adds==1,"Sample of interrupted same source lifetime stays stopped");
    p.state.instance=2;p.life.sequence=3;receive(p);expect(adds==2,"New source association can start");
    setup();receive(frame(1,1,1,true));CTheScripts::ScriptSpace[1]=1;CPlayerAnimationSync::Process();
    expect(remoteClump.list.back()->m_fBlendDelta==-8,"Mission begins: own visual fades");
    CTheScripts::ScriptSpace[1]=0;CPlayerAnimationSync::Process();expect(adds==1,"Prior mission-interrupted pose cannot resume");
    setup();receive(frame(1,1,1,true));own=remoteClump.list.back();CPools::references.erase(100);CPools::references[101]=&remotePed;
    remotePlayer.m_nPedRef=101;CPlayerAnimationSync::Process();expect(adds==1&&own->m_fBlendDelta==0,"Recycled pool ref cannot fade an unrelated association");
    setup(false);Packets::Players::RespawnPlayer reset;reset.playerid.value=1;reset.life={5,2,2,0,0,100,false};
    CPlayerAnimationSync::ReceiveRespawn(reset);receive(frame(2,3,1,true));expect(respawns==0,"Respawn and animation await identity");
    remotePlayer.m_vitals.Bind(1,5);CPlayerAnimationSync::Process();expect(respawns==1&&adds==1,"Queued reset precedes newer visual after SYSTEM");
    reset.life.birth=1;reset.life.sequence=1;CPlayerAnimationSync::ReceiveRespawn(reset);CPlayerAnimationSync::Process();expect(respawns==1,"Stale reset cannot destroy new lifetime");
    setup();reset.life={5,2,2,0,0,100,false};failRespawn=true;CPlayerAnimationSync::ReceiveRespawn(reset);CPlayerAnimationSync::Process();
    expect(respawns==1&&adds==0,"Native pool failure retains boundary");failRespawn=false;CPlayerAnimationSync::Process();expect(respawns==2,"Deferred native recreation retries");
    setup();CNetwork::m_bAuthenticated=false;receive(frame(1,1,1,true));expect(adds==0,"Pre-handshake queue dormant");
    CNetwork::m_bAuthenticated=true;CPlayerAnimationSync::Process();expect(adds==1,"Handshake cannot discard same-peer EVENT");
    setup();CPlayerAnimationSync::Process();auto* sent=dynamic_cast<Packets::Players::PlayerAnimationState*>(GetPacketFactory().sent.back().get());
    expect(sent&&sent->life.generation==0&&sent->life.birth==1,"Initial owner frame carries no chosen generation");
    PlayerAnimation::Life life;expect(!CPlayerAnimationSync::GetLocalLife(life),"Unacknowledged local birth rejects owner operation");
    auto ack=*sent;ack.life.generation=9;CPlayerAnimationSync::Receive(ack);expect(CPlayerAnimationSync::GetLocalLife(life)&&life.birth==1&&life.generation==9,"Exact echo exposes local life");
    reset={};expect(CPlayerAnimationSync::PrepareRespawn(reset)&&reset.life.birth==2,"Respawn advances birth before operation");
    expect(!CPlayerAnimationSync::GetLocalLife(life),"Old acknowledgement rejects new life claim");
    Events::initScriptsEvent.before.Fire();Events::processScriptsEvent.after.Fire();CPlayerAnimationSync::Process();
    sent=dynamic_cast<Packets::Players::PlayerAnimationState*>(GetPacketFactory().sent.back().get());expect(sent&&sent->life.birth==3,"Same-ref New Game advances birth");
    setup();CNetwork::m_bAuthenticated=false;CPlayerAnimationSync::Process();expect(GetPacketFactory().sent.empty(),"No local publication unauthenticated");
    setup();CWorld::PlayerInFocus=3;CPlayerAnimationSync::Process();expect(GetPacketFactory().sent.empty(),"Remote focus cannot publish local life");
    setup();p=frame();p.life.generation=0;
    expect(CPlayerAnimationServer::Receive(p,&remotePlayer),"Registered owner can publish");
    expect(remotePlayer.m_actorLife.life.generation==5,"Server stamps actual connection");
    expect(!CPlayerAnimationServer::Receive(p,&remotePlayer),"Duplicate sequence rejected");
    p.life.sequence=2;p.playerid=0;expect(!CPlayerAnimationServer::Receive(p,&remotePlayer),"Spoofed owner rejected");
    p.playerid=1;p.life.generation=5;expect(!CPlayerAnimationServer::Receive(p,&remotePlayer),"Chosen client generation rejected");
    p.life.generation=0;p.life.birth=2;expect(CPlayerAnimationServer::Receive(p,&remotePlayer),"New owner birth accepted");
    p.life.birth=1;p.life.sequence=3;expect(!CPlayerAnimationServer::Receive(p,&remotePlayer),"Prior birth cannot overwrite current");
    expect(CPlayerAnimationServer::GetActorLife(&remotePlayer,life)&&life.birth==2,"Authenticated read-only server seam");
    CNetworkPlayer impostor=remotePlayer;expect(!CPlayerAnimationServer::GetActorLife(&impostor,life),"Old wrapper cannot expose actor life");
    tick=g_serverTime=2000;CPlayerAnimationServer::Replay(&remotePlayer,&impostor);
    sent=dynamic_cast<Packets::Players::PlayerAnimationState*>(GetPacketFactory().sent.back().get());
    expect(sent&&sent->serverTime==2000&&sent->sampledAt==1000&&sent->life.sequence==2,"Fresh replay timestamp preserves original phase/sequence");
    tick=g_serverTime=3000;CPlayerAnimationServer::Replay(&remotePlayer,&impostor);
    sent=dynamic_cast<Packets::Players::PlayerAnimationState*>(GetPacketFactory().sent.back().get());expect(sent&&sent->serverTime==3000,"Repeated replay never reuses cached outer timestamp");
    setup();receive(frame(1,1,1,true));own=remoteClump.list.back();
    own->m_pCallbackFunc(own,own->m_pCallbackData);remoteClump.list.clear();CPlayerAnimationSync::Process();
    expect(adds==1,"Native delete callback disarms exact owned association");
    setup();receive(frame(1,1,1,true));own=remoteClump.list.back();own->m_pCallbackFunc=nullptr;
    receive(frame(1,2,0));expect(own->m_fBlendDelta==0,"Recycled or task-claimed callback identity never authorizes fade");
    setup();block.bLoaded=0;p=frame();receive(p);tick=g_serverTime=6000;block.bLoaded=1;
    CPlayerAnimationSync::Process();expect(adds==0,"One-shot expires during block loading without restart");
    setup();remotePed.m_nModelIndex=1;receive(frame(1,1,1,true));expect(adds==0,"Future model waits");
    remotePed.m_nModelIndex=0;CPlayerAnimationSync::Process();expect(adds==1,"Model match enables retained exact state");
    setup();remotePed.m_nAreaCode=1;receive(frame(1,1,1,true));expect(adds==0,"Future area waits");
    remotePed.m_nAreaCode=0;CPlayerAnimationSync::Process();expect(adds==1,"Area match enables retained exact state");
    setup();CWorld::Players[3].m_pPed=&localPed;receive(frame(1,1,1,true));expect(adds==0,"Wrong PlayerInfo native binding rejects");
    setup();nativePool.live.erase(&remotePed);receive(frame(1,1,1,true));expect(adds==0,"Deleted native pool actor rejects");
    setup();remotePed.m_nPhysicalFlags.bSubmergedInWater=true;receive(frame(1,1,1,true));expect(adds==0,"Swimming receives no visual task overwrite");
    setup();remotePed.m_ePedState=PEDSTATE_ATTACK;receive(frame(1,1,1,true));expect(adds==0,"Combat receives no visual task overwrite");
    setup();receive(frame(1,1,1,true));own=remoteClump.list.back();Events::initScriptsEvent.before.Fire();
    expect(own->m_fBlendDelta==-8,"New Game fades current owned visual");Events::processScriptsEvent.after.Fire();CPlayerAnimationSync::Process();
    expect(adds==1,"Same-ref script reinit cannot restore cached prior pose");
    p=frame(1,2,1,true);p.state.instance=1;receive(p);expect(adds==1,"Same source association remains retired after local script reset");
    p.life.birth=2;p.life.sequence=3;p.state.instance=2;receive(p);expect(adds==2,"New owner lifetime permits fresh pose");
    setup();receive(frame(1,1,1,true));remotePlayer.m_vitals={};remotePlayer.m_vitals.Bind(1,6);
    p=frame(1,1,1,true);p.life.generation=6;receive(p);expect(adds==2,"Reused player slot has a fresh independent pose lifetime");
    receive(frame(10,100,1,true));expect(adds==2,"Prior connection cannot supersede reused slot");
    setup();receive(frame(2,3,1,true));reset.life={5,2,2,0,0,100,false};reset.playerid.value=1;
    CPlayerAnimationSync::ReceiveRespawn(reset);CPlayerAnimationSync::Process();expect(respawns==1,"Same-birth reset received after state still recreates once");
    CPlayerAnimationSync::ReceiveRespawn(reset);CPlayerAnimationSync::Process();expect(respawns==1,"Repeated reset cannot recreate again");
    setup();CNetworkPlayerManager::m_pPlayers.clear();receive(frame(1,1,1,true));expect(adds==0,"Missing SYSTEM roster retains bounded future state");
    CNetworkPlayerManager::m_pPlayers={&remotePlayer};CPlayerAnimationSync::Process();expect(adds==1,"Roster arriving later resolves retained state");
    setup();CPlayerAnimationSync::Process();sent=dynamic_cast<Packets::Players::PlayerAnimationState*>(GetPacketFactory().sent.back().get());
    ack=*sent;ack.life.generation=9;CPlayerAnimationSync::Receive(ack);expect(CPlayerAnimationSync::GetLocalLife(life),"Local life acknowledgement ready");
    reset.life={5,2,2,0,0,100,false};CPlayerAnimationSync::ReceiveRespawn(reset);
    CPlayerAnimationSync::GetLocalLife(life);expect(respawns==0,"Collector getter cannot recreate another native actor");
    Events::initScriptsEvent.before.Fire();auto* boundary=dynamic_cast<Packets::Players::RespawnPlayer*>(GetPacketFactory().sent.back().get());
    expect(boundary&&boundary->life.birth==2&&!boundary->life.ready,"Scene boundary publishes dormant new birth immediately");
    expect(!CPlayerAnimationSync::GetLocalLife(life),"Menu or load cannot expose initialized actor for pickup");
    setup();CPlayerAnimationSync::Process();auto count=GetPacketFactory().sent.size();localPed.m_nAreaCode=1;
    CPlayerAnimationSync::Process();expect(GetPacketFactory().sent.size()==count+1,"Area transition publishes immediately");
    setup();CPlayerAnimationSync::Process();localPed.m_nModelIndex=7;CPlayerAnimationSync::Process();
    sent=dynamic_cast<Packets::Players::PlayerAnimationState*>(GetPacketFactory().sent.back().get());expect(sent&&sent->life.birth==2&&sent->life.model==7,"Native model change advances shared birth");
    setup();CAnimBlendAssociation source;source.m_pHierarchy=&hierarchy;recordedLocalClump.list.push_back(&source);CPlayerAnimationSync::Process();
    sent=dynamic_cast<Packets::Players::PlayerAnimationState*>(GetPacketFactory().sent.back().get());expect(sent&&sent->state.pose==1&&sent->state.instance==1,"Owner samples actual source association");
    expect(source.m_pCallbackFunc==nullptr&&source.m_fBlendDelta==0,"Owner sampling leaves source task callbacks and blend unchanged");
    source.m_fBlendDelta=-4;CPlayerAnimationSync::Process();sent=dynamic_cast<Packets::Players::PlayerAnimationState*>(GetPacketFactory().sent.back().get());expect(sent&&sent->state.pose==0,"Owner interrupted native association publishes stop");
    setup();p=frame();p.life.generation=0;CNetworkPlayerManager::m_pPlayers.clear();expect(!CPlayerAnimationServer::Receive(p,&remotePlayer),"Unregistered peer cannot publish actor state");
    setup();reset.life={0,2,1,0,0,100,false};reset.playerid.value=1;expect(CPlayerAnimationServer::Respawn(reset,&remotePlayer),"Authenticated dormant boundary accepted");
    expect(!CPlayerAnimationServer::GetActorLife(&remotePlayer,life),"Dormant server life rejects collector claim");
    p=frame(2,2,0);p.life.generation=0;expect(CPlayerAnimationServer::Receive(p,&remotePlayer)&&CPlayerAnimationServer::GetActorLife(&remotePlayer,life),"Ready same-birth owner publication completes boundary");
    expect(!CPlayerAnimationServer::Respawn(reset,&remotePlayer),"Prior boundary cannot erase ready newer sequence");
    setup();receive(frame(1,1,1,true));own=remoteClump.list.back();
    p=frame(1,1,1,true);p.life.generation=6;CPlayerAnimationSync::Receive(p);
    CPlayerAnimationSync::ForgetPlayer(1,5);expect(own->m_fBlendDelta==-8,"Departure fades only departing generation");
    remotePlayer.m_vitals={};remotePlayer.m_vitals.Bind(1,6);CPlayerAnimationSync::Process();
    expect(adds==2,"Future occupant survives old disconnect tombstone");
    CPlayerAnimationSync::ForgetPlayer(1,6);receive(p);expect(adds==2,"Departed incarnation cannot be rebound by delayed EVENT");
    setup();CPlayerAnimationSync::Process();RpClump replacement;clumps.push_back(&replacement);localPed.m_pRwClump=&replacement;
    CPlayerAnimationSync::Process();sent=dynamic_cast<Packets::Players::PlayerAnimationState*>(GetPacketFactory().sent.back().get());
    expect(sent&&sent->life.birth==2,"Same-reference clump recreation advances owner birth");
    setup(false);Events::initScriptsEvent.before.Fire();gGameState=0;receive(frame(1,1,1,true));
    tick=g_serverTime=181000;remotePlayer.m_vitals.Bind(1,5);CPlayerAnimationSync::Process();
    expect(adds==0,"Authenticated menu replay cannot create native association");
    Events::initScriptsEvent.before.Fire();gGameState=9;Events::processScriptsEvent.after.Fire();CPlayerAnimationSync::Process();
    expect(adds==1,"Initial New Game preserves queued menu replay until gameplay readiness");
    setup();Events::initScriptsEvent.before.Fire();gGameState=0;reset.life={5,2,2,0,0,100,false};
    CPlayerAnimationSync::ReceiveRespawn(reset);receive(frame(2,3,1,true));tick=g_serverTime=181000;
    Events::initScriptsEvent.before.Fire();gGameState=9;Events::processScriptsEvent.after.Fire();CPlayerAnimationSync::Process();
    expect(respawns==1&&adds==1,"Menu reset and newer state survive initial script init in EVENT order");
    std::cout<<checks<<" actual service assertions, "<<failures<<" failures\n";return failures?1:0;
}
