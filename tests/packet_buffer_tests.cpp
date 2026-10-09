#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using server_time_t = uint32_t;
enum class ePacketChannel { SYSTEM, SYNC, EVENT, SCRIPT };
enum class ePacketType { TEST };
static unsigned destroyed = 0, checks = 0, failures = 0;
struct Packet {
    server_time_t serverTime;
    int id;
    ePacketChannel channel;
    Packet(int value, uint32_t time, ePacketChannel ch = ePacketChannel::SCRIPT)
        : serverTime(time), id(value), channel(ch) {}
    ~Packet() { ++destroyed; }
    ePacketChannel GetChannel() const { return channel; }
    size_t GetBytesRead() const { return 1; }
    ePacketType GetType() const { return ePacketType::TEST; }
    std::string ToString() const { return std::to_string(id); }
};
struct SPacketRecord {
    size_t m_nSize = 0;
    bool m_bInbound = false, m_bOutbound = false;
    ePacketType m_packetType{};
    server_time_t m_serverTime = 0;
    std::string m_sContent;
};
struct Handler {
    std::vector<int> received;
    std::function<void(Packet*)> callback;
    void ProcessPacket(Packet* packet) {
        received.push_back(packet->id);
        if (callback) callback(packet);
    }
};
struct Factory {
    unsigned records = 0;
    void AddPacketRecord(const SPacketRecord&, uint32_t) { ++records; }
};
static Handler handler;
static Factory factory;
static Handler& GetPacketHandler() { return handler; }
static Factory& GetPacketFactory() { return factory; }
static server_time_t g_serverTime = 100;
// Non-cutscene packets have no vote queue adjustment; voting has its own suites.
struct CCutsceneVotes { static void Queue(Packet&) {} };
// This suite's non-fire packets do not need the separately tested fire ordering.
struct CFireSync { static void Queue(Packet&) {} };
#include "extracted_packet_buffer.inc"

static void expect(bool value, const char* description) {
    ++checks;
    if (!value) { ++failures; std::cout << "FAIL: " << description << '\n'; }
}
int main() {
    CPacketBuffer buffer(10);
    buffer.Receive(new Packet(1, 80));
    buffer.Receive(new Packet(2, 100));
    buffer.Receive(new Packet(3, 70));
    buffer.Process();
    expect(handler.received == std::vector<int>{3,1}, "Timestamp order remains intact.");
    expect(buffer.m_packets.size() == 1 && destroyed == 2, "Future packet remains buffered.");
    buffer.Clear();
    expect(buffer.m_packets.empty() && destroyed == 3, "Disconnect clear deletes pending old-session packets.");
    buffer.Clear();
    expect(destroyed == 3, "Repeated clear does not double-delete.");

    handler.received.clear();
    buffer.Receive(new Packet(4, 50));
    buffer.Receive(new Packet(5, 60));
    handler.callback = [&](Packet* packet) { if (packet->id == 4) buffer.Clear(); };
    buffer.Process();
    expect(handler.received == std::vector<int>{4}, "Disconnect during dispatch prevents later old-session handlers.");
    expect(buffer.m_packets.empty() && destroyed == 5, "Dispatch-time clear preserves current packet ownership.");
    expect(factory.records == 3, "The dispatched packet is still recorded exactly once.");

    handler.callback = {};
    buffer.Receive(new Packet(6, 40));
    buffer.Process();
    expect(handler.received == std::vector<int>{4,6} && destroyed == 6, "New-session packets remain usable after clear.");
    buffer.Receive(new Packet(7, 900, ePacketChannel::SYSTEM));
    expect(handler.received.back() == 7 && destroyed == 7 && buffer.m_packets.empty(),
        "SYSTEM lifecycle packets still dispatch immediately.");
    std::cout << checks << " assertions, " << failures << " failures\n";
    return failures ? 1 : 0;
}
