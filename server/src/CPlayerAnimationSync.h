#pragma once
#include "network/packets/player_animation.h"
namespace Packets::Players { class RespawnPlayer; }
class CNetworkPlayer;
class CPlayerAnimationServer
{
public:
    static bool Receive(Packets::Players::PlayerAnimationState packet, CNetworkPlayer* sender);
    static bool Respawn(Packets::Players::RespawnPlayer packet, CNetworkPlayer* sender);
    static void Replay(CNetworkPlayer* owner, CNetworkPlayer* recipient);
    static bool GetActorLife(const CNetworkPlayer* player, PlayerAnimation::Life& out);
};
