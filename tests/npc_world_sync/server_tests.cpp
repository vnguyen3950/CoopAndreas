#include "server_doubles.h"
using namespace Packets::Peds;
int main(){
 ENetPeer hostPeer,guestPeer,otherPeer;CNetworkPlayer host,guest,outsider;
 host.m_pPeer=&hostPeer;host.m_bIsHost=true;guest.m_pPeer=&guestPeer;guest.m_iPlayerId=1;outsider.m_pPeer=&otherPeer;
 CNetworkPlayerManager::m_pPlayers={&host,&guest};
 PedSpawn spawn;spawn.requestToken=1;spawn.tempid=3;receive(spawn,&host);
 auto* ped=CNetworkPedManager::GetPed(0);
 expect(ped && ped->m_generation>0 && ped->m_pSyncer==&host,"Real spawn handler assigns lifetime and owner.");
 expect(GetPacketFactory().sent.size()==2 && GetPacketFactory().sent[1].packet->GetType()==ePacketType::PED_CONFIRM,"Reliable spawn/confirmation recorded.");
 auto count=CNetworkPedManager::m_pPeds.size();auto sent=GetPacketFactory().sent.size();
 PedSpawn duplicate;duplicate.requestToken=1;duplicate.tempid=4;receive(duplicate,&host);
 expect(CNetworkPedManager::m_pPeds.size()==count&&GetPacketFactory().sent.size()==sent,"Creation token cannot alias a reused temporary slot.");
 PedSpawn forged;forged.requestToken=2;forged.tempid=5;receive(forged,&outsider);
 expect(CNetworkPedManager::m_pPeds.size()==count,"Unregistered peer cannot create a lifetime.");
 forged.pedType=PED_TYPE_PLAYER1;receive(forged,&host);expect(CNetworkPedManager::m_pPeds.size()==count,"Player ped type cannot enter NPC allocator.");
 PedOnFoot state;state.pedid=0;state.stamp={ped->m_generation,ped->m_ownerEpoch,1};state.healthSnapshot.iHealth=0;
 state.weaponSnapshot.iWeaponType=WEAPON_AK47;state.weaponSnapshot.nAmmo=30;state.pos={100,200,20};state.area=18;
 receive(state,&guest);expect(!ped->m_hasState,"Registered foreign owner cannot publish native state.");
 receive(state,&host);expect(ped->m_hasState && ped->m_lastState.onFoot.healthSnapshot.iHealth==0 && ped->m_vecPos.x==100,"Actual handler retains death/weapon/area/position for join replay.");
 auto wrong=state;wrong.stamp.sequence=2;wrong.stamp.generation+=1;wrong.pos.x=200;receive(wrong,&host);
 expect(ped->m_stateSequence==1 && ped->m_vecPos.x==100,"Wrong lifetime cannot replace cached state.");
 wrong=state;wrong.stamp.sequence=3;wrong.stamp.epoch+=1;receive(wrong,&host);
 expect(ped->m_stateSequence==1,"Unconfirmed owner epoch cannot publish.");
 wrong=state;wrong.pos.x=std::numeric_limits<float>::quiet_NaN();receive(wrong,&host);
 expect(ped->m_vecPos.x==100,"Malformed finite payload is rejected before cache/native replay.");
 receive(state,&host);expect(ped->m_stateSequence==1,"Duplicate snapshot does not consume another sequence.");
 GetPacketFactory().sent.clear();auto join=ped->SpawnPacket();GetPacketFactory().Send(join,&guest);CNetworkPedManager::Replay(ped,&guest);
 expect(GetPacketFactory().sent.size()==2 && GetPacketFactory().sent[0].packet->GetType()==ePacketType::PED_SPAWN && GetPacketFactory().sent[1].packet->GetType()==ePacketType::PED_REPLAY,"Actual spawn builder and replay preserve ordered lifecycle domain.");
 expect(GetPacketFactory().sent[1].packet->serverTime==g_serverTime,"Reused replay uses current server timestamp, not earlier join time.");
 // Real BuildPacketStream mutates an outer EVENT timestamp. Repeat a join after
 // the first replay and feed both deliveries through actual client insertion.
 g_serverTime=2000;GetPacketFactory().sent.clear();join=ped->SpawnPacket();GetPacketFactory().Send(join,&guest);CNetworkPedManager::Replay(ped,&guest);
 CPacketBuffer joinBuffer;for(auto&record:GetPacketFactory().sent)joinBuffer.Receive(record.packet->Clone());
 expect(joinBuffer.m_packets.front()->GetType()==ePacketType::PED_SPAWN&&joinBuffer.m_packets.back()->GetType()==ePacketType::PED_REPLAY,"Repeated cached replay cannot timestamp-sort before new join spawn.");
 expect(ped->m_lastState.serverTime==0,"Actual packet builder does not persist a delivery timestamp in retained state.");
 PedClaimOnRelease claim;claim.pedid=0;claim.stamp=ped->GetStamp();receive(claim,&guest);receive(claim,&guest);
 expect(guest.m_vPedClaims.size()==1,"Claims are unique and lifetime-bound.");
 GetPacketFactory().sent.clear();
 PedRemove release;release.pedid=0;release.stamp=ped->GetStamp();receive(release,&host);
 CPacketBuffer transferBuffer;for(auto&record:GetPacketFactory().sent)if(record.packet->GetType()==ePacketType::ASSIGN_PED||record.packet->GetType()==ePacketType::PED_REPLAY)transferBuffer.Receive(record.packet->Clone());
 expect(transferBuffer.m_packets.front()->GetType()==ePacketType::ASSIGN_PED,"New-epoch retained replay remains after explicit assignment through actual timestamp buffer.");
 expect(ped->m_pSyncer==&guest && ped->m_ownerEpoch==2 && ped->m_lastState.onFoot.stamp.epoch==2,"Release transfers explicit ownership and rebases retained state epoch.");
 expect(guest.m_vPedClaims.empty(),"Transfer clears stale claims.");
 receive(state,&host);expect(ped->m_pSyncer==&guest && ped->m_stateSequence==1,"Previous owner cannot replay late snapshots.");
 receive(release,&host);expect(CNetworkPedManager::GetPed(0)==ped,"Previous-epoch removal cannot delete new owner.");
 sent=GetPacketFactory().sent.size();expect(CNetworkPedManager::AssignOwner(ped,&guest)&&GetPacketFactory().sent.size()==sent,"Repeated explicit assignment is idempotent.");
 auto snapshot=ped->GetStamp();ped->m_ownerEpoch=NPCSync::MaxCounter;
 expect(!CNetworkPedManager::AssignOwner(ped,&host)&&ped->m_pSyncer==&guest,"Owner epoch exhaustion fails before mutation.");ped->m_ownerEpoch=snapshot.epoch;
 expect(CNetworkPedManager::AssignOwner(ped,&host),"Host can restore unpinned current NPC authority.");
 claim.stamp=ped->GetStamp();receive(claim,&guest);
 PedPin earlyPin;earlyPin.requestToken=ped->m_requestToken;earlyPin.pinned=true;receive(earlyPin,&host);
 expect(ped->m_bPinned && guest.m_vPedClaims.empty(),"Exact host creation token pins before confirmation roundtrip and clears earlier claims.");
 PedPin pin;pin.pedid=0;pin.stamp=ped->GetStamp();pin.pinned=true;receive(pin,&host);
 expect(ped->m_bPinned && ped->m_pSyncer==&host && guest.m_vPedClaims.empty(),"Host pin dominates a previously queued claim without transferring wave AI.");
 receive(claim,&guest);expect(guest.m_vPedClaims.empty(),"Pinned wave rejects subsequent claim.");
 pin.pinned=false;receive(pin,&guest);expect(ped->m_bPinned,"Guest cannot unpin host native wave.");
 pin.stamp.epoch-=1;receive(pin,&host);expect(ped->m_bPinned,"Stale owner epoch cannot unpin.");
 auto oldGeneration=ped->m_generation;CNetworkPedManager::RemoveAllHostedAndNotify(&host);
 expect(!CNetworkPedManager::GetPed(0) && guest.m_vPedClaims.empty(),"Pinned host departure deletes actor instead of native AI migration.");
 PedSpawn replacement;replacement.requestToken=2;replacement.tempid=3;receive(replacement,&guest);ped=CNetworkPedManager::GetPed(0);
 expect(ped && ped->m_generation>oldGeneration,"Reused slot gets globally newer generation.");
 release.stamp={oldGeneration,1,0};receive(release,&guest);expect(CNetworkPedManager::GetPed(0)==ped,"Delayed old-lifetime delete cannot erase replacement.");
 CNetworkVehicle vehicle;CNetworkVehicleManager::current=&vehicle;
 PedPassengerSync passenger;passenger.stamp={ped->m_generation,ped->m_ownerEpoch,1};passenger.seatid=7;receive(passenger,&guest);
 expect(ped->m_hasState && ped->m_lastState.mode==3,"Native eighth passenger is handled without indexing player slot array past its bound.");
 passenger.stamp.sequence=2;passenger.seatid=0;vehicle.m_pPlayers[1]=&host;receive(passenger,&guest);
 expect(ped->m_stateSequence==1,"NPC cannot displace recorded player passenger.");
 CNetworkPedManager::DeleteAndNotify(ped);
 // Exhaust the real configured slot set; never forward a -1 network ID.
 for(int id=0;id<Config::MAX_SERVER_PEDS;++id){auto*entry=new CNetworkPed(id,&host,MODEL_MALE01,PED_TYPE_CIVMALE,{},RANDOM_CHAR);entry->m_generation=CNetworkPedManager::AllocateGeneration();CNetworkPedManager::Add(entry);}
 auto fullCount=CNetworkPedManager::m_pPeds.size();sent=GetPacketFactory().sent.size();
 PedSpawn overflow;overflow.requestToken=99;overflow.tempid=8;receive(overflow,&guest);
 expect(CNetworkPedManager::GetFreeId()==-1&&CNetworkPedManager::m_pPeds.size()==fullCount&&GetPacketFactory().sent.size()==sent,"Full server capacity fails closed without malformed ID or dangling allocation.");
 auto all=CNetworkPedManager::m_pPeds;for(auto*entry:all)CNetworkPedManager::DeleteAndNotify(entry);
 std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;
}
