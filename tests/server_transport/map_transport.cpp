// Real ENet clients talk to the unmodified production server executable.
// Native gameplay is not exercised. Complete production packets are frozen by
// the runner; unsupported packet types are ignored, never emulated.
#include <winsock2.h>
#include <array>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "enet/enet.h"
class Packet;
struct Factory { std::unordered_map<int,std::unique_ptr<Packet>> packets; void RegisterPacket(Packet*); };
Factory& GetPacketFactory();
#include "network/packets/map.h"
#include "network/packets/vitals.h"
#include "network/packets/player_animation.h"
#include "network/packets/pickups.h"
#include "network/packets/session.h"
#include "semver.h"
#include "sender.inc"
#include "network/npc_sync.h"
struct CVector { float x = 0, y = 0, z = 0; CVector(float a = 0, float b = 0, float c = 0) : x(a), y(b), z(c) {} };
#define ARRAY_SIZE(value) (sizeof(value) / sizeof((value)[0]))
#include "peds.inc"
struct CVector2D { float x = 0, y = 0; CVector2D(float a = 0, float b = 0) : x(a), y(b) {} };
#include "waypoint.inc"
#include "respawn.inc"
#include "system.inc"
Factory& GetPacketFactory() { static Factory factory; return factory; }
void Factory::RegisterPacket(Packet* p) { packets[int(p->GetType())].reset(p); }
static unsigned checks = 0, failures = 0;
void expect(bool value, const char* text) { ++checks; if (!value) { ++failures; std::cout << "FAIL: " << text << '\n'; } }
struct Client
{
    ENetHost* host = nullptr; ENetPeer* peer = nullptr; bool connected = false;
    int id = -1, hostId = -1;
    std::vector<Packets::Map::Discovery> maps;
    std::vector<Packets::Players::PlayerPlaceWaypoint> waypoints;
    std::vector<Packets::Players::PlayerAnimationState> animations;
    std::vector<Packets::Players::RespawnPlayer> respawns;
    std::vector<Packets::Pickups::State> pickups;
    std::vector<Packets::Pickups::Action> pickupActions;
    std::vector<Packets::Session::Update> sessions;
    std::vector<Packets::Peds::PedSpawn> pedSpawns;
    std::vector<Packets::Peds::PedConfirm> pedConfirms;
    std::vector<Packets::Peds::PedDeath> pedDeaths;
    std::vector<Packets::Peds::PedRemove> pedRemovals;
    std::array<uint32_t,8> generations{};
    void Process()
    {
        ENetEvent event;
        while (enet_host_service(host,&event,0) > 0)
        {
            if (event.type == ENET_EVENT_TYPE_CONNECT) connected = true;
            if (event.type == ENET_EVENT_TYPE_DISCONNECT) connected = false;
            if (event.type != ENET_EVENT_TYPE_RECEIVE) continue;
            serialize::ReadStream stream(event.packet->data,int(event.packet->dataLength)); uint32_t type = 0;
            if (stream.SerializeBits(type,16))
            {
                auto found = GetPacketFactory().packets.find(int(type));
                if (found != GetPacketFactory().packets.end())
                {
                    std::unique_ptr<Packet> packet(found->second->Clone()); uint32_t time = 0;
                    bool framed = packet->GetChannel() == ePacketChannel::SYSTEM || stream.SerializeBits(time,32);
                    if (framed && packet->SerializeRead(stream))
                    {
                        packet->serverTime = time;
                        if (auto* p = dynamic_cast<Packets::System::PlayerHandshake*>(packet.get())) id = p->yourid;
                        else if (auto* p = dynamic_cast<Packets::System::PlayerAssignHost*>(packet.get())) hostId = p->playerid;
                        else if (auto* p = dynamic_cast<Packets::Map::Discovery*>(packet.get())) maps.push_back(*p);
                        else if (auto* p = dynamic_cast<Packets::Players::PlayerPlaceWaypoint*>(packet.get())) waypoints.push_back(*p);
                        else if (auto* p = dynamic_cast<Packets::Players::Vitals*>(packet.get())) generations[p->playerid] = p->generation;
                        else if (auto* p = dynamic_cast<Packets::Players::PlayerAnimationState*>(packet.get())) animations.push_back(*p);
                        else if (auto* p = dynamic_cast<Packets::Players::RespawnPlayer*>(packet.get())) respawns.push_back(*p);
                        else if (auto* p = dynamic_cast<Packets::Pickups::State*>(packet.get())) pickups.push_back(*p);
                        else if (auto* p = dynamic_cast<Packets::Pickups::Action*>(packet.get())) pickupActions.push_back(*p);
                        else if (auto* p = dynamic_cast<Packets::Session::Update*>(packet.get())) sessions.push_back(*p);
                        else if (auto* p = dynamic_cast<Packets::Peds::PedSpawn*>(packet.get())) pedSpawns.push_back(*p);
                        else if (auto* p = dynamic_cast<Packets::Peds::PedConfirm*>(packet.get())) pedConfirms.push_back(*p);
                        else if (auto* p = dynamic_cast<Packets::Peds::PedDeath*>(packet.get())) pedDeaths.push_back(*p);
                        else if (auto* p = dynamic_cast<Packets::Peds::PedRemove*>(packet.get())) pedRemovals.push_back(*p);
                    }
                }
            }
            enet_packet_destroy(event.packet);
        }
    }
    bool Send(Packet& packet)
    {
        std::array<uint32_t,2560> words{}; serialize::WriteStream stream(reinterpret_cast<uint8_t*>(words.data()),sizeof(words));
        uint16_t type = uint16_t(packet.GetType()); if (!stream.SerializeBits(type,16)) return false;
        if (packet.GetChannel() != ePacketChannel::SYSTEM && !stream.SerializeBits(packet.serverTime,32)) return false;
        if (!packet.SerializeWrite(stream)) return false; stream.Flush();
        auto* wire = enet_packet_create(stream.GetData(),stream.GetBytesProcessed(),ENET_PACKET_FLAG_RELIABLE);
        if (!wire || enet_peer_send(peer,uint8_t(packet.GetChannel()),wire) != 0) return false;
        enet_host_flush(host); return true;
    }
};
std::vector<Client*> clients;
template<class Predicate> bool Wait(Predicate predicate, int milliseconds = 2000)
{
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do { for (auto* client : clients) client->Process(); if (predicate()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    while (std::chrono::steady_clock::now() < until);
    return false;
}
bool Connect(Client& client, uint16_t port, const char* name, const char* version = COOPANDREAS_VERSION)
{
    client.host = enet_host_create(nullptr,1,4,0,0); if (!client.host) return false;
    clients.push_back(&client); ENetAddress address; enet_address_set_host(&address,"127.0.0.1"); address.port = port;
    client.peer = enet_host_connect(client.host,&address,4,0); if (!client.peer || !Wait([&]{return client.connected;},4000)) return false;
    Packets::System::PlayerConnected hello; hello.payload = {}; strcpy_s(hello.payload.name,name);
    hello.payload.version = semver_parse(version,nullptr);
    return client.Send(hello) && Wait([&]{return client.id >= 0 && !client.maps.empty();},4000);
}
int main(int argc, char** argv)
{
    if (argc != 2 || enet_initialize() != 0) return 2;
    Client older, host, guest, late, corpseObserver, replacement, fresh; const auto port = uint16_t(std::stoi(argv[1]));
    expect(!Connect(older,port,"fixture_older","0.7.0-alpha") && older.id < 0 && !older.connected,
        "Production handshake rejects previous protocol before registering an actor or room host.");
    expect(Connect(host,port,"fixture_host"), "First real peer authenticates and receives initial map state.");
    if (host.id < 0 || host.maps.empty()) return 1;
    expect(host.hostId == host.id && host.maps.back().epoch == 0, "First peer is announced as host before unseeded state.");
    expect(Connect(guest,port,"fixture_guest"), "Second real peer authenticates.");
    if (guest.id < 0) return 1;
    expect(guest.hostId == host.id, "Joining guest receives existing unchanged host assignment.");
    Packets::Map::Discovery seed; seed.mode = Packets::Map::Mode::Seed; seed.playerid = host.id; seed.sequence = 1; seed.cells.Add(4);
    expect(host.Send(seed) && Wait([&]{return !host.maps.empty() && host.maps.back().epoch != 0 && guest.maps.back().epoch != 0;}), "Actual server accepts host seed and broadcasts room discovery.");
    const auto epoch = host.maps.back().epoch; auto prior = host.maps.size();
    seed.playerid = guest.id; seed.cells.Add(99); expect(guest.Send(seed), "Guest invalid seed is well-formed on the wire.");
    Wait([]{return false;},150); expect(host.maps.size() == prior && !guest.maps.back().cells.Has(99), "Actual server rejects guest campaign replacement.");
    Packets::Map::Discovery reveal; reveal.mode = Packets::Map::Mode::Reveal; reveal.playerid = guest.id; reveal.sequence = 1; reveal.epoch = epoch; reveal.cells.Add(8);
    expect(guest.Send(reveal) && Wait([&]{return host.maps.back().cells.Has(8);}), "Actual guest reveal reaches real host through server registration.");
    expect(host.maps.back().cells.Has(4), "Host seed is retained when guest explores.");
    Packets::Players::PlayerPlaceWaypoint waypoint; waypoint.playerid.value = guest.id; waypoint.sequence = 1; waypoint.place = true; waypoint.position = CVector2D(50,60);
    expect(guest.Send(waypoint) && Wait([&]{return !host.waypoints.empty();}), "Real waypoint relay succeeds with omitted C2S owner field.");
    expect(host.waypoints.back().playerid.value == guest.id && host.waypoints.back().generation == host.generations[guest.id], "Server waypoint owner/generation match ordered real vitals identity.");
    Packets::Players::PlayerAnimationState animation;
    animation.playerid = guest.id; animation.life = {0,1,1,0,0,100};
    animation.state = {1,.2f,3,1,1,true,1}; animation.sampledAt = animation.serverTime = 0x7fffffff;
    expect(guest.Send(animation) && Wait([&]{return !host.animations.empty() && !guest.animations.empty();}),
        "Real owner animation reaches host and receives exact life acknowledgment.");
    if (host.animations.empty()) return 1;
    expect(host.animations.back().life.generation == host.generations[guest.id]
        && host.animations.back().serverTime != 0x7fffffff && host.animations.back().sampledAt != 0x7fffffff,
        "Actual server stamps vitals generation and replaces arbitrary EVENT/sample timestamps.");
    auto animationCount = host.animations.size();
    expect(guest.Send(animation), "Duplicate animation remains a valid wire packet.");
    Wait([]{return false;},150); expect(host.animations.size() == animationCount, "Actual server rejects repeated actor sequence.");
    animation.life.sequence = 2; animation.playerid = host.id; guest.Send(animation); Wait([]{return false;},150);
    expect(host.animations.size() == animationCount, "Authenticated guest cannot publish another player's visual life.");
    animation.playerid = guest.id; animation.life.generation = host.generations[guest.id]; guest.Send(animation); Wait([]{return false;},150);
    expect(host.animations.size() == animationCount, "Actual server rejects client-chosen connection generation.");
    animation.life.generation = 0; animation.life.birth = 2;
    expect(guest.Send(animation) && Wait([&]{return host.animations.back().life.birth == 2;}), "New owner actor birth is accepted through real EVENT transport.");
    animationCount = host.animations.size(); animation.life.birth = 1; animation.life.sequence = 3;
    guest.Send(animation); Wait([]{return false;},150);
    expect(host.animations.size() == animationCount, "Old actor birth cannot overwrite current server life despite newer sequence.");
    expect(Connect(late,port,"fixture_late"), "Third real peer authenticates.");
    expect(Wait([&]{return !late.waypoints.empty();}) && late.maps.back().cells.Has(4) && late.maps.back().cells.Has(8), "Late join receives discovery and existing waypoint.");
    expect(Wait([&]{return !late.animations.empty();}) && late.animations.back().life.birth == 2
        && late.animations.back().life.sequence == 2, "Late join receives the exact current actor life rather than a stale visual.");
    Packets::Players::RespawnPlayer respawn; respawn.playerid.value = guest.id;
    respawn.life = {0,3,3,0,0,100,false}; respawn.serverTime = 0x7fffffff;
    expect(guest.Send(respawn) && Wait([&]{return !host.respawns.empty();})
        && host.respawns.back().playerid.value == guest.id && host.respawns.back().life.birth == 3
        && !host.respawns.back().life.ready && host.respawns.back().life.generation == host.generations[guest.id]
        && host.respawns.back().serverTime != 0x7fffffff,
        "Direction-dependent native respawn carries a stamped dormant boundary through real transport.");
    animation.life.birth = 3; animation.life.sequence = 4; animation.state = {};
    expect(guest.Send(animation) && Wait([&]{return host.animations.back().life.birth == 3 && host.animations.back().life.ready;}),
        "Ready owner publication acknowledges the new post-respawn life.");
    auto hostAnimation = animation; hostAnimation.playerid = host.id; hostAnimation.life = {0,1,1,0,0,11};
    expect(host.Send(hostAnimation) && Wait([&]{return host.animations.back().playerid == host.id;}), "Real host publishes acknowledged life for pickup registration.");
    const auto hostLife = host.animations.back().life;
    PickupSync::Actor hostActor{hostLife.generation,hostLife.birth,hostLife.sequence,hostLife.model,hostLife.area};
    PickupSync::Actor guestActor{host.generations[guest.id],3,4,0,0};
    Packets::Pickups::Hello pickupHello; pickupHello.actor = hostActor;
    expect(host.Send(pickupHello) && Wait([&]{return !host.pickups.empty();}), "Integrated host pickup HELLO seeds room and receives SYSTEM state.");
    pickupHello.actor = guestActor;
    expect(guest.Send(pickupHello) && Wait([&]{return !guest.pickups.empty();}), "Guest with exact acknowledged life receives pickup room replay.");
    Packets::Peds::PedSpawn cop; cop.tempid = 1; cop.requestToken = 1;
    cop.modelId = MODEL_LAPD1; cop.pedType = PED_TYPE_COP; cop.createdBy = RANDOM_CHAR;
    expect(guest.Send(cop) && Wait([&]{return !guest.pedConfirms.empty() && !host.pedSpawns.empty();}),
        "Actual guest cop registration receives a server lifetime and matching host spawn.");
    if (guest.pedConfirms.empty()) return 1;
    const auto confirmedCop = guest.pedConfirms.back();
    expect(confirmedCop.ownerid == guest.id && confirmedCop.stamp.Lifetime()
        && host.pedSpawns.back().modelId == MODEL_LAPD1 && host.pedSpawns.back().stamp.SameOwner(confirmedCop.stamp),
        "City-cop model and authenticated owner identity survive the real wire codec.");
    Packets::Peds::PedDeath death; death.pedid = confirmedCop.pedid; death.stamp = confirmedCop.stamp;
    death.stamp.sequence = 1; death.serverTime = 0x7fffffff;
    const auto deathsBefore = late.pedDeaths.size();
    expect(host.Send(death), "Foreign death seal is a well-formed packet.");
    Wait([]{return false;},150);
    expect(late.pedDeaths.size() == deathsBefore, "Room host cannot seal another peer's cop death.");
    Packets::Pickups::Action copDrop; copDrop.operation = Packets::Pickups::Operation::Create;
    copDrop.actor = guestActor; copDrop.epoch = guest.pickups.back().epoch; copDrop.sequence = 1;
    copDrop.item.creation = 1; copDrop.item.owner = guest.id; copDrop.item.model = 346;
    copDrop.item.type = 4; copDrop.item.ammo = 3; copDrop.item.remaining = 30000;
    copDrop.item.cop = {confirmedCop.pedid,death.stamp,1,guestActor.generation};
    const auto pickupCount = guest.pickups.size();
    expect(guest.Send(copDrop), "Original producer sends a bounded cop manifest before the EVENT death seal.");
    Wait([]{return false;},150);
    expect(guest.pickups.size() == pickupCount, "Real server keeps unsealed cop loot inert rather than minting an item.");
    expect(guest.Send(death) && Wait([&]{return host.pedDeaths.size() > 0 && late.pedDeaths.size() > deathsBefore;}),
        "Authenticated NPC owner death reaches every observing peer through real EVENT transport.");
    expect(host.pedDeaths.back().stamp.SameOwner(confirmedCop.stamp)
        && host.pedDeaths.back().stamp.sequence == 1 && host.pedDeaths.back().serverTime != 0x7fffffff,
        "Server preserves the immutable death seal and replaces the client framing timestamp.");
    expect(Wait([&]{return !guest.pickups.back().reset && guest.pickups.back().row.item.cop.Present()
        && !host.pickups.back().reset && host.pickups.back().row.item.cop.Present();}),
        "Normal server tick drains SYSTEM-before-EVENT manifest after the authenticated death seal.");
    const auto copItemId = guest.pickups.back().row.item.id;
    expect(host.pickups.back().row.item.id == copItemId && host.pickups.back().row.item.owner == guest.id
        && host.pickups.back().row.item.cop.producerGeneration == guestActor.generation,
        "Host receives the guest's canonical firearm and exact original producer incarnation.");
    const auto copRevision = guest.pickups.back().row.item.revision;
    copDrop.sequence = 2; guest.Send(copDrop); Wait([]{return false;},150);
    expect(guest.pickups.back().row.item.id == copItemId && guest.pickups.back().row.item.revision == copRevision,
        "Repeated manifest returns the same item without minting another drop.");
    const auto deathCount = host.pedDeaths.size();
    guest.Send(death); Wait([]{return false;},150);
    expect(host.pedDeaths.size() == deathCount, "Duplicate death seal cannot publish another corpse lifecycle.");
    expect(Connect(corpseObserver,port,"fixture_corpse_observer")
        && Wait([&]{return !corpseObserver.pedDeaths.empty() && !corpseObserver.pedSpawns.empty();}),
        "A real late join receives both registered cop identity and retained death seal.");
    expect(corpseObserver.pedDeaths.back().stamp.SameOwner(confirmedCop.stamp)
        && corpseObserver.pedDeaths.back().stamp.sequence == death.stamp.sequence,
        "Late join corpse replay retains the original owner death identity.");
    auto observerAnimation = animation; observerAnimation.playerid = corpseObserver.id;
    observerAnimation.life = {0,1,1,0,0,201}; observerAnimation.state = {};
    expect(corpseObserver.Send(observerAnimation) && Wait([&]{return !corpseObserver.animations.empty()
        && corpseObserver.animations.back().playerid == corpseObserver.id;}),
        "Late observer establishes its own acknowledged actor life before pickup replay.");
    // Identity announcements go to other peers. The owner learns its generation
    // from the exact actor-life acknowledgment, as the real pickup client does.
    pickupHello.actor = {corpseObserver.animations.back().life.generation,1,1,0,0};
    expect(corpseObserver.Send(pickupHello) && Wait([&]{return !corpseObserver.pickups.empty()
        && !corpseObserver.pickups.back().reset && corpseObserver.pickups.back().row.item.id == copItemId;}),
        "Ready late join receives the same retained cop firearm identity.");
    Packets::Pickups::Action pickupCreate; pickupCreate.operation = Packets::Pickups::Operation::Create;
    pickupCreate.actor = hostActor; pickupCreate.epoch = host.pickups.back().epoch; pickupCreate.sequence = 1;
    pickupCreate.item.creation = 1; pickupCreate.item.owner = host.id; pickupCreate.item.model = 1240; pickupCreate.item.type = 3;
    expect(host.Send(pickupCreate) && Wait([&]{return !guest.pickups.back().reset
        && guest.pickups.back().row.item.owner == host.id && guest.pickups.back().row.item.creation == 1
        && guest.pickups.back().row.item.model == 1240;}), "Authenticated host registers supported exterior pickup through real handlers.");
    const auto itemId = guest.pickups.back().row.item.id;
    Packets::Pickups::Action claim; claim.operation = Packets::Pickups::Operation::Claim; claim.actor = guestActor;
    claim.epoch = pickupCreate.epoch; claim.sequence = 3; claim.id = itemId;
    expect(guest.Send(claim) && Wait([&]{return !guest.pickupActions.empty();}), "Real SYSTEM claim atomically reserves and grants the exact collector life.");
    const auto originalGrant = guest.pickupActions.back().grant;
    auto receipt = claim; receipt.operation = Packets::Pickups::Operation::Result; receipt.sequence = 4; receipt.grant = originalGrant;
    receipt.outcome = PickupSync::Outcome::DeclinedBeforeApply;
    expect(guest.Send(receipt) && Wait([&]{return guest.pickups.back().row.item.id == itemId && guest.pickups.back().row.stage == PickupSync::Stage::Removed;}),
        "Exact pre-apply decline settles accounting without native effect or pickup reactivation.");
    const auto actionCount = guest.pickupActions.size(); guest.Send(receipt); Wait([]{return false;},150);
    expect(guest.pickupActions.size() == actionCount, "Duplicate terminal receipt cannot emit another grant.");
    pickupCreate.sequence = 2; pickupCreate.item.creation = 2;
    expect(host.Send(pickupCreate) && Wait([&]{return guest.pickups.back().row.item.id != itemId && guest.pickups.back().row.stage == PickupSync::Stage::Active;}),
        "Terminal receipt releases collector for a distinct non-reused item identity.");
    claim.sequence = 5; claim.id = guest.pickups.back().row.item.id;
    expect(guest.Send(claim) && Wait([&]{return guest.pickupActions.size() > actionCount;}) && guest.pickupActions.back().grant > originalGrant,
        "New reservation uses a distinct monotonic grant token.");
    expect(Wait([&]{return !host.sessions.empty() && !guest.sessions.empty();}),"Integrated session identities are delivered to real peers.");
    Packets::Session::Seed sessionSeed;sessionSeed.state=host.sessions.back().state;sessionSeed.state.money=100;sessionSeed.state.wanted=4;
    expect(host.Send(sessionSeed)&&Wait([&]{return guest.sessions.back().state.ready&&guest.sessions.back().state.wanted==4;}),
        "Authenticated host seeds shared wanted for resurrection transport check.");
    Packets::Session::Transaction resetWanted;resetWanted.op.epoch=guest.sessions.back().state.epoch;resetWanted.op.incarnation=guest.sessions.back().state.incarnation;
    resetWanted.op.sequence=1;resetWanted.op.kind=SessionSync::Kind::WantedLower;resetWanted.op.reason=SessionSync::Reason::Resurrection;resetWanted.op.level=0;
    expect(guest.Send(resetWanted)&&Wait([&]{return host.sessions.back().state.wanted==0&&guest.sessions.back().state.acknowledged==1;}),
        "Real authenticated guest resurrection clears canonical wanted and receives exact receipt.");
    guest.Send(resetWanted);Wait([]{return false;},150);
    expect(guest.sessions.back().state.wanted==0&&guest.sessions.back().state.acknowledged==1,
        "Repeated resurrection receipt cannot restore old wanted or advance the sequence twice.");
    auto oldGeneration = host.waypoints.back().generation; auto guestId = guest.id;
    enet_peer_disconnect(corpseObserver.peer,0); enet_host_flush(corpseObserver.host);
    expect(Wait([&]{return !corpseObserver.connected;}), "Corpse replay observer departs before migration checks.");
    enet_peer_disconnect(guest.peer,0); enet_host_flush(guest.host); Wait([&]{return !guest.connected;});
    expect(Connect(replacement,port,"fixture_replacement"), "Replacement peer authenticates.");
    expect(replacement.id == guestId && host.generations[guestId] != oldGeneration, "Reused slot gets a distinct connection generation.");
    expect(replacement.waypoints.empty(), "Reused slot does not inherit departed waypoint.");
    bool departedLifeReplayed = false;
    for (const auto& state : replacement.animations) if (state.playerid == guestId) departedLifeReplayed = true;
    expect(!departedLifeReplayed, "Reused slot does not inherit departed actor life or animation replay.");
    enet_peer_disconnect(host.peer,0); enet_host_flush(host.host);
    expect(Wait([&]{return late.hostId == late.id;}), "Host departure promotes earliest remaining real peer.");
    expect(late.maps.back().epoch == epoch && late.maps.back().cells.Has(8), "Actual server migration keeps room discoveries.");
    enet_peer_disconnect(late.peer,0); enet_host_flush(late.host);
    expect(Wait([&]{return !late.connected && replacement.hostId == replacement.id;}), "Remaining peer receives second host migration.");
    enet_peer_disconnect(replacement.peer,0); enet_host_flush(replacement.host);
    expect(Wait([&]{return !replacement.connected;}), "All fixture peers depart cleanly.");
    expect(Connect(fresh,port,"fixture_fresh") && fresh.maps.back().epoch == 0 && fresh.maps.back().cells.Count() == 0,
        "Actual last departure clears map for a new room.");
    seed.playerid = fresh.id; seed.sequence = 1; seed.epoch = 0; seed.cells = {}; seed.cells.Add(15);
    expect(fresh.Send(seed) && Wait([&]{return fresh.maps.back().epoch > epoch;}), "New room receives a distinct monotonic campaign epoch.");
    expect(fresh.maps.back().cells.Has(15) && !fresh.maps.back().cells.Has(8), "New room does not retain prior guest discoveries.");
    for (auto* client : clients) if (client->host) enet_host_destroy(client->host);
    enet_deinitialize(); std::cout << checks << " assertions, " << failures << " failures\n";
    return failures ? 1 : 0;
}
