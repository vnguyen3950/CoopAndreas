#define main OriginalNativeSuite
#include "tests.cpp"
#undef main
int main(){
    Setup();hasGrant=false;view.rows={};mappings={};capturedDeaths={};capturedKinds={};copContext={};
    CPed native;native.m_nPedType=PED_TYPE_GANG1;native.m_nModelIndex=102;native.m_nCreatedBy=2;
    CPools::pedRefs={{native.poolRef,&native}};deathIdentityAvailable=true;originalCreation=1;
    generated=0;nativeWeaponDropCalls=0;dropIndex=2;generateOriginal=reinterpret_cast<void*>(&RecordedGenerate);
    auto method=&CPed::RecordedWeaponDrops;std::memcpy(&weaponDropsOriginal,&method,sizeof method);
    recordedWeaponDrops=[](CPed*){GenerateHook({},346,4,15,0,false,nullptr);};
    WeaponDropsHook(&native,nullptr);
    expect(nativeWeaponDropCalls==1&&generated==1,"Authenticated original owner executes unchanged native weapon routine even when native lifetime marker is mission");
    expect(CopCreates()==1,"Original random-created wrapper provenance publishes supported native output despite replica lifetime marker2");
    bool originalSeal=false;for(const auto&p:GetPacketFactory().sent)if(auto*a=dynamic_cast<Packets::Pickups::Action*>(p.get()))if(a->item.cop.Present())originalSeal=PickupSync::SameSeal(a->item.cop.death,deathIdentity);
    expect(originalSeal,"Provenance correction must retain exact original producer seal, not ambient fallback");
    CPickupSync::EndCopDrops();capturedDeaths={};capturedKinds={};mappings={};view.rows={};GetPacketFactory().sent.clear();
    originalCreation=2;generated=0;WeaponDropsHook(&native,nullptr);
    expect(CopCreates()==0,"An actually mission-created original wrapper stays excluded even if native output exists");
    originalCreation=1;deathIdentityAvailable=false;generated=0;nativeWeaponDropCalls=0;WeaponDropsHook(&native,nullptr);
    expect(nativeWeaponDropCalls==0&&generated==0&&CopCreates()==0,"Wrapper provenance never bypasses authenticated owner/seal rejection");
    deathIdentityAvailable=true;
    const unsigned before=RuntimeDiagnostics::calls;
    for(unsigned i=0;i<1000;++i)Trace(TraceStage::Death,"test-cap",9,6,3,1);
    expect(RuntimeDiagnostics::calls-before<=8,"Pickup diagnostic stage cap prevents repeated callbacks or frame retries flooding common sink");
    bool fieldsOnly=true;for(const auto&line:RuntimeDiagnostics::reasons)fieldsOnly&=line.find("@") == std::string::npos&&line.find("password")==std::string::npos;
    expect(fieldsOnly,"Recorded pickup diagnostics include fixed scopes and fields only, no names or credentials");
    for(const auto&line:RuntimeDiagnostics::reasons)std::cout<<"JSON "<<line<<'\n';
    std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;
}
