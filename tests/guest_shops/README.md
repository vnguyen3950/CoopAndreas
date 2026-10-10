Guest local retail access
=========================

Supported controllers: BARB (barber), CLOTH (clothes), TATTO (tattoos), AMUNAT
(Ammu-Nation) and JFUD (food). These already-running streamed non-mission
controllers share Player.CanStartMission / native SCM opcode 03EE. The old
host-only predicate denied guests even after entering the building.

Permission uses the live ESI script and ECX player at supported native call
0x4895B0. The wrapper forwards two cdecl arguments, preserves caller-saved
stack balance/nonvolatile registers, and returns AL; the original caller tests
AL. A559 disk checks cover ESI parameter collection, predicate call, AL test
and bool __thiscall predicate prefix 0x609590. SDK/native 609590 eligibility
remains unchanged; there is no guessed virtual slot. Special save/2-player
pickup call 0x4577E6 remains the original host-only gate. Ordinary firearm and
money pickup paths short-circuit that native predicate; pickup code is untouched.

An authenticated guest requires an active, aligned native script-pool entry in
the active chain, exact retail name, external/non-mission flags, local full ped
pool identity at focus zero, declared clear shared mission flag, canonical wallet
readiness and the native predicate's true result. Host retail also waits for
wallet readiness; host non-retail story predicates retain native behavior.
True offline SCM retains native behavior. A connected unauthenticated handshake
cannot use the offline fallback. No persistent shop state or new reset callback
is introduced; stale/inactive/recycled/synthetic scripts fail classification.

Shop effects remain local for both roles before task-sequence recording or
opcode broadcast. Actual PerformSequence has a single caller: OnOpCodeExecuted
from BuildAndSendOpcode. Retail returns before that caller; a seeded nonempty
sequence verifies zero retail direct sends and one normal story send. Native
retail mission audio still runs locally. Generic network replay cannot inherit
a shop scope. Existing wallet ConsumeOpcode observation is retained before the
local return, including original retail ADD_SCORE effects.

Root prerequisite 9a764da (locally imported as 25acb98) supplies the pure
CSessionSync::IsWalletReadyForLocalService query. Import only the subsequent
shop commits. The getter does not charge, write money or authorize affordability.
Native CShopping::Buy at 0x49BF70 subtracts price from the focused player once;
cosmetic/equipment effects remain on that player. Tests compile the unchanged
native Buy body and actual CaptureMoney/WriteMoney/ConsumeOpcode functions, plus
actual client/server ledger. Repeated observation and receipt delivery produce
one delta and one canonical charge. There is no second payment pathway, new
wallet reservation or serialized purchase authorization; existing optimistic
concurrent-spend/debt policy is retained.

Reproducible source tests (fresh unique .cache child required):
  python tests/guest_shops/run_tests.py --output .cache/guest-shops-tests/unique
  python tests/guest_shops/run_tests.py --mutate-shop-list --output .cache/guest-shops-tests/unique-mutant

The runner freezes and hashes production functions, SDK contracts, all five SCM
controllers, native Shopping source/header and supported A559 EXE bytes. It
compiles the actual x86 wrapper, service, VM send policy, task opcode routing,
direct sequence publisher, audio hooks, wallet observers and native Buy body.
Native tasks/resources/transports are recorded doubles. No native game address
is executed. Red-002 preserves 21 pre-fix permission/locality failures; red-001
was a missing recorded car-door enum compilation issue. Partial green runs remain
separate from final-auth-boundary-007 (188 passes). Membership mutant fails 33
checks at the prior 186-check snapshot, including contact/retired/synthetic
permission and inappropriate host-story wallet gating. Final source mutation
identity is recorded separately in validation evidence if rerun by root.

Actual session regression: 26 fresh-process scenarios / 693 assertions passed
with the root readiness dependency and narrow COpCode changes. Isolated x86
client builds passed in .cache/guest-shops-build/output with GTA_SA_DIR cleared.
client-final-002.log is the SDK checkpoint before the final offline/auth
classification refinement; root owns the exact final combined build. There
are no SCM, SDK, protocol, pickup, vote, NPC or session implementation changes
in shop commits. No shared artifacts, installed files or user/game processes
were touched.

Bounded limits: available doors, inventory/story unlocks and stock script
cleanup stay native; gym, side activities and vehicle tuning are not included.
Shared shopkeeper native-task contention remains possible if players use the
same attendant concurrently. Retail task/history animation is intentionally not
broadcast; normal player state/equipment/cosmetic replication remains existing
behavior. A service already open when connecting or when the host starts a
mission needs human conflict testing; this increment gates new entry and does
not invent a forced stock-script cancellation. Generalized NPC loot is unrelated.

Human tests: after room/wallet ready, each guest enters barber, clothes, tattoo,
Ammu-Nation and food menu, buys/cancels/exits, and verifies camera/control recovery,
owner-specific goods and one shared charge. Test death/arrest, reconnect and two
customers sharing an attendant. Check that guest contacts cannot start story
missions, host story/INTRO2 still works, and native special pickups retain the
prior gate. Native gameplay and actual shop UI execution are unvalidated.
