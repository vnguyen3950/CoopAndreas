#pragma once

#include "CVector.h"

#include <eModelID.h>
#include <ePedType.h>
#include <network/packets/peds.h>

class CNetworkPlayer;

class CNetworkPed
{
public:
    CNetworkPed(
        int pedid, CNetworkPlayer* syncer, eModelID modelId, ePedType pedType, CVector pos, eCharCreatedBy createdBy);

    int m_nPedId;
    CNetworkPlayer* m_pSyncer;
    eModelID m_nModelId;
    ePedType m_nPedType;
    CVector m_vecPos;
    eCharCreatedBy m_nCreatedBy;
    char m_szSpecialModelName[8]{};
    uint32_t m_generation = 0, m_ownerEpoch = 1, m_stateSequence = 0, m_requestToken = 0;
    bool m_bPinned = false;
    Packets::Peds::PedReplay m_lastState{};
    bool m_hasState = false;
    NPCSync::Stamp GetStamp() const { return {m_generation, m_ownerEpoch, m_stateSequence}; }
    bool AcceptState(const NPCSync::Stamp& stamp);
    Packets::Peds::PedSpawn SpawnPacket() const;
};
