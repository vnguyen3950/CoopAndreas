"""Portable launcher for actual session-service extraction and native doubles."""
import argparse
from pathlib import Path
import subprocess
import sys

INPUTS = (
    "client/src/CSessionSync.cpp", "client/src/CSessionSync.h",
    "client/src/COpCodeSync.cpp", "client/src/COpCodeSync.h",
    "client/src/CCore.cpp", "client/src/Main.cpp", "client/src/PacketHandlers/session.cpp",
    "shared/network/session_sync.h", "shared/network/packets/session.h",
    "shared/network/packet.h", "shared/network/packet_types.h", "third_party/serialize.h",
)
CASES = (
    "menu_seed", "guest_reset", "seed_receipt", "receipt_capture", "receipt_side_effects",
    "mission_reward", "cash_feedback", "deferred_action", "rapid_toggle", "flags_reset",
    "death_preservation", "same_frame_resurrection", "native_wanted", "migration",
    "hospital_fee", "arrest_fee", "same_frame_fee", "debt_budget",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Implementation worktree")
    parser.add_argument("--output", type=Path, required=True, help="New child of SOURCE/.cache/session-headless")
    parser.add_argument("--native-source", type=Path, required=True, help="Adjacent gta-reversed GameLogic.cpp")
    parser.add_argument("--msvc-env", type=Path, default=Path(
        r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"))
    args = parser.parse_args()
    owned = Path(__file__).resolve().parent
    command = [sys.executable, str(owned / "run_headless.py"), "--source", str(args.source),
               "--output", str(args.output), "--test", str(owned / "session_native_tests.cpp"),
               "--support", str(owned / "native_doubles.h"), "--service", "client/src/CSessionSync.cpp",
               "--opcode-prefix", "--punishment-source", str(args.native_source),
               "--standard", "c++17", "--msvc-env", str(args.msvc_env)]
    for name in INPUTS: command += ["--input", name]
    for name in ("shared", "third_party", "client/src"): command += ["--include", name]
    for name in CASES: command += ["--case", name]
    return subprocess.run(command).returncode


if __name__ == "__main__":
    sys.exit(main())
