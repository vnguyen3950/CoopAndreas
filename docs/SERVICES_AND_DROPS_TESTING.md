# Shops, NPC loot and Space voting (0.8.0-alpha)

Everyone must use the matching 0.8.0 client and server. Headless tests and builds check the implementation; gameplay still needs these checks on two real clients.

## Local shops

As a guest, enter a barber and open its menu. Buy a haircut and check that only your appearance changes and the shared wallet is charged once. Repeat for clothes, tattoos, Ammu-Nation and food. Let the host use a shop too. Shop entry waits for the canonical wallet and local player to initialize. New guest retail entry is disabled while a shared mission is active.

Check a story contact still cannot start a mission for a guest. Try two players using the same attendant and note any animation contention. Native shop behavior and simultaneous attendant interactions need gameplay validation.

## Ordinary NPC drops

Kill a cop, civilian and armed gang member, alternating which client owns/interacts with the NPC and which player kills it. Both players should see the same supported firearm drops and native civilian/gang cash. Stock cops, medics and firemen do not drop cash. Let the other player collect: the item should disappear for both, with one native resource benefit. Cash enters the existing shared wallet once.

Repeat near an existing weapon pile and after a third player joins. The new drop should have its own lifetime and should not change the existing pile. This slice covers supported random exterior NPC firearms and cash outside missions. Interior, mission-created, player-death, melee/heavy/throwable and persistent mission loot remain outside it. See [the exact scope](../tests/NPC_DROP_SHARING.md).

## Cutscene votes

During a managed shared native cutscene, try Enter, mouse and controller buttons: none should vote. Release Space, then press it to vote. Everyone captured for the scene must vote to skip. Hold Space before a scene starts and confirm it needs release/repress. Repeat after chatting, Alt+Tab and a second scene with the same name.

Offline and excluded scenes keep the game's original skip controls. Short script-only cinematics are not automatically covered by the native cutscene vote system. See [the input contract](../tests/cutscene/SPACE_ONLY_INPUT.md).

If a check fails, record the build version, host/guest roles, location and exact steps. Keep the newest crash log if a client crashes.
