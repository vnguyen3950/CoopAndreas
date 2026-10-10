#define NPC_SDK_MOVE_ANIM_TEST
#include "client_doubles.h"
#include "disk_vtables.h"
using namespace Packets::Peds;

static CPed* lastSavePed = nullptr;
// ECX is the native this pointer. EDX is unused; no stack arguments are consumed.
static void __fastcall RecordSave(CPed* ped, void*) { ++sdkSaveCalls; lastSavePed = ped; }
static void __fastcall RecordMovement(CPed* ped, void*) { ++nativeMoveCalls; lastNativeMovePed = ped; }
static void __fastcall UnusedSlot(CPed*, void*) {}
struct RecordedNativeTable {
    CPed* ped;
    void** original;
    std::array<void*, 26> table{};
    RecordedNativeTable(CPed* actor, const uint32_t* nativeTable) : ped(actor), original(*(void***)actor) {
        for (int i = 0; i < 26; ++i) {
            table[i] = reinterpret_cast<void*>(&UnusedSlot);
            if (nativeTable[i] == 0x5D5730) table[i] = reinterpret_cast<void*>(&RecordSave);
            if (nativeTable[i] == 0x5E4A00) table[i] = reinterpret_cast<void*>(&RecordMovement);
        }
        *(void***)ped = table.data();
    }
    ~RecordedNativeTable() { *(void***)ped = original; }
};

int main()
{
    // Compile the unchanged SDK thunk. Targets come from supported EXE bytes,
    // not a guessed SDK layout; callbacks record dispatch without executing GTA.
    CPed sdkProbe;
    for (const auto* table : {kCivilianTable, kCopTable, kBaseTable}) {
        RecordedNativeTable binding(&sdkProbe, table);
        const auto saves = sdkSaveCalls, moves = nativeMoveCalls;
        sdkProbe.SetMoveAnim();
        expect(sdkSaveCalls == saves + 1 && nativeMoveCalls == moves && lastSavePed == &sdkProbe,
            "Unchanged SDK thunk selects native Save slot, proving the ABI mismatch");
    }

    ModelInfo model;
    for (auto& ptr : CModelInfo::ms_modelInfoPtrs) ptr = &model;
    CPed local(PED_TYPE_PLAYER1); CWorld::Players[0].m_pPed = &local; CPools::GetPedRef(&local);
    CNetworkPedManager::Init(); gGameState = 9; Events::processScriptsEvent.after.Fire();
    struct Case { eModelID model; ePedType type; const uint32_t* table; };
    const Case cases[] = {
        {MODEL_LAPD1, PED_TYPE_COP, kCopTable}, {MODEL_SFPD1, PED_TYPE_COP, kCopTable},
        {MODEL_MALE01, PED_TYPE_CIVMALE, kCivilianTable}, {MODEL_BALLAS1, PED_TYPE_GANG1, kCivilianTable}
    };
    uint32_t birth = 700;
    for (const auto& item : cases) {
        PedSpawn spawn; spawn.pedid = 1; spawn.ownerid = 1; spawn.stamp = {++birth, 1, 0};
        spawn.modelId = item.model; spawn.pedType = item.type; receive(spawn);
        auto* ped = CNetworkPedManager::GetPed(1);
        expect(ped && ped->HasValidPed(), "Actual spawn creates exact replica for animation ABI probe");
        if (!ped) return 1;
        {
            RecordedNativeTable binding(ped->m_pPed, item.table);
            const auto saves = sdkSaveCalls, moves = nativeMoveCalls;
            PedOnFoot state; state.pedid = 1; state.stamp = {birth, 1, 1};
            state.healthSnapshot.iHealth = 100; state.moveState = PEDMOVE_RUN; receive(state);
            expect(nativeMoveCalls == moves + 1 && lastNativeMovePed == ped->m_pPed && sdkSaveCalls == saves,
                "Positive-health actual handler selects movement, never Save, with exact this pointer");
            PedReplay replay; replay.mode = 1; replay.onFoot = state; replay.onFoot.stamp.sequence = 2;
            receive(replay);
            expect(nativeMoveCalls == moves + 2 && sdkSaveCalls == saves && ped->m_stateSequence == 2,
                "Reliable late-join replay selects movement through the actual nested handler");
            const auto tasks = deathTasks, corpseMoves = nativeMoveCalls, corpseSaves = sdkSaveCalls;
            state.stamp.sequence = 3; state.healthSnapshot.iHealth = 0; receive(state);
            expect(deathTasks == tasks + 1 && ped->m_replicaDeath && nativeMoveCalls == corpseMoves && sdkSaveCalls == corpseSaves,
                "Zero-health handler starts one death task without movement animation");
            state.stamp.sequence = 4; state.healthSnapshot.iHealth = 100; receive(state);
            PedDeath seal; seal.pedid = 1; seal.stamp = {birth, 1, 3}; receive(seal); receive(seal);
            expect(ped->m_pPed->m_fHealth == 0 && deathTasks == tasks + 1 &&
                nativeMoveCalls == corpseMoves && sdkSaveCalls == corpseSaves,
                "Delayed alive state and duplicate seal neither animate nor revive terminal corpse");
        }
        CNetworkPedManager::Clear();

        spawn.ownerid = 0; spawn.stamp = {++birth, 1, 0}; receive(spawn);
        ped = CNetworkPedManager::GetPed(1);
        expect(ped && ped->m_bSyncing, "Existing original-owner guard is present");
        if (!ped) return 1;
        {
            RecordedNativeTable binding(ped->m_pPed, item.table);
            const auto saves = sdkSaveCalls, moves = nativeMoveCalls;
            PedOnFoot state; state.pedid = 1; state.stamp = {birth, 1, 1}; state.healthSnapshot.iHealth = 100;
            receive(state);
            expect(nativeMoveCalls == moves && sdkSaveCalls == saves && ped->m_stateSequence == 0,
                "Foreign state does not replace original-owner movement behavior");
        }
        CNetworkPedManager::Clear();
    }
    std::cout << checks << " NPC movement SDK/native ABI assertions, " << failures << " failures\n";
    return failures ? 1 : 0;
}
