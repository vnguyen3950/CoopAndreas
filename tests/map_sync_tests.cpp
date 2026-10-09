#include "map_sync_doubles.h"
static Factory factory;
RegistrationStub& GetPacketFactory() { return factory; }
// Service sends use the real factory interface with a recorded transport double.
#define GetPacketFactory() factory
#include "map_client.inc"
#include "map_server.inc"
#undef GetPacketFactory
static unsigned assertions = 0, failures = 0;
void expect(bool value, const char* text) { ++assertions; if (!value) { ++failures; std::cout << "FAIL: " << text << '\n'; } }
void setup(CNetworkPlayer& host, CNetworkPlayer& guest)
{
    static ENetPeer peers[2]; static Ped ped; static Pool pool;
    host.m_iPlayerId = 0; host.m_pPeer = &peers[0]; host.m_vitals.Bind(0, 10);
    guest.m_iPlayerId = 1; guest.m_pPeer = &peers[1]; guest.m_vitals.Bind(1, 11);
    CNetworkPlayerManager::m_pPlayers = {&host, &guest}; CNetworkPlayerManager::host = &host;
    CWorld::Players[0].m_pPed = &ped; CPools::ms_pPedPool = &pool; CMapSync::Reset(); CMapSync::Init();
    Events::processScriptsEvent.after.Fire();
}
void contracts()
{
    for (int i = 0; i < 100; ++i) { MapSync::Discovery d; expect(d.Add(i) && d.Has(i) && d.Count() == 1, "Each native discovery cell has a unique bit."); expect(!d.Add(i), "Repeated cell is idempotent."); }
    MapSync::Discovery d; expect(!d.Add(-1) && !d.Add(100), "Out-of-range cells rejected.");
    d.bits.back() = 0xf0; expect(!d.Valid(), "Reserved discovery bits rejected.");
    expect(MapSync::Cell(-3000,-3000) == 9 && MapSync::Cell(3000,3000) == 90, "Native clamped edges and inverted Y agree.");
    expect(MapSync::Cell(0,0) == 54, "Native column-major zone index agrees.");
    expect(MapSync::Cell(NAN,0) == -1 && MapSync::Cell(0,INFINITY) == -1, "Nonfinite world positions rejected.");
    MapSync::Room r; MapSync::Discovery seed; seed.Add(3);
    expect(r.Seed(0,seed) && r.epoch == 1 && r.revision == 1, "First host seed establishes campaign.");
    auto bad = seed; bad.bits.back() = 255;
    expect(!r.Seed(1,bad) && r.epoch == 1, "Invalid reset is transactional.");
    MapSync::Discovery cell; cell.Add(7);
    expect(r.Reveal(1,cell) && r.discovery.Has(3) && r.discovery.Has(7), "Guest discovery joins host map.");
    expect(!r.Reveal(1,cell) && r.revision == 2, "Duplicate does not advance revision.");
    expect(!r.Reveal(0,cell), "Old campaign rejected.");
    auto before = r.discovery; r.revision = MapSync::MAX_COUNTER; MapSync::Discovery next; next.Add(8);
    expect(!r.Reveal(1,next) && r.discovery == before, "Revision exhaustion cannot mutate discovery.");
    r.Clear(); expect(r.Seed(0,seed) && r.epoch == 2, "Empty room retains epoch allocator.");
    MapSync::View v; expect(v.Accept(2,1,seed) && !v.Accept(1,50,cell) && !v.Accept(2,1,cell), "Snapshot ordering rejects stale epochs and revisions.");
    expect(MapSync::AcceptWaypoint(9,4,9,5,true,3000,-3000), "Bound waypoint accepted.");
    expect(!MapSync::AcceptWaypoint(10,4,9,5,true,0,0), "Recycled slot rejects old waypoint generation.");
    expect(!MapSync::AcceptWaypoint(9,5,9,5,true,0,0), "Duplicate waypoint rejected.");
    expect(!MapSync::ValidWaypoint(true,NAN,0) && !MapSync::ValidWaypoint(true,0,3001), "Waypoint rejects NaN and range overflow.");
}
void codecs()
{
    for (int mode = 0; mode != 3; ++mode) for (int cell = 0; cell < 100; ++cell)
    {
        Packets::Map::Discovery p; p.mode = static_cast<Packets::Map::Mode>(mode); p.playerid = 7;
        if (mode == 2) { p.generation = 123; p.epoch = 17; p.revision = 9; }
        else { p.sequence = 33; p.epoch = mode == 1 ? 17 : 0; }
        p.cells.Add(cell);
        std::array<uint32_t,64> words{}; auto* bytes = reinterpret_cast<uint8_t*>(words.data());
        serialize::WriteStream writer(bytes, sizeof(words)); expect(static_cast<Packet&>(p).SerializeWrite(writer), "Actual map packet writes."); writer.Flush();
        const int length = writer.GetBytesProcessed();
        Packets::Map::Discovery decoded; serialize::ReadStream reader(bytes,length);
        expect(static_cast<Packet&>(decoded).SerializeRead(reader) && decoded.cells == p.cells && decoded.mode == p.mode, "Actual map packet roundtrip.");
        expect(static_cast<Packet&>(p).GetChannel() == ePacketChannel::SYSTEM, "Map packet ordered with host lifecycle.");
        for (int n = 0; n < length; ++n) { Packets::Map::Discovery truncated; serialize::ReadStream cut(bytes,n); expect(!static_cast<Packet&>(truncated).SerializeRead(cut), "Every shorter map payload rejected."); }
    }
    Packets::Map::Discovery bad; serialize::MeasureStream measure;
    expect(!static_cast<Packet&>(bad).SerializeMeasure(measure), "Invalid unbound state cannot serialize.");
    bad.generation = 1; bad.cells.bits.back() = 240;
    expect(!static_cast<Packet&>(bad).SerializeMeasure(measure), "Reserved bits cannot serialize.");
    Packets::Map::Discovery valid; valid.generation = 1; valid.epoch = 1; valid.revision = 1;
    std::array<uint32_t,64> words{}; auto* bytes = reinterpret_cast<uint8_t*>(words.data());
    serialize::WriteStream writer(bytes,sizeof(words)); expect(static_cast<Packet&>(valid).SerializeWrite(writer), "Valid state for malformed full-length tests writes."); writer.Flush();
    const auto original = words; const int length = writer.GetBytesProcessed();
    auto reject = [&] { Packets::Map::Discovery p; serialize::ReadStream r(bytes,length); return !static_cast<Packet&>(p).SerializeRead(r); };
    bytes[0] |= 3; expect(reject(), "Unused mode code rejected on real read."); words = original;
    // 2 mode bits + 3 owner bits + four 31-bit counters = bitmap offset 129.
    const int reserved = 129 + 99 + 1;
    bytes[reserved / 8] |= uint8_t(1u << (reserved % 8));
    expect(reject(), "Reserved discovery bit rejected on real full-length read.");
}
void server()
{
    CNetworkPlayer host, guest; setup(host,guest);
    Packets::Map::Discovery seed; seed.mode = Packets::Map::Mode::Seed; seed.sequence = 1; seed.cells.Add(4);
    expect(!CMapSyncServer::Receive(seed,&guest), "Guest cannot seed host campaign.");
    expect(CMapSyncServer::Receive(seed,&host) && factory.maps.back().generation == 10, "Host seed stamped with connection identity.");
    auto initial = factory.maps.back(); Packets::Map::Discovery reveal;
    reveal.mode = Packets::Map::Mode::Reveal; reveal.playerid = 1; reveal.sequence = 1; reveal.epoch = initial.epoch; reveal.cells.Add(8);
    expect(CMapSyncServer::Receive(reveal,&guest) && factory.maps.back().cells.Has(4) && factory.maps.back().cells.Has(8), "Authenticated guest reveals native cell.");
    expect(!CMapSyncServer::Receive(reveal,&guest), "Owner sequence rejects replay.");
    reveal.sequence = 2; reveal.playerid = 0;
    expect(!CMapSyncServer::Receive(reveal,&guest), "Forged reveal owner rejected.");
    reveal.playerid = 1; reveal.generation = 123;
    expect(!CMapSyncServer::Receive(reveal,&guest), "Client cannot choose server generation.");
    CMapSyncServer::Replay(&guest); auto replay = factory.maps.back();
    expect(replay.cells.Has(8) && replay.revision == 2, "Late join receives current discoveries.");
    CNetworkPlayerManager::host = &guest; CMapSyncServer::HostChanged();
    expect(factory.maps.back().playerid == 1 && factory.maps.back().epoch == initial.epoch && factory.maps.back().cells == replay.cells, "Host migration preserves discovery and rebinds host identity.");
    Packets::Players::PlayerPlaceWaypoint waypoint; waypoint.playerid.value = 0; waypoint.sequence = 1;
    waypoint.place = true; waypoint.position = CVector2D(10,20);
    expect(CMapSyncServer::Waypoint(waypoint,&guest) && guest.m_waypointState.playerid.value == 1 && guest.m_waypointState.generation == 11, "C2S omitted sender is assigned by authenticated peer.");
    expect(!CMapSyncServer::Waypoint(waypoint,&guest), "Duplicate waypoint cannot relay.");
    waypoint.sequence = 2; waypoint.generation = 99;
    expect(!CMapSyncServer::Waypoint(waypoint,&guest), "Forged waypoint generation rejected.");
    waypoint.generation = 0; waypoint.position.x = NAN;
    expect(!CMapSyncServer::Waypoint(waypoint,&guest), "Nonfinite waypoint rejected before cache.");
    waypoint.position.x = 10; waypoint.place = false;
    expect(CMapSyncServer::Waypoint(waypoint,&guest) && !guest.m_waypointState.place, "Waypoint removal retains ordered lifetime.");
    auto alien = guest; alien.m_mapSequence = 0; waypoint.sequence = 3;
    expect(!CMapSyncServer::Waypoint(waypoint,&alien), "Unregistered peer wrapper rejected.");
    CNetworkPlayerManager::m_pPlayers = {&guest}; CMapSyncServer::Leave(&guest); CMapSyncServer::Replay(&guest);
    expect(factory.maps.back().epoch == 0 && factory.maps.back().cells.Count() == 0, "Last departure clears room discovery.");
}
void client()
{
    CNetworkPlayer host, guest; setup(host,guest); CNetworkPlayerManager::m_nMyId = 1;
    CTheZones::ExploredTerritoriesArray[99] = 1; CMapSync::HostChanged(0);
    Packets::Map::Discovery state; state.generation = 10; state.epoch = 1; state.revision = 1; state.cells.Add(4);
    CMapSync::Receive(state);
    expect(CTheZones::ExploredTerritoriesArray[4] && !CTheZones::ExploredTerritoriesArray[99]
        && CTheZones::TotalNumberExploredTerritories == 1, "Guest personal save cannot seed discovery.");
    FrontEndMenuManager.m_nTargetBlipIndex = 1; CRadar::ms_RadarTrace[0] = {true, RADAR_SPRITE_WAYPOINT, {50,60,0}};
    CWorld::Players[0].m_pPed->pos = {1000,1000,0}; CMapSync::Process();
    expect(!factory.waypoints.empty() && factory.waypoints.back().place && factory.waypoints.back().position.x == 50,
        "Native waypoint already present when connecting is transmitted.");
    FrontEndMenuManager.m_nTargetBlipIndex = 0;
    expect(factory.maps.back().mode == Packets::Map::Mode::Reveal && factory.maps.back().cells.Count() == 1, "Initialized outdoor guest contributes only its current cell.");
    testTick += 6000; CGame::currArea = 1; auto count = factory.maps.size(); CMapSync::Process();
    expect(factory.maps.size() == count, "Interior player does not reveal exterior map."); CGame::currArea = 0;
    state.revision = 2; state.cells.Add(7); state.generation = 999; CMapSync::Receive(state);
    expect(!CTheZones::ExploredTerritoriesArray[7], "Recycled host connection cannot apply old map.");
    state.generation = 10; CMapSync::Receive(state); expect(CTheZones::ExploredTerritoriesArray[7], "Current host map applies.");
    Packets::Players::PlayerPlaceWaypoint waypoint; waypoint.playerid.value = 0; waypoint.generation = 10; waypoint.sequence = 1;
    waypoint.place = true; waypoint.position = CVector2D(20,30); CMapSync::ReceiveWaypoint(waypoint);
    expect(host.m_waypointState.place, "Remote waypoint binds to ordered identity.");
    host.m_vitals.generation = 12; waypoint.sequence = 2; waypoint.place = false; CMapSync::ReceiveWaypoint(waypoint);
    expect(host.m_waypointState.place, "Old connection cannot remove new actor waypoint.");
    waypoint.generation = 12; CMapSync::ReceiveWaypoint(waypoint); expect(!host.m_waypointState.place, "Correct connection can remove waypoint.");
    // Restore the current map authority after the independent waypoint reuse case.
    host.m_vitals.generation = 10;
    state.epoch = 2; state.revision = 1; state.cells = {}; state.cells.Add(15);
    CMapSync::Receive(state); expect(CTheZones::ExploredTerritoriesArray[15] && !CTheZones::ExploredTerritoriesArray[4], "New campaign replaces discovery instead of merging old campaign.");
    state.epoch = 1; state.revision = 99; state.cells.Add(4); CMapSync::Receive(state);
    expect(!CTheZones::ExploredTerritoriesArray[4], "Delayed old campaign cannot restore old discovery.");
    state.epoch = 2; state.revision = 1; state.cells = {}; state.cells.Add(15);
    CMapSync::HostChanged(1); CLocalPlayer::m_bIsHost = true; state.playerid = 1; state.generation = 11;
    CMapSync::Receive(state); count = factory.maps.size(); testTick += 6000; CMapSync::Process();
    expect(factory.maps.size() == count || factory.maps.back().mode != Packets::Map::Mode::Seed, "Promoted guest preserves campaign rather than reseeding.");
    Events::initScriptsEvent.before.Fire(); Events::processScriptsEvent.after.Fire();
    CTheZones::ExploredTerritoriesArray[99] = 1; CMapSync::Process();
    expect(factory.maps.back().mode == Packets::Map::Mode::Seed, "Current host deliberate new game or load requests a fresh seed.");
    CMapSync::Reset(); count = factory.maps.size(); CMapSync::Process();
    expect(factory.maps.size() == count, "Disconnect reset waits for new authoritative room state.");
}
void menu_migration()
{
    CNetworkPlayer host, guest; setup(host,guest); CNetworkPlayerManager::m_nMyId = 1;
    scriptsReady = false; gGameState = 0; CMapSync::HostChanged(0);
    Packets::Map::Discovery state; state.generation = 10; state.epoch = 5; state.revision = 1; state.cells.Add(4);
    CMapSync::Receive(state); CMapSync::HostChanged(1); CLocalPlayer::m_bIsHost = true;
    state.playerid = 1; state.generation = 11; CMapSync::Receive(state);
    Events::initScriptsEvent.before.Fire(); gGameState = 9; Events::processScriptsEvent.after.Fire();
    CTheZones::ExploredTerritoriesArray[99] = 1; CMapSync::Process();
    expect(factory.maps.empty() || factory.maps.back().mode != Packets::Map::Mode::Seed,
        "Promoted menu guest first game initialization cannot replace host campaign discovery.");
    expect(CTheZones::ExploredTerritoriesArray[4] && !CTheZones::ExploredTerritoriesArray[99],
        "Promoted menu guest adopts cached host discovery before publishing anything.");
}
int main(int argc, char** argv)
{
    std::string test = argc > 1 ? argv[1] : "contracts";
    if (test == "contracts") contracts(); else if (test == "codecs") codecs(); else if (test == "server") server(); else if (test == "client") client(); else if (test == "menu-migration") menu_migration(); else return 2;
    std::cout << test << ": " << assertions << " assertions, " << failures << " failures\n";
    return failures ? 1 : 0;
}
