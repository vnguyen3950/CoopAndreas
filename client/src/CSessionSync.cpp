#include "stdafx.h"
#include "CSessionSync.h"
#include <CGame.h>
#include <CPickups.h>

namespace
{
SessionSync::Client g_client;
SessionSync::MoneyObservation g_money;
std::array<void(*)(), SessionSync::CHEATS> g_original{};
std::array<bool, SessionSync::CHEATS> g_flags{};
std::array<bool, SessionSync::CHEATS> g_supported{};
bool g_authenticated = false, g_scriptsCompleted = false, g_seedSent = false;
uint32_t g_connection = 0, g_gameGeneration = 1, g_lastEffect = 0;
int g_suppress = 0, g_lastWanted = -1;
uint64_t g_wantedLife = 0;
int64_t g_unsentMoney = 0;
std::deque<SessionSync::Operation> g_staged;
std::deque<Packets::Session::CheatAction> g_actions;
uint32_t g_retryRevision = 0, g_retryAcknowledged = 0;
DWORD g_lastHello = 0;
bool g_helloPending = false;
struct DeferredResurrection
{
    bool pending = false;
    uint32_t epoch = 0, incarnation = 0;
    uint64_t lifecycle = 0;
} g_resurrection;

struct Suppression { Suppression() { ++g_suppress; } ~Suppression() { --g_suppress; } };
uintptr_t ExpectedCallback(int id)
{
    switch (id)
    {
    case 0: return 0x4385B0; case 1: return 0x438890; case 2: return 0x438B30;
    case 3: return 0x438E40; case 4: return 0x438E90; case 5: return 0x438F20;
    case 63: return 0x4395B0; case 64: return 0x439600; case 65: return 0x4396C0;
    case 66: return 0x4396F0; case 69: return 0x439710;
    default: return 0;
    }
}
bool CashReady()
{
    // WinMain's GAME_STATE_IDLE (9) follows InitialiseGame; script completion
    // additionally waits for MAIN to create and initialize the real local player.
    auto* ped = FindPlayerPed(0);
    return gGameState == 9 && g_scriptsCompleted && CPools::ms_pPedPool && ped
        && CPools::ms_pPedPool->IsObjectValid(ped) && ped->m_pPlayerData;
}
bool NativeReady()
{
    return CashReady() && FindPlayerPed(0)->m_pRwObject && FindPlayerWanted(0)
        && CWorld::Players[0].m_nPlayerState == 0 && FindPlayerPed(0)->m_fHealth > 0;
}
uint64_t Lifecycle()
{ return (uint64_t(g_gameGeneration) << 32) | uint32_t(CPools::GetPedRef(FindPlayerPed(0))); }
bool EnsureSession()
{
    uint32_t connection = CNetwork::m_pPeer ? CNetwork::m_pPeer->connectID : 0;
    if (!CNetwork::m_bAuthenticated)
    {
        if (g_authenticated) { g_client = {}; g_money = {}; g_unsentMoney = 0; g_seedSent = false; g_staged.clear(); g_actions.clear(); }
        g_resurrection = {}; g_authenticated = false; return false;
    }
    if (!g_authenticated || connection != g_connection)
    {
        g_authenticated = true; g_connection = connection;
        g_client = {}; g_money = {}; g_unsentMoney = 0; g_seedSent = false;
        g_lastEffect = 0; g_lastWanted = -1; g_wantedLife = 0;
        g_staged.clear(); g_actions.clear(); g_retryRevision = 0; g_retryAcknowledged = 0;
        g_resurrection = {};
        g_helloPending = true; g_lastHello = GetTickCount();
        Packets::Session::Hello hello; GetPacketFactory().Send(hello);
    }
    return true;
}
bool Submit(SessionSync::Operation op)
{
    if (g_suppress || !EnsureSession() || !g_client.Queue(op)) return false;
    Packets::Session::Transaction packet; packet.op = op; GetPacketFactory().Send(packet); return true;
}
bool StageOrSubmit(SessionSync::Operation op)
{
    if (g_suppress) return false;
    if (g_client.state.ready) return Submit(op);
    if (!g_seedSent || g_staged.size() >= SessionSync::MAX_PENDING) return false;
    g_staged.push_back(op); return true;
}
bool DeferredResurrectionCurrent()
{
    return g_resurrection.pending && g_client.state.ready && CashReady()
        && g_resurrection.epoch == g_client.state.epoch && g_resurrection.incarnation == g_client.state.incarnation
        && g_resurrection.lifecycle == Lifecycle();
}
void RetryResurrection()
{
    if (!g_resurrection.pending) return;
    if (!CashReady()) return;
    if (!DeferredResurrectionCurrent()) { g_resurrection = {}; return; }
    SessionSync::Operation op;
    op.kind = SessionSync::Kind::WantedLower; op.reason = SessionSync::Reason::Resurrection; op.level = 0;
    if (StageOrSubmit(op)) g_resurrection = {};
}
void ObserveLocal();
void WriteMoney()
{
    if (!CashReady() || !g_client.state.ready) return;
    const auto projected = g_client.ProjectedMoney() + g_unsentMoney;
    CWorld::Players[0].m_nMoney = SessionSync::NativeBalance(projected);
    CWorld::Players[0].m_nDisplayMoney = SessionSync::DisplayBalance(projected);
    g_money.Written(CWorld::Players[0].m_nMoney);
}
void CaptureMoney()
{
    if (g_suppress || !EnsureSession()) return;
    const bool ready = CashReady();
    const auto change = g_money.Observe(ready, ready ? Lifecycle() : 0, CWorld::Players[0].m_nMoney);
    if (!ready) return;
    if (change.rebase)
    {
        if (g_client.state.ready) WriteMoney();
        return;
    }
    // Before initial host seed acknowledgement, retain changes after the seed.
    // A guest's pre-ready wallet never contributes to the room.
    if (g_client.state.ready || g_seedSent) g_unsentMoney += change.delta;
    if (g_client.state.ready && g_unsentMoney)
    {
        int64_t delta = std::max<int64_t>(-SessionSync::DELTA_LIMIT,
            std::min<int64_t>(SessionSync::DELTA_LIMIT, g_unsentMoney));
        SessionSync::Operation op; op.kind = SessionSync::Kind::Money; op.delta = int32_t(delta);
        if (Submit(op)) g_unsentMoney -= delta;
    }
    WriteMoney();
}
void ApplyCheat(int id, bool desired, bool action)
{
    if (!NativeReady() || id < 0 || id >= SessionSync::CHEATS || !g_supported[id]) return;
    if (!action && CCheat::m_aCheatsActive[id] == desired) { g_flags[id] = desired; return; }
    CaptureMoney();
    Suppression scope;
    auto* wanted = FindPlayerWanted(0);
    auto maximum = CWanted::MaximumWantedLevel, chaosMaximum = CWanted::nMaximumWantedLevel;
    SessionSync::ApplyWithoutFeedback(CWorld::Players[0].m_nMoney, *wanted, [&]
    {
        if (SessionSync::Mode(id) == SessionSync::CheatMode::FlagToggle) CCheat::m_aCheatsActive[id] = desired;
        else if (g_original[id]) g_original[id]();
    });
    CWanted::MaximumWantedLevel = maximum; CWanted::nMaximumWantedLevel = chaosMaximum;
    g_flags[id] = CCheat::m_aCheatsActive[id];
    CCheat::m_bHasPlayerCheated = true;
}
void ApplyWanted(CWanted* wanted, const SessionSync::Snapshot& state)
{
    if (!wanted) return;
    SessionSync::ApplyWantedLevel(state.wanted, [wanted] { return int(wanted->m_nWantedLevel); },
        [wanted](int level)
        {
            // Native SetWantedLevel early-returns under never-wanted. An explicit
            // canonical zero must still clear a stale initialized wanted object.
            bool never = CCheat::m_aCheatsActive[65];
            if (level == 0) CCheat::m_aCheatsActive[65] = false;
            wanted->SetWantedLevel(level); CCheat::m_aCheatsActive[65] = never;
        });
    wanted->m_bPoliceBackOff = state.policeIgnore;
    wanted->m_bEverybodyBackOff = state.everyoneIgnore;
}
void ApplyCurrent()
{
    WriteMoney();
    if (!NativeReady() || !g_client.state.ready) return;
    auto predicted = g_client.Predicted();
    if (DeferredResurrectionCurrent()) predicted.wanted = 0;
    for (int id = 0; id < SessionSync::CHEATS; ++id)
        if (SessionSync::Mode(id) == SessionSync::CheatMode::FlagToggle || SessionSync::Mode(id) == SessionSync::CheatMode::FunctionToggle)
            ApplyCheat(id, predicted.toggles[id], false);
    Suppression scope;
    if (CWanted::MaximumWantedLevel != predicted.maximumWanted) CWanted::SetMaximumWantedLevel(predicted.maximumWanted);
    ApplyWanted(FindPlayerWanted(0), predicted);
    for (auto* player : CNetworkPlayerManager::m_pPlayers)
        if (player && player->m_pPed && IsPedPointerValid(player->m_pPed))
        {
            int internal = player->GetInternalId();
            if (internal > 0) ApplyWanted(FindPlayerWanted(internal), predicted);
        }
    g_lastWanted = int(FindPlayerWanted(0)->m_nWantedLevel);
    WriteMoney();
}
void WantedChange(SessionSync::Reason reason, int level)
{
    if (g_suppress || !NativeReady()) return;
    SessionSync::Operation op;
    op.kind = reason == SessionSync::Reason::Natural ? SessionSync::Kind::WantedRaise : SessionSync::Kind::WantedLower;
    op.reason = reason; op.level = uint8_t(std::clamp(level, 0, 6));
    // Bribe operations carry reason metadata; cache the actual native result
    // so a receipt cannot mistake the remaining stars for a fresh crime.
    if (StageOrSubmit(op)) g_lastWanted = int(FindPlayerWanted(0)->m_nWantedLevel);
}
void ObserveLocal()
{
    CaptureMoney();
    RetryResurrection();
    if (!NativeReady()) { g_wantedLife = 0; g_lastWanted = -1; return; }
    uint64_t life = Lifecycle();
    int wanted = int(FindPlayerWanted(0)->m_nWantedLevel);
    if (life != g_wantedLife)
    {
        // Native new-game/load resets cheat flags too; they are not user toggles.
        for (int id = 0; id < SessionSync::CHEATS; ++id) g_flags[id] = CCheat::m_aCheatsActive[id];
        g_wantedLife = life; g_lastWanted = wanted; return;
    }
    if (g_client.state.ready || g_seedSent)
    {
        for (int id = 0; id < SessionSync::CHEATS; ++id)
            if (g_supported[id] && SessionSync::Mode(id) == SessionSync::CheatMode::FlagToggle
                && CCheat::m_aCheatsActive[id] != g_flags[id])
            {
                SessionSync::Operation op; op.kind = SessionSync::Kind::CheatToggle; op.cheat = uint8_t(id);
                op.active = CCheat::m_aCheatsActive[id];
                if (StageOrSubmit(op)) g_flags[id] = op.active;
            }
        if (g_lastWanted >= 0 && wanted != g_lastWanted)
        {
            if (wanted > g_lastWanted) WantedChange(SessionSync::Reason::Natural, wanted);
            else if (g_client.state.host == CNetworkPlayerManager::m_nMyId) WantedChange(SessionSync::Reason::HostDecay, wanted);
        }
    }
}
void RetryPending(const SessionSync::Snapshot& receipt)
{
    if (g_client.pending.empty()) return;
    if (!g_helloPending && receipt.revision == g_retryRevision && receipt.acknowledged == g_retryAcknowledged) return;
    g_retryRevision = receipt.revision; g_retryAcknowledged = receipt.acknowledged;
    for (const auto& op : g_client.pending)
        if (op.sequence > receipt.acknowledged)
        { Packets::Session::Transaction packet; packet.op = op; GetPacketFactory().Send(packet); }
}
void ProcessActions()
{
    if (!NativeReady()) return;
    while (!g_actions.empty())
    {
        auto action = g_actions.front(); g_actions.pop_front();
        if (action.epoch == g_client.state.epoch) ApplyCheat(action.cheat, false, true);
    }
}
bool __cdecl PickupHook(unsigned short model, int player)
{
    bool eligible = !g_suppress && NativeReady() && player == 0;
    auto* wanted = eligible ? FindPlayerWanted(0) : nullptr;
    int before = wanted ? int(wanted->m_nWantedLevel) : 0;
    bool result = CPickups::GivePlayerGoodiesWithPickUpMI(model, player);
    if (eligible && result && model == MODEL_BRIBE && NativeReady() && wanted == FindPlayerWanted(0)
        && int(FindPlayerWanted(0)->m_nWantedLevel) < before)
        WantedChange(SessionSync::Reason::Bribe, 0);
    return result;
}
void __fastcall ResprayHook(CWanted* wanted, void*)
{
    bool eligible = !g_suppress && NativeReady() && wanted == FindPlayerWanted(0);
    int before = int(wanted->m_nWantedLevel);
    wanted->ClearWantedLevelAndGoOnParole();
    if (eligible && NativeReady() && wanted == FindPlayerWanted(0) && int(wanted->m_nWantedLevel) < before)
        WantedChange(SessionSync::Reason::Respray, 0);
}
void __fastcall HostParoleHook(CWanted* wanted, void*)
{
    bool eligible = !g_suppress && NativeReady() && wanted == FindPlayerWanted(0)
        && g_client.state.host == CNetworkPlayerManager::m_nMyId;
    int before = int(wanted->m_nWantedLevel);
    wanted->ClearWantedLevelAndGoOnParole();
    if (eligible && NativeReady() && wanted == FindPlayerWanted(0) && int(wanted->m_nWantedLevel) < before)
        WantedChange(SessionSync::Reason::HostScript, int(wanted->m_nWantedLevel));
}
void __fastcall ResurrectionResetHook(CWanted* wanted, void*)
{
    auto* ped = FindPlayerPed(0);
    bool local = CashReady() && ped && ped->m_pPlayerData && wanted == FindPlayerWanted(0);
    bool publish = local && CWorld::PlayerInFocus == 0 && !g_suppress && EnsureSession() && g_client.state.ready && g_client.Predicted().wanted > 0;
    wanted->Reset();
    if (publish)
    {
        // This verified native boundary covers both hospital and arrest recovery.
        // Publish the clear before ApplyCurrent can restore the old room stars.
        SessionSync::Operation op;
        op.kind = SessionSync::Kind::WantedLower;
        op.reason = SessionSync::Reason::Resurrection;
        op.level = 0;
        if (!StageOrSubmit(op))
            g_resurrection = {true, g_client.state.epoch, g_client.state.incarnation, Lifecycle()};
    }
    if (local)
    {
        // The native resurrection path reuses the ped and may complete within
        // one frame. Invalidate wanted only: hospital/arrest cash was charged
        // before this call and must still be observed in the same cash lifecycle.
        g_wantedLife = 0; g_lastWanted = -1;
    }
}
template<int N> struct CheatHook { static void Fn() { CSessionSync::OnLocalCheat(N); } };
template<int N> void HookCheats()
{
    if (g_supported[N] && g_original[N]) patch::SetPointer(uintptr_t(&CCheat::m_aCheatFunctions[N]), &CheatHook<N>::Fn);
    if constexpr (N > 0) HookCheats<N - 1>();
}
}
void CSessionSync::OnLocalCheat(int id)
{
    if (id < 0 || id >= SessionSync::CHEATS) return;
    if (g_suppress || !EnsureSession())
    { if (g_original[id]) g_original[id](); return; }
    if (!g_client.state.ready && !g_seedSent)
    { logger::warn("Shared cheat activation waits for host session initialization"); return; }
    ObserveLocal();
    SessionSync::Operation op; op.cheat = uint8_t(id);
    op.kind = SessionSync::Mode(id) == SessionSync::CheatMode::Action ? SessionSync::Kind::CheatAction : SessionSync::Kind::CheatToggle;
    op.active = !g_client.Predicted().toggles[id];
    if (!g_client.state.ready)
        for (const auto& staged : g_staged)
            if (staged.kind == SessionSync::Kind::CheatToggle && staged.cheat == id) op.active = !staged.active;
    if (!StageOrSubmit(op)) logger::warn("Shared-session cheat request could not be queued");
}
void CSessionSync::HandleState(const Packets::Session::Update& packet)
{
    if (!EnsureSession()) return;
    if (!packet.state.Valid() || packet.state.recipient != CNetworkPlayerManager::m_nMyId) return;
    bool same = packet.state.epoch == g_client.state.epoch && packet.state.incarnation == g_client.state.incarnation;
    if (same && (g_client.state.ready || g_seedSent)) ObserveLocal();
    bool accepted = g_client.Accept(packet.state, CNetworkPlayerManager::m_nMyId);
    if (!accepted)
    {
        if (same && packet.state.revision >= g_client.state.revision && packet.state.acknowledged <= g_client.nextSequence)
            RetryPending(packet.state);
        g_helloPending = false; return;
    }
    if (!same)
    {
        g_money = {}; g_unsentMoney = 0; g_seedSent = false; g_lastEffect = packet.state.revision;
        g_lastWanted = -1; g_wantedLife = 0;
        g_staged.clear(); g_actions.clear(); g_retryRevision = 0; g_retryAcknowledged = 0;
        g_resurrection = {};
    }
    while (g_client.state.ready && !g_staged.empty())
    { if (!Submit(g_staged.front())) break; g_staged.pop_front(); }
    RetryPending(packet.state); g_helloPending = false;
    ProcessActions();
    ApplyCurrent();
}
void CSessionSync::HandleAction(const Packets::Session::CheatAction& packet)
{
    if (!EnsureSession() || !g_client.state.ready || packet.epoch != g_client.state.epoch
        || packet.revision <= g_lastEffect || SessionSync::Mode(packet.cheat) != SessionSync::CheatMode::Action) return;
    if (g_actions.size() >= SessionSync::MAX_PENDING)
    { logger::warn("Shared-session action queue exhausted; disconnecting instead of silently dropping actions"); CNetwork::Disconnect(); return; }
    g_lastEffect = packet.revision;
    g_actions.push_back(packet);
}
void CSessionSync::Process()
{
    if (!EnsureSession()) return;
    ObserveLocal();
    if (!g_client.pending.empty() && GetTickCount() - g_lastHello >= 1000)
    {
        Packets::Session::Hello hello; GetPacketFactory().Send(hello);
        g_lastHello = GetTickCount(); g_helloPending = true;
    }
    if (!NativeReady()) return;
    if (!g_client.state.ready)
    {
        if (!g_seedSent && g_client.state.epoch && g_client.state.host == CNetworkPlayerManager::m_nMyId)
        {
            Packets::Session::Seed seed; seed.state = g_client.state;
            seed.state.money = std::clamp(CWorld::Players[0].m_nMoney, -SessionSync::MONEY_LIMIT, SessionSync::MONEY_LIMIT);
            seed.state.maximumWanted = uint8_t(std::min(CWanted::MaximumWantedLevel, 6u));
            auto* wanted = FindPlayerWanted(0);
            seed.state.wanted = uint8_t(std::min(wanted->m_nWantedLevel, unsigned(seed.state.maximumWanted)));
            seed.state.policeIgnore = wanted->m_bPoliceBackOff; seed.state.everyoneIgnore = wanted->m_bEverybodyBackOff;
            for (int id = 0; id < SessionSync::CHEATS; ++id)
                if (g_supported[id] && SessionSync::Mode(id) != SessionSync::CheatMode::Action)
                    seed.state.toggles[id] = CCheat::m_aCheatsActive[id];
            if (seed.state.toggles[65]) seed.state.wanted = 0;
            GetPacketFactory().Send(seed); g_seedSent = true;
        }
        return;
    }
    while (!g_staged.empty()) { if (!Submit(g_staged.front())) break; g_staged.pop_front(); }
    ProcessActions();
    ApplyCurrent();
}
bool CSessionSync::IsWalletReadyForLocalService()
{
    // A pure readiness query: never seed, rebase, write cash or send a payment.
    if (!CNetwork::m_bAuthenticated || !CNetwork::m_pPeer || !g_authenticated
        || CNetwork::m_pPeer->connectID != g_connection || g_suppress
        || CWorld::PlayerInFocus != 0 || !g_client.state.ready || !g_client.state.Valid()
        || g_client.state.recipient != CNetworkPlayerManager::m_nMyId || !CashReady()) return false;
    return g_money.armed && g_money.lifecycle == Lifecycle();
}

bool CSessionSync::NeedsOpcodeCapture(uint16_t opcode)
{
    if (!CNetwork::m_bAuthenticated || !CLocalPlayer::m_bIsHost) return false;
    return opcode == 0x0109 || opcode == 0x010D || opcode == 0x010E || opcode == 0x0110
        || opcode == 0x01F0 || opcode == 0x01F7 || opcode == 0x03BF;
}
bool CSessionSync::ConsumeOpcode(uint16_t opcode, const int* params, int count)
{
    if (SessionSync::SkipRewardReplay(opcode, CNetwork::m_bAuthenticated)) { CaptureMoney(); return true; }
    if (!NeedsOpcodeCapture(opcode) || !g_client.state.ready || g_suppress) return false;
    if (opcode != 0x01F0 && (!count || params[0] != 0)) return false;
    if (opcode == 0x01F0 || opcode == 0x01F7 || opcode == 0x03BF)
    {
        auto* wanted = FindPlayerPed(0) ? FindPlayerWanted(0) : nullptr;
        SessionSync::Operation op; op.kind = SessionSync::Kind::WantedRules; op.reason = SessionSync::Reason::HostScript;
        op.level = uint8_t(std::min(CWanted::MaximumWantedLevel, 6u));
        op.policeIgnore = wanted ? wanted->m_bPoliceBackOff : g_client.state.policeIgnore;
        op.everyoneIgnore = wanted ? wanted->m_bEverybodyBackOff : g_client.state.everyoneIgnore; Submit(op);
    }
    else if (FindPlayerPed(0) && FindPlayerWanted(0))
    {
        SessionSync::Operation op;
        op.kind = opcode == 0x010E ? SessionSync::Kind::WantedRaise : SessionSync::Kind::WantedLower;
        op.reason = opcode == 0x010E ? SessionSync::Reason::Natural : SessionSync::Reason::HostScript;
        op.level = uint8_t(std::min(FindPlayerWanted(0)->m_nWantedLevel, 6u));
        if (Submit(op)) g_lastWanted = int(FindPlayerWanted(0)->m_nWantedLevel);
    }
    return true;
}
void CSessionSync::Init()
{
    for (int id = 0; id < SessionSync::CHEATS; ++id)
    {
        g_original[id] = CCheat::m_aCheatFunctions[id]; g_flags[id] = CCheat::m_aCheatsActive[id];
        auto mode = SessionSync::Mode(id);
        g_supported[id] = mode == SessionSync::CheatMode::FlagToggle ? !g_original[id]
            : mode != SessionSync::CheatMode::Unsupported && uintptr_t(g_original[id]) == ExpectedCallback(id);
    }
    HookCheats<SessionSync::CHEATS - 1>();
    patch::RedirectCall(0x457DF2, PickupHook); patch::RedirectCall(0x457F91, PickupHook);
    patch::RedirectCall(0x44AF10, ResprayHook);
    patch::RedirectCall(0x47AB4B, HostParoleHook);
    patch::RedirectCall(0x4421A3, ResurrectionResetHook);
    Events::initScriptsEvent.before += [] { g_resurrection = {}; g_scriptsCompleted = false; ++g_gameGeneration; g_money.armed = false; g_lastWanted = -1; };
    Events::processScriptsEvent.after += [] { if (gGameState == 9) g_scriptsCompleted = true; };
    gameShutdownEvent.before += [] { g_resurrection = {}; g_scriptsCompleted = false; g_money.armed = false; };
}
