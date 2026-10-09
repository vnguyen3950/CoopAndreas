"""Check compiled packet IDs and names against the frozen 0.5 wire registry."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
HERE = Path(__file__).resolve().parent


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", default="g++")
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or ROOT / ".cache" not in output.parents:
        parser.error("Use a new directory beneath .cache")
    output.mkdir(parents=True)
    source = ROOT / "shared/network/packet_types.h"
    baseline_path = HERE / "packet_registry_baseline.json"
    baseline = json.loads(baseline_path.read_text())
    before = {str(path.relative_to(ROOT)): digest(path) for path in (source, baseline_path, Path(__file__))}
    header = source.read_text()
    body = re.search(r"enum class ePacketType[^{}]*\{([^}]*)\}", header).group(1)
    body = re.sub(r"//[^\n]*|/\*.*?\*/", "", body, flags=re.S)
    names = [part.strip() for part in body.split(",") if part.strip()]
    if names[-1] != "PACKET_ID_MAX" or any(not re.fullmatch(r"[A-Z][A-Z_0-9]*", name) for name in names):
        parser.error("Registry extraction requires the current simple contiguous enum")
    names.pop()
    shutil.copyfile(source, output / "packet_types.h")
    assertions = "\n".join(
        f'static_assert(unsigned(ePacketType::{name}) == {index}, "Frozen packet ID changed: {name}");'
        for index, name in enumerate(baseline["Names"])
    )
    expected = ",\n".join(json.dumps(name) for name in names)
    cpp = f'''#include <cstring>
#include <iostream>
#include "packet_types.h"
{assertions}
static_assert(unsigned(ePacketType::PACKET_ID_MAX) == {len(names)}, "Enum extraction differs from compiled values");
int main() {{
    const char* expected[] = {{{expected}}};
    unsigned failures = 0;
    for (unsigned i = 0; i < unsigned(ePacketType::PACKET_ID_MAX); ++i) {{
        const char* actual = ePacketType_ToString(static_cast<ePacketType>(i));
        if (!actual || std::strcmp(actual, expected[i])) {{
            ++failures; std::cout << "Incorrect packet label at ID " << i << '\\n';
        }}
    }}
    std::cout << {len(baseline['Names'])} << " frozen IDs compile; " << {len(names)}
              << " compiled packet labels, " << failures << " failures\\n";
    return failures ? 1 : 0;
}}
'''
    (output / "registry.cpp").write_text(cpp)
    command = [args.compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", str(output / "registry.cpp"), "-o", str(output / "registry.exe")]
    compiled = subprocess.run(command, cwd=output, capture_output=True, text=True)
    (output / "compile.log").write_text(compiled.stdout + compiled.stderr)
    report = {"ProductionAndSupportHashes": before, "CompileExitCode": compiled.returncode,
              "BaselineSourceCommit": baseline["SourceCommit"], "FrozenIDs": len(baseline["Names"]),
              "CurrentPacketCount": len(names), "NativeRuntimeValidated": False}
    if not compiled.returncode:
        run = subprocess.run([str(output / "registry.exe")], cwd=output, capture_output=True, text=True)
        report.update(TestExitCode=run.returncode, Output=run.stdout + run.stderr)
        (output / "test.log").write_text(report["Output"])
    report["InputsStable"] = all(digest(ROOT / path) == value for path, value in before.items())
    report["Pass"] = report["InputsStable"] and report.get("TestExitCode") == 0
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(report.get("Output", compiled.stdout + compiled.stderr).strip())
    return 0 if report["Pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
