#include "client_doubles.h"
using namespace Packets::Peds;
int main()
{
    ModelInfo model;for(auto& ptr:CModelInfo::ms_modelInfoPtrs)ptr=&model;
    CPed local;local.m_nPedType=PED_TYPE_PLAYER1;CWorld::Players[0].m_pPed=&local;CPools::GetPedRef(&local);
    CNetworkPedManager::Init();gGameState=9;Events::processScriptsEvent.after.Fire();
    PedSpawn spawn;spawn.pedid=4;spawn.stamp={10,1,0};spawn.ownerid=1;receive(spawn);
    auto* wrapper=CNetworkPedManager::GetPed(4);expect(wrapper!=nullptr,"Actual native constructor binds initial NPC");
    if(!wrapper)return 2;auto* actor=wrapper->m_pPed;
    CAutomobile oldCar,newCar;CNetworkVehicle networkCar;networkCar.m_nVehicleId=7;networkCar.m_pVehicle=&newCar;
    CNetworkVehicleManager::vehicle=&networkCar;
    actor->m_pVehicle=&oldCar;actor->m_nPedFlags.bInVehicle=true;oldCar.m_apPassengers[0]=actor;
    PedReplay replay;replay.mode=3;replay.passenger.pedid=4;replay.passenger.vehicleid=7;replay.passenger.seatid=2;
    replay.passenger.stamp={10,1,1};const auto before=warps;receive(replay);
    expect(warps==before+1&&actor->m_pVehicle==&newCar&&newCar.m_apPassengers[2]==actor,
        "Authoritative retained passenger state must move NPC from different old vehicle to target seat");
    // Native recorded state can already refer to the right car, but wrong seat.
    actor->m_pVehicle=&newCar;newCar.m_apPassengers[0]=actor;newCar.m_apPassengers[2]=nullptr;
    replay.passenger.stamp.sequence=2;receive(replay);
    expect(newCar.m_apPassengers[2]==actor,"Authoritative passenger seat change must apply while already in target vehicle");
    CNetworkPedManager::Clear();std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;
}
