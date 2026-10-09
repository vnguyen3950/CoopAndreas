#define main owner_suite_main
#include "fire_client_tests.cpp"
#undef main
int main()
{
    CPlayerPed local;localPed=&local;CPools::ped.refs[&local]=100;CWorld::Players[0].m_pPed=&local;
    CNetworkPlayerManager::m_nMyId=1;CLocalPlayer::m_bIsHost=false;CFireSync::Init();
    Events::initScriptsEvent.Run();Events::processScriptsEvent.Run();CFireSync::Process();
    // First HELLO was sent, but its own birth binding is still in flight.
    expect(gameGeneration==1&&Bindings()[1].entity.generation==0,"First initialized scene has no received own binding yet");
    Events::initScriptsEvent.Run();Events::processScriptsEvent.Run();
    expect(gameGeneration==2&&ownBirthFloor==0,"Restart before first binding has no observed birth high-water mark");
    Packets::Fires::Bind oldBind;oldBind.epoch=1;oldBind.entity={FireSync::Kind::Player,1,1,10,1,0};
    Packets::Fires::Update oldState;oldState.state=Sample();oldState.state.target=oldBind.entity;
    // Old EVENT data can remain before the new binding on its ordered stream.
    // The fresh SYSTEM acknowledgement can arrive before that new EVENT bind.
    CFireSync::Receive(oldBind);CFireSync::Receive(oldState);
    Packets::Fires::Reset fresh;fresh.epoch=1;fresh.host=0;fresh.connection=10;fresh.gameGeneration=2;fresh.recipientBirth=2;
    serialize::MeasureStream measure;expect(static_cast<Packet&>(fresh).SerializeMeasure(measure),"Fresh current-game acknowledgement is valid under real codec");
    CFireSync::Receive(fresh);CFireSync::Process();
    expect(!local.m_pFire&&!CFireSync::AllowPedDamage(&local),
        "Current-game ack must not promote unseen previous-game binding into current native actor");
    TaskDamage(&local);expect(local.health==100,"Unseen old birth must not grant native damage before current own binding arrives");
    std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;
}
