#include "stdafx.h"
#include "CPlayerAnimationSync.h"
#include "CPlayerVitalsSync.h"
#include "CServerTime.h"
#include "network/packets/players.h"
#include <CAnimManager.h>
#include <CAnimBlendAssociation.h>
#include <CAnimBlendHierarchy.h>
#include <CAnimBlock.h>
#include <CStreaming.h>
#include <common.h>

namespace
{
bool scriptsReady = false, session = false, needBirth = true, respawnBaseline = false;
uint32_t connection = 0, localGeneration = 0, acknowledgedBirth = 0, lastSend = 0;
PlayerAnimation::OwnerClock ownerClock;
PlayerAnimation::Life localLife, acknowledgedLife, publishedLife;
PlayerAnimation::State lastVisual;
RpClump* localClump = nullptr;
bool published = false;
CAnimBlendAssociation* sourceAssociation = nullptr;
CAnimBlendHierarchy* sourceHierarchy = nullptr;
uint32_t visualInstance = 0;
float sourcePhase = 0;
struct Remote
{
    PlayerAnimation::Cache current;
    Packets::Players::PlayerAnimationState pending;
    Packets::Players::RespawnPlayer reset;
    bool hasPending = false, hasReset = false;
    CAnimBlendAssociation* owned = nullptr;
    CAnimBlendHierarchy* hierarchy = nullptr;
    RpClump* clump = nullptr;
    int reference = -1, model = -1, pose = 0;
    uint32_t appliedSequence = 0, blockedInstance = 0, ownedInstance = 0, resetBirth = 0;
    uint32_t retiredGeneration = 0;
};
Remote remotes[PlayerAnimation::MAX_PLAYERS];
void OwnedDeleted(CAnimBlendAssociation* association, void* data)
{
    auto& remote = *static_cast<Remote*>(data);
    if (remote.owned != association) return;
    remote.blockedInstance = remote.ownedInstance;
    remote.owned = nullptr; remote.hierarchy = nullptr;
}

bool LocalReady(CPlayerPed* ped)
{
    return scriptsReady && gGameState == 9 && CWorld::PlayerInFocus == 0 && ped
        && CPools::ms_pPedPool && CPools::ms_pPedPool->IsObjectValid(ped)
        && CWorld::Players[0].m_pPed == ped && ped->m_pPlayerData && ped->m_pRwClump;
}
bool VisualReady(CPlayerPed* ped)
{
    // Never install a visual over mission, swimming, vehicle or combat tasks.
    const bool mission = CTheScripts::OnAMissionFlag
        && CTheScripts::ScriptSpace[CTheScripts::OnAMissionFlag];
    return ped && !mission && ped->m_fHealth > 0 && !ped->m_nPedFlags.bInVehicle
        && !ped->m_nPhysicalFlags.bSubmergedInWater && !CUtil::IsDucked(ped)
        && ped->m_nMoveState == PEDMOVE_STILL
        && (ped->m_ePedState == PEDSTATE_IDLE || ped->m_ePedState == PEDSTATE_CHAT);
}
bool Contains(RpClump* clump, CAnimBlendAssociation* association)
{
    if (!clump || !association) return false;
    int count = 0;
    for (auto* it = RpAnimBlendClumpGetFirstAssociation(clump); it && count++ < 128;
        it = RpAnimBlendGetNextAssociation(it)) if (it == association) return true;
    return false;
}
void Fade(Remote& remote)
{
    auto* ped = remote.reference >= 0 && CPools::ms_pPedPool ? CPools::GetPed(remote.reference) : nullptr;
    // Resolve the full pool reference before touching either clump or association.
    if (ped && ped->m_pRwClump == remote.clump && ped->m_nModelIndex == remote.model
        && Contains(remote.clump, remote.owned) && remote.owned->m_pHierarchy == remote.hierarchy
        && remote.owned->m_nCallbackType == ANIMBLENDCALLBACK_DELETE
        && remote.owned->m_pCallbackFunc == OwnedDeleted && remote.owned->m_pCallbackData == &remote)
    {
        remote.owned->m_fBlendDelta = -8.f;
        remote.owned->m_bUnlockLastFrame = true;
    }
    remote.owned = nullptr; remote.hierarchy = nullptr; remote.clump = nullptr;
    remote.reference = -1; remote.pose = 0; remote.appliedSequence = 0;
}
bool EnsureSession()
{
    if (!CNetwork::m_pPeer) return false;
    const uint32_t id = CNetwork::m_pPeer->connectID;
    if (!session || connection != id)
    {
        CPlayerAnimationSync::Reset(); session = true; connection = id;
    }
    // Receive may precede SYSTEM handshake; preserve this peer's bounded queue.
    return true;
}
PlayerAnimation::State Observe(CPlayerPed* ped)
{
    PlayerAnimation::State result;
    CAnimBlendAssociation* selected = nullptr;
    if (!VisualReady(ped)) { sourceAssociation = nullptr; sourceHierarchy = nullptr; return result; }
    int count = 0;
    for (auto* it = RpAnimBlendClumpGetFirstAssociation(ped->m_pRwClump); it && count++ < 128;
        it = RpAnimBlendGetNextAssociation(it))
    {
        const int pose = PlayerAnimation::Pose(it->m_nAnimGroup, it->m_nAnimId);
        if (!pose || !it->m_pHierarchy || it->m_fBlendDelta < 0 || it->m_fBlendAmount <= .01f) continue;
        PlayerAnimation::State state{pose, it->m_fCurrentTime, it->m_pHierarchy->m_fTotalTime,
            it->m_fSpeed, it->m_fBlendAmount, bool(it->m_bLooped), 1};
        if (state.Valid() && (it->m_bPlaying || state.loop) && state.blend >= result.blend)
        { result = state; selected = it; }
    }
    if (!selected) { sourceAssociation = nullptr; sourceHierarchy = nullptr; return result; }
    if (selected != sourceAssociation || selected->m_pHierarchy != sourceHierarchy
        || (!result.loop && result.phase + .05f < sourcePhase))
    {
        if (visualInstance == PlayerAnimation::MAX_COUNTER) return {};
        ++visualInstance;
    }
    sourceAssociation = selected; sourceHierarchy = selected->m_pHierarchy; sourcePhase = result.phase;
    result.instance = visualInstance; return result;
}
bool RefreshLocal(CPlayerPed* ped)
{
    if (!LocalReady(ped)) return false;
    const int reference = CPools::GetPedRef(ped);
    if (reference < 0 || CPools::GetPed(reference) != ped) return false;
    if (needBirth || (!respawnBaseline && (reference != localLife.nativeReference || ped->m_nModelIndex != localLife.model || ped->m_pRwClump != localClump)))
    {
        if (!ownerClock.NewBirth()) return false;
        acknowledgedBirth = 0; needBirth = false; published = false; sourceAssociation = nullptr; sourceHierarchy = nullptr;
    }
    respawnBaseline = false; localClump = ped->m_pRwClump;
    localLife = {localGeneration, ownerClock.birth, ownerClock.sequence, int(ped->m_nModelIndex), int(ped->m_nAreaCode), reference};
    return localLife.Valid();
}
void ProcessRemote(int id)
{
    auto& remote = remotes[id];
    auto* player = CNetworkPlayerManager::GetPlayer(id);
    if (!player || !player->m_vitals.generation) return;
    const uint32_t generation = player->m_vitals.generation;
    if (remote.current.hasLife && remote.current.life.generation != generation)
    { Fade(remote); remote.current = {}; remote.blockedInstance = remote.resetBirth = 0; }
    if (remote.hasReset && remote.reset.life.generation < generation) remote.hasReset = false;
    if (remote.hasPending && remote.pending.life.generation < generation) remote.hasPending = false;
    if (!scriptsReady || gGameState != 9 || CWorld::PlayerInFocus != 0 || !CPools::ms_pPedPool) return;
    if (remote.hasReset && remote.reset.life.generation == generation)
    {
        const auto life = remote.reset.life;
        // A newer frame may already be queued, but recreation precedes its apply.
        if (life.birth > remote.resetBirth && (!remote.current.hasLife || life.birth >= remote.current.life.birth))
        {
            Fade(remote);
            player->Respawn();
            if (!CPlayerVitalsSync::HasBoundPed(player)) return; // Temporary pool/model exhaustion retries.
            remote.current.Accept(life, {}, remote.reset.serverTime);
            remote.resetBirth = life.birth;
        }
        remote.hasReset = false;
    }
    if (remote.hasPending && remote.pending.life.generation == generation)
    {
        const auto& packet = remote.pending;
        if (remote.current.hasLife && packet.life.birth > remote.current.life.birth) Fade(remote);
        if (remote.current.Accept(packet.life, packet.state, packet.sampledAt)) remote.hasPending = false;
        else if (remote.current.hasLife && packet.life.sequence <= remote.current.life.sequence) remote.hasPending = false;
    }
    if (!remote.current.hasLife || !CPlayerVitalsSync::HasBoundPed(player)) return;
    auto* ped = player->m_pPed;
    if (!ped->m_pRwClump) return;
    const bool changedNative = remote.reference >= 0 && (remote.reference != player->m_nPedRef
        || remote.clump != ped->m_pRwClump || remote.model != ped->m_nModelIndex);
    if (changedNative)
    {
        if (remote.current.state.instance) remote.blockedInstance = remote.current.state.instance;
        Fade(remote); remote.current.state = {}; // Do not adopt a cached pose on a recreated actor.
        return;
    }
    float phase = 0;
    if (ped->m_nModelIndex != remote.current.life.model || ped->m_nAreaCode != remote.current.life.area)
    { Fade(remote); return; } // Await independently delivered model/area readiness.
    if (!VisualReady(ped) || !PlayerAnimation::Phase(remote.current.state, remote.current.sampledAt, g_serverTime, phase))
    { if (remote.current.state.instance) remote.blockedInstance = remote.current.state.instance; Fade(remote); return; }
    const auto& state = remote.current.state;
    if (state.instance == remote.blockedInstance) return;
    if (remote.owned && (!Contains(ped->m_pRwClump, remote.owned) || remote.owned->m_pHierarchy != remote.hierarchy
        || remote.owned->m_nCallbackType != ANIMBLENDCALLBACK_DELETE
        || remote.owned->m_pCallbackFunc != OwnedDeleted || remote.owned->m_pCallbackData != &remote))
    { remote.blockedInstance = remote.ownedInstance; remote.owned = nullptr; remote.hierarchy = nullptr;
      if (state.instance == remote.blockedInstance) return; }
    if (remote.owned && (remote.pose != state.pose || remote.ownedInstance != state.instance)) Fade(remote);
    if (!remote.owned)
    {
        const int group = PlayerAnimation::Group(state.pose), anim = PlayerAnimation::Animation(state.pose);
        const char* name = CAnimManager::GetAnimBlockName(group);
        auto* block = name ? CAnimManager::GetAnimationBlock(name) : nullptr;
        const int index = name ? CAnimManager::GetAnimationBlockIndex(name) : -1;
        if (!block || index < 0) return;
        if (!block->bLoaded)
        {
            // No manager-owned required flags survive retry; live associations hold a block ref.
            CStreaming::RequestModel(25575 + index, 0); return;
        }
        auto* association = CAnimManager::AddAnimation(ped->m_pRwClump, group, anim);
        if (!association) return;
        remote.owned = association; remote.hierarchy = association->m_pHierarchy;
        association->SetDeleteCallback(OwnedDeleted, &remote);
        remote.clump = ped->m_pRwClump; remote.reference = player->m_nPedRef;
        remote.model = ped->m_nModelIndex; remote.pose = state.pose; remote.ownedInstance = state.instance;
        association->m_bEnableMovement = false; association->m_bTranslateX = false;
        association->m_bTranslateY = false; association->m_bIndestructible = false;
        association->m_bFreezeLastFrame = false; association->m_bUnlockLastFrame = true;
    }
    if (remote.appliedSequence != remote.current.life.sequence)
    {
        remote.owned->SetCurrentTime(phase);
        remote.owned->m_fSpeed = state.speed;
        remote.owned->m_bLooped = state.loop; remote.owned->m_bPlaying = true;
        remote.owned->SetBlend(state.blend, 0.f);
        remote.appliedSequence = remote.current.life.sequence;
    }
}
void ConsumeLocalAcknowledgement()
{
    const int id = CNetworkPlayerManager::m_nMyId;
    if (id < 0 || id >= PlayerAnimation::MAX_PLAYERS) return;
    auto& pending = remotes[id];
    if (!pending.hasPending) return;
    const auto& life = pending.pending.life;
    if (!localGeneration || localGeneration == life.generation)
    {
        localGeneration = life.generation;
        if (life.ready && life.birth == ownerClock.birth && life.sequence <= ownerClock.sequence
            && life.sequence >= acknowledgedLife.sequence)
        { acknowledgedBirth = life.birth; acknowledgedLife = life; }
    }
    pending.hasPending = false;
}

void PublishLocal()
{
    auto* ped = FindPlayerPed(0);
    if (!RefreshLocal(ped)) return;
    const auto state = Observe(ped);
    const uint32_t now = GetTickCount();
    const bool changed = localLife.area != publishedLife.area || state.pose != lastVisual.pose || state.loop != lastVisual.loop || state.instance != lastVisual.instance;
    if (published && !changed && now - lastSend < (state.pose ? 250u : 2000u)) return;
    if (!ownerClock.Next()) return;
    localLife.sequence = ownerClock.sequence;
    Packets::Players::PlayerAnimationState packet;
    packet.playerid = CNetworkPlayerManager::m_nMyId; packet.life = localLife;
    packet.life.generation = 0; packet.state = state;
    GetPacketFactory().Send(packet);
    lastVisual = state; publishedLife = localLife; lastSend = now; published = true;
}

void InvalidateNative()
{
    // Publish the scene boundary before any subsequent owner-controlled operation.
    if (CNetwork::m_bAuthenticated && session && localLife.Valid() && ownerClock.NewBirth())
    {
        Packets::Players::RespawnPlayer packet;
        packet.life = localLife; packet.life.birth = ownerClock.birth; packet.life.sequence = ownerClock.sequence;
        packet.life.generation = 0; packet.life.ready = false;
        GetPacketFactory().Send(packet);
        needBirth = false; respawnBaseline = true;
    }
    else needBirth = true;
    scriptsReady = false; acknowledgedBirth = 0; published = false; sourceAssociation = nullptr; sourceHierarchy = nullptr;
    for (auto& remote : remotes)
    {
        if (remote.current.state.instance) remote.blockedInstance = remote.current.state.instance;
        Fade(remote); remote.current.state = {};
        // Initial menu replay has never been applied in this native scene.
        // Preserve that bounded future state through the first script init.
        if (remote.current.hasLife)
        {
            const auto& life = remote.current.life;
            if (remote.hasPending && remote.pending.life.generation <= life.generation
                && (remote.pending.life.generation < life.generation || remote.pending.life.birth <= life.birth))
                remote.hasPending = false;
            if (remote.hasReset && remote.reset.life.generation <= life.generation
                && (remote.reset.life.generation < life.generation || remote.reset.life.birth <= life.birth))
                remote.hasReset = false;
        }
    }
}
}
void CPlayerAnimationSync::Init()
{
    Events::initScriptsEvent.before += [] { InvalidateNative(); };
    Events::processScriptsEvent.after += [] { if (gGameState == 9) scriptsReady = true; };
    gameShutdownEvent.before += [] { InvalidateNative(); };
    Events::shutdownRwEvent.before += [] { InvalidateNative(); };
}
void CPlayerAnimationSync::Reset()
{
    for (auto& remote : remotes) { Fade(remote); remote = {}; }
    ownerClock = {}; localLife = {}; acknowledgedLife = {}; publishedLife = {}; localGeneration = acknowledgedBirth = lastSend = 0;
    lastVisual = {}; localClump = nullptr; sourceAssociation = nullptr; sourceHierarchy = nullptr; visualInstance = 0; session = false; needBirth = true; published = respawnBaseline = false;
}
void CPlayerAnimationSync::ForgetPlayer(int id, uint32_t generation)
{
    if (id < 0 || id >= PlayerAnimation::MAX_PLAYERS || !generation) return;
    auto& remote = remotes[id];
    remote.retiredGeneration = std::max(remote.retiredGeneration, generation);
    if (remote.current.hasLife && remote.current.life.generation <= generation)
    { Fade(remote); remote.current = {}; remote.blockedInstance = remote.resetBirth = 0; }
    // A newer occupant's EVENT may already be waiting for its SYSTEM roster.
    if (remote.hasPending && remote.pending.life.generation <= generation) remote.hasPending = false;
    if (remote.hasReset && remote.reset.life.generation <= generation) remote.hasReset = false;
}
void CPlayerAnimationSync::Process()
{
    if (!EnsureSession() || !CNetwork::m_bAuthenticated || CWorld::PlayerInFocus != 0) return;
    for (int id = 0; id < PlayerAnimation::MAX_PLAYERS; ++id)
    {
        auto& pending = remotes[id];
        if (id == CNetworkPlayerManager::m_nMyId && pending.hasPending)
        {
            ConsumeLocalAcknowledgement();
        }
        else ProcessRemote(id);
    }
    PublishLocal();
}
void CPlayerAnimationSync::Receive(const Packets::Players::PlayerAnimationState& packet)
{
    if (!EnsureSession() || !packet.Valid() || !packet.life.Valid(true)) return;
    auto& remote = remotes[packet.playerid];
    const auto& incoming = packet.life;
    if (incoming.generation <= remote.retiredGeneration) return;
    if (remote.current.hasLife && (incoming.generation < remote.current.life.generation
        || (incoming.generation == remote.current.life.generation && (incoming.birth < remote.current.life.birth
            || incoming.sequence <= remote.current.life.sequence)))) return;
    if (remote.hasPending && (incoming.generation < remote.pending.life.generation
        || (incoming.generation == remote.pending.life.generation && (incoming.birth < remote.pending.life.birth
            || incoming.sequence <= remote.pending.life.sequence)))) return;
    remote.pending = packet; remote.hasPending = true;
}
void CPlayerAnimationSync::ReceiveRespawn(const Packets::Players::RespawnPlayer& packet)
{
    if (!EnsureSession() || packet.playerid.value < 0 || packet.playerid.value >= PlayerAnimation::MAX_PLAYERS
        || !packet.life.Valid(true)) return;
    auto& remote = remotes[packet.playerid.value];
    if (packet.life.generation <= remote.retiredGeneration) return;
    if (remote.hasReset && (packet.life.generation < remote.reset.life.generation
        || (packet.life.generation == remote.reset.life.generation && packet.life.sequence <= remote.reset.life.sequence))) return;
    remote.reset = packet; remote.hasReset = true;
}
bool CPlayerAnimationSync::PrepareRespawn(Packets::Players::RespawnPlayer& packet)
{
    if (!EnsureSession() || !CNetwork::m_bAuthenticated || !LocalReady(FindPlayerPed(0)) || !ownerClock.NewBirth()) return false;
    needBirth = false; respawnBaseline = true; acknowledgedBirth = 0; published = false;
    auto* ped = FindPlayerPed(0);
    localLife = {localGeneration, ownerClock.birth, ownerClock.sequence, int(ped->m_nModelIndex), int(ped->m_nAreaCode), CPools::GetPedRef(ped)};
    packet.life = localLife; packet.life.generation = 0; packet.life.ready = false;
    return packet.life.Valid();
}
bool CPlayerAnimationSync::GetLocalLife(PlayerAnimation::Life& out)
{
    // Refresh only the local actor; a pickup hook must not recreate other players.
    if (!EnsureSession() || !CNetwork::m_bAuthenticated || CWorld::PlayerInFocus != 0) return false;
    PublishLocal();
    ConsumeLocalAcknowledgement();
    if (!CNetwork::m_bAuthenticated || !LocalReady(FindPlayerPed(0)) || !localGeneration
        || acknowledgedBirth != ownerClock.birth || !localLife.Valid()
        || acknowledgedLife.model != localLife.model || acknowledgedLife.area != localLife.area
        || acknowledgedLife.nativeReference != localLife.nativeReference) return false;
    out = acknowledgedLife; return true;
}
uint32_t CPlayerAnimationSync::GetLocalBirth()
{ PlayerAnimation::Life life; return GetLocalLife(life) ? life.birth : 0; }
uint32_t CPlayerAnimationSync::GetLocalSequence()
{ PlayerAnimation::Life life; return GetLocalLife(life) ? life.sequence : 0; }
