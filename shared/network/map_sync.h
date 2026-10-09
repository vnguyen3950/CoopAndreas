#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace MapSync
{
constexpr int CELLS = 100;
constexpr uint32_t MAX_COUNTER = 0x7fffffff;
struct Discovery
{
    std::array<uint8_t, 13> bits{};
    bool Valid() const { return (bits.back() & 0xf0) == 0; }
    bool Has(int cell) const { return cell >= 0 && cell < CELLS && (bits[cell / 8] & (1u << (cell % 8))); }
    bool Add(int cell)
    {
        if (cell < 0 || cell >= CELLS || Has(cell)) return false;
        bits[cell / 8] |= uint8_t(1u << (cell % 8)); return true;
    }
    int Count() const { int count = 0; for (int i = 0; i < CELLS; ++i) count += Has(i); return count; }
    bool operator==(const Discovery& other) const { return bits == other.bits; }
};
inline int Cell(float x, float y)
{
    if (!std::isfinite(x) || !std::isfinite(y)) return -1;
    const int column = int((std::clamp(x, -2999.0f, 2999.0f) + 3000.0f) / 600.0f);
    const int row = 9 - int((std::clamp(y, -2999.0f, 2999.0f) + 3000.0f) / 600.0f);
    return column * 10 + row;
}
inline bool ValidWaypoint(bool place, float x, float y)
{
    return !place || (std::isfinite(x) && std::isfinite(y) && x >= -3000 && x <= 3000 && y >= -3000 && y <= 3000);
}
inline bool AcceptWaypoint(uint32_t boundGeneration, uint32_t previousSequence,
    uint32_t generation, uint32_t sequence, bool place, float x, float y)
{
    return boundGeneration && generation == boundGeneration && sequence && sequence <= MAX_COUNTER
        && sequence > previousSequence && ValidWaypoint(place, x, y);
}
class Room
{
public:
    uint32_t epoch = 0, revision = 0;
    Discovery discovery;
    bool Seed(uint32_t expectedEpoch, const Discovery& seed)
    {
        if (expectedEpoch != epoch || !seed.Valid() || m_lastEpoch == MAX_COUNTER) return false;
        epoch = ++m_lastEpoch; revision = 1; discovery = seed; return true;
    }
    bool Reveal(uint32_t expectedEpoch, const Discovery& cells)
    {
        if (!epoch || expectedEpoch != epoch || !cells.Valid() || cells.Count() != 1 || revision == MAX_COUNTER) return false;
        for (int i = 0; i < CELLS; ++i) if (cells.Has(i))
        {
            if (!discovery.Add(i)) return false;
            ++revision; return true;
        }
        return false;
    }
    void Clear() { epoch = revision = 0; discovery = {}; }
private:
    uint32_t m_lastEpoch = 0; // Empty-room resets never reuse a campaign epoch.
};
struct View
{
    uint32_t epoch = 0, revision = 0;
    Discovery discovery;
    bool Accept(uint32_t nextEpoch, uint32_t nextRevision, const Discovery& next)
    {
        if (!nextEpoch || nextEpoch > MAX_COUNTER || !nextRevision || nextRevision > MAX_COUNTER
            || !next.Valid() || nextEpoch < epoch || (nextEpoch == epoch && nextRevision <= revision)) return false;
        epoch = nextEpoch; revision = nextRevision; discovery = next; return true;
    }
};
}
