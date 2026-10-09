#include "stdafx.h"
#include "CGangWarSync.h"

GangWarSync::Room& CGangWarServer::Room(){static GangWarSync::Room room;return room;}
namespace
{
bool Registered(CNetworkPlayer* player)
{
    return player&&player->m_pPeer&&CNetworkPlayerManager::GetPlayer(player->m_pPeer)==player
        &&CNetworkPlayerManager::GetPlayer(player->m_iPlayerId)==player;
}
}
void CGangWarServer::Send(CNetworkPlayer* player)
{
    if(!Registered(player)||!Room().state.Valid())return;
    Packets::Gangs::State state;state.kind=GangWarSync::Kind::Snapshot;state.authority=Room().state;
    GetPacketFactory().Send(state,player);
    if(Room().state.ready)
    {
        Packets::Gangs::Territory territory;territory.kind=GangWarSync::Kind::Snapshot;
        territory.epoch=Room().state.epoch;territory.campaign=Room().state.campaign;
        territory.revision=Room().state.revision;territory.world=Room().world;
        GetPacketFactory().Send(territory,player);
    }
}
void CGangWarServer::Broadcast(){for(auto* player:CNetworkPlayerManager::m_pPlayers)Send(player);}
void CGangWarServer::HostChanged(CNetworkPlayer* host)
{
    if(!host||!Registered(host)||!host->m_bIsHost)return;
    if(Room().SetHost(host->m_iPlayerId,host->m_vitals.generation))Broadcast();
}
void CGangWarServer::Join(CNetworkPlayer* player)
{
    auto* host=CNetworkPlayerManager::GetHost();
    if(host&&(Room().state.host!=host->m_iPlayerId||Room().hostConnection!=host->m_vitals.generation))HostChanged(host);
    Send(player);
}
void CGangWarServer::Leave(CNetworkPlayer* player)
{
    if(!player)return;
    if(CNetworkPlayerManager::m_pPlayers.size()==1){Room().EmptyRoom();return;}
    if(player->m_iPlayerId==Room().state.host){Room().SetHost(-1,0);Broadcast();}
}
bool CGangWarServer::Receive(const Packets::Gangs::State& packet,CNetworkPlayer* sender)
{
    if(!Registered(sender)||!packet.Valid())return false;
    if(packet.kind==GangWarSync::Kind::Request){Send(sender);return true;}
    if(packet.kind!=GangWarSync::Kind::Publish||!sender->m_bIsHost
        ||CNetworkPlayerManager::GetHost()!=sender)return false;
    if(!Room().PublishWar(sender->m_iPlayerId,packet.authority.epoch,packet.authority.campaign,packet.sequence,packet.authority.war))
    {Send(sender);return false;}
    Broadcast();return true;
}
bool CGangWarServer::Receive(const Packets::Gangs::Territory& packet,CNetworkPlayer* sender)
{
    if(!Registered(sender)||!packet.Valid())return false;
    if(packet.kind==GangWarSync::Kind::Request){Send(sender);return true;}
    if(packet.kind!=GangWarSync::Kind::Publish||!sender->m_bIsHost
        ||CNetworkPlayerManager::GetHost()!=sender)return false;
    if(!Room().PublishWorld(sender->m_iPlayerId,packet.epoch,packet.campaign,packet.sequence,packet.reset,packet.world))
    {Send(sender);return false;}
    Broadcast();return true;
}
