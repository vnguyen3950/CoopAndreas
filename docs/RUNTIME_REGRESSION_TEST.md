# Entrance and NPC-drop diagnosis (0.8.1-alpha)

This build fixes the verified pickup filter for an originally random NPC whose native replica later becomes owned locally. It also records the runtime states needed to diagnose missing entrance markers and loot that only one player sees. The overall reported gameplay failures remain unresolved until a real guest run identifies the failing stage.

Use matching 0.8.1 client and server binaries on both computers. Each process creates a new JSON-lines log in `CoopAndreas_diagnostics` beside its executable. Logs are capped at 1 MiB; they contain fixed diagnostic codes and game state identifiers, with no names, chat, credentials or network addresses.

1. Connect host and guest. Keep the host outside, with menus and cutscenes closed. Let both clients run for ten seconds.
2. Have the guest approach a barber or another ordinary entrance. Note whether the marker appears. Repeat while the host is inside a shop, then after the host returns outside.
3. Kill one ordinary cop and one armed civilian/gang member, alternating who interacts with/kills the NPC. Check whether the other client can see and collect the supported drop. Avoid starting a story mission during this test.
4. Close the game and server normally. Keep each process's new log, and identify which came from host, guest and server. The local coordinator can read the logs on this PC; the friend must return their log separately.

Entrance `state` events record native cutscene, full control WORD, native co-op, replay and global disabled gates, transition state, native player bindings, access counts, nearest entry flags and buffered-packet timestamps. `controls & 0x20` is native player safety; `0x200` is the mod debug UI. The access flag is `nearest_flags & 0x4000`. A false native mission-eligibility query can prevent a transition while leaving its cone visible; all missing cones require checking the manager gates and access state.

Pickup `signature`/`enabled`/`disabled` events establish activation. Follow client readiness/local life, seal and manifest, server receipt/proof/publication, then client row/model wait/generation. Readiness bits are native enabled 1, authenticated 2, scripts ready 4, game state nine 8 and local focus 16. Service records are capped per fixed reason and globally, so use the first few test actors.

No diagnostic clears controls, changes entrance permissions or invents resources. Native money generation from a recreated mission-marked actor is still outside the provenance repair. Existing exit crash reports remain separate evidence.

After these regressions are resolved, the authorized next work is NPC combat/pursuit consistency, broader pickup support and gang recruitment. Persistence and new mission work remain queued.
