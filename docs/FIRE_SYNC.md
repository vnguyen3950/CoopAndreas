# Fire synchronization

The room host controls the native `CFireManager` world: creation, spread, merging, water effects, expiry and script cleanup. Initialized guests display canonical fire replicas. Normal locally owned Molotov/flamethrower ignition and extinguisher/fire-truck water calls submit bounded requests to the host. Remote replay creators and anonymous guest spread do not publish requests. The host deduplicates existing ignition before creating it.

Attached burn damage follows the actual entity owner. A guest-owned NPC/player/vehicle burns on that owner's client; its host/other-client representations cannot add duplicate burn damage. Guest `ProcessFire` does not run native spread, world damage, merging or expiry. PED burning uses the native on-fire event scanner/task and guarded `ComputeFireDamage`, not a second damage loop. Vehicle damage requires both current native ownership and the canonical owner grant. Fireproof, swimming/death and normal native owner extinguishing remain meaningful. Offline/uninitialized gameplay keeps original native behavior.

## Identity and lifecycle

The native pool has 60 slots. Every replicated fire has a bounded slot, host epoch, slot generation and sequence. Removal retains a tombstone; delayed state cannot revive that generation. Host migration discards the previous epoch and managed replicas. A controlling-host campaign load deliberately advances the room epoch before new slot counters publish. HELLO carries a monotonic per-connection script generation; guest restart changes its player attachment incarnation even when native pool reference/model repeat, while preserving the world's other fires. Requests carry connection, script generation and issuer birth identity. RESET acknowledges the current local script generation and the exact recipient player birth assigned by the server. Own-player capture, resolution and binding promotion require that exact birth and connection, including when the previous birth was never received before restart. Future own EVENT bindings stay staged until their matching SYSTEM receipt; an older receipt cannot discard them or roll the acknowledged birth backwards.

`FIRE_RESET` is reliable SYSTEM. `FIRE_HELLO`, `FIRE_STATE`, `FIRE_REMOVE`, `FIRE_REQUEST` and `FIRE_BIND` are reliable EVENT. The client stages at most one future epoch of 60 states/tombstones plus bounded player/vehicle bindings until RESET confirms it. Host assignment suspends mismatched authority without deleting a processed or queued newer epoch. Stale resets/requests are rejected. Fire EVENT times preserve predecessor EVENT ordering; delegated guest request framing times are replaced with current server time before relay.

HELLO is sent only after initialized gameplay, and replays the room's bounded live states/tombstones and bindings. Native entity effects stay deferred until their mapping/model is ready. PED identity is slot `0..254`, nonzero generation, owner epoch and model, resolved through the NPC manager's full pool-reference validation. Player/vehicle bindings carry separate birth/ownership identities and capture native full references. Reused slots cannot inherit old grants. Monotonic object IDs use the existing object registry; its ownership framework is unchanged. Every damage/apply boundary rechecks lifetime and actual ownership. Stop requests resolve the retained target against its current lease before authorization; a cached old driver grant cannot authorize a stop after transfer.

Managed guest script replicas have no local SCM handle: their scripted allocation reservation is explicitly released on teardown/reset/promotion. Pre-ready local guest fires are reconciled when canonical role becomes ready, freeing the pool before replay. New NPC API prerequisite is committed `be6c204c6b61da9b1977fd56374af2a505843b20`; root owns its integration and combined enum/version `0.5.0-alpha`.

## Verified native evidence

Supported executable SHA256: `a559aa772fd136379155efa71f00c47aad34bbfeae6196b0fe1047d0645cbd26`. Source semantics are in adjacent gta-reversed `source/game_sa/Fire.cpp`, `FireManager.cpp`, `CreepingFire.cpp`, `ShotInfo.cpp`, `WaterCannon.cpp`, `Tasks/TaskTypes/TaskComplexOnFire.cpp` and `TaskSimplePlayerOnFire.cpp`.

| Native boundary | Address | Verified relocated prefix |
| --- | --- | --- |
| Ground StartFire | `0x539F00` | `51 55 8B 6C 24 10` |
| Attached StartFire | `0x53A050` | `56 57 8B 7C 24 0C` |
| StartScriptFire | `0x53A270` | `83 EC 10 53 56` |
| Extinguish | `0x5393F0` | `56 8B F1 8A 06` |
| Water extinguishing | `0x5394C0` | `83 EC 4C 53 55` |
| ComputeFireDamage | `0x6333D0` | `64 A1 00 00 00 00` |

These instruction-complete prefixes contain no relative operands. All prefixes and the `0x53AF37 -> 0x53A570` ProcessFire CALL (`E8 34 F6 FF FF`) are checked before installing any hooks. A mismatch/allocation failure leaves the feature disabled. Ground/attached/script creation retain their raw native thiscall ABI and original return semantics; attached StartFire is not assumed to return the created fire.

The native scanner at `0x607E30` checks `CPed::m_pFire` at `0x607F5A`, constructs `CEventOnFire` with vtable `0x86CCC0`, and calls `CEventGroup::Add` at `0x607FD2`. Owner attachment invokes this scanner once. `EventOnFire::AffectsPed` (`0x4B1050`) prevents duplicate on-fire tasks; `ComputeOnFireResponse` (`0x4BAD50`) selects player/NPC tasks. Suppressed damage initializes the verified 12-byte `CPedDamageResponse` and marks it calculated without death. Native manager/SDK fire layout is `0x28` bytes per fire, `0x964` bytes per manager.

## Scope and limits

Supported sources are actual native pool fires: script fires, Molotov/explosion/flamethrower/creeping fires entering the host pool or accepted owner requests, attached burns, water effects and expiry/cleanup. Unnetworked scenery is a positional visual fallback on guests; its independent native burn/destruction state is not reconstructed. Standalone vehicle engine-damage particle systems and riot smoke outside `CFireManager` are not claimed. This is not campaign persistence or whole-world late-join reconstruction. Native ABI installation, appearance, timing, crime/death behavior and mission progression remain human runtime tests.

## Verification and human checklist

```powershell
python tests/fire/run_tests.py --output .cache/fire-headless/my-new-run
```

The output must be new. Actual codecs/streams and extracted production services/native damage boundary compile with MSVC C++17 `/W4 /WX`. The suite covers malformed/nonfinite input, every truncated payload, duplicate/stale/tombstone messages, epoch/channel order, owner-only damage, target reuse, repeated native references across script restart, 125 script-replica lifecycles and pre-ready pool reconciliation. Disabling the actual owner/lifetime damage gate must produce six failures. Recorded native doubles do not execute game addresses. `validation.json` records tested source hashes and results.

Isolated x86 client/server release builds use a private `.cache` source/output overlay with `GTA_SA_DIR` cleared. NPC prerequisites are read-only copied headers with recorded hashes. Absolute precompiled-header paths and two reserved NPC enum entries are compile-only overlay changes; no overlay is shipped. Root must build the merged source after resolving enum/config and shared connection/host/pose/binding hooks.

1. Host and guest ignite a small ground fire with normal controls. Confirm one canonical fire, matching spread/removal and no duplicate guest-created flames.
2. Burn a host-owned NPC, then a guest-owned NPC. Only the actual owner should reduce health; other views should show the burn without doubling damage. Repeat vehicle driver transfer while burning.
3. Use an extinguisher and fire-truck water from each owner. Confirm the host reduces strength/removes fire and replicas follow. Check fireproof/swimming/death behavior.
4. Join or finish loading while fires already exist. Confirm deferred entity effects resolve to the correct model/generation and do not attach to a reused slot.
5. Repeat script-fire creation/removal beyond 60 instances. Confirm effects/pool slots remain available and normal host SCM cleanup/progression is preserved.
6. Disconnect/reconnect and migrate host while burning. Old epochs/queued requests must not revive old burns. Start/load a campaign as host, then restart a guest with unchanged apparent spawn: host world resets only for the controlling-host load, while guest attachment birth advances.

No gameplay validation was performed by this worker.
