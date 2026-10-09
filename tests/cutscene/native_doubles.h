#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>
class Packet;
class CNetworkPlayer;
struct RecordedFactory {
    std::vector<std::pair<int, std::unique_ptr<Packet>>> sent;
    void RegisterPacket(Packet* packet);
    void Send(Packet& packet, CNetworkPlayer* player = nullptr);
    void SendToAll(Packet& packet, CNetworkPlayer* ignore = nullptr);
};
static RecordedFactory& GetPacketFactory() { static RecordedFactory factory; return factory; }
#include "network/packet.h"
#include "network/packets/cutscene.h"
// Cutscene fixtures exercise non-fire queue traffic; fire ordering is covered
// through unchanged actual fire services in their own suite.
struct CFireSync { static void Queue(Packet&) {} };
struct CPacketBuffer {
    std::deque<Packet*> m_packets;
    void Receive(Packet* packet);
};
inline CPacketBuffer& GetPacketBuffer() { static CPacketBuffer buffer; return buffer; }
struct HandlerDouble { void ProcessPacket(Packet*) {} };
inline HandlerDouble& GetPacketHandler() { static HandlerDouble handler; return handler; }
namespace Packets::Scripts {
// Only the legacy opcode packet's container is doubled; its packing is outside this feature.
class OpCodeSync : public Packet {
public:
    DEFINE_PACKET_TYPE(OpCodeSync, ePacketType::OPCODE_SYNC, ePacketChannel::SCRIPT);
    int size = 4;
    uint8_t buffer[32]{};
private: template<class Stream> bool Serialize(Stream&) { return false; }
};
}
namespace Config { constexpr int MAX_SERVER_PLAYERS = 8; }
static uint32_t g_serverTime = 1000;
struct ENetPeer { int state = 5; };
constexpr int ENET_PEER_STATE_CONNECTED = 5;
class CNetworkPlayer { public: ENetPeer* m_pPeer = nullptr; int m_iPlayerId = 0; bool m_bIsHost = false; };
struct CNetworkPlayerManager {
    static inline std::vector<CNetworkPlayer*> m_pPlayers;
    static CNetworkPlayer* GetPlayer(ENetPeer* peer) {
        for (auto* player : m_pPlayers) if (player->m_pPeer == peer) return player;
        return nullptr;
    }
};
inline void RecordedFactory::RegisterPacket(Packet* packet) { delete packet; }
inline void RecordedFactory::Send(Packet& packet, CNetworkPlayer* player) {
    auto copy = std::unique_ptr<Packet>(packet.Clone());
    if (!copy->serverTime) copy->serverTime = g_serverTime;
    sent.emplace_back(player ? player->m_iPlayerId : -1, std::move(copy));
}
inline void RecordedFactory::SendToAll(Packet& packet, CNetworkPlayer* ignore) {
    for (auto* player : CNetworkPlayerManager::m_pPlayers) if (player != ignore) Send(packet, player);
}
namespace Double { static bool focused = true, pressed = false; }
struct CNetwork { static inline bool m_bAuthenticated = true; };
struct CLocalPlayer { static inline bool m_bIsHost = false; };
struct COpCodeSync { static inline bool ms_bLoadingCutscene = false; };
struct CChat { static inline bool m_bInputActive = false; };
struct CCutsceneMgr {
    static inline bool dataFileLoaded = true, ms_running = false, finished = false;
    static inline unsigned ms_cutsceneLoadStatus = 1;
    static inline char ms_cutsceneName[8] = "INTRO1A";
    static bool HasCutsceneFinished() { return finished; }
};
struct CTheScripts {
    static inline unsigned OnAMissionFlag = 1;
    static inline uint8_t ScriptSpace[2] = {0, 1};
    static inline int FailCurrentMission = 0;
};
namespace plugin { template<class T, uintptr_t Address> T CallAndReturn() {
    static_assert(Address == 0x4D5D10, "Only the original native skip query is doubled");
    return T(Double::pressed || !Double::focused);
} }
namespace patch { template<class T> void RedirectCall(uintptr_t, T) {} }
struct CDXFont { static inline int m_fFontSize = 18; static void Draw(int, int, const std::string&, uint32_t) {} };
static struct { int maximumHeight = 720; } RsGlobal;
#define D3DCOLOR_ARGB(a,r,g,b) uint32_t(0xffffffff)
namespace Commands { constexpr int START_CUTSCENE = 0x02E7; }
template<int Op> void Command() {
    static_assert(Op == Commands::START_CUTSCENE, "Only deferred native START is doubled");
    CCutsceneMgr::ms_running = true;
}
static unsigned checks = 0, failures = 0;
static void expect(bool ok, const char* message) {
    ++checks; if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
