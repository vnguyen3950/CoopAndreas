#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#include <type_traits>
#include <limits>
#include <iostream>

namespace Probe {
inline std::string mode;
inline DWORD tick = 100;
inline unsigned modules = 0, directories = 0, opens = 0, writes = 0;
inline std::vector<std::string> records;
inline std::wstring path;
DWORD WINAPI Tick() { return tick; }
DWORD WINAPI Module(HMODULE m, LPWSTR out, DWORD capacity) {
    ++modules;
    if (mode == "module-fail") return 0;
    if (mode == "module-truncated") return capacity;
    if (mode == "module-noslash") { wcscpy_s(out, capacity, L"fixture.exe"); return 11; }
    if (mode == "module-long") { std::wstring s=L"C:\\"+std::wstring(180,L'x')+L"\\fixture.exe"; wcscpy_s(out,capacity,s.c_str()); return DWORD(s.size()); }
    return ::GetModuleFileNameW(m, out, capacity);
}
BOOL WINAPI Directory(LPCWSTR p, LPSECURITY_ATTRIBUTES security) {
    ++directories;
    if (mode == "directory-fail") { ::SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return ::CreateDirectoryW(p, security);
}
HANDLE WINAPI File(LPCWSTR p, DWORD access, DWORD sharing, LPSECURITY_ATTRIBUTES security,
                   DWORD creation, DWORD attributes, HANDLE templateFile) {
    ++opens; path=p;
    if (mode == "file-fail") { ::SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
    return ::CreateFileW(p, access, sharing, security, creation, attributes, templateFile);
}
BOOL WINAPI Write(HANDLE h, LPCVOID data, DWORD count, LPDWORD written, LPOVERLAPPED overlapped) {
    ++writes;
    if (mode == "write-fail") { *written=0; ::SetLastError(ERROR_DISK_FULL); return FALSE; }
    if (mode == "partial-write") count/=2;
    const BOOL ok = ::WriteFile(h, data, count, written, overlapped);
    if (*written) records.emplace_back(static_cast<const char*>(data), *written);
    return ok;
}
static_assert(std::is_same_v<decltype(&Tick), decltype(&::GetTickCount)>);
static_assert(std::is_same_v<decltype(&Module), decltype(&::GetModuleFileNameW)>);
static_assert(std::is_same_v<decltype(&Directory), decltype(&::CreateDirectoryW)>);
static_assert(std::is_same_v<decltype(&File), decltype(&::CreateFileW)>);
static_assert(std::is_same_v<decltype(&Write), decltype(&::WriteFile)>);
}
// Actual Win32 functions remain used in the default path. Failure injection
// uses exact SDK WINAPI prototypes; all paths stay in this executable's cache.
#define GetTickCount Probe::Tick
#define GetModuleFileNameW Probe::Module
#define CreateDirectoryW Probe::Directory
#define CreateFileW Probe::File
#define WriteFile Probe::Write
#include "runtime_diagnostics.h"
#include "native_doubles.h"
#include "functions.inc"

unsigned assertions=0, failures=0;
void Check(bool pass, const char* message) { ++assertions; if(!pass) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }
long long Field(const std::string& record, const char* name) {
    const auto token=std::string("\"")+name+"\":";const auto p=record.find(token);
    if (p==std::string::npos) { Check(false,"Expected diagnostic field exists"); return -99999; }
    return std::stoll(record.substr(p+token.size()));
}
bool Event(const char* name) { return !Probe::records.empty() && Probe::records.back().find(std::string("\"event\":\"")+name+"\"")!=std::string::npos; }
void Sample() { const auto before=Snapshot(); CEntryExitDiagnostics::Process(); Check(before==Snapshot(),"Sampler never changes original gameplay gates or bindings"); }
void Next() { Probe::tick+=5000; Sample(); }
void ValidJSON() {
    if(RuntimeDiagnostics::file!=INVALID_HANDLE_VALUE) {
        ::FlushFileBuffers(RuntimeDiagnostics::file);
        const auto n=::WideCharToMultiByte(CP_UTF8,0,Probe::path.c_str(),-1,nullptr,0,nullptr,nullptr);
        std::string p(n,0);::WideCharToMultiByte(CP_UTF8,0,Probe::path.c_str(),-1,p.data(),n,nullptr,nullptr);p.resize(n-1);
        std::cout << "VALID_JSON_PATH=" << p << '\n';
    }
}
int main(int argc, char** argv) {
    if(argc!=2) return 2;
    const std::string scenario=argv[1];
    CWorld::Players[0].m_pPed=&localPed;
    CEntryExit nearEntry, farEntry;
    nearEntry.m_nArea=2; nearEntry.m_nFlags.bEnableAccess=true;
    nearEntry.m_recEntrance={-2,-4,2,4};
    farEntry.m_nArea=3; farEntry.m_recEntrance={10,10,12,12};
    CEntryExitManager::mp_poolEntryExits={&nearEntry,&farEntry};
    if(scenario=="state") {
        localPad.DisablePlayerControls=0xA220; CWorld::Players[1].m_pPed=&otherPed;
        CCutsceneMgr::ms_cutsceneProcessing=true; CReplay::Mode=1;
        CEntryExitManager::ms_bDisabled=true; CEntryExitManager::ms_exitEnterState=3;
        CTheScripts::ScriptSpace[1]=1; CSessionSync::ready=false; localPed.eligible=false;
        Packet first,second;first.serverTime=0xFFFFFFFFu;second.serverTime=3;
        GetPacketBuffer().m_packets={&first,&second};g_serverTime=0xFFFFFFFEu;
        Sample();Check(Event("state"),"Ready gameplay emits state despite gates being set");
        const auto& line=Probe::records.back();
        for(const auto& item:std::vector<std::pair<const char*,long long>>{{"controls",0xA220},{"safe",1},{"debug_ui",1},{"cutscene",1},{"native_coop",1},{"replay",1},{"disabled",1},{"transition",3},{"native_player0",1},{"native_player1",1},{"can_start_native",0},{"wallet_ready",0},{"mission",1},{"pool",2},{"access_enabled",1},{"nearest_flags",0x4000},{"nearest_area",2},{"buffered",2},{"next_time",4294967295LL},{"server_time",4294967294LL}})
            Check(Field(line,item.first)==item.second,"Full gate, flags and unsigned timestamp values preserved in JSON");
        Check(GetPacketBuffer().m_packets.front()==&first && first.serverTime==0xFFFFFFFFu,"Sampler neither drains nor retimestamps queue");
        Check(localPed.eligibilityCalls==1,"Native eligibility observed once after binding validation");
        ValidJSON();
    } else if(scenario=="sampling" || scenario=="wrap") {
        if(scenario=="wrap") Probe::tick=0xFFFFFF00u;
        Sample();const auto count=Probe::records.size();Check(count==2,"First sample emits one header and state");
        const DWORD initial=Probe::tick;Probe::tick=initial+4999;Sample();Check(Probe::records.size()==count,"No sample before five seconds, including wrap");
        Probe::tick=initial+5000;Sample();Check(Probe::records.size()==count+1,"Exact five-second boundary samples");
        CNetwork::m_bConnected=false;Probe::tick+=5000;Sample();Check(Probe::records.size()==count+1,"Disconnected sampler is inert");
        CNetwork::m_bConnected=true;Sample();Check(Probe::records.size()==count+2,"Disconnected call did not advance sample baseline");ValidJSON();
    } else if(scenario=="unready") {
        CNetwork::m_bAuthenticated=false;Sample();Check(Event("unready") && Field(Probe::records.back(),"auth")==0,"Unauthenticated connection only emits unready");
        CNetwork::m_bAuthenticated=true;gGameState=8;Next();Check(Field(Probe::records.back(),"game_state")==8,"Menu/loading state is observed without actor use");
        gGameState=9;CWorld::PlayerInFocus=1;Next();Check(Field(Probe::records.back(),"focus")==1,"Foreign focus guarded");
        CWorld::PlayerInFocus=0;CPools::ms_pPedPool=nullptr;Next();Check(Field(Probe::records.back(),"ped_pool")==0,"Missing pool guarded");
        Check(localPed.positionCalls==0 && localPed.eligibilityCalls==0,"All unready paths avoid native actor methods");ValidJSON();
    } else if(scenario=="guards") {
        CWorld::Players[0].m_pPed=nullptr;Sample();Check(Event("local-player-unbound"),"Missing local actor guarded");
        CWorld::Players[0].m_pPed=&localPed;CPools::pool.valid=false;Next();Check(Event("local-player-unbound"),"Invalid native pool object guarded");
        CPools::pool.valid=true;CPools::reference=-1;Next();Check(Event("local-reference-unbound"),"Negative full reference guarded");
        CPools::reference=100;CPools::mapped=&otherPed;Next();Check(Event("local-reference-unbound"),"Reused pool reference cannot observe another actor");
        CPools::mapped=&localPed;localPed.m_pPlayerData=nullptr;Next();Check(Event("local-reference-unbound"),"Missing local player data guarded");
        Check(localPed.positionCalls==0 && localPed.eligibilityCalls==0,"Reference guards precede native methods");ValidJSON();
    } else if(scenario=="flags") {
        for(unsigned short controls: {0u,0x20u,0x200u,0xFFFFu}) {
            localPad.DisablePlayerControls=controls;Next();const auto& line=Probe::records.back();
            Check(Field(line,"controls")==controls,"WORD control values are not narrowed to a byte");
            Check(Field(line,"safe")==((controls&0x20)!=0),"Safe bit uses mask 0x20");
            Check(Field(line,"debug_ui")==((controls&0x200)!=0),"Debug UI bit uses mask 0x200");
        }
        CWorld::Players[1].m_pPed=&otherPed;Next();Check(Field(Probe::records.back(),"native_coop")==1,"Recorded native0/native1 pair reports co-op");
        CWorld::Players[1].m_pPed=nullptr;Next();Check(Field(Probe::records.back(),"native_coop")==0,"Remote network identity alone is not native slot1");
        nearEntry.m_nFlags.bEnableAccess=false;Next();Check(Field(Probe::records.back(),"access_enabled")==0 && Field(Probe::records.back(),"nearest_flags")==0,"Access bit0x4000 reflected exactly");
        CTheScripts::OnAMissionFlag=200000;Next();Check(Field(Probe::records.back(),"mission")==-1,"Out-of-range mission index guarded");
        localPed.position.x=std::numeric_limits<float>::quiet_NaN();Next();Check(Field(Probe::records.back(),"nearest_flags")==-1,"NaN position cannot select bogus nearest entry");
        CPad::pad=nullptr;Next();Check(Field(Probe::records.back(),"controls")==65535,"Missing pad reports sentinel without dereference");
        CEntryExitManager::mp_poolEntryExits.present=false;Next();Check(Field(Probe::records.back(),"pool")==0,"Missing entry pool is inert");ValidJSON();
    } else if(scenario=="receive") {
        Packets::Scripts::EnExSync packet;packet.bDisabled=true;packet.bBurglary=true;packet.count=1;
        packet.enexes[0].areaId=2;packet.enexes[0].rectLeft=-2;packet.enexes[0].rectBottom=-4;
        packet.enexes[0].flags.bEnableAccess=false;packet.enexes[0].flags.bDisableExit=true;
        const auto before=Snapshot();CEntryExitMarkerSync::Receive(packet);
        Check(Event("received-enex") && Field(Probe::records.back(),"disabled")==1 && Field(Probe::records.back(),"count")==1,"Actual Receive emits incoming snapshot trace");
        Check(CEntryExitManager::ms_bDisabled && CEntryExitManager::ms_bBurglaryHousesEnabled,"Receive retains original host-global assignments");
        Check(!nearEntry.m_nFlags.bEnableAccess && nearEntry.m_nFlags.bDisableExit,"Matching area/rectangle retains full original flag replacement");
        Check(!farEntry.m_nFlags.bDisableExit,"Unmatched native entry is untouched");
        const auto after=Snapshot();Check(before.controls==after.controls && before.cutscene==after.cutscene && before.player0==after.player0 && before.player1==after.player1,"Receive tracing adds no control or actor mutation");
        Check(CEntryExitMarkerSync::ms_lastData.count==1,"Original cached snapshot preserved");
        packet.bDisabled=false;packet.enexes[0].flags.bEnableAccess=true;CEntryExitMarkerSync::Receive(packet);
        Check(!CEntryExitManager::ms_bDisabled && nearEntry.m_nFlags.bEnableAccess,"Later enabled receipt preserves restoration semantics");
        CEntryExitManager::mp_poolEntryExits.clear();CEntryExitMarkerSync::Receive(packet);
        Check(CEntryExitMarkerSync::ms_lastData.count==1,"Receive-before-pool still caches exact snapshot; no invented readiness replay");ValidJSON();
    } else if(scenario=="cap") {
        const std::string data(690,'x');for(int i=0;i<2000;++i) RuntimeDiagnostics::Write("entry","\"event\":\"cap\",\"data\":\"%s\"",data.c_str());
        Check(RuntimeDiagnostics::bytesWritten<=RuntimeDiagnostics::MaxBytes,"Sink cumulative bytes remain within one MiB");
        LARGE_INTEGER size{};Check(::GetFileSizeEx(RuntimeDiagnostics::file,&size) && size.QuadPart==RuntimeDiagnostics::bytesWritten,"Measured file bytes match capped counter including header");
        const DWORD bytes=RuntimeDiagnostics::bytesWritten;const auto writes=Probe::writes;
        RuntimeDiagnostics::Write("entry","\"event\":\"cap\",\"data\":\"%s\"",data.c_str());
        Check(RuntimeDiagnostics::bytesWritten==bytes && Probe::writes==writes,"Record that exceeds remaining cap never reaches WriteFile");ValidJSON();
    } else if(scenario=="reject-fields") {
        RuntimeDiagnostics::Write(nullptr,"\"event\":\"x\"");RuntimeDiagnostics::Write("bad\"scope","\"event\":\"x\"");
        RuntimeDiagnostics::Write(std::string(49,'x').c_str(),"\"event\":\"x\"");RuntimeDiagnostics::Write("entry",nullptr);
        RuntimeDiagnostics::Write("entry","%s",std::string(800,'x').c_str());
        Check(!RuntimeDiagnostics::attempted && Probe::writes==0,"Invalid scope/null/oversized fragments are inert before opening");
        RuntimeDiagnostics::Write("entry","\"event\":\"ok\"");Check(Event("ok"),"Rejected records do not disable later valid sink operation");ValidJSON();
    } else {
        Probe::mode=scenario;Sample();
        Check(RuntimeDiagnostics::attempted,"Failure paths record one attempted open");
        if(scenario=="write-fail") {
            Check(RuntimeDiagnostics::bytesWritten==0 && Probe::records.empty(),"Write failure counts no nonexistent bytes");
            Probe::mode.clear();Next();Check(Event("state"),"A transient write failure can produce later diagnostic state");
            ValidJSON();
        } else if(scenario=="partial-write") {
            Check(RuntimeDiagnostics::bytesWritten>0 && RuntimeDiagnostics::bytesWritten<1024,"Partial writes count only actual bytes");
            LARGE_INTEGER size{};Check(::GetFileSizeEx(RuntimeDiagnostics::file,&size) && size.QuadPart==RuntimeDiagnostics::bytesWritten,"Partial file evidence is accounted, not claimed valid JSON");
        } else {
            Check(RuntimeDiagnostics::file==INVALID_HANDLE_VALUE && Probe::writes==0,"Path/directory/file failures remain inert");
            const auto attempts=Probe::modules;Next();Check(Probe::modules==attempts,"Failed open is not repeatedly retried");
        }
    }
    if(RuntimeDiagnostics::file!=INVALID_HANDLE_VALUE) ::CloseHandle(RuntimeDiagnostics::file);
    std::cout << assertions << " assertions, " << failures << " failures\n";
    return failures ? 1 : 0;
}
