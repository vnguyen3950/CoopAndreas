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

The NPC/pickup lanes are implementing a reliable owner-sealed death, one native
death task on each replica, and one authenticated stock firearm drop manifest
from the original NPC death producer. Observer cops should use base native
physics/tasks instead of choosing pursuit behavior from the observer's local
wanted state. Their independent source/build handoffs remain pending.

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
