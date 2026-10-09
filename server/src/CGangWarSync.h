#pragma once
#include "network/packets/gang_wars.h"
class CNetworkPlayer;
class CGangWarServer
{
public:
    static GangWarSync::Room& Room();
    static void Join(CNetworkPlayer* player);
    static void Leave(CNetworkPlayer* player);
    static void HostChanged(CNetworkPlayer* host);
    static void Send(CNetworkPlayer* player);
    static void Broadcast();
    static bool Receive(const Packets::Gangs::State& packet,CNetworkPlayer* sender);
    static bool Receive(const Packets::Gangs::Territory& packet,CNetworkPlayer* sender);
};
