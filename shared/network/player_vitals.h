#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

// Per-player values, not session maxima or global CStats replacements.
namespace PlayerVitals
{
constexpr int MAX_PLAYERS = 8;
constexpr uint32_t MAX_COUNTER = 0x7fffffff;
constexpr float MIN_AIR_CAPACITY = 1000.0f, MAX_AIR_CAPACITY = 4001.0f;
// Stock modifier 8: 1000 + (lung stat 225 + stamina stat 22) * .0005 * 3000.
// Both stock stats cap at 1000. One extra native unit covers float rounding.
struct State
{
    uint8_t maxHealth = 100; // PlayerInfo byte used by the native health HUD.
    float pedMaxHealth = 100.0f; // Independent CPed field used by targeting.
    float breath = 1000.0f, airCapacity = 1000.0f;
    bool submerged = false;
    bool Valid() const
    {
        return maxHealth != 0 && std::isfinite(pedMaxHealth) && pedMaxHealth >= 1.0f && pedMaxHealth <= 255.0f
            && std::isfinite(airCapacity) && airCapacity >= MIN_AIR_CAPACITY && airCapacity <= MAX_AIR_CAPACITY
            && std::isfinite(breath) && breath >= 0.0f && breath <= airCapacity;
    }
    bool operator==(const State& other) const
    {
        return maxHealth == other.maxHealth && pedMaxHealth == other.pedMaxHealth
            && breath == other.breath && airCapacity == other.airCapacity && submerged == other.submerged;
    }
};
inline bool NormalizeNative(State& state)
{
    if (!std::isfinite(state.breath) || !std::isfinite(state.airCapacity)) return false;
    // Native recovery adds a whole timestep and may transiently exceed capacity.
    state.breath = std::max(0.0f, std::min(state.breath, state.airCapacity));
    return state.Valid();
}
inline float HealthPercent(float health, const State& state)
{
    if (!state.Valid() || !std::isfinite(health)) return 0.0f;
    return std::clamp(health * 100.0f / float(state.maxHealth), 0.0f, 100.0f);
}
inline float BreathPercent(const State& state)
{ return state.Valid() ? state.breath * 100.0f / state.airCapacity : 0.0f; }
inline bool ShowBreath(const State& state)
{ return state.Valid() && (state.submerged || state.breath < state.airCapacity); }

class GenerationCounter
{
public:
    uint32_t Next()
    { if (m_last == MAX_COUNTER) return 0; return ++m_last; }
private:
    uint32_t m_last = 0;
};

// SYSTEM ordering binds a fresh CNetworkPlayer between its connect and disconnect
// notifications. Identity frames have sequence zero; samples have positive sequence.
struct Cache
{
    int owner = -1;
    uint32_t generation = 0, sequence = 0;
    bool hasState = false;
    State state;
    bool Bind(int id, uint32_t life)
    {
        if (id < 0 || id >= MAX_PLAYERS || !life || life > MAX_COUNTER || generation) return false;
        owner = id; generation = life; sequence = 0; hasState = false; return true;
    }
    bool Accept(int id, uint32_t life, uint32_t revision, const State& sample)
    {
        if (id != owner || !generation || life != generation || !revision || revision > MAX_COUNTER
            || revision <= sequence || !sample.Valid()) return false;
        state = sample; sequence = revision; hasState = true; return true;
    }
    bool AcceptOwner(int authenticatedOwner, int claimedOwner, uint32_t claimedGeneration,
        uint32_t revision, const State& sample)
    {
        // Clients cannot choose a connection generation or publish another slot.
        if (authenticatedOwner != owner || claimedOwner != owner || claimedGeneration != 0) return false;
        return Accept(owner, generation, revision, sample);
    }
};
}
