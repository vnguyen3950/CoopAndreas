# Cutscene skip votes

During a supported co-op cutscene, a small overlay shows **Skip votes: X/Y**. Press the normal game skip button: Enter, Space, left mouse button or controller Cross. Release the button before voting in a new scene. Each captured player votes once; the scene skips only when every remaining captured player agrees. The overlay confirms when your vote is counted. Opening chat or switching away from the game does not vote.

A player who joins or becomes gameplay-ready after the vote starts does not change its threshold. A departing player is removed from the threshold, including their vote if already counted. The host must vote too. Nobody voting, or an empty electorate, never automatically skips a scene.

Host migration cancels the former host's pending vote. A legitimate new-host START/BEGIN is retained when SCRIPT and SYSTEM notifications arrive in either order, including deferred playback. Mission failure/end, CLEAR, replacement LOAD/START and natural completion also cancel the pending vote. A cancelled scene continues normally until its native script cleans it up. A late commit never skips a replacement scene, including one with the same name.

## Verified coverage and limits

This feature covers synchronized, spline-backed native `CCutsceneMgr` scenes launched by the existing registered SCM opcode stream, with a synchronized LOAD before START. Guest START requests waiting for models retain the announcement and local scene ticket until native playback begins. Replacement LOAD, CLEAR and cancellation invalidate that pending start. The feature neither loads scenes independently nor calls Finish/Delete to bypass native cleanup.

Script-only camera sequences, scripted dialogue and short fades without a native cutscene are outside this feature. The game's unskippable `finale` is excluded. Unmanaged/offline scenes keep their original input behavior. Cutscene rendering, timing, camera restoration, subtitles and mission progression still require human game tests; passing headless tests is not gameplay validation.

Eligibility is explicit vote-module state, independent of the vitals lane. Guests become gameplay-ready after their first authenticated, successfully decoded owner gameplay snapshot: on-foot, an accepted driver update after the existing vehicle ownership check, or a passenger update for an existing vehicle. The host's authenticated native LOAD is also gameplay evidence, avoiding a first-frame race with its initial SYNC snapshot. Authentication in the menu alone is insufficient. Readiness is retained for that connection until departure; a captured player who pauses or becomes unavailable without disconnecting must return to vote or leave.

Native cutscene delivery has a separate connection-identity ledger. LOAD is relayed only to ready recipients, START only to those identities that were sent LOAD, and the electorate only includes identities sent both. A peer becoming ready after LOAD receives neither a lone START nor a vote slot, even if ready before BEGIN. It waits for the next synchronized LOAD/START; this batch does not reconstruct an ongoing scene for late observers. CLEAR reaches prepared recipients even after vote/mission cancellation, then clears the delivery ledger. Departure/ID reuse cannot inherit scene preparation. This filtering applies only to cutscene lifecycle relays; unrelated opcode delivery is unchanged.

## Native, wire and authority contract

Four dedicated packet classes use reliable SCRIPT: `CUTSCENE_VOTE_BEGIN`, `CUTSCENE_VOTE`, `CUTSCENE_VOTE_STATE`, `CUTSCENE_VOTE_COMMIT`. Existing enum values are unchanged in this worktree. Root owns final append ordering after `PLAYER_VITALS` and the merged `0.4.0-alpha` version; this lane does not change config/version checks.

The server authenticates the actual ENet peer and resolves its current player object. A host BEGIN is accepted only after its prepared native START relay. The server assigns a monotonic scene generation and connection identity, captures ready scene recipients, derives totals, ignores duplicate/stale votes and commits only at unanimity. Client-supplied STATE/COMMIT packets are discarded. Player ID reuse cannot inherit a captured slot. Host ID, generation, START serial and a separate local scene ticket bind announcements and commits. Migration discards prior-host queued vote packets while retaining packets attributed to the newly assigned host. A bounded staged announcement retains a new scene processed before its SYSTEM authority notification; no vote/skip is authorized until assignment confirms it. LOAD/CLEAR/replacement invalidate staging. Reconnect discards queued votes. Old-host commits cannot overwrite the new binding.

Lifecycle opcode relays are host-validated and stamped with current server time. A narrow client queue guard keeps cutscene lifecycle/announcement packets behind already queued SCRIPT predecessors, so a normalized START cannot overtake preceding setup with an ahead-of-server legacy timestamp. Unrelated packet timestamps and insertion behavior are unchanged.

Minimal shared integration: initialization/frame/draw hooks; START/LOAD/CLEAR/END observation in opcode sync; mission-end and connection/host lifecycle notifications; three readiness notifications in the existing server gameplay snapshot handlers. No session, money, stat, object or vehicle authority/serialization policy is changed. No SCM, SDK, mission catalog or persistent saving changes.

Root integration must retain the separate packet-buffer reconnect repair from main commit `5f1b611`, including Clear on disconnect and safe pop-before-dispatch. This lane adds only the cutscene queue notification at Receive; its dedicated queued-vote discard can coexist with root's complete queue clear. Retain the vote reset alongside root/vitals registry reset hooks in CNetwork, and the three gameplay-readiness notifications when merging the shared snapshot handlers. This worktree does not cherry-pick root's repair.

### Native evidence

Read-only disk check of the supported executable, SHA256 `a559aa772fd136379155efa71f00c47aad34bbfeae6196b0fe1047d0645cbd26`:

| Hook call site | Original bytes | Target |
| --- | --- | --- |
| `0x5B1947` | `E8 C4 43 F2 FF` | `0x4D5D10` |
| `0x469F0E` | `E8 FD BD 06 00` | `0x4D5D10` |
| `0x475459` | `E8 B2 08 06 00` | `0x4D5D10` |

Adjacent gta-reversed `source/game_sa/CutsceneMgr.cpp` implements the bool native query (`0x4D5D10`), native skip (`0x5B1700`), finish (`0x5B04D0`), completion predicate (`0x5B0570`) and update/skip consumer (`0x5B1720`). `source/game_sa/Scripts/RunningScript.cpp` implements the skip-label consumer at `0x469F00`. The bundled SDK declares the query as void; this lane calls the original address with a bool return without changing the SDK. Native source includes loss of foreground as a skip trigger; voting excludes it and uses `isForeground` at `0xC920EC`, identified in `source/app/platform/win/WinPlatform.h`.

The hook returns false while voting, then returns true for the matching committed, loaded, playing, unfinished scene. The original caller handles skip flags, camera completion and script cleanup. No GUI keys are injected. Root must review these native call sites before merge.

## Headless validation

From the worktree:

```powershell
python tests/cutscene/run_tests.py --output .cache/cutscene-headless/my-unique-run
```

The output directory must be new. The runner clears `GTA_SA_DIR`, freezes and hashes production inputs, and uses MSVC with `/std:c++17 /W4 /WX`. It includes real packet wrappers and `serialize.h`; production services are extracted with includes removed and only the fixed-address foreground read doubled. Native calls/SDK containers have explicit recorded doubles. Actual deferred START and buffer insertion bodies are extracted from Main and CPacketBuffer. No game process, DLL loading or native address execution occurs.

Cases cover authenticated authority, duplicate/stale/forged votes, connection reuse, ready/menu/late-ready peers, voted and nonvoted departures, both channel arrival orders during host migration, identical-name replacement, delayed commits, deferred START, cancellation/failure/disconnect, focused input release, legacy timestamp ordering, exact bit counts, complete round trips and every shorter packet payload. The actual server opcode relay body is also extracted to verify bounded menu/late-ready lifecycle delivery and preservation of unrelated relays. Independent review repros `vote-migration-001` and `vote-menu-relay-002` are incorporated in the owned suites. Source hashes and compile/test results are recorded in `result.json`.

## Human checklist

1. With two gameplay-ready players, enter a supported native mission cutscene. Verify both see `0/2`. Host-only and guest-only votes must each leave it playing at `1/2`; the second vote must skip both clients through native cleanup.
2. Let another scene finish without votes. Confirm normal timing, controls, camera, subtitles and mission progression. Skip a subsequent scene and confirm no vote carries over.
3. Alt+Tab with a held skip button, return while holding it, and open/close chat. None should vote. Release and press in the focused game to vote once.
4. Connect a menu-only client before LOAD/BEGIN. It must receive no native LOAD or unmatched START and must not increase the threshold. Let it become ready or join mid-scene; it waits for the next LOAD/START and stays outside the current roster.
5. Have a voted guest leave, then a nonvoted guest leave. Remaining totals/votes must update correctly, without deadlock or an extra vote from a reused player ID.
6. Exercise slow guest loading, mission failure/retry, consecutive identical-name scenes and host departure. Old votes/commits must not skip a later scene; cancelled deferred starts must not run after teardown. Check control/camera restoration and the next mission objective.
7. Confirm script-only cinematics and the finale retain their original coverage and skip restrictions.

Coordination: explicit `herdr agent prompt w1:p1` attempts returned OS error 5, `Access is denied`; neither notification was delivered. This document contains the contract and shared-handler ownership for root's source review. The stopped `los-santos-next` worktree was clean at `7a09780`, with no mission drafts or edits.
