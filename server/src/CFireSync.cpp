#include "stdafx.h"
#include "CFireSync.h"
#include "CNetworkObjectManager.h"
#include "network/vehicle_authority.h"

namespace {
FireSync::Cache& Room() { static FireSync::Cache room; return room; }
std::array<FireSync::Peer,FireSync::MaxPlayers> peers{};
std::array<FireSync::Entity,FireSync::MaxPlayers> players{};
std::array<FireSync::Entity,FireSync::MaxEntities> vehicles{};
std::array<CNetworkVehicle*,FireSync::MaxEntities> nativeVehicles{};
uint32_t identity = 0, entityGeneration = 0;
bool Auth(CNetworkPlayer* p) { return p && p->m_pPeer && p->m_pPeer->state == ENET_PEER_STATE_CONNECTED &&
    p->m_iPlayerId >= 0 && p->m_iPlayerId < FireSync::MaxPlayers && CNetworkPlayerManager::GetPlayer(p->m_pPeer) == p; }
void Broadcast(Packet& packet) {
    for (auto* p : CNetworkPlayerManager::m_pPlayers) if (Auth(p) && peers[p->m_iPlayerId].watching) GetPacketFactory().Send(packet,p);
}
void Binding(const FireSync::Entity& e, bool live = true, CNetworkPlayer* recipient = nullptr) {
    if (!Room().epoch || !e.Valid() || e.kind == FireSync::Kind::World) return;
    Packets::Fires::Bind packet; packet.epoch = Room().epoch; packet.entity = e; packet.live = live;
    if (recipient) GetPacketFactory().Send(packet,recipient); else Broadcast(packet);
}
bool Resolve(FireSync::Entity& e) {
    if (!e.Valid()) return false;
    if (e.kind == FireSync::Kind::World) return true;
    if (e.kind == FireSync::Kind::Player) return FireSync::Matches(e,players[e.id]);
    if (e.kind == FireSync::Kind::Vehicle) {
        auto* car = CNetworkVehicleManager::GetVehicle(int(e.id));
        if (!car) return false;
        CFireSync::VehicleChanged(car);
        return FireSync::Matches(e,vehicles[e.id]);
    }
    if (e.kind == FireSync::Kind::Ped) {
        auto* ped = CNetworkPedManager::GetPed(int(e.id));
        if (!ped || ped->m_generation != e.generation || ped->m_ownerEpoch != e.ownerEpoch ||
            int(ped->m_nModelId) != e.model || !Auth(ped->m_pSyncer)) return false;
        e.owner = ped->m_pSyncer->m_iPlayerId; return true;
    }
    // Object IDs are monotonic in the existing mission-object protocol. No ownership rewrite.
    auto* record = CNetworkObjectManager::Registry().ById(e.id);
    if (!record || int(record->state.model) != e.model) return false;
    e.owner = Room().host; return e.ownerEpoch == Room().epoch;
}
void Reset(CNetworkPlayer* recipient) {
    if (!Auth(recipient) || !Room().epoch) return;
    Packets::Fires::Reset packet; packet.epoch = Room().epoch; packet.host = Room().host;
    packet.connection = peers[recipient->m_iPlayerId].connection; packet.gameGeneration = peers[recipient->m_iPlayerId].gameGeneration;
    packet.recipientBirth = players[recipient->m_iPlayerId].generation;
    GetPacketFactory().Send(packet,recipient);
}
}
void CFireSync::Join(CNetworkPlayer* player) {
    if (!Auth(player) || identity == FireSync::MaxCounter) return;
    peers[player->m_iPlayerId] = {}; peers[player->m_iPlayerId].connection = ++identity;
}
void CFireSync::Leave(CNetworkPlayer* player) {
    if (!player || player->m_iPlayerId < 0 || player->m_iPlayerId >= FireSync::MaxPlayers) return;
    const int id = player->m_iPlayerId;
    if (players[id].Valid() && players[id].kind != FireSync::Kind::World) Binding(players[id],false);
    peers[id] = {}; players[id] = {};
    if (id == Room().host) HostChanged(nullptr);
}
void CFireSync::HostChanged(CNetworkPlayer* player) {
    const int host = player ? player->m_iPlayerId : -1;
    if (Room().epoch == FireSync::MaxCounter) return;
    Room().Reset(Room().epoch+1,host);
    for (auto* p : CNetworkPlayerManager::m_pPlayers) if (Auth(p) && peers[p->m_iPlayerId].watching) Reset(p);
}
void CFireSync::Pose(CNetworkPlayer* player, const CVector& position) {
    if (!Auth(player)) return;
    FireSync::Vec p{position.x,position.y,position.z}; if (p.Valid()) peers[player->m_iPlayerId].position = p;
}
void CFireSync::VehicleChanged(CNetworkVehicle* vehicle, bool removed) {
    if (!vehicle || vehicle->m_nVehicleId < 0 || vehicle->m_nVehicleId >= FireSync::MaxEntities) return;
    auto& e = vehicles[vehicle->m_nVehicleId];
    if (removed) { if (e.kind == FireSync::Kind::Vehicle) Binding(e,false); e = {}; nativeVehicles[vehicle->m_nVehicleId] = nullptr; return; }
    auto* owner = vehicle->m_pPlayers[0] ? vehicle->m_pPlayers[0] : vehicle->m_pSyncer;
    if (!Auth(owner)) return;
    if (nativeVehicles[vehicle->m_nVehicleId] != vehicle || e.kind != FireSync::Kind::Vehicle) {
        if (entityGeneration == FireSync::MaxCounter) return;
        e = {}; e.kind = FireSync::Kind::Vehicle; e.id = uint32_t(vehicle->m_nVehicleId); e.generation = ++entityGeneration; e.ownerEpoch = 1;
        nativeVehicles[vehicle->m_nVehicleId] = vehicle;
    } else if (e.owner != owner->m_iPlayerId) { if (e.ownerEpoch == FireSync::MaxCounter) return; ++e.ownerEpoch; }
    else return;
    e.owner = owner->m_iPlayerId; e.model = int(vehicle->m_nModelId); Binding(e);
}
void CFireSync::Hello(const Packets::Fires::Hello& packet, CNetworkPlayer* sender) {
    if (!Auth(sender) || !packet.position.Valid() || packet.model < 0 || packet.model > 19999 ||
        !packet.gameGeneration || packet.gameGeneration > FireSync::MaxCounter) return;
    auto& peer = peers[sender->m_iPlayerId]; if (!peer.connection) return;
    if (packet.gameGeneration < peer.gameGeneration) return;
    const bool newGame = packet.gameGeneration > peer.gameGeneration;
    if (peer.ready && newGame && packet.controllingRestart && sender->m_iPlayerId == Room().host && sender->m_bIsHost) HostChanged(sender);
    auto& e = players[sender->m_iPlayerId];
    if (newGame || !peer.ready || peer.nativeReference != packet.nativeReference || e.model != packet.model) {
        if (entityGeneration == FireSync::MaxCounter) return;
        e.kind = FireSync::Kind::Player; e.id = uint32_t(sender->m_iPlayerId); e.generation = ++entityGeneration;
        e.ownerEpoch = peer.connection; e.owner = sender->m_iPlayerId; e.model = packet.model;
        peer.nativeReference = packet.nativeReference; Binding(e);
    }
    if (newGame) peer.sequence = 0;
    peer.gameGeneration = packet.gameGeneration; peer.ready = peer.watching = true; peer.position = packet.position;
    Reset(sender);
    for (auto* car : CNetworkVehicleManager::m_pVehicles) VehicleChanged(car);
    for (const auto& binding : players) if (binding.kind == FireSync::Kind::Player) Binding(binding,true,sender);
    for (const auto& binding : vehicles) if (binding.kind == FireSync::Kind::Vehicle) Binding(binding,true,sender);
    for (const auto& slot : Room().slots) if (slot.state.key.Valid()) {
        if (slot.live) { Packets::Fires::Update update; update.state = slot.state; GetPacketFactory().Send(update,sender); }
        else { Packets::Fires::Remove remove; remove.key = slot.state.key; GetPacketFactory().Send(remove,sender); }
    }
}
void CFireSync::Update(Packets::Fires::Update& packet, CNetworkPlayer* sender) {
    if (!Auth(sender) || sender->m_iPlayerId != Room().host || !sender->m_bIsHost ||
        !packet.state.Valid() || !Resolve(packet.state.target) || !Resolve(packet.state.creator)) return;
    if (Room().Accept(packet.state)) Broadcast(packet);
}
void CFireSync::Remove(const Packets::Fires::Remove& packet, CNetworkPlayer* sender) {
    if (Auth(sender) && sender->m_iPlayerId == Room().host && sender->m_bIsHost && Room().Remove(packet.key)) {
        auto copy = packet; Broadcast(copy);
    }
}
void CFireSync::Request(Packets::Fires::Request& packet, CNetworkPlayer* sender) {
    if (!Auth(sender) || !FireSync::AcceptRequest(peers[sender->m_iPlayerId],packet.request,Room().epoch,g_serverTime)) return;
    auto& r = packet.request;
    if (!Resolve(r.creator) || !Resolve(r.target) || !Resolve(r.issuer) || r.issuer.owner != sender->m_iPlayerId ||
        r.issuer.id != uint32_t(sender->m_iPlayerId)) return;
    if ((r.intent == FireSync::Intent::Ground || r.intent == FireSync::Intent::Attached) &&
        (r.creator.kind == FireSync::Kind::World || r.creator.owner != sender->m_iPlayerId)) return;
    if (r.intent == FireSync::Intent::Heat && (r.target.kind != FireSync::Kind::Player || r.target.owner != sender->m_iPlayerId)) return;
    if (r.intent == FireSync::Intent::Water || r.intent == FireSync::Intent::Stop || r.intent == FireSync::Intent::Heat) {
        if (r.fire.epoch != Room().epoch || r.fire.id > FireSync::MaxFires) return;
        const auto& fire = Room().slots[r.fire.id-1];
        if (!fire.live || !FireSync::SameFire(fire.state.key,r.fire)) return;
        if (r.intent == FireSync::Intent::Stop) {
            auto target = fire.state.target;
            // A cached burn may lag a driver/owner transfer. Authorize against the current lease.
            if (!Resolve(target) || target.kind == FireSync::Kind::World || target.owner != sender->m_iPlayerId) return;
        }
        if (r.intent == FireSync::Intent::Heat && FireSync::Distance2(fire.state.position,r.position) > 1.2f) return;
        if (r.intent == FireSync::Intent::Water && FireSync::Distance2(fire.state.position,r.position) > r.radius*r.radius) return;
    }
    packet.sender = sender->m_iPlayerId;
    packet.serverTime = g_serverTime; // Never forward a guest-controlled framing time into the host EVENT queue.
    auto* host = CNetworkPlayerManager::GetPlayer(Room().host); if (Auth(host)) GetPacketFactory().Send(packet,host);
}
