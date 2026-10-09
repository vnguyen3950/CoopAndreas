# Trailer synchronization

The current parent vehicle authority controls a single mapped native CTrailer link and its articulated frame. Native references are installed through SDK virtual SetTowLink/BreakTowLink, not pointer-only attachment. Independent guest auto-attach/detach is rejected; canonical replay cannot echo a local request. An owner detach awaiting its reliable response is not undone by an older attached snapshot. Towtruck cars, train coupling, multi-trailer chains and arbitrary AttachEntity mechanics are outside this batch.

Supported native model pairs: 435/450/584/591 behind 403/514/515; 606/607/608 behind 485/583; 610 behind 531; 611 behind 552. Runtime hitch offsets, support suspension, collision response and visual smoothness still require human testing.

## Authority and identity

Vehicle IDs 0..254 carry a server-allocated nonzero uint31 birth in spawn/confirm/remove/syncer assignment. Hosted requests also have a monotonic nonce; old confirmation cannot bind a reused temp slot. Generation is a birth identifier, not another owner source. Current valid player driver seat wins over assigned idle syncer. NPC driving requires the current ped generation/owner epoch/state sequence and driver vehicle/mode; stale m_bUsedByPed alone never grants authority. Parent lease epochs also advance when an authenticated player connection incarnation changes, even if player slot/pointer repeat.

TRAILER_HELLO, TRAILER_LEASE and TRAILER_LINK use reliable EVENT. TRAILER_POSE uses sequenced unreliable SYNC. Server replaces trailer framing times with its current clock. Lease packets have a scene receipt mode; requests include the current ready scene. Only initialized, authenticated recipients receive module state. All caches, pending births, lease/link/pose records and tombstones are bounded to 255 slots. Poses require finite bounded positions, near-unit orthogonal axes, bounded velocity/turn rate and health. Reliable detach tombstones defeat delayed poses; poses ahead of their reliable link are staged until that exact revision arrives.

Existing vehicle occupancy/syncer rules remain the authority source. The trailer owner receives the linked child syncer; existing CFireSync::VehicleChanged calls remain intact. Legacy idle snapshots for an attached child are suppressed, preventing a second pose stream from overwriting its articulated frame. The regular vehicle deletion policy is retained; deleting either endpoint invalidates the link.

## Native lifetime and readiness

Public integration seam: `bool CNetworkVehicle::HasValidVehicle() const`, delegated to `CTrailerSync::NativeValid(const CNetworkVehicle*)`. It checks local script scene, full native pool reference, pool membership, native matrix and model. `GetVehicle(int)` resolves a valid native mapping; `GetVehicle(CEntity*)` scans all matching candidates until one is valid. `FindVehicle(int/CEntity*)` retains raw registry lookup for targeted lifecycle cleanup. Root must use valid mapping first and reject stale raw mappings before any fire host-world ownership fallback.

Auth-menu spawns are retained without constructing native vehicles. Initialized HELLO replays all existing regular mappings, including unlinked vehicles, with explicit idempotent syncer assignment. This is required compatibility for the new validity gate, not mission/task/AI reconstruction. Native model or pool failure retains the exact birth for 500 ms bounded retries; failed wrappers are not exposed as mappings. Removed births cancel queued spawns even when no wrapper exists; a lower-birth removal cannot cancel a newer queued birth. Tombstones persist across a connected script restart and clear at connection reset. Reset binds the current connection baseline so a spawn after handshake is not invalidated by the first Process call.

Unconfirmed temp wrappers are canceled on script/connection reset and on exact native full-reference destruction. Up to 255 canceled request-token/slot pairs are retained in a bounded ring. A matching late confirmation retires its exact orphaned server birth; unknown/evicted cancellations are ignored rather than inferred from a raw slot. A valid current mapping defeats duplicate orphan retirement. Temp capacity exhaustion sends no temp ID 255 and frees only the wrapper, preserving the native local vehicle. Native generation/ref/scene and nonce prevent stale confirmations and pointer matches from adopting replacement actors.

## Verified native evidence and event order

Supported executable SHA256: a559aa772fd136379155efa71f00c47aad34bbfeae6196b0fe1047d0645cbd26. Disk bytes confirm SDK virtual slots 61/62: automobile entries 0x871214/0x871218 contain 0x6B4410/0x6A4400; trailer entries 0x871D1C/0x871D20 contain 0x6CFDF0/0x6CEFB0. All four original pointers are checked before any hook. A mismatch disables trailer hooks; regular vehicle readiness/replay still operates.

Adjacent gta-reversed Trailer.cpp implements SetTowLink at 0x6CFDF0 and BreakTowLink at 0x6CEFB0: reciprocal registered references, tow status and moving lists are changed. SDK CVehicle wrappers dispatch the actual dynamic slots. SDK native allocator at 0x6E2D50 calls vehicle-pool New; reversed Vehicle.cpp confirms the pool allocation. The native pool is CPool<CVehicle,CHeli>. Creation checks pool capacity, model pointer and LOADSTATE_LOADED, then checks the native allocation result before global placement construction. It never invokes a constructor on nullptr storage.

The custom gameShutdownEvent call at 0x748E6B targets CGame::Shutdown (0x53C900), and existing CCore::Init disconnects before it. Disk call 0x53C98C invokes CWorld::Remove during that shutdown. SDK script-init sites 0x53BDD7, 0x5BA340 and 0x5D4FD7 call CTheScripts::Init (0x468D50). Reversed CGame::ShutDownForRestart (0x53C550) clears the world before ReInitGameObjectVariables (0x53BCF0) initializes scripts. CTheScripts::Init itself initializes scripting rather than deleting the vehicle pool. This does not establish the suggested scripts-before-owned-car-deletion trace; no bypassed shutdown path was patched to satisfy that synthetic concern.

Room policy: normal shutdown disconnects and existing authenticated vehicle removal/disconnect cleanup applies. A connected guest script reset preserves server world identities and requests fresh native mapping replay. A deliberate initialized controlling-host script restart advances the trailer room epoch and cancels links/pose high-water state; it does not invent deletion of server cars that native/script lifecycle did not remove. Standalone script reset, save/load and seat/task behavior remain focused human checks.

## Headless evidence and human checks

Root integration additionally rejects confirmations for retired or older births,
releases only the matching temporary nonce, and preserves newer queued birth
high-water across connected script reset. Generic spawn/remove relays use the
server clock so a sender cannot reorder their lifecycle against confirmations.
The original independent retirement/confirmation and queued-model cases failed
before these guards and pass against the corrected actual handlers/constructor.

Run `python tests/trailer/run_tests.py --output .cache/trailer-headless/unique-new-name`. Actual module codecs/services, birth extension serializer lines, vehicle handlers, manager getters/temp allocator, constructor/CreateVehicle and CreateHosted are extracted from hashed production source. SDK/native primitives are recorded doubles; they do not execute game addresses. Birth extraction deliberately omits unchanged legacy position/rotation compression; its wire test covers the newly changed birth/nonce/syncer fields. Disabling actual parent-owner attach validation must cause two failures. Native lifecycle/physics is not proven by codec tests.

The isolated release builds use actual trailer worktree source, unique cache output, x86 MSVC and GTA_SA_DIR cleared. No SDK/SCM/config edits, deployment or gameplay was performed. Root owns final enum order/version, merged integration and fire native readiness adoption.

Human checklist:

1. Authenticate in menu, start New Game, then observe an unlinked vehicle and a linked truck/trailer pair. Confirm one mapping per birth, both references and correct cab/trailer articulation.
2. Drive and turn each supported family with a second observer. Confirm child pose follows its actual owner; detach/retry does not immediately reattach from an older packet.
3. Transfer a cab driver before the first new driver snapshot. Confirm old owner cannot control trailer; validate player and stamped NPC driving separately.
4. Delete either endpoint, change cab after detaching, disconnect owner and reuse slots. Confirm no dangling references or revived old links.
5. Reconnect and perform a connected guest script reset/load under the user's testing policy. Confirm valid mapping replay, no duplicate native actor, no stale saved pool reference adoption and no unlinked mapping stranded by readiness.
6. Exhaust native vehicle or temp capacity deliberately in a private test. Confirm pending exact births retry when capacity/model returns, and temp failure preserves the local car.
7. Check script mission cleanup, fire owner transfer and shutdown behavior in root's merged integration. No automatic runtime claim is made.
