#include "stdafx.h"
#include "CFireSync.h"
#include "CPacketBuffer.h"
#include "CNetworkObjectManager.h"
#include <CFireManager.h>
#include <CPed.h>
#include <CPedIntelligence.h>
#include <CVehicle.h>
#include <type_traits>

namespace {
FireSync::Ledger& Ledger() { static FireSync::Ledger ledger; return ledger; }
struct NativeSlot {
    FireSync::Key key{};
    bool live = false, replica = false, suppressed = false;
    CFire* fire = nullptr;
    CEntity* attached = nullptr;
    int reference = -1;
    uint32_t lastSent = 0;
    FireSync::Entity capturedTarget{};
};
auto& Native() { static std::array<NativeSlot,FireSync::MaxFires> slots{}; return slots; }
struct Binding { FireSync::Entity entity{}; bool live = false; int reference = -1; uint32_t epoch = 0; };
auto& Bindings() { static std::array<Binding,FireSync::MaxPlayers+FireSync::MaxEntities> bindings{}; return bindings; }
auto& PendingBindings() { static std::array<Binding,FireSync::MaxPlayers+FireSync::MaxEntities> bindings{}; return bindings; }
uint32_t connection = 0, requestSequence = 0, lastHello = 0, gameGeneration = 0, networkConnection = 0;
bool controllingRestart = false, nativeEnabled = false;
uint32_t acknowledgedGame = 0, acknowledgedBirth = 0, ownBirthFloor = 0;
int expectedHost = -1, lastPedRef = -1, replayDepth = 0;
bool scriptsReady = false;
bool guestReconciled = false;
bool CurrentOwnBirth(const FireSync::Entity& e) {
    const int own = CNetworkPlayerManager::m_nMyId;
    return acknowledgedGame == gameGeneration && acknowledgedBirth && connection &&
        e.kind == FireSync::Kind::Player && int(e.id) == own && e.owner == own &&
        e.generation == acknowledgedBirth && e.ownerEpoch == connection;
}
int BindingIndex(FireSync::Kind kind, uint32_t id) {
    if (kind == FireSync::Kind::Player && id < FireSync::MaxPlayers) return int(id);
    if (kind == FireSync::Kind::Vehicle && id < FireSync::MaxEntities) return FireSync::MaxPlayers+int(id);
    return -1;
}
int FireIndex(CFire* fire) {
    const auto address = reinterpret_cast<uintptr_t>(fire), begin = reinterpret_cast<uintptr_t>(&gFireManager.m_aFires[0]);
    if (address < begin || address >= begin+sizeof(gFireManager.m_aFires) || (address-begin)%sizeof(CFire)) return -1;
    return int((address-begin)/sizeof(CFire));
}
bool ValidNative(CEntity* entity, int reference = -1) {
    if (!entity) return false;
    // Pool containment is checked before reading the entity's type or model.
    if (CPools::ms_pPedPool && CPools::ms_pPedPool->IsObjectValid(static_cast<CPed*>(entity)))
        return reference < 0 || CPools::GetPedRef(static_cast<CPed*>(entity)) == reference;
    if (CPools::ms_pVehiclePool && CPools::ms_pVehiclePool->IsObjectValid(static_cast<CVehicle*>(entity)))
        return reference < 0 || CPools::GetVehicleRef(static_cast<CVehicle*>(entity)) == reference;
    if (CPools::ms_pObjectPool && CPools::ms_pObjectPool->IsObjectValid(static_cast<CObject*>(entity)))
        return reference < 0 || CPools::GetObjectRef(static_cast<CObject*>(entity)) == reference;
    return false;
}
int Reference(CEntity* entity) {
    if (!ValidNative(entity)) return -1;
    if (entity->m_nType == ENTITY_TYPE_PED) return CPools::GetPedRef(static_cast<CPed*>(entity));
    if (entity->m_nType == ENTITY_TYPE_VEHICLE) return CPools::GetVehicleRef(static_cast<CVehicle*>(entity));
    if (entity->m_nType == ENTITY_TYPE_OBJECT) return CPools::GetObjectRef(static_cast<CObject*>(entity));
    return -1;
}
bool Owned(CEntity* entity) {
    if (!ValidNative(entity)) return false;
    if (entity == FindPlayerPed(0)) return true;
    if (CNetworkPlayerManager::GetPlayer(entity)) return false;
    if (auto* ped = CNetworkPedManager::GetPed(entity)) return ped->HasValidPed() && ped->m_bSyncing && ped->m_generation;
    if (auto* car = CNetworkVehicleManager::GetVehicle(entity)) {
        if (!car->HasValidVehicle()) return false;
        const int index = car->m_nVehicleId >= 0 ? BindingIndex(FireSync::Kind::Vehicle,uint32_t(car->m_nVehicleId)) : -1;
        if (index >= 0 && Bindings()[index].live && Bindings()[index].entity.owner != CNetworkPlayerManager::m_nMyId) return false;
        return car->m_pVehicle && (car->m_pVehicle->m_pDriver == FindPlayerPed(0) ||
            (!car->m_pVehicle->m_pDriver && car->m_bSyncing));
    }
    // A stale mapped car cannot become host-owned scenery after script reset.
    if (CNetworkVehicleManager::FindVehicle(entity)) return false;
    return CFireSync::IsHost(); // Host-only unnetworked world targets.
}
FireSync::Entity Capture(CEntity* entity) {
    FireSync::Entity e;
    if (!ValidNative(entity)) return e;
    if (auto* ped = CNetworkPedManager::GetPed(entity)) {
        if (!ped->m_generation || !ped->m_ownerEpoch) return e;
        e.kind = FireSync::Kind::Ped; e.id = uint32_t(ped->m_nPedId); e.generation = ped->m_generation;
        e.ownerEpoch = ped->m_ownerEpoch; e.model = ped->m_pPed->m_nModelIndex;
        e.owner = CNetworkPlayerManager::m_nMyId; // Server derives actual NPC owner from matching stamp.
        return e.Valid() ? e : FireSync::Entity{};
    }
    int id = -1; FireSync::Kind kind = FireSync::Kind::World;
    if (entity == FindPlayerPed(0)) { id = CNetworkPlayerManager::m_nMyId; kind = FireSync::Kind::Player; }
    else if (auto* p = CNetworkPlayerManager::GetPlayer(entity)) { id = p->m_iPlayerId; kind = FireSync::Kind::Player; }
    else if (auto* car = CNetworkVehicleManager::GetVehicle(entity)) {
        if (!car->HasValidVehicle()) return e;
        id = car->m_nVehicleId; kind = FireSync::Kind::Vehicle;
    }
    const int index = id >= 0 ? BindingIndex(kind,uint32_t(id)) : -1;
    if (index >= 0 && Bindings()[index].live && Bindings()[index].entity.model == entity->m_nModelIndex) {
        auto& binding = Bindings()[index];
        if (entity == FindPlayerPed(0) && !CurrentOwnBirth(binding.entity)) return {};
        if (binding.reference < 0) binding.reference = Reference(entity);
        if (ValidNative(entity,binding.reference)) return binding.entity;
        return {};
    }
    const int object = CNetworkObjectManager::GetNetworkId(entity);
    if (object > 0) {
        e.kind = FireSync::Kind::Object; e.id = uint32_t(object); e.generation = 1;
        e.ownerEpoch = Ledger().active.epoch; e.owner = Ledger().active.host; e.model = entity->m_nModelIndex;
    }
    return e.Valid() ? e : FireSync::Entity{};
}
CEntity* Resolve(const FireSync::Entity& e) {
    if (!e.Valid() || e.kind == FireSync::Kind::World) return nullptr;
    CEntity* target = nullptr;
    if (e.kind == FireSync::Kind::Ped) {
        auto* p = CNetworkPedManager::GetPed(int(e.id));
        if (!p || !p->HasValidPed() || p->m_generation != e.generation || p->m_ownerEpoch != e.ownerEpoch) return nullptr;
        target = p->m_pPed;
    } else if (e.kind == FireSync::Kind::Object) {
        const int ref = CNetworkObjectManager::GetHandle(e.id);
        target = ref >= 0 && CPools::ms_pObjectPool ? CPools::ms_pObjectPool->GetAtRef(ref) : nullptr;
    } else {
        const int index = BindingIndex(e.kind,e.id);
        if (index < 0 || !Bindings()[index].live || !FireSync::Matches(e,Bindings()[index].entity)) return nullptr;
        if (e.kind == FireSync::Kind::Player) {
            if (int(e.id) == CNetworkPlayerManager::m_nMyId) {
                if (!CurrentOwnBirth(e)) return nullptr;
                target = FindPlayerPed(0);
            }
            else if (auto* p = CNetworkPlayerManager::GetPlayer(int(e.id))) target = p->m_pPed;
        } else if (auto* car = CNetworkVehicleManager::GetVehicle(int(e.id))) {
            if (!car->HasValidVehicle()) return nullptr;
            target = car->m_pVehicle;
        }
        if (!ValidNative(target)) return nullptr;
        auto& binding = Bindings()[index];
        if (binding.reference < 0) binding.reference = Reference(target);
        if (!ValidNative(target,binding.reference)) return nullptr;
    }
    return ValidNative(target) && target->m_nModelIndex == e.model ? target : nullptr;
}
void ClearManaged() {
    CFireSync::BeginReplay();
    for (auto& n : Native()) {
        if (n.fire && FireIndex(n.fire) >= 0 && n.live) {
            CFireSync::OriginalExtinguish(n.fire);
            if (n.replica) n.fire->m_nFlags.bCreatedByScript = false;
        }
        n = {};
    }
    guestReconciled = false;
    CFireSync::EndReplay();
}
void ReleaseReplica(NativeSlot& n) {
    if (n.fire && n.replica) {
        CFireSync::OriginalExtinguish(n.fire);
        // No guest SCM handle owns this reservation; native Extinguish leaves it set.
        n.fire->m_nFlags.bCreatedByScript = false;
    }
    n = {};
}
void EmitRequest(FireSync::Request r) {
    if (!connection || acknowledgedGame != gameGeneration || !Ledger().active.epoch || requestSequence == FireSync::MaxCounter || CFireSync::Replaying()) return;
    r.epoch = Ledger().active.epoch; r.connection = connection; r.sequence = ++requestSequence;
    r.gameGeneration = gameGeneration; r.issuer = Capture(FindPlayerPed(0));
    if (!r.Valid()) return;
    Packets::Fires::Request packet; packet.sender = CNetworkPlayerManager::m_nMyId; packet.request = r;
    GetPacketFactory().Send(packet);
}
bool TargetReady(const FireSync::State& s, CEntity*& target) {
    target = s.target.kind == FireSync::Kind::World ? nullptr : Resolve(s.target);
    return s.target.kind == FireSync::Kind::World || target != nullptr;
}
void ApplyReplicas() {
    for (int i = 0; i < FireSync::MaxFires; ++i) {
        const auto& slot = Ledger().active.slots[i]; auto& n = Native()[i]; CEntity* target = nullptr;
        const bool valid = slot.live && TargetReady(slot.state,target);
        if (n.live && (!valid || !FireSync::SameFire(n.key,slot.state.key) ||
            (n.attached && !ValidNative(n.attached,n.reference)))) {
            ReleaseReplica(n);
        }
        if (!slot.live && n.replica) ReleaseReplica(n);
        if (!valid || n.suppressed) continue;
        const auto& s = slot.state;
        const bool owner = target && Owned(target) && s.target.owner == CNetworkPlayerManager::m_nMyId;
        if (n.live && n.attached && (!owner || n.attached != target)) ReleaseReplica(n);
        if (!n.live) {
            CFire* fire = nullptr;
            for (auto& candidate : gFireManager.m_aFires) if (!candidate.m_nFlags.bActive && !candidate.m_nFlags.bCreatedByScript) { fire = &candidate; break; }
            if (!fire) continue; // Native pool exhaustion is bounded and retries after cleanup.
            fire->Initialise(); fire->m_nFlags.bActive = true; fire->m_nFlags.bCreatedByScript = s.script;
            fire->m_fStrength = s.strength; fire->m_vecPosition = {s.position.x,s.position.y,s.position.z};
            fire->m_nNumGenerationsAllowed = 0; // Never independent guest spread.
            fire->CreateFxSysForStrength(reinterpret_cast<RwV3d*>(&fire->m_vecPosition),nullptr);
            n.fire = fire; n.live = n.replica = true; n.key = s.key;
        }
        n.key = s.key; auto* fire = n.fire;
        if (owner && !n.attached && s.target.kind != FireSync::Kind::Object) {
            n.attached = target; n.reference = Reference(target); fire->m_pEntityTarget = target;
            target->RegisterReference(&fire->m_pEntityTarget);
            if (target->m_nType == ENTITY_TYPE_PED) static_cast<CPed*>(target)->m_pFire = fire;
            else if (target->m_nType == ENTITY_TYPE_VEHICLE) static_cast<CVehicle*>(target)->m_pFire = fire;
            if (target->m_nType == ENTITY_TYPE_PED && static_cast<CPed*>(target)->m_pIntelligence) {
                // Native 0x607E30 emits EVENT_ON_FIRE from m_pFire; its AffectsPed gate
                // suppresses duplicate on-fire tasks. No guessed event or parallel damage loop.
                plugin::CallMethod<0x607E30>(&static_cast<CPed*>(target)->m_pIntelligence->m_eventScanner,static_cast<CPed*>(target));
            }
        }
        fire->m_nFlags.bMakesNoise = s.noise; fire->m_nTimeToBurn = CTimer::m_snTimeInMilliseconds+s.remaining;
        const auto pos = target ? target->GetPosition() : CVector{s.position.x,s.position.y,s.position.z};
        fire->m_vecPosition = pos;
        if (std::abs(fire->m_fStrength-s.strength) >= 0.5f) {
            fire->m_fStrength = s.strength; fire->CreateFxSysForStrength(reinterpret_cast<RwV3d*>(&fire->m_vecPosition),nullptr);
        }
        if (fire->m_pFxSystem) fire->m_pFxSystem->SetOffsetPos(reinterpret_cast<RwV3d*>(&fire->m_vecPosition));
    }
}
void Publish() {
    if (!CFireSync::IsHost() || !Ledger().active.epoch) return;
    const uint32_t now = GetTickCount();
    for (int i = 0; i < FireSync::MaxFires; ++i) {
        auto* fire = &gFireManager.m_aFires[i]; auto& n = Native()[i];
        if (!fire->m_nFlags.bActive) continue;
        if (!n.live) {
            if (n.key.generation == FireSync::MaxCounter) continue;
            ++n.key.generation; n.key.epoch = Ledger().active.epoch; n.key.id = uint32_t(i+1); n.key.sequence = 0;
            n.fire = fire; n.live = true; n.lastSent = 0;
        }
        if (now-n.lastSent < 100 || n.key.sequence == FireSync::MaxCounter) continue;
        FireSync::State s; s.key = n.key; ++s.key.sequence;
        s.position = {fire->m_vecPosition.x,fire->m_vecPosition.y,fire->m_vecPosition.z};
        s.target = Capture(fire->m_pEntityTarget); s.creator = Capture(fire->m_pEntityCreator);
        if (n.capturedTarget.kind != FireSync::Kind::World &&
            (s.target.kind != n.capturedTarget.kind || s.target.id != n.capturedTarget.id ||
             s.target.generation != n.capturedTarget.generation || s.target.model != n.capturedTarget.model)) {
            fire->Extinguish(); continue; // Old burn cannot follow a reused entity or campaign incarnation.
        }
        // A tracked actor whose generation/binding is not ready stays deferred, never a raw-slot attachment.
        if (fire->m_pEntityTarget && ValidNative(fire->m_pEntityTarget) &&
            (CNetworkPedManager::GetPed(fire->m_pEntityTarget) || CNetworkVehicleManager::GetVehicle(fire->m_pEntityTarget) ||
             CNetworkPlayerManager::GetPlayer(fire->m_pEntityTarget) || fire->m_pEntityTarget == FindPlayerPed(0)) && s.target.kind == FireSync::Kind::World) continue;
        s.strength = fire->m_fStrength; s.generations = uint8_t(fire->m_nNumGenerationsAllowed);
        s.script = fire->m_nFlags.bCreatedByScript; s.noise = fire->m_nFlags.bMakesNoise;
        s.remaining = fire->m_nTimeToBurn > CTimer::m_snTimeInMilliseconds ?
            (std::min)(120000u,fire->m_nTimeToBurn-CTimer::m_snTimeInMilliseconds) : 0;
        if (!s.Valid()) continue;
        Packets::Fires::Update packet; packet.state = s; GetPacketFactory().Send(packet);
        n.key = s.key; n.lastSent = now; n.capturedTarget = s.target;
    }
}
}
bool CFireSync::Ready() {
    auto* ped = FindPlayerPed(0);
    return nativeEnabled && CNetwork::m_bAuthenticated && gGameState == 9 && scriptsReady && CWorld::PlayerInFocus == 0 &&
        ped && CPools::ms_pPedPool && CPools::ms_pPedPool->IsObjectValid(ped) && CWorld::Players[0].m_pPed == ped;
}
bool CFireSync::IsHost() { return Ready() && Ledger().active.epoch && Ledger().active.host == CNetworkPlayerManager::m_nMyId &&
    expectedHost == Ledger().active.host && acknowledgedGame == gameGeneration && CLocalPlayer::m_bIsHost; }
bool CFireSync::Replaying() { return replayDepth != 0; }
void CFireSync::EnableNative() { nativeEnabled = true; }
void CFireSync::BeginReplay() { ++replayDepth; }
void CFireSync::EndReplay() { --replayDepth; }
void CFireSync::Init() {
    NativeInit();
    Events::initScriptsEvent.before += [] {
        controllingRestart = gameGeneration && CNetwork::m_bAuthenticated && Ledger().active.host == CNetworkPlayerManager::m_nMyId && CLocalPlayer::m_bIsHost;
        if (gameGeneration < FireSync::MaxCounter) ++gameGeneration;
        const int own = CNetworkPlayerManager::m_nMyId;
        if (own >= 0 && own < FireSync::MaxPlayers) ownBirthFloor = (std::max)(ownBirthFloor,Bindings()[own].entity.generation);
        scriptsReady = false; Reset();
    };
    Events::processScriptsEvent.after += [] { if (gGameState == 9) scriptsReady = true; };
    gameShutdownEvent.before += [] { scriptsReady = false; Reset(); };
}
void CFireSync::Reset() {
    ClearManaged(); Ledger() = {}; Bindings() = {}; PendingBindings() = {}; connection = requestSequence = lastHello = 0;
    expectedHost = lastPedRef = -1; acknowledgedGame = acknowledgedBirth = 0;
}
void CFireSync::HostChanged(int host) {
    expectedHost = host;
    // Do not erase a processed/queued newer epoch merely because SYSTEM follows EVENT.
    if (Ledger().active.host != host) ClearManaged();
    lastHello = 0;
}
void CFireSync::Queue(Packet& packet) {
    const auto type = packet.GetType();
    if (type != ePacketType::FIRE_STATE && type != ePacketType::FIRE_REMOVE && type != ePacketType::FIRE_BIND && type != ePacketType::FIRE_REQUEST) return;
    const auto& queue = GetPacketBuffer().m_packets;
    for (auto it = queue.rbegin(); it != queue.rend(); ++it) if ((*it)->GetChannel() == ePacketChannel::EVENT) {
        packet.serverTime = (std::max)(packet.serverTime,(*it)->serverTime); break;
    }
}
void CFireSync::VehicleRemoved(int id) {
    const int index = id >= 0 ? BindingIndex(FireSync::Kind::Vehicle,uint32_t(id)) : -1;
    if (index >= 0) { Bindings()[index].live = false; Bindings()[index].reference = -1; }
}
void CFireSync::Receive(const Packets::Fires::Reset& p) {
    if (!CNetwork::m_bAuthenticated || !p.epoch || p.epoch > FireSync::MaxCounter || !p.connection || p.connection > FireSync::MaxCounter ||
        p.host < -1 || p.host >= FireSync::MaxPlayers || p.epoch < Ledger().active.epoch || p.gameGeneration != gameGeneration ||
        !p.recipientBirth || p.recipientBirth > FireSync::MaxCounter) return;
    if (p.epoch == Ledger().active.epoch && (p.host != Ledger().active.host || (connection && connection != p.connection))) return;
    if (acknowledgedGame == gameGeneration && p.recipientBirth < acknowledgedBirth) return;
    if (p.epoch > Ledger().active.epoch) { ClearManaged(); Ledger().Reset(p.epoch,p.host); Bindings() = {}; }
    if (expectedHost == -1) expectedHost = p.host;
    connection = p.connection; acknowledgedGame = p.gameGeneration; acknowledgedBirth = p.recipientBirth;
    const int own = CNetworkPlayerManager::m_nMyId;
    if (own >= 0 && own < FireSync::MaxPlayers && !CurrentOwnBirth(Bindings()[own].entity)) Bindings()[own] = {};
    for (int i = 0; i < int(Bindings().size()); ++i) if (PendingBindings()[i].epoch == p.epoch) {
        if (i != own || CurrentOwnBirth(PendingBindings()[i].entity)) {
            Bindings()[i] = PendingBindings()[i]; PendingBindings()[i] = {};
        } else if (PendingBindings()[i].entity.generation <= acknowledgedBirth) PendingBindings()[i] = {};
        // A future own binding can precede its SYSTEM receipt; retain it until that exact birth is acknowledged.
    }
}
void CFireSync::Receive(const Packets::Fires::Update& p) { if (CNetwork::m_bAuthenticated) Ledger().State(p.state); }
void CFireSync::Receive(const Packets::Fires::Remove& p) { if (CNetwork::m_bAuthenticated) Ledger().Remove(p.key); }
void CFireSync::Receive(const Packets::Fires::Bind& p) {
    if (!CNetwork::m_bAuthenticated || !p.entity.Valid() || p.epoch < Ledger().active.epoch) return;
    const int i = BindingIndex(p.entity.kind,p.entity.id); if (i < 0) return;
    const bool own = p.entity.kind == FireSync::Kind::Player && int(p.entity.id) == CNetworkPlayerManager::m_nMyId;
    if (own && (p.entity.generation <= ownBirthFloor ||
        (acknowledgedGame == gameGeneration && p.entity.generation < acknowledgedBirth))) return;
    const bool active = p.epoch == Ledger().active.epoch && (!own || CurrentOwnBirth(p.entity));
    auto& binding = active ? Bindings()[i] : PendingBindings()[i];
    if (binding.epoch > p.epoch || (binding.epoch == p.epoch &&
        (binding.entity.generation > p.entity.generation || (binding.entity.generation == p.entity.generation && binding.entity.ownerEpoch > p.entity.ownerEpoch)))) return;
    const bool same = FireSync::Matches(binding.entity,p.entity);
    if (same && !binding.live && p.live) return; // An equal-birth replay cannot undo an entity removal.
    binding = {p.entity,p.live,same ? binding.reference : -1,p.epoch};
    if (active && p.live) Resolve(p.entity); // Capture current full native reference when already ready.
}
void CFireSync::Receive(const Packets::Fires::Request& p) {
    if (IsHost() && p.request.Valid() && p.request.epoch == Ledger().active.epoch) ExecuteRequest(p.request);
}
void CFireSync::Process() {
    if (!CNetwork::m_bAuthenticated) { Reset(); return; }
    if (!Ready()) return;
    const uint32_t peerConnection = CNetwork::m_pPeer ? CNetwork::m_pPeer->connectID : 0;
    if (networkConnection != peerConnection) { networkConnection = peerConnection; gameGeneration = 1; controllingRestart = false; ownBirthFloor = 0; Reset(); }
    if (!gameGeneration) gameGeneration = 1;
    auto* ped = FindPlayerPed(0); const int ref = CPools::GetPedRef(ped); const uint32_t now = GetTickCount();
    if (!connection || ref != lastPedRef || now-lastHello > 2000) {
        Packets::Fires::Hello p; p.gameGeneration = gameGeneration; p.controllingRestart = controllingRestart;
        p.nativeReference = uint32_t(ref); p.model = ped->m_nModelIndex;
        const auto pos = ped->GetPosition(); p.position = {pos.x,pos.y,pos.z};
        GetPacketFactory().Send(p); lastHello = now; lastPedRef = ref;
    }
    if (!Ledger().active.epoch || expectedHost != Ledger().active.host || acknowledgedGame != gameGeneration) return;
    if (IsHost()) { Publish(); return; }
    if (!guestReconciled) {
        BeginReplay();
        for (auto& fire : gFireManager.m_aFires) if (fire.m_nFlags.bActive) {
            OriginalExtinguish(&fire); fire.m_nFlags.bCreatedByScript = false;
        }
        EndReplay(); guestReconciled = true;
    }
    BeginReplay(); ApplyReplicas(); EndReplay();
    if (!ped->m_pFire && !ped->m_nPedFlags.bInVehicle && !ped->m_nPhysicalFlags.bFireProof && !ped->m_pAttachedTo) {
        const auto pos = ped->GetPosition(); FireSync::Vec point{pos.x,pos.y,pos.z};
        for (const auto& slot : Ledger().active.slots) if (slot.live && slot.state.target.kind != FireSync::Kind::Vehicle &&
            FireSync::Distance2(point,slot.state.position) < 1.2f) {
            FireSync::Request r; r.intent = FireSync::Intent::Heat; r.fire = slot.state.key; r.target = Capture(ped); r.position = point;
            EmitRequest(r); break;
        }
    }
}
bool CFireSync::NativeStart(CEntity* creator,CEntity* target,const CVector& pos) {
    if (!Ready() || Replaying() || IsHost() || (!Ledger().active.epoch && CLocalPlayer::m_bIsHost)) return true;
    if (creator && Owned(creator)) {
        FireSync::Request r; r.intent = target ? FireSync::Intent::Attached : FireSync::Intent::Ground;
        r.creator = Capture(creator); r.target = Capture(target); r.position = {pos.x,pos.y,pos.z}; EmitRequest(r);
    }
    return false;
}
bool CFireSync::NativeWater(const CVector& pos,float radius,float strength) {
    if (!Ready() || Replaying() || IsHost()) return true;
    auto* ped = FindPlayerPed(0); auto* pad = CPad::GetPad(0);
    const bool localInput = pad && pad->NewState.ButtonCircle && (ped->m_aWeapons[ped->m_nActiveWeaponSlot].m_eWeaponType == WEAPON_EXTINGUISHER ||
        (ped->m_nPedFlags.bInVehicle && ped->m_pVehicle && ped->m_pVehicle->m_pDriver == ped &&
            (ped->m_pVehicle->m_nModelIndex == 407 || ped->m_pVehicle->m_nModelIndex == 544)));
    if (localInput && std::isfinite(radius) && std::isfinite(strength)) for (const auto& slot : Ledger().active.slots) if (slot.live) {
        FireSync::Request r; r.intent = FireSync::Intent::Water; r.fire = slot.state.key; r.position = {pos.x,pos.y,pos.z};
        r.radius = (std::min)(8.0f,(std::max)(0.0f,radius)); r.water = (std::min)(2.0f,(std::max)(0.0f,strength));
        if (FireSync::Distance2(r.position,slot.state.position) <= r.radius*r.radius) EmitRequest(r);
    }
    return false;
}
bool CFireSync::NativeExtinguish(CFire* fire) {
    const int index = FireIndex(fire); if (index < 0 || Replaying() || !Ready()) return true;
    for (auto& n : Native()) if (n.fire == fire && n.live) {
        if (n.replica) {
            if (n.attached && Owned(n.attached) && ValidNative(n.attached,n.reference)) {
                FireSync::Request r; r.intent = FireSync::Intent::Stop; r.fire = n.key;
                const auto p = fire->m_vecPosition; r.position = {p.x,p.y,p.z}; EmitRequest(r); n.suppressed = true;
            }
            n.live = false; fire->m_nFlags.bCreatedByScript = false; return true;
        }
        if (IsHost() && n.key.Valid() && n.key.sequence < FireSync::MaxCounter) {
            Packets::Fires::Remove p; p.key = n.key; ++p.key.sequence; GetPacketFactory().Send(p);
            n.key = p.key; n.live = false;
        }
        break;
    }
    return true;
}
void CFireSync::Created() { if (!Replaying()) Publish(); }
bool CFireSync::AllowPedDamage(CPed* ped) {
    if (!Ready()) return true;
    if (!ValidNative(ped) || !ped->m_pFire || FireIndex(ped->m_pFire) < 0) return false;
    const bool local = Owned(ped);
    if (IsHost() || (!Ledger().active.epoch && CLocalPlayer::m_bIsHost)) return local;
    for (const auto& n : Native()) if (n.live && n.replica && n.fire == ped->m_pFire && n.attached == ped && ValidNative(ped,n.reference)) {
        const auto& slot = Ledger().active.slots[n.key.id-1];
        return slot.live && FireSync::SameFire(n.key,slot.state.key) && Resolve(slot.state.target) == ped &&
            FireSync::AllowDamage(false,CNetworkPlayerManager::m_nMyId,slot.state.target,local,true,true);
    }
    return false;
}
void CFireSync::Tick(CFire* fire) {
    if (!Ready() || IsHost() || (!Ledger().active.epoch && CLocalPlayer::m_bIsHost)) {
        // Native ProcessFire directly writes car health as well as calling InflictDamage.
        struct Saved { CVehicle* car = nullptr; int ref = -1; float health = 0; bool proof = false; };
        std::array<Saved,FireSync::MaxEntities> saved{}; int count = 0;
        if (Ready()) for (auto* car : CNetworkVehicleManager::m_pVehicles) if (car && ValidNative(car->m_pVehicle) && !Owned(car->m_pVehicle) && count < FireSync::MaxEntities) {
            auto* v = car->m_pVehicle; saved[count++] = {v,Reference(v),v->m_fHealth,bool(v->m_nPhysicalFlags.bFireProof)};
            v->m_nPhysicalFlags.bFireProof = true;
        }
        OriginalProcess(fire);
        for (int i = 0; i < count; ++i) if (ValidNative(saved[i].car,saved[i].ref)) {
            saved[i].car->m_fHealth = saved[i].health; saved[i].car->m_nPhysicalFlags.bFireProof = saved[i].proof;
        }
        Publish(); return;
    }
    // No guest native ProcessFire: it combines spread, world damage, merge and local expiry.
    for (const auto& n : Native()) if (n.live && n.replica && n.fire == fire && n.attached && ValidNative(n.attached,n.reference)) {
        const auto& slot = Ledger().active.slots[n.key.id-1]; auto* target = Resolve(slot.state.target);
        if (!slot.live || !FireSync::SameFire(n.key,slot.state.key) || target != n.attached ||
            !FireSync::AllowDamage(false,CNetworkPlayerManager::m_nMyId,slot.state.target,Owned(target),true,true)) return;
        if (target->m_nType == ENTITY_TYPE_VEHICLE && !slot.state.script) {
            auto* v = static_cast<CVehicle*>(target); v->InflictDamage(nullptr,WEAPON_FTHROWER,CTimer::ms_fTimeStep*1.2f,CVector{});
        } // PED damage is exclusively the guarded native on-fire task, never duplicated here.
    }
}
void CFireSync::ExecuteRequest(const FireSync::Request& r) {
    if (!Resolve(r.issuer)) return; // Delayed intent cannot cross the issuer's game/ped incarnation.
    CEntity* creator = r.creator.kind == FireSync::Kind::World ? nullptr : Resolve(r.creator);
    CEntity* target = r.target.kind == FireSync::Kind::World ? nullptr : Resolve(r.target);
    if ((r.creator.kind != FireSync::Kind::World && !creator) || (r.target.kind != FireSync::Kind::World && !target)) return;
    const CVector pos{r.position.x,r.position.y,r.position.z};
    if (r.intent == FireSync::Intent::Ground || r.intent == FireSync::Intent::Attached || r.intent == FireSync::Intent::Heat) {
        for (const auto& f : gFireManager.m_aFires) if (f.m_nFlags.bActive &&
            ((target && f.m_pEntityTarget == target) || (!target && FireSync::Distance2(r.position,{f.m_vecPosition.x,f.m_vecPosition.y,f.m_vecPosition.z}) < 0.36f && f.m_pEntityCreator == creator))) return;
        if (target) gFireManager.StartFire(target,creator,0.8f,1,7000,100);
        else gFireManager.StartFire(pos,0.8f,1,creator,20000,3,1);
    } else {
        if (r.fire.epoch != Ledger().active.epoch || !r.fire.id || r.fire.id > FireSync::MaxFires) return;
        auto& n = Native()[r.fire.id-1]; if (!n.live || !FireSync::SameFire(n.key,r.fire)) return;
        if (r.intent == FireSync::Intent::Stop) n.fire->Extinguish();
        else gFireManager.ExtinguishPointWithWater(pos,r.radius,r.water);
    }
    Publish();
}
