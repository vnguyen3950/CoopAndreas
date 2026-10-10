Police/corpse source checkpoint
==============================

The old constructor rejected city models 280/281/282: SDK MODEL_LAPDM1 is
motorcycle model 284, not 280. All stock models 280..287 are now supported.
Native city selection is streamed before construction, and the requested skin
is explicitly restored. Existing sheriff/biker/SWAT/FBI/army cop types retain
their constructor semantics. The test uses unchanged gta-reversed model
resolution functions with recorded constructors, not a guessed model table.

Health zero now enters one native CTaskComplexDie (0x630040), default group 0
KO-shot-front animation 15. Native task/event processing produces the corpse;
no synthetic kill attribution is inserted. Replica movement/aim work stops,
repeat states do not restart the task, and delayed alive state cannot restore
health in the same lifetime. Replica weapons are marked not to drop and native
money count is zeroed before the task. Corpse cleanup remains authoritative
PED_REMOVE, full native reference and generation guarded.

PED_DEATH is reliable EVENT. The owner reserves a positive state sequence once,
publishes position/area, and retains that immutable identity. Server seals only
the authenticated current NPC owner, captures producer connection/vitals
generation, and never grants a second seal after transfer. A newer SYNC sample
cannot discard or lower a current-owner reliable seal. Replay sends the original
death identity with a fresh server timestamp; old epoch proof remains terminal
for that NPC generation after transfer. Old/reused/removed lifetimes reject it.

Stable pickup APIs:
  client CNetworkPedManager::GetOwnerDeathIdentity(CPed*, int&, NPCSync::Stamp&)
  server CNetworkPedManager::GetDeathProducer(CNetworkPlayer*, int, const NPCSync::Stamp&)
Server CNetworkPed retains m_deathStamp, m_deathPosition, m_deathArea,
m_deathProducer and m_deathProducerGeneration until authoritative removal;
m_nModelId/m_nPedType remain the original registered model/type. Unknown,
unconfirmed or removed identities supply no permission. Pickup owns the native
weapon-drop detour and captured-ammo manifest path. This checkpoint does NOT
claim linked pickup participation; that requires p7/root integration and tests.
SYSTEM manifest before EVENT seal must remain bounded/inert in the pickup lane.

Cop ProcessControl slot 10 at 0x86C148, native 0x5DE160 and generic base
0x5E8CD0 were independently disk-verified on supported EXE SHA256
A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26.
Slot/prefix checks precede installation. Exact bound nonowners process generic
native physics/tasks; owners, offline and untracked cops retain original control.
This is not full cop AI/arrest/task-history synchronization.

Commands (actual worktree, fresh absolute/relative .cache output):
  python tests/npc_world_sync/run_tests.py --suite police --output .cache/police-tests/unique-police
  python tests/npc_world_sync/run_tests.py --suite deathserver --output .cache/police-tests/unique-deathserver
  python tests/npc_world_sync/run_tests.py --suite codec --output .cache/police-tests/unique-codec
  python tests/npc_world_sync/run_tests.py --suite deathserver --mutate-death-producer --output .cache/police-tests/unique-mutant

The incarnation mutant must fail the reused-slot producer check. Preserved red
red-004 has 19 genuine constructor/zero-health failures; earlier red-001..003
include fixture compilation/readiness corrections and are not source findings.
Recorded native collaborators prove calls/state/authority, not native address
execution, task animation, corpse collision or game timing. Root owns matched
0.7 protocol/version and all shared builds; local config remains unchanged.

Human checks after root integration: kill the same authenticated cop from each
player; verify one corpse everywhere, normal animation and owner cleanup; repeat
with city/biker skins, car deaths, late join, transfer and disconnect. Confirm
only the original owning native instance produces eligible loot, only one shared
manifest/reward is admitted, and no replica kill/drop or wanted feedback occurs.
Batch 008 and installed runtime were not touched by this source checkpoint.
