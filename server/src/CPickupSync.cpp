#include "stdafx.h"
#include "CPickupSync.h"
#include "CPlayerAnimationSync.h"

namespace {
PickupSync::Room& Room(){static PickupSync::Room room;return room;}
struct Peer {uint32_t sequence=0;bool watching=false;};
std::array<Peer,PickupSync::MaxPlayers> peers{};
bool mission=false;
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
    PickupSync::Actor life;if(!packet.Valid()||!Life(sender,life)||!PickupSync::CurrentActor(packet.actor,life))return;
    peers[sender->m_iPlayerId].watching=true;
    if(sender->m_bIsHost&&CNetworkPlayerManager::GetHost()==sender){
        if(!PickupSync::SameLife(Room().ownerLife,life)||Room().host!=sender->m_iPlayerId){Room().ChangeHost(sender->m_iPlayerId,life);Broadcast();}
        Room().ownerLife=life;
    }
    Replay(sender);
}
bool CPickupServer::Action(const Packets::Pickups::Action& packet,CNetworkPlayer* sender){
    if(!Registered(sender)||!packet.Valid()||packet.operation==Packets::Pickups::Operation::Grant||packet.epoch!=Room().epoch)return false;
    auto&peer=peers[sender->m_iPlayerId];
    if(packet.operation!=Packets::Pickups::Operation::Result&&packet.sequence<=peer.sequence)return false;
    if(packet.operation==Packets::Pickups::Operation::Result){
        // A native outcome can arrive after its actor boundary. It is accepted
        // only for the exact reserved old life on this still-connected peer.
        if(packet.actor.generation!=sender->m_vitals.generation)return false;
        const auto*old=Room().Find(packet.id);
        if(old&&old->stage==PickupSync::Stage::Collected&&old->collector==sender->m_iPlayerId&&old->grant==packet.grant){Send(sender,old);return true;}
        if(!Room().Complete(sender->m_iPlayerId,packet.epoch,packet.id,packet.grant,packet.actor,packet.outcome))return false;
        Broadcast(Room().Find(packet.id));return true;
    }
    PickupSync::Actor life;if(!Life(sender,life)||!PickupSync::CurrentActor(packet.actor,life))return false;
    if(packet.operation==Packets::Pickups::Operation::Replay){peer.sequence=packet.sequence;Replay(sender);return true;}
    if(packet.operation==Packets::Pickups::Operation::Create){
        if(!Owner(sender,life)||mission||packet.mission||life.area!=0)return false;
        Room().ownerLife=life;auto*row=Room().Create(sender->m_iPlayerId,packet.epoch,life,packet.item);if(!row)return false;
        peer.sequence=packet.sequence;Broadcast(row);return true;
    }
    if(packet.operation==Packets::Pickups::Operation::Remove){
        if(!Owner(sender,life)||!Room().Remove(sender->m_iPlayerId,packet.epoch,packet.id,packet.reason))return false;
        peer.sequence=packet.sequence;Broadcast(Room().Find(packet.id));return true;
    }
    if(packet.operation==Packets::Pickups::Operation::Claim){
        if(mission||packet.mission||life.area!=0||packet.inVehicle||sender->m_nVehicleId!=-1)return false;
        auto*row=Room().Reserve(sender->m_iPlayerId,packet.epoch,packet.id,life,packet.position,false,false);if(!row)return false;
        peer.sequence=packet.sequence;Broadcast(row);Grant(*row,sender);return true;
    }
    return false;
}
