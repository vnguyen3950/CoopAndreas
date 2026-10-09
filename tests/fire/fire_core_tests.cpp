#include <iostream>
#include "network/fire_sync.h"
using namespace FireSync;
static int checks = 0, failures = 0;
static void expect(bool ok, const char* msg) { ++checks; if (!ok) { ++failures; std::cerr << "FAIL: " << msg << '\n'; } }
int main() {
    Cache cache;
    expect(cache.Reset(1, 0), "First host epoch binds");
    State s; s.key = {1, 1, 1, 1}; s.strength = 1; s.remaining = 7000;
    expect(s.Valid() && cache.Accept(s), "New bounded fire accepted");
    expect(!cache.Accept(s), "Duplicate state ignored");
    expect(!cache.Remove({1, 1, 1, 1}), "Equal/stale removal ignored");
    expect(cache.Remove({1, 1, 1, 2}), "Fresh removal accepted");
    expect(!cache.Accept(s), "Delayed state cannot resurrect removed fire");
    s.key.sequence = 3; expect(!cache.Accept(s), "Same-generation tombstone cannot resurrect even with a later sequence");
    s.key.generation = 2; s.key.sequence = 1;
    expect(cache.Accept(s), "Slot reuse requires new generation");
    expect(!cache.Remove({1, 1, 1, 3}), "Old generation cannot remove reused slot");
    expect(cache.Reset(2, 1) && !cache.Accept(s), "Host migration fences old epoch");
    s.key.epoch = 2; expect(cache.Accept(s), "New epoch can populate");
    expect(!cache.Reset(1, 0), "Stale host reset rejected");
    Entity ped; ped.kind = Kind::Ped; ped.id = 0; ped.generation = 3; ped.ownerEpoch = 4; ped.owner = 1; ped.model = 105;
    expect(ped.Valid(), "NPC identity contract valid");
    expect(!AllowDamage(true, 0, ped, false, true, true), "Host cannot damage guest-owned NPC representation");
    expect(AllowDamage(false, 1, ped, true, true, true), "Actual owner damages canonical fire target");
    expect(!AllowDamage(false, 2, ped, false, true, true), "Other guest cannot duplicate burn damage");
    ped.owner = 0;
    expect(!AllowDamage(false, 1, ped, false, true, true), "Guest cannot damage host-owned NPC representation");
    expect(AllowDamage(true, 0, ped, true, true, true), "Host-owned target burns on host");
    expect(!AllowDamage(false, 1, ped, true, false, true), "Remote visual entry cannot enter native damage");
    expect(!AllowDamage(false, 1, ped, true, true, false), "Stale native pool binding cannot damage");
    Entity reused = ped; reused.generation++;
    expect(!Matches(ped, reused), "Reused NPC slot cannot match old generation");
    reused = ped; reused.ownerEpoch++;
    expect(!Matches(ped, reused), "Owner epoch transfer invalidates old target grant");
    std::cout << checks << " assertions, " << failures << " failures\n";
    return failures ? 1 : 0;
}
