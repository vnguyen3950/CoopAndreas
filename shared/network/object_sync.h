#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

// IDs fit the positive half of OpcodeParameter's signed 28-bit field.
namespace ObjectSync
{
constexpr uint32_t MAX_ID = 0x07ffffff;
constexpr size_t MAX_OBJECTS = 128;
constexpr size_t MAX_PENDING = 512;
constexpr uint32_t MAX_MODEL = 19999;

struct Vec3
{
    float x = 0, y = 0, z = 0;
    bool Valid(float bound) const
    {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z)
            && std::fabs(x) <= bound && std::fabs(y) <= bound && std::fabs(z) <= bound;
    }
};

struct State
{
    Vec3 position, rotation, velocity, turnSpeed;
    float health = 1000, scale = 1;
    uint16_t model = 0;
    uint8_t area = 0, lastWeaponDamage = 255;
    bool collision = true, visible = true, dynamic = false, targetable = false;
    bool bulletProof = false, fireProof = false, collisionProof = false;
    bool meleeProof = false, explosionProof = false, playerOnlyDamage = false;

    bool Valid() const
    {
        return model <= MAX_MODEL && area <= 18 && position.Valid(20000)
            && rotation.Valid(100) && velocity.Valid(200) && turnSpeed.Valid(200)
            && std::isfinite(health) && health >= 0 && health <= 100000
            && std::isfinite(scale) && scale > 0 && scale <= 100;
    }
};

// Transport-independent authority/lifetime state, shared with headless tests.
// Deleted owner tokens remain below the high-water mark and cannot be reused.
class Registry
{
public:
    struct Record { uint32_t id, token, revision; int owner; State state; };
    std::unordered_map<uint32_t, Record> records;
    uint32_t Create(int owner, uint32_t token, uint32_t revision, const State& state)
    {
        if (owner < 0 || !token || token > MAX_ID || !revision || revision > 0x7fffffff || !state.Valid()
            || records.size() >= MAX_OBJECTS || m_nextId > MAX_ID || token <= m_highWater[owner])
            return 0;
        uint32_t id = m_nextId++;
        records.emplace(id, Record{id, token, revision, owner, state});
        m_tokens[Key(owner, token)] = id;
        m_highWater[owner] = token;
        return id;
    }
    Record* ByToken(int owner, uint32_t token)
    {
        auto it = m_tokens.find(Key(owner, token));
        return it == m_tokens.end() ? nullptr : ById(it->second);
    }
    Record* ById(uint32_t id)
    {
        auto it = records.find(id);
        return it == records.end() ? nullptr : &it->second;
    }
    bool Update(int owner, uint32_t token, uint32_t revision, const State& state)
    {
        auto* r = ByToken(owner, token);
        if (!r || revision <= r->revision || revision > 0x7fffffff || !state.Valid() || state.model != r->state.model)
            return false;
        r->revision = revision; r->state = state; return true;
    }
    uint32_t Remove(int owner, uint32_t token)
    {
        auto* r = ByToken(owner, token);
        if (!r) return 0;
        auto id = r->id;
        m_tokens.erase(Key(owner, token)); records.erase(id);
        return id;
    }
    std::vector<uint32_t> RemoveOwner(int owner)
    {
        std::vector<uint32_t> ids;
        for (const auto& item : records) if (item.second.owner == owner) ids.push_back(item.first);
        for (auto id : ids) Remove(owner, records.at(id).token);
        return ids;
    }
    void Disconnect(int owner)
    {
        RemoveOwner(owner);
        // A fresh authenticated peer may reuse a player slot, but never a global ID.
        m_highWater.erase(owner);
    }
private:
    static uint64_t Key(int owner, uint32_t token)
    { return (uint64_t(uint32_t(owner)) << 32) | token; }
    uint32_t m_nextId = 1;
    std::unordered_map<uint64_t, uint32_t> m_tokens;
    std::unordered_map<int, uint32_t> m_highWater;
};

// Primitive object setters may be replayed; queries/conditional Slide are
// observed on the host and never broadcast their return values or pool handles.
// Filled alongside the actual SDK opcode definitions in COpCodeSync.
inline bool IsCreate(uint16_t opcode) { return opcode == 0x0107 || opcode == 0x029b; }
inline int InputCount(uint16_t opcode)
{
    switch (opcode)
    {
    case 0x0107: case 0x029b: return 4;
    case 0x0108: case 0x01c4: case 0x0176: case 0x01bb: case 0x0366: return 1;
    case 0x0177: case 0x035d: case 0x0382: case 0x0392: case 0x0550:
    case 0x0566: case 0x071f: case 0x0723: case 0x0750: case 0x0875: case 0x08d2: return 2;
    case 0x01bc: case 0x0381: case 0x0453: return 4;
    case 0x034e: return 8;
    case 0x09ca: return 6;
    default: return 0;
    }
}
inline bool IsObjectOpcode(uint16_t opcode) { return InputCount(opcode) != 0; }
inline int ObjectOperand(uint16_t opcode) { return IsObjectOpcode(opcode) && !IsCreate(opcode) ? 0 : -1; }
inline bool IsSnapshotOnlyOpcode(uint16_t opcode)
{
    return IsCreate(opcode) || opcode == 0x0108 || opcode == 0x01c4
        || opcode == 0x0176 || opcode == 0x01bb || opcode == 0x0366
        || opcode == 0x034e || opcode == 0x0723;
}

// Reject malformed object opcode envelopes before any native parameter read.
inline bool ValidOpcode(const uint8_t* bytes, size_t size)
{
    if (!bytes || size < 4) return false;
    uint16_t op; std::memcpy(&op, bytes, sizeof op);
    auto count = InputCount(op);
    if (!count || IsSnapshotOnlyOpcode(op) || (bytes[2] & 15) != count
        || (bytes[2] >> 4) != 0 || size != 4 + size_t(count) * 4) return false;
    auto integer = [bytes](int i) { int32_t v; std::memcpy(&v, bytes + 4 + i * 4, 4); return v; };
    auto real = [bytes](int i) { float v; std::memcpy(&v, bytes + 4 + i * 4, 4); return v; };
    auto finite = [&real](int i, float bound) { float v = real(i); return std::isfinite(v) && std::fabs(v) <= bound; };
    switch (op)
    {
    case 0x01bc: return finite(1, 20000) && finite(2, 20000) && finite(3, 20000);
    case 0x0381: return finite(1, 200) && finite(2, 200) && finite(3, 200);
    // Native heading/rotation operands are degrees; snapshots use radians.
    case 0x0177: return finite(1, 36000);
    case 0x0453: return finite(1, 36000) && finite(2, 36000) && finite(3, 36000);
    case 0x08d2: return finite(1, 100) && real(1) > 0;
    case 0x071f: return integer(1) >= 0 && integer(1) <= 100000;
    case 0x0566: return integer(1) >= 0 && integer(1) <= 18;
    case 0x09ca:
        for (int i = 1; i < 6; ++i) if (integer(i) != 0 && integer(i) != 1) return false;
        return true;
    default: return integer(1) == 0 || integer(1) == 1;
    }
}
}
