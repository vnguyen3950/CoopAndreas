#pragma once
#include "network/gang_wars.h"
#include <CZoneInfo.h>
#include <CGangWars.h>
#include <CRadar.h>
#include <algorithm>
#include <cstring>

// SDK CZone and several CTheZones globals are misdeclared. Use the verified
// native view locally; do not change SDK, discovery bits or population settings.
namespace GangWarsNative
{
struct Zone
{
    char label[8],text[8];
    int16_t x1,y1,z1,x2,y2,z2,info;
    uint8_t type,level;
};
static_assert(sizeof(Zone)==0x20,"Native navigation-zone layout changed");
static_assert(offsetof(Zone,info)==0x1C,"Native zone-info index offset changed");
static_assert(sizeof(CZoneInfo)==0x11,"Native zone-info layout changed");
struct View
{
    Zone* zones=nullptr;
    CZoneInfo* infos=nullptr;
    int navigationCount=0,infoCount=0;
    bool Valid()const{return zones&&infos&&navigationCount>0&&navigationCount<=GangWarSync::MAX_ZONES
        &&infoCount>0&&infoCount<=GangWarSync::MAX_ZONES;}
};
inline View Live()
{
    return {reinterpret_cast<Zone*>(0xBA3798),reinterpret_cast<CZoneInfo*>(0xBA1DF0),
        *reinterpret_cast<int16_t*>(0xBA3794),*reinterpret_cast<int16_t*>(0xBA1DE8)};
}
inline void ClearNativeBlip()
{
    // SDK declares this integer handle as CRadar*. Keep that declaration unused.
    auto& handle=*reinterpret_cast<int*>(0x96AB98);
    if(handle!=0&&handle!=-1)CRadar::ClearBlip(handle);
    handle=0;
}
inline uint32_t Layout(const View& view)
{
    if(!view.Valid())return 0;
    uint32_t hash=2166136261u;
    auto byte=[&](uint8_t value){hash=(hash^value)*16777619u;};
    byte(uint8_t(view.navigationCount));byte(uint8_t(view.navigationCount>>8));
    byte(uint8_t(view.infoCount));byte(uint8_t(view.infoCount>>8));
    for(int i=0;i<view.navigationCount;++i)
    {
        if(view.zones[i].info<0||view.zones[i].info>=view.infoCount)return 0;
        const auto* data=reinterpret_cast<const uint8_t*>(&view.zones[i]);
        for(size_t j=0;j<sizeof(Zone);++j)byte(data[j]);
    }
    hash&=GangWarSync::MAX_COUNTER;return hash?hash:1;
}
inline bool CaptureWorld(const View& view,GangWarSync::World& out)
{
    const auto layout=Layout(view);if(!layout)return false;
    GangWarSync::World value;value.layout=layout;value.count=uint16_t(view.infoCount);
    for(int i=0;i<view.infoCount;++i)
    {
        for(int gang=0;gang<GangWarSync::GANGS;++gang)value.zones[i].density[gang]=uint8_t(view.infos[i].m_nGangDensity[gang]);
        const auto& color=view.infos[i].m_ZoneColor;value.zones[i].color={color.r,color.g,color.b,color.a};
        value.zones[i].radar=(uint8_t(view.infos[i].m_nFlags)>>5)&3;
    }
    if(!value.Valid())return false;out=value;return true;
}
inline bool ApplyWorld(const View& view,const GangWarSync::World& world)
{
    if(!world.Valid()||world.count!=view.infoCount||world.layout!=Layout(view))return false;
    for(int i=0;i<view.infoCount;++i)
    {
        for(int gang=0;gang<GangWarSync::GANGS;++gang)view.infos[i].m_nGangDensity[gang]=char(world.zones[i].density[gang]);
        auto& color=view.infos[i].m_ZoneColor;const auto& incoming=world.zones[i].color;
        color.r=incoming[0];color.g=incoming[1];color.b=incoming[2];color.a=incoming[3];
        view.infos[i].m_nFlags=char((uint8_t(view.infos[i].m_nFlags)&uint8_t(~0x60))|(world.zones[i].radar<<5));
    }
    return true;
}
template<class T>inline int Index(T* pointer,T* base,int count)
{
    if(!pointer||!base)return -1;
    const uintptr_t value=reinterpret_cast<uintptr_t>(pointer),start=reinterpret_cast<uintptr_t>(base);
    if(value<start||value>=start+sizeof(T)*size_t(count)||(value-start)%sizeof(T))return -1;
    return int((value-start)/sizeof(T));
}
inline bool CaptureWar(const View& view,uint32_t now,GangWarSync::War& out)
{
    GangWarSync::War value;
    value.enabled=CGangWars::bGangWarsActive;value.training=CGangWars::bTrainingMission;
    value.allowMission=CGangWars::bCanTriggerGangWarWhenOnAMission;value.onMission=CGangWars::bIsPlayerOnAMission;
    if(CGangWars::NumSpecificZones<0||CGangWars::NumSpecificZones>6)return false;
    value.specificCount=uint8_t(CGangWars::NumSpecificZones);
    for(int i=0;i<value.specificCount;++i)
    {
        if(CGangWars::aSpecificZones[i]<0||CGangWars::aSpecificZones[i]>=view.navigationCount)return false;
        value.specificZones[i]=CGangWars::aSpecificZones[i];
    }
    value.trainingInfo=CGangWars::ZoneInfoForTraining;
    if(value.trainingInfo>=view.infoCount)return false;
    value.ferocity=CGangWars::WarFerocity;value.difficulty=CGangWars::Difficulty;
    value.territoryPercent=CGangWars::TerritoryUnderControlPercentage;value.closeby=CGangWars::bPlayerIsCloseby;
    if(!std::isfinite(CGangWars::TimeTillNextAttack))return false;
    value.nextAttack=std::clamp(CGangWars::TimeTillNextAttack,0.0f,float(GangWarSync::MAX_DURATION));
    if(value.enabled)
    {
        if(int(CGangWars::State)<0||int(CGangWars::State)>6||int(CGangWars::State2)<0||int(CGangWars::State2)>2)return false;
        value.offense=uint8_t(CGangWars::State);value.defense=uint8_t(CGangWars::State2);
        if(value.Active())
        {
            value.zone=Index(reinterpret_cast<Zone*>(CGangWars::pZoneToFightOver),view.zones,view.navigationCount);
            value.info=Index(CGangWars::pZoneInfoToFightOver,view.infos,view.infoCount);
            value.gang1=CGangWars::Gang1;value.gang2=CGangWars::Gang2;
            value.fightRemaining=uint32_t(std::clamp(CGangWars::FightTimer,0,int(GangWarSync::MAX_DURATION)));
            value.stageElapsed=std::min(now-CGangWars::TimeStarted,GangWarSync::MAX_DURATION);
            auto point=value.defense?CGangWars::PointOfAttack:CGangWars::CoorsOfPlayerAtStartOfWar;
            value.x=point.x;value.y=point.y;value.z=point.z;
        }
    }
    if(!value.Valid())return false;out=value;return true;
}
inline bool ApplyWar(const View& view,const GangWarSync::War& value,uint32_t now)
{
    if(!view.Valid()||!value.Valid()||(value.Active()&&(value.zone>=view.navigationCount||value.info>=view.infoCount)))return false;
    if(value.trainingInfo>=view.infoCount)return false;
    for(int i=0;i<value.specificCount;++i)if(value.specificZones[i]>=view.navigationCount)return false;
    CGangWars::bGangWarsActive=value.enabled;CGangWars::bTrainingMission=value.training;
    CGangWars::bCanTriggerGangWarWhenOnAMission=value.allowMission;CGangWars::bIsPlayerOnAMission=value.onMission;
    CGangWars::NumSpecificZones=value.specificCount;
    for(int i=0;i<6;++i)CGangWars::aSpecificZones[i]=value.specificZones[i];
    CGangWars::ZoneInfoForTraining=value.trainingInfo;CGangWars::WarFerocity=value.ferocity;
    CGangWars::Difficulty=value.difficulty;CGangWars::TerritoryUnderControlPercentage=value.territoryPercent;
    CGangWars::TimeTillNextAttack=value.nextAttack;CGangWars::bPlayerIsCloseby=value.closeby;
    CGangWars::State=eGangWarState(value.offense);CGangWars::State2=eGangAttackState(value.defense);
    CGangWars::pZoneToFightOver=value.Active()?reinterpret_cast<CZone*>(&view.zones[value.zone]):nullptr;
    CGangWars::pZoneInfoToFightOver=value.Active()?&view.infos[value.info]:nullptr;
    CGangWars::Gang1=value.gang1;CGangWars::Gang2=value.gang2;
    CGangWars::FightTimer=int(value.fightRemaining);CGangWars::TimeStarted=now-value.stageElapsed;
    CGangWars::Provocation=0; // Mirrored guests skip Update; host native kill attribution is preserved.
    CGangWars::PointOfAttack=CVector(value.x,value.y,value.z);
    CGangWars::CoorsOfPlayerAtStartOfWar=CGangWars::PointOfAttack;
    return true;
}
}
