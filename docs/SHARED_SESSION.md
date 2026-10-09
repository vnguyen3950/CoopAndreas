# Shared wallet, wanted level and cheats

This development replaces the balance/star/cheat mirroring proposed in [upstream PR #106](https://github.com/Tornamic/CoopAndreas/pull/106) with a server-owned session ledger. Clients and server must use protocol **0.3.3-alpha** together. Native multiplayer behavior still requires the human checks below.

## Session policy

The host's initialized game state seeds the room. A joining guest does not contribute starting cash. Earn/spend changes become signed transactions, with connection identities and sequence receipts to prevent retrying a transaction from applying it twice. Shared mission rewards must be counted once rather than added independently on every client's replay of `ADD_SCORE`.

Host migration preserves the room's wallet, wanted policy and cheat toggles. Disconnecting and reconnecting creates a new connection identity; a reused player slot cannot reuse the old occupant's receipts. The room resets after its final participant leaves.

Shopping remains native and optimistic: each player can spend the balance they currently see. Concurrent purchases can therefore overspend. Both debits remain in the signed ledger. The game receives a nonnegative spending budget and HUD balance, so its ordinary clamp to zero cannot forgive room debt; subsequent earnings repay that debt. Cash observation continues through death/arrest for the same initialized player, retaining native fees. This feature does not authorize purchases before native goods are delivered or roll those goods back.

Any participant can raise the shared wanted level. A guest's death or ordinary native reset must not clear the room's pursuit. Host mission scripts and host decay can lower it; bribes, resprays and supported wanted cheats have explicit operations. Never-wanted takes precedence over a later script assignment.

Cheat actions and toggles have different semantics. An accepted action executes once per connected client; historical actions are not replayed to late joiners. Toggle snapshots express the desired state, rather than asking clients to toggle again on every update. The health/armor/cash cheat adds **$250,000 once to the room**; local health/armor effects must not feed its cash effect back into the ledger.

The current shared subset is **34 entries**: nine actions, two native function toggles and twenty-three flag toggles. The other fifty-eight entries are outside shared coverage. In particular, this does not establish shared vehicle-spawn, weather, themed-world or player-body cheats. Native cheat-table behavior was checked against the compatible game's source references; source review does not establish runtime hook behavior.

## Human checks

Use a separate test installation, matching clients/server and a fresh New Game. Keep normal saves separate, and close all lab programs before replacing files. Test ordinary mission gameplay without cheats first, then use disposable sessions for cheat checks.

| Check | Expected behavior |
| --- | --- |
| Connect in the menu, then start New Game | Host cash is seeded from initialized gameplay, not menu zero; guest starting cash is not added. |
| Complete a paying mission with two players | Both balances converge to one reward, not one reward per participant. |
| Both players earn or spend in quick succession | Both changes remain after receipts arrive. |
| Buy simultaneously when the wallet cannot cover both | Both debits remain; later earnings repay any debt. Goods are not rolled back. |
| Guest joins after cash changes | The guest adopts the room balance without changing it. |
| Die or get arrested with a positive balance | The native fee is retained once, without treating resurrection as a fresh wallet. |
| Die while the room has debt, then earn money | Native clamp to zero does not erase the debt; earnings repay it before becoming spendable. |
| Disconnect the host while a transaction is pending | The remaining host preserves the wallet and applies the pending transaction once. |
| Raise stars as a guest, then die or restart locally | Guest reset does not erase room wanted state. |
| Finish a mission which changes wanted limits or clears pursuit | Host script changes converge without repeated native echo. |
| Collect a bribe or complete a respray | The attributed operation lowers pursuit once. Report which player and location were used. |
| Enter HESOYAM once on either player | The room gains $250,000 once, with health/armor effects on the connected players. |
| Turn AEZAKMI on, then trigger a scripted wanted assignment | Shared wanted remains zero while the toggle is active. |
| Turn a supported toggle on/off, then join another client | Desired state converges; joining does not invert it or replay previous actions. |
| Restart/load or recreate the local player | Native initialization does not appear as a new reward or expense. |

Record the initiating player, balances/stars before and after, exact actions, connection changes, build manifest and logs. A successful ledger or serializer test is separate from these native game checks.
