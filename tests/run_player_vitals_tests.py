"""Freeze production vitals inputs and compile tests with the real serializer."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
INPUTS = ("shared/network/player_vitals.h", "shared/network/packets/vitals.h",
          "shared/network/packet.h", "shared/network/packet_types.h", "shared/config.h", "third_party/serialize.h")
SERVICE_INPUTS = ("client/src/CPlayerVitalsSync.cpp", "client/src/CPlayerVitalsSync.h",
                  "server/src/CPlayerVitalsSync.cpp", "server/src/CPlayerVitalsSync.h",
                  "client/src/CNetworkPlayer.cpp", "client/src/UI/CNetworkPlayerList.cpp",
                  "client/src/UI/CNetworkPlayerList.h", "client/src/UI/CNetworkPlayerNameTag.cpp",
                  "client/src/Hooks/PlayerHooks.cpp", "client/src/Hooks/VehicleHooks.cpp",
                  "client/src/CCore.cpp", "client/src/Main.cpp", "server/src/CNetwork.cpp",
                  "client/src/CNetworkPlayerManager.cpp",
                  "third_party/plugin-sdk/plugin_sa/game_sa/CHudColours.h")


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"',
                    lambda match: " " * len(match[0]), source, flags=re.S)
    depth = 1
    for index in range(opening + 1, len(source)):
        if masked[index] == "{": depth += 1
        if masked[index] == "}": depth -= 1
        if depth == 0: return source[start:index+1]
    raise ValueError("Unbalanced production function " + signature)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--suite", choices=["codec", "service"], default="codec")
    parser.add_argument("--msvc-env", type=Path, default=Path(
        r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"))
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or ROOT / ".cache" not in output.parents:
        parser.error("Choose a new evidence directory within this worktree's ignored .cache")
    output.mkdir(parents=True)
    inputs = INPUTS + (SERVICE_INPUTS if args.suite == "service" else ())
    before = {name: digest(ROOT / name) for name in inputs}
    for name in inputs:
        target = output / "source" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, target)
    test = ROOT / ("tests/player_vitals_service_tests.cpp" if args.suite == "service" else "tests/player_vitals_tests.cpp")
    test_hash = digest(test)
    shutil.copyfile(test, output / "tests.cpp")
    frozen = {name: digest(output / "source" / name) for name in inputs}
    extraction = {}
    support_hash = None
    if args.suite == "service":
        support = ROOT / "tests/player_vitals_doubles.h"
        support_hash = digest(support); shutil.copyfile(support, output / support.name)
        for role in ("client", "server"):
            name = role + "/src/CPlayerVitalsSync.cpp"
            source = (output / "source" / name).read_text(encoding="utf-8-sig")
            includes = re.findall(r'^#include[^\n]*',source,flags=re.M)
            if includes != ['#include "stdafx.h"', '#include "CPlayerVitalsSync.h"']:
                raise ValueError("Refusing stale service extraction: " + name)
            text = re.sub(r'^#include[^\n]*\n?', '', source, flags=re.M)
            target = output / ("vitals_" + role + ".inc")
            target.write_text(text,encoding="utf-8"); extraction[name] = {"IncludesOnlyRemoved":includes,"Hash":digest(target)}
        for name,signature,target in (
            ("client/src/CNetworkPlayer.cpp","int CNetworkPlayer::GetInternalId()","vitals_ped_binding.inc"),
            ("client/src/CNetworkPlayerManager.cpp","void CNetworkPlayerManager::RemoveById(","vitals_registry_remove.inc"),
            ("client/src/UI/CNetworkPlayerList.cpp","void CNetworkPlayerList::DrawBars(","vitals_bars.inc")):
            text = function((output / "source" / name).read_text(),signature)
            (output / target).write_text(text,encoding="utf-8")
            extraction[name] = {"ActualFunction":signature,"Hash":digest(output / target)}
        colors = (output / "source/third_party/plugin-sdk/plugin_sa/game_sa/CHudColours.h").read_text()
        enum = re.search(r'enum\s+eHudColours\s*\{[^}]+\};', colors)
        if not enum: raise ValueError("SDK HUD enum shape changed")
        (output / "vitals_hud_colors.inc").write_text(enum[0],encoding="utf-8")
    command = ["cl.exe", "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", "/O2",
               "/I" + str(output / "source"), "/I" + str(output / "source/shared"), "/I" + str(output / "source/third_party"),
               str(output / "tests.cpp"), "/Fe:" + str(output / "tests.exe"), "/Fo:" + str(output / "tests.obj")]
    batch = '@echo off\ncall "' + str(args.msvc_env.resolve()) + '" -arch=x86 -host_arch=x64 > environment.log 2>&1\n'
    batch += 'if errorlevel 1 exit /b 1\ncl.exe /Bv > compiler-version.log 2>&1\n'
    batch += subprocess.list2cmdline(command) + '\n'
    (output / "compile.cmd").write_text(batch, encoding="utf-8")
    record = {"ProductionHashes": frozen, "TestHash": test_hash, "CompilerCommand": command,
              "RuntimeValidated": False,"Suite":args.suite}
    if extraction: record["ProductionExtractions"] = extraction; record["DoubleHeaderHash"] = support_hash
    if before == frozen:
        compile_result = subprocess.run(["cmd.exe", "/d", "/c", str(output / "compile.cmd")],
                                        cwd=output, capture_output=True, text=True)
        (output / "compile.log").write_text(compile_result.stdout + compile_result.stderr, encoding="utf-8")
        record["CompileExitCode"] = compile_result.returncode
        if compile_result.returncode == 0:
            cases = ["menu","spawn_bars","focus","recycle","reconnect","native_transients","server_replay","duplicate_wrapper"] if args.suite == "service" else [None]
            runs = []
            for case in cases:
                result = subprocess.run([str(output / "tests.exe")] + ([case] if case else []), capture_output=True, text=True)
                runs.append({"Case":case,"ExitCode":result.returncode,"Output":result.stdout+result.stderr})
            (output / "test.log").write_text("\n".join(run["Output"] for run in runs), encoding="utf-8")
            record["TestExitCode"] = 0 if all(run["ExitCode"] == 0 for run in runs) else 1
            record["Cases"] = runs
    after = {name: digest(ROOT / name) for name in inputs}
    record["InputsStable"] = before == frozen == after and digest(test) == test_hash
    if support_hash: record["InputsStable"] &= digest(support) == support_hash
    record["Pass"] = record["InputsStable"] and record.get("CompileExitCode") == 0 and record.get("TestExitCode") == 0
    (output / "result.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"Pass": record["Pass"], "Evidence": str(output / "result.json")}, indent=2))
    return 0 if record["Pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
