#include <iostream>
#include <limits>
#include "player_vitals_doubles.h"
#include "vitals_client.inc"
#include "vitals_server.inc"
#include "vitals_ped_binding.inc"
#include "vitals_bars.inc"
#include "vitals_registry_remove.inc"

using State = PlayerVitals::State;
using Vitals = Packets::Players::Vitals;
static unsigned checks = 0, failures = 0;
static void expect(bool value, const char* message)
{ ++checks; if (!value) { ++failures; std::cout << "FAIL: " << message << '\n'; } }
static Peer localPeer;
static CPlayerData localData, remoteData;
static CPlayerPed localPed, remotePed;
static CNetworkPlayer remotePlayer;
static void local(bool ready = true)
{
    CNetwork::m_pPeer = &localPeer;
    CPlayerVitalsSync::Init();
    localPed.m_pPlayerData = &localData; localData.m_fBreath = 900;
    CWorld::Players[0].m_pPed = &localPed;
    nativePool.live.insert(&localPed); CPools::references[100] = &localPed;
    globalFloatStats[22] = 100; globalFloatStats[24] = 569; globalIntStats[105] = 200;
    gGameState = ready ? 9 : 0;
    if (ready) Events::processScriptsEvent.after.Fire();
}
static State profile()
{ State s; s.maxHealth = 176; s.pedMaxHealth = 100.144f; s.airCapacity = 4000; s.breath = 3000; s.submerged = true; return s; }
static void connect_remote()
{
    remotePlayer.m_iPlayerId = 1;
    CNetworkPlayerManager::m_pPlayers = {&remotePlayer};
    Vitals identity; identity.playerid = 1; identity.generation = 5;
    CPlayerVitalsSync::Receive(identity);
    Vitals sample = identity; sample.hasState = true; sample.sequence = 1; sample.state = profile();
    CPlayerVitalsSync::Receive(sample);
}
static void spawn_remote()
{
    remotePed.m_pPlayerData = &remoteData; remoteData.m_fBreath = 500;
    remotePlayer.m_pPed = &remotePed; remotePlayer.m_nPedRef = 200;
    nativePool.live.insert(&remotePed); CPools::references[200] = &remotePed;
    CWorld::Players[3].m_pPed = &remotePed;
}
static Vitals last_packet()
{ return static_cast<const Vitals&>(*GetPacketFactory().sent.back()); }
static void menu()
{
    local(false); connect_remote(); CPlayerVitalsSync::Process();
    expect(GetPacketFactory().sent.empty() && statReads == 0, "Menu authentication cannot sample uninitialized native vitals.");
    expect(remotePlayer.m_vitals.hasState && !CPlayerVitalsSync::ApplyRemote(&remotePlayer), "SYSTEM replay caches state before native spawn in menu.");
    gGameState = 9; CPlayerVitalsSync::Process();
    expect(GetPacketFactory().sent.empty(), "Game idle state alone does not bypass completed-scripts gate.");
    Events::processScriptsEvent.after.Fire(); CPlayerVitalsSync::Process();
    expect(GetPacketFactory().sent.size() == 1 && last_packet().generation == 0 && last_packet().sequence == 1
        && last_packet().state.airCapacity == 1150, "Initialized real owner publishes an unstamped first snapshot.");
}
static void spawn_and_bars()
{
    local(); connect_remote(); expect(remotePlayer.m_vitals.hasState, "Full snapshot buffered before ped exists.");
    expect(!CPlayerVitalsSync::ApplyRemote(&remotePlayer), "Missing native entity is never dereferenced.");
    spawn_remote(); auto floatStats = globalFloatStats; auto intStats = globalIntStats;
    CPlayerVitalsSync::Process();
    expect(remotePed.m_fMaxHealth == profile().pedMaxHealth && CWorld::Players[3].m_nMaxHealth == 176
        && remoteData.m_fBreath == 3000, "Actual native application preserves independent maxima and remaining air after spawn.");
    expect(globalFloatStats == floatStats && globalIntStats == intStats && CWorld::Players[0].m_nMaxHealth == 100
        && localData.m_fBreath == 900, "Remote state does not overwrite local native values or global lung/stamina/max-health stats.");
    expect(CPlayerVitalsSync::HealthPercent(&remotePed,88) == 50, "Actual service health fraction uses owner PlayerInfo maximum.");
    remotePed.m_fHealth = 88; remotePed.m_fArmour = 20;
    CNetworkPlayerList::DrawBars(&remotePed,0,0);
    bool health = false, breath = false;
    for (const auto& bar : bars) {
        if (bar.color == HUD_COLOUR_RED && bar.progress == 50) health = true;
        if (bar.color == HUD_COLOUR_BLUELIGHT && bar.progress == 75) breath = true;
    }
    expect(health && breath, "Actual extracted visible-bar function receives owner health and breath fractions.");
    remoteData.m_fBreath = 1200; remotePed.m_fMaxHealth = 100; // Recorded native ProcessControl change.
    expect(CPlayerVitalsSync::ApplyRemote(&remotePlayer) && remoteData.m_fBreath == 3000
        && remotePed.m_fMaxHealth == profile().pedMaxHealth, "Owner state is restored after native control modifies the representation.");
}
static void focus()
{
    local(); connect_remote(); spawn_remote(); CPlayerVitalsSync::Process();
    GetPacketFactory().sent.clear(); statReads = nativeReads = 0;
    CWorld::PlayerInFocus = 3; tick += 300; remoteData.m_fBreath = 500;
    CPlayerVitalsSync::Process();
    expect(GetPacketFactory().sent.empty() && statReads == 0 && nativeReads == 0,
        "Swapped player focus prevents all publisher native/stat sampling.");
    expect(!CPlayerVitalsSync::ApplyRemote(&remotePlayer) && remoteData.m_fBreath == 500 && nativeReads == 0,
        "Swapped focus also prevents remote native application.");
    CWorld::PlayerInFocus = 0;
    expect(CPlayerVitalsSync::ApplyRemote(&remotePlayer) && remoteData.m_fBreath == 3000, "Safe local focus allows cached restoration again.");
}
static void recycle()
{
    local(); connect_remote(); spawn_remote(); CPlayerVitalsSync::Process();
    CPools::references.erase(200); CPools::references[201] = &remotePed; remoteData.m_fBreath = 777;
    expect(!CPlayerVitalsSync::ApplyRemote(&remotePlayer) && remoteData.m_fBreath == 777,
        "Recycled pool slot at the same pointer cannot receive the old player's vitals.");
    remotePlayer.m_nPedRef = 201; CWorld::Players[3].m_pPed = nullptr;
    expect(!CPlayerVitalsSync::ApplyRemote(&remotePlayer), "Incorrect PlayerInfo binding rejects even a current pool reference.");
    CWorld::Players[3].m_pPed = &remotePed;
    expect(CPlayerVitalsSync::ApplyRemote(&remotePlayer) && remoteData.m_fBreath == 3000, "Valid recreated native binding reapplies retained owner profile.");
    CNetworkPlayerManager::m_pPlayers.clear(); remoteData.m_fBreath = 888;
    expect(!CPlayerVitalsSync::ApplyRemote(&remotePlayer) && remoteData.m_fBreath == 888, "Removed network entity cannot be applied through an old pointer.");
}
static void reconnect()
{
    local(); connect_remote(); spawn_remote(); CPlayerVitalsSync::Process();
    ++localPeer.connectID; tick += 300; CPlayerVitalsSync::Process();
    expect(!remotePlayer.m_vitals.hasState && remotePlayer.m_vitals.generation == 0 && last_packet().sequence == 1,
        "Real transport connection change resets caches and publication sequence.");
    Vitals identity; identity.playerid = 1; identity.generation = 6; CPlayerVitalsSync::Receive(identity);
    Vitals old = identity; old.generation = 5; old.hasState = true; old.sequence = PlayerVitals::MAX_COUNTER; old.state = profile();
    CPlayerVitalsSync::Receive(old);
    expect(!remotePlayer.m_vitals.hasState, "Old connection generation cannot populate a new slot binding.");
    old.generation = 6; old.sequence = 1; CPlayerVitalsSync::Receive(old); old.state.breath = 0;
    CPlayerVitalsSync::Receive(old);
    expect(remotePlayer.m_vitals.state.breath == 3000, "Duplicate sequence cannot overwrite new connection profile.");
    CNetwork::m_bAuthenticated = false; CPlayerVitalsSync::Process();
    expect(!remotePlayer.m_vitals.hasState, "Disconnection clears client cached metadata.");
}
static void native_transients()
{
    local(); localData.m_fBreath = 1160; CPlayerVitalsSync::Process();
    expect(last_packet().state.breath == 1150 && localData.m_fBreath == 1160,
        "Legitimate native recovery overshoot clamps only the publication, not owner's native state.");
    tick += 300; localCapacity = std::numeric_limits<float>::quiet_NaN(); auto count = GetPacketFactory().sent.size();
    CPlayerVitalsSync::Process(); expect(GetPacketFactory().sent.size() == count, "Nonfinite native ceiling cannot publish.");
    localCapacity = 4002; CPlayerVitalsSync::Process(); expect(GetPacketFactory().sent.size() == count, "Out-of-stock ceiling cannot publish.");
    localCapacity = 1150; Events::initScriptsEvent.before.Fire(); CPlayerVitalsSync::Process();
    expect(GetPacketFactory().sent.size() == count, "Load lifecycle suspends sampling until initialized again.");
    Events::processScriptsEvent.after.Fire(); CPlayerVitalsSync::Process();
    expect(last_packet().sequence == 2, "New Game/load on the same connection preserves sequence monotonicity.");
}
static void duplicate_wrapper_reconnect()
{
    local();
    auto* old = new CNetworkPlayer; old->m_iPlayerId = 1;
    CNetworkPlayerManager::m_pPlayers = {old};
    Vitals oldIdentity; oldIdentity.playerid = 1; oldIdentity.generation = 5;
    CPlayerVitalsSync::Receive(oldIdentity);
    ++localPeer.connectID; CPlayerVitalsSync::Process();
    CNetworkPlayerManager::RemoveById(1); // Actual registry removal before a new SYSTEM wrapper.
    CNetworkPlayer fresh; fresh.m_iPlayerId = 1;
    CPlayerPed freshPed; CPlayerData freshData; freshPed.m_pPlayerData = &freshData;
    fresh.m_pPed = &freshPed; fresh.m_nPedRef = 300;
    nativePool.live.insert(&freshPed); CPools::references[300] = &freshPed;
    CWorld::Players[3].m_pPed = &freshPed;
    CNetworkPlayerManager::m_pPlayers.push_back(&fresh);
    Vitals identity; identity.playerid = 1; identity.generation = 6; CPlayerVitalsSync::Receive(identity);
    Vitals packet = identity; packet.hasState = true; packet.sequence = 1; packet.state = profile(); CPlayerVitalsSync::Receive(packet);
    expect(fresh.m_vitals.hasState && CNetworkPlayerManager::m_pPlayers.size() == 1,
        "Reconnect replay binds the single replacement wrapper after actual registry removal.");
    CPlayerVitalsSync::Process();
    expect(freshData.m_fBreath == 3000 && CWorld::Players[3].m_nMaxHealth == 176,
        "Current spawned wrapper receives native vitals despite an old same-ID wrapper.");
}
static void server_authority_replay()
{
    CNetworkPlayer owner, recipient, outsider; Peer ownerPeer, recipientPeer, outsiderPeer;
    owner.m_iPlayerId = 1; owner.m_pPeer = &ownerPeer; recipient.m_iPlayerId = 2; recipient.m_pPeer = &recipientPeer;
    outsider.m_iPlayerId = 1; outsider.m_pPeer = &outsiderPeer;
    const auto generation = CPlayerVitalsServer::AllocateGeneration(); owner.m_vitals.Bind(1,generation);
    CNetworkPlayerManager::m_pPlayers = {&owner,&recipient};
    Vitals packet; packet.playerid = 1; packet.sequence = 1; packet.hasState = true; packet.state = profile();
    expect(!CPlayerVitalsServer::Receive(packet,&outsider), "Unregistered peer with a copied player ID cannot publish.");
    auto forged = packet; forged.playerid = 2;
    expect(!CPlayerVitalsServer::Receive(forged,&owner), "Authenticated player cannot publish another owner ID.");
    forged = packet; forged.generation = generation;
    expect(!CPlayerVitalsServer::Receive(forged,&owner), "Client-chosen generation is rejected by actual handler.");
    expect(CPlayerVitalsServer::Receive(packet,&owner) && last_packet().generation == generation,
        "Actual server validates and stamps authenticated owner's generation.");
    expect(!CPlayerVitalsServer::Receive(packet,&owner), "Actual server rejects duplicate sequence.");
    forged = packet; forged.sequence = 2; forged.state.breath = std::numeric_limits<float>::infinity();
    expect(!CPlayerVitalsServer::Receive(forged,&owner) && owner.m_vitals.sequence == 1,
        "Malformed update does not corrupt stored join snapshot or consume sequence.");
    GetPacketFactory().sent.clear(); CPlayerVitalsServer::Replay(&owner,&recipient);
    expect(GetPacketFactory().sent.size() == 2, "Actual join replay emits ordered identity and cached full state.");
    auto identity = static_cast<const Vitals&>(*GetPacketFactory().sent[0]); auto sample = last_packet();
    expect(!identity.hasState && identity.sequence == 0 && sample.hasState && sample.state == profile(),
        "Join identity/full-state envelopes retain owner metadata exactly.");
    local(false); remotePlayer.m_iPlayerId = 1; CNetworkPlayerManager::m_nMyId = 2;
    CNetworkPlayerManager::m_pPlayers = {&remotePlayer};
    CPlayerVitalsSync::Receive(identity); CPlayerVitalsSync::Receive(sample);
    expect(remotePlayer.m_vitals.hasState && !remotePlayer.m_pPed,
        "Actual server replay reaches actual client cache before native spawn.");
    CNetworkPlayer replacement; replacement.m_iPlayerId = 1; replacement.m_pPeer = &outsiderPeer;
    const auto newGeneration = CPlayerVitalsServer::AllocateGeneration(); replacement.m_vitals.Bind(1,newGeneration);
    expect(newGeneration > generation, "Reused server slot receives a strictly newer generation.");
    CNetworkPlayerManager::m_pPlayers = {&replacement,&recipient};
    expect(!CPlayerVitalsServer::Receive(packet,&owner) && CPlayerVitalsServer::Receive(packet,&replacement),
        "Old sender pointer is rejected after slot reuse while new owner may restart sequence one.");
}
int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    const std::string name = argv[1];
    if (name == "menu") menu(); else if (name == "spawn_bars") spawn_and_bars(); else if (name == "focus") focus();
    else if (name == "recycle") recycle(); else if (name == "reconnect") reconnect();
    else if (name == "native_transients") native_transients(); else if (name == "server_replay") server_authority_replay();
    else if (name == "duplicate_wrapper") duplicate_wrapper_reconnect();
    else return 2;
    std::cout << name << ": " << checks << " assertions, " << failures << " failures\n";
    return failures ? 1 : 0;
}
