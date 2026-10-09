#include "pickup_native_doubles.h"
#include "pickup_native_outcome.inc"
#include "pickup_client.inc"
#include "pickup_native_merge.inc"
static unsigned checks=0,failures=0;
static void expect(bool value,const char*text){++checks;if(!value){++failures;std::cout<<"FAIL: "<<text<<'\n';}}
static CObject object;
static unsigned Removes(){unsigned count=0;for(const auto&packet:GetPacketFactory().sent){const auto*p=dynamic_cast<const Packets::Pickups::Action*>(packet.get());
    if(p&&p->operation==Packets::Pickups::Operation::Remove&&p->reason==PickupSync::Reason::Ambiguous)++count;}return count;}
static void Setup(int model=346,int type=3,uint32_t ammo=10){
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
    HeldViewCases();
    std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;
}
