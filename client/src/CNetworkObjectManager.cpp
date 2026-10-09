#include "stdafx.h"
#include "CNetworkObjectManager.h"
#include "COpCodeSync.h"
#include <CObject.h>
#include <CPools.h>
#include <CWeaponInfo.h>
#include <deque>
#include <map>
#include <tuple>

namespace
{
struct Hosted
{
    CObject* object = nullptr;
    int ref = -1;
    uint32_t token = 0, id = 0, revision = 1;
    ObjectSync::State state;
};
struct Replica { CObject* object = nullptr; int ref = -1; uint32_t revision = 0; };
enum class Kind { Create, Update, Remove, Opcode };
struct Pending
{
    Kind kind; uint32_t id = 0, revision = 0, waitSince = 0;
    ObjectSync::State state; std::vector<uint8_t> bytes;
    bool modelRequested = false;
    std::vector<int> introducedRequirements;
};
std::map<int, Hosted> g_hosted;
std::map<uint32_t, Replica> g_replicas;
std::deque<Pending> g_pending;
// Merged deletion intervals retain tombstones without one allocation per bottle.
std::map<uint32_t, uint32_t> g_retired;
uint32_t g_nextToken = 1; // NEVER reset by Clear, mission completion or retry.
DWORD g_lastPublish = 0;
bool g_authenticated = false, g_host = false, g_clearing = false, g_replaying = false;
uint32_t g_connectId = 0;
bool g_mission = false, g_requestResync = false;

bool Retired(uint32_t id)
{
    auto it = g_retired.upper_bound(id);
    return it != g_retired.begin() && id <= std::prev(it)->second;
}
void Retire(uint32_t id)
{
    if (!id || id > ObjectSync::MAX_ID) return;
    auto it = g_retired.lower_bound(id);
    uint32_t first = id, last = id;
    if (it != g_retired.begin())
    {
        auto previous = std::prev(it);
        if (previous->second >= id) return;
        if (previous->second + 1 == id) { first = previous->first; g_retired.erase(previous); }
    }
    if (it != g_retired.end() && it->first == id + 1)
    { last = it->second; g_retired.erase(it); }
    g_retired[first] = last;
}
CObject* Resolve(int ref, CObject* expected)
{
    auto* pool = CPools::ms_pObjectPool;
    if (!pool || ref < 0) return nullptr;
    auto* object = pool->GetAtRef(ref);
    return object && object == expected ? object : nullptr;
}
ObjectSync::Vec3 Vector(const CVector& value) { return {value.x, value.y, value.z}; }
CVector Vector(const ObjectSync::Vec3& value) { return {value.x, value.y, value.z}; }
float LengthSquared(const CVector& v) { return v.x * v.x + v.y * v.y + v.z * v.z; }
ObjectSync::State Capture(CObject* object)
{
    ObjectSync::State s;
    s.model = object->m_nModelIndex; s.area = object->m_nAreaCode;
    s.position = Vector(object->GetPosition());
    object->GetOrientation(s.rotation.x, s.rotation.y, s.rotation.z);
    s.velocity = Vector(object->m_vecMoveSpeed); s.turnSpeed = Vector(object->m_vecTurnSpeed);
    s.health = std::clamp(object->m_fHealth, 0.0f, 100000.0f); s.scale = object->m_fScale;
    s.collision = object->m_bUsesCollision; s.visible = object->m_bIsVisible;
    s.dynamic = !object->m_bIsStatic; s.targetable = object->m_nObjectFlags.bIsTargatable;
    s.lastWeaponDamage = object->m_nLastWeaponDamage;
    s.bulletProof = object->m_nPhysicalFlags.bBulletProof;
    s.fireProof = object->m_nPhysicalFlags.bFireProof;
    s.collisionProof = object->m_nPhysicalFlags.bCollisionProof;
    s.meleeProof = object->m_nPhysicalFlags.bMeleeProof;
    s.explosionProof = object->m_nPhysicalFlags.bExplosionProof;
    s.playerOnlyDamage = object->m_nPhysicalFlags.bInvulnerable;
    return s;
}
bool Same(const ObjectSync::State& a, const ObjectSync::State& b)
{
    auto vec = [](const ObjectSync::Vec3& x, const ObjectSync::Vec3& y)
    { return x.x == y.x && x.y == y.y && x.z == y.z; };
    return vec(a.position, b.position) && vec(a.rotation, b.rotation)
        && vec(a.velocity, b.velocity) && vec(a.turnSpeed, b.turnSpeed)
        && std::tie(a.health,a.scale,a.area,a.lastWeaponDamage,a.collision,a.visible,a.dynamic,a.targetable,
            a.bulletProof,a.fireProof,a.collisionProof,a.meleeProof,a.explosionProof,a.playerOnlyDamage)
        == std::tie(b.health,b.scale,b.area,b.lastWeaponDamage,b.collision,b.visible,b.dynamic,b.targetable,
            b.bulletProof,b.fireProof,b.collisionProof,b.meleeProof,b.explosionProof,b.playerOnlyDamage);
}
void RequestReplicaModel(Pending& event)
{
    if (event.modelRequested) return;
    event.modelRequested = true;
    // Native DFF requests can recursively request TXDs and their parents. Record
    // only GAME flags introduced by this synchronous request, never MISSION flags.
    std::vector<uint8_t> txdFlags(5000);
    for (int i = 0; i < 5000; ++i) txdFlags[i] = CStreaming::ms_aInfoForModel[20000 + i].m_nFlags;
    auto oldFlags = CStreaming::ms_aInfoForModel[event.state.model].m_nFlags;
    CStreaming::RequestModel(event.state.model, eStreamingFlags::GAME_REQUIRED);
    if (!(oldFlags & GAME_REQUIRED)) event.introducedRequirements.push_back(event.state.model);
    for (int i = 0; i < 5000; ++i)
        if (!(txdFlags[i] & GAME_REQUIRED) && (CStreaming::ms_aInfoForModel[20000 + i].m_nFlags & GAME_REQUIRED))
            event.introducedRequirements.push_back(20000 + i);
}
void ReleaseReplicaModel(Pending& event)
{
    for (int model : event.introducedRequirements) CStreaming::SetModelIsDeletable(model);
    event.introducedRequirements.clear();
}
void ClearPending()
{
    for (auto& event : g_pending) ReleaseReplicaModel(event);
    g_pending.clear();
}
void GuardReplica(CObject* object)
{
    // Replicas are render/collision representations, never damage authorities.
    object->m_nPhysicalFlags.bBulletProof = true;
    object->m_nPhysicalFlags.bFireProof = true;
    object->m_nPhysicalFlags.bCollisionProof = true;
    object->m_nPhysicalFlags.bMeleeProof = true;
    object->m_nPhysicalFlags.bExplosionProof = true;
    object->m_nPhysicalFlags.bDisableCollisionForce = true;
    object->m_nPhysicalFlags.bDontApplySpeed = true;
    object->m_nPhysicalFlags.bApplyGravity = false;
}
void Apply(CObject* object, const ObjectSync::State& s)
{
    CWorld::Remove(object);
    object->SetPosn(Vector(s.position));
    object->SetOrientation(s.rotation.x, s.rotation.y, s.rotation.z);
    object->SetIsStatic(!s.dynamic);
    object->m_vecMoveSpeed = Vector(s.velocity); object->m_vecTurnSpeed = Vector(s.turnSpeed);
    object->m_fHealth = s.health; object->m_fScale = s.scale;
    object->m_nAreaCode = s.area; object->m_nLastWeaponDamage = s.lastWeaponDamage;
    object->m_bUsesCollision = s.collision; object->m_bIsVisible = s.visible;
    object->SetObjectTargettable(s.targetable);
    GuardReplica(object);
    object->UpdateRwMatrix(); object->UpdateRwFrame();
    CWorld::Add(object);
}
void DeleteReplica(uint32_t id)
{
    auto it = g_replicas.find(id);
    if (it == g_replicas.end()) return;
    auto entry = it->second; g_replicas.erase(it);
    if (Resolve(entry.ref, entry.object)) Command<Commands::DELETE_OBJECT>(entry.ref);
}
void Publish(Hosted& entry, bool force = false)
{
    auto* object = Resolve(entry.ref, entry.object);
    if (!object || entry.revision == 0x7fffffff) return;
    auto state = Capture(object);
    if (!state.Valid() || (!force && Same(state, entry.state))) return;
    entry.state = state;
    Packets::Objects::Update packet;
    packet.id = entry.token; packet.revision = ++entry.revision; packet.state = state;
    GetPacketFactory().Send(packet);
}
bool EnsureSession()
{
    if (!CNetwork::m_bAuthenticated)
    {
        if (g_authenticated) CNetworkObjectManager::Clear();
        g_authenticated = false;
        return false;
    }
    const bool host = CLocalPlayer::m_bIsHost;
    const uint32_t connectId = CNetwork::m_pPeer ? CNetwork::m_pPeer->connectID : 0;
    if (!g_authenticated || host != g_host || connectId != g_connectId)
    {
        CNetworkObjectManager::Clear();
        g_authenticated = true; g_host = host; g_connectId = connectId;
        if (!host) g_requestResync = true;
    }
    return true;
}
void Enqueue(Pending event)
{
    if (!EnsureSession() || g_host) return;
    if (g_pending.size() >= ObjectSync::MAX_PENDING)
    {
        // Fail closed and resnapshot live authority state, rather than dropping
        // one removal while leaving a permanently stale replica behind.
        g_clearing = true;
        while (!g_replicas.empty()) DeleteReplica(g_replicas.begin()->first);
        g_clearing = false; ClearPending(); g_requestResync = true;
        logger::warn("Object execution queue overflow; requesting authority resnapshot");
        return;
    }
    event.waitSince = GetTickCount(); g_pending.push_back(std::move(event));
}
}

void CNetworkObjectManager::ObserveOpcode(uint16_t opcode, const int* inputs, int count, int resultHandle)
{
    if (!EnsureSession() || !g_host || !CPools::ms_pObjectPool) return;
    if (ObjectSync::IsCreate(opcode))
    {
        auto* object = CPools::ms_pObjectPool->GetAtRef(resultHandle);
        if (!object || g_hosted.count(resultHandle) || g_hosted.size() >= ObjectSync::MAX_OBJECTS
            || g_nextToken > ObjectSync::MAX_ID) return;
        Hosted entry; entry.ref = resultHandle; entry.object = object;
        entry.state = Capture(object);
        if (!entry.state.Valid()) return;
        entry.token = g_nextToken++;
        g_hosted.emplace(resultHandle, entry);
        Packets::Objects::Create packet; packet.id = entry.token; packet.state = entry.state;
        GetPacketFactory().Send(packet);
        g_mission = true;
    }
    else if (count && ObjectSync::ObjectOperand(opcode) == 0)
    {
        auto it = g_hosted.find(inputs[0]);
        // Existing globals/unrelated fragments are deliberately NOT adopted.
        if (it != g_hosted.end()) Publish(it->second);
    }
}
void CNetworkObjectManager::BeforeDelete(CObject* object)
{
    if (g_clearing || !object) return;
    for (auto it = g_hosted.begin(); it != g_hosted.end(); ++it)
    {
        if (it->second.object != object) continue;
        auto token = it->second.token; g_hosted.erase(it);
        if (CNetwork::m_bAuthenticated && CLocalPlayer::m_bIsHost)
        { Packets::Objects::Remove packet; packet.id = token; GetPacketFactory().Send(packet); }
        return;
    }
    for (auto it = g_replicas.begin(); it != g_replicas.end(); ++it)
        if (it->second.object == object) { g_replicas.erase(it); return; }
}
int CNetworkObjectManager::GetHostToken(int ref)
{
    auto it = g_hosted.find(ref);
    return it != g_hosted.end() && Resolve(ref, it->second.object) ? int(it->second.token) : -1;
}
int CNetworkObjectManager::GetHandle(uint32_t id)
{
    if (!id || id > ObjectSync::MAX_ID || Retired(id)) return -1;
    for (const auto& item : g_hosted)
        if (item.second.id == id && Resolve(item.first, item.second.object)) return item.first;
    auto it = g_replicas.find(id);
    return it != g_replicas.end() && Resolve(it->second.ref, it->second.object) ? it->second.ref : -1;
}
int CNetworkObjectManager::GetNetworkId(CEntity* object)
{
    if (!object || object->m_nType != ENTITY_TYPE_OBJECT) return -1;
    for (const auto& item : g_hosted)
        if (item.second.object == object && Resolve(item.first, item.second.object)) return item.second.id ? int(item.second.id) : -1;
    for (const auto& item : g_replicas)
        if (item.second.object == object && Resolve(item.second.ref, item.second.object)) return int(item.first);
    return -1;
}
void CNetworkObjectManager::Confirm(uint32_t token, uint32_t id)
{
    if (!EnsureSession() || !g_host || !id || id > ObjectSync::MAX_ID) return;
    for (auto& item : g_hosted)
        if (item.second.token == token && Resolve(item.first, item.second.object))
        { if (!item.second.id || item.second.id == id) item.second.id = id; return; }
    // Late confirmation after deletion cannot adopt a recycled pool slot.
}
void CNetworkObjectManager::ReceiveCreate(const Packets::Objects::Create& p)
{
    if (!p.state.Valid() || !p.id || p.id > ObjectSync::MAX_ID || !p.revision || p.revision > 0x7fffffff) return;
    Pending event; event.kind = Kind::Create; event.id = p.id;
    event.revision = p.revision; event.state = p.state; Enqueue(std::move(event));
}
void CNetworkObjectManager::ReceiveUpdate(const Packets::Objects::Update& p)
{
    if (!p.state.Valid() || !p.id || p.id > ObjectSync::MAX_ID || !p.revision || p.revision > 0x7fffffff) return;
    Pending event; event.kind = Kind::Update; event.id = p.id;
    event.revision = p.revision; event.state = p.state; Enqueue(std::move(event));
}
void CNetworkObjectManager::ReceiveRemove(uint32_t id)
{
    if (!id || id > ObjectSync::MAX_ID) return;
    Pending event; event.kind = Kind::Remove; event.id = id; Enqueue(std::move(event));
}
bool CNetworkObjectManager::QueueOpcode(const uint8_t* bytes, int size)
{
    if (g_replaying) return false;
    if (size < 0 || !ObjectSync::ValidOpcode(bytes, size)) return true;
    Pending event; event.kind = Kind::Opcode; event.bytes.assign(bytes, bytes + size);
    Enqueue(std::move(event)); return true;
}
void CNetworkObjectManager::Clear()
{
    g_clearing = true;
    while (!g_replicas.empty()) DeleteReplica(g_replicas.begin()->first);
    g_clearing = false; ClearPending(); g_hosted.clear(); g_retired.clear();
    g_mission = false; g_requestResync = false;
    g_authenticated = false;
    // g_nextToken must remain monotonic across this operation.
}
void CNetworkObjectManager::Process()
{
    if (!EnsureSession()) return;
    if (g_requestResync)
    { Packets::Objects::Resync packet; GetPacketFactory().Send(packet); g_requestResync = false; }
    if (g_host)
    {
        if (g_mission && !CTheScripts::IsPlayerOnAMission())
        {
            for (const auto& item : g_hosted)
            { Packets::Objects::Remove p; p.id = item.second.token; GetPacketFactory().Send(p); }
            g_hosted.clear(); g_mission = false;
        }
        if (GetTickCount() - g_lastPublish < 100) return;
        g_lastPublish = GetTickCount();
        for (auto it = g_hosted.begin(); it != g_hosted.end();)
        {
            if (!Resolve(it->first, it->second.object))
            { Packets::Objects::Remove p; p.id = it->second.token; GetPacketFactory().Send(p); it = g_hosted.erase(it); }
            else { Publish(it->second); ++it; }
        }
        return;
    }
    for (size_t count = 0; count < 64 && !g_pending.empty(); ++count)
    {
        auto& p = g_pending.front();
        if (p.kind == Kind::Create && !Retired(p.id))
        {
            auto it = g_replicas.find(p.id);
            if (it == g_replicas.end())
            {
                if (g_replicas.size() >= ObjectSync::MAX_OBJECTS || !CModelInfo::ms_modelInfoPtrs[p.state.model])
                { ReleaseReplicaModel(p); Retire(p.id); g_pending.pop_front(); continue; }
                auto modelType = CModelInfo::ms_modelInfoPtrs[p.state.model]->GetModelType();
                if (modelType != MODEL_INFO_ATOMIC && modelType != MODEL_INFO_TIME)
                { ReleaseReplicaModel(p); Retire(p.id); g_pending.pop_front(); continue; }
                RequestReplicaModel(p);
                if (CStreaming::ms_aInfoForModel[p.state.model].m_nLoadState != LOADSTATE_LOADED || !CPools::ms_pObjectPool)
                {
                    if (GetTickCount() - p.waitSince < 5000) break;
                    logger::warn("Object model %u was not ready within the replica creation timeout", unsigned(p.state.model));
                    ReleaseReplicaModel(p); Retire(p.id); g_pending.pop_front(); continue;
                }
                // Native allocation uses the object pool, not the C++ process heap.
                auto* object = plugin::CallAndReturn<CObject*, 0x5A1F60, int, bool>(p.state.model, false);
                if (!object) { ReleaseReplicaModel(p); Retire(p.id); g_pending.pop_front(); continue; }
                object->m_nObjectType = OBJECT_MISSION;
                int ref = CPools::GetObjectRef(object);
                CWorld::Add(object);
                g_replicas.emplace(p.id, Replica{object, ref, p.revision});
                Apply(object, p.state);
            }
            else if (p.revision > it->second.revision && Resolve(it->second.ref, it->second.object))
            { Apply(it->second.object, p.state); it->second.revision = p.revision; }
        }
        else if (p.kind == Kind::Update && !Retired(p.id))
        {
            auto it = g_replicas.find(p.id);
            if (it != g_replicas.end() && p.revision > it->second.revision && Resolve(it->second.ref, it->second.object))
            { Apply(it->second.object, p.state); it->second.revision = p.revision; }
        }
        else if (p.kind == Kind::Remove)
        { Retire(p.id); DeleteReplica(p.id); }
        else if (p.kind == Kind::Opcode)
        {
            g_replaying = true;
            COpCodeSync::HandlePacket(p.bytes.data(), int(p.bytes.size()));
            g_replaying = false;
            for (const auto& item : g_replicas)
                if (auto* object = Resolve(item.second.ref, item.second.object)) GuardReplica(object);
        }
        ReleaseReplicaModel(p);
        g_pending.pop_front();
    }
}
void CNetworkObjectManager::ReceiveHit(const Packets::Objects::Hit& p)
{
    if (!EnsureSession() || !g_host || p.weapon < 22 || p.weapon > 34
        || !p.origin.Valid(20000) || !p.impact.Valid(20000)) return;
    int ref = GetHandle(p.id);
    auto it = g_hosted.find(ref);
    auto* player = CNetworkPlayerManager::GetPlayer(p.playerId);
    if (it == g_hosted.end() || !player || !IsPedPointerValid(player->m_pPed)) return;
    auto* object = Resolve(ref, it->second.object);
    if (!object || !object->m_bUsesCollision || player->m_pPed->m_nAreaCode != object->m_nAreaCode) return;
    auto origin = Vector(p.origin), impact = Vector(p.impact);
    auto* info = CWeaponInfo::GetWeaponInfo(eWeaponType(p.weapon), WEAPSKILL_STD);
    if (!info || info->m_nDamage <= 0 || LengthSquared(origin - player->m_pPed->GetPosition()) > 100.0f
        || LengthSquared(impact - origin) > (info->m_fWeaponRange + 5.0f) * (info->m_fWeaponRange + 5.0f)) return;
    auto direction = impact - origin;
    if (LengthSquared(direction) < 0.000001f) return;
    direction.Normalise();
    CColPoint collision{}; CEntity* victim = nullptr;
    auto* ignored = CWorld::pIgnoreEntity; CWorld::pIgnoreEntity = player->m_pPed;
    bool hit = CWorld::ProcessLineOfSight(origin, impact + direction * 0.5f, collision, victim,
        true, true, true, true, true, false, false, true);
    CWorld::pIgnoreEntity = ignored;
    if (!hit || victim != object || !FindPlayerPed(0)) return;
    // Match native DoBulletImpact's breakable-object damage calculation.
    // Low-effect physics objects are outside this bottle-target contract.
    auto* objectInfo = object->m_pObjectInfo;
    if (!objectInfo || object->m_nColDamageEffect < 200) return;
    float damage = objectInfo->m_nGunBreakMode == 1 ? 151.0f
        : objectInfo->m_nGunBreakMode == 2 ? 151.0f * objectInfo->m_fSmashMultiplier : 50.0f;
    if (!std::isfinite(damage) || damage <= 0) return;
    // Authenticated co-op hits are eligible player damage. The original native
    // object state and destruction drive the host's mission damage query.
    object->ObjectDamage(damage, &impact, &collision.m_vecNormal, FindPlayerPed(0), eWeaponType(p.weapon));
    it = g_hosted.find(ref);
    if (it != g_hosted.end() && it->second.id == p.id && Resolve(ref, object)) Publish(it->second, true);
}
void CNetworkObjectManager::Init()
{
    Events::objectDtorEvent.before += [](CObject* object) { BeforeDelete(object); };
    Events::gameProcessEvent.after += [] { Process(); };
    // Release replicas and pending streaming pins while world/pools are alive.
    gameShutdownEvent.before += [] { Clear(); };
    Events::shutdownRwEvent.before += [] { Clear(); };
}
