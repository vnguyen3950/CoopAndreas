#include "stdafx.h"

CNetworkPed::CNetworkPed(
    int pedid, CNetworkPlayer* syncer, eModelID modelId, ePedType pedType, CVector pos, eCharCreatedBy createdBy)
{
    m_nPedId = pedid;
    m_pSyncer = syncer;
    m_nModelId = modelId;
    m_nPedType = pedType;
    m_vecPos = pos;
    m_nCreatedBy = createdBy;
}

bool CNetworkPed::AcceptState(const NPCSync::Stamp& stamp)
{
    if (!stamp.Newer(GetStamp())) return false;
    m_stateSequence = stamp.sequence;
    return true;
}
Packets::Peds::PedSpawn CNetworkPed::SpawnPacket() const
{
    Packets::Peds::PedSpawn packet;
    packet.pedid = m_nPedId; packet.modelId = m_nModelId; packet.pedType = m_nPedType;
    packet.pos = m_vecPos; packet.createdBy = m_nCreatedBy; packet.stamp = GetStamp();
    packet.ownerid = m_pSyncer ? m_pSyncer->m_iPlayerId : -1;
    std::memcpy(packet.specialModelName, m_szSpecialModelName, sizeof packet.specialModelName);
    return packet;
}
