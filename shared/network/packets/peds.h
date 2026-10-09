#pragma once
#include <cmath>
#include <type_traits>
#include "network/npc_sync.h"
#include <eModelID.h>
#include <ePedType.h>
#include <CPed.h>
#include <CVehicle.h>
#include <eGlobalSpeechContexts.h>

namespace Packets::Peds
{
class PedSpawn : public Packet
{
    DEFINE_PACKET_TYPE(PedSpawn, ePacketType::PED_SPAWN, ePacketChannel::EVENT);

public:
    int pedid = 0;
    uint8_t tempid = 255;
    eModelID modelId = MODEL_MALE01;
    ePedType pedType = PED_TYPE_CIVMALE;
    WorldPositionCompressed pos{};
    eCharCreatedBy createdBy = MISSION_CHAR;
    char specialModelName[8] = {'\0'};

    NPCSync::Stamp stamp{};
    uint32_t requestToken = 0;
    int ownerid = -1;
    bool Valid() const { return NPCSync::Position(pos) && modelId > MODEL_NULL && modelId <= MODEL_SPECIAL10 && pedType >= PED_TYPE_CIVMALE && pedType <= PED_TYPE_MISSION8 && createdBy >= UNUSED_CHAR && createdBy <= REPLAY_CHAR && ((stamp.Lifetime() && ownerid >= 0 && ownerid < Config::MAX_SERVER_PLAYERS) || (!stamp.generation && !stamp.epoch && requestToken > 0 && requestToken <= NPCSync::MaxCounter && ownerid == -1)); }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (!Stream::IsReading && !Valid()) return false;
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        serialize_uint8(stream, tempid);
        serialize_int(stream, (int&)modelId, MODEL_NULL, MODEL_SPECIAL10);
        serialize_int(stream, (int&)pedType, PED_TYPE_CIVMALE, PED_TYPE_MISSION8);
        serialize_object(stream, pos);
        serialize_int(stream, (int&)createdBy, UNUSED_CHAR, REPLAY_CHAR);
        serialize_string(stream, specialModelName, 8);
        specialModelName[ARRAY_SIZE(specialModelName) - 1] = '\0';
        serialize_object(stream, stamp);
        serialize_int(stream, requestToken, 0, int(NPCSync::MaxCounter));
        serialize_int(stream, ownerid, -1, Config::MAX_SERVER_PLAYERS - 1);
        return Valid();
    }
};

class PedConfirm : public Packet
{
    DEFINE_PACKET_TYPE(PedConfirm, ePacketType::PED_CONFIRM, ePacketChannel::EVENT);

public:
    uint8_t tempid = 255;
    int pedid = 0;

    NPCSync::Stamp stamp{};
    uint32_t requestToken = 0;
    int ownerid = -1;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_uint8(stream, tempid);
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        serialize_object(stream, stamp);
        serialize_int(stream, requestToken, 0, int(NPCSync::MaxCounter));
        serialize_int(stream, ownerid, -1, Config::MAX_SERVER_PLAYERS - 1);
        return stamp.Lifetime() && requestToken > 0 && tempid < 255 && ownerid >= 0;
    }
};

class PedRemove : public Packet
{
    DEFINE_PACKET_TYPE(PedRemove, ePacketType::PED_REMOVE, ePacketChannel::EVENT);

public:
    int pedid = 0;

    NPCSync::Stamp stamp{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        serialize_object(stream, stamp);
        return true;
    }
};

class AssignPedSyncer : public Packet
{
    DEFINE_PACKET_TYPE(AssignPedSyncer, ePacketType::ASSIGN_PED, ePacketChannel::EVENT);

public:
    int pedid;

    NPCSync::Stamp stamp{};
    int ownerid = -1;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        serialize_object(stream, stamp);
        serialize_int(stream, ownerid, 0, Config::MAX_SERVER_PLAYERS - 1);
        return true;
    }
};

class PedOnFoot : public Packet
{
    DEFINE_PACKET_TYPE(PedOnFoot, ePacketType::PED_ONFOOT, ePacketChannel::SYNC);

public:
    int pedid = 0;
    WorldPositionCompressed pos{};
    MoveSpeedCompressed velocity{};
    Packets::Players::SHealthSnapshot healthSnapshot{};
    Packets::Players::SWeaponSnapshot weaponSnapshot{};
    RadianAngleCompressed aimingRotation{};
    RadianAngleCompressed currentRotation{};
    RadianAngleCompressed lookDirection{};  // this one isnt needed i think
    eMoveState moveState = PEDMOVE_STILL;
    bool bDucked = false;
    bool bAiming = false;
    uint8_t fightingStyle = 4;
    WorldPositionCompressed weaponAim{};

    NPCSync::Stamp stamp{};
    uint8_t area = 0;
    bool Valid() const { return stamp.State() && NPCSync::Position(pos) && NPCSync::Velocity(velocity) && NPCSync::Finite(aimingRotation.m_angle, 100.0f) && NPCSync::Finite(currentRotation.m_angle, 100.0f) && NPCSync::Finite(lookDirection.m_angle, 100.0f) && moveState >= PEDMOVE_NONE && moveState <= PEDMOVE_SPRINT && fightingStyle >= 4 && fightingStyle <= 16 && (!bAiming || NPCSync::Position(weaponAim)); }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (!Stream::IsReading && !Valid()) return false;
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        serialize_object(stream, pos);
        serialize_object(stream, velocity);
        serialize_object(stream, healthSnapshot);
        serialize_object(stream, weaponSnapshot);
        serialize_object(stream, aimingRotation);
        serialize_object(stream, currentRotation);
        serialize_object(stream, lookDirection);
        serialize_int(stream, (int&)moveState, PEDMOVE_NONE, PEDMOVE_SPRINT);
        serialize_bool(stream, bDucked);
        serialize_bool(stream, bAiming);
        serialize_uint8(stream, fightingStyle);  // todo
        if (bAiming)
        {
            serialize_object(stream, weaponAim);
        }
        serialize_object(stream, stamp);
        serialize_uint8(stream, area);
        return Valid();
    }
};

class PedDriverUpdate : public Packet
{
    DEFINE_PACKET_TYPE(PedDriverUpdate, ePacketType::PED_DRIVER_UPDATE, ePacketChannel::SYNC);

public:
    int pedid = 0;
    int vehicleid = 0;
    eVehicleType vehicleSubType = VEHICLE_AUTOMOBILE;

    WorldPositionCompressed pos{};
    NormalizedVector rot{};
    NormalizedVector roll{};
    MoveSpeedCompressed velocity{};
    CVector turnSpeed{};

    uint8_t color1{};
    uint8_t color2{};
    int8_t paintjob{};

    Packets::Players::SHealthSnapshot pedHealth{};
    Packets::Players::SWeaponSnapshot pedWeapon{};

    float health{};

    eDoorLock locked = DOORLOCK_UNLOCKED;
    float gasPedal = 0.0f;
    float breakPedal = 0.0f;
    float steerAngle = 0.0f;

    // subType specific fields
    float bikeLean{};               // bike/bmx
    float controlPedaling{};        // bmx
    float planeGearState{};         // plane
    uint16_t miscComponentAngle{};  // automobile/mtruck/plane

    bool engineState{}, lightState{}, engineBroken{}, sirenOrAlarm{};
    uint16_t alarmState{}; // Includes the native 65535 armed-alarm sentinel.
    float dirtLevel{};

    NPCSync::Stamp stamp{};
    uint8_t area = 0;
    bool Valid() const { return stamp.State() && NPCSync::Position(pos) && NPCSync::Velocity(velocity) && NPCSync::VectorValid(rot, 1.0f) && NPCSync::VectorValid(roll, 1.0f) && NPCSync::VectorValid(turnSpeed, 100.0f) && NPCSync::Finite(health, 100000.0f) && NPCSync::Finite(gasPedal, 100.0f) && NPCSync::Finite(breakPedal, 100.0f) && NPCSync::Finite(steerAngle, 100.0f) && NPCSync::Finite(bikeLean, 100.0f) && NPCSync::Finite(controlPedaling, 100.0f) && NPCSync::Finite(planeGearState, 1.0f) && NPCSync::Finite(dirtLevel, 15.0f) && dirtLevel >= 0.0f; }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        // Retain the existing native sender dirt normalization. Received dirt
        // is compressed/bounded; the complete state still validates identity
        // and all raw float operands before relay or native application.
        if (Stream::IsWriting)
            dirtLevel = std::isfinite(dirtLevel) ? std::clamp(dirtLevel, 0.0f, 15.0f) : 0.0f;
        if (!Stream::IsReading && !Valid()) return false;
#pragma region IDs
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        serialize_int(stream, vehicleid, 0, Config::MAX_SERVER_VEHICLES - 1);
        serialize_int(stream, (int&)vehicleSubType, VEHICLE_AUTOMOBILE, VEHICLE_TRAILER);
#pragma endregion
#pragma region clamp stuff
        if (Stream::IsWriting)
        {
            turnSpeed.x = std::clamp(turnSpeed.x, -0.5f, 0.5f);
            turnSpeed.y = std::clamp(turnSpeed.y, -0.5f, 0.5f);
            turnSpeed.z = std::clamp(turnSpeed.z, -0.5f, 0.5f);
            health = std::clamp(health, 0.0f, 1000.0f);
            gasPedal = std::clamp(gasPedal, -1.0f, 1.0f);
            breakPedal = std::clamp(breakPedal, -1.0f, 1.0f);
            steerAngle = std::clamp(steerAngle, -1.0f, 1.0f);
            bikeLean = std::clamp(bikeLean, -1.0f, 1.0f);
            controlPedaling = std::clamp(controlPedaling, -5.0f, 5.0f);
        }
#pragma endregion
#pragma region matrix speeds
        serialize_object(stream, pos);
        serialize_object(stream, rot);
        serialize_object(stream, roll);
        serialize_object(stream, velocity);

        bool sendTurnSpeed = false;
        if (Stream::IsWriting)
        {
            if (std::abs(turnSpeed.x) >= 0.0001f || std::abs(turnSpeed.y) >= 0.0001f ||
                std::abs(turnSpeed.z) >= 0.0001f)
            {
                sendTurnSpeed = true;
            }
        }
        serialize_bool(stream, sendTurnSpeed);
        if (sendTurnSpeed)
        {
            serialize_compressed_float(stream, turnSpeed.x, -0.5f, 0.5f, 0.0001f);
            serialize_compressed_float(stream, turnSpeed.y, -0.5f, 0.5f, 0.0001f);
            serialize_compressed_float(stream, turnSpeed.z, -0.5f, 0.5f, 0.0001f);
        }

#pragma endregion
#pragma region painjob
        serialize_uint8(stream, color1);
        serialize_uint8(stream, color2);
        if (Stream::IsWriting)
        {
            if (paintjob < -1 || paintjob > 2)  // got limits here https://wiki.multitheftauto.com/wiki/Paintjob
            {
                paintjob = -1;
            }
        }
        serialize_int(stream, paintjob, -1, 2);

#pragma endregion
#pragma region ped health
        {
            serialize_object(stream, pedHealth);
            serialize_object(stream, pedWeapon);
        }
#pragma endregion
#pragma region health doors
        serialize_compressed_float(stream, health, 0.0f, 1000.0f, 1.0f);

        serialize_int(stream, (int&)locked, DOORLOCK_NOT_USED, DOORLOCK_SKIP_SHUT_DOORS);
#pragma endregion
#pragma region pedals
        {
            serialize_compressed_float(stream, gasPedal, -1.0f, 1.0f, 0.01f);
            serialize_compressed_float(stream, breakPedal, -1.0f, 1.0f, 0.01f);
            serialize_compressed_float(stream, steerAngle, -1.0f, 1.0f, 0.01f);
        }
#pragma endregion
#pragma region type specific

        if (vehicleSubType == VEHICLE_BIKE || vehicleSubType == VEHICLE_BMX)
        {
            serialize_compressed_float(stream, bikeLean, -1.0f, 1.0f, 0.01f);
        }
        if (vehicleSubType == VEHICLE_BMX)
        {
            serialize_compressed_float(stream, controlPedaling, -5.0f, 5.0f, 0.01f);
        }
        if (vehicleSubType == VEHICLE_PLANE)
        {
            if (Stream::IsWriting)
            {
                bool temp = planeGearState > 0.0f;
                serialize_bool(stream, temp);
            }
            else if (Stream::IsReading)
            {
                bool temp;
                serialize_bool(stream, temp);
                planeGearState = temp ? 1.0f : 0.0f;
            }
        }
        if (vehicleSubType == VEHICLE_AUTOMOBILE || vehicleSubType == VEHICLE_MTRUCK || vehicleSubType == VEHICLE_PLANE)
        {
            bool syncAngle = false;
            if (Stream::IsWriting && miscComponentAngle != 0)
            {
                syncAngle = true;
            }
            serialize_bool(stream, syncAngle);
            if (syncAngle)
            {
                serialize_uint16(stream, miscComponentAngle);
            }
        }
#pragma endregion

        // Append to this NPC driver stream, preserving its existing prefix.
        serialize_bool(stream, engineState);
        serialize_bool(stream, lightState);
        serialize_bool(stream, engineBroken);
        serialize_bool(stream, sirenOrAlarm);
        serialize_uint16(stream, alarmState);
        serialize_compressed_float(stream, dirtLevel, 0.0f, 15.0f, 1.0f);
        serialize_object(stream, stamp);
        serialize_uint8(stream, area);
        return Valid();
    }
};

class PedPassengerSync : public Packet
{
    DEFINE_PACKET_TYPE(PedPassengerSync, ePacketType::PED_PASSENGER_UPDATE, ePacketChannel::SYNC);

public:
    int pedid{};
    int vehicleid{};

    Packets::Players::SHealthSnapshot healthSnapshot{};
    Packets::Players::SWeaponSnapshot weaponSnapshot{};

    int8_t seatid = 0;

    NPCSync::Stamp stamp{};
    uint8_t area = 0;
    bool Valid() const { return stamp.State() && seatid >= 0 && seatid <= 7; }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (!Stream::IsReading && !Valid()) return false;
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        serialize_int(stream, vehicleid, 0, Config::MAX_SERVER_VEHICLES - 1);
        serialize_object(stream, healthSnapshot);
        serialize_object(stream, weaponSnapshot);
        serialize_int(stream, seatid, -1, 7);  // TODO test properly TODO(v0.3.1-alpha): limits
        serialize_object(stream, stamp);
        serialize_uint8(stream, area);
        return Valid();
    }
};

class PedShotSync : public Packet
{
    DEFINE_PACKET_TYPE(PedShotSync, ePacketType::PED_SHOT_SYNC, ePacketChannel::EVENT);

public:
    int pedid{};
    eWeaponType weaponType = WEAPON_UNARMED;
    WorldPositionCompressed origin{};
    WorldPositionCompressed effect{};
    WorldPositionCompressed target{};

    NPCSync::Stamp stamp{};
    bool Valid() const { return stamp.Lifetime() && NPCSync::Position(origin) && NPCSync::Position(effect) && NPCSync::Position(target); }

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        if (!Stream::IsReading && !Valid()) return false;
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        serialize_int(stream, (int&)weaponType, WEAPON_UNARMED, WEAPON_FLARE);
        serialize_object(stream, origin);
        serialize_object(stream, effect);
        serialize_object(stream, target);
        serialize_object(stream, stamp);
        return Valid();
    }
};

class PedSay : public Packet
{
    DEFINE_PACKET_TYPE(PedSay, ePacketType::PED_SAY, ePacketChannel::EVENT);

public:
    CNetworkEntitySerializer entity{};
    eGlobalSpeechContexts phraseId = CONTEXT_GLOBAL_NO_SPEECH;
    uint32_t startTimeDelay = 0;
    bool overrideSilence = false;
    bool isForceAudible = false;
    bool isFrontEnd = false;

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_object(stream, entity);
        if (entity.entityType != NETWORK_ENTITY_TYPE_PLAYER && entity.entityType != NETWORK_ENTITY_TYPE_PED)
        {
            return false;
        }
        serialize_int(stream, (int&)phraseId, CONTEXT_GLOBAL_NO_SPEECH + 1, CONTEXT_GLOBAL_END - 1);
        serialize_uint32(stream, startTimeDelay);
        serialize_bool(stream, overrideSilence);
        serialize_bool(stream, isForceAudible);
        serialize_bool(stream, isFrontEnd);
        return true;
    }
};

class PedClaimOnRelease : public Packet
{
    DEFINE_PACKET_TYPE(PedClaimOnRelease, ePacketType::PED_CLAIM_ON_RELEASE, ePacketChannel::EVENT);

public:
    int pedid = 0;

    NPCSync::Stamp stamp{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        serialize_object(stream, stamp);
        return true;
    }
};

class PedCancelClaim : public Packet
{
    DEFINE_PACKET_TYPE(PedCancelClaim, ePacketType::PED_CANCEL_CLAIM, ePacketChannel::EVENT);

public:
    int pedid = 0;

    NPCSync::Stamp stamp{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        serialize_object(stream, stamp);
        return true;
    }
};

class PedResetAllClaims : public Packet
{
    DEFINE_PACKET_TYPE(PedResetAllClaims, ePacketType::PED_RESET_ALL_CLAIMS, ePacketChannel::EVENT);

public:
    int pedid = 0;

    NPCSync::Stamp stamp{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        serialize_object(stream, stamp);
        return true;
    }
};

class PedTakeHost : public Packet
{
    DEFINE_PACKET_TYPE(PedTakeHost, ePacketType::PED_TAKE_HOST, ePacketChannel::EVENT);

public:
    int pedid = 0;
    bool allowReturnToPreviousHost = false;

    NPCSync::Stamp stamp{};

private:
    template <typename Stream>
    bool Serialize(Stream& stream)
    {
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        serialize_bool(stream, allowReturnToPreviousHost);
        serialize_object(stream, stamp);
        return true;
    }
};

class PedPin : public Packet
{
    DEFINE_PACKET_TYPE(PedPin, ePacketType::PED_PIN, ePacketChannel::EVENT);
public:
    int pedid = 0;
    NPCSync::Stamp stamp{};
    bool pinned = false;
    uint32_t requestToken = 0;
private:
    template<class Stream> bool Serialize(Stream& stream) {
        serialize_int(stream, pedid, 0, Config::MAX_SERVER_PEDS - 1);
        serialize_object(stream, stamp);
        serialize_bool(stream, pinned);
        serialize_int(stream, requestToken, 0, int(NPCSync::MaxCounter));
        return (stamp.Lifetime() && !requestToken) ||
            (!stamp.generation && !stamp.epoch && !stamp.sequence && requestToken > 0);
    }
};

class PedReplay : public Packet
{
    DEFINE_PACKET_TYPE(PedReplay, ePacketType::PED_REPLAY, ePacketChannel::EVENT);
public:
    uint8_t mode = 1; // on-foot, driver, passenger; one cached authoritative state.
    PedOnFoot onFoot{};
    PedDriverUpdate driver{};
    PedPassengerSync passenger{};
    Packet& StatePacket() { return mode == 1 ? static_cast<Packet&>(onFoot) : mode == 2 ? static_cast<Packet&>(driver) : static_cast<Packet&>(passenger); }
private:
    template<class Stream> bool Serialize(Stream& stream) {
        serialize_int(stream, mode, 1, 3);
        auto& state = StatePacket();
        if constexpr (std::is_same_v<Stream, serialize::ReadStream>) return state.SerializeRead(stream);
        else if constexpr (std::is_same_v<Stream, serialize::WriteStream>) return state.SerializeWrite(stream);
        else return state.SerializeMeasure(stream);
    }
};

}  // namespace Packets::Peds
