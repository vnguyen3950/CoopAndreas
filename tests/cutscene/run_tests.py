"""Frozen production state machines, packets and include-only extracted services; no game access."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="New directory under this worktree/.cache/cutscene-headless")
    parser.add_argument("--supported-exe", type=Path, default=Path(r"C:\Users\Vu\work\gta-coop\game-lab\gta_sa.exe"))
    parser.add_argument("--msvc-env", type=Path, default=Path(
        r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"))
    args = parser.parse_args()
    owned = Path(__file__).resolve().parent
    source = owned.parents[1]
    output = args.output.resolve()
    allowed = source / ".cache" / "cutscene-headless"
    if allowed not in output.parents or output.exists():
        parser.error("Output must be a new child of worktree/.cache/cutscene-headless")
    output.mkdir(parents=True)
    inputs = [
        "third_party/plugin-sdk/plugin_sa/game_sa/CPad.h",
        "third_party/plugin-sdk/plugin_sa/game_sa/CPad.cpp",
        "shared/network/cutscene_votes.h", "shared/network/packets/cutscene.h",
        "shared/network/packet.h", "shared/network/packet_types.h", "third_party/serialize.h",
        "shared/network/session_sync.h", "shared/network/object_sync.h",
        "client/src/CCutsceneVotes.h", "client/src/CCutsceneVotes.cpp", "client/src/Main.cpp",
        "client/src/COpCodeSync.cpp", "client/src/CNetwork.cpp", "client/src/CCore.cpp",
        "client/src/CPacketBuffer.cpp", "client/src/CPacketBuffer.h",
        "client/src/PacketHandlers/cutscene.cpp", "client/src/PacketHandlers/scripts.cpp",
        "client/src/PacketHandlers/system.cpp", "server/src/CCutsceneVotes.h", "server/src/CCutsceneVotes.cpp",
        "server/src/CNetwork.cpp", "server/src/CNetworkPlayerManager.cpp",
        "server/src/PacketHandlers/cutscene.cpp", "server/src/PacketHandlers/scripts.cpp",
        "server/src/PacketHandlers/players.cpp", "server/src/PacketHandlers/vehicles.cpp",
    ]
    hashes = {name: sha(source / name) for name in inputs}
    snapshot = output / "source"
    for name in inputs:
        target = snapshot / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / name, target)
    tests = ["cutscene_votes_tests", "cutscene_packet_tests", "cutscene_client_tests", "cutscene_server_tests",
             "cutscene_migration_tests", "cutscene_relay_tests", "cutscene_input_tests"]
    support = ["native_doubles.h"] + [name + ".cpp" for name in tests]
    support_hashes = {name: sha(owned / name) for name in support}
    for name in support:
        shutil.copyfile(owned / name, output / name)
    # Keyboard/controller layouts come from the actual SDK; no invented field layout.
    sdk_pad = (snapshot / "third_party/plugin-sdk/plugin_sa/game_sa/CPad.h").read_text()
    layouts = []
    for name in ("CControllerState", "CMouseControllerState", "CKeyboardState"):
        match = re.search(r"class " + name + r"\s*\{.*?\n\};", sdk_pad, flags=re.S)
        if not match:
            parser.error("SDK input layout changed: " + name)
        layouts.append(match[0])
    (output / "native_input_layout.inc").write_text("\n".join(layouts))
    sdk_refs = (snapshot / "third_party/plugin-sdk/plugin_sa/game_sa/CPad.cpp").read_text()
    if "CPad::NewKeyState = *(CKeyboardState *)0xB73190;" not in sdk_refs:
        parser.error("SDK keyboard reference changed")
    # Read supported game bytes only: validate the native Space and foreground cells.
    import struct
    native = args.supported_exe.resolve()
    native_hash = sha(native)
    if native_hash.upper() != "A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26":
        parser.error("Supported executable identity differs")
    binary = native.read_bytes()
    pe = struct.unpack_from("<I", binary, 0x3c)[0]
    sections = struct.unpack_from("<H", binary, pe + 6)[0]
    optional_size = struct.unpack_from("<H", binary, pe + 20)[0]
    optional = pe + 24
    image_base = struct.unpack_from("<I", binary, optional + 28)[0]
    def native_bytes(address, length):
        rva = address - image_base
        for index in range(sections):
            virtual_size, start, raw_size, raw = struct.unpack_from("<IIII", binary, optional + optional_size + 40 * index + 8)
            if start <= rva < start + max(virtual_size, raw_size) and rva - start + length <= raw_size:
                return binary[raw + rva - start:raw + rva - start + length]
        parser.error("Native address not backed by disk bytes")
    key_bytes = native_bytes(0x4D5D80, 44)
    if not key_bytes.startswith(bytes.fromhex("66 83 3D E8 31 B7 00 00")) or bytes.fromhex("66 83 3D 78 2F B7 00 00") not in key_bytes:
        parser.error("Native Space keyboard-cell proof differs")
    foreground_bytes = native_bytes(0x746070, 12)
    if bytes.fromhex("E8 D7 02 27 00") not in key_bytes or not foreground_bytes.startswith(bytes.fromhex("A1 EC 20 C9 00 85 C0 0F 95 C0 C3")):
        parser.error("Native foreground-cell proof differs")
    native_proof = {"Path": str(native), "SHA256": native_hash, "ReadOnly": True,
                    "SpaceNew": "0xB731E8", "SpaceOld": "0xB72F78", "Foreground": "0xC920EC",
                    "NativeQueryAddress": "0x4D5D10", "NativeQueryABI": "bool __cdecl()",
                    "SpaceQueryBytes": key_bytes.hex(" "), "ForegroundQueryBytes": foreground_bytes.hex(" ")}
    (output / "native-input-proof.json").write_text(json.dumps(native_proof, indent=2))
    # All service logic is compiled verbatim. Only includes and the fixed-address
    # foreground read are replaced by explicit native doubles; no hook/body rewrite.
    extraction = {}
    for side in ("client", "server"):
        text = (snapshot / f"{side}/src/CCutsceneVotes.cpp").read_text()
        text = re.sub(r"^#include[^\n]*\n", "", text, flags=re.M)
        if side == "client":
            old_read = "*reinterpret_cast<const bool*>(0xC920EC)"
            new_read = "*reinterpret_cast<const uint32_t*>(0xC920EC)"
            if text.count(old_read) + text.count(new_read) != 1:
                parser.error("Foreground native-read shape changed")
            text = text.replace(old_read, "(Double::focused && ((Double::foreground & 0xffu) != 0))")
            text = text.replace(new_read, "(Double::focused ? Double::foreground : 0u)")
        path = output / f"extracted_{side}.inc"
        path.write_text(text)
        extraction[side] = sha(path)
    text = (snapshot / "client/src/Main.cpp").read_text()
    deferred = re.search(r"if \(COpCodeSync::ms_bLoadingCutscene[^}]+Command<Commands::START_CUTSCENE>\(\);\s*}", text)
    if not deferred:
        parser.error("Main deferred START shape changed")
    path = output / "extracted_deferred.inc"
    path.write_text("void ProcessActualDeferredStart() {\n" + deferred[0] + "\n}\n")
    extraction["deferred_start"] = sha(path)
    text = (snapshot / "client/src/CPacketBuffer.cpp").read_text()
    receive = text.split("void CPacketBuffer::Receive(Packet* pPacket)", 1)[1].split("void CPacketBuffer::Process()", 1)[0]
    path = output / "extracted_receive.inc"
    path.write_text("void CPacketBuffer::Receive(Packet* pPacket)" + receive)
    extraction["timestamp_receive"] = sha(path)
    text = (snapshot / "server/src/PacketHandlers/scripts.cpp").read_text()
    relay = text.split("PACKET_HANDLER(ePacketType::OPCODE_SYNC", 1)[1].split("\n{", 1)[1]
    relay = relay.split("PACKET_HANDLER(ePacketType::PERFORM_TASK_SEQUENCE", 1)[0]
    path = output / "extracted_relay.inc"
    path.write_text("void Relay(Packets::Scripts::OpCodeSync* pOpCodeSync, CNetworkPlayer* pNetworkPlayer)\n{" + relay)
    extraction["opcode_relay"] = sha(path)
    env = os.environ.copy()
    env.pop("GTA_SA_DIR", None)
    results = []
    for name in tests:
        command = ["cl.exe", "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", "/O2"]
        for path in (snapshot, snapshot / "shared", snapshot / "third_party", output):
            command += ["/I" + str(path)]
        command += [str(output / (name + ".cpp")), "/Fe:" + str(output / (name + ".exe")), "/Fo:" + str(output / (name + ".obj"))]
        batch = output / (name + ".cmd")
        batch.write_text('@echo off\ncall "' + str(args.msvc_env) + '" -arch=x86 -host_arch=x64 > "'
                         + str(output / (name + "-environment.log")) + '" 2>&1\nif errorlevel 1 exit /b 1\n'
                         + subprocess.list2cmdline(command) + "\n")
        compiled = subprocess.run(["cmd.exe", "/d", "/c", str(batch)], cwd=output, env=env, capture_output=True, text=True)
        log = compiled.stdout + compiled.stderr
        (output / (name + "-compile.log")).write_text(log)
        item = {"Test": name, "CompileExitCode": compiled.returncode, "CompilerCommand": command}
        if compiled.returncode == 0:
            run = subprocess.run([str(output / (name + ".exe"))], cwd=output, env=env, capture_output=True, text=True)
            item.update(TestExitCode=run.returncode, Output=run.stdout + run.stderr)
            (output / (name + "-test.log")).write_text(item["Output"])
        else:
            print(log)
        results.append(item)
    stable = all(sha(source / name) == value for name, value in hashes.items())
    stable = stable and all(sha(owned / name) == value for name, value in support_hashes.items())
    report = {"ProductionHashes": hashes, "SupportHashes": support_hashes, "ExtractionHashes": extraction,
              "InputsStable": stable, "GTA_SA_DIR_Cleared": True, "RuntimeValidated": False,
              "NativeInputProof": native_proof, "NativeInputStable": sha(native) == native_hash,
              "Tests": results, "Pass": stable and sha(native) == native_hash and all(x.get("TestExitCode", 1) == 0 for x in results)}
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    for item in results:
        print(item["Test"], item.get("Output", "Compilation failed").strip())
    print("Pass:", report["Pass"], "Evidence:", output / "result.json")
    return 0 if report["Pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
