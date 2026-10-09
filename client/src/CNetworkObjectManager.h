#pragma once
#include "network/object_sync.h"
#include "network/packets/objects.h"

class CNetworkObjectManager
{
public:
    static void Init();
    static void Process();
    static void ObserveOpcode(uint16_t opcode, const int* inputs, int count, int resultHandle);
    static void BeforeDelete(CObject* object);
    static int GetHostToken(int poolRef);
    static int GetHandle(uint32_t networkId);
    static int GetNetworkId(CEntity* object);
    static void Confirm(uint32_t token, uint32_t networkId);
    static void ReceiveCreate(const Packets::Objects::Create& packet);
    static void ReceiveUpdate(const Packets::Objects::Update& packet);
    static void ReceiveRemove(uint32_t networkId);
    static bool QueueOpcode(const uint8_t* buffer, int size);
    static void ReceiveHit(const Packets::Objects::Hit& packet);
    static void Clear();
};
