# NPCs, fires, maps and gang wars

This development batch targets matching 0.5.0-alpha clients and server. Integration is in progress; the approved updater still selects the earlier 0.4.0-alpha package until combined validation finishes. Gameplay testing belongs to the user. Gamepad support is skipped.

## NPC state and lifetime

The existing NPC network now distinguishes actor generations, ownership epochs and increasing state sequences. Late joiners receive retained on-foot, driver or passenger state. Actors and state wait in a bounded queue when the guest is in menus, models are unavailable, native pools are full or a vehicle is missing. Delayed removal and creation confirmations cannot affect a newer occupant of the same slot. Native AI stays with its owner; this does not transfer arbitrary task history or reproduce a mid-war AI chain.

Human checks: join while in menus and then start the game; compare moving, shooting and driving NPCs; leave/rejoin and walk between streaming areas. Check NPC health, armour, weapon/ammo, interiors, car flags and dirt. Test passengers moving between cars/seats, remote ownership changes, deleted actors and rapid replacement. See [NPC contract and checks](../tests/npc_world_sync/README.md).

## Shared fire

The fire service uses the game's bounded CFire pool. The host controls creation, spread, expiry and removal. Guests display canonical fires and apply permitted effects only to targets they actually own. Guest ignition and extinguishing requests require authenticated identities and valid nearby state. Actor attachments wait for matching lifetimes and native readiness. Host migration clears the previous fire epoch; controlling-host game loads establish a fresh fire scene.

Human checks: create ordinary ground fires with molotovs/flamethrowers; ignite players, NPCs and cars; compare both clients. Use an extinguisher and a fire truck as either participant. Check swimming, fireproof targets, death, expiry, script cleanup and repeated script-fire creation/removal. Connect late and from menus, reload a game, change host and reuse actor/vehicle slots. There must be one fire effect and one owner damage path. Standalone engine particles, smoke and unregistered scenery burn state are outside the CFire replication contract.

## Maps

Both players' positions and waypoints appear on radar/pause map. The host's initialized loaded game seeds shared discovery; each outdoor participant contributes the cell it explores. Guest personal saves do not merge their whole map into the room. Discovery survives host migration, with first-time promoted menu guests adopting it before publishing. A later deliberate controlling-host load starts a new campaign. Territory colors/densities come from gang-war state.

Human checks: compare different saves, explore as a guest, place/move/clear markers, join late and reconnect into the same slot. Check map pins on foot/in cars and across aspect ratios. Exercise host departure while a guest is still loading, then an established host's later load with concurrent guest exploration. See [map contract and checks](MAP_SYNC.md).

## Gang wars and territory

The host runs native wave progression and rewards. Guests share territory density/color, receive wave guidance and can damage the mapped host actors through the existing bullet path. Native wave actors stay pinned to that host. Leaving the host cancels the active fight and retains territory, without awarding a victory or defeat. It does not reconstruct an active AI chain on the promoted guest.

Human checks: offensive and defensive wars, guest bullet kills, ordinary failure/escape and territory changes. Join with different saves or while loading; compare colors, stage guidance and actor placement. Disconnect the host during a fight and retry under the promoted host. Guest kills may contribute to native provocation using the host's location/on-foot context; wave AI remains host anchored. Melee, projectile and explosion credit need separate gameplay checks. See [gang-war contract and checklist](../tests/gang_war_notes.txt).

## Reporting results

For a failure, record build version, which participant was host, whether anyone joined late/reconnected/loaded a save, the exact steps and observed difference. Keep the client's crash/log output and server log. Test a fresh game first when using the modified mission scripts; preserve normal saves. The server does not provide persistent campaign/player storage. Source tests and x86 builds do not establish native gameplay reliability.
