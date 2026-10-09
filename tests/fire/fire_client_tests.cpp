#include "client_doubles.h"
#include "client/src/CFireSync.h"
#include "extracted_client.inc"
using Damage = void (__cdecl*)(CPed*,void*);
static void __cdecl NativeDamage(CPed* ped,void*) { ++originalDamage; ped->health-=0.5f; }
static struct { void* original=reinterpret_cast<void*>(&NativeDamage); } damage;
#include "extracted_damage.inc"
void CFireSync::NativeInit() { EnableNative(); }
void CFireSync::OriginalProcess(CFire*) { ++originalProcess; }
void CFireSync::OriginalExtinguish(CFire* f) {
    f->m_nFlags.bActive=false; f->m_pFxSystem=nullptr;
    if (f->m_pEntityTarget && f->m_pEntityTarget->m_nType==ENTITY_TYPE_PED) static_cast<CPed*>(f->m_pEntityTarget)->m_pFire=nullptr;
    if (f->m_pEntityTarget && f->m_pEntityTarget->m_nType==ENTITY_TYPE_VEHICLE) static_cast<CVehicle*>(f->m_pEntityTarget)->m_pFire=nullptr;
    f->m_pEntityTarget=nullptr;
}
void CFire::Extinguish() { if(CFireSync::NativeExtinguish(this)) CFireSync::OriginalExtinguish(this); }
static void Setup(CPlayerPed& local,int id=1,int host=0) {
    CFireSync::Reset(); for(auto& f:gFireManager.m_aFires)f.Initialise();
    CPools::ped.refs.clear(); CPools::car.refs.clear(); CPools::object.refs.clear();
    CNetworkPedManager::m_pPeds.clear(); CNetworkVehicleManager::m_pVehicles.clear(); CNetworkPlayerManager::players.clear();
    localPed=&local; CPools::ped.refs[&local]=100; CWorld::Players[0].m_pPed=&local;
    CNetworkPlayerManager::m_nMyId=id; CLocalPlayer::m_bIsHost=id==host;
    CFireSync::EnableNative(); scriptsReady=true; gameGeneration=1; controllingRestart=false; networkConnection=1; ownBirthFloor=0;
    Packets::Fires::Reset reset; reset.epoch=1; reset.host=host; reset.connection=10;reset.gameGeneration=1;reset.recipientBirth=1; CFireSync::Receive(reset);
    lastHello=CTimer::m_snTimeInMilliseconds; lastPedRef=100;
    Packets::Fires::Bind bind; bind.epoch=1; bind.entity={FireSync::Kind::Player,uint32_t(id),1,10,id,0}; CFireSync::Receive(bind);
}
static FireSync::State Sample(uint32_t generation=1) {
    FireSync::State s; s.key={1,1,generation,1}; s.strength=1;s.remaining=7000;return s;
}
static void TaskDamage(CPed* ped) {
    if(ped->burningTask) { alignas(4) uint8_t response[12];std::memset(response,0xAA,sizeof response);DamageHook(ped,response); }
}
int main() {
    CPlayerPed local; Setup(local);
    CPed npc; npc.m_nModelIndex=105; CPools::ped.refs[&npc]=501;
    CNetworkPed wrapper; wrapper.m_nPedId=0;wrapper.m_pPed=&npc;wrapper.m_nPedPoolRef=501;wrapper.m_bSyncing=true;wrapper.m_generation=3;
    CNetworkPedManager::m_pPeds.push_back(&wrapper);
    auto s=Sample(); s.target={FireSync::Kind::Ped,0,3,1,1,105};
    Packets::Fires::Update update;update.state=s;CFireSync::Receive(update); ApplyReplicas();
    expect(npc.m_pFire && npc.burningTask,"Actual ApplyReplicas initializes owner's native on-fire scanner/task seam");
    expect(npc.m_pIntelligence->m_eventScanner.requests==1,"Native scanner requested once per attached burn");
    ApplyReplicas();expect(npc.m_pIntelligence->m_eventScanner.requests==1,"Repeated state apply does not duplicate native burn setup");
    TaskDamage(&npc);expect(originalDamage==1 && npc.health==99.5f,"Actual owner applies one native task burn damage");
    wrapper.m_bSyncing=false; CNetworkPlayerManager::m_nMyId=0; CLocalPlayer::m_bIsHost=true;
    expectedHost=0;
    expect(!CFireSync::AllowPedDamage(&npc),"Host cannot damage guest-owned NPC representation");
    TaskDamage(&npc);expect(originalDamage==1,"Host representation adds no duplicate burn damage");
    alignas(4) uint8_t response[12];std::memset(response,0xAA,sizeof response);DamageHook(&npc,response);
    expect(response[10]==1 && response[0]==0 && response[8]==0,"Suppressed actual native damage hook initializes response and cannot fake death");
    CLocalPlayer::m_bIsHost=false;CNetworkPlayerManager::m_nMyId=2;
    expect(!CFireSync::AllowPedDamage(&npc),"Other guest representation cannot damage target");
    TaskDamage(&npc);expect(originalDamage==1,"Remote representation leaves owner damage applied exactly once");
    CNetworkPlayerManager::m_nMyId=1;wrapper.m_bSyncing=true;CPools::ped.refs[&npc]=502;
    expect(!CFireSync::AllowPedDamage(&npc),"Reused native pool reference cannot inherit active damage grant");

    Setup(local); int reserved=0;
    for(uint32_t gen=1;gen<=125;++gen) {
        update.state=Sample(gen);update.state.script=true;CFireSync::Receive(update);ApplyReplicas();
        expect(Native()[0].live,"Repeated scripted replica creates without exhausting native pool");
        Packets::Fires::Remove remove;remove.key={1,1,gen,2};CFireSync::Receive(remove);ApplyReplicas();
        reserved=0;for(auto& f:gFireManager.m_aFires)if(f.m_nFlags.bCreatedByScript)++reserved;
        expect(reserved==0,"Managed script reservation released after native extinguish");
    }
    Setup(local);scriptsReady=false;
    for(auto& f:gFireManager.m_aFires){f.m_nFlags.bActive=true;f.m_nFlags.bCreatedByScript=true;}
    scriptsReady=true;update.state=Sample();CFireSync::Receive(update);CFireSync::Process();
    int active=0;reserved=0;for(auto& f:gFireManager.m_aFires){active+=f.m_nFlags.bActive;reserved+=f.m_nFlags.bCreatedByScript;}
    expect(active==1 && reserved==0,"Pre-ready untracked guest fires reconciled before canonical replica creation");

    Setup(local);Packets::Fires::Reset next;next.epoch=2;next.host=2;next.connection=10;next.gameGeneration=1;next.recipientBirth=1;
    update.state=Sample();update.state.key.epoch=2;CFireSync::Receive(update);
    expect(Ledger().pending.slots[0].live && !Ledger().active.slots[0].live,"Future EVENT state staged before SYSTEM reset");
    CFireSync::Receive(next);CFireSync::HostChanged(2);
    expect(Ledger().active.epoch==2 && Ledger().active.slots[0].live,"Processed new epoch survives later host assignment");
    next.epoch=1;next.host=0;CFireSync::Receive(next);
    expect(Ledger().active.epoch==2 && Ledger().active.host==2,"Stale old-host reset cannot replace new epoch");
    Setup(local);auto* queued=new Packets::Fires::Update;queued->state=Sample();queued->state.key.epoch=2;
    GetPacketBuffer().m_packets.push_back(queued);CFireSync::HostChanged(2);
    expect(GetPacketBuffer().m_packets.size()==1,"SYSTEM host assignment preserves queued future EVENT state");
    CFireSync::Receive(*queued);GetPacketBuffer().m_packets.pop_front();delete queued;
    next.epoch=2;next.host=2;CFireSync::Receive(next);
    expect(Ledger().active.slots[0].live,"Queued EVENT-before-SYSTEM replay is not lost");

    Setup(local);CVehicle vehicle;vehicle.m_nModelIndex=400;CPools::car.refs[&vehicle]=601;
    CNetworkVehicle mapped;mapped.m_nVehicleId=0;mapped.m_pVehicle=&vehicle;CNetworkVehicleManager::m_pVehicles={&mapped};
    Packets::Fires::Bind carBind;carBind.epoch=1;carBind.entity={FireSync::Kind::Vehicle,0,7,1,0,400};CFireSync::Receive(carBind);
    update.state=Sample();update.state.target=carBind.entity;CFireSync::Receive(update);ApplyReplicas();
    expect(Native()[0].live && Native()[0].attached==nullptr,"Remote vehicle has visuals without independent damage attachment");
    CPools::car.refs[&vehicle]=602;ApplyReplicas();
    expect(!Native()[0].live,"Reused native vehicle pool reference invalidates old lease at apply time");
    carBind.live=false;CFireSync::Receive(carBind);carBind.live=true;CFireSync::Receive(carBind);
    expect(!Bindings()[FireSync::MaxPlayers].live,"Same-birth delayed bind cannot resurrect removed vehicle");
    carBind.entity.generation=8;CFireSync::Receive(carBind);
    expect(Bindings()[FireSync::MaxPlayers].live,"Fresh vehicle birth can bind reused slot");
    Setup(local);CVehicle ownCar;ownCar.m_nModelIndex=400;ownCar.m_pDriver=&local;CPools::car.refs[&ownCar]=701;
    CNetworkVehicle ownMapping;ownMapping.m_nVehicleId=1;ownMapping.m_pVehicle=&ownCar;ownMapping.m_bSyncing=true;
    CNetworkVehicleManager::m_pVehicles={&ownMapping};
    carBind.live=true;carBind.entity={FireSync::Kind::Vehicle,1,9,1,1,400};CFireSync::Receive(carBind);
    update.state=Sample();update.state.target=carBind.entity;CFireSync::Receive(update);ApplyReplicas();
    CFireSync::Tick(Native()[0].fire);const float ownerHealth=ownCar.m_fHealth;
    expect(ownerHealth<1000,"Actual granted vehicle owner applies native fire damage once");
    carBind.entity.owner=2;carBind.entity.ownerEpoch=2;CFireSync::Receive(carBind);
    update.state.key.sequence++;update.state.target=carBind.entity;CFireSync::Receive(update);
    CFireSync::Tick(Native()[0].fire);
    expect(ownCar.m_fHealth==ownerHealth,"Published owner transfer stops old-owner damage before native driver pointer catches up");

    Setup(local);CFireSync::Init();const uint32_t before=gameGeneration;
    Events::initScriptsEvent.Run();expect(gameGeneration==before+1,"Script lifetime changes even with reused native pool reference");
    expect(lastPedRef==-1 && !scriptsReady,"New script lifetime clears local fire tracking before ready");
    expect(CFireSync::NativeStart(nullptr,nullptr,CVector{}),"Uninitialized boundary remains original rather than globally suppressing offline setup");
    // Independent fire-actor-birth-001: old reset/binding/burn arrives after a
    // real script-generation change, with the same native ref 100 and model 0.
    scriptsReady=true;Packets::Fires::Reset oldReset;oldReset.epoch=1;oldReset.connection=10;oldReset.host=0;oldReset.gameGeneration=before;oldReset.recipientBirth=1;
    CFireSync::Receive(oldReset);
    Packets::Fires::Bind oldBind;oldBind.epoch=1;oldBind.entity={FireSync::Kind::Player,1,1,10,1,0};CFireSync::Receive(oldBind);
    update.state=Sample();update.state.target=oldBind.entity;CFireSync::Receive(update);
    local.burningTask=false;CFireSync::Process();
    expect(!local.m_pFire && !local.burningTask,"Delayed old-game RESET/BIND/STATE cannot initialize fresh player's native burn");
    expect(acknowledgedGame!=gameGeneration,"Own burn adoption waits for explicit current-game acknowledgement");
    auto ack=oldReset;ack.gameGeneration=gameGeneration;ack.recipientBirth=2;CFireSync::Receive(ack);CFireSync::Process();
    expect(!local.m_pFire,"Current ACK alone cannot adopt an old avatar birth from staged events");
    oldBind.entity.generation=2;CFireSync::Receive(oldBind);
    update.state.key.generation=2;update.state.target=oldBind.entity;CFireSync::Receive(update);CFireSync::Process();
    expect(local.m_pFire && local.burningTask,"Matching current-game ACK and fresh birth permit legitimate burn replay");
    // Independent unseen_birth_probe: first HELLO's binding has never arrived.
    Setup(local);CFireSync::Reset();gameGeneration=1;ownBirthFloor=0;scriptsReady=true;
    local.burningTask=false;local.m_pFire=nullptr;CFireSync::Process();
    Events::initScriptsEvent.Run();scriptsReady=true;
    expect(gameGeneration==2 && ownBirthFloor==0,"Reload before first binding keeps unseen previous birth floor zero");
    oldBind.epoch=1;oldBind.live=true;oldBind.entity={FireSync::Kind::Player,1,1,10,1,0};CFireSync::Receive(oldBind);
    update.state=Sample();update.state.target=oldBind.entity;CFireSync::Receive(update);
    ack.epoch=1;ack.host=0;ack.connection=10;ack.gameGeneration=2;ack.recipientBirth=2;CFireSync::Receive(ack);
    const int beforeOldBurn=originalDamage;CFireSync::Process();
    expect(!local.m_pFire && !local.burningTask,"Current-game ACK cannot adopt unseen old birth staged before fresh binding");
    TaskDamage(&local);expect(originalDamage==beforeOldBurn,"Unseen old birth cannot damage fresh local player");
    oldBind.entity.generation=2;CFireSync::Receive(oldBind);
    update.state.key.generation=2;update.state.target=oldBind.entity;CFireSync::Receive(update);CFireSync::Process();
    expect(local.m_pFire && local.burningTask,"Fresh birth binding completes deferred replay after SYSTEM acknowledgment");
    TaskDamage(&local);expect(originalDamage==beforeOldBurn+1,"Acknowledged current birth applies native damage exactly once");
    // A valid future own EVENT binding must survive an older same-game receipt.
    Setup(local);oldBind.entity={FireSync::Kind::Player,1,2,10,1,0};CFireSync::Receive(oldBind);
    expect(PendingBindings()[1].entity.generation==2 && Bindings()[1].entity.generation==1,
        "Future own EVENT birth stages without replacing currently acknowledged binding");
    update.state=Sample();update.state.target=oldBind.entity;CFireSync::Receive(update);
    ack.gameGeneration=1;ack.recipientBirth=1;CFireSync::Receive(ack);CFireSync::Process();
    expect(PendingBindings()[1].entity.generation==2 && !local.m_pFire,
        "Repeated older receipt preserves future binding without granting future burn authority");
    ack.recipientBirth=2;CFireSync::Receive(ack);CFireSync::Process();
    expect(Bindings()[1].entity.generation==2 && local.m_pFire && local.burningTask,
        "Matching later receipt promotes already staged EVENT birth and applies legitimate native burn");
    ack.recipientBirth=1;CFireSync::Receive(ack);
    expect(acknowledgedBirth==2,"Stale same-game receipt cannot roll acknowledged actor birth backwards");
    // Cross-feature boundary: repeated native ref/model with an old vehicle scene.
    Setup(local,1,1);CVehicle staleCar;staleCar.m_nModelIndex=400;staleCar.m_pDriver=&local;CPools::car.refs[&staleCar]=901;
    CNetworkVehicle staleMapping;staleMapping.m_nVehicleId=0;staleMapping.m_pVehicle=&staleCar;staleMapping.m_bSyncing=true;
    CNetworkVehicleManager::m_pVehicles={&staleMapping};
    carBind.entity={FireSync::Kind::Vehicle,0,21,1,1,400};carBind.live=true;carBind.epoch=1;CFireSync::Receive(carBind);
    staleMapping.nativeValid=false;
    expect(!Owned(&staleCar),"Invalid vehicle scene cannot fall back to host-owned world burn authority");
    expect(Capture(&staleCar).kind==FireSync::Kind::World,"Fire capture rejects stale mapped vehicle despite repeated native ref/model");
    expect(!Resolve(carBind.entity),"Fire attachment resolution waits for a valid current vehicle binding");
    std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;
}
