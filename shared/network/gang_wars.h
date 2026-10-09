#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace GangWarSync
{
constexpr int MAX_PLAYERS = 8, MAX_ZONES = 380, GANGS = 10;
constexpr uint32_t MAX_COUNTER = 0x7fffffff, MAX_DURATION = 2000000;
enum class Kind : uint8_t { Request, Publish, Snapshot };
struct Territory
{
    std::array<uint8_t,GANGS> density{};
    std::array<uint8_t,4> color{};
    uint8_t radar = 0;
    bool Valid() const { return radar <= 3; }
    bool operator==(const Territory& b) const { return density == b.density && color == b.color && radar == b.radar; }
};
struct World
{
    uint32_t layout = 0;
    uint16_t count = 0;
    std::array<Territory,MAX_ZONES> zones{};
    bool Valid() const
    {
        if (!layout || layout > MAX_COUNTER || !count || count > MAX_ZONES) return false;
        for (int i=0;i<count;++i) if (!zones[i].Valid()) return false;
        return true;
    }
    bool operator==(const World& b) const
    {
        if (layout != b.layout || count != b.count) return false;
        for (int i=0;i<count;++i) if (!(zones[i] == b.zones[i])) return false;
        return true;
    }
};
struct War
{
    uint8_t offense = 0, defense = 0;
    bool enabled = false, training = false, allowMission = false, onMission = false;
    int zone = -1, info = -1, gang1 = -1, gang2 = -1;
    uint32_t fightRemaining = 0, stageElapsed = 0;
    float x = 0, y = 0, z = 0;
    bool Active() const { return offense != 0 || defense != 0; }
    bool Valid() const
    {
        if (offense > 6 || defense > 2 || zone < -1 || zone >= MAX_ZONES || info < -1 || info >= MAX_ZONES
            || gang1 < -1 || gang1 >= GANGS || gang2 < -1 || gang2 >= GANGS
            || fightRemaining > MAX_DURATION || stageElapsed > MAX_DURATION
            || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)
            || x < -6000 || x > 6000 || y < -6000 || y > 6000 || z < -1000 || z > 2000) return false;
        return !Active() || (enabled && zone >= 0 && info >= 0 && gang1 >= 0);
    }
    bool operator==(const War& b) const
    {
        return offense==b.offense && defense==b.defense && enabled==b.enabled && training==b.training
            && allowMission==b.allowMission && onMission==b.onMission && zone==b.zone && info==b.info
            && gang1==b.gang1 && gang2==b.gang2 && fightRemaining==b.fightRemaining && stageElapsed==b.stageElapsed
            && x==b.x && y==b.y && z==b.z;
    }
};
struct Authority
{
    uint32_t epoch = 0, revision = 0, campaign = 0, warGeneration = 0;
    int host = -1;
    bool ready = false;
    War war;
    bool Valid() const
    {
        return epoch && epoch <= MAX_COUNTER && revision && revision <= MAX_COUNTER
            && campaign <= MAX_COUNTER && warGeneration <= MAX_COUNTER && host >= -1 && host < MAX_PLAYERS
            && (!ready || campaign != 0) && war.Valid() && (!war.Active() || (ready && warGeneration));
    }
};
class Room
{
public:
    Authority state;
    World world;
    uint32_t hostConnection = 0, lastWorld = 0, lastWar = 0;
    bool SetHost(int id, uint32_t connection)
    {
        if (id < -1 || id >= MAX_PLAYERS || (id >= 0 && (!connection || connection > MAX_COUNTER))) return false;
        if (state.epoch && id == state.host && connection == hostConnection) return true;
        if (state.epoch == MAX_COUNTER || state.revision == MAX_COUNTER) return false;
        ++state.epoch; ++state.revision; state.host = id; hostConnection = connection;
        lastWorld = lastWar = 0;
        // Native wave AI cannot be reconstructed by a promoted guest. Keep the
        // campaign/territory, invalidate the old fight without awarding a win.
        const bool enabled = state.war.enabled;
        state.war = {}; state.war.enabled = enabled;
        return true;
    }
    bool Owner(int sender, uint32_t epoch) const { return sender >= 0 && sender == state.host && epoch == state.epoch; }
    bool PublishWorld(int sender, uint32_t epoch, uint32_t campaign, uint32_t sequence, bool reset, const World& value)
    {
        if (!Owner(sender,epoch) || !sequence || sequence > MAX_COUNTER || sequence <= lastWorld
            || campaign != state.campaign || !value.Valid() || state.revision == MAX_COUNTER
            || ((!state.ready || reset) && state.campaign == MAX_COUNTER)) return false;
        if (state.ready && !reset && (value.layout != world.layout || value.count != world.count)) return false;
        if (!state.ready || reset)
        {
            ++state.campaign; state.war = {}; state.ready = true;
        }
        world = value; lastWorld = sequence; ++state.revision; return true;
    }
    bool PublishWar(int sender, uint32_t epoch, uint32_t campaign, uint32_t sequence, const War& value)
    {
        if (!Owner(sender,epoch) || !state.ready || campaign != state.campaign || !sequence || sequence > MAX_COUNTER
            || sequence <= lastWar || !value.Valid() || state.revision == MAX_COUNTER
            || (value.Active() && value.info >= world.count)) return false;
        const bool starts = value.Active() && (!state.war.Active() || value.zone != state.war.zone || value.info != state.war.info);
        if (starts && state.warGeneration == MAX_COUNTER) return false;
        if (starts) ++state.warGeneration;
        state.war = value; lastWar = sequence; ++state.revision; return true;
    }
    void EmptyRoom()
    {
        // Do not reuse an authority epoch during this server process.
        state.ready=false;state.campaign=0;state.war={};world={};state.host=-1;hostConnection=0;
        lastWorld=lastWar=0;
    }
};
class Client
{
public:
    Authority state;
    World world;
    uint32_t worldCampaign = 0, worldRevision = 0;
    bool Accept(const Authority& value)
    {
        if (!value.Valid() || (state.epoch && value.epoch < state.epoch)
            || (value.epoch == state.epoch && value.revision <= state.revision)) return false;
        if (value.epoch == state.epoch && value.campaign < state.campaign) return false;
        state=value; return true;
    }
    bool AcceptWorld(uint32_t epoch, uint32_t campaign, uint32_t revision, const World& value)
    {
        if (epoch != state.epoch || !state.ready || campaign != state.campaign || !revision || revision > MAX_COUNTER
            || revision < worldRevision || !value.Valid()) return false;
        if (campaign == worldCampaign && revision == worldRevision) return false;
        world=value;worldCampaign=campaign;worldRevision=revision;return true;
    }
    bool HasWorld() const { return state.ready && worldCampaign == state.campaign && world.Valid(); }
};
}
