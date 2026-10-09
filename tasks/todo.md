# Current development tasks

This is the operational list for non-mission development. README retains the broader upstream backlog. Checkboxes describe reviewed source/build preparation; gameplay validation is recorded separately. Gamepad support is skipped at the user's request.

## Current world synchronization batch

- [ ] NPC lifetime generations, explicit ownership, retained state and reliable join replay.
- [ ] Host-controlled native fire lifecycle, owner-target effects and validated guest requests.
- [ ] Shared map discovery, player positions and connection-safe waypoints.
- [ ] Host-controlled gang wars, pinned native wave NPCs and shared territory densities/colors.
- [ ] Independent review, combined regression checks and matched 0.5.0-alpha client/server package.

Native gameplay testing remains with the user. The prepared 0.4.0-alpha package stays selected until the next combined package passes its checks.

## NPC-driven vehicle state

- [x] Verify the real NPC driver packet, sender/receiver ownership and native flag/dirt semantics.
- [x] Implement validated NPC-driven engine/light/broken/siren/alarm/dirt parity without weakening vehicle authority.
- [x] Add real serializer and authority regressions; horn remains outside this slice.

Acceptance: authenticated NPC owner updates only its mapped vehicle; finite/range/boolean validation passes; delayed/foreign updates cannot take control. Verification: focused packet/authority tests, isolated compile, root combined release build and manual NPC/mission-car checks.

## Native cutscene skip voting

- [x] Establish a scene generation and capture eligible connected voters.
- [x] Implement native skip requests, unanimous commit, vote display and generation-aware cancellation.
- [x] Cover duplicate/stale votes, late joins, departures, host migration, replacement, teardown and delayed commits.

Acceptance: one authenticated vote per captured identity; unanimity controls the verified native skip path; a stale vote or commit cannot skip a replacement scene. Verification: production contract/service tests, codec truncation checks, native call-site evidence, combined build and manual voting checks.

## Per-player max health and breath

- [x] Verify PlayerInfo/ped max health, remaining breath and global lung-stat relationships.
- [x] Implement per-player transmission/application and join/spawn lifecycle handling.
- [x] Test finite bounds, authenticated ownership, missing/recycled entities and packet serialization.

Acceptance: each player's values remain their own; remote application does not overwrite local/global skill state; existing fourteen-float weapon stats and reserved slots remain unchanged. Verification: actual packet/production-function tests, SDK compile and manual damage/diving/bar checks.

## Map marker proportions

- [x] Reproduce the aspect-ratio distortion from the actual drawing function.
- [x] Correct marker scale using native rendering evidence and verify affected dimensions.

Acceptance: marker geometry keeps its proportions at 4:3, 16:9 and ultrawide resolutions while retaining readable size and native positioning. Verification: native/source comparison, dimension checks, client compile and human visual checks.

## Integration checkpoint

- [x] Review each worker's tests and committed diff.
- [x] Resolve packet enum/startup conflicts, select matching protocol and run existing/new checks.
- [x] Build all four release targets and verify unchanged SCM/SDK against the reviewed script pair.
- [x] Update README/status/testing docs; commit and push the development branch.
- [x] Freeze a matched package and verify its updater in a filesystem fixture.
- [x] Hand off human tests; native runtime validation remains pending until the user reports results.

## Queued after this batch

- Passenger gamepad support: skipped by user request.
- [ ] Idle/gesture animation synchronization.
- [ ] Broader pickup lifetime/collection/reward accounting.
- [ ] Additional shared cheat entries with verified action/toggle semantics.
- [ ] Gang-group recruitment and trailer synchronization beyond the current gang-war slice.
- [ ] Mission candidates: OG Loc, Los Sepulcros, The Green Sabre, Jizzy, Outrider and Lure.
- [ ] Separate campaign/player persistence design and implementation.
