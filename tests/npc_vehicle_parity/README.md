# NPC vehicle state parity

`PED_DRIVER_UPDATE` remains the existing unreliable `SYNC` packet. Its complete
prefix from base `7a09780` is preserved; a 24-bit suffix adds four independent
booleans (`engineState`, `lightState`, `engineBroken`, `sirenOrAlarm`), a full
16-bit `alarmState`, and `dirtLevel` compressed over 0..15 at precision 1.
Nonfinite dirt normalizes to zero; finite dirt is clamped before quantization.
Alarm values include the native 65535 armed sentinel. No packet ID is added.
The coordinator selects the compatible protocol version for the combined build.

The sending NPC's ped syncer owns the stream through
`CNetworkPedManager::Update`. The server requires that authenticated owner and an
empty recorded player-driver slot. It retains the existing vehicle idle syncer:
that owner can differ from the NPC owner. A reliable player entry takes precedence
over delayed NPC snapshots. The client rejects snapshots for a locally owned NPC
or a vehicle with a native player driver, while allowing remote NPC state to reach
a locally owned idle vehicle. The existing per-field native vehicle assignments
are used; no fabricated mission state or separate player/idle packet is involved.

Native source provenance:

- `third_party/plugin-sdk/plugin_sa/game_sa/CVehicle.h`: the engine/light/damage/
  siren flags, `m_nAlarmState`, and `m_fDirtLevel` (0..15).
- Adjacent gta-reversed `source/game_sa/Entity/Vehicle/Vehicle.cpp`,
  `CVehicle::ProcessCarAlarm` (0x6D21F0): zero and 65535 alarm states, timer behavior.
- Adjacent gta-reversed `source/game_sa/Audio/Entities/AEVehicleAudioEntity.cpp`,
  `GetSirenState` and `GetHornState` (0x4F61E0): NPC horn countdown/pattern and
  player horn semantics differ. **Horn is not implemented by this slice.**

## Headless tests

From this worktree run:

```powershell
python tests/npc_vehicle_parity/run_tests.py --output .cache/npc-vehicle-parity/review-001
```

Use a fresh output name; old evidence is never overwritten. `--msvc-env` can select
another installed `vcvarsall.bat`. The runner targets x86. It freezes/hashes every
production dependency, actual SDK enum headers, and test source; it rejects inputs
changed during the run. Repository history must include base `7a09780` for the
independent legacy packet reference.

The real Packet wrappers and serialize.h are used. Complete packet/enum/helper
declarations, the actual NPC sender branch, and the server/client driver-handler
bodies are extracted unchanged. Only native entity data/methods, logging, static
registration, and transport have doubles. Tests cover all 16 flag combinations,
alarm zero/countdown/65535, dirt quantization and NaN/infinity, all 12 subtypes,
bit offsets, truncated payloads, malformed IDs/subtypes, unchanged prefix bits,
ownership handoff, receiver authority, and native field capture/application.

`--mutate-authority` inverts the extracted server guard in the frozen test output
only. It must produce a failing suite; live source is never modified.

## Human runtime checks

Use matching coordinator-built clients/server. Have an NPC drive a police vehicle
and a normal vehicle with differing vehicle/ped sync owners. Observe lights,
engine off/on and engine damage, siren/alarm transitions, and clean/dirty body
state on another client. Test an NPC-to-player takeover and delayed snapshots,
then player exit and resumed NPC driving. Check a late join's next periodic NPC
snapshot. Audible siren/alarm behavior and native AI overrides remain unvalidated.

This slice does not solve older NPC/vehicle ID reuse, streaming lifetimes,
interpolation or general NPC driver timing. Player/idle codecs, missions, session
money/wanted/cheats, and horn behavior are unchanged.
