#include <iostream>
#include "../shared/network/vehicle_authority.h"

struct Player { int id; };
static unsigned checks = 0, failures = 0;
static void expect(bool value, const char* message)
{
    ++checks;
    if (!value) { ++failures; std::cout << "FAIL: " << message << '\n'; }
}

int main()
{
    Player a{1}, b{2}, outsider{3};
    const Player* syncer = &a;
    const Player* driver = nullptr;
    expect(VehicleAuthority::CanUpdateDriver(&a, syncer, driver),
        "Assigned syncer can publish while the driver slot is empty.");
    expect(!VehicleAuthority::CanUpdateDriver(&b, syncer, driver),
        "An unrelated sender cannot claim an empty driver slot through a snapshot.");

    // VEHICLE_ENTER records B before VEHICLE_DRIVER_UPDATE reassigns the syncer.
    driver = &b;
    expect(!VehicleAuthority::CanUpdateDriver(&a, syncer, driver),
        "A delayed snapshot from old syncer A is rejected after B enters.");
    expect(syncer == &a && driver == &b,
        "Rejecting A leaves the pending B entry and old syncer unchanged.");
    expect(VehicleAuthority::CanUpdateDriver(&b, syncer, driver),
        "Recorded driver B may send the first snapshot before receiving syncer assignment.");
    if (VehicleAuthority::CanUpdateDriver(&b, syncer, driver))
        syncer = &b; // Models the server's guarded ReassignSyncer operation.
    expect(syncer == &b && driver == &b, "B's accepted snapshot completes authority transfer.");
    expect(!VehicleAuthority::CanUpdateDriver(&a, syncer, driver),
        "Old driver A stays rejected after the authority transfer.");
    expect(!VehicleAuthority::CanUpdateDriver(&outsider, syncer, driver),
        "An unrelated sender cannot update a vehicle with a recorded driver.");

    const Player* identities[] = {nullptr, &a, &b, &outsider};
    for (const auto* sender : identities)
        for (const auto* owner : identities)
            for (const auto* occupant : identities)
            {
                const bool expected = sender && (occupant ? sender == occupant : sender == owner);
                expect(VehicleAuthority::CanUpdateDriver(sender, owner, occupant) == expected,
                    "All sender/syncer/driver identities obey recorded-driver precedence.");
            }
    driver = nullptr;
    expect(VehicleAuthority::CanUpdateDriver(&b, syncer, driver),
        "The current syncer fallback becomes available again after the driver exits.");
    std::cout << "RESULT: " << checks << " assertions, " << failures << " failures.\n";
    std::cout << "SCOPE: Tests the production authority predicate, not native entry or ENet timing.\n";
    return failures ? 1 : 0;
}
