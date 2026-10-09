#include <iostream>
#include <stdexcept>
#include "native_doubles.h"
#include "extracted_service.inc"
static unsigned replayReached = 0;
#include "extracted_replay.inc"
#include "extracted_punishment.inc"

using namespace SessionSync;
static unsigned checks = 0, failures = 0, cashEffects = 0, functionEffects = 0;
static void expect(bool condition, const char* description)
{
    ++checks;
    if (!condition) { ++failures; std::cout << "FAIL: " << description << '\n'; }
}
static void cash_native()
{
    ++cashEffects;
    CWorld::Players[0].m_nMoney += 250000;
    localPed.m_fHealth = 100; localPed.armor = 100;
    localWanted.m_nWantedLevel = 0;
    CWanted::MaximumWantedLevel = 6;
}
static void riot_native() { ++functionEffects; CCheat::m_aCheatsActive[69] = !CCheat::m_aCheatsActive[69]; }
static void never_native() { ++functionEffects; localWanted.m_nWantedLevel = 0; CCheat::m_aCheatsActive[65] = !CCheat::m_aCheatsActive[65]; }
static void prepare(int id, int32_t cash, bool ready = true)
{
    CNetworkPlayerManager::m_nMyId = id; CLocalPlayer::m_bIsHost = id == 0;
    CWorld::Players[0].m_nMoney = cash; CWorld::Players[0].m_nDisplayMoney = std::max(0,cash);
    gGameState = ready ? 9 : 0; playerExists = ready;
    CSessionSync::Init();
    // Recorded callbacks replace native addresses only in test setup. Actual
    // address identity and patch installation are not proven by these doubles.
    for (int i = 0; i < CHEATS; ++i) g_supported[i] = Mode(i) != CheatMode::Unsupported;
    g_original[3] = cash_native; g_original[65] = never_native; g_original[69] = riot_native;
    if (ready) Events::processScriptsEvent.after.Fire();
}
static Server room(int32_t cash = 100, unsigned wanted = 0, bool toggles = false)
{
    Server s; expect(s.Join(0,0) != 0 && s.Join(1,0) != 0, "Real server creates host and guest identities.");
    auto seed = s.For(0); seed.money = cash; seed.wanted = uint8_t(wanted);
    seed.toggles[31] = toggles; seed.toggles[69] = toggles;
    expect(s.Seed(0,seed), "Real server accepts initial seed."); return s;
}
static void receipt(const Server& s, int id)
{
    Packets::Session::Update update; update.state = s.For(id); CSessionSync::HandleState(update);
}
template<class T> static std::vector<T> sent(ePacketType type)
{
    std::vector<T> result;
    for (const auto& p : GetPacketFactory().sent) if (p->GetType() == type) result.push_back(static_cast<const T&>(*p));
    return result;
}
static std::vector<Operation> operations()
{
    std::vector<Operation> result;
    for (const auto& p : sent<Packets::Session::Transaction>(ePacketType::SESSION_OPERATION)) result.push_back(p.op);
    return result;
}
static std::vector<Operation> unique_operations()
{
    std::vector<Operation> result;
    for (const auto& op : operations()) {
        bool seen = false;
        for (const auto& previous : result) if (op.epoch == previous.epoch && op.incarnation == previous.incarnation
            && op.sequence == previous.sequence) seen = true;
        if (!seen) result.push_back(op);
    }
    return result;
}
static void boot(const Server& s, int id, int32_t nativeCash = 100)
{
    prepare(id,nativeCash); receipt(s,id); CSessionSync::Process(); GetPacketFactory().sent.clear();
}
static Operation remote(const Server& s, int id, uint32_t sequence, int32_t delta)
{
    Operation op; op.epoch = s.state.epoch; op.incarnation = s.peers[id].incarnation;
    op.sequence = sequence; op.delta = delta; return op;
}
static void menu_seed()
{
    Server s; s.Join(0,0); prepare(0,0,false); receipt(s,0);
    CSessionSync::Process(); expect(sent<Packets::Session::Seed>(ePacketType::SESSION_SEED).empty(), "Menu authentication cannot seed zero cash.");
    gGameState = 9; playerExists = true; CWorld::Players[0].m_nMoney = 100;
    CSessionSync::Process(); expect(sent<Packets::Session::Seed>(ePacketType::SESSION_SEED).empty(), "Player presence alone cannot bypass script initialization gate.");
    Events::processScriptsEvent.after.Fire(); CSessionSync::Process(); CSessionSync::Process();
    auto seeds = sent<Packets::Session::Seed>(ePacketType::SESSION_SEED);
    expect(seeds.size() == 1 && seeds[0].state.money == 100, "Initialized host seeds correct native cash exactly once.");
}
static void guest_reset()
{
    auto s = room(-50); prepare(1,350,false); receipt(s,1);
    gGameState = 9; playerExists = true; Events::processScriptsEvent.after.Fire(); CSessionSync::Process();
    expect(operations().empty() && CWorld::Players[0].m_nMoney == 0 && CWorld::Players[0].m_nDisplayMoney == 0
        && g_client.ProjectedMoney() == -50,
        "Guest initial cash becomes zero budget while signed ledger debt remains.");
    Events::initScriptsEvent.before.Fire(); CWorld::Players[0].m_nMoney = 350; gGameState = 0;
    CSessionSync::Process(); gGameState = 9; Events::processScriptsEvent.after.Fire(); CSessionSync::Process();
    expect(operations().empty() && CWorld::Players[0].m_nMoney == 0 && g_client.ProjectedMoney() == -50,
        "New Game/load cash reset does not become earning or spending or erase debt.");
    ++nativePedRef; CWorld::Players[0].m_nMoney = 0; CSessionSync::Process();
    expect(operations().empty() && CWorld::Players[0].m_nMoney == 0 && g_client.ProjectedMoney() == -50,
        "New ped lifetime rebases cash without a wallet delta or debt forgiveness.");
}
static void seed_receipt()
{
    Server s; s.Join(0,0); prepare(0,100); receipt(s,0); CSessionSync::Process();
    auto seeds = sent<Packets::Session::Seed>(ePacketType::SESSION_SEED); expect(seeds.size() == 1, "Host seed sent before receipt race.");
    expect(s.Seed(0,seeds.at(0).state), "Seed is accepted by actual room ledger.");
    CWorld::Players[0].m_nMoney += 15; receipt(s,0);
    expect(CWorld::Players[0].m_nMoney == 115, "Post-seed cash survives the receipt before the next process tick.");
    CSessionSync::Process();
    auto ops = unique_operations();
    expect(ops.size() == 1 && ops[0].kind == Kind::Money && ops[0].delta == 15
        && CWorld::Players[0].m_nMoney == 115, "Cash changed after seed but before receipt survives capture.");
}
static void receipt_capture()
{
    auto s = room(); boot(s,0); CWorld::Players[0].m_nMoney += 25;
    expect(s.Apply(1,remote(s,1,1,7)).status == Status::Accepted, "Remote earn produces an incoming snapshot.");
    receipt(s,0); auto ops = unique_operations();
    expect(ops.size() == 1 && ops[0].delta == 25 && CWorld::Players[0].m_nMoney == 132,
        "Incoming receipt captures unobserved local earn before canonical overwrite.");
    expect(s.Apply(0,ops.at(0)).status == Status::Accepted, "Captured local earn is accepted once.");
    receipt(s,0); receipt(s,0); CSessionSync::Process();
    expect(s.state.money == 132 && CWorld::Players[0].m_nMoney == 132 && g_client.pending.empty(),
        "Duplicate receipt does not lose or echo local pending delta.");
}
static void receipt_side_effects()
{
    auto s = room(); boot(s,0);
    localWanted.m_nWantedLevel = 2; CCheat::m_aCheatsActive[31] = true;
    s.Apply(1,remote(s,1,1,7)); receipt(s,0);
    auto ops = unique_operations(); bool wanted = false, toggle = false;
    for (const auto& op : ops) {
        if (op.kind == Kind::WantedRaise && op.level == 2) wanted = true;
        if (op.kind == Kind::CheatToggle && op.cheat == 31 && op.active) toggle = true;
    }
    expect(ops.size() == 2 && wanted && toggle && localWanted.m_nWantedLevel == 2 && CCheat::m_aCheatsActive[31],
        "Receipt captures unobserved wanted and flag changes before applying shared targets.");
    for (const auto& op : ops) expect(s.Apply(0,op).status == Status::Accepted, "Captured side effect accepted in original sequence.");
    receipt(s,0); CSessionSync::Process();
    expect(g_client.pending.empty() && s.state.wanted == 2 && s.state.toggles[31],
        "Wanted and flag receipts converge without lost native changes.");
}
static void mission_reward()
{
    auto s = room(); boot(s,0); CWorld::Players[0].m_nMoney += 500; int params[] = {0,500};
    expect(CSessionSync::ConsumeOpcode(0x0109,params,2), "Actual host opcode service consumes already-applied ADD_SCORE.");
    CSessionSync::ConsumeOpcode(0x0109,params,2); auto ops = unique_operations();
    expect(ops.size() == 1 && ops[0].kind == Kind::Money && ops[0].delta == 500, "Repeated observation emits one mission reward transaction.");
    expect(s.Apply(0,ops.at(0)).status == Status::Accepted && s.Apply(0,ops.at(0)).status == Status::Duplicate
        && s.state.money == 600, "Mission reward counts once in actual room ledger.");
    receipt(s,0); expect(CWorld::Players[0].m_nMoney == 600, "Receipt preserves native reward without double application.");
    OpcodeSyncHeader header{}; header.opcode = 0x0109;
    ExtractedReplayPrefix(reinterpret_cast<const uint8_t*>(&header),sizeof(header));
    expect(replayReached == 0, "Actual opcode replay prefix returns before guest ADD_SCORE dispatch.");
    header.opcode = 0x010A; ExtractedReplayPrefix(reinterpret_cast<const uint8_t*>(&header),sizeof(header));
    expect(replayReached == 1, "Actual replay prefix does not suppress unrelated opcode.");
}
static Packets::Session::CheatAction accepted_cash(Server& s, int id)
{
    CSessionSync::OnLocalCheat(3); auto ops = unique_operations(); expect(ops.size() == 1, "Cash cheat queues one shared action.");
    auto accepted = s.Apply(id,ops.at(0)); expect(accepted.action && accepted.status == Status::Accepted, "Server accepts one cash action.");
    Packets::Session::CheatAction action; action.epoch = s.state.epoch; action.revision = s.state.revision;
    action.incarnation = ops[0].incarnation; action.sequence = ops[0].sequence; action.sender = id; action.cheat = 3;
    return action;
}
static void cash_feedback()
{
    auto s = room(100,3); boot(s,0); auto action = accepted_cash(s,0);
    CSessionSync::HandleAction(action); CSessionSync::HandleAction(action);
    CSessionSync::Process(); receipt(s,0); CSessionSync::Process();
    auto ops = unique_operations();
    expect(cashEffects == 1 && localPed.armor == 100 && localPed.m_fHealth == 100,
        "Duplicate native action has one health and armor effect.");
    expect(ops.size() == 1 && ops[0].kind == Kind::CheatAction && s.state.money == 250100
        && CWorld::Players[0].m_nMoney == 250100 && localWanted.m_nWantedLevel == 3,
        "Native cash/wanted changes are suppressed without a second Money transaction.");
    CSessionSync::HandleAction(action); CSessionSync::Process();
    expect(cashEffects == 1, "Delayed duplicate action still cannot reward again.");
}
static void deferred_action()
{
    auto s = room(); boot(s,0); auto action = accepted_cash(s,0);
    localPed.m_fHealth = 0; CSessionSync::HandleAction(action); CSessionSync::HandleAction(action); CSessionSync::Process();
    expect(cashEffects == 0, "Accepted action waits when native player is unavailable.");
    localPed.m_fHealth = 100; CSessionSync::Process(); receipt(s,0);
    expect(cashEffects == 1 && CWorld::Players[0].m_nMoney == 250100, "Queued action resumes exactly once after native readiness returns.");
}
static void rapid_toggle()
{
    auto s = room(); boot(s,0); CSessionSync::OnLocalCheat(69); CSessionSync::OnLocalCheat(69);
    auto ops = unique_operations();
    expect(ops.size() == 2 && ops[0].kind == Kind::CheatToggle && ops[0].active && !ops[1].active,
        "Two function-toggle presses before a frame or receipt request on then off.");
    CSessionSync::Process(); expect(!CCheat::m_aCheatsActive[69], "Latest pending toggle target stays off before acknowledgement.");
    expect(s.Apply(0,ops.at(0)).status == Status::Accepted, "First toggle accepted."); receipt(s,0);
    expect(!CCheat::m_aCheatsActive[69], "Partial on acknowledgement cannot override pending off target.");
    expect(s.Apply(0,ops.at(1)).status == Status::Accepted, "Second toggle accepted."); receipt(s,0);
    receipt(s,0); CSessionSync::Process(); expect(!CCheat::m_aCheatsActive[69] && g_client.pending.empty(), "Final off converges without a replay toggle.");
}
static void flags_reset()
{
    auto s = room(100,0,true); boot(s,0);
    expect(CCheat::m_aCheatsActive[31] && CCheat::m_aCheatsActive[69], "Initial shared flag and function toggles applied.");
    Events::initScriptsEvent.before.Fire(); CCheat::m_aCheatsActive.fill(false); CWorld::Players[0].m_nMoney = 350;
    gGameState = 0; CSessionSync::Process(); gGameState = 9; Events::processScriptsEvent.after.Fire(); CSessionSync::Process();
    expect(operations().empty(), "Native load resetting all cheat flags does not request shared toggle off.");
    expect(CCheat::m_aCheatsActive[31] && CCheat::m_aCheatsActive[69] && CWorld::Players[0].m_nMoney == 100,
        "New lifecycle restores shared toggles and wallet after native defaults reset.");
}
static void death_preservation()
{
    auto s = room(100,4); boot(s,0);
    localPed.m_fHealth = 0; CWorld::Players[0].m_nPlayerState = 1; localWanted.Reset(); CSessionSync::Process();
    localPed.m_fHealth = 100; CWorld::Players[0].m_nPlayerState = 0; CSessionSync::Process();
    expect(operations().empty() && localWanted.m_nWantedLevel == 4, "Death native wanted reset does not lower canonical wanted.");
}
static void same_frame_resurrection()
{
    auto s = room(100,4); boot(s,0);
    bool found = false;
    for (const auto& hook : patch::wantedHooks) if (hook.first == 0x4421A3) { found = true; hook.second(&localWanted); }
    expect(found, "Actual Init installs the researched reused-ped resurrection observation guard.");
    if (!found) localWanted.Reset();
    // No not-ready Process call occurs between the reset and this tick.
    CSessionSync::Process();
    expect(operations().empty() && localWanted.m_nWantedLevel == 4,
        "Same-frame reused-ped resurrection restores stars without submitting a lowering operation.");
}
static void native_wanted()
{
    auto s = room(); auto never = remote(s,0,1,0); never.kind = Kind::CheatToggle; never.cheat = 65; never.active = true;
    s.Apply(0,never); localWanted.m_nWantedLevel = 4;
    boot(s,1);
    expect(CCheat::m_aCheatsActive[65] && localWanted.m_nWantedLevel == 0,
        "Actual wanted application clears stale initialized stars even under never-wanted.");
    unsigned calls = CWanted::setterCalls; CSessionSync::Process(); CSessionSync::Process();
    expect(CWanted::setterCalls == calls, "Actual service avoids repeated SetWantedLevel calls once stars are correct.");
}
static void reset_same_ped()
{
    bool found = false;
    for (const auto& hook : patch::wantedHooks) if (hook.first == 0x4421A3) { found = true; hook.second(&localWanted); }
    expect(found, "Actual resurrection reset hook is available for punishment cash test.");
    if (!found) localWanted.Reset();
}
static void punishment_fee(int fee, bool arrest, bool observeDead)
{
    auto s = room(1000,4); boot(s,0);
    CWorld::Players[0].m_nPlayerState = arrest ? 2 : 1;
    if (!arrest) localPed.m_fHealth = 0;
    if (observeDead) CSessionSync::Process();
    // This is the exact PunishPlayer lambda extracted from adjacent native
    // GameLogic.cpp, not a replacement fee or wallet implementation.
    ExtractedPunishment(fee); reset_same_ped();
    localPed.m_fHealth = 100; CWorld::Players[0].m_nPlayerState = 0; CSessionSync::Process();
    auto ops = unique_operations();
    expect(ops.size() == 1 && ops[0].kind == Kind::Money && ops[0].delta == -fee,
        "Initialized cash observation retains the real hospital/arrest fee across death and Reset.");
    for (const auto& operation : ops) expect(s.Apply(0,operation).status == Status::Accepted, "Punishment delta is accepted once by canonical ledger.");
    receipt(s,0); CSessionSync::Process();
    expect(s.state.money == 1000-fee && CWorld::Players[0].m_nMoney == 1000-fee
        && localWanted.m_nWantedLevel == 4 && localPed.weaponsCleared == 1,
        "Fee remains deducted after receipt without lowering shared wanted or replaying native punishment.");
}
static void debt_budget()
{
    auto s = room(-50); boot(s,0);
    expect(CWorld::Players[0].m_nMoney == 0 && g_client.ProjectedMoney() == -50,
        "Canonical signed debt writes a zero spendable native budget.");
    CWorld::Players[0].m_nPlayerState = 1; localPed.m_fHealth = 0; CSessionSync::Process();
    ExtractedPunishment(100); reset_same_ped();
    localPed.m_fHealth = 100; CWorld::Players[0].m_nPlayerState = 0; CSessionSync::Process();
    expect(operations().empty() && g_client.ProjectedMoney() == -50 && CWorld::Players[0].m_nMoney == 0,
        "Native punishment clamp-to-zero cannot invent income or forgive signed debt.");
    CWorld::Players[0].m_nMoney += 30; CSessionSync::Process(); auto first = unique_operations();
    expect(first.size() == 1 && first[0].delta == 30 && g_client.ProjectedMoney() == -20
        && CWorld::Players[0].m_nMoney == 0, "Genuine earning repays debt while pending signed delta stays intact.");
    expect(s.Apply(0,first.at(0)).status == Status::Accepted, "First debt repayment is canonical."); receipt(s,0);
    CWorld::Players[0].m_nMoney += 50; CSessionSync::Process(); auto both = unique_operations();
    expect(both.size() == 2 && both[1].delta == 50 && g_client.ProjectedMoney() == 30
        && CWorld::Players[0].m_nMoney == 30, "Later earning crosses debt boundary into real spendable cash.");
    expect(s.Apply(0,both.at(1)).status == Status::Accepted, "Second debt repayment is canonical."); receipt(s,0);
    expect(s.state.money == 30 && g_client.pending.empty() && CWorld::Players[0].m_nMoney == 30,
        "Acknowledgements retain both repayment deltas exactly once.");
}
static void bribe_receipt()
{
    auto s = room(100,3); boot(s,1);
    expect(PickupHook(MODEL_BRIBE,0) && localWanted.m_nWantedLevel == 2,
        "Actual pickup adapter lowers the native level by one.");
    auto ops = unique_operations();
    expect(ops.size() == 1 && ops[0].kind == Kind::WantedLower && ops[0].reason == Reason::Bribe,
        "Pickup emits one attributed bribe operation.");
    if (ops.size() != 1) throw std::runtime_error("Bribe fixture requires one operation.");
    expect(s.Apply(1,ops[0]).status == Status::Accepted && s.state.wanted == 2,
        "Guest bribe lowers canonical pursuit once.");
    auto host = remote(s,0,1,0); host.kind = Kind::WantedLower; host.reason = Reason::Bribe;
    expect(s.Apply(0,host).status == Status::Accepted && s.state.wanted == 1,
        "Concurrent host bribe lowers pursuit again before receipt delivery.");
    receipt(s,1);
    expect(unique_operations().size() == 1 && g_client.pending.empty(),
        "Receipt must not interpret bribe reason metadata as a new native wanted raise.");
    expect(localWanted.m_nWantedLevel == 1 && g_lastWanted == 1,
        "Concurrent bribes converge without restoring an obsolete wanted level.");
}
static void migration()
{
    auto s = room(); boot(s,1); CWorld::Players[0].m_nMoney += 30; CSessionSync::Process();
    auto ops = unique_operations(); expect(ops.size() == 1 && s.Apply(1,ops.at(0)).status == Status::Accepted,
        "Guest request is accepted before migration but still pending locally.");
    auto epoch = s.state.epoch; s.Leave(0); s.HostChanged(1); CLocalPlayer::m_bIsHost = true;
    receipt(s,1); CSessionSync::Process();
    expect(g_client.pending.empty() && s.state.epoch == epoch && CWorld::Players[0].m_nMoney == 130
        && g_client.state.host == 1, "Migrated receipt retains ledger and acknowledges surviving guest request.");
    expect(s.Apply(1,ops[0]).status == Status::Duplicate && s.state.money == 130,
        "Migration retry cannot double the accepted money delta.");
}
int main(int argc, char** argv)
{
    if (argc != 2) { std::cout << "One named case is required for fresh-process isolation.\n"; return 2; }
    const std::string name = argv[1];
    try {
        if (name == "menu_seed") menu_seed(); else if (name == "guest_reset") guest_reset();
        else if (name == "seed_receipt") seed_receipt(); else if (name == "receipt_capture") receipt_capture();
        else if (name == "receipt_side_effects") receipt_side_effects();
        else if (name == "mission_reward") mission_reward(); else if (name == "cash_feedback") cash_feedback();
        else if (name == "deferred_action") deferred_action(); else if (name == "rapid_toggle") rapid_toggle();
        else if (name == "flags_reset") flags_reset(); else if (name == "death_preservation") death_preservation();
        else if (name == "same_frame_resurrection") same_frame_resurrection(); else if (name == "migration") migration();
        else if (name == "native_wanted") native_wanted();
        else if (name == "hospital_fee") punishment_fee(100,false,true);
        else if (name == "arrest_fee") punishment_fee(600,true,true);
        else if (name == "same_frame_fee") punishment_fee(100,false,false);
        else if (name == "debt_budget") debt_budget();
        else if (name == "bribe_receipt") bribe_receipt();
        else return 2;
    } catch (const std::exception& error) { ++failures; std::cout << "FIXTURE STOP: " << error.what() << '\n'; }
    std::cout << name << ": " << checks << " assertions, " << failures << " failures\n";
    return failures ? 1 : 0;
}
