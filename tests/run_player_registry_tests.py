"""Test actual registry, lifecycle and SYSTEM handler bodies with native doubles."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
INPUTS = ("shared/config.h", "client/src/CNetworkPlayer.cpp", "client/src/CNetworkPlayer.h",
          "client/src/CNetworkPlayerManager.cpp", "client/src/CNetworkPlayerManager.h",
          "client/src/PacketHandlers/system.cpp", "client/src/CNetwork.cpp", "client/src/Main.cpp",
          "third_party/plugin-sdk/plugin_sa/game_sa/CPlayerPed.h",
          "third_party/plugin-sdk/plugin_sa/game_sa/CPed.h",
          "third_party/plugin-sdk/plugin_sa/game_sa/CPed.cpp",
          "third_party/plugin-sdk/plugin_sa/game_sa/CPlaceable.h")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def extract(source, signature):
    start = source.index(signature); opening = source.index("{", start)
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', lambda m: " " * len(m[0]), source, flags=re.S)
    depth = 1
    for index in range(opening + 1, len(source)):
        if masked[index] == "{": depth += 1
        if masked[index] == "}": depth -= 1
        if depth == 0: return source[start:index+1]
    raise ValueError("Unbalanced source function " + signature)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--msvc-env", type=Path, default=Path(
        r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"))
    args = parser.parse_args(); output = args.output.resolve()
    if output.exists() or ROOT / ".cache" not in output.parents:
        parser.error("Output must be a new ignored evidence directory in this worktree")
    output.mkdir(parents=True)
    before = {name: digest(ROOT / name) for name in INPUTS}
    for name in INPUTS:
        path = output / "source" / name; path.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(ROOT / name, path)
    frozen = {name: digest(output / "source" / name) for name in INPUTS}
    support = ROOT / "tests/player_registry_doubles.h"; test = ROOT / "tests/player_registry_tests.cpp"
    support_hash, test_hash = digest(support), digest(test)
    shutil.copyfile(support, output / support.name); shutil.copyfile(test, output / "tests.cpp")
    player = (output / "source/client/src/CNetworkPlayer.cpp").read_text()
    functions = [extract(player, signature) for signature in (
        "CNetworkPlayer::~CNetworkPlayer()", "CNetworkPlayer::CNetworkPlayer(int id, CVector position)",
        "void CNetworkPlayer::CreatePed(", "void CNetworkPlayer::DestroyPed()",
        "void CNetworkPlayer::Respawn()", "int CNetworkPlayer::GetInternalId()")]
    manager = (output / "source/client/src/CNetworkPlayerManager.cpp").read_text()
    functions.append(re.sub(r'^#include[^\n]*\n?', '', manager, flags=re.M))
    handlers = (output / "source/client/src/PacketHandlers/system.cpp").read_text()
    for packet,signature in (
        ("PLAYER_CONNECTED", "void HandleConnected(Packets::System::PlayerConnected* pPlayerConnected)"),
        ("PLAYER_DISCONNECTED", "void HandleDisconnected(Packets::System::PlayerDisconnected* pPlayerDisconnected)"),
        ("PLAYER_HANDSHAKE", "void HandleHandshake(Packets::System::PlayerHandshake* pPlayerHandshake)")):
        body = extract(handlers, "PACKET_HANDLER(ePacketType::" + packet)
        functions.append(signature + "\n" + body[body.index("{"):])
    generated = output / "registry_functions.inc"; generated.write_text("\n\n".join(functions), encoding="utf-8")
    # Source-level wiring proof is distinct from recorded execution of the bodies.
    process = extract(manager, "void CNetworkPlayerManager::ProcessPendingReset()")
    reset = extract(manager, "void CNetworkPlayerManager::Reset()")
    wiring = {"SingleResetConsumer": "exchange" not in process and reset.count("exchange") == 1,
              "RemoveBeforeConstruction": handlers.index("RemoveById") < handlers.index("new CNetworkPlayer"),
              "NullLookupGuard": "if (!m_pPed) return -1;" in extract(player,"int CNetworkPlayer::GetInternalId()")}
    command = ["cl.exe", "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", "/O2", "/D_CRT_SECURE_NO_WARNINGS",
               "/I" + str(output / "source"), "/I" + str(output / "source/shared"),
               str(output / "tests.cpp"), "/Fe:" + str(output / "tests.exe"), "/Fo:" + str(output / "tests.obj")]
    batch = '@echo off\ncall "' + str(args.msvc_env.resolve()) + '" -arch=x86 -host_arch=x64 > environment.log 2>&1\n'
    batch += 'if errorlevel 1 exit /b 1\ncl.exe /Bv > compiler-version.log 2>&1\n' + subprocess.list2cmdline(command) + '\n'
    (output / "compile.cmd").write_text(batch,encoding="utf-8")
    result = {"ProductionHashes": frozen, "TestHash":test_hash,"DoubleHash":support_hash,
              "ExtractedHash":digest(generated), "CompilerCommand":command,"SourceWiring":wiring,"RuntimeValidated":False}
    if before == frozen:
        compiled = subprocess.run(["cmd.exe","/d","/c",str(output / "compile.cmd")],cwd=output,capture_output=True,text=True)
        (output / "compile.log").write_text(compiled.stdout+compiled.stderr,encoding="utf-8"); result["CompileExitCode"] = compiled.returncode
        if compiled.returncode == 0:
            runs = []
            for case in ("duplicate","pending","invalid","creation"):
                run = subprocess.run([str(output / "tests.exe"),case],capture_output=True,text=True)
                runs.append({"Case":case,"ExitCode":run.returncode,"Output":run.stdout+run.stderr})
            (output / "test.log").write_text("\n".join(run["Output"] for run in runs),encoding="utf-8")
            result["Cases"] = runs; result["TestExitCode"] = 0 if all(run["ExitCode"]==0 for run in runs) else 1
    after = {name:digest(ROOT / name) for name in INPUTS}
    result["InputsStable"] = before == frozen == after and digest(support)==support_hash and digest(test)==test_hash
    result["Pass"] = result["InputsStable"] and result.get("CompileExitCode")==0 and result.get("TestExitCode")==0 and all(wiring.values())
    (output / "result.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({"Pass":result["Pass"],"Evidence":str(output / "result.json")},indent=2))
    return 0 if result["Pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
