#include "server_doubles.h"
using namespace Packets::Peds;
int main()
{
    ENetPeer pa,pb;CNetworkPlayer a,b;a.m_pPeer=&pa;b.m_pPeer=&pb;b.m_iPlayerId=1;b.m_vitals.generation=11;
    CNetworkPlayerManager::m_pPlayers={&a,&b};
    auto* ped=new CNetworkPed(2,&a,MODEL_LAPD1,PED_TYPE_COP,{},RANDOM_CHAR);
    ped->m_generation=50;ped->m_ownerEpoch=1;CNetworkPedManager::Add(ped);
    PedDeath death;death.pedid=2;death.stamp={50,1,2};death.position={10,20,3};
    receive(death,&b);expect(!ped->m_deathStamp.State(),"Nonowner cannot seal a death");
    PedOnFoot later;later.pedid=2;later.stamp={50,1,3};later.healthSnapshot.iHealth=0;receive(later,&a);
    receive(death,&a);expect(ped->m_deathStamp.sequence==2&&ped->m_stateSequence==3,"Reliable seal survives newer SYNC without lowering sequence");
    expect(ped->m_deathPosition.x==10&&ped->m_deathProducerGeneration==10,"Death metadata retains original position and connection incarnation");
    expect(CNetworkPedManager::GetDeathProducer(&a,2,death.stamp),"Exact original producer proof available");
    auto sent=GetPacketFactory().sent.size();receive(death,&a);expect(GetPacketFactory().sent.size()==sent,"Duplicate seal is inert");
    auto wrong=death.stamp;++wrong.sequence;expect(!CNetworkPedManager::GetDeathProducer(&a,2,wrong),"Different death sequence rejected");
    expect(CNetworkPedManager::AssignOwner(ped,&b),"Ownership can transfer a retained corpse");
    expect(CNetworkPedManager::GetDeathProducer(&a,2,death.stamp)&&!CNetworkPedManager::GetDeathProducer(&b,2,death.stamp),"Transfer preserves exactly one original producer");
    auto second=death;second.stamp={50,2,4};receive(second,&b);expect(ped->m_deathStamp.epoch==1,"New owner cannot reseal same lifetime");
    later.stamp={50,2,4};later.healthSnapshot.iHealth=100;receive(later,&b);
    expect(ped->m_lastState.onFoot.healthSnapshot.iHealth==0,"Later alive SYNC cannot resurrect sealed corpse");
    GetPacketFactory().sent.clear();g_serverTime=2000;CNetworkPedManager::Replay(ped,&b);
    expect(GetPacketFactory().sent.size()==2&&GetPacketFactory().sent[0].packet->GetType()==ePacketType::PED_DEATH,"Join replay includes reliable original death plus current state");
    auto& seal=static_cast<PedDeath&>(*GetPacketFactory().sent[0].packet);
    expect(seal.stamp.epoch==1&&seal.serverTime==2000,"Replay retains sealed identity with fresh server timestamp");
    pa.state=0;expect(!CNetworkPedManager::GetDeathProducer(&a,2,death.stamp),"Disconnected producer rejected");pa.state=5;
    a.m_vitals.generation=12;expect(!CNetworkPedManager::GetDeathProducer(&a,2,death.stamp),"Reused pointer/slot with new incarnation rejected");
    CNetworkPedManager::Remove(ped);delete ped;
    expect(!CNetworkPedManager::GetDeathProducer(&a,2,death.stamp),"Removed lifetime supplies no drop proof");
    ped=new CNetworkPed(2,&b,MODEL_LAPD1,PED_TYPE_COP,{},RANDOM_CHAR);ped->m_generation=51;CNetworkPedManager::Add(ped);
    receive(death,&a);expect(!ped->m_deathStamp.State(),"Old seal cannot kill reused ped slot");
    CNetworkPedManager::Remove(ped);delete ped;
    std::cout<<checks<<" death producer/server assertions, "<<failures<<" failures\n";return failures?1:0;
}
