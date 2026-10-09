"""Compile owned tests against a frozen, hashed set of actual production inputs."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--input", action="append", required=True,
                        help="Production input relative to source, including transitive headers")
    parser.add_argument("--test", type=Path, required=True,
                        help="Owned test source; no untrusted upstream test execution")
    parser.add_argument("--compiler", type=Path, default=Path(shutil.which("g++") or "g++"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--standard", default="c++14", choices=["c++14", "c++17"])
    parser.add_argument("--include", action="append", default=[], help="Include directory relative to frozen source")
    parser.add_argument("--msvc-env", type=Path, help="VsDevCmd.bat for an isolated cl.exe compile")
    parser.add_argument("--service", help="Production CSessionSync.cpp input to extract with include-only removal")
    parser.add_argument("--support", type=Path, action="append", default=[], help="Owned recorded-double headers")
    parser.add_argument("--case", action="append", default=[], help="Run each named test case in a fresh process")
    parser.add_argument("--opcode-prefix", action="store_true", help="Extract the actual reward-replay early-return prefix")
    parser.add_argument("--punishment-source", type=Path, help="Adjacent native source for the actual PunishPlayer lambda")
    args = parser.parse_args()
    source, output, test = args.source.resolve(), args.output.resolve(), args.test.resolve()
    evidence_root = (source / ".cache" / "session-headless").resolve()
    if output == evidence_root or evidence_root not in output.parents:
        parser.error("Output must be a new child of SOURCE/.cache/session-headless")
    if output.exists():
        parser.error("Output already exists; choose a new run name to preserve old evidence")
    inputs = {}
    for name in args.input:
        relative = Path(name)
        path = (source / relative).resolve()
        if relative.is_absolute() or source not in path.parents or not path.is_file():
            parser.error("Invalid production input: " + name)
        inputs[relative.as_posix()] = path
    test_hash = sha(test)
    before = {name: sha(path) for name, path in inputs.items()}
    output.mkdir(parents=True)
    snapshot = output / "source"
    for name, path in inputs.items():
        target = snapshot / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    frozen = {name: sha(snapshot / name) for name in inputs}
    owned_test = output / "tests.cpp"
    shutil.copyfile(test, owned_test)
    frozen_test_hash = sha(owned_test)
    support_hashes = {}
    for support in args.support:
        support = support.resolve()
        support_hashes[str(support)] = sha(support)
        shutil.copyfile(support, output / support.name)
    extraction = None
    if args.service:
        if args.service not in inputs:
            parser.error("Service must also be a named production input")
        service = (snapshot / args.service).read_text(encoding="utf-8-sig")
        includes = re.findall(r'^#include[^\n]*', service, flags=re.M)
        expected = ['#include "stdafx.h"', '#include "CSessionSync.h"', '#include <CGame.h>', '#include <CPickups.h>']
        if includes != expected:
            parser.error("Service include shape changed; refusing stale extraction")
        extracted = re.sub(r'^#include[^\n]*\n?', '', service, flags=re.M)
        extracted_file = output / "extracted_service.inc"
        extracted_file.write_text(extracted, encoding="utf-8")
        extraction = {"Original": args.service, "RemovedIncludes": includes,
                      "ExtractedHash": sha(extracted_file), "FunctionBodiesChanged": False}
    if args.opcode_prefix:
        op_path = "client/src/COpCodeSync.cpp"
        hdr_path = "client/src/COpCodeSync.h"
        if op_path not in inputs or hdr_path not in inputs:
            parser.error("Opcode prefix requires both named production inputs")
        op_source = (snapshot / op_path).read_text()
        op_header = (snapshot / hdr_path).read_text()
        header_match = re.search(r'struct OpcodeSyncHeader\s*\{[^}]+\};', op_header)
        prefix_match = re.search(r'void COpCodeSync::HandlePacket\(const uint8_t\* buffer, int bufferSize\)\s*\{(.*?)\n\s*if \(bufferSize < sizeof\(header\)', op_source, flags=re.S)
        if not header_match or not prefix_match or "SkipRewardReplay" not in prefix_match[1]:
            parser.error("Opcode prefix shape changed; refusing stale extraction")
        prefix = header_match[0] + "\nvoid ExtractedReplayPrefix(const uint8_t* buffer, int bufferSize)\n{" + prefix_match[1] + "\n++replayReached;\n}\n"
        prefix_file = output / "extracted_replay.inc"
        prefix_file.write_text(prefix, encoding="utf-8")
        extraction["ReplayPrefixHash"] = sha(prefix_file)
    if args.punishment_source:
        native_source = args.punishment_source.resolve()
        support_hashes[str(native_source)] = sha(native_source)
        native_text = native_source.read_text(encoding="utf-8-sig")
        punishment = re.search(r'const auto PunishPlayer = \[&player1, player1Ped\]\(int32 fee\)\s*\{[^}]+\};', native_text)
        if not punishment:
            parser.error("Native punishment lambda shape changed; refusing fake extraction")
        shutil.copyfile(native_source, output / "native_GameLogic.cpp")
        native_file = output / "extracted_punishment.inc"
        native_file.write_text('void ExtractedPunishment(int32_t fee)\n{\nauto& player1 = CWorld::Players[0];\nauto* player1Ped = FindPlayerPed(0);\n'
                              + punishment[0] + '\nPunishPlayer(fee);\n}\n', encoding="utf-8")
        extraction["PunishmentHash"] = sha(native_file)
    include_paths = [snapshot]
    for name in args.include:
        include = (snapshot / name).resolve()
        if snapshot not in include.parents:
            parser.error("Include directory escapes the frozen source")
        include_paths.append(include)
    if args.msvc_env:
        command = ["cl.exe", "/nologo", "/std:" + args.standard, "/EHsc", "/W4", "/WX", "/O2"]
        command += ["/I" + str(path) for path in include_paths]
        command += [str(owned_test), "/Fe:" + str(output / "tests.exe"), "/Fo:" + str(output / "tests.obj")]
        batch = output / "compile.cmd"
        batch.write_text('@echo off\ncall "' + str(args.msvc_env.resolve())
                         + '" -arch=x64 -host_arch=x64 > "' + str(output / "environment.log")
                         + '" 2>&1\nif errorlevel 1 exit /b 1\n'
                         + subprocess.list2cmdline(command) + '\n', encoding="utf-8")
        invocation = ["cmd.exe", "/d", "/c", str(batch)]
    else:
        command = [str(args.compiler.resolve()), "-std=" + args.standard, "-O2",
                   "-Wall", "-Wextra", "-Werror", "-pedantic", "-static"]
        for path in include_paths: command += ["-I", str(path)]
        command += [str(owned_test), "-o", str(output / "tests.exe")]
        invocation = command
    record = {"SourceRoot": str(source), "ProductionHashes": frozen,
              "TestPath": str(test), "TestHash": frozen_test_hash,
              "CompilerCommand": command, "RuntimeValidated": False}
    if extraction: record["ServiceExtraction"] = extraction
    if support_hashes: record["SupportHashes"] = support_hashes
    identity_ok = before == frozen and test_hash == frozen_test_hash
    if identity_ok:
        if args.msvc_env:
            record["CompilerEnvironment"] = str(args.msvc_env.resolve())
            record["CompilerVersion"] = "MSVC; initialized environment and compiler output in logs"
        else:
            version = subprocess.run([str(args.compiler.resolve()), "--version"],
                                     capture_output=True, text=True)
            record["CompilerVersion"] = version.stdout.strip()
        compiled = subprocess.run(invocation, cwd=output, capture_output=True, text=True)
        (output / "compile.log").write_text(compiled.stdout + compiled.stderr, encoding="utf-8")
        record["CompileExitCode"] = compiled.returncode
        if compiled.returncode == 0:
            cases = args.case or [None]
            runs = []
            for case in cases:
                run = subprocess.run([str(output / "tests.exe")] + ([case] if case else []), capture_output=True, text=True)
                runs.append({"Case": case, "ExitCode": run.returncode, "Output": run.stdout + run.stderr})
            (output / "test.log").write_text("\n".join(run["Output"] for run in runs), encoding="utf-8")
            record["TestExitCode"] = 0 if all(run["ExitCode"] == 0 for run in runs) else 1
            if args.case: record["Cases"] = runs
    after = {name: sha(path) for name, path in inputs.items()}
    supports_stable = all(sha(Path(path)) == value for path, value in support_hashes.items())
    record["InputsStable"] = identity_ok and before == after and sha(test) == test_hash and supports_stable
    record["ChangedInputs"] = [name for name in inputs if before[name] != after[name]]
    record["Pass"] = (record["InputsStable"] and record.get("CompileExitCode") == 0
                      and record.get("TestExitCode") == 0)
    (output / "result.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"Pass": record["Pass"], "InputsStable": record["InputsStable"],
                      "Report": str(output / "result.json")}, indent=2))
    return 0 if record["Pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
