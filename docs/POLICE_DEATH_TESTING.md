# Police death, loot and wanted recovery

This follow-up addresses the reported 0.6 test failures: corpses and cop weapon
drops appearing only on their native owner, and shared wanted stars returning
after hospital/arrest recovery. Development protocol is 0.7; the installed 0.6
batch remains separate until a matched corrected package is prepared.

The wanted fix publishes a zero-only shared clear from the verified native
resurrection boundary. It preserves hospital/arrest fees and holds one deferred
clear if the transaction queue is full. Guest natural decay still cannot change
the room's wanted level. Death or arrest recovery by either player clears shared
stars; a later crime can raise them again.

The source now applies a reliable owner-sealed death and one native death task
on each replica through the shared NPC handlers. This includes civilians and
gang members: explicit male/female and Ballas/Families regressions cover both
death-seal/SYNC orders, delayed alive packets and original-owner behavior.
Observer cops use base native physics/tasks instead of choosing pursuit behavior
from the observer's local wanted state. This does not reproduce full AI or task
history.

Loot publication currently admits one authenticated stock firearm manifest from
the original cop death producer. The host and other guests create replicas of
that canonical item. The normal server tick releases a queued manifest when its
death proof arrives. Other NPCs' weapon/money publication is the next separate
increment after this path is validated; shared corpse synchronization already
applies to them. Supported drops remain exterior and outside active missions.

The combined client, server, proxy and launcher compile. The actual ENet server
fixture passes 66 checks, including guest-origin loot, delayed death proof,
late-join corpse/item replay and resurrection wanted clear. Native-function
tests use recorded collaborators; gameplay and in-process hook execution still
need the two-client checks below. Build 008 remains unchanged during preparation.

When the matched follow-up is ready, use two clients and test each player as host
and guest:

1. Kill a city cop on foot. Compare death, body position and corpse lifetime on
   both clients, then collect its pistol. Repeat for biker, sheriff and available
   SWAT/FBI/army models and with the other player doing the killing.
2. Confirm the stock eligible firearm/ammo appears once on both clients and its
   real collector receives one native benefit. Nightsticks and mission-only
   persistent pickups are outside the current ordinary slice. Stock cop money
   is not created by native CreateDeadPedMoney; identify any nearby money source.
3. Join after the death, reconnect, and provoke ownership transfer around it.
   Check that corpses/loot do not revive, disappear only on an observer, or mint
   another weapon drop. Test native deletion and pool/slot reuse separately.
4. Get wanted stars, die and respawn. Repeat while busted. Check both players'
   stars clear and fees/weapons follow the ordinary native recovery once.
5. Commit a new crime after recovery and check wanted can increase again. Repeat
   with delayed networking and a guest performing the recovery.

Headless tests and disk/native source inspection do not prove gameplay or hook
execution. Record the host/guest role, cop model/city, who shot, who owned the NPC,
and whether the issue followed death, join, transfer, respawn or collection.
