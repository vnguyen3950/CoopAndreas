#pragma once
#include "network/packet.h"
#include "network/pickup_lifecycle.h"
namespace Packets::Pickups {
enum class Operation:uint8_t { Create,Remove,Claim,Grant,Result,Replay };
template<class Stream>bool Actor(Stream&stream,PickupSync::Actor&a) {
    serialize_int(stream,a.generation,1,PickupSync::MaxCounter);serialize_int(stream,a.birth,1,PickupSync::MaxCounter);
    serialize_int(stream,a.sequence,1,PickupSync::MaxCounter);serialize_int(stream,a.model,0,299);serialize_int(stream,a.area,0,255);return a.Valid();
}
template<class Stream>bool Position(Stream&stream,PickupSync::Position&p) {
    serialize_float(stream,p.x);serialize_float(stream,p.y);serialize_float(stream,p.z);return p.Valid();
}
template<class Stream>bool Item(Stream&stream,PickupSync::Item&i) {
    serialize_int(stream,i.id,0,PickupSync::MaxCounter);serialize_int(stream,i.epoch,0,PickupSync::MaxCounter);
    serialize_int(stream,i.revision,0,PickupSync::MaxCounter);serialize_int(stream,i.creation,1,PickupSync::MaxCounter);
    serialize_int(stream,i.owner,0,7);serialize_int(stream,i.model,0,19999);serialize_int(stream,i.type,0,22);serialize_int(stream,i.area,0,255);
    serialize_int(stream,i.ammo,0,100000);serialize_int(stream,i.remaining,0,600000);
    bool cop=i.cop.Present();serialize_bool(stream,cop);
    if(cop){serialize_int(stream,i.cop.ped,0,254);serialize_int(stream,i.cop.death.generation,1,PickupSync::MaxCounter);
        serialize_int(stream,i.cop.death.epoch,1,PickupSync::MaxCounter);serialize_int(stream,i.cop.death.sequence,1,PickupSync::MaxCounter);
        serialize_int(stream,i.cop.sequence,1,PickupSync::MaxCounter);serialize_int(stream,i.cop.producerGeneration,1,PickupSync::MaxCounter);}
    else if(Stream::IsReading)i.cop={};
    return Position(stream,i.position)&&i.ValidMetadata();
}
template<class Stream>bool Row(Stream&stream,PickupSync::Row&r) {
    if(!Item(stream,r.item))return false;
    int stage=int(r.stage),reason=int(r.reason);serialize_int(stream,stage,0,3);serialize_int(stream,reason,0,int(PickupSync::Reason::SceneEnded));
    r.stage=PickupSync::Stage(stage);r.reason=PickupSync::Reason(reason);
    serialize_int(stream,r.grant,0,PickupSync::MaxCounter);serialize_int(stream,r.collector,-1,7);
    if(r.collector>=0&&!Actor(stream,r.collectorLife))return false;return r.Valid();
}
class Hello:public Packet {
    DEFINE_PACKET_TYPE(Hello,ePacketType::PICKUP_HELLO,ePacketChannel::SYSTEM);
public:PickupSync::Actor actor;PickupSync::Position position;bool mission=false;
    bool Valid()const{return actor.Valid()&&position.Valid();}
private:template<class Stream>bool Serialize(Stream&stream) {
    if(Stream::IsWriting&&!Valid())return false;
    if(!Actor(stream,actor)||!Position(stream,position))return false;serialize_bool(stream,mission);return Valid();
} };
class State:public Packet {
    DEFINE_PACKET_TYPE(State,ePacketType::PICKUP_STATE,ePacketChannel::SYSTEM);
public:bool reset=true;uint32_t epoch=0;int host=-1;PickupSync::Actor recipient;PickupSync::Row row;
    bool Valid()const{return epoch&&epoch<=PickupSync::MaxCounter&&host>=-1&&host<8&&recipient.Valid()&&(reset||row.Valid()&&row.item.epoch==epoch);}
private:template<class Stream>bool Serialize(Stream&stream) {
    if(Stream::IsWriting&&!Valid())return false;
    serialize_bool(stream,reset);serialize_int(stream,epoch,1,PickupSync::MaxCounter);serialize_int(stream,host,-1,7);
    if(!Actor(stream,recipient))return false;if(!reset&&!Row(stream,row))return false;return Valid();
} };
class Action:public Packet {
    DEFINE_PACKET_TYPE(Action,ePacketType::PICKUP_ACTION,ePacketChannel::SYSTEM);
public:Operation operation=Operation::Replay;uint32_t epoch=0,sequence=0,id=0,grant=0;
    PickupSync::Actor actor;PickupSync::Position position;PickupSync::Item item;
    PickupSync::Outcome outcome=PickupSync::Outcome::UnknownAfterApply;
    PickupSync::Reason reason=PickupSync::Reason::Cleanup;
    bool mission=false,inVehicle=false;
    bool Valid()const {
        if(int(operation)>int(Operation::Replay)||!epoch||epoch>PickupSync::MaxCounter||!sequence||sequence>PickupSync::MaxCounter||!actor.Valid()||!position.Valid())return false;
        if(operation==Operation::Create)return item.ValidMetadata();
        if(operation==Operation::Replay)return true;
        if(!id||id>PickupSync::MaxCounter)return false;
        if(operation==Operation::Grant||operation==Operation::Result)return grant&&grant<=PickupSync::MaxCounter&&int(outcome)<=2;
        return operation!=Operation::Remove||int(reason)>=1&&int(reason)<=int(PickupSync::Reason::SceneEnded);
    }
private:template<class Stream>bool Serialize(Stream&stream) {
    if(Stream::IsWriting&&!Valid())return false;
    int op=int(operation);serialize_int(stream,op,0,int(Operation::Replay));operation=Operation(op);
    serialize_int(stream,epoch,1,PickupSync::MaxCounter);serialize_int(stream,sequence,1,PickupSync::MaxCounter);
    if(!Actor(stream,actor)||!Position(stream,position))return false;serialize_bool(stream,mission);serialize_bool(stream,inVehicle);
    if(operation==Operation::Create){if(!Item(stream,item))return false;}
    else if(operation!=Operation::Replay) {
        serialize_int(stream,id,1,PickupSync::MaxCounter);
        if(operation==Operation::Grant||operation==Operation::Result){serialize_int(stream,grant,1,PickupSync::MaxCounter);int result=int(outcome);serialize_int(stream,result,0,2);outcome=PickupSync::Outcome(result);}
        if(operation==Operation::Remove){int value=int(reason);serialize_int(stream,value,1,int(PickupSync::Reason::SceneEnded));reason=PickupSync::Reason(value);}
    }
    return Valid();
} };
}
