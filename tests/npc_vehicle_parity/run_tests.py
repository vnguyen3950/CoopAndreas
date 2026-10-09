"""Test frozen production NPC codec and handler bodies; no game or ENet calls."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
INPUTS = [
    "shared/network/packets/peds.h", "shared/network/packets/players.h",
    "shared/network/packet.h", "shared/network/packet_types.h",
    "shared/network/serializable_types.h", "shared/network/vehicle_authority.h",
    "shared/network/npc_sync.h", "client/src/CNetworkPed.cpp",
    "shared/config.h", "third_party/serialize.h",
    "third_party/plugin-sdk/plugin_sa/game_sa/CVehicle.h",
    "third_party/plugin-sdk/plugin_sa/game_sa/CWeapon.h",
    "third_party/plugin-sdk/plugin_sa/game_sa/eWeaponType.h",
    "client/src/CNetworkPedManager.cpp", "client/src/PacketHandlers/peds.cpp",
    "server/src/PacketHandlers/peds.cpp",
]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def block(source, pattern, declaration=False):
    match = re.search(pattern, source, re.S)
    if not match:
        raise ValueError("Production source seam missing: " + pattern)
    start = source.index("{", match.start())
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"',
                    lambda m: " " * len(m[0]), source, flags=re.S)
    depth = 1
    for end in range(start + 1, len(source)):
        depth += (masked[end] == "{") - (masked[end] == "}")
        if depth == 0:
            return source[match.start():end + 2] if declaration else source[start:end + 1]
    raise ValueError("Unbalanced source seam")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--msvc-env", type=Path, default=Path(
        r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"))
    parser.add_argument("--mutate-authority", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    allowed = (ROOT / ".cache" / "npc-vehicle-parity").resolve()
    if allowed not in output.parents or output.exists():
        parser.error("Use a fresh child of this worktree's .cache/npc-vehicle-parity")
    output.mkdir(parents=True)
    before = {p: sha(ROOT / p) for p in INPUTS}
    tests_before = {p: sha(HERE / p) for p in ("tests.cpp", "doubles.h", "run_tests.py")}
    source = output / "source"
    for p in INPUTS:
        target = source / p
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / p, target)
    read = lambda p: (source / p).read_text(encoding="utf-8-sig")
    fragments = []
    for path, name in [
        ("third_party/plugin-sdk/plugin_sa/game_sa/CVehicle.h", "eVehicleType"),
        ("third_party/plugin-sdk/plugin_sa/game_sa/CVehicle.h", "eDoorLock"),
        ("third_party/plugin-sdk/plugin_sa/game_sa/CWeapon.h", "eWeaponState"),
        ("third_party/plugin-sdk/plugin_sa/game_sa/eWeaponType.h", "eWeaponType"),
    ]:
        fragments.append(block(read(path), r"enum\s+" + name + r"\b[^{}]*\{", True))
    vectors = read("shared/network/serializable_types.h")
    for name in ("WorldPositionCompressed", "MoveSpeedCompressed", "NormalizedVector"):
        fragments.append(block(vectors, r"struct\s+" + name + r"\b[^{}]*\{", True))
    players = read("shared/network/packets/players.h")
    fragments.append("namespace Packets::Players {\n" + "\n".join(
        block(players, r"struct\s+" + name + r"\s*\{", True)
        for name in ("SHealthSnapshot", "SWeaponSnapshot")) + "\n}")
    packet = block(read(INPUTS[0]), r"class\s+PedDriverUpdate\s*:\s*public\s+Packet\s*\{", True)
    legacy = subprocess.check_output(["git", "show", "7a09780:" + INPUTS[0]], cwd=ROOT).decode()
    legacy_packet = block(legacy, r"class\s+PedDriverUpdate\s*:\s*public\s+Packet\s*\{", True)
    legacy_packet = legacy_packet.replace("PedDriverUpdate", "LegacyPedDriverUpdate")
    fragments.append("namespace Packets::Peds {\n" + packet + "\n" + legacy_packet + "\n}")
    (output / "extracted_packet.h").write_text("\n".join(fragments), encoding="utf-8")
    server = block(read("server/src/PacketHandlers/peds.cpp"),
                   r"PACKET_HANDLER\(\s*ePacketType::PED_DRIVER_UPDATE\b")
    client = block(read("client/src/PacketHandlers/peds.cpp"),
                   r"PACKET_HANDLER\(\s*ePacketType::PED_DRIVER_UPDATE\b")
    sender = block(read("client/src/CNetworkPedManager.cpp"), r"if\s*\(isDriver\)")
    if args.mutate_authority:
        mutated = server.replace("!VehicleAuthority::CanUpdateNpcDriver", "VehicleAuthority::CanUpdateNpcDriver", 1)
        if mutated == server:
            raise ValueError("Authority guard missing; cannot perform the requested mutation")
        server = mutated
    handlers = (
        "void ServerDriver(Packets::Peds::PedDriverUpdate* pPedDriverUpdate, CNetworkPlayer* pNetworkPlayer) " + server +
        "\nvoid ClientDriver(Packets::Peds::PedDriverUpdate* pPedDriverUpdate) " + client +
        "\nvoid CaptureDriver(CNetworkPed* pNetworkPed, CPed* pPed, CVehicle* pVehicle, CNetworkVehicle* pNetworkVehicle) { do " + sender + " while (false); }")
    helpers = "\n".join(block(read("client/src/CNetworkPed.cpp"), re.escape(signature) + r"[^{}]*\{", True)
        for signature in ("bool CNetworkPed::NextState(", "bool CNetworkPed::AcceptState("))
    (output / "extracted_handlers.h").write_text(helpers + "\n" + handlers, encoding="utf-8")
    for p in ("tests.cpp", "doubles.h"):
        shutil.copyfile(HERE / p, output / p)
    command = ["cl.exe", "/nologo", "/std:c++17", "/EHsc", "/W4", "/DNDEBUG", "/DNOMINMAX",
               "tests.cpp", "/I" + str(source / "shared"), "/I" + str(source / "third_party"),
               "/Fe:tests.exe", "/Fo:tests.obj"]
    batch = output / "compile.cmd"
    batch.write_text('@echo off\ncall "' + str(args.msvc_env) + '" x64_x86 > environment.log 2>&1\n'
                     'if errorlevel 1 exit /b 1\n' + subprocess.list2cmdline(command) + '\n', encoding="utf-8")
    build = subprocess.run(["cmd.exe", "/d", "/c", str(batch)], cwd=output, capture_output=True, text=True)
    (output / "compile.log").write_text(build.stdout + build.stderr)
    record = {"ProductionHashes": before, "TestHashes": tests_before,
              "FrozenTestHashes": {p: sha(output / p) for p in ("tests.cpp", "doubles.h")},
              "ExtractedPacketHash": sha(output / "extracted_packet.h"), "ExtractedHandlersHash": sha(output / "extracted_handlers.h"),
              "CompileCommand": command, "CompileExitCode": build.returncode, "Mutation": args.mutate_authority,
              "RuntimeValidated": False}
    if build.returncode == 0:
        run = subprocess.run([str(output / "tests.exe")], cwd=output, capture_output=True, text=True)
        (output / "test.log").write_text(run.stdout + run.stderr)
        record.update(TestExitCode=run.returncode, Output=run.stdout + run.stderr)
        print(run.stdout)
    else:
        print(build.stdout + build.stderr)
    record["InputsUnchanged"] = before == {p: sha(ROOT / p) for p in INPUTS}
    record["TestsUnchanged"] = tests_before == {p: sha(HERE / p) for p in tests_before}
    (output / "result.json").write_text(json.dumps(record, indent=2) + "\n")
    return 0 if record["InputsUnchanged"] and record["TestsUnchanged"] and record.get("TestExitCode") == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
