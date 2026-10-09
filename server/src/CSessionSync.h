#pragma once
#include "network/packets/session.h"
class CSessionSync
{
public:
    static SessionSync::Server& Room();
    static void Join(CNetworkPlayer* player);
    static void Leave(CNetworkPlayer* player);
    static void HostChanged(CNetworkPlayer* player);
    static void Send(CNetworkPlayer* player);
    static void Broadcast();
};
