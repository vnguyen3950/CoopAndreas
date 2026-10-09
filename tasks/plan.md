# Non-mission synchronization batch

## Current increment: animations, trailers and ordinary pickups

The reviewed world batch is frozen as `mission-batch-007`, protocol `0.5.0-alpha`, and pushed to the development fork. The user authorized further non-mission work while away. Three existing Herdr lanes now own isolated `player-animation-sync` (w1:p3), `trailer-sync` (w1:p4), and `pickup-lifecycle` (w1:p7) worktrees. Root owns independent review, shared lifetime integration, enum/version selection and the next matched testing package. Gameplay remains user-owned.

Animation synchronization is limited to five verified native idle/chat visual associations. It supplies an authenticated connection and actor-birth API shared with pickups; script restart, respawn, readiness and departing-player queues must agree before owner operations are allowed. Native task history is outside this slice.

Trailers use actual native tow links for supported cabs/tractors and trailer models. Vehicle birth, creation nonce, full pool reference and local script scene protect attachment and ordinary vehicle replay. Menu spawns are bounded and deferred; removal must cancel the matching pending birth. Driver authority controls the pair, including validated NPC drivers. Root checks existing fire attachment/ownership against the new vehicle validity API before import is considered complete.

Pickups cover supported exterior consumables outside active missions. Host registration assigns non-reused IDs; atomic reservation permits one outstanding grant per collector. Native eligibility and exact actor life are checked again before calling the original pickup update. A known pre-apply decline may release a reservation; any uncertain post-apply outcome retires it. Native money benefits flow through existing wallet observation once. Mission pickups, collectibles, shops, interiors, respawning types and partial weapon benefits remain outside this slice.

Each lane hands off stable commits, actual codec/service regressions and isolated SDK builds. Root reviews shared startup/join/leave/reset ordering, reruns affected prior suites, builds all four x86 release targets and records native runtime limits. The approved 0.5 package and actual game lab stay unchanged during implementation. The next protocol must reject mismatched prior clients because vehicle and player lifecycle payloads change.

## Completed batch: NPCs, fires, maps and gang wars

The user requested these four systems and explicitly skipped gamepad support. Base is `5ab497658d1649f520395e88248c92f78dc251c1`. Three existing Herdr agents work in isolated branches: `npc-world-sync` (w1:p3), `fire-sync` (w1:p4), and `gang-war-sync` (w1:p7). Root owns map discovery, waypoint safety, player-marker rendering and integration. Gameplay stays with the user.

NPCs retain their owner's native AI. Generations, owner epochs and state sequences protect recycled slots and ownership changes; reliable replay supplies retained on-foot/driver/passenger state to joiners. Gang-war wave NPCs are pinned to the native host through a dedicated API. Fire world simulation stays on the host; replicas cannot spread or independently apply world damage, and owner-target effects require valid attachments. Gang wars keep native host progression and rewards, with guest suppression, shared territory and actual guest hits on the mapped host actors. Host departure cancels the active war rather than pretending to transfer native AI.

Map discovery uses the native 100-cell bitmap. The host's initialized loaded game seeds it; initialized outdoor players reveal their current cell. Reliable SYSTEM packets order snapshots with host assignment and connection identity. Menu/loading guests cache the room state, including when promoted, before applying native state. A deliberate later load by a controlling host starts a new campaign epoch. Waypoints use existing authenticated connection generations, increasing owner sequences and ordered replay. Terrain discovery and gang density/color ownership are separate native fields.

Root owns the final packet enum and matched `0.5.0-alpha` protocol. Do not install this batch or replace the approved package during implementation. New tests exercise actual codecs/services with explicitly recorded native/transport doubles, plus targeted mutations; review committed changes before import, preserve all prior regression suites and run all four x86 release targets. Reuse the reviewed SCM pair only after confirming source/SDK identity.

The sections below record the completed earlier batch and its original verification contract.

Base: `7a09780151c984a37e74e7075546b74b326d7e24`, development branch `coop-missions`.

The user prioritizes the non-mission README backlog. Implement independent gameplay slices in isolated worktrees, review their real packet/native boundaries, and combine them into one matched client/server testing package. Game launches and runtime testing belong to the user; source preparation, tests and builds belong to the agents.

## Owners and dependencies

| Lane | Owner / worktree | Deliverable |
| --- | --- | --- |
| NPC vehicle state | `coop-audit`, `npc-vehicle-parity` | Extend the actual NPC driver update with validated vehicle flags/dirt and horn only when its native semantics are verified. |
| Cutscene voting | `reverse-map`, `cutscene-votes` | Authenticated unanimous voting for verified native cutscenes, including scene lifetime, membership changes and cancellation. |
| Player vitals | `mission-build`, `player-vitals` | Per-player maximum health and breath state with explicit native/global-stat ownership. |
| Map markers and integration | Root, main checkout | Fix player marker proportions; combine reviewed changes, select the protocol version, update task status/docs, build and freeze a matched package. |

Contract proposals precede production edits. Root owns the final packet enum order, protocol version, combined builds and package selection. Vote packet names are reserved as `CUTSCENE_VOTE_BEGIN`, `CUTSCENE_VOTE`, `CUTSCENE_VOTE_STATE`, and `CUTSCENE_VOTE_COMMIT`; vitals reserve `PLAYER_VITALS`. All new fields require a matching-version handshake. Existing weapon-stat reserved slots stay zero.

The two story worktrees were redirected before edits; the six proposed stories are queued rather than silently marked adapted. Carrying, true trains, existing-world object adoption and ordered IPL changes remain separate dependencies for the four already documented blocked missions. Server campaign persistence is a separate feature; the save-system explanation did not authorize an implicit persistence redesign.

## Implementation sequence

1. Each worker verifies source/native semantics and records a bounded contract.
2. Implement and test its contract, then native integration, in reviewable commits. Use unique build directories with `GTA_SA_DIR` cleared; shared artifacts are root-owned.
3. Root fixes marker proportions using native radar rendering evidence.
4. Review committed diffs and tests before import; resolve shared enum/startup/lifecycle conflicts sequentially.
5. Run the appropriate regression suites and all four x86 release targets. Existing reviewed SCM/IMG artifacts can be reused only after script/SDK identity is verified.
6. Update the public backlog and human test checklist, commit/push the development branch, and freeze a new package. Test the updater on a filesystem fixture, leaving the actual lab and user processes untouched.

## Verification limits

Synchronization tests must exercise production validation/codecs or actual extracted functions, with recorded native doubles identified explicitly. Positive tests, malformed/truncated payloads, sender authority, duplicate/stale messages and relevant lifecycle transitions are required. Preserve existing session, object, vehicle-authority and weapon-stat checks. Use a targeted negative/mutation check where it proves a new regression test detects the added behavior. The small marker rendering correction uses native/source comparison, numeric dimensions and a client compile; its visible result remains a human check.

Compiling native hooks and testing doubles do not prove x86 hook execution, ENet timing, visuals or gameplay. Those remain explicit human handoff checks. No original game files, derived game binaries or credentials enter source commits.
