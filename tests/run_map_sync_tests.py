"""Hash-bound real map packet/service tests, with native/ENet doubles only."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
INPUTS = ("shared/network/map_sync.h", "shared/network/packets/map.h", "shared/network/packets/players.h",
          "shared/network/packet.h", "shared/network/packet_types.h", "shared/network/serializable_types.h",
          "shared/network/player_vitals.h", "shared/config.h", "third_party/serialize.h",
          "client/src/CMapSync.h", "client/src/CMapSync.cpp", "server/src/CMapSync.h", "server/src/CMapSync.cpp")

def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def extract_class(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', lambda m: " " * len(m[0]), text, flags=re.S)
    depth = 1
    for index in range(opening + 1, len(masked)):
        if masked[index] == "{": depth += 1
        if masked[index] == "}": depth -= 1
        if depth == 0: return text[start:index + 2]
    raise ValueError(signature)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mutation", action="store_true", help="Drop waypoint generation check only in frozen copy")
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or ROOT / ".cache" not in output.parents: parser.error("Choose a new directory within .cache")
    output.mkdir(parents=True)
    inputs = INPUTS + ("tests/map_sync_doubles.h", "tests/map_sync_tests.cpp", "tests/map_waypoint_codec_tests.cpp")
    before = {name: digest(ROOT / name) for name in inputs}
    for name in inputs:
        target = output / "source" / name; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, target)
    frozen = {name: digest(output / "source" / name) for name in inputs}
    for role in ("client", "server"):
        text = (output / "source" / role / "src/CMapSync.cpp").read_text(encoding="utf-8-sig")
        (output / ("map_" + role + ".inc")).write_text(re.sub(r'^#include[^\n]*\n?', '', text, flags=re.M), encoding="utf-8")
    sender = extract_class((output / "source/shared/network/serializable_types.h").read_text(), "struct SenderPlayerId")
    (output / "sender.inc").write_text(sender, encoding="utf-8")
    waypoint = extract_class((output / "source/shared/network/packets/players.h").read_text(), "class PlayerPlaceWaypoint")
    (output / "waypoint.inc").write_text("namespace Packets::Players {\n" + waypoint + "\n}\n", encoding="utf-8")
    if args.mutation:
        path = output / "source/shared/network/map_sync.h"
        path.write_text(path.read_text().replace("generation == boundGeneration", "generation != 0", 1), encoding="utf-8")
    shutil.copyfile(output / "source/tests/map_sync_doubles.h", output / "map_sync_doubles.h")
    shutil.copyfile(output / "source/tests/map_sync_tests.cpp", output / "tests.cpp")
    shutil.copyfile(output / "source/tests/map_waypoint_codec_tests.cpp", output / "waypoint_tests.cpp")
    env = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat")
    command = ["cl.exe", "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", "/O2", "/DCOOP_SERVER",
               "/I" + str(output / "source"), "/I" + str(output / "source/shared"), "/I" + str(output / "source/third_party"),
               str(output / "tests.cpp"), "/Fe:" + str(output / "tests.exe"), "/Fo:" + str(output / "tests.obj")]
    (output / "compile.cmd").write_text('@echo off\ncall "' + str(env) + '" -arch=x86 -host_arch=x64 > environment.log 2>&1\nif errorlevel 1 exit /b 1\n' + subprocess.list2cmdline(command) + '\n')
    compile_result = subprocess.run(["cmd.exe", "/d", "/c", str(output / "compile.cmd")], cwd=output, capture_output=True, text=True)
    (output / "compile.log").write_text(compile_result.stdout + compile_result.stderr, encoding="utf-8")
    runs = []
    if compile_result.returncode == 0:
        for case in ("contracts", "codecs", "server", "client", "menu-migration", "seed-ack"):
            result = subprocess.run([str(output / "tests.exe"), case], cwd=output, capture_output=True, text=True)
            runs.append({"Case": case, "ExitCode": result.returncode, "Output": result.stdout + result.stderr})
        for role in ("client", "server"):
            role_command = [part.replace("/DCOOP_SERVER", "/DCOOP_" + role.upper())
                            .replace("tests.cpp", "waypoint_tests.cpp")
                            .replace("tests.exe", "waypoint_" + role + ".exe")
                            .replace("tests.obj", "waypoint_" + role + ".obj") for part in command]
            batch = '@echo off\ncall "' + str(env) + '" -arch=x86 -host_arch=x64 > environment.log 2>&1\nif errorlevel 1 exit /b 1\n' + subprocess.list2cmdline(role_command) + '\n'
            role_script = output / ("compile_" + role + ".cmd"); role_script.write_text(batch)
            result = subprocess.run(["cmd.exe", "/d", "/c", str(role_script)], cwd=output, capture_output=True, text=True)
            (output / ("compile_" + role + ".log")).write_text(result.stdout + result.stderr)
            if result.returncode: runs.append({"Case": "compile-waypoint-" + role, "ExitCode": result.returncode, "Output": result.stdout + result.stderr})
        if (output / "waypoint_client.exe").exists() and (output / "waypoint_server.exe").exists():
            for role, mode in (("client", "write"), ("server", "read"), ("server", "write"), ("client", "read")):
                result = subprocess.run([str(output / ("waypoint_" + role + ".exe")), mode], cwd=output, capture_output=True, text=True)
                runs.append({"Case": "waypoint-" + role + "-" + mode, "ExitCode": result.returncode, "Output": result.stdout + result.stderr})
    (output / "test.log").write_text("\n".join(r["Output"] for r in runs), encoding="utf-8")
    stable = before == frozen == {name: digest(ROOT / name) for name in inputs}
    passed = compile_result.returncode == 0 and bool(runs) and all(r["ExitCode"] == 0 for r in runs) and stable
    record = {"Pass": passed, "InputsStable": stable, "FrozenHashes": frozen, "CompileExitCode": compile_result.returncode,
              "Cases": runs, "Mutation": args.mutation, "RuntimeValidated": False,
              "BoundaryDoubles": "Native player/pool/events/zone fields and ENet transport; unchanged complete map and waypoint classes and services."}
    (output / "result.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
    print(json.dumps({"Pass": passed, "Evidence": str(output / "result.json"), "CompileExitCode": compile_result.returncode, "Cases": runs}))
    return 0 if passed else 1

if __name__ == "__main__": raise SystemExit(main())
