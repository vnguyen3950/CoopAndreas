#include "stdafx.h"
#include "CPickupSync.h"
#include "PickupNativeOutcome.h"
#include "CPlayerAnimationSync.h"
#include <CGame.h>

namespace {
PickupSync::View view;
struct Mapping {uint32_t id=0,creation=0;int handle=-1;bool replica=false;};
std::array<Mapping,PickupSync::MaxPickups> mappings{};
struct Hidden {int handle=-1;bool hidden=false,previous=false;};
std::array<Hidden,PickupSync::MaxPickups> hidden{};
PickupSync::Receipt receipt;
Packets::Pickups::Action pendingGrant;
bool hasGrant=false,scriptsReady=false,nativeEnabled=false,receiving=false;
bool authenticated=false;uint32_t connection=0,sequence=0,creation=0,lastHello=0,lastClaim=0;
int expectedHost=-1;
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
PickupSync::Item Metadata(CPickup*pickup,uint32_t token){PickupSync::Item item;const auto pos=pickup->GetPosn();item.position={pos.x,pos.y,pos.z};
    item.creation=token;item.owner=CNetworkPlayerManager::m_nMyId;item.model=pickup->m_nModelIndex;item.type=pickup->m_nPickupType;item.ammo=pickup->m_nAmmo;
    item.remaining=pickup->m_nRegenerationTime>CTimer::m_snTimeInMilliseconds?(std::min)(600000u,pickup->m_nRegenerationTime-CTimer::m_snTimeInMilliseconds):0;
    item.area=CGame::currArea;return item;}
bool Action(Packets::Pickups::Action&packet){PickupSync::Actor life;if(sequence==PickupSync::MaxCounter||!Life(life)||!view.epoch)return false;
    packet.epoch=view.epoch;packet.sequence=++sequence;packet.actor=life;const auto pos=FindPlayerPed(0)->GetPosition();packet.position={pos.x,pos.y,pos.z};
    packet.mission=Mission();packet.inVehicle=FindPlayerPed(0)->m_nPedFlags.bInVehicle;if(!packet.Valid())return false;GetPacketFactory().Send(packet);return true;}
void RestoreHidden(){for(int i=0;i<PickupSync::MaxPickups;++i)if(hidden[i].hidden){
    if(Handle(i)==hidden[i].handle&&CPickups::aPickUps[i].m_pObject)CPickups::aPickUps[i].m_pObject->m_nObjectFlags.bDoNotRender=hidden[i].previous;hidden[i]={};}}
void ClearMappings(){const bool saved=receiving;receiving=true;for(auto&map:mappings){if(map.replica)if(auto*pickup=Bound(map))CPickupSync::NativeRemove(pickup);map={};}receiving=saved;RestoreHidden();}
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
        if(!found&&!Host()){
            const CVector pos{row.item.position.x,row.item.position.y,row.item.position.z};
            const int handle=CPickupSync::Generate(pos,row.item.model,uint8_t(row.item.type),row.item.ammo,0,false,nullptr);
            if(handle==-1)continue;const int i=int(uint32_t(handle)&0xffff);if(i>=PickupSync::MaxPickups)continue;
            mappings[i]={row.item.id,row.item.creation,handle,true};found=&mappings[i];
        }
        if(found)if(auto*pickup=Bound(*found))if(pickup->m_pObject)pickup->m_pObject->m_nObjectFlags.bDoNotRender=
            row.stage==PickupSync::Stage::Reserved&&row.collector!=CNetworkPlayerManager::m_nMyId;
    }
    receiving=saved;
}
}
bool CPickupSync::Replay(){return receiving;}
void CPickupSync::EnableNative(){nativeEnabled=true;}
void CPickupSync::Reset(){ClearMappings();view={};hasGrant=false;receipt={};lastHello=lastClaim=0;expectedHost=-1;}
void CPickupSync::HostChanged(int host){expectedHost=host;lastHello=0;}
void CPickupSync::Init(){NativeInit();Events::initScriptsEvent.before+=[]{scriptsReady=false;Reset();};
    Events::processScriptsEvent.after+=[]{if(gGameState==9)scriptsReady=true;};gameShutdownEvent.before+=[]{scriptsReady=false;Reset();};}
void CPickupSync::Receive(const Packets::Pickups::State&packet){
    if(!CNetwork::m_bAuthenticated||!packet.Valid())return;PickupSync::Actor life;if(!Life(life)||!PickupSync::SameLife(life,packet.recipient))return;
    if(packet.reset){if(packet.epoch>view.epoch)ClearMappings();if(view.Reset(packet.epoch,packet.host)&&expectedHost==-1)expectedHost=packet.host;return;}
    if(packet.epoch!=view.epoch||!view.Accept(packet.row))return;
    if(packet.row.item.owner==CNetworkPlayerManager::m_nMyId){bool found=false;
        for(auto&map:mappings)if(map.creation==packet.row.item.creation&&!map.replica){map.id=packet.row.item.id;found=Bound(map)&&Bound(map)->m_nPickupType!=PICKUP_NONE;break;}
        if(!found&&packet.row.stage==PickupSync::Stage::Active){Packets::Pickups::Action remove;remove.operation=Packets::Pickups::Operation::Remove;remove.id=packet.row.item.id;Action(remove);}
    }
}
void CPickupSync::Receive(const Packets::Pickups::Action&packet){if(!CNetwork::m_bAuthenticated||!packet.Valid()||packet.operation!=Packets::Pickups::Operation::Grant)return;
    if(hasGrant&&pendingGrant.grant!=packet.grant)return;pendingGrant=packet;hasGrant=true;}
void CPickupSync::Created(int handle,bool freshCreation){
    if(Replay()||!Host()||handle==-1)return;
    const uint32_t index=uint32_t(handle)&0xffff;if(index>=PickupSync::MaxPickups)return;auto*pickup=&CPickups::aPickUps[index];
    auto&map=mappings[index];
    // A successful native creation callback terminates any prior mapping even
    // if its new native type is unsupported. Do not expose that new type as shared.
    if(freshCreation&&map.id){Packets::Pickups::Action remove;remove.operation=Packets::Pickups::Operation::Remove;remove.id=map.id;remove.reason=PickupSync::Reason::Ambiguous;Action(remove);map={};}
    if(Mission()||CGame::currArea!=0||creation==PickupSync::MaxCounter)return;
    auto item=Metadata(pickup,creation+1);if(!item.ValidMetadata())return;
    if(!freshCreation&&map.creation&&map.handle==handle)return;
    if(map.id){Packets::Pickups::Action remove;remove.operation=Packets::Pickups::Operation::Remove;remove.id=map.id;Action(remove);}
    ++creation;map={0,creation,handle,false};Packets::Pickups::Action create;create.operation=Packets::Pickups::Operation::Create;create.item=item;Action(create);
}
void CPickupSync::Removed(CPickup*pickup){if(Replay()||!Host())return;const int i=Index(pickup);if(i<0)return;auto&map=mappings[i];
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
        if(Host()){Packets::Pickups::Action packet;packet.operation=Packets::Pickups::Operation::Remove;packet.id=map.id;packet.reason=PickupSync::Reason::Expired;Action(packet);}return false;}
    PickupSync::Actor life;if(map.id&&Life(life)&&life.area==0&&!hasGrant&&GetTickCount()-lastClaim>=500&&PickupNative::Eligible(pickup,FindPlayerPed(0),false,0)){
        const PickupSync::Row*row=nullptr;for(const auto&r:view.rows)if(r.item.id==map.id){row=&r;break;}
        if(row&&row->stage==PickupSync::Stage::Active){Packets::Pickups::Action packet;packet.operation=Packets::Pickups::Operation::Claim;packet.id=map.id;if(Action(packet))lastClaim=GetTickCount();}
    }
    return false;
}
void CPickupSync::Process(){
    if(!CNetwork::m_bAuthenticated){if(authenticated)Reset();authenticated=false;return;}if(!Ready())return;
    const uint32_t current=CNetwork::m_pPeer?CNetwork::m_pPeer->connectID:0;
    if(!authenticated||current!=connection){Reset();sequence=0;authenticated=true;connection=current;}
    PickupSync::Actor life;if(!Life(life))return;
    if(GetTickCount()-lastHello>=2000||!view.epoch){Packets::Pickups::Hello packet;packet.actor=life;const auto pos=FindPlayerPed(0)->GetPosition();
        packet.position={pos.x,pos.y,pos.z};packet.mission=Mission();GetPacketFactory().Send(packet);lastHello=GetTickCount();}
    if(!view.epoch||expectedHost!=view.host)return;
    if(Mission()||CGame::currArea!=0){RestoreHidden();ApplyGrant();return;}
    Replicas();ApplyGrant();
    if(Host())for(auto&map:mappings)if(map.id)if(auto*pickup=Bound(map))if(pickup->m_pObject){
        const PickupSync::Row*row=nullptr;for(const auto&r:view.rows)if(r.item.id==map.id){row=&r;break;}
        if(row&&(row->stage==PickupSync::Stage::Active||row->stage==PickupSync::Stage::Reserved)&&!PickupNative::Matches(pickup,row->item)){
            Packets::Pickups::Action remove;remove.operation=Packets::Pickups::Operation::Remove;remove.id=map.id;remove.reason=PickupSync::Reason::Ambiguous;
            if(Action(remove)){const bool saved=receiving;receiving=true;NativeRemove(pickup);receiving=saved;map={};}
        }
    }
    if(Host())for(auto&map:mappings)if(map.creation&&!map.id)if(auto*pickup=Bound(map))if(pickup->m_nPickupType!=PICKUP_NONE){
        auto item=Metadata(pickup,map.creation);if(item.ValidMetadata()){Packets::Pickups::Action create;create.operation=Packets::Pickups::Operation::Create;create.item=item;Action(create);}
    }
}
