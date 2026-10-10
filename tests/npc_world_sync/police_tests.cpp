#include "client_doubles.h"
extern "C" __declspec(dllimport) unsigned int __stdcall SetErrorMode(unsigned int);
#include "cop_control.inc"
using namespace Packets::Peds;
int main(int argc,char**)
{
    ModelInfo model; for(auto& ptr:CModelInfo::ms_modelInfoPtrs)ptr=&model;
    CPed local(PED_TYPE_PLAYER1);CWorld::Players[0].m_pPed=&local;CPools::GetPedRef(&local);
    CNetworkPedManager::Init();gGameState=9;Events::processScriptsEvent.after.Fire();
    SetErrorMode(0x1|0x2);
    if (argc > 1) {
        PedSpawn failure;failure.pedid=1;failure.ownerid=1;failure.stamp={99,1,0};failure.pedType=PED_TYPE_COP;failure.modelId=MODEL_SFPD1;
        nativeCopCityModel=MODEL_LAPD1;copAllocationFails=true;
        const auto creates=nativeCreates;const auto writes=copModelWrites;
        receive(failure);
        expect(!CNetworkPedManager::GetPed(1)&&Deferred().size()==1,"Allocation-null retains exact pending birth without native wrapper");
        expect(nativeCreates==creates&&copModelWrites==writes,"Null allocation invokes neither native cop constructor nor skin restoration");
        copAllocationFails=false;tick+=300;CNetworkPedManager::ProcessPendingNative();
        auto* recovered=CNetworkPedManager::GetPed(1);
        expect(recovered&&recovered->m_generation==99&&recovered->m_pPed->m_nModelIndex==MODEL_SFPD1,"Retry restores exact requested cop skin and birth");
        expect(Deferred().empty(),"Successful retry consumes only the matching pending creation");
        CNetworkPedManager::Clear();std::cout<<checks<<" cop allocation assertions, "<<failures<<" failures\n";return failures?1:0;
    }
    uint32_t birth=100;
    for(int city:{MODEL_LAPD1,MODEL_SFPD1,MODEL_LVPD1})
    {
        nativeCopCityModel=city;
        for(int desired=MODEL_LAPD1;desired<=MODEL_ARMY;++desired)
        {
            PedSpawn spawn;spawn.pedid=1;spawn.ownerid=1;spawn.stamp={++birth,1,0};
            spawn.modelId=eModelID(desired);spawn.pedType=PED_TYPE_COP;receive(spawn);
            auto* ped=CNetworkPedManager::GetPed(1);
            expect(ped&&ped->HasValidPed(),"Every stock city/sheriff/biker/SWAT/FBI/army cop creates through actual constructor");
            expect(ped&&ped->m_pPed->m_nModelIndex==desired,"Replica preserves requested skin across different native local city");
            CNetworkPedManager::Clear();
        }
    }
    PedSpawn spawn;spawn.pedid=1;spawn.ownerid=1;spawn.stamp={++birth,1,0};spawn.modelId=MODEL_LAPDM1;spawn.pedType=PED_TYPE_COP;receive(spawn);
    PedOnFoot state;state.pedid=1;state.stamp={birth,1,1};state.healthSnapshot.iHealth=0;state.moveState=PEDMOVE_STILL;receive(state);
    auto* ped=CNetworkPedManager::GetPed(1);
    expect(ped&&ped->m_pPed->m_fHealth==0,"Actual authoritative zero-health snapshot is accepted");
    expect(ped&&(ped->m_pPed->m_ePedState==PEDSTATE_DIE||ped->m_pPed->m_ePedState==PEDSTATE_DEAD),"Zero health must enter a native corpse/death state instead of alive idle");
    auto tasks=deathTasks;state.stamp.sequence=2;receive(state);
    expect(deathTasks==tasks&&ped->m_pPed->m_nPedFlags.bDoesntDropWeaponsWhenDead&&ped->m_pPed->m_nMoneyCount==0,"Repeated zero health cannot restart task or produce replica loot");
    state.stamp.sequence=3;state.healthSnapshot.iHealth=100;receive(state);
    expect(ped->m_pPed->m_fHealth==0,"Higher sequence alive state cannot resurrect native corpse");
    PedDeath seal;seal.pedid=1;seal.stamp={birth,1,1};receive(seal);
    expect(ped->m_deathStamp.sequence==1&&deathTasks==tasks,"Seal after SYNC preserves immutable death without restarting");
    PedHooks::ProcessCopControl(static_cast<CCopPed*>(ped->m_pPed));expect(baseCopControls==1&&ownerCopControls==0,"Exact bound replica uses generic native control only");
    AssignPedSyncer transfer;transfer.pedid=1;transfer.stamp={birth,2,3};transfer.ownerid=0;receive(transfer);
    int id;NPCSync::Stamp proof;expect(!CNetworkPedManager::GetOwnerDeathIdentity(ped->m_pPed,id,proof),"Corpse transfer cannot gain a native loot allowance");
    receive(seal);expect(deathTasks==tasks,"Original old-epoch seal remains inert after transfer");
    CNetworkPedManager::Clear();
    spawn.stamp={++birth,1,0};spawn.ownerid=0;receive(spawn);ped=CNetworkPedManager::GetPed(1);ped->m_bSyncing=true;
    ped->m_pPed->m_fHealth=0;
    expect(CNetworkPedManager::GetOwnerDeathIdentity(ped->m_pPed,id,proof)&&proof.State(),"Actual owner reserves reliable seal before native loot");
    auto count=GetPacketFactory().sent.size();NPCSync::Stamp repeat;
    expect(CNetworkPedManager::GetOwnerDeathIdentity(ped->m_pPed,id,repeat)&&repeat.sequence==proof.sequence&&GetPacketFactory().sent.size()==count,"Repeated native drop context returns same seal without another packet");
    PedHooks::ProcessCopControl(static_cast<CCopPed*>(ped->m_pPed));expect(ownerCopControls==1,"Actual owner retains original cop control");
    CPools::pool.refs.erase(ped->m_nPedPoolRef);expect(!CNetworkPedManager::GetOwnerDeathIdentity(ped->m_pPed,id,repeat),"Recycled native ped cannot produce loot identity");
    CNetworkPedManager::Clear();
    CCopPed untracked(COP_TYPE_CITYCOP);PedHooks::ProcessCopControl(&untracked);expect(ownerCopControls==2,"Untracked/offline path retains original cop control");
    gGameState=0;Events::initScriptsEvent.before.Fire();spawn.ownerid=1;spawn.stamp={++birth,2,0};receive(spawn);
    seal.stamp={birth,1,2};receive(seal);
    gGameState=9;Events::processScriptsEvent.after.Fire();tick+=300;CNetworkPedManager::ProcessPendingNative();
    ped=CNetworkPedManager::GetPed(1);expect(ped&&ped->m_replicaDeath&&ped->m_deathStamp.epoch==1,"Dormant join replay applies original producer seal after current ownership assignment");
    CNetworkPedManager::Clear();std::cout<<checks<<" police constructor/handler assertions, "<<failures<<" failures\n";return failures?1:0;
}
