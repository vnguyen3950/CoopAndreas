Non-cop corpse regression supplement
===================================

This test-only increment verifies the existing shared NPC death/corpse code.
Production remains byte-identical to the reviewed 6b83390 allocation correction.
The original police freeze and its evidence are unchanged.

Actual client constructor, hosted registration/confirmation, state/death/owner
assignment handlers and death identity API are extracted without rewriting
function bodies. Native task and pool collaborators are recorded doubles.
Cases use SDK model/type identities: civilian male 7/CIVMALE, female 12/CIVFEMALE,
Ballas 102/GANG1 and Families 105/GANG2. Each replica is tested with reliable
seal before zero-health SYNC and with the opposite order. One recorded death
task, terminal health, immutable seal, stopped motion, weapon/money suppression,
delayed alive rejection and no transferred corpse loot allowance are required.
Actual hosted instances retain their native death processing, loot flags and
money through repeated producer capture, seal echo and rejected foreign SYNC.

The actual server supplement covers the same four identities: foreign seal
rejection, duplicate idempotency, terminal cached health, original producer
retention through transfer, denied reseal and revoked proof after removal.

Commands (fresh .cache output required):
  python tests/npc_world_sync/run_tests.py --suite noncop --output .cache/non-cop-tests/unique-client
  python tests/npc_world_sync/run_tests.py --suite deathserver --output .cache/non-cop-tests/unique-server
  python tests/npc_world_sync/run_tests.py --suite noncop --mutate-replica-loot --output .cache/non-cop-tests/unique-mutant

The loot mutant removes only the two actual ApplyReplicaHealth suppression
assignments in the frozen extracted copy. It must fail sixteen checks. Original
production is never modified. The runner also retains root's native-reference
freeze/hash and main-checkout path repair, so imports can use the same commands.

Passing actual-function tests establish this bounded generic corpse/seal
contract for representative civilians and gangs; they do not execute native
addresses, prove arbitrary NPC AI/task history, or publish non-cop loot.
Pickup generalization remains a separate root/pickup lane after cop-origin
validation. Human tests should kill civilian and gang actors owned by each
player, verify one corpse and terminal cleanup across clients, and check late
joins/transfers. Native gameplay and generalized loot remain unvalidated.
