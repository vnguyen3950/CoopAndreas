# Upstream pull request review

Reviewed the upstream inventory of 71 pull requests: 8 open, 40 merged and 23 closed without merging. Every merged PR's merge commit is already an ancestor of our starting branch, so none required duplicate integration. Reviews use the actual patches and existing engine/SDK code; PR titles are not treated as proof of implementation or testing.

## Selected changes

| Upstream PR | Selected behavior | Integration notes |
| --- | --- | --- |
| [#102](https://github.com/Tornamic/CoopAndreas/pull/102) | Network-ped lifetime validation for the reported 0x4D68BA crash path. | Preserve full pool references; reject recycled/deleted ped lifetimes before processing updates and native references. Follow-up deletion no longer requires a matrix to be allocated. |
| [#96](https://github.com/Tornamic/CoopAndreas/pull/96) | Door guards during native interior transitions. | Follow-up validates object/ped references, restores only the collision bit owned by a lease, releases guards on death/disconnection/area changes/distant transfers and clears state before render shutdown. |
| [#111](https://github.com/Tornamic/CoopAndreas/pull/111) | Vehicle engine, lights, engine-broken, siren/alarm and alarm timer state. | Player-driver and idle snapshots were imported; a local follow-up adds NPC-driver parity. Boolean initialization/serialization and receive authority checks are reviewed alongside existing ownership. A recorded player driver takes precedence over previous syncers and delayed NPC updates. NPC horn remains outside the follow-up. |
| [#110](https://github.com/Tornamic/CoopAndreas/pull/110) | Vehicle dirt level. | Finite, bounded serialization and compatibility with #111 are checked. |
| [#112](https://github.com/Tornamic/CoopAndreas/pull/112), limited extraction | Repair existing stat descriptors and setter notification ordering. | Keep eleven real descriptors and three reserved zero slots in the existing fourteen-float packet. Notify after the native setter has applied the new value. Per-player ownership and wire layout are preserved. The pickup/session-stat feature is deferred. |
| [#106](https://github.com/Tornamic/CoopAndreas/pull/106), corrected implementation | Shared room wallet, explicit wanted policy and 34 supported cheat entries. | Replace balance mirroring with deduplicated signed transactions and receipts. Preserve pending work through host migration; exclude guest starting cash and duplicate ADD_SCORE replay. Use desired toggle states and one-shot action receipts, suppress native cash/wanted feedback, and retain hospital/arrest fees. The other 58 cheats remain outside shared coverage; shopping is optimistic and can leave signed room debt. See [session policy](SHARED_SESSION.md). |

Original upstream authorship is preserved through cherry-picks for the four complete PRs. Local follow-up commits record compatibility and lifetime/validation repairs. The stat extraction credits #112 as its reference and is intentionally scoped independently from shared pickup/session policy.

The current combined gameplay packet changes require local protocol **0.5.0-alpha** on both clients and server. The existing version handshake rejects earlier builds. Mission scripts and the reviewed matched SCM/IMG pair retain their existing source/content; this review adds no mission completion claims.

## Deferred or superseded proposals

| PR | Decision and reason |
| --- | --- |
| [#112, full feature](https://github.com/Tornamic/CoopAndreas/pull/112) | Scripted pickup removal is reported as collection; deduplication consumes the native collection flag; coordinate/slot matching lacks lifetime IDs; spawn/collect relays do not establish authority; negative-coordinate keys collide; late-join history is limited to 128 collections with no active-spawn replay/reset. Collection state also propagates without necessarily granting the resources required by host mission checks. Session-wide stat maxima change existing skill ownership. A proper pickup design needs stable identities, single collection/reward accounting, mission semantics and lifecycle/reset handling. |
| [#92](https://github.com/Tornamic/CoopAndreas/pull/92) | Draft with a substantial proximity/shared-vehicle wanted policy and many native hooks. Mission-specific wanted support remains a TODO; the generic any-remote wanted predicate is not restricted to our captured mission roster. Defer to a separate mission-aware design and regression pass. |
| [#90](https://github.com/Tornamic/CoopAndreas/pull/90) | Linux build changes target an older xmake layout. Linux compilation is not validated in this Windows lab; the reported PR CI failed. Avoid claiming platform support from a build-file patch alone. |
| [#104](https://github.com/Tornamic/CoopAndreas/pull/104) | Closed weather/time/lightning overhaul includes a large native-state rework, hook bodies with commented installation sites, and unrelated formatting/resource changes. It needs a separately tested extraction; the current host weather/time sync remains present. |
| [#109](https://github.com/Tornamic/CoopAndreas/pull/109), [#108](https://github.com/Tornamic/CoopAndreas/pull/108) | Dirt is covered by the cleaner live #110. Head movement adds a separate native IK/network stream and is deferred. #108's current diff contains dirt/head movement, not the pause-state feature suggested by its title. |
| [#68](https://github.com/Tornamic/CoopAndreas/pull/68) | Excluded after native-byte inspection: 0x741185 is a short jump (EB 02), not the proposed call; the actual 0x741199 call is already hooked. |
| [#58](https://github.com/Tornamic/CoopAndreas/pull/58) | Closed radio-station proposal uses the former project_files/server packet layout and does not synchronize audio tracks. A current ownership-aware implementation would need a port and dedicated validation. |
| [#64](https://github.com/Tornamic/CoopAndreas/pull/64) | Old .NET/WPF launcher replacement does not fit the current native launcher. It also requests admin rights and kills all matching GTA processes. Our module-relative launcher fix preserves normal authentication and avoids those broad process actions. |
| [#88](https://github.com/Tornamic/CoopAndreas/pull/88) | Closed plugin-SDK migration targets a former dependency/layout, with unrelated source movement. The current repository already vendors the SDK used by the successful builds. |
| [#7](https://github.com/Tornamic/CoopAndreas/pull/7) | Old font/quote workaround is superseded by current nickname sanitization and font handling; it targets project_files/CDXFont.cpp. |
| [#14](https://github.com/Tornamic/CoopAndreas/pull/14), [#15](https://github.com/Tornamic/CoopAndreas/pull/15) | Old mouse/background-window patches target the former layout. The current optional windowed helper and earlier confirmed window behavior already address our testing workflow. |

The remaining closed proposals concern historical server/platform/channel/chat/config rewrites. They were classified against their obsolete layouts or superseding merged changes, rather than presented as missing current features. Their unmerged status does not mean their functionality is absent: the merged moon, Unicode/chat, config, EnEx and server work is already in our ancestry.

## Validation and human checks

All four release targets compile together. The stat extraction passes 63 assertions, the production vehicle-authority predicate passes 73, and the existing object contract passes 122,243 regression assertions. The protocol guard rejects earlier versions. These headless checks do not run the native game. The real serializer extension tests pass 9,728 round-trip cases, 37,696 truncated reads and 179,970 assertions, including all flag combinations, NaN/infinite dirt inputs and alarm 65,535. They cover the extracted extension sections, not the full native packet prefix or ENet timing.

The corrected #106 work passes 515 ledger/helper assertions, 9,580 assertions against the actual packet classes/serializer, and 110 assertions across nineteen fresh-process service scenarios. The service tests compile its actual functions with recorded native/transport doubles, including the guest reward-replay prefix and the reference punishment lambda. They cover initialization, receipt races, action/toggle deduplication, loading, migration, resurrection, hospital/arrest charges, debt repayment and concurrent bribes. Executable call sites for bribes, Pay 'n' Spray and resurrection were checked separately on disk. None of these checks executes the hooks in the game or proves native shopping serialization.

Human testing remains necessary:

- Two players compare lights, engine state, siren/alarm and dirt on the same vehicle, including ownership transfer, idle updates and reconnection.
- Repeat interior entry/exit while wanted; interrupt a doorway animation through death, disconnection or a mission transfer. Check that collision/control state and invisible guards clear.
- Mission doorway/NPC/bullet behavior needs testing because door guards use invisible collision blockers. Same-area transfers shorter than twenty metres remain an explicit cleanup uncertainty.
- Exercise ped deletion/recreation and mission retries to check the reported crash path.
- Change weapon skills and repeat stat setters; verify transmitted values remain per-player and reserved packet slots stay zero.

Source review and successful builds are preparation for these tests, not native gameplay validation.
