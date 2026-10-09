#define ACTUAL_CONSTRUCTOR_TEST
#define main client_suite_main
#include "trailer_client_tests.cpp"
#undef main
#include "vehicle_hosted_extracted.inc"
int main(){Start();CVehicleModelInfo model;CModelInfo::ms_modelInfoPtrs[435]=&model;model.m_nVehicleType=VEHICLE_TRAILER;
 CPools::pool.freeSpaces=0;Packets::Vehicles::VehicleSpawn spawn;spawn.vehicleid=8;spawn.generation=10;spawn.modelid=435;
 ReceiveVehicleSpawn(&spawn);expect(!CNetworkVehicleManager::FindVehicle(8)&&pendingSpawns[8]&&pendingSpawns[8]->generation==10,
 "Actual constructor/handler retains exact birth when native pool is full");expect(sdkConstructors==0&&allocationCalls==0,"Capacity guard prevents native constructor/allocation calls");
 CPools::pool.freeSpaces=1;allocationFails=true;CTrailerSync::Process();
 expect(!CNetworkVehicleManager::FindVehicle(8)&&pendingSpawns[8],"Native allocator nullptr retains exact birth without exposing stale wrapper");
 expect(sdkConstructors==0,"Placement construction never executes on nullptr native allocation");
 allocationFails=false;CStreaming::loadSucceeds=false;CStreaming::ms_aInfoForModel[435].m_nLoadState=0;CTimer::m_snTimeInMilliseconds+=500;CTrailerSync::Process();
 expect(pendingSpawns[8]&&sdkConstructors==0,"Unloaded model retains pending birth without native constructor");
 CStreaming::loadSucceeds=true;CTimer::m_snTimeInMilliseconds+=500;CTrailerSync::Process();
 auto* v=CNetworkVehicleManager::GetVehicle(8);expect(v&&v->m_generation==10&&v->m_createdScene==scene&&sdkConstructors==1,
 "Actual constructor/CreateVehicle retry restores valid native mapping with exact birth and scene");
 expect(!pendingSpawns[8],"Successful native retry consumes bounded pending lifecycle");
 auto p=v->m_pVehicle;CTrailerSync::NativeRemoved(p);CNetworkVehicleManager::Remove(v);delete p;delete v;
 CVehicle localCar;localCar.m_nModelIndex=403;CPools::pool.refs[&localCar]=900;
 CNetworkVehicle occupied;for(auto& slot:CNetworkVehicleManager::m_apTempVehicles)slot=&occupied;
 const auto beforeSend=GetPacketFactory().sent.size();auto* rejected=CNetworkVehicle::CreateHosted(&localCar);
 expect(!rejected&&GetPacketFactory().sent.size()==beforeSend,"Actual CreateHosted rejects full temp registry without sending temp ID 255");
 expect(CPools::pool.IsObjectValid(&localCar)&&localCar.m_nModelIndex==403,"Temp exhaustion frees only wrapper and preserves native local car");
 for(auto& slot:CNetworkVehicleManager::m_apTempVehicles)slot=nullptr;
 auto* temporary=CNetworkVehicle::CreateHosted(&localCar);const auto oldToken=temporary->m_requestToken;const auto tempSlot=temporary->m_nTempId;
 CTrailerSync::NativeRemoved(&localCar);expect(!CNetworkVehicleManager::m_apTempVehicles[tempSlot],"Destroyed unconfirmed native car releases exact temporary registry slot");
 CPools::pool.refs[&localCar]=901;auto* fresh=CNetworkVehicle::CreateHosted(&localCar);
 expect(fresh->m_nTempId==tempSlot&&fresh->m_requestToken!=oldToken,"Reused temp slot carries fresh monotonic nonce");
 Packets::Vehicles::VehicleConfirm late;late.tempid=tempSlot;late.vehicleid=12;late.generation=12;late.requestToken=oldToken;ReceiveVehicleConfirm(&late);
 expect(!fresh->m_generation,"Old confirmation cannot adopt new native car after temp slot reuse");
 expect(GetPacketFactory().sent.back()->GetType()==ePacketType::VEHICLE_REMOVE &&
        static_cast<Packets::Vehicles::VehicleRemove*>(GetPacketFactory().sent.back().get())->generation==12 &&
        static_cast<Packets::Vehicles::VehicleRemove*>(GetPacketFactory().sent.back().get())->vehicleid==12,
        "Known orphan confirmation retires exact old server birth without consuming replacement temp token");
 late.requestToken=fresh->m_requestToken;late.generation=13;ReceiveVehicleConfirm(&late);expect(fresh->m_generation==13,"Matching nonce confirms exact replacement native lifetime");
 const auto duplicateCount=GetPacketFactory().sent.size();ReceiveVehicleConfirm(&late);
 expect(GetPacketFactory().sent.size()==duplicateCount && CNetworkVehicleManager::GetVehicle(12)==fresh,
        "Duplicate confirmation of valid current mapping cannot emit orphan removal");
 CNetworkVehicleManager::Remove(fresh);fresh->m_pVehicle=nullptr;delete fresh;
 std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;}
