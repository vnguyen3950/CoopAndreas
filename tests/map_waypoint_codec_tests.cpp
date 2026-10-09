#include "map_sync_doubles.h"
#include <fstream>
static RegistrationStub registration;
RegistrationStub& GetPacketFactory() { return registration; }
static unsigned assertions = 0, failures = 0;
void expect(bool condition, const char* text) { ++assertions; if (!condition) { ++failures; std::cout << "FAIL: " << text << '\n'; } }
using Waypoint = Packets::Players::PlayerPlaceWaypoint;
void write(const char* name, bool place)
{
    Waypoint p; p.playerid.value = 1; p.sequence = place ? 8 : 9;
    p.generation = Config::IsServer ? 11 : 0; p.place = place; p.position = CVector2D(-3000,3000);
    std::array<uint32_t,32> words{}; auto* bytes = reinterpret_cast<uint8_t*>(words.data());
    serialize::MeasureStream measure; expect(static_cast<Packet&>(p).SerializeMeasure(measure), "Actual waypoint measures.");
    serialize::WriteStream writer(bytes,sizeof(words)); expect(static_cast<Packet&>(p).SerializeWrite(writer), "Actual waypoint writes."); writer.Flush();
    expect(writer.GetBytesProcessed() == measure.GetBytesProcessed(), "Waypoint measured length matches write.");
    std::ofstream file(name,std::ios::binary); file.write(reinterpret_cast<char*>(bytes),writer.GetBytesProcessed());
    expect(static_cast<Packet&>(p).GetChannel() == ePacketChannel::SYSTEM, "Waypoint ordered with connection identities.");
}
void read(const char* name, bool place)
{
    std::ifstream file(name,std::ios::binary); std::vector<char> data((std::istreambuf_iterator<char>(file)),{});
    expect(!data.empty(), "Opposite-role wire fixture exists.");
    std::array<uint32_t,32> words{}; std::memcpy(words.data(),data.data(),data.size());
    auto* bytes = reinterpret_cast<uint8_t*>(words.data());
    Waypoint p; serialize::ReadStream reader(bytes,int(data.size()));
    expect(static_cast<Packet&>(p).SerializeRead(reader), "Actual opposite-role waypoint decodes.");
    expect(p.place == place && p.sequence == (place ? 8u : 9u) && p.generation == (Config::IsClient ? 11u : 0u), "Wire state, sequence and generation preserved.");
    expect(p.playerid.value == (Config::IsClient ? 1 : 0), "SenderPlayerId is present S2C and omitted C2S.");
    expect(!place || (p.position.x == -3000 && p.position.y == 3000), "Finite coordinate boundaries preserved.");
    for (int length = 0; length < int(data.size()); ++length)
    { Waypoint cut; serialize::ReadStream shortReader(bytes,length); expect(!static_cast<Packet&>(cut).SerializeRead(shortReader), "Every shorter waypoint payload rejected."); }
}
int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    if (std::string(argv[1]) == "write") { write(Config::IsClient ? "c2s-place.bin" : "s2c-place.bin",true); write(Config::IsClient ? "c2s-clear.bin" : "s2c-clear.bin",false); }
    else { read(Config::IsClient ? "s2c-place.bin" : "c2s-place.bin",true); read(Config::IsClient ? "s2c-clear.bin" : "c2s-clear.bin",false); }
    for (float bad : {NAN, INFINITY, -3001.0f, 3001.0f})
    { Waypoint p; p.sequence = 1; p.place = true; p.position.x = bad; serialize::MeasureStream m; expect(!static_cast<Packet&>(p).SerializeMeasure(m), "Invalid waypoint cannot be serialized."); }
    std::cout << "waypoint " << (Config::IsClient ? "client" : "server") << ": " << assertions << " assertions, " << failures << " failures\n";
    return failures ? 1 : 0;
}
