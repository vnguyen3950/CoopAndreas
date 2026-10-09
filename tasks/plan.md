# Non-mission synchronization batch

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
