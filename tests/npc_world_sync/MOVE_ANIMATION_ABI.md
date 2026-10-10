NPC movement animation ABI correction
=====================================

Primary batch 009 joining crash (2026-10-10_00-06-07.log): native 0x5D12DD
is REP MOVSD inside SaveDataToWorkBuffer. EDX/EDI zero, EBX/EAX four and
ECX one match its first four-byte write through an uninitialized save buffer.
Return 0x5D578F is CPed::Save's first storage call. Packaged DLL call
0x10089CC1 invokes SDK SetMoveAnim at 0x10108360; return RVA 89CC6 occurs
in the recorded stack. Thunk bytes 8B01 8B4060 FFE0 select virtual slot 24.
Nested PED_REPLAY dispatcher return RVA 8938B also matches the stack.

Supported EXE SHA256:
A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26
Batch 009 DLL/lab SHA256:
5399622D9254697C3D9342DFA0DCC014D4741FB602EC55586321DD59BEB7331A

Disk native vtables civilian 0x86C0A8, cop 0x86C120 and base 0x86C358
all store SetMoveAnim 0x5E4A00 at slot 23 and Save 0x5D5730 at slot 24.
SDK CPed.cpp lines 27-30 uses slot 24. Reversed Ped.cpp lines 186-188
and 386-389 independently name these native methods. The NPC fixture
previously used a no-op SetMoveAnim, hiding this incorrect SDK dispatch.

Only production change: local SetNPCMoveAnimation(CPed*) adapter in
client/src/PacketHandlers/peds.cpp invokes plugin::CallMethod<0x5E4A00,
CPed*>(ped). Existing accepted-state, owner/replay, positive-health/corpse
branches and SetMoveState calls are preserved. SDK, native storage, death
producer APIs, wire, server, cop control/offline and cleanup are untouched.
PluginBase.h implements CallMethod as void(__thiscall *)(C, Args...).
The isolated x86 build confirms mov eax,0x5E4A00; mov ecx,edi; call eax.

Actual-fixture regression: the runner freezes/hashes CPed.cpp, compiles its
unchanged SetMoveAnim body and reads 26 native vtable DWORDs directly from
the hash-checked supported EXE. Recorded callbacks stand in for Save and
movement at their disk-selected positions; native addresses are never executed.
The fixture compiles with x86 MSVC, exercises actual constructor, on-foot and
replay handlers, and checks exact this pointer. It proves the old thunk selects
Save for cop/civilian/base tables. The pre-fix handler has eight live state and
reliable replay failures across two city cop skins, a civilian and a gang actor.
Fixed code passes all 31 checks. Corpses do not animate or revive, repeated
seals do not restart tasks, and owner states remain rejected.

Preserved runs under .cache/move-abi-tests:
  red-001: fixture extraction initially matched an SDK comment; compile error.
  red-002: original failure plus eight downstream cumulative-counter failures.
  red-003: independent counters; eight genuine alive/replay dispatch failures.
  green-001: unchanged final fixture; 31 pass after the production fix.
  police-001: 62; allocation-001: 4; noncop-001: 88;
  deathserver-001: 44; client-001: 37. All pass; total 266.

Commands (fresh unique .cache output required):
  python tests/npc_world_sync/run_tests.py --suite moveabi --output .cache/move-abi-tests/unique
  python tests/npc_world_sync/run_tests.py --suite police --output .cache/move-abi-tests/unique-police
  python tests/npc_world_sync/run_tests.py --suite police --null-cop --output .cache/move-abi-tests/unique-allocation
  python tests/npc_world_sync/run_tests.py --suite noncop --output .cache/move-abi-tests/unique-noncop
  python tests/npc_world_sync/run_tests.py --suite deathserver --output .cache/move-abi-tests/unique-server
  python tests/npc_world_sync/run_tests.py --suite client --output .cache/move-abi-tests/unique-client

Actual-source-cwd isolated x86 release client build passed in
.cache/move-abi-build/output, with GTA_SA_DIR unset and its own XMAKE_CONFIGDIR.
See configure.log/client.log in that directory's parent. No root artifacts,
installed files, game/user processes or batch 009 contents were modified.

Native movement/animation execution and joining game timing remain user-owned
runtime validation. Human checks: join with existing alive cops/civilians/gangs,
verify locomotion and no crash; repeat menu-to-game, late join/reconnect and
corpse replay/removal. Subsequent shutdown faults are separate and not diagnosed
or claimed fixed by this primary-call correction. Root owns patch version,
combined integration/build, packaging and installation.
