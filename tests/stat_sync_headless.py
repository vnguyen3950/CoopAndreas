"""Test selected production stat functions with engine and transport doubles.

No game code or native hook address is executed. Generated files stay in .cache.
Use --source-ref to demonstrate the same assertions against a historical revision.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FILES = [
    "client/src/CStatsSync.h",
    "client/src/CStatsSync.cpp",
    "client/src/Hooks/StatsHooks.cpp",
    "client/src/PacketHandlers/players.cpp",
    "shared/network/packets/players.h",
    "third_party/plugin-sdk/plugin_sa/game_sa/eStats.h",
]


def function(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        depth += (text[index] == "{") - (text[index] == "}")
        if depth == 0:
            return text[start:index + 1]
    raise ValueError("Unterminated production function: " + signature)


HARNESS = r'''
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <set>
#include <vector>
#include "eStats.h"
struct CNetworkPlayer;
#include "CStatsSync.h"

static unsigned assertions = 0, failures = 0;
static void check(bool ok, const char* message) {
    ++assertions;
    if (!ok) { ++failures; std::cout << "FAIL: " << message << '\n'; }
}
static float values[512]{};
static unsigned reads[512]{};
class CStats {
public:
    static float GetStatValue(eStats id) { ++reads[int(id)]; return values[int(id)]; }
    static void SetStatValue(eStats id, float value) {
        // Model an engine applying a value different from the caller's request.
        values[int(id)] = std::max(0.0f, std::min(1000.0f, value));
    }
};
class CNetwork { public: static bool m_bAuthenticated; };
bool CNetwork::m_bAuthenticated = false;
namespace Packets { namespace Players {
    struct PlayerStats { uint32_t playerid{}; float stats[WIRE_COUNT]{}; };
}}
static std::vector<Packets::Players::PlayerStats> sent;
struct PacketFactory {
    void Send(const Packets::Players::PlayerStats& packet) { sent.push_back(packet); }
};
static PacketFactory factory;
static PacketFactory& GetPacketFactory() { return factory; }
struct StatCache {
    float values[512]{};
    float& operator[](eStats id) { return values[int(id)]; }
};
struct CNetworkPlayer { StatCache m_stats; };
static CNetworkPlayer peer1, peer2;
struct CNetworkPlayerManager {
    static CNetworkPlayer* GetPlayer(uint32_t id) {
        return id == 1 ? &peer1 : id == 2 ? &peer2 : nullptr;
    }
};
#define PACKET_HANDLER(TYPE, ARGUMENT) void ReceiveStats(ARGUMENT)

PRODUCTION_FUNCTIONS

int main() {
    const eStats expected[] = {
        STAT_PISTOL_SKILL, STAT_SILENCED_PISTOL_SKILL, STAT_DESERT_EAGLE_SKILL,
        STAT_SHOTGUN_SKILL, STAT_SAWN_OFF_SHOTGUN_SKILL, STAT_COMBAT_SHOTGUN_SKILL,
        STAT_MACHINE_PISTOL_SKILL, STAT_SMG_SKILL, STAT_AK_47_SKILL,
        STAT_M4_SKILL, STAT_RIFLE_SKILL
    };
    check(CStatsSync::SYNCED_STATS_COUNT == 11, "Exactly eleven actual weapon descriptors are mapped.");
    check(sizeof(Packets::Players::PlayerStats::stats) == 14 * sizeof(float), "The legacy stat array retains fourteen floats.");
    std::set<eStats> unique;
    for (unsigned i=0; i<11; ++i) {
        unique.insert(CStatsSync::m_aeSyncedStats[i]);
        check(CStatsSync::m_aeSyncedStats[i] == expected[i], "Every original weapon skill retains its slot order.");
        check(CStatsSync::GetSyncIdByInternal(expected[i]) == int(i), "Every supported skill resolves to its original slot.");
        values[int(expected[i])] = 100.0f + i;
    }
    check(unique.size() == 11, "Mapped descriptors do not alias one another.");
    check(CStatsSync::GetSyncIdByInternal(STAT_PROGRESS_MADE) == -1, "Reserved slots do not become story progress descriptors.");
    check(CStatsSync::GetSyncIdByInternal(STAT_STAMINA) == -1, "Personal stamina is not added to this bounded repair.");
    check(CStatsSync::GetSyncIdByInternal(eStats(-1)) == -1, "An unsupported descriptor does not resolve to a slot.");
    values[STAT_PROGRESS_MADE] = 777.0f;
    CStatsSync::NotifyChanged();
    check(sent.size() == 1, "One snapshot notification sends one packet.");
    for (unsigned i=0; i<11; ++i) check(sent.back().stats[i] == 100.0f+i, "The outgoing supported slots contain their actual skill values.");
    for (unsigned i=11; i<14; ++i) check(sent.back().stats[i] == 0, "All three reserved outgoing slots remain zero.");
    check(reads[STAT_PROGRESS_MADE] == 0, "Building a skill snapshot does not read story progress.");

    sent.clear();
    CNetwork::m_bAuthenticated = true;
    CStats__SetStatValue_Hook(STAT_PISTOL_SKILL, 321.0f);
    check(values[STAT_PISTOL_SKILL] == 321 && sent.size() == 1 && sent.back().stats[0] == 321,
          "An authenticated setter snapshot observes the newly applied value.");
    CStats__SetStatValue_Hook(STAT_PISTOL_SKILL, 1500.0f);
    check(sent.size() == 2 && sent.back().stats[0] == 1000.0f,
          "A setter notification observes the engine-adjusted value, not the request.");
    sent.clear();
    CStats__SetStatValue_Hook(STAT_PROGRESS_MADE, 555.0f);
    check(values[STAT_PROGRESS_MADE] == 555 && sent.empty(), "A story stat setter applies locally without broadcasting a skill packet.");
    sent.clear();
    CNetwork::m_bAuthenticated = false;
    CStats__SetStatValue_Hook(STAT_RIFLE_SKILL, 456.0f);
    check(values[STAT_RIFLE_SKILL] == 456 && sent.empty(), "An offline skill setter applies without a network notification.");

    Packets::Players::PlayerStats incoming{};
    incoming.playerid=1;
    for (unsigned i=0; i<11; ++i) incoming.stats[i]=20.0f+i;
    incoming.stats[11]=777; incoming.stats[12]=888; incoming.stats[13]=999;
    peer1.m_stats[STAT_PROGRESS_MADE]=111; peer2.m_stats[STAT_PISTOL_SKILL]=222;
    const float localSkill=values[STAT_PISTOL_SKILL], localStory=values[STAT_PROGRESS_MADE];
    ReceiveStats(&incoming);
    for (unsigned i=0; i<11; ++i) check(peer1.m_stats[expected[i]] == 20.0f+i, "Incoming skills update only the identified peer cache.");
    check(peer1.m_stats[STAT_PROGRESS_MADE] == 111, "Incoming reserved fields cannot overwrite the peer's story progress cache.");
    check(peer2.m_stats[STAT_PISTOL_SKILL] == 222, "An incoming snapshot does not overwrite another peer's cache.");
    check(values[STAT_PISTOL_SKILL] == localSkill && values[STAT_PROGRESS_MADE] == localStory,
          "Incoming peer stats never overwrite local skill or story state.");
    incoming.playerid=99; incoming.stats[0]=999; ReceiveStats(&incoming);
    check(peer1.m_stats[STAT_PISTOL_SKILL] == 20, "An unknown player snapshot leaves known peer state unchanged.");
    std::cout << "RESULT: " << assertions << " assertions, " << failures << " failures.\n";
    return failures ? 1 : 0;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--source-ref")
    parser.add_argument("--output", default=".cache/stat-sync-headless/current")
    args = parser.parse_args()
    output = (ROOT / args.output).resolve()
    if ROOT not in output.parents:
        raise ValueError("Headless test output must remain inside this worktree.")
    output.mkdir(parents=True, exist_ok=True)
    raw = {}
    for name in FILES:
        raw[name] = subprocess.check_output(["git", "show", args.source_ref + ":" + name], cwd=ROOT) if args.source_ref else (ROOT / name).read_bytes()
    source = {name: value.decode("utf-8-sig") for name, value in raw.items()}
    packet = function(source[FILES[4]], "class PlayerStats :")
    wire_count = int(re.search(r"float stats\[(\d+)\]", packet)[1])
    if wire_count != 14:
        raise ValueError("The legacy PlayerStats array changed; review the test contract.")
    # GCC 6 lacks inline variables. The integral constant is not odr-used here.
    header = source[FILES[0]].replace("static constexpr inline", "static constexpr")
    (output / "CStatsSync.h").write_text(header)
    (output / "eStats.h").write_text(source[FILES[5]])
    descriptor = re.search(r"std::array<eStats, CStatsSync::SYNCED_STATS_COUNT> CStatsSync::m_aeSyncedStats\s*=\s*\{.*?\};", source[FILES[1]], re.S)[0]
    functions = "\n\n".join([
        descriptor,
        function(source[FILES[1]], "void CStatsSync::NotifyChanged()"),
        function(source[FILES[1]], "int CStatsSync::GetSyncIdByInternal("),
        function(source[FILES[2]], "void CStats__SetStatValue_Hook("),
        function(source[FILES[3]], "PACKET_HANDLER(ePacketType::PLAYER_STATS,"),
    ])
    generated = HARNESS.replace("WIRE_COUNT", str(wire_count)).replace("PRODUCTION_FUNCTIONS", functions)
    test_source = output / "stat_sync_tests.cpp"
    test_source.write_text(generated)
    executable = output / "stat_sync_tests.exe"
    command = [args.compiler, "-std=c++14", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic", "-static", str(test_source), "-o", str(executable)]
    compile_result = subprocess.run(command, capture_output=True, text=True)
    (output / "compile.log").write_text(compile_result.stdout + compile_result.stderr)
    if compile_result.returncode:
        print(compile_result.stdout + compile_result.stderr)
        return compile_result.returncode
    result = subprocess.run([str(executable)], capture_output=True, text=True)
    (output / "test.log").write_text(result.stdout + result.stderr)
    evidence = {
        "source_ref": args.source_ref or "WorkingTree",
        "source_sha256": {name: hashlib.sha256(value).hexdigest() for name, value in raw.items()},
        "compiler_command": command,
        "wire_stat_slots": wire_count,
        "result": result.returncode,
        "output": result.stdout,
        "scope": "Selected production descriptor, send, lookup, setter-hook and receive functions; mocked native stats and transport. Native hook addresses and network serialization are not executed.",
        "header_adapter": "The temporary header omits inline on the integral constant solely for GCC 6 compatibility.",
    }
    (output / "result.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(result.stdout, end="")
    print("Evidence: " + str(output / "result.json"))
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
