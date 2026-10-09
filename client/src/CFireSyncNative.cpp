#include "stdafx.h"
#include "CFireSync.h"
#include <CFireManager.h>
#include <cstring>

namespace {
// Exact relocated prefixes are instruction-complete and contain no relative operands.
// Verified on supported A559... executable; mismatched bytes disable the entire hook installation.
struct Entry { uintptr_t address; const char* prefix; size_t length; void* original = nullptr; };
Entry ground{0x539F00,"\x51\x55\x8B\x6C\x24\x10",6};
Entry attached{0x53A050,"\x56\x57\x8B\x7C\x24\x0C",6};
Entry script{0x53A270,"\x83\xEC\x10\x53\x56",5};
Entry extinguish{0x5393F0,"\x56\x8B\xF1\x8A\x06",5};
Entry water{0x5394C0,"\x83\xEC\x4C\x53\x55",5};
Entry damage{0x6333D0,"\x64\xA1\x00\x00\x00\x00",6};
using Ground = CFire* (__thiscall*)(CFireManager*,CVector,float,uint8_t,CEntity*,uint32_t,int8_t,uint8_t);
using Attached = CFire* (__thiscall*)(CFireManager*,CEntity*,CEntity*,float,uint8_t,uint32_t,int8_t);
using Script = int (__thiscall*)(CFireManager*,const CVector&,CEntity*,float,uint8_t,int8_t,int);
using Extinguish = void (__thiscall*)(CFire*);
using Water = bool (__thiscall*)(CFireManager*,CVector,float,float);
using Damage = void (__cdecl*)(CPed*,void*);
void* Trampoline(Entry& e) {
    auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr,e.length+5,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if (!memory) return nullptr;
    std::memcpy(memory,reinterpret_cast<const void*>(e.address),e.length);
    memory[e.length] = 0xE9;
    const int32_t relative = int32_t((e.address+e.length)-(reinterpret_cast<uintptr_t>(memory)+e.length+5));
    std::memcpy(memory+e.length+1,&relative,sizeof relative); FlushInstructionCache(GetCurrentProcess(),memory,e.length+5);
    return memory;
}
CFire* __fastcall GroundHook(CFireManager* manager,void*,CVector pos,float size,uint8_t unused,CEntity* creator,uint32_t time,int8_t generations,uint8_t unused2) {
    if (!CFireSync::NativeStart(creator,nullptr,pos)) return nullptr;
    auto* fire = reinterpret_cast<Ground>(ground.original)(manager,pos,size,unused,creator,time,generations,unused2);
    CFireSync::Created(); return fire;
}
CFire* __fastcall AttachedHook(CFireManager* manager,void*,CEntity* target,CEntity* creator,float size,uint8_t unused,uint32_t time,int8_t generations) {
    if (!target || !CFireSync::NativeStart(creator,target,target->GetPosition())) return nullptr;
    auto* result = reinterpret_cast<Attached>(attached.original)(manager,target,creator,size,unused,time,generations);
    // Native 0x53A050 can return null despite creating the target's fire; scan active pool, never depend on return.
    CFireSync::Created(); return result;
}
int __fastcall ScriptHook(CFireManager* manager,void*,const CVector& pos,CEntity* target,float unused,uint8_t unused2,int8_t generations,int strength) {
    if (!CFireSync::NativeStart(nullptr,target,pos)) return -1;
    const int result = reinterpret_cast<Script>(script.original)(manager,pos,target,unused,unused2,generations,strength);
    CFireSync::Created(); return result;
}
void __fastcall ExtinguishHook(CFire* fire,void*) {
    if (CFireSync::NativeExtinguish(fire)) reinterpret_cast<Extinguish>(extinguish.original)(fire);
}
bool __fastcall WaterHook(CFireManager* manager,void*,CVector pos,float radius,float strength) {
    if (!CFireSync::NativeWater(pos,radius,strength)) return false;
    const bool result = reinterpret_cast<Water>(water.original)(manager,pos,radius,strength); CFireSync::Created(); return result;
}
void __fastcall ProcessHook(CFire* fire,void*) { CFireSync::Tick(fire); }
void __cdecl DamageHook(CPed* ped,void* response) {
    if (CFireSync::AllowPedDamage(ped)) { reinterpret_cast<Damage>(damage.original)(ped,response); return; }
    // CPedDamageResponse is 0xC: two floats and four bools. Suppressed damage must not leave stack garbage.
    if (response) { std::memset(response,0,12); static_cast<uint8_t*>(response)[10] = 1; }
}
}
void CFireSync::NativeInit() {
    Entry* entries[] = {&ground,&attached,&script,&extinguish,&water,&damage};
    for (const auto* e : entries) if (std::memcmp(reinterpret_cast<const void*>(e->address),e->prefix,e->length)) {
        logger::warn("Fire sync disabled: native prefix mismatch at 0x%X",unsigned(e->address)); return;
    }
    const uint8_t processCall[] = {0xE8,0x34,0xF6,0xFF,0xFF}; // 0x53AF37 -> 0x53A570
    if (std::memcmp(reinterpret_cast<const void*>(0x53AF37),processCall,sizeof processCall)) {
        logger::warn("Fire sync disabled: ProcessFire call mismatch"); return;
    }
    for (auto* e : entries) if (!(e->original = Trampoline(*e))) {
        logger::warn("Fire sync disabled: trampoline allocation failed"); return;
    }
    patch::RedirectJump(ground.address,GroundHook); patch::RedirectJump(attached.address,AttachedHook);
    patch::RedirectJump(script.address,ScriptHook); patch::RedirectJump(extinguish.address,ExtinguishHook);
    patch::RedirectJump(water.address,WaterHook); patch::RedirectJump(damage.address,DamageHook);
    patch::RedirectCall(0x53AF37,ProcessHook);
    EnableNative();
}
void CFireSync::OriginalProcess(CFire* fire) { plugin::CallMethod<0x53A570>(fire); }
void CFireSync::OriginalExtinguish(CFire* fire) {
    if (extinguish.original) reinterpret_cast<Extinguish>(extinguish.original)(fire);
    else fire->Extinguish();
}
