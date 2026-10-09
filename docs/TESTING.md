# Multiplayer testing

Use a separate compatible installation that you own, the normal CoopAndreas launcher/key flow, and a server you control. Install the same reviewed client/script pair and matching server on every test machine. Keep normal progress separate from the test lab.

Close lab games, the launcher and server before updating. Start the server, then launch both clients with distinct names. For this follow-up, start a fresh **New Game**: the persistent main script changed for the race handoff, and existing saves have not been validated against the changed instruction offsets.

The local object/vehicle protocol is **0.3.2-alpha**. Restart the server after replacing its executable; older 0.3.0-alpha and 0.3.1-alpha binaries are rejected by the version handshake.

## Mission launch

Connect all participants before starting a mission. The captured roster supports up to three guests; late joining is outside these mission tests. As the authenticated host, stand outside with no active mission, close chat, type **D1212**, enable **Missions**, and select a candidate. The menu defaults to adapted stories; exposing unadapted entries does not add guest support.

## Every candidate

1. Check guest placement, weapons/transport, objectives and markers.
2. Let a guest lag behind at a group rendezvous, then catch up. Check that the gate waits and resumes correctly.
3. Complete the mission and check host progression, controls/camera/fade, markers and vehicle cleanup.
4. Fail normally, then retry without reconnecting. Check for stale entities, duplicate transport or stuck input.
5. Disconnect a guest during travel/combat. Check for crashes or a gate that cannot progress.

Some mechanics remain host-owned, including photography, book/object collection, demolition, native gang-war counters and specialist driving. Follow the guest roles in [DEVELOPMENT.md](DEVELOPMENT.md); a support adaptation does not imply independent guest ownership of those mechanics.

## Nines and AK's

- Confirm all guests see the same one/three/five bottle groups and their replacements.
- Shoot a target as a guest and confirm the original host counter advances once and the target disappears everywhere.
- Check native NPC demonstrations, cinematic skips, failure, immediate retry and disconnect/reconnect. Old IDs and late hits must not affect replacement targets.
- At the Emmet and Smoke-home rendezvous, drive the host away while another guest is still approaching. The cinematic must wait for the original car-location, Smoke-seat and on-wheels conditions again.
- Check delayed model loading and normal menu/gameplay exits. The bounded queue can time out after five seconds; note missing/stale targets or resnapshot messages.

## High Stakes, Low Rider

- Launch story mission **36**, complete its prelude, and verify automatic handoff into specialized race controller **35**.
- Check separate guest cars, grid/countdown, ordered checkpoints and recorded finishes.
- Try vehicle loss, guest death/disconnection and a slow finisher. Check disqualification, bounded result waiting and cleanup without deleting occupied cars.
- A guest finishing ahead must not make a host who beats the original NPCs fail the story mission. Inclusive race standings are separate from that host qualification.
- Guest messages currently show podium/winner/disqualification outcomes; individual numeric standings need another targeted text API.

## Reports

The new upstream integrations also need the vehicle and interior/ped-lifetime checks listed in [UPSTREAM_PRS.md](UPSTREAM_PRS.md). In particular, compare player-driven and idle vehicle states and check cancelled door transitions after mission transfers. NPC-driven vehicle flag parity is outside the imported vehicle feature.

Record mission name/ID, host/guest role, client count, one or multiple PCs, exact steps, last objective and result. Identify the binary/script pair with the local build manifest and include relevant screenshots/crash/server logs. Keep account keys and credentials out of reports.

The earlier two-player connection and windowed-mode checks passed. New mission gameplay, native object damage/visibility, cutscene timing, failure/retry/disconnects and shutdown behavior remain human validation work.
