#include "stdafx.h"
#include "CGangWarSync.h"
#include "GangWarsNative.h"

namespace
{
GangWarSync::Client cache;
bool connected=false, initializedScripts=false, authorityReady=false, nativeAuthority=false, mirrored=false;
bool resetIntent=false, seedPending=false, sentWorld=false, sentWar=false;
uint32_t connection=0, authorityEpoch=0, worldSequence=0, warSequence=0, lastSend=0;
uint32_t seedEpoch=0, seedCampaign=0;
GangWarSync::War seedWar, previousWar;
GangWarSync::World previousWorld;
int guidanceBlip=-1;
uint32_t guidanceEpoch=0, guidanceWar=0;
uint8_t guidanceOffense=0, guidanceDefense=0;
struct Pin { CPed* ped; int reference; };
std::vector<Pin> pins;

bool LivePed(CPed* ped,int reference)
{
    return ped&&reference>=0&&CPools::ms_pPedPool&&CPools::GetPed(reference)==ped;
}
void ClearGuidance()
{
    if(guidanceBlip!=-1)CRadar::ClearBlip(guidanceBlip);
    guidanceBlip=-1;guidanceEpoch=guidanceWar=0;guidanceOffense=guidanceDefense=0;
}
void UnpinAll()
{
    for(const auto& pin:pins)if(LivePed(pin.ped,pin.reference))
        CNetworkPedManager::PinGangWarPedToHost(pin.ped,false);
    pins.clear();
}
void CancelNative()
{
    // Only an engine owned war is released. Guest mirrors never run victory,
    // defeat or influence functions; native actor ownership stays with NPC sync.
    if(nativeAuthority&&initializedScripts&&CWorld::PlayerInFocus==0&&gGameState==9&&CPools::ms_pPedPool)
    {
        // EndGangWar(false) can increase enemy density for a notified defense.
        // Authority cancellation is neither a native defeat nor a victory.
        CGangWars::ReleasePedsInAttackWave(true,true);
        CGangWars::ReleaseCarsInAttackWave();
        GangWarSync::War idle;
        GangWarsNative::CaptureWar(GangWarsNative::Live(),CTimer::m_snTimeInMilliseconds,idle);
        idle.offense=idle.defense=0;idle.zone=idle.info=idle.gang1=idle.gang2=-1;
        idle.fightRemaining=idle.stageElapsed=0;idle.closeby=false;
        GangWarsNative::ApplyWar(GangWarsNative::Live(),idle,CTimer::m_snTimeInMilliseconds);
        GangWarsNative::ClearNativeBlip();
        UnpinAll();
    }
    pins.clear();
    if(mirrored&&initializedScripts&&CWorld::PlayerInFocus==0&&gGameState==9)
        GangWarsNative::ApplyWar(GangWarsNative::Live(),{},CTimer::m_snTimeInMilliseconds);
    mirrored=false;
    nativeAuthority=false;authorityReady=false;ClearGuidance();
}
bool EnsureConnection()
{
    const uint32_t current=CNetwork::m_pPeer?CNetwork::m_pPeer->connectID:0;
    if(!CNetwork::m_bAuthenticated)
    {
        if(connected)CancelNative();
        connected=false;cache={};seedPending=resetIntent=false;sentWorld=sentWar=false;
        worldSequence=warSequence=authorityEpoch=0;return false;
    }
    if(!connected||connection!=current)
    {
        CancelNative();cache={};connected=true;connection=current;
        worldSequence=warSequence=authorityEpoch=0;seedPending=resetIntent=false;sentWorld=sentWar=false;
        Packets::Gangs::State request;GetPacketFactory().Send(request);
    }
    return true;
}
bool Owner()
{
    return CLocalPlayer::m_bIsHost&&cache.state.epoch&&cache.state.host==CNetworkPlayerManager::m_nMyId;
}
bool NativeReady()
{
    if(CWorld::PlayerInFocus!=0||gGameState!=9||!initializedScripts||!CPools::ms_pPedPool)return false;
    auto* ped=FindPlayerPed(0);
    return ped&&CPools::ms_pPedPool->IsObjectValid(ped)&&CWorld::Players[0].m_pPed==ped&&ped->m_pPlayerData;
}
void ScanPins()
{
    if(!Owner()||!NativeReady())return;
    for(auto it=pins.begin();it!=pins.end();)
    {
        if(!LivePed(it->ped,it->reference)){it=pins.erase(it);continue;}
        if(!it->ped->m_nPedFlags.bPartOfAttackWave)
        {
            CNetworkPedManager::PinGangWarPedToHost(it->ped,false);it=pins.erase(it);continue;
        }
        ++it;
    }
    auto* pool=CPools::ms_pPedPool;
    for(int i=0;i<pool->m_nSize;++i)
    {
        auto* ped=pool->GetAt(i);
        if(!ped||!ped->m_nPedFlags.bPartOfAttackWave||ped->m_nPedType<=1)continue;
        const int reference=CPools::GetPedRef(ped);
        if(!LivePed(ped,reference))continue;
        if(std::any_of(pins.begin(),pins.end(),[&](const Pin& p){return p.reference==reference&&p.ped==ped;}))continue;
        // Native Add happens before assignment of bPartOfAttackWave. Retry the
        // manager's pending-confirmation pin; never replace the original actor.
        if(!CNetworkPedManager::IsPedTracked(ped))
        {
            if(!ped->m_matrix)continue;
            CNetworkPed::CreateHosted(ped);
        }
        if(!CNetworkPedManager::PinGangWarPedToHost(ped,true))continue;
        pins.push_back({ped,reference});
    }
}
bool PublishWorld(const GangWarSync::World& world,bool reset)
{
    if(worldSequence==GangWarSync::MAX_COUNTER)return false;
    Packets::Gangs::Territory packet;packet.kind=GangWarSync::Kind::Publish;
    packet.epoch=cache.state.epoch;packet.campaign=cache.state.campaign;packet.sequence=++worldSequence;
    packet.reset=reset;packet.world=world;GetPacketFactory().Send(packet);
    previousWorld=world;sentWorld=true;return true;
}
void PublishWar(const GangWarSync::War& war)
{
    if(warSequence==GangWarSync::MAX_COUNTER)return;
    Packets::Gangs::State packet;packet.kind=GangWarSync::Kind::Publish;
    packet.authority.epoch=cache.state.epoch;packet.authority.campaign=cache.state.campaign;
    packet.authority.war=war;packet.sequence=++warSequence;GetPacketFactory().Send(packet);
    previousWar=war;sentWar=true;
}
void Guidance()
{
    const auto& state=cache.state;
    if(Owner()||!state.war.Active()){ClearGuidance();return;}
    const bool moved=state.epoch!=guidanceEpoch||state.warGeneration!=guidanceWar;
    if(moved)
    {
        ClearGuidance();guidanceEpoch=state.epoch;guidanceWar=state.warGeneration;
        guidanceBlip=CRadar::SetCoordBlip(BLIP_COORD,CVector(state.war.x,state.war.y,state.war.z),0,
            BLIP_DISPLAY_BLIP_ONLY,nullptr);
    }
    if(moved||guidanceOffense!=state.war.offense||guidanceDefense!=state.war.defense)
    {
        guidanceOffense=state.war.offense;guidanceDefense=state.war.defense;
        CChat::AddMessage("{cecedb}[Gang war] Support the host against the marked gang wave. Native host progression owns the result.");
    }
}
}
void CGangWarSync::Init()
{
    // Verified 1.0 US CGame::Process CALL, leaving the native entry callable.
    patch::RedirectCall(0x53C122,CGangWarSync::NativeUpdate);
    Events::initScriptsEvent.before+=[]
    {
        // Menu authentication or a promoted guest's first initialization does
        // not give its unrelated save permission to reseed the room.
        resetIntent=connected&&Owner()&&authorityReady;
        CancelNative();
        initializedScripts=false;authorityReady=false;nativeAuthority=false;
        seedPending=false;sentWorld=sentWar=false;pins.clear();ClearGuidance();
    };
    Events::processScriptsEvent.after+=[]{if(gGameState==9)initializedScripts=true;};
    gameShutdownEvent.before+=[]{initializedScripts=false;CancelNative();};
}
void CGangWarSync::Receive(const Packets::Gangs::State& packet)
{
    if(!EnsureConnection()||packet.kind!=GangWarSync::Kind::Snapshot||!packet.Valid())return;
    const uint32_t oldEpoch=cache.state.epoch;
    if(!cache.Accept(packet.authority))return;
    if(oldEpoch!=cache.state.epoch)
    {
        CancelNative();worldSequence=warSequence=0;sentWorld=sentWar=false;
        seedPending=resetIntent=false;authorityEpoch=cache.state.epoch;
    }
}
void CGangWarSync::Receive(const Packets::Gangs::Territory& packet)
{
    if(!EnsureConnection()||packet.kind!=GangWarSync::Kind::Snapshot||!packet.Valid())return;
    cache.AcceptWorld(packet.epoch,packet.campaign,packet.revision,packet.world);
}
void CGangWarSync::Process()
{
    if(!EnsureConnection()||!NativeReady()||!cache.state.epoch)return;
    const auto view=GangWarsNative::Live();if(!view.Valid())return;
    if(Owner())
    {
        if(seedPending&&(seedEpoch!=cache.state.epoch||cache.state.campaign>seedCampaign))
            seedPending=false;
        if(!authorityReady)
        {
            if((!cache.state.ready||resetIntent)&&!seedPending)
            {
                GangWarSync::World world;GangWarSync::War war;
                if(!GangWarsNative::CaptureWorld(view,world)||!GangWarsNative::CaptureWar(view,CTimer::m_snTimeInMilliseconds,war))return;
                if(!PublishWorld(world,resetIntent))return;
                seedWar=war;seedEpoch=cache.state.epoch;seedCampaign=cache.state.campaign+1;
                seedPending=true;resetIntent=false;return;
            }
            if(!cache.HasWorld())return;
            if(seedPending&&cache.state.campaign!=seedCampaign)return;
            if(!GangWarsNative::ApplyWorld(view,cache.world))return;
            const auto& war=seedPending?seedWar:cache.state.war;
            if(!GangWarsNative::ApplyWar(view,war,CTimer::m_snTimeInMilliseconds))return;
            seedPending=false;authorityReady=true;nativeAuthority=true;
            previousWorld=cache.world;sentWorld=true;sentWar=false;ClearGuidance();
        }
        ScanPins();
        const uint32_t now=GetTickCount();
        if(now-lastSend<250)return;
        GangWarSync::World world;GangWarSync::War war;
        if(!GangWarsNative::CaptureWorld(view,world)||!GangWarsNative::CaptureWar(view,CTimer::m_snTimeInMilliseconds,war))return;
        if(!sentWorld||!(world==previousWorld))PublishWorld(world,false);
        if(!sentWar||!(war==previousWar))PublishWar(war);
        lastSend=now;
    }
    else
    {
        if(authorityReady||nativeAuthority)CancelNative();
        if(!cache.HasWorld()||!GangWarsNative::ApplyWorld(view,cache.world))return;
        if(!GangWarsNative::ApplyWar(view,cache.state.war,CTimer::m_snTimeInMilliseconds))return;
        mirrored=true;
        Guidance();
    }
}
void CGangWarSync::NativeUpdate()
{
    if(!CNetwork::m_bAuthenticated){CGangWars::Update();return;}
    // Adoption is done before entering the engine even if the usual plugin
    // Process event follows this native call on the first initialized frame.
    Process();
    if(!NativeReady()||!Owner()||!authorityReady||authorityEpoch!=cache.state.epoch)return;
    ScanPins();CGangWars::Update();ScanPins();
}
