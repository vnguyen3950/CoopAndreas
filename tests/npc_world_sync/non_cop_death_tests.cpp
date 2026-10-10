#include "client_doubles.h"
using namespace Packets::Peds;

int main()
{
    ModelInfo model;
    for (auto& ptr : CModelInfo::ms_modelInfoPtrs) ptr = &model;
    CPed local(PED_TYPE_PLAYER1);
    CWorld::Players[0].m_pPed = &local;
    CPools::GetPedRef(&local);
    CNetworkPedManager::Init();
    gGameState = 9;
    Events::processScriptsEvent.after.Fire();

    struct Case { eModelID model; ePedType type; };
    const Case cases[] = {
        {MODEL_MALE01, PED_TYPE_CIVMALE}, {MODEL_BFYRI, PED_TYPE_CIVFEMALE},
        {MODEL_BALLAS1, PED_TYPE_GANG1}, {MODEL_FAM1, PED_TYPE_GANG2}
    };
    uint32_t birth = 500;
    for (const auto& item : cases) {
        // Both reliable seal before SYNC and zero-health SYNC before seal.
        for (bool sealFirst : {false, true}) {
            PedSpawn spawn;
            spawn.pedid = 1; spawn.ownerid = 1; spawn.stamp = {++birth, 1, 0};
            spawn.modelId = item.model; spawn.pedType = item.type;
            receive(spawn);
            auto* ped = CNetworkPedManager::GetPed(1);
            expect(ped && ped->HasValidPed() && ped->m_pPed->m_nModelIndex == item.model &&
                ped->m_pPed->m_nPedType == item.type, "Actual civilian/gang constructor preserves model and type");
            if (!ped) return 1;
            ped->m_pPed->m_nMoneyCount = 137;
            ped->m_pPed->m_vecMoveSpeed = {1, 2, 3};
            const auto tasks = deathTasks;
            PedDeath seal;
            seal.pedid = 1; seal.stamp = {birth, 1, 2}; seal.position = {10, 20, 3};
            PedOnFoot state;
            state.pedid = 1; state.stamp = {birth, 1, 3}; state.healthSnapshot.iHealth = 0;
            state.moveState = PEDMOVE_RUN;
            if (sealFirst) { receive(seal); receive(state); }
            else { receive(state); receive(seal); }
            expect(deathTasks == tasks + 1 && ped->m_replicaDeath && ped->m_pPed->m_ePedState == PEDSTATE_DIE,
                "Either channel order starts exactly one recorded native death task");
            expect(ped->m_deathStamp.sequence == 2 && ped->m_stateSequence == 3,
                "Earlier reliable terminal seal survives newer SYNC high-water");
            expect(ped->m_pPed->m_nPedFlags.bDoesntDropWeaponsWhenDead,
                "Non-cop replica disables native weapon loot before death task");
            expect(ped->m_pPed->m_nMoneyCount == 0,
                "Non-cop replica suppresses native money loot before death task");
            expect(ped->m_nMoveState == PEDMOVE_STILL && ped->m_pPed->m_vecMoveSpeed.x == 0 &&
                ped->m_pPed->m_vecMoveSpeed.y == 0 && ped->m_pPed->m_vecMoveSpeed.z == 0,
                "Terminal non-cop replica stops motion");
            state.stamp.sequence = 4; state.healthSnapshot.iHealth = 100; receive(state);
            receive(seal);
            expect(ped->m_pPed->m_fHealth == 0 && deathTasks == tasks + 1,
                "Delayed alive SYNC and duplicate seal neither revive nor restart non-cop corpse");
            AssignPedSyncer transfer;
            transfer.pedid = 1; transfer.ownerid = 0; transfer.stamp = {birth, 2, 4}; receive(transfer);
            int id = -1; NPCSync::Stamp proof;
            expect(!CNetworkPedManager::GetOwnerDeathIdentity(ped->m_pPed, id, proof),
                "Transferred replica corpse cannot become a new native loot producer");
            CNetworkPedManager::Clear();
        }

        // Register an original native civilian/gang instance, not a constructed replica.
        auto* native = new CCivilianPed(item.type, item.model);
        native->m_nMoneyCount = 137;
        auto* owner = CNetworkPed::CreateHosted(native);
        expect(owner && owner->m_bSyncing, "Actual hosted path registers original non-cop actor");
        if (!owner) return 1;
        PedConfirm confirm;
        confirm.pedid = 1; confirm.tempid = owner->m_nTempId; confirm.requestToken = owner->m_requestToken;
        confirm.ownerid = 0; confirm.stamp = {++birth, 1, 0}; receive(confirm);
        expect(CNetworkPedManager::GetPed(1) == owner && owner->m_pPed == native,
            "Confirmation preserves original owning native instance");
        native->m_fHealth = 0;
        const auto tasks = deathTasks;
        int id = -1; NPCSync::Stamp proof;
        expect(CNetworkPedManager::GetOwnerDeathIdentity(native, id, proof) && id == 1 && proof.State(),
            "Original civilian/gang owner publishes terminal proof");
        const auto packets = GetPacketFactory().sent.size();
        NPCSync::Stamp repeated;
        expect(CNetworkPedManager::GetOwnerDeathIdentity(native, id, repeated) &&
            repeated.sequence == proof.sequence && GetPacketFactory().sent.size() == packets,
            "Repeated owner capture retains one death sequence and one seal");
        PedDeath echo; echo.pedid = id; echo.stamp = proof; echo.position = native->GetPosition(); receive(echo);
        PedOnFoot remote; remote.pedid = id; remote.stamp = {birth, 1, proof.sequence + 1};
        remote.healthSnapshot.iHealth = 100; receive(remote);
        expect(native->m_fHealth == 0 && deathTasks == tasks && !owner->m_replicaDeath,
            "Seal echo and foreign replay do not replace the original owner's native death task");
        expect(!native->m_nPedFlags.bDoesntDropWeaponsWhenDead && native->m_nMoneyCount == 137,
            "Original owner retains native weapon and money loot eligibility");
        CNetworkPedManager::Remove(owner); owner->DetachPed(); delete owner; delete native;
        CNetworkPedManager::Clear();
    }
    std::cout << checks << " non-cop corpse/owner assertions, " << failures << " failures\n";
    return failures ? 1 : 0;
}
