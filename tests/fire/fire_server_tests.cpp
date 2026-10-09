#include "server_doubles.h"
#include "server/src/CFireSync.h"
#include "extracted_server.inc"
static void Hello(CNetworkPlayer& peer,uint32_t game,bool restart=false) {
    Packets::Fires::Hello h;h.nativeReference=100;h.gameGeneration=game;h.model=0;h.controllingRestart=restart;CFireSync::Hello(h,&peer);
}
int main(){
    ENetPeer hp,gp;CNetworkPlayer host,guest;host.m_pPeer=&hp;host.m_iPlayerId=0;host.m_bIsHost=true;guest.m_pPeer=&gp;guest.m_iPlayerId=1;
    CNetworkPlayerManager::m_pPlayers={&host,&guest};CFireSync::Join(&host);CFireSync::Join(&guest);CFireSync::HostChanged(&host);
    Hello(host,1);Hello(guest,1);
    auto epoch=Room().epoch;
    Packets::Fires::Update u;u.state.key={epoch,1,1,40};u.state.strength=1;u.state.remaining=7000;
    CFireSync::Update(u,&guest);expect(!Room().slots[0].live,"Guest cannot publish world fire state");
    CFireSync::Update(u,&host);expect(Room().slots[0].live,"Authenticated host publishes canonical fire");
    auto oldPlayer=players[1];Hello(guest,2);
    const Packets::Fires::Reset* receipt=nullptr;
    for (const auto& sent : GetPacketFactory().sent) if (sent.first==1 && sent.second->GetType()==ePacketType::FIRE_RESET)
        receipt=static_cast<const Packets::Fires::Reset*>(sent.second.get());
    expect(receipt && receipt->recipientBirth==players[1].generation && receipt->gameGeneration==2,
        "Actual server reset acknowledges exact new recipient birth and script generation");
    expect(receipt && receipt->recipientBirth!=oldPlayer.generation && receipt->connection==players[1].ownerEpoch,
        "Repeated native reference cannot reuse old birth receipt or mismatched connection");
    expect(Room().epoch==epoch && Room().slots[0].live,"Guest script restart preserves host's world burns");
    expect(players[1].generation>oldPlayer.generation,"Guest restart advances attachment incarnation despite repeated pool reference/model");
    auto oldHost=players[0];Hello(host,2,true);
    expect(Room().epoch>epoch && !Room().slots[0].live,"Deliberate controlling-host load invalidates old room burns");
    expect(players[0].generation>oldHost.generation,"Host unchanged pool reference is a new attachment incarnation");
    u.state.key={Room().epoch,1,1,1};CFireSync::Update(u,&host);
    expect(Room().slots[0].live && Room().slots[0].state.key.sequence==1,"New epoch accepts reset native slot generation/sequence after old high-water 40");
    Hello(host,1,true);expect(Room().slots[0].live,"Stale old game HELLO cannot reset current campaign");
    auto beforePromotion=Room().epoch;guest.m_bIsHost=true;host.m_bIsHost=false;CFireSync::HostChanged(&guest);
    expect(Room().epoch>beforePromotion,"Host migration advances fire epoch");
    auto migrated=Room().epoch;Hello(guest,2,true);
    expect(Room().epoch==migrated,"Same initialized game after promotion is not a deliberate campaign load");
    CFireSync::Update(u,&host);expect(!Room().slots[0].live,"Old host cannot publish after migration");
    guest.m_bIsHost=false;host.m_bIsHost=true;CFireSync::HostChanged(&host);
    epoch=Room().epoch;u.state.key={epoch,1,1,1};CFireSync::Update(u,&host);
    Packets::Fires::Request r;r.request.epoch=epoch;r.request.connection=peers[1].connection;r.request.sequence=1;
    r.request.gameGeneration=peers[1].gameGeneration;r.request.intent=FireSync::Intent::Water;r.request.fire=u.state.key;
    r.request.issuer=players[1];r.request.radius=2;r.request.water=1;
    r.serverTime=0x7fffffff;GetPacketFactory().sent.clear();CFireSync::Request(r,&guest);
    expect(GetPacketFactory().sent.size()==1,"Authenticated bounded extinguishing intent delegated to host");
    expect(GetPacketFactory().sent[0].second->serverTime==g_serverTime,"Actual delegated request overwrites untrusted far-future framing timestamp");
    CFireSync::Request(r,&guest);expect(GetPacketFactory().sent.size()==1,"Duplicate intent cannot extinguish twice");
    ++r.request.sequence;r.request.epoch=epoch-1;CFireSync::Request(r,&guest);
    expect(GetPacketFactory().sent.size()==1,"Stale old-host epoch intent rejected");
    r.request.epoch=epoch;r.request.gameGeneration--;CFireSync::Request(r,&guest);
    expect(GetPacketFactory().sent.size()==1,"Old guest-game intent rejected after restart");
    r.request.gameGeneration=peers[1].gameGeneration;r.request.issuer=oldPlayer;CFireSync::Request(r,&guest);
    expect(GetPacketFactory().sent.size()==1,"Old avatar attachment cannot issue valid new-game intent");
    CNetworkPed npc;npc.m_pSyncer=&guest;npc.m_generation=4;npc.m_ownerEpoch=2;CNetworkPedManager::peds={&npc};
    u.state.key.sequence=2;u.state.target={FireSync::Kind::Ped,0,4,2,0,105};CFireSync::Update(u,&host);
    expect(Room().slots[0].state.target.owner==1,"Server derives actual NPC owner rather than trusting sender's owner tag");
    u.state.key.sequence=3;u.state.target.generation=3;CFireSync::Update(u,&host);
    expect(Room().slots[0].state.key.sequence==2,"Reused/stale NPC generation cannot replace live attachment");
    ENetPeer op;CNetworkPlayer other;other.m_pPeer=&op;other.m_iPlayerId=2;
    CNetworkPlayerManager::m_pPlayers.push_back(&other);CFireSync::Join(&other);Hello(other,1);
    CNetworkVehicle car;car.m_nVehicleId=4;car.m_pPlayers[0]=&guest;car.m_pSyncer=&guest;
    CNetworkVehicleManager::m_pVehicles={&car};CFireSync::VehicleChanged(&car);
    Packets::Fires::Update burn;burn.state.key={epoch,2,1,1};burn.state.target=vehicles[4];CFireSync::Update(burn,&host);
    auto retained=Room().slots[1].state.target;
    car.m_pPlayers[0]=&other;car.m_pSyncer=&other;CFireSync::VehicleChanged(&car);
    expect(retained.owner==1 && vehicles[4].owner==2 && vehicles[4].ownerEpoch>retained.ownerEpoch,
        "Driver transfer advances current lease while retained burn still names old owner");
    Packets::Fires::Request stop;stop.request.epoch=epoch;stop.request.connection=peers[1].connection;
    stop.request.gameGeneration=peers[1].gameGeneration;stop.request.sequence=peers[1].sequence+1;
    stop.request.intent=FireSync::Intent::Stop;stop.request.fire=burn.state.key;stop.request.issuer=players[1];
    GetPacketFactory().sent.clear();CFireSync::Request(stop,&guest);
    expect(GetPacketFactory().sent.empty(),"Former owner cannot stop a burn using its retained stale target lease");
    burn.state.key.sequence=2;burn.state.target=vehicles[4];CFireSync::Update(burn,&host);
    stop.request.connection=peers[2].connection;stop.request.gameGeneration=peers[2].gameGeneration;
    stop.request.sequence=1;stop.request.issuer=players[2];GetPacketFactory().sent.clear();CFireSync::Request(stop,&other);
    expect(GetPacketFactory().sent.size()==1,"Current owner can stop burn after canonical target lease refresh");
    std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;
}
