#define main OriginalNativeSuite
#include "tests.cpp"
#undef main
int main(){
    Setup();hasGrant=false;view.rows={};mappings={};GetPacketFactory().sent.clear();RuntimeDiagnostics::reasons.clear();
    CNetworkPlayerManager::m_nMyId=1;CLocalPlayer::m_bIsHost=false;localLife.generation=11;
    Packets::Pickups::State state;state.reset=false;state.epoch=1;state.host=0;state.recipient={11,2,7,0,0};
    state.row.item.id=51;state.row.item.epoch=1;state.row.item.revision=1;state.row.item.creation=1;
    state.row.item.owner=0;state.row.item.model=346;state.row.item.type=4;state.row.item.ammo=15;state.row.stage=PickupSync::Stage::Active;
    CStreaming::ms_aInfoForModel[346].m_nLoadState=0;CPickupSync::Receive(state);
    for(unsigned i=0;i<32;++i)CPickupSync::Process(); // Real missing-model retry path, not direct trace calls.
    unsigned waiting=0;for(const auto&line:RuntimeDiagnostics::reasons)waiting+=line.find("model-wait")!=std::string::npos;
    expect(waiting==8,"Repeated actual replica model waits cap their fixed reason at eight records");
    replicaGenerate=[](CVector pos,uint32_t model,uint8_t type,uint32_t ammo){dropIndex=2;return RecordedGenerate(pos,model,type,ammo,0,false,nullptr);};
    CStreaming::ms_aInfoForModel[346].m_nLoadState=LOADSTATE_LOADED;CPickupSync::Process();
    unsigned generatedEvents=0;for(const auto&line:RuntimeDiagnostics::reasons)generatedEvents+=line.find("generated")!=std::string::npos;
    expect(generatedEvents==1&&mappings[2].replica,"Successful actual replica creation remains observable after more than eight wait frames");
    expect(nativeCalls==0&&nativeQueryFlags==0,"Diagnostic transition coverage changes no native collection behavior");
    for(unsigned i=0;i<16;++i){state.row.item.id=100+i;state.row.item.creation=100+i;state.row.item.revision=1;CPickupSync::Receive(state);}
    unsigned rows=0;for(const auto&line:RuntimeDiagnostics::reasons)rows+=line.find("\"reason\":\"row\"")!=std::string::npos;
    expect(rows==8,"Repeated initial rows cap only their recorded fixed reason");
    ++state.recipient.birth;CPickupSync::Receive(state);
    bool rejected=false;for(const auto&line:RuntimeDiagnostics::reasons)rejected|=line.find("recipient-life-mismatch")!=std::string::npos;
    expect(rejected,"A distinct receiver failure remains observable after initial row cap");
    for(const auto&line:RuntimeDiagnostics::reasons)std::cout<<"JSON "<<line<<'\n';
    std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;
}
