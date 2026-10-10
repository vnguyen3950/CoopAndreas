# Cop-origin pickup increment

This increment publishes one stock firearm item per authenticated sealed cop
death. It depends on the frozen NPC death API in police-sync-fix commit
9fa9ae173ff9cca410cede131905f2d97766cfae. Root owns the final matched 0.7 protocol,
PED_DEATH enum, shared lifecycle integration, combined builds and packaging.

The original native weapon-drop function at 0x4591D0 runs only for the validated
local cop owner while sharing is active. Its Generate callback captures actual
type, model, position and ammunition. It does not reconstruct drop quantity.
The A559 executable uses half the street table value, unlike the reversed
source's twice-table expression. Receive bounds are 15 pistol, 30 micro-Uzi,
30 MP5 and 40 M4 ammunition; smaller actual native amounts remain unchanged.

Ordinary ambient creation remains room-host-only. Cop-origin items carry NPC
slot, immutable sealed generation/owner epoch/death sequence, monotonic producer
sequence and original producer vitals incarnation. SYSTEM creation can arrive
before EVENT death proof. The server retains a bounded pending request, then
checks the original producer, exact seal, current actor life, retained cop model,
death area and finite position within five metres. No unproved request mints an
item. One accepted manifest spends the death allowance; ownership transfer,
duplicate requests and scene changes cannot grant another item from that seal.

The canonical item owner is its original producer, including a guest. Host and
other guests create native replicas; owner-slot reuse also compares incarnation.
Missing models are requested asynchronously before native replica generation.
Only the original producer's native cleanup removes its source item; replica
cleanup cannot act as producer cleanup. Fresh native-slot replacement retires
an older owned canonical mapping, while replacing a replica does not remove the
foreign canonical item. Pending proofs and unpublished source items expire
conservatively after 15 seconds. Removal/reuse of the source NPC rejects proof.

Supported stock models are city/sheriff/biker cop 280–284 with pistol 346, SWAT
285 with micro-Uzi 352, FBI 286 with MP5 353, and army 287 with M4 356. Nightstick
334, persistent mission pickup type 22, interiors and active missions remain
outside publication. Stock cops have no native death money in the verified
cop money path. Civilian/gang pickup publication is a later separate increment.

Original native merging still runs first and can skip Generate entirely.
Existing conservative metadata mismatch retirement is retained; this increment
does not claim full merged-pile accounting. Existing exact terminal receipts,
held-view declines, one native benefit and collector-local query flags remain.
Money is never credited again through a second wallet transaction.

Root must call CPickupServer::ProcessPending after normal receive dispatch.
Hello/Action also drain it. Final packet/version selection and batch 009 are
root-owned. Batch 008, game files and user processes were not changed here.

## Evidence and human checks

Actual client/native-hook functions run with recorded native/transport doubles;
actual server functions and real codecs are used. Original engine machine code
was read from disk only. The SDK checkpoint links the frozen NPC API in a unique
absolute worktree cache with GTA_SA_DIR unset. These are source/ABI compilation
checks, not native gameplay proof.

- Kill a confirmed stock cop owned by the host, then one owned by each guest.
  Verify both corpse propagation (NPC lane) and visible firearm loot on all peers.
- Collect from each peer; verify the actual collector receives ammunition once,
  other peers see removal, and repeated packets give no second benefit.
- Transfer ownership before and after death, disconnect/reconnect the producer,
  and reuse player/NPC slots. Check that no new owner creates a second drop.
- Exercise Create-before-PED_DEATH and the opposite order, late joins, delayed
  actor acknowledgement, missing models and exhausted native pickup pools.
- Verify melee/persistent/interior/mission drops remain excluded, and same-handle
  merges follow conservative retirement instead of full-pile republication.
