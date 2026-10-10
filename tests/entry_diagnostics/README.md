# Entrance diagnostic function tests

This test-only lane is based on `e4ddfaec1a2d2f7a0a9c2fb30997d38a2f0d6922`.
It does not change entrance eligibility, permissions, marker state or gameplay.

Run from the repository root:

```powershell
python tests/entry_diagnostics/run_tests.py --output .cache/entry-diagnostics-new-run
```

The output directory must be new and inside this checkout's `.cache`. The runner
uses installed MSVC through `vcvarsall.bat x64_x86`, clears `GTA_SA_DIR`, and
builds only its own headless executable. It does not use shared build helpers,
load game modules, call GTA addresses, attach to a process or launch a game.

The runner freezes raw source inputs and extracts complete, unchanged
`CEntryExitDiagnostics::Process` and `CEntryExitMarkerSync::Receive` functions.
The real common `runtime_diagnostics.h` and `config.h` are included in the
executable. SDK `SEntryExitFlags` and packet payload/fields are extracted without
rewriting their layout. Native actor, pool and task methods are explicit doubles;
no native game execution or complete engine ABI validation is claimed.

Recorded native evidence is read only from the user-owned supported executable
with SHA256 `A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26`.
The runner checks the WORD control cell, replay comparison, disabled BYTE,
access flag `0x4000`, and native player 0/1 operands. The recorded `PlayerInfo`
source assigns the SDK `bPlayerSafe` bit, and the current debug UI uses `0x200`.
Resources and extracts are hash-bound in the private result, not committed game
content. The prior 11 native gate proofs and 14 static sink checks remain
separate evidence, not counted again here.

Each scenario runs in a fresh test process so the real sampler and sink static
lifetime is retained. Checks cover unready and invalid local references;
five-second sampling and tick wrap; full WORD controls and safe/debug masks;
native 0/1 bindings; access flags, nearest entry, non-finite position and mission
index; queue timestamps without queue mutation; original Receive assignments;
and unchanged gameplay gate values around observation. The receipt-before-pool
case checks existing caching semantics, not a new replay or permission feature.

The default Win32 sink calls create files only beneath the test executable's
private output folder. Failure injection wrappers have compile-time signature
checks against real Win32 `WINAPI` prototypes. They test module path failure,
truncation, missing separator, long path, directory/file failure, failed and
partial writes, invalid/truncated fields, actual saved JSON and the cumulative
one MiB cap. Partial writes deliberately produce incomplete evidence and are
not described as valid JSON. No durability or hard real-time latency claim is
made. The test closes its own file after assertions; production has no static
shutdown destructor and relies on process handle cleanup.

A targeted nonshipping mutation changes only the extracted sampler safe mask:

```powershell
python tests/entry_diagnostics/run_tests.py --output .cache/entry-diagnostics-mask-mutant --mutate-safe-mask
```

That command must fail assertions. It never changes production source.
All output hashes, failures and JSON files are retained. Runtime investigation
and manual gameplay remain owned by the user and root coordinator.
