#include "stdafx.h"
#include "CPickupSync.h"
#include <CPickups.h>
#include <cstring>
namespace {
void*generateOriginal=nullptr;
void*removeOriginal=nullptr;
void*weaponDropsOriginal=nullptr;
void*mergeOriginal=nullptr;
void*moneyDropsOriginal=nullptr;
using GenerateFn=int(__cdecl*)(CVector,uint32_t,uint8_t,uint32_t,uint32_t,bool,char*);
using RemoveFn=void(__thiscall*)(CPickup*);
using MergeFn=bool(__cdecl*)(CVector,eWeaponType,uint8_t,uint32_t,bool);
using WeaponDropsFn=void(__thiscall*)(CPed*);
void*Trampoline(uintptr_t address,size_t length){
    auto*memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,length+5,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!memory)return nullptr;std::memcpy(memory,reinterpret_cast<const void*>(address),length);memory[length]=0xE9;
    const int32_t displacement=int32_t(address+length-(reinterpret_cast<uintptr_t>(memory)+length+5));
    std::memcpy(memory+length+1,&displacement,4);FlushInstructionCache(GetCurrentProcess(),memory,length+5);return memory;
}
int __cdecl GenerateHook(CVector position,uint32_t model,uint8_t type,uint32_t ammo,uint32_t money,bool empty,char*message){
    const int handle=reinterpret_cast<GenerateFn>(generateOriginal)(position,model,type,ammo,money,empty,message);
    CPickupSync::Created(handle,true);return handle;
}
bool __cdecl MergeHook(CVector position,eWeaponType weapon,uint8_t type,uint32_t ammo,bool empty){
    const auto*info=CWeaponInfo::GetWeaponInfo(weapon,WEAPSKILL_STD);
    // Keep issued grants and prior native piles immutable. Returning false here
    // takes the original death caller's Generate branch with its native amount.
    if(info&&CPickupSync::SeparateDeathWeapon(info->m_nModelId1,type,ammo))return false;
    return reinterpret_cast<MergeFn>(mergeOriginal)(position,weapon,type,ammo,empty);
}
bool __fastcall UpdateHook(CPickup*pickup,void*,CPlayerPed*player,CVehicle*vehicle,int playerId){return CPickupSync::Update(pickup,player,vehicle,playerId);}
void __fastcall RemoveHook(CPickup*pickup,void*){CPickupSync::Removed(pickup);reinterpret_cast<RemoveFn>(removeOriginal)(pickup);}
void __fastcall WeaponDropsHook(CPed*ped,void*){
    if(CPickupSync::BeginCopDrops(ped))reinterpret_cast<WeaponDropsFn>(weaponDropsOriginal)(ped);
    CPickupSync::EndCopDrops();
}
void __fastcall MoneyDropsHook(CPed*ped,void*){
    if(CPickupSync::BeginMoneyDrops(ped))reinterpret_cast<WeaponDropsFn>(moneyDropsOriginal)(ped);
    CPickupSync::EndCopDrops();
}
bool Calls(uintptr_t source,uintptr_t target){const auto*p=reinterpret_cast<const uint8_t*>(source);int32_t relative=0;std::memcpy(&relative,p+1,4);return p[0]==0xE8&&source+5+relative==target;}
}
void CPickupSync::NativeInit(){
    // Disk-verified supported executable A559AA... . Prefixes end at complete
    // instructions before relative transfers; the native Update entry stays callable.
    const uint8_t money[]={0xE9,0x4B,0xB0,0x10,0x01};
    const uint8_t moneyTarget[]={0x56,0x8B,0xF1};
    const uint8_t generate[]={0x8A,0x4C,0x24,0x14,0x80,0xF9,0x0D};
    const uint8_t remove[]={0x56,0x8B,0xF1,0x8B,0xC6};
    const uint8_t merge[]={0x8B,0x44,0x24,0x10,0x83,0xEC,0x1C};
    const uint8_t weapons[]={0x83,0xEC,0x14,0x53,0x55,0x56,0x57,0x8B,0xF9};
    if(std::memcmp(reinterpret_cast<const void*>(0x456F20),generate,sizeof generate)
        ||std::memcmp(reinterpret_cast<const void*>(0x4556C0),remove,sizeof remove)
        ||std::memcmp(reinterpret_cast<const void*>(0x4555A0),merge,sizeof merge)
        ||std::memcmp(reinterpret_cast<const void*>(0x4591D0),weapons,sizeof weapons)
        ||std::memcmp(reinterpret_cast<const void*>(0x4590F0),money,sizeof money)
        ||std::memcmp(reinterpret_cast<const void*>(0x1564140),moneyTarget,sizeof moneyTarget)
        ||!Calls(0x45902E,0x457410)||!Calls(0x459095,0x457410)){
        logger::warn("Ordinary pickup sharing disabled: native prefix/call mismatch");return;
    }
    moneyDropsOriginal=reinterpret_cast<void*>(0x1564140); // Follow verified relative JMP; never trampoline-copy it.
    generateOriginal=Trampoline(0x456F20,sizeof generate);removeOriginal=Trampoline(0x4556C0,sizeof remove);
    weaponDropsOriginal=Trampoline(0x4591D0,sizeof weapons);mergeOriginal=Trampoline(0x4555A0,sizeof merge);
    if(!generateOriginal||!removeOriginal||!weaponDropsOriginal||!mergeOriginal){logger::warn("Ordinary pickup sharing disabled: trampoline allocation failed");return;}
    patch::RedirectJump(0x456F20,GenerateHook);patch::RedirectJump(0x4556C0,RemoveHook);
    patch::RedirectCall(0x45902E,UpdateHook);patch::RedirectCall(0x459095,UpdateHook);EnableNative();
    patch::RedirectJump(0x4590F0,MoneyDropsHook);patch::RedirectJump(0x4591D0,WeaponDropsHook);patch::RedirectJump(0x4555A0,MergeHook);
}
int CPickupSync::Generate(CVector position,uint32_t model,uint8_t type,uint32_t ammo,uint32_t money,bool empty,char*message){
    return generateOriginal?reinterpret_cast<GenerateFn>(generateOriginal)(position,model,type,ammo,money,empty,message)
        :CPickups::GenerateNewOne(position,model,type,ammo,money,empty,message);
}
void CPickupSync::NativeRemove(CPickup*pickup){if(removeOriginal)reinterpret_cast<RemoveFn>(removeOriginal)(pickup);else pickup->Remove();}
