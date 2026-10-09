#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace PickupSync {
constexpr uint32_t MaxCounter=0x7fffffff;
constexpr int MaxPlayers=8,MaxPickups=620;
enum class Stage:uint8_t { Active,Reserved,Collected,Removed };
enum class Outcome:uint8_t { Consumed,DeclinedBeforeApply,UnknownAfterApply };
enum class Reason:uint8_t { None,Cleanup,Expired,OwnerLeft,CollectorLeft,Ambiguous,SceneEnded };
struct Position {
    float x=0,y=0,z=0;
    bool Valid()const{return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z)&&std::abs(x)<=4000&&std::abs(y)<=4000&&z>=-200&&z<=2000;}
};
inline bool Touching(const Position&a,const Position&b){return a.Valid()&&b.Valid()&&(a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)<1.8f&&std::abs(a.z-b.z)<2;}
struct Actor {
    uint32_t generation=0,birth=0,sequence=0;
    int model=0,area=0;
    bool Valid()const{return generation&&generation<=MaxCounter&&birth&&birth<=MaxCounter&&sequence&&sequence<=MaxCounter&&model>=0&&model<=299&&area>=0&&area<=255;}
};
inline bool SameLife(const Actor&a,const Actor&b){return a.Valid()&&b.Valid()&&a.generation==b.generation&&a.birth==b.birth&&a.model==b.model&&a.area==b.area;}
inline bool CurrentActor(const Actor&request,const Actor&current){return SameLife(request,current)&&request.sequence<=current.sequence;}
inline bool WeaponModel(int model){return (model>=346&&model<=353)||(model>=355&&model<=358);}
struct Item {
    uint32_t id=0,epoch=0,revision=0,creation=0;
    uint32_t ammo=0,remaining=0;
    int owner=-1,model=0,type=0,area=0;
    Position position;
    bool ValidMetadata()const {
        if(owner<0||owner>=MaxPlayers||area!=0||!position.Valid()||!creation||creation>MaxCounter||ammo>100000||remaining>600000)return false;
        if(type==8)return model==1212&&ammo>0;
        return (type==3||type==4||type==5)&&(model==1240||model==1242||(WeaponModel(model)&&ammo>0));
    }
    bool Valid()const{return id&&id<=MaxCounter&&epoch&&epoch<=MaxCounter&&revision&&revision<=MaxCounter&&ValidMetadata();}
};
struct Row {
    Item item;
    Stage stage=Stage::Removed;
    Reason reason=Reason::None;
    uint32_t grant=0;
    int collector=-1;
    Actor collectorLife;
    bool Valid()const {
        if(!item.Valid()||int(stage)>3||int(reason)>int(Reason::SceneEnded)||grant>MaxCounter||collector<-1||collector>=MaxPlayers)return false;
        return stage!=Stage::Reserved&&stage!=Stage::Collected||(grant&&collector>=0&&collectorLife.Valid()&&collectorLife.area==0);
    }
};
class Room {
public:
    uint32_t epoch=0;
    int host=-1;
    Actor ownerLife;
    std::array<Row,MaxPickups> rows{};
    bool ChangeHost(int id,const Actor&life) {
        if(id<-1||id>=MaxPlayers||(id>=0&&!life.Valid())||epoch==MaxCounter)return false;
        if(id==host&&SameLife(life,ownerLife)&&epoch)return true;
        ++epoch;host=id;ownerLife=life;rows={};creationHighWater=0;return true;
    }
    Row* Find(uint32_t id){for(auto&row:rows)if(row.item.id==id&&id)return &row;return nullptr;}
    const Row* Find(uint32_t id)const{for(const auto&row:rows)if(row.item.id==id&&id)return &row;return nullptr;}
    Row* Create(int sender,uint32_t expectedEpoch,const Actor&life,Item item) {
        if(sender!=host||!CurrentActor(life,ownerLife)||expectedEpoch!=epoch||!item.ValidMetadata()||item.owner!=sender||item.creation<=creationHighWater||nextId==MaxCounter)return nullptr;
        Row* target=nullptr;for(auto&row:rows)if(!row.item.id||row.stage==Stage::Collected||row.stage==Stage::Removed){target=&row;break;}
        if(!target)return nullptr;
        item.id=++nextId;item.epoch=epoch;item.revision=1;*target={};target->item=item;target->stage=Stage::Active;
        creationHighWater=item.creation;return target;
    }
    bool Remove(int sender,uint32_t expectedEpoch,uint32_t id,Reason reason) {
        auto* row=Find(id);if(sender!=host||expectedEpoch!=epoch||!row||row->item.epoch!=epoch||row->item.revision==MaxCounter||int(reason)<1||int(reason)>int(Reason::SceneEnded))return false;
        if(row->stage==Stage::Collected||row->stage==Stage::Removed)return false;
        row->stage=Stage::Removed;row->reason=reason;++row->item.revision;return true;
    }
    Row* Reserve(int sender,uint32_t expectedEpoch,uint32_t id,const Actor&life,const Position&position,bool mission,bool inVehicle) {
        if(sender<0||sender>=MaxPlayers||!life.Valid()||life.area!=0||mission||inVehicle||expectedEpoch!=epoch||nextGrant==MaxCounter)return nullptr;
        for(const auto&row:rows)if(row.stage==Stage::Reserved&&row.collector==sender)return nullptr;
        auto*row=Find(id);if(!row||row->stage!=Stage::Active||row->item.epoch!=epoch||row->item.revision==MaxCounter||!Touching(position,row->item.position))return nullptr;
        row->grant=++nextGrant;row->collector=sender;row->collectorLife=life;row->stage=Stage::Reserved;++row->item.revision;return row;
    }
    bool Complete(int sender,uint32_t expectedEpoch,uint32_t id,uint32_t grant,const Actor&life,Outcome outcome) {
        auto*row=Find(id);if(expectedEpoch!=epoch||!row||row->stage!=Stage::Reserved||row->collector!=sender||row->grant!=grant||!SameLife(life,row->collectorLife)||row->item.revision==MaxCounter||int(outcome)>2)return false;
        if(outcome==Outcome::Consumed){row->stage=Stage::Collected;}
        else if(outcome==Outcome::DeclinedBeforeApply){row->stage=Stage::Active;row->collector=-1;row->collectorLife={};}
        else{row->stage=Stage::Removed;row->reason=Reason::Ambiguous;}
        ++row->item.revision;return true;
    }
    void RetireCollector(int id) {
        for(auto&row:rows)if(row.stage==Stage::Reserved&&row.collector==id){row.stage=Stage::Removed;row.reason=Reason::CollectorLeft;if(row.item.revision<MaxCounter)++row.item.revision;}
    }
private:
    uint32_t nextId=0,nextGrant=0,creationHighWater=0; // Never reset lifetime/grant IDs during a server process.
};
class View {
public:
    uint32_t epoch=0;
    int host=-1;
    std::array<Row,MaxPickups> rows{};
    bool Reset(uint32_t next,int owner) {
        if(!next||next>MaxCounter||next<epoch||owner<-1||owner>=MaxPlayers)return false;
        if(next==epoch)return owner==host;
        epoch=next;host=owner;rows={};return true;
    }
    bool Accept(const Row&next) {
        if(!next.Valid()||next.item.epoch!=epoch)return false;
        Row*slot=nullptr;
        for(auto&row:rows)if(row.item.id==next.item.id){if(next.item.revision<=row.item.revision)return false;slot=&row;break;}
        if(!slot)for(auto&row:rows)if(!row.item.id||row.stage==Stage::Collected||row.stage==Stage::Removed){slot=&row;break;}
        if(!slot)return false;*slot=next;return true;
    }
};
// One server reservation per collector permits a bounded receipt cache. The
// actual native call is made once; replay repeats the same outcome, never effects.
struct Receipt {
    uint32_t epoch=0,id=0,grant=0;
    Actor actor;
    Outcome outcome=Outcome::UnknownAfterApply;
    bool applied=false;
    bool Matches(uint32_t e,uint32_t i,uint32_t g,const Actor&a)const{return applied&&epoch==e&&id==i&&grant==g&&SameLife(actor,a);}
};
}
