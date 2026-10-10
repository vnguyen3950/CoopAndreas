#include "stdafx.h"
#include "CPickupSync.h"
#include "PickupNativeOutcome.h"
#include "CPlayerAnimationSync.h"
#include "CNetworkPedManager.h"
#include <CGame.h>
#include "runtime_diagnostics.h"

namespace {
enum class TraceStage {State,Death,Create,Hello,Receive,Replica,Grant,Remove,Reset,Count};
void Trace(TraceStage stage,const char*reason,uint32_t generation=0,uint32_t sequence=0,uint32_t id=0,int code=0){
    static std::array<uint8_t,size_t(TraceStage::Count)> counts{};
    auto&count=counts[size_t(stage)];if(count>=8)return;++count;
    RuntimeDiagnostics::Write("pickup-client","\"event\":\"pickup-stage\",\"stage\":%u,\"reason\":\"%s\",\"generation\":%u,\"sequence\":%u,\"id\":%u,\"code\":%d,\"count\":%u",unsigned(stage),reason,generation,sequence,id,code,unsigned(count));
}
PickupSync::View view;
struct Mapping {uint32_t id=0,creation=0;int handle=-1;bool replica=false;PickupSync::Item copItem;uint32_t copStarted=0;};
std::array<Mapping,PickupSync::MaxPickups> mappings{};
struct Hidden {int handle=-1;bool hidden=false,previous=false;};
std::array<Hidden,PickupSync::MaxPickups> hidden{};
PickupSync::Receipt receipt;
Packets::Pickups::Action pendingGrant;
bool hasGrant=false,scriptsReady=false,nativeEnabled=false,receiving=false;
bool authenticated=false;uint32_t connection=0,sequence=0,creation=0,lastHello=0,lastClaim=0;
int expectedHost=-1;
struct CopContext {bool active=false;PickupSync::CopOrigin origin;int model=-1,area=-1,type=-1,createdBy=0;bool money=false,persistent=false;unsigned output=0;};
CopContext copContext;
std::array<NPCSync::Stamp,255> capturedDeaths{};
std::array<uint8_t,255> capturedKinds{};
int Index(CPickup* pickup){const auto p=reinterpret_cast<uintptr_t>(pickup),b=reinterpret_cast<uintptr_t>(CPickups::aPickUps);
    return p>=b&&p<b+sizeof(CPickup)*PickupSync::MaxPickups&&(p-b)%sizeof(CPickup)==0?int((p-b)/sizeof(CPickup)):-1;}
int Handle(int index){return int((uint32_t(uint16_t(CPickups::aPickUps[index].m_nReferenceIndex))<<16)|uint32_t(index));}
CPickup* Bound(const Mapping&map){if(map.handle==-1)return nullptr;const auto raw=uint32_t(map.handle);const uint32_t i=raw&0xffff;
    return i<PickupSync::MaxPickups&&Handle(int(i))==map.handle?&CPickups::aPickUps[i]:nullptr;}
bool Mission(){return CTheScripts::OnAMissionFlag&&CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag]!=0;}
bool Life(PickupSync::Actor&actor){PlayerAnimation::Life life;if(!CPlayerAnimationSync::GetLocalLife(life)||!life.ready)return false;
    auto*ped=FindPlayerPed(0);if(!ped||!CPools::ms_pPedPool||CPools::GetPed(life.nativeReference)!=ped||CWorld::Players[0].m_pPed!=ped)return false;
    actor={life.generation,life.birth,life.sequence,life.model,life.area};return actor.Valid();}
bool Ready(){return nativeEnabled&&CNetwork::m_bAuthenticated&&scriptsReady&&gGameState==9&&CWorld::PlayerInFocus==0;}
bool Host(){return Ready()&&CLocalPlayer::m_bIsHost&&view.epoch&&view.host==CNetworkPlayerManager::m_nMyId&&expectedHost==view.host;}
bool OwnItem(const PickupSync::Item&item){
    if(item.owner!=CNetworkPlayerManager::m_nMyId)return false;
    PickupSync::Actor life;return !item.cop.Present()||(Life(life)&&life.generation==item.cop.producerGeneration);
}
PickupSync::Item Metadata(CPickup*pickup,uint32_t token){PickupSync::Item item;const auto pos=pickup->GetPosn();item.position={pos.x,pos.y,pos.z};
    item.creation=token;item.owner=CNetworkPlayerManager::m_nMyId;item.model=pickup->m_nModelIndex;item.type=pickup->m_nPickupType;item.ammo=pickup->m_nAmmo;
    item.remaining=pickup->m_nRegenerationTime>CTimer::m_snTimeInMilliseconds?(std::min)(600000u,pickup->m_nRegenerationTime-CTimer::m_snTimeInMilliseconds):0;
    item.area=CGame::currArea;return item;}
bool Action(Packets::Pickups::Action&packet){PickupSync::Actor life;if(sequence==PickupSync::MaxCounter||!Life(life)||!view.epoch)return false;
    packet.epoch=view.epoch;packet.sequence=++sequence;packet.actor=life;const auto pos=FindPlayerPed(0)->GetPosition();packet.position={pos.x,pos.y,pos.z};
    packet.mission=Mission();packet.inVehicle=FindPlayerPed(0)->m_nPedFlags.bInVehicle;if(!packet.Valid())return false;GetPacketFactory().Send(packet);return true;}
void RestoreHidden(){for(int i=0;i<PickupSync::MaxPickups;++i)if(hidden[i].hidden){
    if(Handle(i)==hidden[i].handle&&CPickups::aPickUps[i].m_pObject)CPickups::aPickUps[i].m_pObject->m_nObjectFlags.bDoNotRender=hidden[i].previous;hidden[i]={};}}
void ClearMappings(){const bool saved=receiving;receiving=true;for(auto&map:mappings){if(map.replica||map.copItem.cop.Present())if(auto*pickup=Bound(map))CPickupSync::NativeRemove(pickup);map={};}receiving=saved;RestoreHidden();}
void SendResult(const PickupSync::Receipt&result){if(!CNetwork::m_bAuthenticated||!result.applied||sequence==PickupSync::MaxCounter)return;
    Packets::Pickups::Action packet;packet.operation=Packets::Pickups::Operation::Result;packet.epoch=result.epoch;packet.sequence=++sequence;
    packet.id=result.id;packet.grant=result.grant;packet.actor=result.actor;packet.outcome=result.outcome;GetPacketFactory().Send(packet);}
void ApplyGrant(){
    if(!hasGrant||!Ready())return;const auto grant=pendingGrant;
    if(receipt.Matches(grant.epoch,grant.id,grant.grant,grant.actor)){SendResult(receipt);hasGrant=false;return;}
    if(receipt.applied&&grant.grant<receipt.grant){hasGrant=false;return;}
    PickupSync::Actor life;if(!Life(life))return; // Grant before exact actor acknowledgement stays bounded and inert.
    receipt={grant.epoch,grant.id,grant.grant,grant.actor,PickupSync::Outcome::DeclinedBeforeApply,true};
    if(grant.epoch!=view.epoch||!PickupSync::SameLife(life,grant.actor)||Mission()||life.area!=0||CGame::currArea!=0){SendResult(receipt);hasGrant=false;return;}
    const PickupSync::Row*row=nullptr;for(const auto&r:view.rows)if(r.item.id==grant.id){row=&r;break;}
    // A terminal view slot may already hold a newer item. Settle the exact
    // issued grant even when its metadata is gone; never call native effects.
    if(!row||row->grant!=grant.grant||row->collector!=CNetworkPlayerManager::m_nMyId){SendResult(receipt);hasGrant=false;return;}
    if(row->stage!=PickupSync::Stage::Reserved){SendResult(receipt);hasGrant=false;return;}
    Mapping*map=nullptr;for(auto&m:mappings)if(m.id==grant.id){map=&m;break;}
    if(!map||!Bound(*map)){receipt.applied=false;return;} // Model/pool unavailable: never consume or fabricate a benefit.
    auto*pickup=Bound(*map);auto*ped=FindPlayerPed(0);
    if(!PickupNative::MetadataMatches(pickup,row->item)){SendResult(receipt);hasGrant=false;return;}
    const auto position=ped->GetPosition();
    if(!ped->IsAlive()||ped->m_nPedFlags.bInVehicle||!PickupSync::Touching({position.x,position.y,position.z},row->item.position)){SendResult(receipt);hasGrant=false;return;}
    // An offscreen visual or failed model/pool allocation is not a changed
    // resource. Keep this one grant inert until its native representation exists.
    if(!pickup->m_pObject||!PickupNative::ModelsReady(pickup)){receipt.applied=false;return;}
    if(!PickupNative::Matches(pickup,row->item)||!PickupNative::Eligible(pickup,ped,Mission(),CGame::currArea)){SendResult(receipt);hasGrant=false;return;}
    const auto before=PickupNative::Capture(ped);const auto savedExpiry=pickup->m_nRegenerationTime;
    // Update's expiry branch also returns true. A reserved grant must exercise
    // actual collection, never timeout cleanup. Restore only on unchanged data.
    pickup->m_nRegenerationTime=CTimer::m_snTimeInMilliseconds+600000;
    const bool saved=receiving;receiving=true;const bool result=pickup->Update(ped,nullptr,0);receiving=saved;
    const auto after=PickupNative::Capture(ped);const bool retired=pickup->m_nPickupType==PICKUP_NONE||pickup->m_nFlags.bDisabled;
    receipt.outcome=PickupNative::Outcome(result,row->item,before,after,retired);
    Trace(TraceStage::Grant,"native-outcome",grant.actor.generation,grant.actor.sequence,grant.id,int(receipt.outcome));
    if(!retired)pickup->m_nRegenerationTime=savedExpiry;
    if(receipt.outcome==PickupSync::Outcome::Consumed){const int index=Index(pickup);if(index>=0)CPickups::AddToCollectedPickupsArray(index);}
    SendResult(receipt);hasGrant=false;
}
void Replicas(){
    const bool saved=receiving;receiving=true;
    for(const auto&row:view.rows){if(!row.item.id)continue;Mapping*found=nullptr;
        for(auto&map:mappings)if(map.id==row.item.id){found=&map;break;}
        if(row.stage==PickupSync::Stage::Collected||row.stage==PickupSync::Stage::Removed){if(found){if(auto*pickup=Bound(*found)){
            if(row.reason!=PickupSync::Reason::SceneEnded)CPickupSync::NativeRemove(pickup);}*found={};}continue;}
        if(!found&&!OwnItem(row.item)){
            if(!PickupNative::RequestReplicaModels(row.item)){Trace(TraceStage::Replica,"model-wait",row.item.cop.death.generation,0,row.item.id,row.item.model);continue;}
            const CVector pos{row.item.position.x,row.item.position.y,row.item.position.z};
            const int handle=CPickupSync::Generate(pos,row.item.model,uint8_t(row.item.type),row.item.ammo,0,false,nullptr);
            if(handle==-1){Trace(TraceStage::Replica,"generation-failed",row.item.cop.death.generation,0,row.item.id,row.item.model);continue;}Trace(TraceStage::Replica,"generated",row.item.cop.death.generation,0,row.item.id,row.item.model);const int i=int(uint32_t(handle)&0xffff);if(i>=PickupSync::MaxPickups)continue;
            mappings[i]={row.item.id,row.item.creation,handle,true};found=&mappings[i];
        }
        if(found)if(auto*pickup=Bound(*found))if(pickup->m_pObject)pickup->m_pObject->m_nObjectFlags.bDoNotRender=
            row.stage==PickupSync::Stage::Reserved&&row.collector!=CNetworkPlayerManager::m_nMyId;
    }
    receiving=saved;
}
}
bool CPickupSync::Replay(){return receiving;}
namespace {
bool BeginDeath(CPed*ped,bool money){
    copContext={};
    if(!nativeEnabled||!CNetwork::m_bAuthenticated)return true;
    if(!ped||!CPools::ms_pPedPool)return false;
    const int reference=CPools::GetPedRef(ped);
    if(reference<0||CPools::GetPed(reference)!=ped)return false;
    if(ped->m_nPedType<PED_TYPE_CIVMALE)return true; // Player death remains native, outside this slice.
    copContext.active=true;copContext.model=ped->m_nModelIndex;copContext.area=ped->m_nAreaCode;
    copContext.type=ped->m_nPedType;copContext.createdBy=ped->m_nCreatedBy;copContext.money=money;copContext.persistent=ped->m_nPedFlags.bDeathPickupsPersist;
    // This gate applies to every NPC, including a nonowner on the room host.
    if(!Ready()){Trace(TraceStage::Death,"not-ready",0,0,0);return false;}
    if(!CNetworkPedManager::GetOwnerDeathIdentity(ped,copContext.origin.ped,copContext.origin.death)){Trace(TraceStage::Death,"owner-seal-unavailable",0,0,0);return false;}
    // Engine lifetime markers on recreated replicas are not original provenance.
    // The owner seal already validates authority; GetPed revalidates the full
    // pool reference before reading the wrapper's retained creation metadata.
    auto*networkPed=CNetworkPedManager::GetPed(ped);
    if(!networkPed||!networkPed->HasValidPed()||networkPed->m_pPed!=ped){Trace(TraceStage::Death,"wrapper-unbound",copContext.origin.death.generation,copContext.origin.death.sequence);return false;}
    copContext.createdBy=networkPed->m_nCreatedBy;
    Trace(TraceStage::Death,"sealed",copContext.origin.death.generation,copContext.origin.death.sequence,uint32_t(copContext.origin.ped),copContext.createdBy);
    PickupSync::Actor life;if(!Life(life)){Trace(TraceStage::Death,"local-life-unavailable");return false;}copContext.origin.producerGeneration=life.generation;
    const int id=copContext.origin.ped;if(id<0||id>=255||!copContext.origin.death.State())return false;
    if(!PickupSync::SameSeal(capturedDeaths[id],copContext.origin.death)){capturedDeaths[id]=copContext.origin.death;capturedKinds[id]=0;}
    const uint8_t kind=money?2:1;if(capturedKinds[id]&kind)return false;
    capturedKinds[id]|=kind;return true; // Execute each native routine at most once per exact death.
}
}
bool CPickupSync::BeginCopDrops(CPed*ped){return BeginDeath(ped,false);}
bool CPickupSync::BeginMoneyDrops(CPed*ped){return BeginDeath(ped,true);}
void CPickupSync::EndCopDrops(){copContext={};}
bool CPickupSync::SeparateDeathWeapon(int model,uint8_t type,uint32_t ammo){
    return copContext.active&&!copContext.money&&!copContext.persistent&&copContext.origin.death.State()&&copContext.origin.producerGeneration
        &&!Mission()&&copContext.area==0&&PickupSync::OrdinaryNPC(copContext.model,copContext.type,copContext.createdBy)&&type==4
        &&(copContext.type!=PED_TYPE_COP||PickupSync::StockCopWeapon(copContext.model,model))&&ammo>0&&ammo<=PickupSync::DeathWeaponLimit(model);
}
void CPickupSync::EnableNative(){nativeEnabled=true;}
void CPickupSync::Reset(){Trace(TraceStage::Reset,"reset",0,0,view.epoch);ClearMappings();view={};hasGrant=false;receipt={};lastHello=lastClaim=0;expectedHost=-1;}
void CPickupSync::HostChanged(int host){expectedHost=host;lastHello=0;}
void CPickupSync::Init(){NativeInit();Events::initScriptsEvent.before+=[]{scriptsReady=false;Reset();};
    Events::processScriptsEvent.after+=[]{if(gGameState==9)scriptsReady=true;};gameShutdownEvent.before+=[]{scriptsReady=false;Reset();};}
void CPickupSync::Receive(const Packets::Pickups::State&packet){
    if(!CNetwork::m_bAuthenticated||!packet.Valid()){Trace(TraceStage::Receive,"unauthenticated-or-invalid");return;}
    PickupSync::Actor life;if(!Life(life)||!PickupSync::SameLife(life,packet.recipient)){Trace(TraceStage::Receive,"recipient-life-mismatch",packet.recipient.generation,packet.recipient.sequence,packet.epoch);return;}
    Trace(TraceStage::Receive,packet.reset?"reset":"row",packet.recipient.generation,packet.recipient.sequence,packet.reset?packet.epoch:packet.row.item.id);
    if(packet.reset){if(packet.epoch>view.epoch)ClearMappings();if(view.Reset(packet.epoch,packet.host)&&expectedHost==-1)expectedHost=packet.host;return;}
    if(packet.epoch!=view.epoch||!view.Accept(packet.row))return;
    if(OwnItem(packet.row.item)){bool found=false;
        for(auto&map:mappings)if(map.creation==packet.row.item.creation&&!map.replica){map.id=packet.row.item.id;found=Bound(map)&&Bound(map)->m_nPickupType!=PICKUP_NONE;break;}
        if(!found&&packet.row.stage==PickupSync::Stage::Active){Packets::Pickups::Action remove;remove.operation=Packets::Pickups::Operation::Remove;remove.id=packet.row.item.id;Action(remove);}
    }
}
void CPickupSync::Receive(const Packets::Pickups::Action&packet){if(!CNetwork::m_bAuthenticated||!packet.Valid()||packet.operation!=Packets::Pickups::Operation::Grant)return;
    if(hasGrant&&pendingGrant.grant!=packet.grant)return;pendingGrant=packet;hasGrant=true;}
void CPickupSync::Created(int handle,bool freshCreation){
    if(Replay()||!Ready()||handle==-1)return;
    const uint32_t index=uint32_t(handle)&0xffff;if(index>=PickupSync::MaxPickups)return;auto*pickup=&CPickups::aPickUps[index];
    auto&map=mappings[index];
    if(freshCreation&&map.id){
        if(!map.replica&&(Host()||map.copItem.cop.Present())){Packets::Pickups::Action remove;remove.operation=Packets::Pickups::Operation::Remove;
            remove.id=map.id;remove.reason=PickupSync::Reason::Ambiguous;Action(remove);}
        map={}; // A local replacement never deletes a foreign canonical item.
    }
    if(copContext.active){
        // Captured outputs, never reconstructed rewards or ambient host Create.
        const unsigned ordinal=++copContext.output;
        if(Mission()||copContext.area!=0||!PickupSync::OrdinaryNPC(copContext.model,copContext.type,copContext.createdBy)){Trace(TraceStage::Create,"out-of-scope",copContext.origin.death.generation,copContext.origin.death.sequence,0,copContext.createdBy);return;}
        if(copContext.money){
            if(!PickupSync::NativeMoneyNPC(copContext.type)||ordinal>PickupSync::MaxDeathMoney||pickup->m_nPickupType!=8||pickup->m_nModelIndex!=1212)return;
        }else if(copContext.persistent||ordinal>PickupSync::MaxDeathWeapons||pickup->m_nPickupType!=4
            ||!PickupSync::WeaponModel(pickup->m_nModelIndex)
            ||(copContext.type==PED_TYPE_COP&&!PickupSync::StockCopWeapon(copContext.model,pickup->m_nModelIndex)))return;
        if(!copContext.origin.death.State()||creation==PickupSync::MaxCounter)return;
        auto item=Metadata(pickup,creation+1);item.area=copContext.area;item.cop=copContext.origin;item.cop.sequence=item.creation;
        item.cop.ordinal=uint8_t(ordinal+(copContext.money?PickupSync::MaxDeathWeapons:0));
        if(!item.ValidMetadata())return;
        ++creation;map={0,creation,handle,false,item,GetTickCount()};
        Packets::Pickups::Action create;create.operation=Packets::Pickups::Operation::Create;create.item=item;const bool sent=Action(create);
        Trace(TraceStage::Create,sent?"manifest-sent":"manifest-pending",item.cop.death.generation,item.cop.death.sequence,item.creation,item.cop.ordinal);return;
    }
    if(!Host())return;
    // A successful native creation callback terminates any prior mapping even
    // if its new native type is unsupported. Do not expose that new type as shared.
    if(Mission()||CGame::currArea!=0||creation==PickupSync::MaxCounter)return;
    auto item=Metadata(pickup,creation+1);if(!item.ValidMetadata())return;
    if(!freshCreation&&map.creation&&map.handle==handle)return;
    if(map.id){Packets::Pickups::Action remove;remove.operation=Packets::Pickups::Operation::Remove;remove.id=map.id;Action(remove);}
    ++creation;map={0,creation,handle,false};Packets::Pickups::Action create;create.operation=Packets::Pickups::Operation::Create;create.item=item;Action(create);
}
void CPickupSync::Removed(CPickup*pickup){if(Replay()||!Ready())return;const int i=Index(pickup);if(i<0)return;auto&map=mappings[i];
    if(map.replica||(!Host()&&!map.copItem.cop.Present()))return;
    if(map.id){Packets::Pickups::Action packet;packet.operation=Packets::Pickups::Operation::Remove;packet.id=map.id;Action(packet);}map={};}
bool CPickupSync::Update(CPickup*pickup,CPlayerPed*player,CVehicle*vehicle,int playerId){
    if(Replay()||!Ready())return pickup->Update(player,vehicle,playerId);const int i=Index(pickup);if(i<0)return pickup->Update(player,vehicle,playerId);
    auto&map=mappings[i];if(map.handle!=-1&&map.handle!=Handle(i))map={};
    if(!map.creation&&!Mission()&&CGame::currArea==0){auto metadata=Metadata(pickup,1);if(metadata.ValidMetadata()){
        if(Host())Created(Handle(i));else if(pickup->m_pObject){if(!hidden[i].hidden||hidden[i].handle!=Handle(i))hidden[i]={Handle(i),true,bool(pickup->m_pObject->m_nObjectFlags.bDoNotRender)};
            pickup->m_pObject->m_nObjectFlags.bDoNotRender=true;return false;}}}
    if(!map.creation)return pickup->Update(player,vehicle,playerId);
    if(Mission()||CGame::currArea!=0)return false;
    if((pickup->m_nPickupType==4||pickup->m_nPickupType==5||pickup->m_nPickupType==8)&&pickup->m_nRegenerationTime<CTimer::m_snTimeInMilliseconds){
        if(!map.replica&&(Host()||map.copItem.cop.Present())){Packets::Pickups::Action packet;packet.operation=Packets::Pickups::Operation::Remove;packet.id=map.id;packet.reason=PickupSync::Reason::Expired;Action(packet);}return false;}
    PickupSync::Actor life;if(map.id&&Life(life)&&life.area==0&&!hasGrant&&GetTickCount()-lastClaim>=500&&PickupNative::Eligible(pickup,FindPlayerPed(0),false,0)){
        const PickupSync::Row*row=nullptr;for(const auto&r:view.rows)if(r.item.id==map.id){row=&r;break;}
        if(row&&row->stage==PickupSync::Stage::Active){Packets::Pickups::Action packet;packet.operation=Packets::Pickups::Operation::Claim;packet.id=map.id;if(Action(packet))lastClaim=GetTickCount();}
    }
    return false;
}
void CPickupSync::Process(){
    const unsigned state=(nativeEnabled?1u:0u)|(CNetwork::m_bAuthenticated?2u:0u)|(scriptsReady?4u:0u)|(gGameState==9?8u:0u)|(CWorld::PlayerInFocus==0?16u:0u);
    static unsigned lastState=~0u;if(lastState!=state){lastState=state;Trace(TraceStage::State,"readiness-mask",0,0,view.epoch,int(state));}
    if(!CNetwork::m_bAuthenticated){if(authenticated)Reset();authenticated=false;return;}if(!Ready())return;
    const uint32_t current=CNetwork::m_pPeer?CNetwork::m_pPeer->connectID:0;
    if(!authenticated||current!=connection){Reset();sequence=0;capturedDeaths={};capturedKinds={};authenticated=true;connection=current;}
    PickupSync::Actor life;const bool lifeReady=Life(life);static int previousLife=-1;
    if(previousLife!=int(lifeReady)){previousLife=int(lifeReady);Trace(TraceStage::State,lifeReady?"local-life-ready":"local-life-unavailable",life.generation,life.sequence,view.epoch);}
    if(!lifeReady)return;
    if(GetTickCount()-lastHello>=2000||!view.epoch){Packets::Pickups::Hello packet;packet.actor=life;const auto pos=FindPlayerPed(0)->GetPosition();
        packet.position={pos.x,pos.y,pos.z};packet.mission=Mission();GetPacketFactory().Send(packet);lastHello=GetTickCount();Trace(TraceStage::Hello,"sent",life.generation,life.sequence,view.epoch);}
    if(!view.epoch||expectedHost!=view.host)return;
    if(Mission()||CGame::currArea!=0){RestoreHidden();ApplyGrant();return;}
    Replicas();ApplyGrant();
    for(auto&map:mappings)if(!map.replica&&(Host()||map.copItem.cop.Present())&&map.id)if(auto*pickup=Bound(map))if(pickup->m_pObject){
        const PickupSync::Row*row=nullptr;for(const auto&r:view.rows)if(r.item.id==map.id){row=&r;break;}
        if(row&&(row->stage==PickupSync::Stage::Active||row->stage==PickupSync::Stage::Reserved)&&!PickupNative::Matches(pickup,row->item)){
            Packets::Pickups::Action remove;remove.operation=Packets::Pickups::Operation::Remove;remove.id=map.id;remove.reason=PickupSync::Reason::Ambiguous;
            if(Action(remove)){const bool saved=receiving;receiving=true;NativeRemove(pickup);receiving=saved;map={};}
        }
    }
    for(auto&map:mappings)if(!map.replica&&(Host()||map.copItem.cop.Present())&&map.creation&&!map.id)if(auto*pickup=Bound(map))if(pickup->m_nPickupType!=PICKUP_NONE){
        if(map.copItem.cop.Present()&&GetTickCount()-map.copStarted>15000){const bool saved=receiving;receiving=true;NativeRemove(pickup);receiving=saved;map={};continue;}
        auto item=map.copItem.cop.Present()?map.copItem:Metadata(pickup,map.creation);
        if(item.ValidMetadata()){Packets::Pickups::Action create;create.operation=Packets::Pickups::Operation::Create;create.item=item;Action(create);}
    }
}
