# Non-mission synchronization tests

The current development batch uses **0.4.0-alpha** on every client and the server. Earlier 0.3.x binaries fail the exact-version handshake. The reviewed mission script pair is unchanged; no additional stories were added in this batch. Native gameplay and visuals remain human validation work.

## Changes prepared

- NPC-driven vehicles now transmit engine, lights, broken-engine, siren/alarm, full alarm timer and dirt state through the existing NPC driver stream. A recorded player entry takes priority over a delayed NPC update. NPC horn behavior remains outside this slice. See [codec and authority evidence](../tests/npc_vehicle_parity/README.md).
- Supported native cutscenes require a unanimous skip vote from the captured gameplay-ready players. The overlay shows the count. Menu-only and late-ready peers do not join an existing vote or receive unmatched native lifecycle commands. See [controls, coverage and lifecycle checks](CUTSCENE_VOTES.md).
- Each player publishes their own maximum health, remaining breath and computed air capacity. Remote health and blue breath bars use that owner's values. Global lung/stamina skill state is not overwritten; this is not a rewrite of native swimming progression.
- Disconnect/reconnect resets the player registry on the game thread, removes an old slot before its replacement is created and protects teardown with the full native pool reference and remote-player binding. Queued messages from the old connection are discarded safely.
- Player map pins use one pixel scale for both axes, preserving their proportions at 4:3, 16:9 and ultrawide resolutions.

## Human checks

Use a separate installation, matching client/server builds and a fresh New Game. Close all lab programs before installing the selected prepared package; the updater retains window settings. Keep normal saves separate.

1. Drive or observe the same NPC vehicle on both clients. Compare lights, engine/broken state, siren/alarm countdown and dirt. Enter its driver seat as a player while updates are arriving; old NPC snapshots must not reclaim it. Repeat after a late join.
2. Run the complete [cutscene checklist](CUTSCENE_VOTES.md). Host-only and guest-only votes each leave a two-player scene playing at `1/2`; unanimity skips through original cleanup. Try held input, Alt+Tab, chat, departures, host migration, delayed loading, failure and identical-name replacement.
3. Compare players with different maximum health and lung/stamina progress. Damage/heal them independently, then dive and surface. TAB and name-tag bars must use the correct owner's health maximum and air capacity; local HUD/skills must stay their own. Check underwater recovery, death/respawn and the existing oxygen cheat.
4. Disconnect and reconnect, including reuse of the same server slot. Verify one remote actor per slot, no stale vitals/markers, no deleted local actor and safe repeated respawns/exits. Join from the menu before native gameplay starts, then load New Game.
5. Inspect player map pins at 4:3, 16:9 and ultrawide sizes. Their shape and direction should remain proportional; positions and visibility still require a visual check.
6. Recheck the existing paying mission, shared wallet/debt, wanted/bribe behavior and bottle/race tests to detect cross-feature regressions.

Record build/package identity, participant roles, resolution, exact steps and logs/screenshots. Source tests and SDK compilation do not prove native hook execution, ENet timing or multiplayer reliability. Campaign/player persistence is still a separate queued feature.
