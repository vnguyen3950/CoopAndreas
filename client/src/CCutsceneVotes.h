#pragma once
#include "network/packets/cutscene.h"
class CCutsceneVotes {
public:
    static void Init();
    static void Process();
    static void Draw();
    static void ObserveOpcode(uint16_t opcode, bool fromHostScript);
    static void Cancel(bool notifyHost = false);
    static void Reset();
    static void HostChanged(int hostId);
    static void Queue(Packet& packet);
    static void ReceiveBegin(const Packets::Cutscene::Begin& packet);
    static void ReceiveState(const Packets::Cutscene::Update& packet);
    static void ReceiveCommit(const Packets::Cutscene::Commit& packet);
    static bool NativeSkipQuery();
};
