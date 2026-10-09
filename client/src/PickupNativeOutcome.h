#pragma once
#include "network/pickup_lifecycle.h"
#include <CPickups.h>
#include <CWeaponInfo.h>
#include <CDarkel.h>

namespace PickupNative {
// Native CPed initialization/RequestDelayedWeapon use 55 (0x37). The bundled
// SDK omits that enum name; keep the verified sentinel local, leaving SDK intact.
constexpr int DelayedWeaponEmpty=55;
struct Weapon {int type=0;uint32_t total=0,clip=0;bool operator==(const Weapon&b)const{return type==b.type&&total==b.total&&clip==b.clip;}};
struct Resources {
    float health=0,armour=0;int money=0,delayed=-1;uint32_t delayedAmmo=0;
    std::array<Weapon,13> weapons{};
    bool operator==(const Resources&b)const{return health==b.health&&armour==b.armour&&money==b.money&&delayed==b.delayed&&delayedAmmo==b.delayedAmmo&&weapons==b.weapons;}
};
inline Resources Capture(CPlayerPed* ped){
    Resources value;value.health=ped->m_fHealth;value.armour=ped->m_fArmour;value.money=CWorld::Players[0].m_nMoney;
    value.delayed=ped->m_nDelayedWeapon;value.delayedAmmo=ped->m_nDelayedWeaponAmmo;
    for(int i=0;i<13;++i){const auto&w=ped->m_aWeapons[i];value.weapons[i]={int(w.m_eWeaponType),w.m_nTotalAmmo,w.m_nAmmoInClip};}
    return value;
}
inline bool MetadataMatches(CPickup*pickup,const PickupSync::Item&item){
    if(!pickup||pickup->m_nModelIndex!=item.model||pickup->m_nPickupType!=item.type||pickup->m_nAmmo!=item.ammo)return false;
    const auto position=pickup->GetPosn();return position.x==item.position.x&&position.y==item.position.y&&position.z==item.position.z;
}
inline bool Matches(CPickup*pickup,const PickupSync::Item&item){
    if(!MetadataMatches(pickup,item)||!pickup->m_pObject||!CPools::ms_pObjectPool||!CPools::ms_pObjectPool->IsObjectValid(pickup->m_pObject))return false;
    const int reference=CPools::GetObjectRef(pickup->m_pObject);
    if(reference<0||CPools::ms_pObjectPool->GetAtRef(reference)!=pickup->m_pObject||pickup->m_pObject->m_nModelIndex!=item.model||pickup->m_pObject->m_nAreaCode!=item.area
        // SDK b01 is bit zero: native CObject::bIsPickup (Object.h, 0x1).
        ||!pickup->m_pObject->m_nObjectFlags.b01)return false;
    return true;
}
inline bool ModelsReady(CPickup*pickup){
    if(!pickup||!PickupSync::WeaponModel(pickup->m_nModelIndex))return true;
    auto*info=CWeaponInfo::GetWeaponInfo(eWeaponType(CPickups::WeaponForModel(pickup->m_nModelIndex)),WEAPSKILL_STD);if(!info)return false;
    const int models[]={info->m_nModelId1,info->m_nModelId2};
    for(int model:models)if(model>=0&&(model>19999||!CModelInfo::ms_modelInfoPtrs[model]||CStreaming::ms_aInfoForModel[model].m_nLoadState!=LOADSTATE_LOADED))return false;
    return true;
}
inline bool Eligible(CPickup* pickup,CPlayerPed* ped,bool mission,int area){
    if(!pickup||!ped||mission||area!=0||!ped->IsAlive()||ped->m_nPedFlags.bInVehicle||!ped->m_pPlayerData
        ||pickup->m_nFlags.bDisabled||!pickup->m_pObject||pickup->m_pObject->m_nAreaCode!=0||pickup->m_pObject->m_nObjectFlags.bDoNotRender)return false;
    const auto pos=pickup->GetPosn();const auto player=ped->GetPosition();
    if(!PickupSync::Touching({pos.x,pos.y,pos.z},{player.x,player.y,player.z}))return false;
    if(pickup->m_nModelIndex==1240)return std::isfinite(ped->m_fHealth)&&float(CWorld::Players[0].m_nMaxHealth)-.2f>=ped->m_fHealth;
    if(pickup->m_nModelIndex==1242)return std::isfinite(ped->m_fArmour)&&float(CWorld::Players[0].m_nMaxArmour)-.2f>=ped->m_fArmour;
    if(pickup->m_nPickupType==8)return pickup->m_nModelIndex==1212&&pickup->m_nAmmo>0;
    if(!PickupSync::WeaponModel(pickup->m_nModelIndex)||!pickup->m_nAmmo||pickup->m_nAmmo>100000
        ||CDarkel::FrenzyOnGoing()||int(ped->m_nDelayedWeapon)!=DelayedWeaponEmpty)return false;
    const auto type=eWeaponType(CPickups::WeaponForModel(pickup->m_nModelIndex));
    if(type==WEAPON_UNARMED||type>WEAPON_PARACHUTE)return false;
    auto*info=CWeaponInfo::GetWeaponInfo(type,WEAPSKILL_STD);if(!info||info->m_nSlot>=13)return false;
    const auto&weapon=ped->m_aWeapons[info->m_nSlot];
    // Native Update can extract ammo before returning false for replacement.
    // This slice admits only identical or genuinely empty slots, never swaps.
    if(weapon.m_eWeaponType!=type&&weapon.m_eWeaponType!=WEAPON_UNARMED)return false;
    if(!ped->DoesPlayerWantNewWeapon(type,false))return false;
    if(weapon.m_eWeaponType!=type&&(CTimer::m_snTimeInMilliseconds-ped->m_pPlayerData->m_nLastHSMissileLOSTime<1500
        ||!CPickups::PlayerCanPickUpThisWeaponTypeAtThisMoment(type)))return false;
    return ModelsReady(pickup);
}
inline PickupSync::Outcome Outcome(bool nativeCollected,const PickupSync::Item&item,const Resources&before,const Resources&after,bool nativeRetired){
    if(!nativeCollected)return PickupSync::Outcome::UnknownAfterApply;
    bool benefit=false;
    if(item.model==1240)benefit=after.health>before.health;
    else if(item.model==1242)benefit=after.armour>before.armour;
    else if(item.type==8)benefit=int64_t(after.money)-int64_t(before.money)==item.ammo;
    else if(PickupSync::WeaponModel(item.model)){
        const auto type=CPickups::WeaponForModel(item.model);
        for(size_t i=0;i<before.weapons.size();++i)if(after.weapons[i].type==type&&after.weapons[i].total>before.weapons[i].total)benefit=true;
    }
    // An issued native call is never a pre-apply decline. Pending delayed
    // inventory or any contradictory native/resource result is quarantined.
    return benefit&&nativeRetired?PickupSync::Outcome::Consumed:PickupSync::Outcome::UnknownAfterApply;
}
}
