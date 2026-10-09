#pragma once
#include "network/packets/gang_wars.h"
class CGangWarSync
{
public:
    static void Init();
    static void Process();
    static void NativeUpdate();
    static void Receive(const Packets::Gangs::State& packet);
    static void Receive(const Packets::Gangs::Territory& packet);
};
