"""Extract the current extension codec and run a standalone, headless C++ test.

Production source is read only. Generated files and the executable live in a unique
temporary directory, never in the game directory or shared build output.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADERS = (
    "shared/network/packets/vehicles.h",
    "third_party/serialize.h",
    "shared/config.h",
)
FIELDS = ("dirtLevel", "engineState", "lightState", "engineBroken", "sirenOrAlarm", "alarmState")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def class_body(source: str, name: str) -> str:
    match = re.search(r"\bclass\s+" + re.escape(name) + r"\s*:\s*public\s+Packet\s*\{", source)
    if not match:
        raise ValueError(f"Cannot locate the current {name} declaration")
    start = match.end()
    depth = 1
    # Remove comments/quoted strings only for brace matching; preserve original text.
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"',
                    lambda m: " " * len(m[0]), source, flags=re.S)
    for i in range(start, len(source)):
        if masked[i] == "{":
            depth += 1
        elif masked[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start:i]
    raise ValueError(f"Unbalanced declaration for {name}")


def extract(body: str, name: str) -> tuple[str, dict]:
    declarations = []
    for field in FIELDS:
        matches = re.findall(r"(?m)^\s*((?:float|bool|uint16_t)\s+" + field + r"\s*\{\s*\};)\s*$", body)
        if len(matches) != 1:
            raise ValueError(f"{name}: expected one initialized declaration for {field}")
        declarations.append(matches[0])
    dirt = re.findall(
        r"if\s*\(\s*Stream::IsWriting\s*\)\s*\{[^{}]*\bdirtLevel\b[^{}]*\}\s*"
        r"serialize_compressed_float\s*\(\s*stream\s*,\s*dirtLevel\b[^;]*\);", body)
    tail = re.findall(
        r"serialize_bool\s*\(\s*stream\s*,\s*engineState\s*\);.*?"
        r"serialize_uint16\s*\(\s*stream\s*,\s*alarmState\s*\);", body, flags=re.S)
    if len(dirt) != 1 or len(tail) != 1:
        raise ValueError(f"{name}: extension shape changed; refusing a stale or partial extraction")
    calls = re.findall(r"serialize_(bool|uint16)\s*\(\s*stream\s*,\s*(\w+)\s*\)", tail[0])
    expected = [("bool", field) for field in FIELDS[1:5]] + [("uint16", "alarmState")]
    if calls != expected:
        raise ValueError(f"{name}: unexpected flag/alarm codec order: {calls}")
    # The two feature sections are copied verbatim. Existing intervening packet fields
    # are intentionally outside this extension-only harness.
    code = dirt[0] + "\n" + tail[0]
    fixture_name = "VehicleIdleExtension" if name == "VehicleIdleUpdate" else "VehicleDriverExtension"
    fixture = (f"struct {fixture_name}\n{{\n" + "\n".join(declarations) +
               "\ntemplate <typename Stream> bool Serialize(Stream& stream)\n{\n" + code +
               "\nreturn true;\n}\n};\n")
    return fixture, {"packet": name, "extracted_code_sha256": hashlib.sha256(code.encode()).hexdigest(),
                     "sections": ["dirt normalization/compression", "four flags and alarm"]}


def generate() -> tuple[str, dict]:
    source = (ROOT / HEADERS[0]).read_text(encoding="utf-8-sig")
    hashes = {path: digest(ROOT / path) for path in HEADERS}
    fixtures, sections = [], []
    for name in ("VehicleIdleUpdate", "VehicleDriverUpdate"):
        fixture, metadata = extract(class_body(source, name), name)
        fixtures.append(fixture)
        sections.append(metadata)
    header = ("#pragma once\n#include <algorithm>\n#include <cmath>\n#include <cstdint>\n#include \"serialize.h\"\n"
              f'inline constexpr const char* kVehicleHeaderSha256 = "{hashes[HEADERS[0]]}";\n'
              f'inline constexpr const char* kSerializeHeaderSha256 = "{hashes[HEADERS[1]]}";\n' +
              "\n".join(fixtures))
    manifest = {"scope": "current extension serializer sections only; no native timing proof",
                "header_sha256": hashes, "sections": sections,
                "test_source_sha256": digest(ROOT / "tests/vehicle_packet_extensions.cpp")}
    return header, manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", help="cl.exe, clang++, or g++ available in an initialized compiler environment")
    parser.add_argument("--extract-only", action="store_true", help="Validate extraction and print hashes without compiling")
    parser.add_argument("--evidence", type=Path, help="Optional JSON evidence output outside production source")
    args = parser.parse_args()
    header, manifest = generate()
    if args.extract_only:
        manifest["status"] = "extraction verified; C++ tests not executed"
    else:
        compiler = args.compiler or shutil.which("cl.exe") or shutil.which("clang++") or shutil.which("g++")
        if not compiler:
            raise RuntimeError("No compiler found. Run in the same compiler environment as the combined build, or use --compiler.")
        with tempfile.TemporaryDirectory(prefix="coop-vehicle-packet-tests-") as folder:
            temporary = Path(folder)
            (temporary / "vehicle_packet_extensions_extracted.h").write_text(header, encoding="utf-8")
            executable = temporary / ("vehicle_packet_extensions.exe" if os.name == "nt" else "vehicle_packet_extensions")
            source = ROOT / "tests/vehicle_packet_extensions.cpp"
            if Path(compiler).name.lower() in ("cl", "cl.exe"):
                command = [compiler, "/nologo", "/std:c++17", "/EHsc", "/W4", str(source),
                           f"/I{temporary}", f"/I{ROOT / 'third_party'}", f"/Fe:{executable}",
                           f"/Fo:{temporary / 'test.obj'}"]
            else:
                command = [compiler, "-std=c++17", "-Wall", "-Wextra", str(source),
                           "-I", str(temporary), "-I", str(ROOT / "third_party"), "-o", str(executable)]
            options = {"cwd": temporary, "capture_output": True, "text": True,
                       "creationflags": subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0}
            build = subprocess.run(command, **options)
            manifest["compile_command"] = command
            manifest["compile_exit_code"] = build.returncode
            manifest["compile_stdout"] = build.stdout
            manifest["compile_stderr"] = build.stderr
            if build.returncode:
                manifest["status"] = "test harness compilation failed"
            else:
                run = subprocess.run([str(executable)], **options)
                manifest.update(status="passed" if run.returncode == 0 else "failed",
                                test_exit_code=run.returncode, test_stdout=run.stdout, test_stderr=run.stderr)
                print(run.stdout, end="")
    # Record/check the actual inputs, not the upstream reference header or a cached fixture.
    assert manifest["header_sha256"] == {path: digest(ROOT / path) for path in HEADERS}, "Headers changed during the test"
    if args.evidence:
        args.evidence.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))
    return 0 if args.extract_only or manifest["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
