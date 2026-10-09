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
             "cutscene_migration_tests", "cutscene_relay_tests"]
    support = ["native_doubles.h"] + [name + ".cpp" for name in tests]
    support_hashes = {name: sha(owned / name) for name in support}
    for name in support:
        shutil.copyfile(owned / name, output / name)
    # All service logic is compiled verbatim. Only includes and the fixed-address
    # foreground read are replaced by explicit native doubles; no hook/body rewrite.
    extraction = {}
    for side in ("client", "server"):
        text = (snapshot / f"{side}/src/CCutsceneVotes.cpp").read_text()
        text = re.sub(r"^#include[^\n]*\n", "", text, flags=re.M)
        if side == "client":
            needle = "*reinterpret_cast<const bool*>(0xC920EC)"
            if text.count(needle) != 1:
                parser.error("Foreground native-read shape changed")
            text = text.replace(needle, "Double::focused")
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
        batch.write_text('@echo off\ncall "' + str(args.msvc_env) + '" -arch=x64 -host_arch=x64 > "'
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
              "Tests": results, "Pass": stable and all(x.get("TestExitCode", 1) == 0 for x in results)}
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    for item in results:
        print(item["Test"], item.get("Output", "Compilation failed").strip())
    print("Pass:", report["Pass"], "Evidence:", output / "result.json")
    return 0 if report["Pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
