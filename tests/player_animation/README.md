Player animation and shared actor-life contract
=============================================

This lane observes stock PLAYIDLES group 49 IDs 261–264 and default group 0
IDLE_CHAT 12. It sends no arbitrary task, animation name, callback or pointer.
The source association is sampled without changing its native task callbacks.
Native visual application creates a separate association using AddAnimation
(SDK 0x4D3AA0), with its own delete callback (0x4CEBC0). Cancellation fades only
that exact association while its full ped reference, clump, hierarchy and
callback identity still match. Mission, swimming, combat and vehicle contexts
receive no new association. No native task is replaced or aborted.

PLAYER_ANIMATION is reliable EVENT. The existing RESPAWN_PLAYER gains the same
Life payload on EVENT; all previous enum IDs remain unchanged. Root must choose
the final next-batch protocol/version and enum order; this isolated lane leaves
config at 0.5.0-alpha and is not wire-compatible with the frozen 0.5 package.

Life binds vitals connection generation, owner birth, monotonic sample sequence,
model, area and the owner's native pool reference (diagnostic identity, never a
guest CPools operand). Server authenticates both peer and registered wrapper,
rejects C2S-chosen generations/owners and stamps its actual generation and time.
Birth advances on script scenes, respawn, model/ref/clump changes, without
wrapping in a connection. Dormant scene/respawn boundaries have ready=false;
ready frames complete that boundary. A late join receives a fresh outer server
timestamp but the original sample time and sequence, so expired one-shots do
not restart. Migration does not change actor ownership: each player owns itself.
Departure tombstones discard old generations while preserving a queued newer
occupant. Disconnect resets the service with the existing buffer/registry hooks.

Future EVENT state/reset is bounded to one of each per slot and waits without
an authentication-time model timeout for matching SYSTEM roster/vitals and
native readiness. Initial menu replay survives first script initialization.
A prior applied source instance is retired during local script reinitialization
and native interruption; a new source association may then play. Missing blocks
request streaming with zero additional required flags. Live associations retain
their native block references. Native tasks may independently blend or interrupt
visuals; this does not reproduce arbitrary task history or unseen associations
created/deleted entirely between sampling frames.

Pickup integration (no pickup source owned here):
- Client: CPlayerAnimationSync::GetLocalLife(Life&), GetLocalBirth(), GetLocalSequence().
- Server: CPlayerAnimationServer::GetActorLife(const CNetworkPlayer*, Life&).
- Unknown/dormant/unacknowledged current birth/model/area/reference returns false.
- Local getter refreshes only the real local actor at focus zero and returns
  its last exact server-acknowledged life/sequence. It never recreates other peds.
- Server getter requires the current registered peer and vitals generation.
  This is authenticated owner-reported model/area, not native server position
  verification. Pickup must supply its finite exterior position, check non-future
  sequence/exact generation and birth/model/area, and recheck native eligibility.

Portable tests, from the actual worktree root:

    python tests/player_animation/run_tests.py --suite service --out .cache/animation-tests/final-service
    python tests/player_animation/run_tests.py --suite codec --out .cache/animation-tests/final-codec
    python tests/player_animation/run_tests.py --suite contract --out .cache/animation-tests/final-contract
    python tests/player_animation/run_tests.py --suite service --mutate-birth --out .cache/animation-tests/final-mutation

The mutation is expected to return nonzero with the actual stale-birth/lookup/
replay assertions failing. The runner freezes and hashes production dependencies
and tests, compiles x86 MSVC, and rejects changed inputs. Client/server service
bodies and Init lambdas remain unchanged except include-line removal; actual
HasBoundPed and SDK enums are extracted. Only native/transport collaborators are
recorded doubles. Codec tests use actual packet wrappers and serialize.h, with
separate opposite-role executables for asymmetric SenderPlayerId. They exercise
every shorter payload plus nonfinite and finite out-of-range hostile reads.
These tests and isolated x86 builds do not execute native addresses/ABI in GTA.

Native source provenance: adjacent gta-reversed TaskSimplePlayerOnFoot.cpp
PlayIdleAnimations (0x6872C0), TaskSimpleChat.cpp constructor, AnimationEnums.h,
AnimManager.cpp AddAnimation/CreateAnimAssociation and AnimBlendAssociation.cpp
SetDeleteCallback/UpdateBlend. SDK fields, enum values and bindings are hashed by
the runner. No SDK or reversed-game code is copied into production.

Human runtime checks:
1. Two initialized players: observe each of the four idle gestures/chat when
   native tasks produce them; verify phase, natural completion, loop and stop.
2. Join while still in the menu, then New Game after several minutes; repeat
   with model/block loading delay, respawn, New Game/load and reconnect/slot reuse.
   Confirm expired gestures stay expired and old-life loops do not return.
3. Interrupt a gesture by movement, combat, swimming, vehicle entry or a mission;
   verify normal native tasks survive, only owned visuals fade, and shutdown
   and host migration leave no stuck poses. Test pickup eligibility separately.
