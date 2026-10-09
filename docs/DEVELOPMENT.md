# Development fork

The `coop-missions` branch contains the reviewed source work based on upstream commit `89aafbae93e86a61af6d8435690e8f8a7bc83240`. `main` retains upstream history; `upstream` points to Tornamic/CoopAndreas and `origin` points to the development fork.

This is a source development branch. All four x86 release targets and the full script pack compile, and bounded source reviews passed. The user confirmed two-player connection and windowed mode in the earlier startup build. The new mission adaptations have no multiplayer gameplay validation yet.

## Current changes

- Defer Discord initialization until the render engine starts, guard repeated initialization/shutdown, and resolve launcher paths relative to its executable.
- Load the optional [ThirteenAG Windowed Mode v2.2](https://github.com/ThirteenAG/III.VC.SA.WindowedMode/releases/tag/v2.2) helper through the existing proxy. Its separately obtained module and license are required; the helper is optional and is not bundled here.
- Fix guest mission falling-edge cleanup and cancel deferred cutscene starts during scene replacement/clear or mission teardown.
- Resolve remote PLAYER opcode parameters safely, preserve complete eight-character script names, and collect only spawned players into mission rosters.
- Replicate bounded mission-created object state/lifetimes and validate guest bullet hits before native host damage. Object queries remain host-local. Lifecycle/setters share reliable SCRIPT ordering, with model-ready queues, ownership/revision checks and non-reused IDs.
- Add host-authoritative guest checkpoint racing for the High Stakes instance, with a persistent main-block handoff, separate guest cars, ordered progress and results. Host story qualification remains against the original NPCs.
- Default the mission menu to the adapted candidates and add host-only guest input predicates for future cargo work.
- Integrate reviewed upstream vehicle-state/dirt and ped-lifetime/interior-transition fixes. See [upstream PR decisions](UPSTREAM_PRS.md) for provenance, limits and deferred features.
- Replace PR #106's balance/star/cheat mirroring with a server-owned wallet, explicit wanted policy and a bounded shared-cheat subset. See [session policy and native checks](SHARED_SESSION.md) for accounting, migration and coverage limits.
- Add NPC-driven vehicle state parity, unanimous native cutscene voting, per-player maximum health/air state and bars, guarded reconnect cleanup and proportional map pins. See [non-mission checks](NON_MISSION_SYNC.md).

Development source now uses **0.6.0-alpha** for changed player and vehicle lifecycle payloads. Clients and server must match; the existing handshake rejects 0.5 and earlier builds. The prepared `mission-batch-007` remains the reviewed **0.5.0-alpha** world package; see [world tests and limits](WORLD_SYNC.md).

The next increment has integrated five verified native idle/chat visual associations and an acknowledged actor-life API shared with the pickup work. Independent tests pass 82 service, 4,243 codec, 40 cross-role and 100,017 contract assertions; the stale-birth mutation fails three expected checks. All four x86 targets compile, and the isolated production-server ENet fixture passes 36 map/actor-life/respawn/version checks, including rejection of a real 0.5 peer before registration. Native animation blending and gameplay remain unvalidated. Trailer and ordinary pickup lanes are still undergoing implementation and review; follow [current tasks](../tasks/todo.md) and [animation runtime checks](../tests/player_animation/README.md).

## Adapted story candidates

The four existing adaptations are Big Smoke (11), Ryder (12), Tagging Up Turf (13), and Cleaning the Hood (14).

The twenty-two new candidates are:

| ID | Mission | Main guest role |
| --- | --- | --- |
| 15 | Drive-Thru | Escort cars, pursuit support and shared arrival gates. |
| 16 | Nines and AK's | Shared bottle shooting, target guidance and travel. |
| 17 | Drive-By | Armed convoy/combat support; host controls respray progression. |
| 18 | Sweet's Girl | Rescue cover and escort, preserving NPC seats. |
| 21 | Doberman | Support in the host's native gang war. |
| 23 | Gray Imports | Warehouse combat/chase; staged transfers around host-only barriers. |
| 28 | Running Dog | Escort and fleeing-target pursuit. |
| 32 | Madd Dogg's Rhymes | Infiltration/cover; host collects the book. |
| 34 | House Party | Defense and wave guidance. |
| 36 | High Stakes, Low Rider | Convoy plus specialized race controller 35. Other tournament variants are unadapted. |
| 39 | Badlands | Armed cover; host supplies the required photograph. |
| 41 | Local Liquor Store | Pursuit and host-checked briefcase collection. |
| 42 | Small Town Bank | Exterior cover and bike escape; host handles bank objects. |
| 43 | Tanker Commander | Delivery escort. |
| 44 | Against All Odds | Interior cover/escape; host handles demolition. |
| 46 | Body Harvest | Farm combat and combine escort. |
| 49 | Wear Flowers in Your Hair | Independent bike tour/rallies, with guest seat protection. |
| 58 | Photo Opportunity | Travel/roof rally and lookout role; host supplies photographs. |
| 62 | Toreno's Last Flight | Helipad assault, rockets and helicopter pursuit. |
| 65 | T-Bone Mendez | Bike pursuit and host-checked ground parcels; attached snatching stays host-only. |
| 78 | Verdant Meadows | Synchronized cinematic/control state and final placement. |
| 87 | Fish in a Barrel | Synchronized cinematic/control state and final placement. |

Four candidates remain unchanged: Home Invasion (24), Catalyst (25), Deconstruction (50), and Yay Ka-Boom-Boom (63). They need shared carrying/noise, native train/carriage mapping, existing world-object adoption/construction physics, or ordered IPL/geometry changes. The bottle implementation supplies none of those mechanisms by itself.

## Reproducible lab layout

Keep game files, downloaded tools and generated outputs outside the checkout:

```text
workspace/
  CoopAndreas/                 source checkout
  gta-reversed/               optional source reference for capacity checks
  tools/xmake/xmake/xmake.exe
  script-build/tools/sanny-builder/sanny.exe
  tools/windowed-mode-v2.2/    optional helper and its license/config
  artifacts/                  compiled and frozen test batches
  staging/                    install files
  game-lab/                   the user's separate compatible game copy
  backups/
```

Use Visual Studio 2022 C++ tools with the x86 compiler and a Windows SDK. The tested lab used xmake 3.1.1 and Sanny Builder 4.2.0. The mod targets the compatible 1.0 US game installation, as described by upstream; the lab updater intentionally verifies the executable it was developed against.

From the checkout:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\devtools\build-coop.ps1 -Mode release
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\devtools\build-scripts.ps1 -Source WorkingTree -BuildName mission-build-current-001
```

`-WorkspaceRoot` overrides the surrounding workspace directory. The build helper clears `GTA_SA_DIR` during compilation so outputs stay in the artifact folder. The script helper freezes SCM/SDK inputs, serializes compiler access with a mutex, and checks actual compiler diagnostics and engine capacities; Sanny's exit code alone is insufficient.

Staging requires a successful build report with source review marked READY, matching live source hashes, and no preliminary-only flag. The updater selects a frozen matched batch through `artifacts/approved-build.json`, refuses running lab programs, backs up binaries/scripts, verifies copies and preserves window settings. Compiled SCM/IMG files already tracked by upstream were left unchanged; rebuilt pairs are generated outside the checkout.

## Validation

The final mission build compiled 135 missions, 79 streamed scripts and 31 custom opcode definitions. Main block: 194,216/200,000 bytes. Largest mission: unchanged Reuniting the Families, 68,439/69,000 bytes. Mission locals: 964/1,024 slots.

Transport-independent object tests passed 122,243 assertions against header SHA-256 `8CCA507880D14AB5B829B44B477FE7E229814B61471F7B11C5773BFB10123523`. They cover identity lifetime, ownership/revisions, capacity, finite bounds and malformed setter envelopes. They do not execute native game rendering, collision or damage.

Run the source tests without launching a game:

```powershell
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -static .\tests\object_sync_tests.cpp -o ..\artifacts\object_sync_tests.exe
..\artifacts\object_sync_tests.exe
g++ -std=c++11 .\tests\version_compatibility.cpp -o ..\artifacts\version_compatibility.exe
..\artifacts\version_compatibility.exe
```

Development used OpenAI Codex with collaborating GPT-6.1-Sol agents. The upstream project and its GPL license are retained. [gta-reversed](https://github.com/gta-reversed/gta-reversed) and the existing plugin SDK were used as behavioral references; the gta-reversed DLL is not installed alongside this mod.
