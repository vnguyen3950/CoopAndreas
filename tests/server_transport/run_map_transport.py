"""Run isolated real-server ENet map and actor-life tests; never launch GTA."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT / "tests"))
from run_map_sync_tests import extract_class

def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--enet-lib", type=Path, required=True)
    args = parser.parse_args(); output = args.output.resolve()
    if output.exists() or ROOT / ".cache" not in output.parents: parser.error("Use a new directory under .cache")
    output.mkdir(parents=True)
    names = ("shared/config.h", "shared/semver.h", "shared/network/packet.h", "shared/network/packet_types.h",
             "shared/network/packets/map.h", "shared/network/map_sync.h", "shared/network/packets/vitals.h",
             "shared/network/player_vitals.h", "shared/network/packets/players.h", "shared/network/packets/system.h",
             "shared/network/serializable_types.h", "third_party/serialize.h", "server/src/CMapSync.cpp",
             "shared/network/player_animation_sync.h", "shared/network/packets/player_animation.h",
             "server/src/CPlayerAnimationSync.cpp", "server/src/PacketHandlers/player_animation.cpp",
             "server/src/CNetwork.cpp", "server/src/CNetworkPlayerManager.cpp", "server/src/PacketHandlers/map.cpp",
             "server/src/PacketHandlers/players.cpp", "tests/server_transport/map_transport.cpp")
    before = {name:digest(ROOT / name) for name in names}
    for name in names:
        target = output / "source" / name; target.parent.mkdir(parents=True,exist_ok=True); shutil.copyfile(ROOT / name,target)
    shutil.copytree(ROOT / "third_party/enet",output / "source/third_party/enet")
    (output / "sender.inc").write_text(extract_class((output / "source/shared/network/serializable_types.h").read_text(),"struct SenderPlayerId"))
    waypoint = extract_class((output / "source/shared/network/packets/players.h").read_text(),"class PlayerPlaceWaypoint")
    (output / "waypoint.inc").write_text("namespace Packets::Players {\n" + waypoint + "\n}\n")
    respawn = extract_class((output / "source/shared/network/packets/players.h").read_text(),"class RespawnPlayer")
    (output / "respawn.inc").write_text("namespace Packets::Players {\n" + respawn + "\n}\n")
    system = (output / "source/shared/network/packets/system.h").read_text()
    (output / "system.inc").write_text("namespace Packets::System {\n" + "\n".join(extract_class(system,"class " + name) for name in ("PlayerConnected","PlayerHandshake","PlayerAssignHost")) + "\n}\n")
    shutil.copyfile(output / "source/tests/server_transport/map_transport.cpp",output / "test.cpp")
    shutil.copyfile(args.server,output / "server.exe"); shutil.copyfile(args.enet_lib,output / "enet.lib")
    compiler = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat")
    command = ["cl.exe","/nologo","/std:c++17","/EHsc","/O2","/DCOOP_CLIENT","/DNOMINMAX","/D_CRT_SECURE_NO_WARNINGS",
               "/I" + str(output),"/I" + str(output / "source/shared"),"/I" + str(output / "source/third_party"),
               str(output / "test.cpp"),str(output / "enet.lib"),"ws2_32.lib","winmm.lib",
               "/Fe:" + str(output / "test.exe"),"/Fo:" + str(output / "test.obj")]
    (output / "compile.cmd").write_text('@echo off\ncall "' + str(compiler) + '" -arch=x86 -host_arch=x64 > environment.log 2>&1\nif errorlevel 1 exit /b 1\n' + subprocess.list2cmdline(command) + '\n')
    compile_result = subprocess.run(["cmd.exe","/d","/c",str(output / "compile.cmd")],cwd=output,capture_output=True,text=True)
    (output / "compile.log").write_text(compile_result.stdout+compile_result.stderr)
    record = {"ProductionHashes":before,"ServerHash":digest(output / "server.exe"),"CompileExitCode":compile_result.returncode,
              "ActualProductionServer":True,"GameLaunched":False,"GameplayValidated":False}
    if compile_result.returncode == 0:
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1",0)); port = probe.getsockname()[1]
        (output / "server-config.ini").write_text("port = " + str(port) + "\nmaxplayers = 8\n")
        with (output / "server.log").open("w") as log:
            process = subprocess.Popen([str(output / "server.exe")],cwd=output,stdout=log,stderr=subprocess.STDOUT,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                time.sleep(.3)
                test = subprocess.run([str(output / "test.exe"),str(port)],cwd=output,capture_output=True,text=True,timeout=35)
                (output / "test.log").write_text(test.stdout+test.stderr); record.update(TestExitCode=test.returncode,TestOutput=test.stdout+test.stderr,Port=port)
            finally:
                # Only this fixture's own Popen handle, never enumerate/stop user servers.
                process.terminate(); process.wait(timeout=5)
    record["InputsStable"] = before == {name:digest(ROOT / name) for name in names}
    record["Pass"] = record["InputsStable"] and record.get("TestExitCode") == 0
    (output / "result.json").write_text(json.dumps(record,indent=2)); print(json.dumps({k:record.get(k) for k in ("Pass","CompileExitCode","TestExitCode","TestOutput")}))
    return 0 if record["Pass"] else 1
if __name__ == "__main__": raise SystemExit(main())
