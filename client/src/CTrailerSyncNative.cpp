#include "stdafx.h"
#include "CTrailerSync.h"
namespace {
bool __fastcall AttachAuto(CVehicle* c,void*,CVehicle* p,bool place){return CTrailerSync::AllowAttach(c,p)&&plugin::CallMethodAndReturn<bool,0x6B4410>(c,p,place);}
bool __fastcall AttachTrailer(CVehicle* c,void*,CVehicle* p,bool place){return CTrailerSync::AllowAttach(c,p)&&plugin::CallMethodAndReturn<bool,0x6CFDF0>(c,p,place);}
bool __fastcall DetachAuto(CVehicle* c,void*){return CTrailerSync::AllowDetach(c)&&plugin::CallMethodAndReturn<bool,0x6A4400>(c);}
bool __fastcall DetachTrailer(CVehicle* c,void*){return CTrailerSync::AllowDetach(c)&&plugin::CallMethodAndReturn<bool,0x6CEFB0>(c);}
}
void CTrailerSync::NativeInit(){
 // SDK dispatches virtual slots 61/62. Verify every original before installing any pointer.
 if(*reinterpret_cast<uintptr_t*>(0x871214)!=0x6B4410 || *reinterpret_cast<uintptr_t*>(0x871218)!=0x6A4400 ||
    *reinterpret_cast<uintptr_t*>(0x871D1C)!=0x6CFDF0 || *reinterpret_cast<uintptr_t*>(0x871D20)!=0x6CEFB0)return;
 patch::SetPointer(0x871214,AttachAuto);patch::SetPointer(0x871218,DetachAuto);
 patch::SetPointer(0x871D1C,AttachTrailer);patch::SetPointer(0x871D20,DetachTrailer);EnableNative();
}
