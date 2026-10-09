#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <vector>
#include "../shared/network/object_sync.h"

using namespace ObjectSync;
static unsigned checks = 0, failures = 0;
static void expect(bool value, const char* description) {
    ++checks;
    if (!value) { ++failures; std::cout << "FAIL: " << description << '\n'; }
}
static State normal() { State s; s.model = 1486; return s; }
static std::vector<uint8_t> envelope(uint16_t op, unsigned count) {
    std::vector<uint8_t> b(4 + count * 4, 0);
    b[0] = uint8_t(op); b[1] = uint8_t(op >> 8); b[2] = uint8_t(count);
    if (count) { uint32_t operand = 0x14; std::memcpy(b.data()+4, &operand, 4); }
    if (op == 0x08d2 && count > 1) { float scale = 1; std::memcpy(b.data()+8, &scale, 4); }
    return b;
}

static void registry_tests() {
    State s = normal(); Registry r;
    auto a = r.Create(7, 10, 1, s);
    expect(a != 0 && r.ById(a) && r.ByToken(7,10)->id == a, "Creation establishes both identity indexes.");
    expect(r.Create(7,10,2,s) == 0, "A live owner token cannot be created twice.");
    expect(r.Create(7,9,1,s) == 0, "An out-of-order lower owner token is rejected.");
    expect(!r.Update(8,10,2,s) && r.Remove(8,10) == 0, "A different owner cannot update or remove the object.");
    State changed = s; changed.health = 321;
    expect(r.Update(7,10,2,changed), "A later revision from the owner is accepted.");
    expect(!r.Update(7,10,2,s) && !r.Update(7,10,1,s) && !r.Update(7,10,0,s), "Duplicate, stale, and zero revisions are rejected.");
    expect(r.ById(a)->state.health == 321 && r.ById(a)->revision == 2, "Rejected revisions do not change the record.");
    changed.model++;
    expect(!r.Update(7,10,3,changed), "An update cannot change the object's model identity.");
    changed = s; changed.position.x = std::numeric_limits<float>::infinity();
    expect(!r.Update(7,10,3,changed), "An invalid finite-value update is rejected.");
    expect(r.ById(a)->revision == 2, "Invalid updates do not consume a revision.");
    expect(r.Remove(7,10) == a && !r.ById(a) && !r.ByToken(7,10), "Removal clears both identity indexes.");
    expect(!r.Update(7,10,99,s) && r.Remove(7,10) == 0 && r.Create(7,10,1,s) == 0, "A deleted token cannot mutate, remove, or recreate an object.");
    expect(r.Create(7,8,1,s) == 0, "Deleting an object does not reset the owner's high-water mark.");
    auto next = r.Create(7,11,1,s);
    expect(next > a, "A replacement gets a new global object ID.");
    auto other = r.Create(8,11,1,s);
    expect(other > next && r.ByToken(7,11)->id != r.ByToken(8,11)->id, "Different owners may use the same token without aliasing.");
    auto removed = r.RemoveOwner(7);
    expect(removed.size() == 1 && removed[0] == next && r.ById(other), "Owner removal preserves other owners' records.");
    expect(r.Create(7,11,1,s) == 0, "Mission-style owner removal preserves token history.");
    r.Disconnect(7);
    auto reconnected = r.Create(7,1,1,s);
    expect(reconnected > other && !r.ById(a) && !r.ById(next), "A reused player slot gets a fresh global ID after disconnect.");
    expect(r.ByToken(7,1)->id == reconnected && r.ById(other), "Reconnect does not resurrect old IDs or affect other owners.");
    r.Disconnect(7);
    expect(!r.ById(reconnected), "Disconnect removes the owner's newly created object.");
    expect(r.Create(-1,1,1,s) == 0 && r.Create(9,0,1,s) == 0 && r.Create(9,MAX_ID+1,1,s) == 0 && r.Create(9,1,0,s) == 0,
           "Invalid owners, token bounds, and zero creation revisions are rejected.");
    expect(r.Create(9,MAX_ID,1,s) > other, "The maximum valid owner token is accepted.");
    Registry capacity;
    for (uint32_t i = 1; i <= MAX_OBJECTS; ++i) expect(capacity.Create(1,i,1,s) != 0, "Each object within the documented capacity is accepted.");
    expect(capacity.records.size() == MAX_OBJECTS && capacity.Create(1,MAX_OBJECTS+1,1,s) == 0, "The next object beyond capacity is rejected.");
    auto released = capacity.Remove(1,1);
    auto replacement = capacity.Create(1,MAX_OBJECTS+1,1,s);
    expect(released && replacement > MAX_OBJECTS && capacity.records.size() == MAX_OBJECTS, "Capacity can be reclaimed without recycling global IDs.");
    expect(capacity.Create(1,1,1,s) == 0, "Capacity reclamation does not revive a stale token.");
    Registry invalid;
    State bad = s; bad.health = -1;
    expect(invalid.Create(1,5,1,bad) == 0 && invalid.Create(1,5,1,s) != 0, "A rejected creation does not consume its token or ID.");
    Registry revisions;
    auto id = revisions.Create(1,1,1,s);
    expect(revisions.Update(1,1,0x7fffffffu,s), "The largest revision representable by the wire contract is accepted.");
    expect(!revisions.Update(1,1,0x80000000u,s) && !revisions.Update(1,1,std::numeric_limits<uint32_t>::max(),s),
           "Revisions outside the wire contract are rejected.");
    expect(!revisions.Update(1,1,0,s) && !revisions.Update(1,1,1,s) && revisions.ById(id)->revision == 0x7fffffffu,
           "Zero, stale, or wrapped revisions cannot replace the last valid record.");
    Registry creation_revision;
    expect(creation_revision.Create(1,1,0x7fffffffu,s) != 0 && creation_revision.Create(2,1,0x80000000u,s) == 0,
           "Creation applies the same wire revision bounds as updates.");
}

static void state_tests() {
    State s = normal();
    expect(s.Valid(), "A normal state is valid.");
    s.model = uint16_t(MAX_MODEL); expect(s.Valid(), "The maximum model index is accepted.");
    s.model = uint16_t(MAX_MODEL+1); expect(!s.Valid(), "A model index beyond the contract limit is rejected.");
    s = normal(); s.area = 18; expect(s.Valid(), "The maximum area is accepted.");
    s.area = 19; expect(!s.Valid(), "An area beyond the contract limit is rejected.");
    Vec3 State::* fields[] = {&State::position,&State::rotation,&State::velocity,&State::turnSpeed};
    const float bounds[] = {20000,100,200,200};
    auto set_axis = [](Vec3& v, int i, float x) { if (i==0) v.x=x; else if (i==1) v.y=x; else v.z=x; };
    const float inf = std::numeric_limits<float>::infinity();
    for (unsigned f=0; f<4; ++f) for (int axis=0; axis<3; ++axis) {
        for (float sign : {-1.0f,1.0f}) {
            s=normal(); set_axis(s.*fields[f],axis,sign*bounds[f]); expect(s.Valid(), "A vector component exactly on either boundary is accepted.");
            set_axis(s.*fields[f],axis,sign*std::nextafter(bounds[f],inf)); expect(!s.Valid(), "A vector component just beyond either boundary is rejected.");
        }
        for (float value : {inf,-inf,std::numeric_limits<float>::quiet_NaN()}) {
            s=normal(); set_axis(s.*fields[f],axis,value); expect(!s.Valid(), "Every vector component rejects infinity and NaN.");
        }
    }
    for (float value : {0.0f,100000.0f}) { s=normal(); s.health=value; expect(s.Valid(), "Health includes both documented endpoints."); }
    for (float value : {-1.0f,std::nextafter(100000.0f,inf),inf,-inf,std::numeric_limits<float>::quiet_NaN()}) {
        s=normal(); s.health=value; expect(!s.Valid(), "Health rejects out-of-range and nonfinite values.");
    }
    for (float value : {std::numeric_limits<float>::min(),1.0f,100.0f}) { s=normal(); s.scale=value; expect(s.Valid(), "Positive scale includes its documented upper endpoint."); }
    for (float value : {0.0f,-1.0f,std::nextafter(100.0f,inf),inf,-inf,std::numeric_limits<float>::quiet_NaN()}) {
        s=normal(); s.scale=value; expect(!s.Valid(), "Scale rejects zero, negative, oversized, and nonfinite values.");
    }
}

struct Opcode { uint16_t id; unsigned inputs; bool snapshot_only; };
// Counts are independent SDK input counts; outputs are intentionally excluded.
static const Opcode sdk[] = {
    {0x0107,4,true},{0x029b,4,true},{0x0108,1,true},{0x01c4,1,true},
    {0x0176,1,true},{0x01bb,1,true},{0x0366,1,true},{0x034e,8,true},{0x0723,2,true},
    {0x0177,2,false},{0x035d,2,false},{0x0382,2,false},{0x0392,2,false},{0x0550,2,false},
    {0x0566,2,false},{0x071f,2,false},{0x0750,2,false},{0x0875,2,false},{0x08d2,2,false},
    {0x01bc,4,false},{0x0381,4,false},{0x0453,4,false},{0x09ca,6,false}
};
static bool oracle(uint16_t op, uint8_t packed_count, size_t length, const uint8_t* data) {
    bool framing=false;
    for (const auto& item : sdk) if (item.id == op)
        framing=!item.snapshot_only && packed_count == item.inputs && length == 4 + item.inputs*4;
    if (!framing) return false;
    auto integer=[data](unsigned i) { int32_t x; std::memcpy(&x,data+4+4*i,4); return x; };
    auto real=[data](unsigned i) { float x; std::memcpy(&x,data+4+4*i,4); return x; };
    // Independent SDK signatures: coordinates, velocity, angles, scale,
    // integer health/area, and boolean proof/flag setters have distinct domains.
    if (op==0x01bc || op==0x0381 || op==0x0453 || op==0x0177 || op==0x08d2) {
        unsigned end=op==0x0177 || op==0x08d2 ? 2 : 4;
        float bound=op==0x01bc ? 20000 : op==0x0381 ? 200 : op==0x08d2 ? 100 : 36000;
        for (unsigned i=1;i<end;++i) {
            float x=real(i);
            if (!std::isfinite(x) || x < -bound || x > bound) return false;
        }
        return op!=0x08d2 || real(1)>0;
    }
    if (op==0x071f) return integer(1)>=0 && integer(1)<=100000;
    if (op==0x0566) return integer(1)>=0 && integer(1)<=18;
    unsigned end=op==0x09ca ? 6 : 2;
    for (unsigned i=1;i<end;++i) if (integer(i)!=0 && integer(i)!=1) return false;
    return true;
}
static void envelope_tests() {
    expect(!ValidOpcode(nullptr,0) && !ValidOpcode(nullptr,4096), "Null envelopes are rejected regardless of claimed length.");
    for (const auto& op : sdk) {
        expect(InputCount(op.id)==int(op.inputs), "The contract input count agrees with the independent SDK definition.");
        expect(IsSnapshotOnlyOpcode(op.id)==op.snapshot_only, "Queries, creation, deletion, slide, and break use the snapshot-only path.");
        auto b=envelope(op.id,op.inputs);
        expect(ValidOpcode(b.data(),b.size())==!op.snapshot_only, "Only primitive setter envelopes are replayable.");
        for (size_t n=0;n<b.size();++n) expect(!ValidOpcode(b.data(),n), "Every truncation of an otherwise valid envelope is rejected.");
        b.push_back(0); expect(!ValidOpcode(b.data(),b.size()), "Trailing bytes are rejected."); b.pop_back();
        for (unsigned n=0;n<256;++n) {
            b[2]=uint8_t(n); expect(ValidOpcode(b.data(),b.size())==(!op.snapshot_only && n==op.inputs), "All count/text-count encodings obey the exact envelope shape.");
        }
    }
    for (unsigned op=0;op<=65535;++op) {
        auto b=envelope(uint16_t(op),2);
        expect(ValidOpcode(b.data(),b.size())==oracle(uint16_t(op),2,b.size(),b.data()), "An exhaustive opcode sweep rejects unsupported and negated opcodes.");
    }
    std::mt19937 random(0xC001CAFE);
    for (unsigned i=0;i<50000;++i) {
        const size_t size=random()%80; std::vector<uint8_t> b(size);
        for (auto& x:b) x=uint8_t(random());
        const bool expected=size>=4 && oracle(uint16_t(b[0])|(uint16_t(b[1])<<8),b[2],size,b.data());
        expect(ValidOpcode(b.empty()?nullptr:b.data(),size)==expected, "Deterministic malformed-envelope fuzzing agrees with the independent oracle.");
    }
    struct Domain { uint16_t op; unsigned count, first, last; float bound; };
    const Domain floats[]={{0x01bc,4,1,3,20000},{0x0381,4,1,3,200},{0x0453,4,1,3,36000},{0x0177,2,1,1,36000},{0x08d2,2,1,1,100}};
    const float inf=std::numeric_limits<float>::infinity();
    for (const auto& domain:floats) for (unsigned i=domain.first;i<=domain.last;++i) {
        for (float x:{inf,-inf,std::numeric_limits<float>::quiet_NaN(),std::nextafter(domain.bound,inf),-std::nextafter(domain.bound,inf)}) {
            auto b=envelope(domain.op,domain.count); std::memcpy(b.data()+4+4*i,&x,4);
            expect(!ValidOpcode(b.data(),b.size()), "Float operands reject nonfinite and out-of-range payloads before native replay.");
        }
        for (float x:{domain.bound,-domain.bound}) {
            auto b=envelope(domain.op,domain.count); std::memcpy(b.data()+4+4*i,&x,4);
            expect(ValidOpcode(b.data(),b.size())==(domain.op!=0x08d2 || x>0), "Float payload boundary acceptance respects the setter signature.");
        }
    }
    for (uint16_t op:{uint16_t(0x071f),uint16_t(0x0566),uint16_t(0x035d),uint16_t(0x09ca)}) {
        unsigned count=op==0x09ca?6:2; int32_t maximum=op==0x071f?100000:op==0x0566?18:1;
        for (unsigned i=1;i<count;++i) for (int32_t x:{int32_t(-1),int32_t(0),maximum,int32_t(maximum+1),std::numeric_limits<int32_t>::min(),std::numeric_limits<int32_t>::max()}) {
            auto b=envelope(op,count); std::memcpy(b.data()+4+4*i,&x,4);
            expect(ValidOpcode(b.data(),b.size())==(x>=0 && x<=maximum), "Integer and boolean payloads enforce their signature-specific domains.");
        }
    }
    auto scale=envelope(0x08d2,2); float zero=0;std::memcpy(scale.data()+8,&zero,4);
    expect(!ValidOpcode(scale.data(),scale.size()), "A zero scale payload is rejected before native replay.");
    auto probe=envelope(0x01bc,4);float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(probe.data()+8,&nan,4);
    expect(!ValidOpcode(probe.data(),probe.size()), "The previously accepted NaN coordinate probe is now rejected.");
    std::cout << "PROBE: The NaN coordinate setter is rejected by the new signature-aware validator.\n";
    std::cout << "SCOPE: Native object identity, model loading, collision and damage execution remain integration responsibilities.\n";
}
int main() {
    registry_tests(); state_tests(); envelope_tests();
    std::cout << "RESULT: " << checks << " assertions, " << failures << " failures.\n";
    return failures?1:0;
}
