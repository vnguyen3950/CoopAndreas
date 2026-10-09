#include <iostream>
#include "player_registry_doubles.h"
#include "registry_functions.inc"
static unsigned checks=0,failures=0;
static void expect(bool value,const char* message){++checks;if(!value){++failures;std::cout<<"FAIL: "<<message<<'\n';}}
static CNetworkPlayer* connected(int id)
{
    Packets::System::PlayerConnected p;p.payload.playerid=id;
    HandleConnected(&p);return CNetworkPlayerManager::GetPlayer(id);
}
static void duplicate()
{
    auto* first=connected(1);auto* ped=first->m_pPed;
    expect(first && ped && CNetworkPlayerManager::m_pPlayers.size()==1,"Actual connected handler creates one wrapper.");
    CNetworkPlayerManager::Add(first);
    expect(CNetworkPlayerManager::m_pPlayers.size()==1,"Re-adding the same pointer does not duplicate registry ownership.");
    auto* replacement=connected(1);
    expect(replacement && replacement->m_pPed && CNetworkPlayerManager::m_pPlayers.size()==1 && destroyed==1,
        "Duplicate slot tears down previous native actor before creating a replacement.");
    expect(live.size()==1 && removed==1,"No owned orphan actor remains after duplicate-slot replacement.");
    Packets::System::PlayerDisconnected departure;departure.payload.playerid=1;HandleDisconnected(&departure);
    expect(CNetworkPlayerManager::m_pPlayers.empty() && live.empty() && destroyed==2,
        "Ordinary PLAYER_DISCONNECTED follows guarded native destruction once.");
    CNetworkPlayerManager::Reset();CNetworkPlayerManager::Reset();
    expect(destroyed==2,"Repeated empty reset cannot double-delete.");
}
static void queued_reset()
{
    connected(1);auto main=std::this_thread::get_id();
    std::thread request([]{CNetworkPlayerManager::RequestReset();});request.join();
    expect(destroyed==0 && CNetworkPlayerManager::m_pPlayers.size()==1,"Networking-thread reset request does no native cleanup.");
    duringDestruction=[] {CNetworkPlayerManager::RequestReset();};
    CNetworkPlayerManager::ProcessPendingReset();duringDestruction={};
    expect(destroyed==1 && destructionThread==main && CNetworkPlayerManager::m_pPlayers.empty(),
        "Actual pending reset performs native cleanup on the caller's main/game path.");
    connected(2);CNetworkPlayerManager::ProcessPendingReset();
    expect(destroyed==2 && CNetworkPlayerManager::m_pPlayers.empty(),"Request arriving during teardown remains pending and is consumed next time.");
    connected(3);CNetworkPlayerManager::ProcessPendingReset();
    expect(CNetworkPlayerManager::m_pPlayers.size()==1,"Consumed request does not spuriously reset a later wrapper.");
    Packets::System::PlayerHandshake hello;hello.yourid=4;HandleHandshake(&hello);
    expect(destroyed==3 && CNetworkPlayerManager::m_pPlayers.empty() && CNetworkPlayerManager::m_nMyId==4,
        "Real handshake resets the old registry before incoming new-session wrappers.");
}
static void invalid_actors()
{
    auto* owned=connected(1);auto* recycled=owned->m_pPed;int old=owned->m_nPedRef;
    references.erase(old);references[old+1]=recycled;nativeReads=0;
    owned->DestroyPed();
    expect(!owned->m_pPed && owned->m_nPedRef==-1 && destroyed==0 && nativeReads==0,
        "Recycled pool reference is rejected before native dereference and clears wrapper ownership.");
    CNetworkPlayerManager::Reset();delete recycled;CWorld::Players[3].m_pPed=nullptr;
    auto* localActor=new CPlayerPed;references[200]=localActor;live.insert(localActor);
    CWorld::Players[0].m_pPed=localActor;CWorld::Players[3].m_pPed=localActor;
    CNetworkPlayer localAlias;localAlias.m_iPlayerId=1;localAlias.m_pPed=localActor;localAlias.m_nPedRef=200;
    auto count=destroyed;nativeReads=0;localAlias.DestroyPed();
    expect(destroyed==count && nativeReads==0 && !localAlias.m_pPed,"Local actor aliased in a remote slot is never dereferenced or deleted.");
    CWorld::Players[0].m_pPed=nullptr;CWorld::Players[3].m_pPed=nullptr;delete localActor;
    auto* unbound=new CPlayerPed;references[201]=unbound;live.insert(unbound);
    CNetworkPlayer orphan;orphan.m_iPlayerId=1;orphan.m_pPed=unbound;orphan.m_nPedRef=201;
    count=destroyed;nativeReads=0;orphan.DestroyPed();
    expect(destroyed==count && nativeReads==0 && !orphan.m_pPed,"Unbound actor is never deleted by a stale wrapper.");
    delete unbound;
    auto* normal=connected(1);auto* actor=normal->m_pPed;
    duringDestruction=[normal]{expect(!normal->m_pPed && normal->m_nPedRef==-1,"Ownership clears before native deleting destructor.");normal->DestroyPed();};
    count=destroyed;normal->DestroyPed();duringDestruction={};normal->DestroyPed();
    expect(destroyed==count+1 && !live.count(actor),"Native delete path frees exactly once across reentrant and repeated teardown.");
    CNetworkPlayerManager::Reset();expect(destroyed==count+1,"Wrapper destructor cannot repeat native deletion.");
}
static void failures_and_respawn()
{
    failure=CREATE_FAILURE;auto* p=connected(1);
    expect(!p->m_pPed && p->m_nPedRef==-1 && p->GetInternalId()==-1,"Failed CREATE leaves no bound ped and lookup cannot match an empty slot.");
    CNetworkPlayerManager::Reset();failure=GET_FAILURE;p=connected(1);
    expect(!p->m_pPed && p->m_nPedRef==-1 && destroyed==1 && live.empty(),"Failed GET cleans the known just-created actor once without dereferencing null.");
    CNetworkPlayerManager::Reset();failure=GET_NULL;p=connected(1);
    expect(!p->m_pPed && p->m_nPedRef==-1 && destroyed==2 && live.empty(),"Null pool lookup during GET fails closed and releases its known owned creation.");
    CNetworkPlayerManager::Reset();failure=NONE;p=connected(1);int original=p->m_nPedRef;
    p->Respawn();expect(destroyed==3 && p->m_pPed && p->m_nPedRef!=original && live.size()==1,
        "Actual respawn destroys old generation once and stores fresh full reference.");
    p->m_pPed->m_pVehicle=reinterpret_cast<void*>(1);CNetworkPlayerManager::Reset();
    expect(destroyed==4 && warps==1 && live.empty(),"Native vehicle unseat happens only after valid ownership and actor is freed once.");
}
int main(int argc,char** argv)
{
    if(argc!=2)return 2;std::string test=argv[1];
    if(test=="duplicate")duplicate();else if(test=="pending")queued_reset();else if(test=="invalid")invalid_actors();
    else if(test=="creation")failures_and_respawn();else return 2;
    std::cout<<test<<": "<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;
}
