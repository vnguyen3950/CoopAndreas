"""Freeze production vitals inputs and compile tests with the real serializer."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
INPUTS = ("shared/network/player_vitals.h", "shared/network/packets/vitals.h",
          "shared/network/packet.h", "shared/network/packet_types.h", "shared/config.h", "third_party/serialize.h")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--msvc-env", type=Path, default=Path(
        r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"))
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or ROOT / ".cache" not in output.parents:
        parser.error("Choose a new evidence directory within this worktree's ignored .cache")
    output.mkdir(parents=True)
    before = {name: digest(ROOT / name) for name in INPUTS}
    for name in INPUTS:
        target = output / "source" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, target)
    test = ROOT / "tests/player_vitals_tests.cpp"
    test_hash = digest(test)
    shutil.copyfile(test, output / "tests.cpp")
    frozen = {name: digest(output / "source" / name) for name in INPUTS}
    command = ["cl.exe", "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", "/O2",
               "/I" + str(output / "source/shared"), "/I" + str(output / "source/third_party"),
               str(output / "tests.cpp"), "/Fe:" + str(output / "tests.exe"), "/Fo:" + str(output / "tests.obj")]
    batch = '@echo off\ncall "' + str(args.msvc_env.resolve()) + '" -arch=x86 -host_arch=x64 > environment.log 2>&1\n'
    batch += 'if errorlevel 1 exit /b 1\ncl.exe /Bv > compiler-version.log 2>&1\n'
    batch += subprocess.list2cmdline(command) + '\n'
    (output / "compile.cmd").write_text(batch, encoding="utf-8")
    record = {"ProductionHashes": frozen, "TestHash": test_hash, "CompilerCommand": command,
              "RuntimeValidated": False}
    if before == frozen:
        compile_result = subprocess.run(["cmd.exe", "/d", "/c", str(output / "compile.cmd")],
                                        cwd=output, capture_output=True, text=True)
        (output / "compile.log").write_text(compile_result.stdout + compile_result.stderr, encoding="utf-8")
        record["CompileExitCode"] = compile_result.returncode
        if compile_result.returncode == 0:
            result = subprocess.run([str(output / "tests.exe")], capture_output=True, text=True)
            (output / "test.log").write_text(result.stdout + result.stderr, encoding="utf-8")
            record["TestExitCode"] = result.returncode
    after = {name: digest(ROOT / name) for name in INPUTS}
    record["InputsStable"] = before == frozen == after and digest(test) == test_hash
    record["Pass"] = record["InputsStable"] and record.get("CompileExitCode") == 0 and record.get("TestExitCode") == 0
    (output / "result.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"Pass": record["Pass"], "Evidence": str(output / "result.json")}, indent=2))
    return 0 if record["Pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
