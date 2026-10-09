#pragma once
#include <array>
#include <cmath>
#include <cstdint>
namespace TrailerSync {
constexpr uint32_t MaxCounter=0x7fffffff;
constexpr int MaxVehicles=255, MaxPlayers=8;
inline bool Counter(uint32_t n) { return n && n<=MaxCounter; }
struct Ref {
    uint32_t id=0,generation=0; int model=0;
    bool Valid() const { return id<MaxVehicles && Counter(generation) && model>=400 && model<=611; }
};
inline bool Same(const Ref& a,const Ref& b) { return a.Valid() && b.Valid() && a.id==b.id && a.generation==b.generation && a.model==b.model; }
inline bool Pair(int cab,int trailer) {
    switch(trailer) {
    case 435: case 450: case 584: case 591: return cab==403 || cab==514 || cab==515;
    case 606: case 607: case 608: return cab==485 || cab==583;
    case 610: return cab==531;
    case 611: return cab==552;
    default: return false;
    }
}
struct Vec {
    float x=0,y=0,z=0;
    bool Valid(float limit) const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
        std::abs(x)<=limit && std::abs(y)<=limit && std::abs(z)<=limit; }
    float Dot(const Vec& b) const { return x*b.x+y*b.y+z*b.z; }
};
struct Frame {
    Vec position{},right{1,0,0},forward{0,1,0},velocity{},turn{}; float health=1000;
    bool Valid() const { return position.Valid(20000) && right.Valid(1.1f) && forward.Valid(1.1f) &&
        right.Dot(right)>=0.9f && right.Dot(right)<=1.1f && forward.Dot(forward)>=0.9f && forward.Dot(forward)<=1.1f &&
        std::abs(right.Dot(forward))<=0.05f && velocity.Valid(50) && turn.Valid(10) &&
        std::isfinite(health) && health>=0 && health<=10000; }
};
struct Lease {
    Ref vehicle{}; uint32_t epoch=0; int owner=-1; bool live=false;
    bool Valid() const { return vehicle.Valid() && Counter(epoch) && owner>=-1 && owner<MaxPlayers; }
};
struct Link {
    uint32_t room=0,ownerEpoch=0,sequence=0,revision=0; Ref parent{},child{}; bool attached=false; Frame frame{};
    bool Valid() const { return Counter(room) && Counter(ownerEpoch) && Counter(sequence) && revision<=MaxCounter &&
        parent.Valid() && child.Valid() && parent.id!=child.id && Pair(parent.model,child.model) && frame.Valid(); }
};
struct Pose {
    Link link{};
    bool Valid() const { return link.Valid() && link.attached && Counter(link.revision); }
};
class Cache {
public:
    uint32_t room=0;
    std::array<Lease,MaxVehicles> leases{};
    std::array<Link,MaxVehicles> links{},pendingPoses{};
    bool Reset(uint32_t epoch) { if(!Counter(epoch) || epoch<room)return false; if(epoch>room){*this={};room=epoch;}return true; }
    bool Bind(const Lease& next) {
        if(!next.Valid())return false; auto& old=leases[next.vehicle.id];
        if(old.vehicle.Valid() && (next.vehicle.generation<old.vehicle.generation ||
            (next.vehicle.generation==old.vehicle.generation && (next.epoch<old.epoch || (!old.live && next.live)))))return false;
        old=next;return true;
    }
    bool Current(const Ref& ref) const { return ref.Valid() && leases[ref.id].live && Same(ref,leases[ref.id].vehicle); }
    bool Authorized(const Link& link,int sender) const {
        return link.Valid() && link.room==room && Current(link.parent) && Current(link.child) &&
            leases[link.parent.id].epoch==link.ownerEpoch && leases[link.parent.id].owner==sender && sender>=0;
    }
    bool State(const Link& next) {
        if(!next.Valid() || !Counter(next.revision) || next.room!=room)return false;
        auto& old=links[next.child.id];
        if(old.revision>=next.revision || (old.child.Valid() && old.child.generation>next.child.generation))return false;
        old=next; auto& pending=pendingPoses[next.child.id];
        if(next.attached && pending.revision==next.revision && pending.sequence>next.sequence && Same(pending.parent,next.parent) && Same(pending.child,next.child) && pending.ownerEpoch==next.ownerEpoch)old=pending;
        if(pending.revision<=next.revision)pending={};return true;
    }
    bool Position(const Link& next) {
        if(!next.Valid() || !next.attached || !Counter(next.revision) || next.room!=room)return false;
        auto& old=links[next.child.id];
        if(next.revision<old.revision || (next.revision==old.revision && (!old.attached || next.sequence<=old.sequence ||
            !Same(next.parent,old.parent) || !Same(next.child,old.child) || next.ownerEpoch!=old.ownerEpoch)))return false;
        if(next.revision==old.revision){old=next;return true;}
        auto& pending=pendingPoses[next.child.id];
        if(next.revision<pending.revision || (next.revision==pending.revision && next.sequence<=pending.sequence))return false;
        pending=next;return true;
    }
    bool Linked(uint32_t id,uint32_t birth) const { return id<MaxVehicles && links[id].attached && links[id].child.generation==birth &&
        Current(links[id].child) && Current(links[id].parent) && leases[links[id].parent.id].epoch==links[id].ownerEpoch; }
};
}
