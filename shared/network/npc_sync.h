#pragma once
#include <cmath>
#include <cstdint>

namespace NPCSync {
using Counter = uint32_t;
constexpr Counter MaxCounter = 0x7fffffffu;
struct Stamp {
    Counter generation = 0, epoch = 0, sequence = 0;
    bool Lifetime() const { return generation > 0 && generation <= MaxCounter && epoch > 0 && epoch <= MaxCounter; }
    bool State() const { return Lifetime() && sequence > 0 && sequence <= MaxCounter; }
    bool SameOwner(const Stamp& other) const { return Lifetime() && generation == other.generation && epoch == other.epoch; }
    bool Newer(const Stamp& other) const { return State() && SameOwner(other) && sequence > other.sequence; }
    template<class Stream> bool Serialize(Stream& stream) {
        serialize_int(stream, generation, 0, int(MaxCounter));
        serialize_int(stream, epoch, 0, int(MaxCounter));
        serialize_int(stream, sequence, 0, int(MaxCounter));
        return true;
    }
};
inline bool Finite(float value, float limit) { return std::isfinite(value) && std::abs(value) <= limit; }
template<class Vector> bool VectorValid(const Vector& v, float limit) {
    return Finite(v.x, limit) && Finite(v.y, limit) && Finite(v.z, limit);
}
// Native values may exceed the compression window (e.g. interior positions).
// Reject nonfinite/raw extremes rather than passing them to engine setters.
template<class Vector> bool Position(const Vector& v) { return VectorValid(v, 20000.0f); }
template<class Vector> bool Velocity(const Vector& v) { return VectorValid(v, 100.0f); }
inline bool Assignable(const Stamp& current, const Stamp& next, int currentOwner, int nextOwner) {
    return next.Lifetime() && current.generation == next.generation && nextOwner >= 0 && nextOwner < 8 &&
        (next.epoch > current.epoch || (next.epoch == current.epoch && nextOwner == currentOwner));
}
}
