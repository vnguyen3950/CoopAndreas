#include "pickup_native_doubles.h"
#include "pickup_native_outcome.inc"
#include "pickup_client.inc"
#include "pickup_native_merge.inc"
#include "pickup_native_hooks.inc"
#include <cstring>
static unsigned checks=0,failures=0;
static void expect(bool value,const char*text){++checks;if(!value){++failures;std::cout<<"FAIL: "<<text<<'\n';}}
static CObject object;
static void Setup(int model=346,int type=3,uint32_t ammo=10);
static unsigned Removes();
static CObject dropObject;static int dropIndex=2;static unsigned generated=0;
static int __cdecl RecordedGenerate(CVector position,uint32_t model,uint8_t type,uint32_t ammo,uint32_t,bool,char*){
    ++generated;auto&pickup=CPickups::aPickUps[dropIndex];const auto reference=int16_t(pickup.m_nReferenceIndex+1);pickup={};
    pickup.m_nReferenceIndex=reference;pickup.m_nModelIndex=int16_t(model);pickup.m_nPickupType=type;pickup.m_nAmmo=ammo;pickup.position=position;
    dropObject.m_nModelIndex=int(model);dropObject.m_nAreaCode=0;dropObject.m_nObjectFlags={false,true};CPools::objects.refs[&dropObject]=8;
    pickup.m_pObject=&dropObject;return Handle(dropIndex);
}
static unsigned CopCreates(){unsigned count=0;for(const auto&p:GetPacketFactory().sent){const auto*a=dynamic_cast<const Packets::Pickups::Action*>(p.get());
    if(a&&a->operation==Packets::Pickups::Operation::Create&&a->item.cop.Present())++count;}return count;}
static void CopCases(){
    CPed cop;cop.m_nPedType=PED_TYPE_COP;cop.m_nModelIndex=280;
    auto method=&CPed::RecordedWeaponDrops;static_assert(sizeof(method)==sizeof(weaponDropsOriginal));std::memcpy(&weaponDropsOriginal,&method,sizeof method);
    generateOriginal=reinterpret_cast<void*>(&RecordedGenerate);
    auto prepare=[&](int owner){Setup();CNetworkPlayerManager::m_nMyId=owner;CLocalPlayer::m_bIsHost=owner==0;
        hasGrant=false;view.rows={};mappings={};capturedDeaths={};copContext={};deathIdentityAvailable=true;deathIdentity={9,2,6};
        CPools::pedRefs={{cop.poolRef,&cop}};nativeWeaponDropCalls=generated=0;dropIndex=2;replicaGenerate={};
        recordedWeaponDrops=[](CPed*){GenerateHook({},346,4,15,0,false,nullptr);};};
    for(int owner:{0,1}){
        prepare(owner);WeaponDropsHook(&cop,nullptr);
        expect(nativeWeaponDropCalls==1&&CopCreates()==1&&mappings[2].copItem.ammo==15,"Actual original native hook captures one unchanged cop quantity for either host or guest producer");
        unsigned ambient=0;for(const auto&p:GetPacketFactory().sent){auto*a=dynamic_cast<const Packets::Pickups::Action*>(p.get());if(a&&a->operation==Packets::Pickups::Operation::Create&&!a->item.cop.Present())++ambient;}
        expect(ambient==0,"A host cop-origin hook never also publishes ambient Create");
        dropIndex=3;WeaponDropsHook(&cop,nullptr);expect(CopCreates()==1&&nativeRemoves==1,"Repeated native death context cannot publish another manifest for its immutable seal");
    }
    prepare(0);deathIdentityAvailable=false;WeaponDropsHook(&cop,nullptr);
    expect(nativeWeaponDropCalls==0&&generated==0&&GetPacketFactory().sent.empty(),"Rejected replica/nonowner cop cannot execute native loot or fall back to host publication");
    prepare(0);CPed replacement;CPools::pedRefs[cop.poolRef]=&replacement;WeaponDropsHook(&cop,nullptr);
    expect(nativeWeaponDropCalls==0&&generated==0,"Recycled full native ped reference rejects the original drop caller");
    prepare(0);dropIndex=0;WeaponDropsHook(&cop,nullptr);
    expect(Removes()==0,"No canonical retirement when the fixture has no prior native-slot mapping");
    prepare(0);mappings[0]={31,1,Handle(0),false};dropIndex=0;WeaponDropsHook(&cop,nullptr);
    expect(Removes()==1&&CopCreates()==1,"Actual fresh cop generation retires an older owned canonical mapping before native-slot replacement");
    prepare(1);mappings[0]={31,1,Handle(0),true};dropIndex=0;WeaponDropsHook(&cop,nullptr);
    expect(Removes()==0&&CopCreates()==1,"Local cop generation replacing a replica slot does not delete its foreign canonical item");
    prepare(1);WeaponDropsHook(&cop,nullptr);deathIdentity={9,3,7};deathIdentityAvailable=false;CPickupSync::Process();
    bool originalSeal=true;for(const auto&p:GetPacketFactory().sent){auto*a=dynamic_cast<const Packets::Pickups::Action*>(p.get());if(a&&a->operation==Packets::Pickups::Operation::Create&&a->item.cop.Present())originalSeal&=a->item.cop.death.epoch==2&&a->item.cop.death.sequence==6;}
    expect(originalSeal,"Retry preserves the captured original death seal after the NPC lease changes");
    mappings[2].id=44;CPickupSync::Removed(&CPickups::aPickUps[2]);bool removed=false;for(const auto&p:GetPacketFactory().sent){auto*a=dynamic_cast<const Packets::Pickups::Action*>(p.get());if(a&&a->operation==Packets::Pickups::Operation::Remove&&a->id==44)removed=true;}
    expect(removed,"Original guest producer cleanup emits one owned-item removal after transfer");
    for(int local:{0,1}){
        prepare(local);localLife.generation=local==1?12:10;CStreaming::requests.clear();CStreaming::ms_aInfoForModel[346].m_nLoadState=0;
        PickupSync::Row row;row.item={};row.item.id=55;row.item.epoch=1;row.item.revision=1;row.item.creation=1;row.item.owner=1;
        row.item.model=346;row.item.type=4;row.item.ammo=15;row.item.cop={3,{9,2,6},1,11};row.stage=PickupSync::Stage::Active;
        Packets::Pickups::State state;state.reset=false;state.epoch=1;state.host=0;state.recipient={localLife.generation,2,7,0,0};state.row=row;CPickupSync::Receive(state);
        replicaGenerate=[](CVector pos,uint32_t model,uint8_t type,uint32_t ammo){dropIndex=4;return RecordedGenerate(pos,model,type,ammo,0,false,nullptr);};
        CPickupSync::Process();expect(generated==0&&!CStreaming::requests.empty(),"Remote cop loot requests missing native models before creating an inert replica");
        CStreaming::ms_aInfoForModel[346].m_nLoadState=LOADSTATE_LOADED;CPickupSync::Process();
        expect(generated==1&&mappings[4].replica&&!dropObject.m_nObjectFlags.bDoNotRender&&nativeCalls==0,"Host and reused producer slot both render remote-owner loot as replicas without benefits");
        CPickupSync::Removed(&CPickups::aPickUps[4]);expect(mappings[4].id==55,"Replica cleanup cannot publish original producer removal");
    }
    replicaGenerate={};CNetworkPlayerManager::m_nMyId=0;CLocalPlayer::m_bIsHost=true;localLife.generation=10;
}
static unsigned Removes(){unsigned count=0;for(const auto&packet:GetPacketFactory().sent){const auto*p=dynamic_cast<const Packets::Pickups::Action*>(packet.get());
    if(p&&p->operation==Packets::Pickups::Operation::Remove&&p->reason==PickupSync::Reason::Ambiguous)++count;}return count;}
static void Setup(int model,int type,uint32_t ammo){
    CPickupSync::Reset();view.Reset(1,0);expectedHost=0;scriptsReady=true;nativeEnabled=true;authenticated=true;connection=10;
    object.m_nModelIndex=model;object.m_nAreaCode=0;object.m_nObjectFlags={false,true};CPools::objects.refs[&object]=5;
    for(auto&entry:CModelInfo::ms_modelInfoPtrs)entry=&CModelInfo::info;
    auto&pickup=CPickups::aPickUps[0];pickup={};pickup.m_nModelIndex=int16_t(model);pickup.m_nPickupType=uint8_t(type);pickup.m_nAmmo=ammo;pickup.m_pObject=&object;
    PickupSync::Row row;row.item={};row.item.id=1;row.item.epoch=1;row.item.revision=2;row.item.creation=1;row.item.owner=0;row.item.model=model;row.item.type=type;row.item.ammo=ammo;
    row.stage=PickupSync::Stage::Reserved;row.grant=8;row.collector=0;row.collectorLife={10,2,7,0,0};view.Accept(row);
    mappings[0]={1,1,Handle(0),false};pendingGrant={};pendingGrant.operation=Packets::Pickups::Operation::Grant;pendingGrant.epoch=1;pendingGrant.sequence=8;pendingGrant.id=1;pendingGrant.grant=8;pendingGrant.actor=row.collectorLife;hasGrant=true;
    testPlayer={};localLife={10,2,7,0,0,10,true};lifeKnown=true;CTheScripts::ScriptSpace[1]=0;CGame::currArea=0;nativeCalls=nativeRemoves=nativeQueryFlags=0;GetPacketFactory().sent.clear();
}
static void HeldViewCases(){
    const int savedId=CNetworkPlayerManager::m_nMyId;const bool savedHost=CLocalPlayer::m_bIsHost;
    for(int mode=0;mode<6;++mode){
        Setup();CNetworkPlayerManager::m_nMyId=1;CLocalPlayer::m_bIsHost=false;view.rows[0].collector=1;
        const auto original=view.rows[0];const auto grant=pendingGrant;
        Packets::Pickups::State state;state.reset=false;state.epoch=1;state.host=0;state.recipient=original.collectorLife;state.row=original;++state.row.item.revision;
        if(mode==0)view.rows={};
        else if(mode==1||mode==3){state.row.stage=PickupSync::Stage::Removed;state.row.reason=PickupSync::Reason::Ambiguous;CPickupSync::Receive(state);}
        else if(mode==2){state.row.stage=PickupSync::Stage::Collected;CPickupSync::Receive(state);}
        else if(mode==4){++state.row.grant;CPickupSync::Receive(state);}
        else{state.row.collector=2;CPickupSync::Receive(state);}
        if(mode==3){state.row.item.id=2;state.row.item.creation=2;state.row.item.revision=1;state.row.stage=PickupSync::Stage::Active;state.row.grant=0;state.row.collector=-1;state.row.collectorLife={};CPickupSync::Receive(state);
            bool retained=false;for(const auto&row:view.rows)if(row.item.id==grant.id)retained=true;
            expect(!retained&&hasGrant,"Actual incoming terminal/active burst replaces the view row while retaining its pending grant");
        }
        const auto resources=PickupNative::Capture(&testPlayer);CPickupSync::Process();
        auto reports=[&](){unsigned count=0;for(const auto&packet:GetPacketFactory().sent){const auto*p=dynamic_cast<const Packets::Pickups::Action*>(packet.get());
            if(p&&p->operation==Packets::Pickups::Operation::Result&&p->epoch==grant.epoch&&p->id==grant.id&&p->grant==grant.grant
                &&PickupSync::SameLife(p->actor,grant.actor)&&p->actor.sequence==grant.actor.sequence&&p->outcome==PickupSync::Outcome::DeclinedBeforeApply)++count;}return count;};
        expect(!hasGrant&&receipt.Matches(grant.epoch,grant.id,grant.grant,grant.actor)&&reports()==1,"Absent, terminal or replaced metadata reports exact declined accounting before dropping the pending grant");
        expect(nativeCalls==0&&nativeQueryFlags==0&&PickupNative::Capture(&testPlayer)==resources,"Held-view decline never grants native resources or a collection query flag");
        CPickupSync::Receive(grant);CPickupSync::Process();
        expect(reports()==2&&nativeCalls==0&&nativeQueryFlags==0,"Duplicate held-view grant replays its exact declined receipt without native effects");
    }
    CNetworkPlayerManager::m_nMyId=savedId;CLocalPlayer::m_bIsHost=savedHost;
}
int main(){
    Setup(1240,3,0);nativeUpdate=[](CPickup*p,CPlayerPed*ped){ped->m_fHealth=100;p->m_nPickupType=0;return true;};ApplyGrant();
    expect(nativeCalls==1&&testPlayer.m_fHealth==100&&receipt.outcome==PickupSync::Outcome::Consumed&&nativeQueryFlags==1,"Actual native health benefit and local query flag follow one reserved call");
    hasGrant=true;ApplyGrant();expect(nativeCalls==1&&nativeQueryFlags==1,"Duplicate grant receipt never invokes native effects or consumptive query twice");
    Setup(1242,3,0);nativeUpdate=[](CPickup*p,CPlayerPed*ped){ped->m_fArmour=100;p->m_nPickupType=0;return true;};ApplyGrant();
    expect(testPlayer.m_fArmour==100&&receipt.outcome==PickupSync::Outcome::Consumed,"Actual collector armour change is acknowledged");
    Setup(1212,8,50);CWorld::Players[0].m_nMoney=0;nativeUpdate=[](CPickup*p,CPlayerPed*){CWorld::Players[0].m_nMoney+=p->m_nAmmo;p->m_nPickupType=0;return true;};ApplyGrant();
    expect(CWorld::Players[0].m_nMoney==50&&receipt.outcome==PickupSync::Outcome::Consumed,"Native money is credited once without a second wallet transaction");
    Setup();nativeUpdate=[](CPickup*p,CPlayerPed*ped){ped->m_aWeapons[2].m_eWeaponType=WEAPON_PISTOL;ped->m_aWeapons[2].m_nTotalAmmo+=p->m_nAmmo;p->m_nPickupType=0;return true;};ApplyGrant();
    expect(testPlayer.m_aWeapons[2].m_nTotalAmmo==10&&receipt.outcome==PickupSync::Outcome::Consumed,"Observed real inventory change acknowledges native weapon benefit");
    Setup(346,4,10);const int oldHandle=Handle(0);
    expect(CPickups::TryToMerge_WeaponType({},WEAPON_PISTOL,PICKUP_ONCE_TIMEOUT,5,false)&&CPickups::aPickUps[0].m_nAmmo==15&&Handle(0)==oldHandle,
        "Actual native dead-ped merge increases same-handle pile metadata");
    ApplyGrant();expect(nativeCalls==0&&receipt.outcome==PickupSync::Outcome::DeclinedBeforeApply,"Same-handle merge mismatch is rejected before native effects");
    Setup();nativeUpdate=[](CPickup*,CPlayerPed*ped){ped->m_aWeapons[2].m_nTotalAmmo+=5;return false;};ApplyGrant();
    expect(nativeCalls==1&&testPlayer.m_aWeapons[2].m_nTotalAmmo==5&&receipt.outcome==PickupSync::Outcome::UnknownAfterApply&&nativeQueryFlags==0,"False return with real ammo mutation is unknown retirement, never pre-apply decline");
    Setup();testPlayer.m_aWeapons[2].m_eWeaponType=WEAPON_AK47;ApplyGrant();expect(nativeCalls==0,"Conflicting weapon slot is rejected before partial extraction/replacement");
    Setup();testPlayer.m_nDelayedWeapon=WEAPON_PISTOL;ApplyGrant();expect(nativeCalls==0,"Existing delayed weapon excludes native silent dropped reward");
    Setup();CTheScripts::ScriptSpace[1]=1;ApplyGrant();expect(nativeCalls==0&&receipt.outcome==PickupSync::Outcome::DeclinedBeforeApply,"Mission transition during round trip prevents native grant");
    Setup();CGame::currArea=1;ApplyGrant();expect(nativeCalls==0,"Interior transition prevents native grant");
    Setup();localLife.birth=3;ApplyGrant();expect(nativeCalls==0,"Receipt cannot grant after actor restart");
    Setup();lifeKnown=false;ApplyGrant();expect(nativeCalls==0&&hasGrant,"Grant before acknowledged actor stays inert and bounded");
    Setup();CPickups::aPickUps[0].m_pObject=nullptr;ApplyGrant();expect(nativeCalls==0&&hasGrant&&!receipt.applied,"Unavailable native pickup object keeps inert pending work without fake resource");
    CPickups::aPickUps[0].m_pObject=&object;nativeUpdate=[](CPickup*p,CPlayerPed*ped){ped->m_aWeapons[2].m_eWeaponType=WEAPON_PISTOL;ped->m_aWeapons[2].m_nTotalAmmo+=p->m_nAmmo;p->m_nPickupType=0;return true;};ApplyGrant();
    expect(nativeCalls==1&&receipt.outcome==PickupSync::Outcome::Consumed,"Later native object readiness permits one legitimate grant");
    Setup();CPools::objects.refs.clear();ApplyGrant();expect(nativeCalls==0,"Recycled/unbound native object cannot pass grant metadata guard");
    Setup();testPlayer.position.x=3;ApplyGrant();expect(nativeCalls==0&&!hasGrant&&receipt.outcome==PickupSync::Outcome::DeclinedBeforeApply,"Moving away during a grant declines before native effects");
    Setup(1240,3,0);testPlayer.m_fHealth=100;ApplyGrant();expect(nativeCalls==0&&!hasGrant&&receipt.outcome==PickupSync::Outcome::DeclinedBeforeApply,"Becoming ineligible during a grant declines without native benefit");
    Setup();CStreaming::ms_aInfoForModel[346].m_nLoadState=0;ApplyGrant();expect(nativeCalls==0&&hasGrant&&!receipt.applied,"Missing weapon model retains inert pending work");
    CStreaming::ms_aInfoForModel[346].m_nLoadState=LOADSTATE_LOADED;ApplyGrant();expect(nativeCalls==1&&!hasGrant,"Ready model permits exactly one native attempt");
    for(auto stage:{PickupSync::Stage::Active,PickupSync::Stage::Reserved}){
        Setup(346,4,10);hasGrant=false;auto row=view.rows[0];row.stage=stage;++row.item.revision;view.Accept(row);
        CPickups::aPickUps[0].m_pObject=nullptr;CPickupSync::Process();
        expect(nativeRemoves==0&&Removes()==0&&mappings[0].id==1,"Ordinary offscreen object absence does not retire active or reserved metadata");
        CPickups::aPickUps[0].m_pObject=&object;CPickups::TryToMerge_WeaponType({},WEAPON_PISTOL,PICKUP_ONCE_TIMEOUT,5,false);CPickupSync::Process();
        expect(nativeRemoves==1&&Removes()==1&&mappings[0].id==0&&CPickups::aPickUps[0].m_nPickupType==PICKUP_NONE,"Actual active or reserved native merge retires the pile and clears its mapping");
        CPickupSync::Process();CPickupSync::Update(&CPickups::aPickUps[0],&testPlayer,nullptr,0);
        unsigned creates=0;for(const auto&packet:GetPacketFactory().sent){const auto*p=dynamic_cast<const Packets::Pickups::Action*>(packet.get());if(p&&p->operation==Packets::Pickups::Operation::Create)++creates;}
        expect(nativeRemoves==1&&creates==0,"Retired merged full pile is never automatically republished or re-reserved");
    }
    HeldViewCases();CopCases();
    std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;
}
