#pragma once
#include "network/packet.h"
#include "network/gang_wars.h"

namespace Packets::Gangs
{
template<class Stream> bool War(Stream& stream, GangWarSync::War& value)
{
    serialize_int(stream,value.offense,0,6);serialize_int(stream,value.defense,0,2);
    serialize_bool(stream,value.enabled);serialize_bool(stream,value.training);
    serialize_bool(stream,value.allowMission);serialize_bool(stream,value.onMission);
    serialize_int(stream,value.zone,-1,GangWarSync::MAX_ZONES-1);serialize_int(stream,value.info,-1,GangWarSync::MAX_ZONES-1);
    serialize_int(stream,value.gang1,-1,GangWarSync::GANGS-1);serialize_int(stream,value.gang2,-1,GangWarSync::GANGS-1);
    serialize_int(stream,value.fightRemaining,0,GangWarSync::MAX_DURATION);serialize_int(stream,value.stageElapsed,0,GangWarSync::MAX_DURATION);
    serialize_float(stream,value.x);serialize_float(stream,value.y);serialize_float(stream,value.z);
    serialize_int(stream,value.specificCount,0,6);
    for(int i=0;i<value.specificCount;++i){serialize_int(stream,value.specificZones[i],0,GangWarSync::MAX_ZONES-1);}
    serialize_int(stream,value.trainingInfo,-1,GangWarSync::MAX_ZONES-1);serialize_int(stream,value.ferocity,0,5);
    serialize_float(stream,value.nextAttack);serialize_float(stream,value.difficulty);serialize_float(stream,value.territoryPercent);
    serialize_bool(stream,value.closeby);
    return value.Valid();
}
class State : public Packet
{
    DEFINE_PACKET_TYPE(State,ePacketType::GANG_WAR_STATE,ePacketChannel::SYSTEM);
public:
    GangWarSync::Kind kind = GangWarSync::Kind::Request;
    GangWarSync::Authority authority;
    uint32_t sequence = 0;
    bool Valid() const
    {
        if (unsigned(kind)>2 || sequence>GangWarSync::MAX_COUNTER) return false;
        if (kind==GangWarSync::Kind::Request) return sequence==0;
        if (kind==GangWarSync::Kind::Snapshot) return sequence==0 && authority.Valid();
        return sequence && authority.epoch && authority.epoch<=GangWarSync::MAX_COUNTER
            && authority.campaign && authority.campaign<=GangWarSync::MAX_COUNTER && authority.war.Valid();
    }
private:
    template<class Stream> bool Serialize(Stream& stream)
    {
        if (Stream::IsWriting && !Valid()) return false;
        int mode=int(kind);serialize_int(stream,mode,0,2);kind=GangWarSync::Kind(mode);
        if (kind==GangWarSync::Kind::Request) return true;
        serialize_int(stream,authority.epoch,1,GangWarSync::MAX_COUNTER);
        serialize_int(stream,authority.campaign,0,GangWarSync::MAX_COUNTER);
        if (kind==GangWarSync::Kind::Snapshot)
        {
            serialize_int(stream,authority.revision,1,GangWarSync::MAX_COUNTER);
            serialize_int(stream,authority.warGeneration,0,GangWarSync::MAX_COUNTER);
            serialize_int(stream,authority.host,-1,GangWarSync::MAX_PLAYERS-1);serialize_bool(stream,authority.ready);
        }
        else { serialize_int(stream,sequence,1,GangWarSync::MAX_COUNTER); }
        if (!War(stream,authority.war)) return false;
        return Valid();
    }
};
class Territory : public Packet
{
    DEFINE_PACKET_TYPE(Territory,ePacketType::GANG_TERRITORY,ePacketChannel::SYSTEM);
public:
    GangWarSync::Kind kind = GangWarSync::Kind::Request;
    uint32_t epoch=0,campaign=0,revision=0,sequence=0;
    bool reset=false;
    GangWarSync::World world;
    bool Valid() const
    {
        if (unsigned(kind)>2 || epoch>GangWarSync::MAX_COUNTER || campaign>GangWarSync::MAX_COUNTER
            || revision>GangWarSync::MAX_COUNTER || sequence>GangWarSync::MAX_COUNTER) return false;
        if (kind==GangWarSync::Kind::Request) return sequence==0;
        return epoch && world.Valid() && (kind==GangWarSync::Kind::Snapshot ? campaign && revision && sequence==0 : sequence!=0);
    }
private:
    template<class Stream> bool Serialize(Stream& stream)
    {
        if(Stream::IsWriting && !Valid())return false;
        int mode=int(kind);serialize_int(stream,mode,0,2);kind=GangWarSync::Kind(mode);
        if(kind==GangWarSync::Kind::Request)return true;
        serialize_int(stream,epoch,1,GangWarSync::MAX_COUNTER);serialize_int(stream,campaign,0,GangWarSync::MAX_COUNTER);
        if(kind==GangWarSync::Kind::Snapshot){serialize_int(stream,revision,1,GangWarSync::MAX_COUNTER);}
        else{serialize_int(stream,sequence,1,GangWarSync::MAX_COUNTER);serialize_bool(stream,reset);}
        serialize_int(stream,world.layout,1,GangWarSync::MAX_COUNTER);serialize_int(stream,world.count,1,GangWarSync::MAX_ZONES);
        for(int i=0;i<world.count;++i)
        {
            for(auto& density:world.zones[i].density){serialize_int(stream,density,0,255);}
            for(auto& color:world.zones[i].color){serialize_int(stream,color,0,255);}
            serialize_int(stream,world.zones[i].radar,0,3);
        }
        return Valid();
    }
};
}
