"""Compile the actual packet-buffer class and methods with boundary doubles."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    allowed = (ROOT / ".cache" / "packet-buffer").resolve()
    if allowed not in output.parents or output.exists():
        parser.error("Choose a new child of .cache/packet-buffer")
    inputs = [ROOT / "client/src/CPacketBuffer.h", ROOT / "client/src/CPacketBuffer.cpp",
              ROOT / "tests/packet_buffer_tests.cpp"]
    before = {str(p.relative_to(ROOT)): sha(p) for p in inputs}
    output.mkdir(parents=True)
    parts = []
    for path in inputs[:2]:
        text = path.read_text()
        text = re.sub(r"^\s*#(?:include|pragma once)[^\n]*\n", "", text, flags=re.M)
        if path.suffix == ".h":
            text = text[:text.index("extern inline CPacketBuffer& GetPacketBuffer()")]
        parts.append(text)
    (output / "extracted_packet_buffer.inc").write_text("\n".join(parts))
    shutil.copyfile(inputs[2], output / "tests.cpp")
    command = [shutil.which("g++") or "g++", "-std=c++17", "-O2", "-Wall", "-Wextra",
               "-Werror", "-pedantic", "-static", "tests.cpp", "-o", "tests.exe"]
    compiled = subprocess.run(command, cwd=output, capture_output=True, text=True)
    (output / "compile.log").write_text(compiled.stdout + compiled.stderr)
    result = {"SourceHashes": before, "CompilerCommand": command,
              "CompileExitCode": compiled.returncode, "RuntimeValidated": False}
    if compiled.returncode == 0:
        run = subprocess.run([str(output / "tests.exe")], cwd=output, capture_output=True, text=True)
        result.update(TestExitCode=run.returncode, Output=run.stdout + run.stderr)
        print(result["Output"].strip())
    else:
        print(compiled.stdout + compiled.stderr)
    result["InputsUnchanged"] = all(sha(p) == before[str(p.relative_to(ROOT))] for p in inputs)
    result["Pass"] = result["InputsUnchanged"] and compiled.returncode == 0 and result.get("TestExitCode") == 0
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    return 0 if result["Pass"] else 1

if __name__ == "__main__":
    raise SystemExit(main())
