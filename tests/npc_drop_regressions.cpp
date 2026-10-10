#define main OriginalPickupNativeSuite
#include "tests.cpp"
#undef main
static bool __cdecl RecordedMerge(CVector p,eWeaponType w,uint8_t t,uint32_t a,bool e){return CPickups::TryToMerge_WeaponType(p,w,ePickupType(t),a,e);}
int main(){
#ifdef PICKUP_MERGE_HOOK
    mergeOriginal=reinterpret_cast<void*>(&RecordedMerge);
#endif
    CPed cop;cop.m_nPedType=PED_TYPE_COP;cop.m_nModelIndex=280;
    Setup(346,4,10);hasGrant=false;view.rows={};mappings={};capturedDeaths={};capturedKinds={};copContext={};
    CPools::pedRefs={{cop.poolRef,&cop}};deathIdentityAvailable=true;generated=0;dropIndex=2;
    auto method=&CPed::RecordedWeaponDrops;std::memcpy(&weaponDropsOriginal,&method,sizeof method);
    generateOriginal=reinterpret_cast<void*>(&RecordedGenerate);
    recordedWeaponDrops=[](CPed*){
#ifdef PICKUP_MERGE_HOOK
        const bool merged=MergeHook({},WEAPON_PISTOL,4,5,false);
#else
        const bool merged=CPickups::TryToMerge_WeaponType({},WEAPON_PISTOL,PICKUP_ONCE_TIMEOUT,5,false);
#endif
        if(!merged)GenerateHook({},346,4,5,0,false,nullptr);
    };
    WeaponDropsHook(&cop,nullptr);
    expect(CopCreates()==1,"Native same-handle merge boundary must preserve one publishable sealed death output");
    expect(CPickups::aPickUps[0].m_nAmmo==10,"Shared death cannot mutate an existing potentially reserved pile");
    expect(generated==1,"Original native generation path exposes the engine supplied amount");
    #ifdef PICKUP_MERGE_HOOK
    CPickupSync::EndCopDrops();CPickups::aPickUps[0].m_nAmmo=10;
    expect(MergeHook({},WEAPON_PISTOL,4,5,false)&&CPickups::aPickUps[0].m_nAmmo==15,"Ordinary merge outside sealed death remains native");
    copContext.active=true;copContext.origin={3,{9,2,6},1,10};copContext.model=280;copContext.area=0;copContext.type=PED_TYPE_COP;copContext.createdBy=1;
    CPickups::aPickUps[0].m_nAmmo=10;CTheScripts::ScriptSpace[1]=1;
    expect(MergeHook({},WEAPON_PISTOL,4,5,false)&&CPickups::aPickUps[0].m_nAmmo==15,"Mission/out-of-slice merge remains native");
    CTheScripts::ScriptSpace[1]=0;CPickups::aPickUps[0].m_nAmmo=10;
    expect(MergeHook({},WEAPON_PISTOL,4,16,false)&&CPickups::aPickUps[0].m_nAmmo==26,"Unsupported native quantity is not reconstructed or suppressed");
#endif
    std::cout<<checks<<" assertions, "<<failures<<" failures\n";return failures?1:0;
}
