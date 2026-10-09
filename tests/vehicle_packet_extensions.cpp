#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include "vehicle_packet_extensions_extracted.h"

static unsigned checks = 0, failures = 0, cases = 0, truncations = 0;
static void expect(bool result, const char* description)
{
    ++checks;
    if (!result) { ++failures; std::cout << "FAIL: " << description << '\n'; }
}

struct DirtCase { float input, normalized, decoded; };

template <typename Extension>
static void exercise(const char* name)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const DirtCase dirtCases[] = {
        {0.0f, 0.0f, 0.0f}, {15.0f, 15.0f, 15.0f},
        {-1.0f, 0.0f, 0.0f}, {16.0f, 15.0f, 15.0f},
        {-std::numeric_limits<float>::max(), 0.0f, 0.0f},
        {std::numeric_limits<float>::max(), 15.0f, 15.0f},
        {0.49f, 0.49f, 0.0f}, {0.5f, 0.5f, 1.0f}, {0.51f, 0.51f, 1.0f},
        {7.49f, 7.49f, 7.0f}, {7.5f, 7.5f, 8.0f}, {7.51f, 7.51f, 8.0f},
        {14.49f, 14.49f, 14.0f}, {14.5f, 14.5f, 15.0f},
        {std::nextafter(15.0f, 0.0f), std::nextafter(15.0f, 0.0f), 15.0f},
        {std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::denorm_min(), 0.0f},
        {nan, 0.0f, 0.0f}, {inf, 0.0f, 0.0f}, {-inf, 0.0f, 0.0f}
    };
    const uint16_t alarms[] = {0, 65535};
    Extension defaults{};
    expect(!defaults.engineState && !defaults.lightState && !defaults.engineBroken &&
        !defaults.sirenOrAlarm && defaults.alarmState == 0 && defaults.dirtLevel == 0.0f,
        "Production extension declarations initialize every value.");

    for (unsigned flags = 0; flags < 16; ++flags)
        for (const auto alarm : alarms)
            for (const auto& dirt : dirtCases)
                for (int offset = 0; offset < 8; ++offset)
                {
                    ++cases;
                    Extension input{};
                    input.engineState = (flags & 1) != 0;
                    input.lightState = (flags & 2) != 0;
                    input.engineBroken = (flags & 4) != 0;
                    input.sirenOrAlarm = (flags & 8) != 0;
                    input.alarmState = alarm;
                    input.dirtLevel = dirt.input;

                    // The prefix is envelope padding, not a second implementation of the codec.
                    const uint32_t prefix = offset ? ((1u << offset) - 1u) : 0u;
                    auto measured = input;
                    serialize::MeasureStream measure;
                    if (offset) expect(measure.SerializeBits(prefix, offset), "Measure prefix succeeds.");
                    expect(measured.Serialize(measure), "Actual extracted serializer measures successfully.");
                    expect(std::isfinite(measured.dirtLevel) && measured.dirtLevel == dirt.normalized,
                        "Measurement executes the production finite-value normalization.");

                    alignas(4) std::array<uint8_t, 32> storage{};
                    auto written = input;
                    serialize::WriteStream writer(storage.data(), int(storage.size()));
                    if (offset) expect(writer.SerializeBits(prefix, offset), "Write prefix succeeds.");
                    expect(written.Serialize(writer), "Actual extracted serializer writes successfully.");
                    const int bits = writer.GetBitsProcessed();
                    writer.Flush();
                    const int bytes = writer.GetBytesProcessed();
                    expect(bits == measure.GetBitsProcessed(), "Measured and written bit counts agree.");
                    expect(bytes == measure.GetBytesProcessed(), "Measured and written byte counts agree.");
                    expect(bits == offset + 24, "Four flags, alarm and dirt occupy exactly 24 bits.");
                    expect(std::isfinite(written.dirtLevel) && written.dirtLevel == dirt.normalized,
                        "NaN/infinity normalize to zero and finite dirt is clamped before quantization.");

                    Extension decoded{};
                    serialize::ReadStream reader(storage.data(), bytes);
                    uint32_t decodedPrefix = 0;
                    if (offset) expect(reader.SerializeBits(decodedPrefix, offset) && decodedPrefix == prefix,
                        "Envelope prefix survives serialization.");
                    expect(decoded.Serialize(reader), "Actual extracted serializer reads successfully.");
                    expect(reader.GetBitsProcessed() == bits, "Reader consumes exactly the measured payload.");
                    expect(decoded.engineState == input.engineState && decoded.lightState == input.lightState &&
                        decoded.engineBroken == input.engineBroken && decoded.sirenOrAlarm == input.sirenOrAlarm,
                        "Every boolean combination round-trips without changing another flag.");
                    expect(decoded.alarmState == alarm, "Alarm zero and the 65,535 sentinel round-trip exactly.");
                    expect(std::isfinite(decoded.dirtLevel) && decoded.dirtLevel == dirt.decoded &&
                        decoded.dirtLevel >= 0.0f && decoded.dirtLevel <= 15.0f,
                        "Dirt decoding matches the independently specified quantization boundaries.");

                    // Backing storage remains padded to a dword, as ReadStream requires;
                    // only the advertised length is truncated. No out-of-bounds buffer is used.
                    for (int length = 0; length < bytes; ++length)
                    {
                        ++truncations;
                        Extension cut{};
                        serialize::ReadStream shortened(storage.data(), length);
                        uint32_t ignored = 0;
                        const bool prefixRead = !offset || shortened.SerializeBits(ignored, offset);
                        const bool accepted = prefixRead && cut.Serialize(shortened);
                        expect(!accepted, "Every shorter byte length rejects an incomplete extension.");
                    }
                }
    std::cout << "COVERAGE: " << name << " extension, all 16 flag combinations, alarms 0/65,535, "
                 "19 dirt inputs and all eight bit offsets.\n";
}

int main()
{
    std::cout << "SOURCE: " << kVehicleHeaderSha256 << '\n';
    std::cout << "SERIALIZER: " << kSerializeHeaderSha256 << '\n';
    exercise<VehicleIdleExtension>("VehicleIdleUpdate");
    exercise<VehicleDriverExtension>("VehicleDriverUpdate");
    std::cout << "RESULT: " << cases << " round-trip cases, " << truncations << " truncated reads, "
              << checks << " assertions, " << failures << " failures.\n";
    std::cout << "SCOPE: Exact extracted extension code and real serialize.h; no native vehicle, "
                 "full packet prefix, ENet ordering or authority timing is proved.\n";
    return failures ? 1 : 0;
}
