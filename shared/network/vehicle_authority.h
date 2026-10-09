#pragma once

namespace VehicleAuthority
{
// A reliable entry reserves the driver slot before the first unreliable snapshot.
// Once reserved, an old syncer must not reclaim it through a delayed snapshot.
template <typename Player>
constexpr bool CanUpdateDriver(const Player* sender, const Player* syncer, const Player* driver) noexcept
{
    return sender && (driver ? driver == sender : syncer == sender);
}
// NPC driving follows the ped's owner, which can differ from the idle vehicle
// syncer. A reliable player entry takes precedence over stale NPC snapshots.
template <typename Player>
constexpr bool CanUpdateNpcDriver(const Player* sender, const Player* pedSyncer, const Player* playerDriver) noexcept
{
    return sender && sender == pedSyncer && !playerDriver;
}
}
