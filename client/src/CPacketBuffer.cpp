#include "CServerTime.h"
#include "network/packet.h"
#include "network/packet_types.h"
#include "network/packets/system.h"
#include "stdafx.h"
#include "CFireSync.h"
#include "CTrailerSync.h"
#include "CPacketBuffer.h"
#include "CCutsceneVotes.h"

void CPacketBuffer::Receive(Packet* pPacket)
{
    CCutsceneVotes::Queue(*pPacket);
    CFireSync::Queue(*pPacket);
    CTrailerSync::Queue(*pPacket);
    if (pPacket->GetChannel() == ePacketChannel::SYSTEM)
    {
        GetPacketHandler().ProcessPacket(pPacket);
        delete pPacket;
        return;
    }

    if (m_packets.empty() || pPacket->serverTime >= m_packets.back()->serverTime)
    {
        m_packets.push_back(pPacket);
        return;
    }

    auto it = std::upper_bound(m_packets.begin(), m_packets.end(), pPacket->serverTime,
        [](server_time_t time, const Packet* snapshot) { return time < snapshot->serverTime; });

    m_packets.insert(it, pPacket);
}

void CPacketBuffer::Process()
{
    uint32_t renderTime = GetRenderTime(g_serverTime);

    while (!m_packets.empty())
    {
        Packet* pPacket = m_packets.front();
        if (pPacket->serverTime > renderTime)
        {
            break;
        }

        // A handler can disconnect and clear the remaining queue. Remove this
        // packet first and retain no iterator across the callback.
        m_packets.pop_front();

        GetPacketHandler().ProcessPacket(pPacket);

        SPacketRecord packetRecord{};
        packetRecord.m_nSize = pPacket->GetBytesRead();
        packetRecord.m_bInbound = true;
        packetRecord.m_bOutbound = false;
        packetRecord.m_packetType = pPacket->GetType();
        packetRecord.m_serverTime = pPacket->serverTime;
        packetRecord.m_sContent = pPacket->ToString();
        GetPacketFactory().AddPacketRecord(packetRecord, renderTime);

        delete pPacket;
    }
}

void CPacketBuffer::Clear()
{
    while (!m_packets.empty())
    {
        Packet* packet = m_packets.front();
        m_packets.pop_front();
        delete packet;
    }
}
