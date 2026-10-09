# Animation, trailer and pickup checks

Use matching **0.6.0-alpha** clients and server in the separate test installation. Source reviews, headless regressions and all four x86 release targets pass. Native gameplay remains unvalidated. The previous 0.5 world package is preserved.

## What changed

| Feature | Supported behavior | Limits |
| --- | --- | --- |
| Player animations | Mirror four stock idle gestures and idle chat, including phase, completion and interruption. | Visual associations only; no general task replay or mission animation support. |
| Trailers | Native supported cab/tractor and trailer links, articulated pose, driver ownership and late-join replay. | Supported model families only; no train or general AI reconstruction. |
| Ordinary pickups | Exterior consumable health, armour, selected firearm/ammo and money pickups outside active missions. | Respawning, mission, shop, interior and collectible types remain outside this slice. |

Vehicle generations, creation nonces, pool references and script scenes prevent old lifecycle messages from adopting replacement actors. Pickup grants use the acknowledged player connection and actor birth. Native rewards go to the real collector; money uses the existing shared wallet observation once.

## Manual tests

1. Connect two players, including one who authenticates in a menu before starting New Game. Observe stock idle/chat gestures, then interrupt them with movement, combat, swimming, vehicle entry and mission start. Check that expired gestures do not restart after joining or loading.
2. Drive a supported truck/trailer or tractor pair while the other player observes. Turn, detach, reattach, change drivers, delete either endpoint and reconnect. Check both native references, articulation and current-owner control. Repeat with an unlinked ordinary vehicle and delayed model/pool readiness.
3. Approach one supported pickup simultaneously. Confirm only one collector receives a benefit and everyone sees removal. Check money increases the shared wallet once. Repeat after respawn, reconnect, range changes, a mission/interior transition and offscreen object recreation.
4. Kill an armed NPC near an existing dropped weapon pile. Check the documented merge quarantine and absence of duplicate ammo. Exercise delayed grants where the collector moves away or becomes ineligible.
5. Recheck NPC driving, fire ownership when a vehicle changes driver, map markers/waypoints, gang-war wave actors, cutscene voting, health/breath bars, mission cleanup and shutdown.

The first pickup slice deliberately retires a grant when the collector becomes ineligible before application. A native ammo merge that changes a shared pile also retires it, and extra merged ammo can be lost. Missing models, full pools and normally absent offscreen objects keep pending work inert. See [pickup policy](PICKUP_LIFECYCLE_NOTES.txt) for the exact distinction.

Report the client/server version, host and guest actions, affected model or pickup type, and whether the problem followed join, respawn, load, ownership transfer or retry. Existing window settings are preserved by the lab updater.

Detailed source limits and native checks: [animations](../tests/player_animation/README.md), [trailers](TRAILER_SYNC.md), [pickup integration](PICKUP_INTEGRATION.txt), [earlier world features](WORLD_SYNC.md).
