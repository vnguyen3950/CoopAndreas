#include "stdafx.h"
#include "CPickupSync.h"
#include "CPlayerAnimationSync.h"
#include "CNetworkPedManager.h"
#include "CNetworkPed.h"

namespace {
PickupSync::Room& Room(){static PickupSync::Room room;return room;}
struct Peer {uint32_t sequence=0,copSequence=0;bool watching=false;};
std::array<Peer,PickupSync::MaxPlayers> peers{};
bool mission=false;
struct DeathOutput {
    uint32_t itemId=0,epoch=0,started=0;
    bool used=false,pending=false,spent=false,authorised=false,expired=false;
    Packets::Pickups::Action request;
};
struct CopManifest { // One immutable original-producer death, bounded native outputs.
    NPCSync::Stamp death;
    uint32_t producerGeneration=0;
    int producer=-1;
    bool authorised=false;
    std::array<DeathOutput,PickupSync::MaxDeathOutputs> outputs{};
};
std::array<CopManifest,255> copManifests{};
bool Registered(CNetworkPlayer* player){return player&&player->m_pPeer&&player->m_pPeer->state==ENET_PEER_STATE_CONNECTED
    &&player->m_iPlayerId>=0&&player->m_iPlayerId<PickupSync::MaxPlayers
    &&CNetworkPlayerManager::GetPlayer(player->m_pPeer)==player&&CNetworkPlayerManager::GetPlayer(player->m_iPlayerId)==player;}
bool Life(CNetworkPlayer* player,PickupSync::Actor& out){
    PlayerAnimation::Life life;if(!Registered(player)||!CPlayerAnimationServer::GetActorLife(player,life)||!life.ready)return false;
    out={life.generation,life.birth,life.sequence,life.model,life.area};return out.Valid();
}
void Send(CNetworkPlayer* player,const PickupSync::Row* row=nullptr){
    PickupSync::Actor actor;if(!Life(player,actor)||!Room().epoch)return;
    Packets::Pickups::State packet;packet.epoch=Room().epoch;packet.host=Room().host;packet.recipient=actor;
    if(row){packet.reset=false;packet.row=*row;}GetPacketFactory().Send(packet,player);
}
void Broadcast(const PickupSync::Row* row=nullptr){for(auto*player:CNetworkPlayerManager::m_pPlayers)if(Registered(player)&&peers[player->m_iPlayerId].watching)Send(player,row);}
void Grant(const PickupSync::Row& row,CNetworkPlayer* player){
    if(row.stage!=PickupSync::Stage::Reserved||row.collector!=player->m_iPlayerId)return;
    Packets::Pickups::Action grant;grant.operation=Packets::Pickups::Operation::Grant;
    grant.epoch=row.item.epoch;grant.sequence=row.grant;grant.id=row.item.id;grant.grant=row.grant;
    grant.actor=row.collectorLife;grant.position=row.item.position;GetPacketFactory().Send(grant,player);
}
void Replay(CNetworkPlayer* player){
    Send(player);for(const auto&row:Room().rows)if(row.item.id){Send(player,&row);Grant(row,player);}
}
bool Owner(CNetworkPlayer* sender,const PickupSync::Actor& actor){return Registered(sender)&&sender->m_bIsHost
    &&CNetworkPlayerManager::GetHost()==sender&&Room().host==sender->m_iPlayerId&&PickupSync::SameLife(actor,Room().ownerLife);}
bool SameManifest(const PickupSync::Item&a,const PickupSync::Item&b){
    return a.owner==b.owner&&a.creation==b.creation&&a.model==b.model&&a.type==b.type&&a.ammo==b.ammo&&a.remaining==b.remaining&&a.area==b.area
        &&a.position.x==b.position.x&&a.position.y==b.position.y&&a.position.z==b.position.z&&a.cop.ped==b.cop.ped
        &&PickupSync::SameSeal(a.cop.death,b.cop.death)&&a.cop.sequence==b.cop.sequence&&a.cop.producerGeneration==b.cop.producerGeneration&&a.cop.ordinal==b.cop.ordinal;
}
bool NativeDeathItem(const CNetworkPed*ped,const PickupSync::Item&item){
    if(!ped||!PickupSync::OrdinaryNPC(int(ped->m_nModelId),int(ped->m_nPedType),int(ped->m_nCreatedBy)))return false;
    if(item.type==8)return PickupSync::NativeMoneyNPC(int(ped->m_nPedType));
    return ped->m_nPedType!=PED_TYPE_COP||PickupSync::StockCopWeapon(int(ped->m_nModelId),item.model);
}
bool CopCreate(const Packets::Pickups::Action&packet,CNetworkPlayer*sender){
    const auto&item=packet.item;auto*ped=CNetworkPedManager::GetPed(item.cop.ped);
    if(!item.cop.Valid()||item.owner!=sender->m_iPlayerId||item.cop.producerGeneration!=packet.actor.generation
        ||!ped||ped->m_generation!=item.cop.death.generation||!NativeDeathItem(ped,item))return false;
    auto&entry=copManifests[item.cop.ped];auto&peer=peers[sender->m_iPlayerId];
    const bool proof=CNetworkPedManager::GetDeathProducer(sender,item.cop.ped,item.cop.death);
    // Unknown proof cannot poison a different actual producer. Once any output
    // is authorised, retain this exact seal, incarnation and every spent ordinal.
    if(entry.death.generation==item.cop.death.generation&&!entry.authorised&&proof
        &&(!PickupSync::SameSeal(entry.death,item.cop.death)||entry.producer!=sender->m_iPlayerId
            ||entry.producerGeneration!=packet.actor.generation))entry={};
    if(entry.death.generation!=item.cop.death.generation){
        if(entry.death.generation>item.cop.death.generation||item.cop.sequence<=peer.copSequence)return false;
        if(!proof&&(ped->m_deathStamp.State()||ped->m_pSyncer!=sender||ped->m_ownerEpoch!=item.cop.death.epoch))return false;
        entry={};entry.death=item.cop.death;entry.producer=sender->m_iPlayerId;entry.producerGeneration=packet.actor.generation;
    }
    if(!PickupSync::SameSeal(entry.death,item.cop.death)||entry.producer!=sender->m_iPlayerId||entry.producerGeneration!=packet.actor.generation)return false;
    auto&output=entry.outputs[item.cop.ordinal-1];
    if(output.used){
        if(!SameManifest(output.request.item,item))return false;
        if(output.spent){
            if(output.epoch==Room().epoch)if(auto*row=Room().Find(output.itemId)){peer.sequence=packet.sequence;Send(sender,row);return true;}
            return false;
        }
        if(output.expired)return false;
    }else{
        if(item.cop.sequence<=peer.copSequence)return false;
        // Two ordinal slots cannot disguise a replayed creation nonce.
        for(const auto&prior:entry.outputs)if(prior.used&&prior.request.item.creation==item.creation)return false;
        output.used=true;output.pending=true;output.started=enet_time_get();
    }
    output.request=packet;peer.sequence=packet.sequence;CPickupServer::ProcessPending();return true;
}
}
void CPickupServer::ProcessPending(){
    struct Pending {int ped,ordinal;};
    std::array<Pending,255*PickupSync::MaxDeathOutputs> order{};size_t count=0;
    for(int i=0;i<255;++i)for(int j=0;j<PickupSync::MaxDeathOutputs;++j)if(copManifests[i].outputs[j].pending)order[count++]={i,j};
    std::sort(order.begin(),order.begin()+count,[](const Pending&a,const Pending&b){
        const auto&x=copManifests[a.ped];const auto&y=copManifests[b.ped];
        return x.producer!=y.producer?x.producer<y.producer:x.outputs[a.ordinal].request.item.cop.sequence<y.outputs[b.ordinal].request.item.cop.sequence;
    });
    for(size_t index=0;index<count;++index){auto&entry=copManifests[order[index].ped];auto&output=entry.outputs[order[index].ordinal];
        auto*sender=CNetworkPlayerManager::GetPlayer(entry.producer);const auto&packet=output.request;const auto&item=packet.item;
        auto*ped=CNetworkPedManager::GetPed(item.cop.ped);
        if(!Registered(sender)||sender->m_vitals.generation!=entry.producerGeneration||packet.epoch!=Room().epoch
            ||!ped||ped->m_generation!=entry.death.generation||enet_time_get()-output.started>15000){output.pending=false;output.expired=true;continue;}
        if(!CNetworkPedManager::GetDeathProducer(sender,item.cop.ped,entry.death))continue;
        PickupSync::Actor life;if(!Life(sender,life)||!PickupSync::CurrentActor(packet.actor,life))continue;
        const auto&pos=ped->m_deathPosition;const auto&drop=item.position;
        const double dx=double(pos.x)-drop.x,dy=double(pos.y)-drop.y,dz=double(pos.z)-drop.z;
        // Original CreateSomeMoney cumulatively scatters seven native wads.
        // Do not recenter them or reconstruct their values. Admit only bounded
        // exterior ground drops. Ground projection may legitimately change Z;
        // finite world bounds remain enforced, without an invented Z-distance limit.
        const bool withinDeathRange=item.type==8?dx*dx+dy*dy<=225.0:dx*dx+dy*dy+dz*dz<=25.0;
        if(mission||packet.mission||life.area!=0||ped->m_deathArea!=0
            ||ped->m_deathProducerGeneration!=entry.producerGeneration||!PickupSync::SameSeal(ped->m_deathStamp,entry.death)
            ||!NativeDeathItem(ped,item)||!NPCSync::Position(pos)||!withinDeathRange){output.pending=false;output.expired=true;continue;}
        auto&peer=peers[entry.producer];
        if(!output.authorised&&item.cop.sequence<=peer.copSequence){output.pending=false;output.expired=true;continue;}
        output.authorised=entry.authorised=true;peer.copSequence=(std::max)(peer.copSequence,item.cop.sequence);
        auto*row=Room().CreateCopDrop(entry.producer,packet.epoch,life,item);if(!row)continue;
        output.pending=false;output.spent=true;output.itemId=row->item.id;output.epoch=Room().epoch;Broadcast(row);
    }
}
void CPickupServer::Join(CNetworkPlayer* player){if(Registered(player))peers[player->m_iPlayerId]={};}
void CPickupServer::HostChanged(CNetworkPlayer* player){
    mission=false;PickupSync::Actor life;
    if(player&&!Life(player,life)){Room().ChangeHost(-1,{});Broadcast();return;}
    if(Room().ChangeHost(player?player->m_iPlayerId:-1,life))Broadcast();
}
void CPickupServer::Leave(CNetworkPlayer* player){
    if(!player||player->m_iPlayerId<0||player->m_iPlayerId>=PickupSync::MaxPlayers)return;
    Room().RetireCollector(player->m_iPlayerId);Broadcast();
    for(auto&row:Room().rows)if(row.item.cop.Present()&&row.item.owner==player->m_iPlayerId
        &&row.item.cop.producerGeneration==player->m_vitals.generation&&(row.stage==PickupSync::Stage::Active||row.stage==PickupSync::Stage::Reserved)){
        Room().Remove(Room().host,Room().epoch,row.item.id,PickupSync::Reason::OwnerLeft);
    }
    for(const auto&row:Room().rows)if(row.item.id&&row.stage==PickupSync::Stage::Removed)Broadcast(&row);
    peers[player->m_iPlayerId]={};if(player->m_iPlayerId==Room().host)HostChanged(nullptr);
}
void CPickupServer::Mission(CNetworkPlayer* player,bool active){
    if(!Registered(player)||!player->m_bIsHost||CNetworkPlayerManager::GetHost()!=player)return;
    if(mission==active)return;mission=active;
    // Host pickups remain native during mission suspension. This is lifecycle
    // teardown, not native collection and not a mission-objective receipt.
    PickupSync::Actor life;if(Life(player,life)){Room().ChangeHost(-1,{});Room().ChangeHost(player->m_iPlayerId,life);Broadcast();}
}
void CPickupServer::Hello(const Packets::Pickups::Hello& packet,CNetworkPlayer* sender){
    ProcessPending();
    PickupSync::Actor life;if(!packet.Valid()||!Life(sender,life)||!PickupSync::CurrentActor(packet.actor,life))return;
    peers[sender->m_iPlayerId].watching=true;
    if(sender->m_bIsHost&&CNetworkPlayerManager::GetHost()==sender){
        if(!PickupSync::SameLife(Room().ownerLife,life)||Room().host!=sender->m_iPlayerId){Room().ChangeHost(sender->m_iPlayerId,life);Broadcast();}
        Room().ownerLife=life;
    }
    Replay(sender);
}
bool CPickupServer::Action(const Packets::Pickups::Action& packet,CNetworkPlayer* sender){
    ProcessPending();
    if(!Registered(sender)||!packet.Valid()||packet.operation==Packets::Pickups::Operation::Grant||packet.epoch!=Room().epoch)return false;
    auto&peer=peers[sender->m_iPlayerId];
    if(packet.operation!=Packets::Pickups::Operation::Result&&packet.sequence<=peer.sequence)return false;
    if(packet.operation==Packets::Pickups::Operation::Result){
        // A native outcome can arrive after its actor boundary. It is accepted
        // only for the exact reserved old life on this still-connected peer.
        if(packet.actor.generation!=sender->m_vitals.generation)return false;
        const auto*old=Room().Find(packet.id);
        // Exact terminal duplicates acknowledge retained state only. Removed
        // outcomes are as terminal as collection; neither may issue a new grant.
        if(old&&!old->awaitingOutcome&&(old->stage==PickupSync::Stage::Collected||old->stage==PickupSync::Stage::Removed)
            &&old->collector==sender->m_iPlayerId&&old->grant==packet.grant&&PickupSync::SameLife(packet.actor,old->collectorLife)){
            Send(sender,old);return true;
        }
        if(!Room().Complete(sender->m_iPlayerId,packet.epoch,packet.id,packet.grant,packet.actor,packet.outcome))return false;
        Broadcast(Room().Find(packet.id));return true;
    }
    PickupSync::Actor life;if(!Life(sender,life)||!PickupSync::CurrentActor(packet.actor,life))return false;
    if(packet.operation==Packets::Pickups::Operation::Replay){peer.sequence=packet.sequence;Replay(sender);return true;}
    if(packet.operation==Packets::Pickups::Operation::Create){
        if(packet.item.cop.Present()){
            if(mission||packet.mission||life.area!=0)return false;
            return CopCreate(packet,sender);
        }
        if(!Owner(sender,life)||mission||packet.mission||life.area!=0)return false;
        Room().ownerLife=life;auto*row=Room().Create(sender->m_iPlayerId,packet.epoch,life,packet.item);if(!row)return false;
        peer.sequence=packet.sequence;Broadcast(row);return true;
    }
    if(packet.operation==Packets::Pickups::Operation::Remove){
        auto*row=Room().Find(packet.id);
        const bool copOwner=row&&row->item.cop.Present()&&row->item.owner==sender->m_iPlayerId&&row->item.cop.producerGeneration==life.generation;
        if((!Owner(sender,life)&&!copOwner)||!Room().Remove(Room().host,packet.epoch,packet.id,packet.reason))return false;
        peer.sequence=packet.sequence;Broadcast(Room().Find(packet.id));return true;
    }
    if(packet.operation==Packets::Pickups::Operation::Claim){
        if(mission||packet.mission||life.area!=0||packet.inVehicle||sender->m_nVehicleId!=-1)return false;
        auto*row=Room().Reserve(sender->m_iPlayerId,packet.epoch,packet.id,life,packet.position,false,false);if(!row)return false;
        peer.sequence=packet.sequence;Broadcast(row);Grant(*row,sender);return true;
    }
    return false;
}
