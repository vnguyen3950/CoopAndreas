#pragma once
#include <cmath>
#include <cstdint>

// Owner-observed visual associations only. These are not replicated native tasks.
namespace PlayerAnimation
{
constexpr uint32_t MAX_COUNTER = 0x7fffffff;
constexpr int MAX_PLAYERS = 8;
struct Life
{
    uint32_t generation = 0, birth = 0, sequence = 0;
    int model = 0, area = 0, nativeReference = 0;
    bool ready = true; // False boundaries invalidate collector eligibility until a ready owner sample.
    bool Valid(bool stamped = false) const
    {
        return generation <= MAX_COUNTER && (!stamped || generation != 0)
            && birth && birth <= MAX_COUNTER && sequence && sequence <= MAX_COUNTER
            && model >= 0 && model <= 299 && area >= 0 && area <= 255
            && nativeReference >= 0 && uint32_t(nativeReference) <= MAX_COUNTER;
    }
};
// 1..4: PLAYIDLES 261..264; 5: default IDLE_CHAT 12. Zero means stop.
struct State
{
    int pose = 0;
    float phase = 0, duration = 0, speed = 0, blend = 0;
    bool loop = false;
    uint32_t instance = 0; // Owner source association lifetime, distinct from sample sequence.
    bool Valid() const
    {
        if (pose < 0 || pose > 5 || !std::isfinite(phase) || !std::isfinite(duration)
            || !std::isfinite(speed) || !std::isfinite(blend)) return false;
        if (!pose) return phase == 0 && duration == 0 && speed == 0 && blend == 0 && !loop && !instance;
        return instance && instance <= MAX_COUNTER && duration > 0 && duration <= 60 && phase >= 0 && phase <= duration
            && speed >= 0 && speed <= 3 && blend >= 0 && blend <= 1;
    }
};
inline int Group(int pose) { return pose >= 1 && pose <= 4 ? 49 : 0; }
inline int Animation(int pose) { return pose >= 1 && pose <= 4 ? 260 + pose : pose == 5 ? 12 : -1; }
inline int Pose(int group, int animation)
{ return group == 49 && animation >= 261 && animation <= 264 ? animation - 260 : group == 0 && animation == 12 ? 5 : 0; }
inline bool Phase(const State& state, uint32_t sampledAt, uint32_t now, float& phase)
{
    if (!state.Valid() || !state.pose) return false;
    // Signed modular difference handles the server tick wrap and future samples.
    const int32_t age = int32_t(now - sampledAt);
    phase = state.phase + (age > 0 ? float(age) * .001f * state.speed : 0.f);
    if (state.loop) phase = std::fmod(phase, state.duration);
    return state.loop || phase < state.duration;
}
struct Cache
{
    Life life;
    State state;
    uint32_t sampledAt = 0;
    bool hasLife = false;
    bool Accept(const Life& next, const State& visual, uint32_t time)
    {
        if (!next.Valid(true) || !visual.Valid() || (!next.ready && visual.pose)) return false;
        if (hasLife && (next.generation != life.generation || next.birth < life.birth
            || next.sequence <= life.sequence)) return false;
        life = next; state = visual; sampledAt = time; hasLife = true; return true;
    }
};
// Used by both owner operations and publications; never wrap within a connection.
struct OwnerClock
{
    uint32_t birth = 0, sequence = 0;
    bool NewBirth()
    { if (birth == MAX_COUNTER || sequence == MAX_COUNTER) return false; ++birth; ++sequence; return true; }
    bool Next()
    { if (!birth || sequence == MAX_COUNTER) return false; ++sequence; return true; }
};
}
