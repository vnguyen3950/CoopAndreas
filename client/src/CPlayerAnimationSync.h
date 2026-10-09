#pragma once
#include "network/packets/player_animation.h"
namespace Packets::Players { class RespawnPlayer; }
class CPlayerAnimationSync
{
public:
    static void Init();
    static void Process();
    static void Reset();
    static void ForgetPlayer(int id, uint32_t generation);
    static void Receive(const Packets::Players::PlayerAnimationState& packet);
    static void ReceiveRespawn(const Packets::Players::RespawnPlayer& packet);
    static bool PrepareRespawn(Packets::Players::RespawnPlayer& packet);
    static bool GetLocalLife(PlayerAnimation::Life& out);
    static uint32_t GetLocalBirth();
    static uint32_t GetLocalSequence();
};
