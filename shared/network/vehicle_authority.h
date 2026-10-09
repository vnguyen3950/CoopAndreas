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
}
