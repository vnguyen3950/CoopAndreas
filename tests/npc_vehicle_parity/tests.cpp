#include "doubles.h"
#include "extracted_handlers.h"
static unsigned checks = 0, failures = 0, cases = 0;
void expect(bool good, const char* message)
{ ++checks; if (!good) { ++failures; std::cout << "FAIL: " << message << '\n'; } }
template<class T, class = void> struct HasState : std::false_type {};
template<class T> struct HasState<T, std::void_t<decltype(T{}.engineState), decltype(T{}.lightState),
    decltype(T{}.engineBroken), decltype(T{}.sirenOrAlarm), decltype(T{}.alarmState), decltype(T{}.dirtLevel)>> : std::true_type {};
struct Wire { std::array<uint32_t, 128> words{}; int bytes = 0, bits = 0; uint8_t* data() { return reinterpret_cast<uint8_t*>(words.data()); } };
template<class T> Wire encode(T input, int offset = 0)
{
    Wire wire;
    serialize::MeasureStream measure;
    serialize::WriteStream stream(wire.data(), int(wire.words.size() * 4));
    if (offset) { measure.SerializeBits(0u, offset); stream.SerializeBits(0u, offset); }
    expect(static_cast<Packet&>(input).SerializeMeasure(measure), "Production packet measures successfully.");
    expect(static_cast<Packet&>(input).SerializeWrite(stream), "Production packet writes successfully.");
    wire.bits = stream.GetBitsProcessed(); stream.Flush(); wire.bytes = stream.GetBytesProcessed();
    expect(measure.GetBitsProcessed() >= wire.bits, "Production measurement covers the entire payload.");
    return wire;
}
template<class T> bool decode(Wire& wire, T& output, int offset = 0, int length = -1)
{
    serialize::ReadStream stream(wire.data(), length < 0 ? wire.bytes : length);
    uint32_t prefix = 0;
    return (!offset || stream.SerializeBits(prefix, offset)) && static_cast<Packet&>(output).SerializeRead(stream);
}
template<class T> void codec()
{
    if constexpr (!HasState<T>::value) expect(false, "NPC driver packet must carry state parity fields.");
    else
    {
        T defaults;
        defaults.stamp = {1,1,1};
        expect(!defaults.engineState && !defaults.lightState && !defaults.engineBroken && !defaults.sirenOrAlarm
            && defaults.alarmState == 0 && defaults.dirtLevel == 0, "New fields have deterministic defaults.");
        expect(static_cast<Packet&>(defaults).GetType() == ePacketType::PED_DRIVER_UPDATE
            && static_cast<Packet&>(defaults).GetChannel() == ePacketChannel::SYNC,
            "NPC state extends the existing unreliable driver stream.");
        expect(GetChannelReliability(static_cast<Packet&>(defaults).GetChannel()) == ePacketReliability::UNRELIABLE,
            "NPC parity retains the existing stream reliability contract.");
        const float dirt[] = {0,15,-1,16,7.49f,7.5f,std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()};
        for (int subtype = VEHICLE_AUTOMOBILE; subtype <= VEHICLE_TRAILER; ++subtype)
            for (unsigned flags = 0; flags < 16; ++flags)
                for (uint16_t alarm : {uint16_t(0),uint16_t(1000),uint16_t(65535)})
                    for (float value : dirt) for (int offset : {0,7})
                    {
                        ++cases;
                        T input; input.pedid = 7; input.vehicleid = 9; input.vehicleSubType = eVehicleType(subtype);
                        input.stamp = {1,1,1};
                        input.engineState = flags & 1; input.lightState = flags & 2;
                        input.engineBroken = flags & 4; input.sirenOrAlarm = flags & 8;
                        input.alarmState = alarm; input.dirtLevel = value;
                        auto wire = encode(input, offset); T output;
                        expect(decode(wire, output, offset), "Full NPC packet round-trips through real serialize.h.");
                        expect(output.engineState == input.engineState && output.lightState == input.lightState
                            && output.engineBroken == input.engineBroken && output.sirenOrAlarm == input.sirenOrAlarm,
                            "Independent state bits survive all boolean combinations.");
                        expect(output.alarmState == alarm, "Alarm countdown and 65535 sentinel survive unchanged.");
                        float normalized = std::isfinite(value) ? std::clamp(value, 0.0f, 15.0f) : 0;
                        expect(std::isfinite(output.dirtLevel) && output.dirtLevel == std::floor(normalized + 0.5f),
                            "Dirt is finite, bounded, and quantized at the established precision.");
                        for (int length = 0; length < wire.bytes; ++length)
                        { T truncated; expect(!decode(wire, truncated, offset, length), "Every truncated byte payload is rejected."); }
                        Packets::Peds::LegacyPedDriverUpdate legacy;
                        legacy.pedid = input.pedid; legacy.vehicleid = input.vehicleid; legacy.vehicleSubType = input.vehicleSubType;
                        auto old = encode(legacy, offset);
                        expect(wire.bits == old.bits + 125, "Parity plus three 31-bit lifetime counters and area add exactly 125 bits.");
                        bool same = true;
                        for (int bit = 0; bit < old.bits; ++bit)
                            same &= ((wire.data()[bit/8] >> (bit%8)) & 1) == ((old.data()[bit/8] >> (bit%8)) & 1);
                        expect(same, "The prior NPC packet prefix is byte/bit compatible before the extension.");
                    }
        // Invalid IDs/subtype are read from deliberately malformed bytes, not
        // passed to the serializer's writer assertions.
        T valid; valid.stamp = {1,1,1}; auto wire = encode(valid);
        for (auto bad : {std::pair<int,unsigned>{0,255}, {8,255}, {16,15}})
        {
            auto malformed = wire;
            int width = bad.first == 16 ? 4 : 8;
            for (int i = 0; i < width; ++i)
            { int bit=bad.first+i; malformed.data()[bit/8] |= uint8_t(((bad.second>>i)&1)<<(bit%8)); }
            T output; expect(!decode(malformed, output), "Malformed IDs and subtype ranges reject before native use.");
        }
    }
}
void authority()
{
    CNetworkPlayer owner{1}, other{2}, recorded{3};
    CNetworkPed ped; CNetworkVehicle vehicle;
    ped.m_pSyncer = &owner; vehicle.m_pSyncer = &other;
    CNetworkPedManager::ped = &ped; CNetworkVehicleManager::vehicle = &vehicle;
    Packets::Peds::PedDriverUpdate packet; packet.pedid = 7; packet.vehicleid = 9; packet.pos.x = 20;
    packet.stamp = {1,1,1};
    packet.pedHealth.iHealth = 100; // Vehicle parity uses an explicitly living driver.
    GetPacketFactory().forwarded = 0;
    ServerDriver(&packet, &other);
    expect(!GetPacketFactory().forwarded && !vehicle.m_bUsedByPed, "Wrong ped owner cannot mutate or forward NPC vehicle state.");
    ServerDriver(&packet, &owner);
    expect(GetPacketFactory().forwarded == 1 && vehicle.m_bUsedByPed && vehicle.m_vecPosition.x == 20,
        "Ped owner may update a vehicle whose idle syncer is another peer.");
    vehicle.m_pPlayers[0] = &recorded; vehicle.m_vecPosition.x = 99; vehicle.m_bUsedByPed = false;
    ServerDriver(&packet, &owner);
    expect(GetPacketFactory().forwarded == 1 && vehicle.m_vecPosition.x == 99 && !vehicle.m_bUsedByPed,
        "Delayed NPC state cannot take over a driver slot reserved by reliable player entry.");
    vehicle.m_pPlayers[0] = &owner;
    ServerDriver(&packet, &owner);
    expect(GetPacketFactory().forwarded == 1, "Even the NPC owner cannot overwrite its own recorded player driver.");
    vehicle.m_pPlayers[0] = nullptr;
    packet.stamp.sequence = 2;
    ServerDriver(&packet, &owner);
    expect(GetPacketFactory().forwarded == 2, "NPC stream becomes eligible after the recorded player exits.");
    ServerDriver(&packet, nullptr);
    expect(GetPacketFactory().forwarded == 2, "Unauthenticated/null sender cannot update NPC state.");
    packet.pedid = 8; ServerDriver(&packet, &owner); packet.pedid = 7;
    packet.vehicleid = 10; ServerDriver(&packet, &owner);
    expect(GetPacketFactory().forwarded == 2, "Missing ped or vehicle mapping cannot mutate authority.");
}
template<class T> void native_pipeline()
{
    CPed actor, player; player.player = true;
    CAutomobile car; CNetworkPed ped; CNetworkVehicle vehicle;
    ped.m_pPed = &actor; vehicle.m_pVehicle = &car;
    CNetworkPedManager::ped = &ped; CNetworkVehicleManager::vehicle = &vehicle;
    T packet; packet.pedid = 7; packet.vehicleid = 9; packet.pos.x = 22;
    packet.stamp = {1,1,1};
    car.m_matrix->pos.x = 99; ped.m_bSyncing = true;
    packet.pedHealth.iHealth = 100;
    ClientDriver(&packet);
    expect(car.m_matrix->pos.x == 99 && ped.warps == 0, "Queued remote NPC state cannot overwrite a locally owned ped.");
    ped.m_bSyncing = false; car.m_pDriver = &player;
    ClientDriver(&packet);
    expect(car.m_matrix->pos.x == 99 && ped.warps == 0, "Queued NPC state cannot displace a native player driver.");
    car.m_pDriver = nullptr; vehicle.m_bSyncing = true;
    if constexpr (HasState<T>::value)
    {
        packet.engineState = true; packet.lightState = true; packet.engineBroken = true;
        packet.sirenOrAlarm = true; packet.alarmState = 65535; packet.dirtLevel = 12;
    }
    ClientDriver(&packet);
    expect(car.m_matrix->pos.x == 22 && ped.warps == 1, "Remote NPC state may reach this peer's idle-owned vehicle.");
    if constexpr (HasState<T>::value)
    {
        expect(car.m_nVehicleFlags.bEngineOn && car.m_nVehicleFlags.bLightsOn && car.m_nVehicleFlags.bEngineBroken
            && car.m_nVehicleFlags.bSirenOrAlarm && car.m_nAlarmState == 65535 && car.m_fDirtLevel == 12,
            "Actual receiver applies all captured state to native vehicle fields.");
        for (unsigned flags = 0; flags < 16; ++flags)
        {
            ++packet.stamp.sequence;
            packet.engineState = flags & 1; packet.lightState = flags & 2;
            packet.engineBroken = flags & 4; packet.sirenOrAlarm = flags & 8;
            packet.alarmState = flags ? 65535 : 0; packet.dirtLevel = flags ? 15 : 0;
            ClientDriver(&packet);
            expect(car.m_nVehicleFlags.bEngineOn == bool(flags & 1) && car.m_nVehicleFlags.bLightsOn == bool(flags & 2)
                && car.m_nVehicleFlags.bEngineBroken == bool(flags & 4) && car.m_nVehicleFlags.bSirenOrAlarm == bool(flags & 8),
                "Native receiver preserves independent flags including transitions back off.");
            expect(car.m_nAlarmState == packet.alarmState && car.m_fDirtLevel == packet.dirtLevel,
                "Native receiver clears alarm/dirt as well as applying active values.");
        }
    }
    car.m_matrix = nullptr; unsigned before = ped.warps;
    ClientDriver(&packet);
    expect(ped.warps == before, "A vehicle without a native matrix cannot be warped or overwritten.");
    car.m_matrix = &car.storage;
    car.m_nVehicleFlags = {true,false,true,true}; car.m_nAlarmState = 1000; car.m_fDirtLevel = 9;
    ped.m_bSyncing = true;
    CaptureDriver(&ped, &actor, &car, &vehicle);
    auto* captured = dynamic_cast<T*>(GetPacketFactory().sent.get());
    expect(captured != nullptr, "Actual NPC driver branch sends the existing packet type.");
    if constexpr (HasState<T>::value)
        expect(captured && captured->engineState && !captured->lightState && captured->engineBroken
            && captured->sirenOrAlarm && captured->alarmState == 1000 && captured->dirtLevel == 9,
            "Actual sender captures engine, lights, damage, siren/alarm and dirt rather than fabricated values.");
}
int main()
{
    codec<Packets::Peds::PedDriverUpdate>();
    authority(); native_pipeline<Packets::Peds::PedDriverUpdate>();
    std::cout << "RESULT: " << cases << " complete packet cases, " << checks << " assertions, " << failures << " failures.\n";
    std::cout << "SCOPE: Real serializers and unchanged extracted handler/sender bodies with native/transport doubles; no game/audio/ENet runtime.\n";
    return failures ? 1 : 0;
}
