#include "stdafx.h"
#include "CPlayerVitalsSync.h"

namespace
{
bool initializedScripts = false, authenticated = false, sentState = false;
uint32_t connection = 0, sequence = 0, lastSend = 0;
int lastPed = -1;
PlayerVitals::State previous;
bool EnsureConnection()
{
    if (!CNetwork::m_bAuthenticated)
    {
        authenticated = false; sentState = false; sequence = 0; lastPed = -1;
        for (auto* player : CNetworkPlayerManager::m_pPlayers) if (player) player->m_vitals = {};
        return false;
    }
    const uint32_t current = CNetwork::m_pPeer ? CNetwork::m_pPeer->connectID : 0;
    if (!authenticated || current != connection)
    {
        authenticated = true; connection = current; sequence = 0; sentState = false; lastPed = -1;
        for (auto* player : CNetworkPlayerManager::m_pPlayers) if (player) player->m_vitals = {};
    }
    return true;
}
bool LocalReady(CPlayerPed* ped)
{
    return CWorld::PlayerInFocus == 0 && gGameState == 9 && initializedScripts && ped
        && CPools::ms_pPedPool && CPools::ms_pPedPool->IsObjectValid(ped)
        && CWorld::Players[0].m_pPed == ped && ped->m_pPlayerData;
}
}
void CPlayerVitalsSync::Init()
{
    Events::initScriptsEvent.before += [] { initializedScripts = false; sentState = false; lastPed = -1; };
    Events::processScriptsEvent.after += [] { if (gGameState == 9) initializedScripts = true; };
    gameShutdownEvent.before += [] { initializedScripts = false; sentState = false; lastPed = -1; };
}
void CPlayerVitalsSync::Process()
{
    if (!EnsureConnection() || CWorld::PlayerInFocus != 0) return;
    for (auto* player : CNetworkPlayerManager::m_pPlayers) ApplyRemote(player);
    auto* ped = FindPlayerPed(0);
    if (!LocalReady(ped)) return;
    PlayerVitals::State state;
    state.maxHealth = CWorld::Players[0].m_nMaxHealth;
    state.pedMaxHealth = ped->m_fMaxHealth;
    state.breath = ped->m_pPlayerData->m_fBreath;
    // Query only the real owner's stats, never while a remote focus is installed.
    state.airCapacity = CStats::GetFatAndMuscleModifier(STAT_MOD_AIR_IN_LUNG);
    state.submerged = ped->m_nPhysicalFlags.bSubmergedInWater;
    if (!PlayerVitals::NormalizeNative(state)) return;
    const uint32_t now = GetTickCount();
    const int reference = CPools::GetPedRef(ped);
    const bool changedPed = reference != lastPed;
    if (sentState && !changedPed && (now - lastSend < 100 || (state == previous && now - lastSend < 2000))) return;
    if (sequence == PlayerVitals::MAX_COUNTER) return; // Never wrap an owner sequence.
    Packets::Players::Vitals packet;
    packet.playerid = CNetworkPlayerManager::m_nMyId;
    packet.sequence = ++sequence; packet.hasState = true; packet.state = state;
    if (!packet.Valid()) { --sequence; return; }
    GetPacketFactory().Send(packet);
    previous = state; sentState = true; lastSend = now; lastPed = reference;
}
void CPlayerVitalsSync::Receive(const Packets::Players::Vitals& packet)
{
    if (!EnsureConnection() || !packet.Valid() || !packet.generation
        || packet.playerid == CNetworkPlayerManager::m_nMyId) return;
    auto* player = CNetworkPlayerManager::GetPlayer(packet.playerid);
    if (!player) return; // Same reliable SYSTEM stream orders connect before identity.
    if (!packet.hasState)
    {
        player->m_vitals.Bind(player->m_iPlayerId, packet.generation);
        return;
    }
    player->m_vitals.Accept(packet.playerid, packet.generation, packet.sequence, packet.state);
}
bool CPlayerVitalsSync::HasBoundPed(CNetworkPlayer* player)
{
    if (CWorld::PlayerInFocus != 0 || !player
        || std::find(CNetworkPlayerManager::m_pPlayers.begin(), CNetworkPlayerManager::m_pPlayers.end(), player)
            == CNetworkPlayerManager::m_pPlayers.end()) return false;
    if (!player->m_pPed || player->m_nPedRef < 0
        || !CPools::ms_pPedPool || !CPools::ms_pPedPool->IsObjectValid(player->m_pPed)
        || CPools::GetPed(player->m_nPedRef) != player->m_pPed) return false;
    // The script/pool generation and PlayerInfo binding must both still match.
    const int internal = player->GetInternalId();
    return internal >= 2 && internal < Config::MAX_SERVER_PLAYERS + 2
        && CWorld::Players[internal].m_pPed == player->m_pPed;
}
const PlayerVitals::State* CPlayerVitalsSync::GetState(const CNetworkPlayer* player)
{
    return player && player->m_vitals.hasState && player->m_vitals.state.Valid() ? &player->m_vitals.state : nullptr;
}
bool CPlayerVitalsSync::ApplyRemote(CNetworkPlayer* player)
{
    if (!CNetwork::m_bAuthenticated || !initializedScripts || gGameState != 9 || !HasBoundPed(player)) return false;
    auto* state = GetState(player);
    if (!state || !player->m_pPed->m_pPlayerData) return false;
    auto* ped = player->m_pPed;
    const int internal = player->GetInternalId();
    ped->m_fMaxHealth = state->pedMaxHealth;
    CWorld::Players[internal].m_nMaxHealth = state->maxHealth;
    ped->m_pPlayerData->m_fBreath = state->breath;
    // Owner-computed airCapacity remains cached. Global lung/stamina stats and
    // native swimming progression are not changed on this representation.
    return true;
}
float CPlayerVitalsSync::HealthPercent(CPlayerPed* ped, float health)
{
    if (!ped || CWorld::PlayerInFocus != 0) return 0.0f;
    if (auto* remote = CNetworkPlayerManager::GetPlayer(ped))
        if (auto* state = GetState(remote)) return PlayerVitals::HealthPercent(health, *state);
    PlayerVitals::State fallback;
    if (ped == CWorld::Players[0].m_pPed) fallback.maxHealth = CWorld::Players[0].m_nMaxHealth;
    return PlayerVitals::HealthPercent(health, fallback);
}
