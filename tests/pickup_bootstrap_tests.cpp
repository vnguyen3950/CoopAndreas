#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <iostream>
#include <map>
#include <vector>
#include <string>
#define __cdecl __cdecl
struct CVector{float x=0,y=0,z=0;};enum eWeaponType{WEAPON_UNARMED};enum{WEAPSKILL_STD=1};
struct CWeaponInfo{int m_nModelId1=346;static CWeaponInfo*GetWeaponInfo(eWeaponType,int){static CWeaponInfo info;return &info;}};
struct CPlayerPed{};struct CVehicle{};struct CPed{};
struct CPickup{bool Update(CPlayerPed*,CVehicle*,int){return false;}void Remove(){}};
struct CPickups{static int GenerateNewOne(CVector,uint32_t,uint8_t,uint32_t,uint32_t,bool,char*){return -1;}};
struct CPickupSync{static void NativeInit();static void Created(int,bool){}static bool SeparateDeathWeapon(int,uint8_t,uint32_t){return false;}
 static bool Update(CPickup*,CPlayerPed*,CVehicle*,int){return false;}static void Removed(CPickup*){}static bool BeginCopDrops(CPed*){return true;}
 static bool BeginMoneyDrops(CPed*){return true;}static void EndCopDrops(){}static void EnableNative();
 static int Generate(CVector,uint32_t,uint8_t,uint32_t,uint32_t,bool,char*);static void NativeRemove(CPickup*);};
static bool enabled=false,allocationFailure=false;static unsigned patches=0;
void CPickupSync::EnableNative(){enabled=true;}
namespace RuntimeDiagnostics{static std::vector<std::string>lines;void Write(const char*scope,const char*format,...){
 char fields[768]{};va_list args;va_start(args,format);std::vsnprintf(fields,sizeof fields,format,args);va_end(args);
 lines.emplace_back(std::string("{\"scope\":\"")+scope+"\","+fields+"}");}}
struct logger{static void warn(const char*){}};
namespace patch{template<class T>void RedirectJump(uintptr_t,T){++patches;}template<class T>void RedirectCall(uintptr_t,T){++patches;}}
static std::map<uintptr_t,std::array<uint8_t,32>> image;
static const uint8_t*SnapshotAddress(uintptr_t address){return image.at(address).data();}
using DWORD=uint32_t;using HANDLE=void*;
struct MEMORY_BASIC_INFORMATION{void*BaseAddress=nullptr;size_t RegionSize=0;DWORD State=0,Protect=0;};
constexpr DWORD MEM_COMMIT=0x1000,MEM_RESERVE=0x2000,PAGE_EXECUTE_READWRITE=0x40,PAGE_NOACCESS=1,PAGE_GUARD=0x100,PAGE_READONLY=2,PAGE_READWRITE=4,PAGE_WRITECOPY=8,PAGE_EXECUTE_READ=0x20,PAGE_EXECUTE_WRITECOPY=0x80;
static size_t VirtualQuery(const void*pointer,MEMORY_BASIC_INFORMATION*out,size_t){uintptr_t address=reinterpret_cast<uintptr_t>(pointer);if(!image.count(address))return 0;
 out->BaseAddress=const_cast<void*>(pointer);out->RegionSize=32;out->State=MEM_COMMIT;out->Protect=PAGE_EXECUTE_READWRITE;return sizeof(*out);}
static std::vector<void*>allocations;
static void*VirtualAlloc(void*,size_t size,DWORD,DWORD){if(allocationFailure)return nullptr;auto*result=std::malloc(size);allocations.push_back(result);return result;}
static HANDLE GetCurrentProcess(){return nullptr;}static bool FlushInstructionCache(HANDLE,const void*,size_t){return true;}
#include "bootstrap_native.inc"
#include "bootstrap_image.inc"
static unsigned checks=0,failures=0;
static void expect(bool value,const char*reason){++checks;if(!value){++failures;std::cout<<"FAIL: "<<reason<<'\n';}}
static void Reset(){image=RecordedImage();enabled=false;patches=0;allocationFailure=false;RuntimeDiagnostics::lines.clear();}
int main(){
 Reset();CPickupSync::NativeInit();expect(enabled&&patches==7,"Recorded supported image runs actual activation flow and installs all seven hooks");
 expect(RuntimeDiagnostics::lines.size()==9,"Actual activation emits eight guard results and one enabled event");
 for(uintptr_t address:{uintptr_t(0x456F20),uintptr_t(0x4556C0),uintptr_t(0x4555A0),uintptr_t(0x4591D0),uintptr_t(0x4590F0),uintptr_t(0x1564140),uintptr_t(0x45902E),uintptr_t(0x459095)}){
  Reset();image[address][0]^=1;CPickupSync::NativeInit();expect(!enabled&&patches==0,"Any original signature/call mismatch leaves all native hooks untouched");
  expect(!RuntimeDiagnostics::lines.empty()&&RuntimeDiagnostics::lines.back().find("signature")!=std::string::npos,"Disabled signature reason is persisted through recorded common sink");
 }
 Reset();image.erase(0x1564140);CPickupSync::NativeInit();expect(!enabled&&patches==0,"Unreadable relocated target fails closed without dereferencing missing memory");
 bool unreadable=false;for(const auto&line:RuntimeDiagnostics::lines)unreadable|=line.find("unreadable")!=std::string::npos;
 expect(unreadable,"Missing target receives an explicit diagnostic reason");
 Reset();allocationFailure=true;CPickupSync::NativeInit();expect(!enabled&&patches==0,"Allocation failure leaves every native hook untouched");
 expect(RuntimeDiagnostics::lines.back().find("allocation")!=std::string::npos,"Allocation failure is an explicit bounded native diagnostic");
 Reset();CPickupSync::NativeInit();for(const auto&line:RuntimeDiagnostics::lines)std::cout<<"JSON "<<line<<'\n';
 for(void*memory:allocations)std::free(memory);
 std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;
}
