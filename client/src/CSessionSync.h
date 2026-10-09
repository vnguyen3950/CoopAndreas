#pragma once
#include "network/packets/session.h"
class CSessionSync
{
public:
    static void Init();
    static void Process();
    static void HandleState(const Packets::Session::Update& packet);
    static void HandleAction(const Packets::Session::CheatAction& packet);
    static bool NeedsOpcodeCapture(uint16_t opcode);
    static bool ConsumeOpcode(uint16_t opcode, const int* params, int count);
    static void OnLocalCheat(int id);
};
