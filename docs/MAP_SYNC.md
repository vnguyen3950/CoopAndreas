# Shared maps

The current source batch shares explored map cells, remote player pins and each player's waypoint. Territory densities and colors belong to the gang-war service. This batch requires matching 0.5.0-alpha clients and server; it is not yet a gameplay-validated release.

The controlling host's initialized game seeds the native 100-cell explored bitmap. Each initialized outdoor participant contributes its current cell at the native five-second observation cadence. A guest's previously explored personal save is not merged wholesale. The server holds the union, replays it to joins and preserves it when the host changes. Empty-room departure clears it. A promoted guest in menus/loading adopts the cached room map on first initialization. A later deliberate New Game/load by an already initialized controlling host starts a fresh epoch.

`MAP_DISCOVERY` uses reliable SYSTEM ordering, server-assigned campaign epochs/revisions and the current host's authenticated connection generation. Input is finite/bounded, requests have increasing per-connection sequences, reveals contain exactly one valid cell, and stale epochs cannot change the room. Native bitmap/count writes happen only with an initialized, valid local player at focus zero. This adds no server-side disk persistence. The shared bitmap becomes part of an ordinary native save if that client saves it.

Waypoints use reliable SYSTEM ordering with player connect/disconnect and existing vitals identity messages. The server stamps the authenticated sender and connection generation; each owner increases a sequence. Guests reject stale or mismatched lifetimes before changing a marker. Joins replay current markers. Native target polling also handles a waypoint placed before connection and menu paths outside the old mouse hook. Player pins validate their native pooled actor binding and calculate coordinates directly without changing PlayerInFocus.

Native references: gta-reversed `TheZones.h`/`TheZones.cpp` at reference commit 01709e8, bitmap 0xBA3730 and count 0xBA372C; the 10-by-10 index is column-major with inverted Y and positions clamped to +/-2999. The bundled SDK agrees on those two fields. `MenuManager_Input.cpp` and `MenuManager_Pages.cpp` show `m_nTargetBlipIndex`; the validated radar handle supplies its current position. No SDK or game source is modified or copied into the implementation.

## Verification

Run `python tests/run_map_sync_tests.py --output .cache/map-sync/<new-directory>`. It freezes and hashes production inputs, exercises complete map/waypoint packet classes and unchanged client/server map services, and records native pool/events/zone/ENet doubles. Separate client/server executables check the real SenderPlayerId C2S/S2C asymmetry. Every shorter payload is rejected. The menu-host-migration regression originally failed twice; the current service adopts the old campaign correctly. `--mutation` removes only the waypoint generation comparison from a frozen copy and should fail the recycled-owner tests.

## Human checks

1. Connect two players with different saved discovery. Confirm the host's map is the initial shared map; explore an undiscovered outdoor cell as a guest and compare both pause maps after five seconds. Interiors must not unlock unrelated outside cells.
2. Place, move and clear both players' waypoints. Join late, reconnect into the same player slot, and confirm current markers are replayed and old markers stay gone. Test a waypoint set before connecting.
3. Compare remote player pins on radar and pause map at 4:3, 16:9 and ultrawide sizes, on foot/in vehicles and after remote respawn. Pins must not disturb the local player HUD/control focus.
4. Let the host leave while another connected participant is still in menus. After that participant initializes a game, confirm it adopts the room discovery. Then deliberately load a different save as the controlling initialized host and check the new campaign replaces the old discovery.

Native rendering, save/load event timing and real ENet/gameplay remain unvalidated by the headless suite.
