INSURGENCY ADVANCED

Reusable gameplay layer for Insurgency Reforged.
Project GUID: 78926100260C48E5
Dependencies: Insurgency Reforged (E55A9FA78E5FCFE4) and base game
(58D0FB3206B6F859). No WCS, FIA or scenario dependency.

Contents
- Unified grid territory, capture, site influence and counterattacks.
- Shared AI budget, adaptive activation and configured faction-pool spawning.
- Persistent woodland camp layouts, cache/intel integration and objectives.
- Zone/debug map resources and GM gameplay attributes.
- Reusable opt-in game-mode prefab and offline regression tests.
- Player Field Manual: Zones, Camps, Caches, Acquiring intel and Pastafarian.
- Capture, medical recovery and one-shot prisoner questioning.

Player guidance
In game, open Field Manual from the pause menu. Find the Insurgency Advanced
heading and select its single Insurgency Advanced submenu, like Conflict.
All five document tiles appear together: Zones, Camps, Caches, Acquiring intel
and Pastafarian. Pastafarian guidance prohibits killing captured prisoners.
Documents use native section headers, short paragraphs and explicit spacing.
Everon's welcome cards are short signposts; detailed guidance stays here in
the Field Manual rather than being duplicated on the welcome screen.
The menu moves Insurgency Advanced to the top before building its sidebar.
The native menu then selects its first subcategory automatically, showing the
five document tiles when the manual opens. The manual does not pop up on
mission start; stock sections, third-party sections and article links remain.
Ordering is applied by INF_FieldManualUI.c because inherited config-array
declaration order alone did not put the added category first in the UI.
The native menu requires section > subcategory > document; placing entries
directly in a top-level section produces an unresponsive section button.
The single source for concise player guidance is:
  Configs\FieldManual\FieldManualConfigRoot.conf
Refer to these pages when explaining the mission. Keep activation rules,
travel-efficiency settings, spawn budgets, probabilities and GM-only debug
details out of player-facing text. Technical maintenance notes remain here
and in MARKERS.txt. Everon's welcome screen points players to the manual.
The manual extends the stock shared Field Manual, so its section is visible
whenever Advanced is loaded; stock categories are retained.
Offline Field Manual tests check shared-resource registration, category/document
navigation and native menu integration, not prose, page ordering or paragraph
layout. Actual rendering must be checked in game.

Prisoner implementation / maintenance
Capture Prisoner is available on unconscious hostile AI, never players, friendlies
or dead targets. The interacting player must be conscious, on foot and nearby.
Captured AI are removed from their old group, switched to CIV if configured
(otherwise explicitly unaffiliated), and have AI, movement and weapon controls
disabled. This uses native restraint controls, not a custom surrender animation.
Capture drops carried guns, launchers and sidearms through the native inventory
manager, including weapons stored in carried inventory. Grenades, explosives,
ammunition, medical supplies and other non-weapon equipment stay on the body.
Busy inventory operations retry; the prisoner stays restrained throughout.
Once conscious and disarmed, a prisoner on foot is requested to stand and use
the native standing loiter animation. One server-side roll selects smoking
instead for 20% of prisoners, using the native cigar prop/loiter playback.
Pose selection is not rerolled by retries. With ACE Captives loaded, the 80%
loiter branch instead requests ACE_Captives_SetSurrender; the 20% cigar-smoking
branch remains native loiter. Without ACE Captives, standing loiter remains
the fallback. ACE Core alone does not enable surrender. Runtime type detection
and checked script calls avoid a required ACE dependency or copied ACE assets.
ACE owns surrender-helper replication and termination on incapacitation/death;
native loiter replication handles smoking and the non-ACE fallback.
Failed ACE calls log warnings; unsuccessful starts retry every five seconds
without releasing restraints. Verify with and without ACE Captives on a listen
host, dedicated server and late join, including recovery after incapacitation.
Incapacitation/death stops loiter; treatment is still required before standing.
No movement/weapon controls or AI are re-enabled for a pose. Verify standing,
smoking props, dropped loot and late joins in actual multiplayer gameplay.
Players must treat wounds normally; capture does not heal or revive.
Question Prisoner requires a captured, conscious AI with no active bleeding.
Players must stop bleeding before questioning; physical searching remains
available while a captured prisoner is bleeding. An otherwise eligible bleeding
prisoner keeps Question Prisoner visible but disabled, with a prompt to bandage
them before questioning. The one attempt is shared
by all players; it is spent before rolling, including refusals. Physical intel
searching is a separate, once-per-enemy team attempt using the original
replicated body-search tracker. Captured enemies can be searched unconscious,
conscious or dead. Searching and questioning work in either order; neither
consumes the other. Death does not reset either attempt.
Success awards one normal intel item. The default chance is twice the effective
body-search chance at mission start, capped at 100%; the GM can set Prisoner
Questioning Intel Chance (%) from 0 to 100. Later body-chance changes do not
silently overwrite a GM's questioning setting. Workbench uses -1 for the
automatic initial default, or an explicit probability from 0 to 1.
Dialogue currently has twelve refusal lines and four helpful lines. The
questioner's reliable owner RPC shows their response; successful intel follows
the existing shared map/hint system. No reward is consumed before intel startup.
Captured prisoners' native damage callbacks distinguish direct fatal shooting
or explosion damage from bleed-outs; pre-capture wounds and bleeding deaths
do not trigger execution accountability. Each death is processed once on the
server. Any resulting rebuke names the responsible player and uses the existing
reliable owner notification path for every connected player. Visual effects
inherit native replication; direct penalty damage affects only the offender,
not bystanders. Verify fatal shooting, explosions, bleed-outs, duplicate damage
callbacks and notifications on a listen host and dedicated server.
Prisoner flags are replicated for current clients and JIP, not saved to disk.
This is runtime capture, not a persistent prisoner roster or extraction task.
Capturing records the original managed spawn source before detaching the AI.
Questioning never deletes a prisoner immediately. Questioned prisoners and
unquestioned prisoners abandoned by players become eligible for normal cleanup:
grid infantry uses the patrol deactivation radius and continuous 60-second delay;
counterattacks use their 800m abandonment radius and configured abandonment delay;
both also protect the original zone centre. Field threats use their configured
abandonment range, conservatively checking all players. Zone vehicle prisoners
use the vehicle deactivation radius and concealment checks; ambient vehicle
prisoners retain their deactivation radius and remaining player-occupancy grace.
Returning players reset continuous abandonment timers. Unknown/unmanaged spawn
sources are preserved with a warning, not assigned an arbitrary cleanup radius.
Interactions attach directly to the faction soldier bases, matching upstream
Insurgency's body-search setup, not through a shared Character_Base override.
Advanced extends Character_USSR_Base with the original body-search action and
tracker plus the prisoner component, capture and questioning actions. The
optional Insurgency Advanced FIA addon does the same on Character_FIA_base,
preserving Insurgency FIA's search component/action IDs and one-second search.
All actions use the existing LowerTorso context. Non-grid missions keep the
Advanced capture/question actions unavailable; original body search remains.
Other factions need equivalent integration on their original soldier bases.
Reload the project/world after changing these prefab attachments; existing
spawned characters do not gain new components/actions from disk edits.
Validate in Workbench and on a dedicated server: capture an unconscious enemy,
heal them, verify restraint after recovery, question once, try another player,
try a pre-searched target and a dead prisoner, and join late after capture.
Also confirm a prisoner does not block zone capture or vanish with old squad
cleanup while nearby, then test abandoned cleanup before and after questioning.
Check both USSR and FIA soldiers, including a late-joining client.

Scenario integration
1. Add 78926100260C48E5 to the scenario addon's Dependencies.
2. Inherit the scenario's game-mode prefab from:
   {CC46DF266B89439D}Prefabs/MP/Modes/InsurgencyAdvanced/GameMode_InsurgencyAdvanced.et
3. Place that scenario prefab in the terrain's world. Keep the terrain,
   navmeshes, spawns, authored cache positions, base compositions, mission
   header, branding and scenario-specific settings in the scenario addon.
4. Override manager attributes in the scenario prefab when tuning that map.
   Do not copy faction pools; optional content addons supply inherited pools.

Loading Advanced does not itself enable the grid. The prefab enables it via
m_bINFBuildingGrid, along with cache objectives and GM zone rendering.
Non-grid missions delegate garrison/cache/intel lifecycle to upstream.
Editor attribute configuration is an inherited shared resource; Advanced-only
controls are unavailable when the grid is off.

Migration from INF Insurgency Everon
Scripts retain their INF class names. Moved map/marker/document resources
retain their paths and GUIDs. Everon's world, mission and scenario-prefab GUIDs
are retained except for Everon's scenario game-mode prefab. That prefab now
uses its own path/GUID (6AA19CB200000500) rather than overriding upstream's
shared Editor/GameMode_Editor_Full.et (DA3986AF1B5032B8). The world references
the unique scenario prefab directly, which inherits Advanced. Advanced's
parent is the upstream Insurgency prefab DA3986AF1B5032B8, not the stock
editor prefab DA3986AF1B5032B7. This keeps the scenario out of its own
upstream inheritance chain and preserves native manager settings/pools.
Existing INS_Saves filenames and data formats are unchanged. Do not delete
saves for this extraction. Use separate server profiles for different maps:
the inherited saves are not automatically namespaced per scenario.

Publishing/deployment
Open the new addon in Workbench to index its resources and compile it, then
open the Everon project with Advanced loaded as its dependency. Workbench owns
resourceDatabase.rdb generation; do not copy the Everon database into Advanced.
Publish Advanced first, then republish Everon with this dependency. Verify
the published project GUID matches the dependency before deployment.
Restart the server with both updated packages. Do not deploy the thin Everon
package without Advanced, and do not combine the old script-bearing Everon
package with Advanced: duplicate modded classes can change behaviour.
No publication, engine launch or server restart is performed by this migration.

Progressive clean startup
Players receive local startup hints only while the mission is initialising,
with an update every 30 seconds after spawning. Once ready, hints stop without
a completion popup; players joining an already-ready mission see no startup hint. The
server publishes the current status every five seconds only when it changes;
status and readiness are replicated so late joiners see current progress.
Dedicated servers do not display local UI. Stages report actual terrain/camp
work counts, placement, cache preparation and intel recovery, not a misleading
overall percentage. The initialising camp task also shows the current status.
Missing intel managers and invalid camp setup are explicitly reported as
blocked. Other unready placements remain visibly in their preparation stage.
Startup callbacks are removed at readiness or mission end.
The server scans an uncached world in queued 400m tiles, one tile per 50ms
callback, prioritising tiles nearest controlled players (or the game mode
position before players spawn). This is cooperative main-thread work, not a
worker thread or a guaranteed frame-time bound. Each fully scanned tile
publishes building zones for normal activation, capture and replicated maps.
Tile boundaries align with the 100m grid; half-open origin ownership prevents
large entities from being counted in several tiles. Already published
building counts, defender reservations and captured state are retained.
Only a completed scan writes the reusable raw terrain index. Camp candidate
search, final site selection and layout placement then proceed on their
existing queues. Camps/caches, final objective totals, counterattacks and
victory remain gated on site readiness; partial discovery cannot win a
mission. Reused raw indexes publish building zones without a new world scan.
Stopping the mission cancels outstanding scan callbacks without saving a
partial index. No mission-save format or terrain-index version change.

Runtime scheduling / efficiency
The server owns one shared player/presence sampler per grid mission. Normal
player samples last at most two seconds; local capture checks request one-second
samples. Managed hostile groups are deduplicated and sampled only on demand,
with per-group results and 100m spatial buckets shared during that sample.
Spawns/removals and prisoner captures invalidate presence; capture completion,
spawn safety and destructive cleanup refresh decision inputs before committing.
All controlled-player positions remain separate from living capture-eligible
players so vehicle/prisoner cleanup retains its original protection rules.
Capture checks query player-near cells plus running holds, not all territory.
Unresolved cells remain eligible after AI deactivation; permanent cells leave
the work set. Checks run at one second near potential captures and two seconds
otherwise, sleeping without connected players and waking on connection/activity.
Activation queries use nearby grid cells. Active infantry maintenance runs no
faster than five seconds (existing slower settings remain), and uses elapsed
abandonment durations. Capacity decisions always recount current reservations.
Budget admission skips eviction discovery entirely while capacity is available.
Preliminary defender checks share sampled group positions; destructive cleanup
still checks living defenders directly before committing. Activation and eviction
ordering calculate distances once per list and use stable O(n log n) sorting.
Player activation policy/grouping is reused within the current player sample;
world changes invalidate it and mutable allocation counts are recounted separately.
These optimizations target 32-player servers, with 128 players as a stretch target.
Zone vehicle maintenance/eligibility runs every five seconds only while players
or tracked vehicles require it. Distant vehicle cleanup candidates and prisoner
maintenance cleanup checks use ten seconds, with fresh safety/occupancy checks.
These intervals can delay activation/cleanup detection by one evaluation interval;
capture holds, wave monitoring, attack retry limits and cooldowns are unchanged.
Pending attacks use demand-driven five-second retries and lazy target records;
new targets inherit regional cooldowns from actual previous launches.
Objectives update through coalesced readiness/credit/cache/setting events.
There is no periodic full objective recount after readiness. Missing task-system
dependencies retry at five seconds; JIP receives existing shared tasks.
Territory markers are created once and only dirty state keys are updated.
Distant markers stay visible without polling; yellow cancellation and orange/
green transitions come from the authoritative state owner. Initial creation is
sliced into at most 64 markers per callback; failed creations retry at five
seconds. No marker loop independently scans players, hostiles or attack zones.
Permanent territory saves are immediate, idempotent transition writes, not an
all-zone capture-tick sweep. Failed dirty writes retry after 5/10/20/30 seconds,
then every 30 seconds until recovered; clean saves have no retry timer.
Capture finalization owns saving/notifications once per transition; delayed camp
ownership success commits separately. Cache and capture tasks share setup logic.
GM debug content is revisioned and owner snapshots are sent only when changed.
Only subscribed controllers receive two-second permission checks; local
visibility/initialization checks run only while needed, never on a dedicated
server. Delayed local editor/marker initialization retries without new snapshots.
Static camp ownership is applied on the authority without a permanent recursive
server timer; clients retain reconciliation for late/relevant composition
children. Native map rendering, AI scheduling and upstream callqueue pumps are
not disabled. Startup terrain discovery remains a bounded initialization task.
Periodic population-log scans and routine per-spawn/per-save chatter are removed.
Actionable errors/warnings, failed-save throttling/recovery and useful startup/
mission-end diagnostics remain. Shutdown cancels retries and subscriptions.
Offline efficiency tests check operation counts and lifecycle/source contracts;
actual timing and multiplayer relevance still require engine profiling.

Validation
From this addon:
  python -B -m unittest discover -s .\Tests -p "validate_*.py"
The Everon addon has separate scenario/dependency integration tests.
Project-wide structural tests check balanced script delimiters and reject
methods accidentally nested inside other methods. Vehicle lifecycle contracts
also check the native wreck override parameter names required by Enforce.
Offline tests do not replace Workbench compilation or dedicated-server tests.
For command-line Workbench compilation, use -gproj with this addon's project
and -addonsDir with comma-separated base-game and installed Workshop addon
directories. Launch from the game installation directory so relative engine
paths resolve. Missing search paths can report the base game or Insurgency
Reforged as unavailable even when installed. Workbench compiles scripts during
project startup; no scenario or play session is required for this check.
Verify static camps/caches and grid startup, FIA defenders, capture and
counterattacks, GM maps/attributes and objectives on Everon. Check a non-grid
Insurgency scenario retains authored garrisons and normal cache/intel startup.
Verify a second terrain with a thin inheriting scenario before claiming full
multi-map runtime support.

Replication / authority audit
Server-owned: discovery, camp/cache spawning and verification, zone/counterattack
state, travel timers, AI budget, file I/O, intel rolls, objective totals and
victory. These are not duplicated as client simulation. Native task and map
marker systems distribute public objectives/hints; zone marker entities carry
replicated presentation state and are not spatially hidden from late joiners.
Replicated custom state: GM tunable settings, intel recovery readiness, camp
arsenal identity/availability/friendly faction, and prisoner capture/question flags. Private
debug snapshots and prisoner replies use reliable player-controller owner RPCs.
Map widgets, debug marker handles and local permission cleanup stay local.
Local debug-marker cleanup removes off-screen markers from the native disabled
list before normal removal, preventing disabled markers from returning on pan
or map reopening. This applies to both camp and cache debug layers.

Shared spatial helpers / offline contracts
INF_BuildingGrid owns cell indices, XZ centres and keys; callers add terrain
height where needed. Zone creation, woodland discovery, site influence and map cells use these
helpers so negative coordinates and cell boundaries follow the same rules.
INF_BlockRules.IsConcealed owns world-only visibility traces with the existing
0.98 threshold and 1.5m player eye height. Infantry/counterattacks trace from
1.5m; vehicles retain their 3m height. Distance and clearance rules stay with
the owning spawn system. Offline tests share Tests\source_contracts.py for
brace-aware method extraction; these checks do not execute Enforce scripts.

Reusable terrain indexes / mission separation
Server-only derived data lives in $profile:INF_TerrainIndexes, separately from
$profile:INS_Saves. It contains raw building/settlement positions, woodland
cell tree counts and up to 96 validated potential camp layouts, not active
camps, ownership, selected caches, intel, search consumption or objective progress.
New missions shuffle that larger pool and select up to 24 sites, rechecking
current exclusions, footprint and navigation. Existing mission manifests take
priority on recovery and keep the actual selected layouts unchanged.
Zone building counts use indexed building positions, not another world scan.
Clean mission-end cleanup removes mission sidecars but never terrain indexes.
Index identity includes world resource, bounds, format version and Terrain
Index Revision. Camp pools also include composition, footprint radius and
safe-zone positions/radii; changing those does not invalidate the raw map scan.
Increase the patrol manager's Terrain Index Revision after terrain/content
changes under the same world resource. Content changes are not auto-detected.
Missing, stale or incomplete derived indexes are logged and rebuilt without
resetting mission saves. The first candidate-pool build has an eight-minute
search budget; warm selection has a two-minute safety/navigation budget.
Neither is a measured startup guarantee, and native navmesh streaming remains.

Camp arsenals and permanent friendly camps
Intel is bound to the composition's existing arsenal.
Fresh missions randomly select 7 camps by default, configurable from 1 to 20
only through the mission header's m_Insurgency.m_iINFCampCount. The Everon
scenario header explicitly defaults it to 7. It is read before discovery and
cannot be changed live through GM. Invalid values log an error and use 7.
In a dedicated server's game.missionHeader configuration, add:
  "missionHeader": {
    "m_Insurgency": {
      "m_iINFCampCount": 7
    }
  }
Use any integer from 1 to 20. Restart into a fresh mission for a new selection.
Recovered missions preserve their saved camp/cache roster, even if a new count
is configured; a smaller number never silently removes saved cache objectives.
Fresh selections prefer woodland interiors before falling back to forest edges.
An interior has at least 8 trees in its 100m cell and in at least 6 of its 8
neighbouring cells. This is a woodland-coverage heuristic, not a guarantee of
trees immediately beside the composition. Discovery prioritises these cells;
selection shuffles within the interior and edge tiers. All existing clearance,
spacing and navigation checks still apply, so fewer camps may qualify.
The placement-policy change rebuilds candidate layouts, retaining raw terrain
indexes and saved mission rosters. Use a fresh mission to see a new selection.
Layout and reserved cache/intel clearance tolerate trees and bushes clipping
the camp; rocks, buildings, terrain, water, slope and navigation checks remain.
Vegetation can overlap the reward position, so verify access in game.
The new placement-policy context rebuilds stale
camp candidates without discarding the reusable raw terrain index.
Non-cache camps retain one shared persisted search attempt and the existing
80% intel chance; cache camps do not gain an additional arsenal reward.
The shared ArsenalBox_Base override adds an otherwise unavailable interaction;
ordinary unbound arsenals retain their native actions and faction.
Both native USSR and FIA camp compositions inherit it. Custom compositions
must inherit BuildableComposition_Base (including its RplComponent), and supply
an inheriting arsenal and a native SCR_SpawnPoint. The added ownership component
is inactive on ordinary compositions that are not bound as secured camps.
Subtle purple means "Defenders approaching": initial inbound defenders, not a
counterattack. These approaching defenders receive a running-speed group setting
after initialisation; defenders spawned inside the square retain their defaults.
Orange attacks, permanent green and yellow capture holds take
priority over purple. Arrival within 200m, defeat or a five-minute wait after
an inbound group first has living members ends its inbound-only capture block;
timed-out groups remain alive and managed hostiles within 200m still block capture.
Eliminated, previously observed inbound groups resolve immediately; only groups
without observed living members wait for the materialisation grace period.
Capture maintenance tracks outstanding defender observations and inbound arrivals,
not every active zone. Arrival checks sample only their groups; global hostile
sampling is requested by nearby capture checks, which reuse the arrival result.
Three complete initial activation attempts without any living defenders unlock
the normal grounded, eligible on-foot capture hold (15 seconds within a circular
100m horizontal radius of the zone centre, no managed hostiles within 200m).
Activation priority for eligible on-foot players and eviction protection share
that 100m circular range. Eviction protection includes living players in vehicles,
but capture/priority still require eligible grounded on-foot players. The marked
100m square, defender placement and enemy-retake occupancy remain unchanged.
Group placeholders are not success;
actual living evidence resets failures. Each failure retries after 30 seconds,
except the third, which stops repeat activation. Materialisation is allowed
until 30 seconds after spawn readiness. Capacity/policy rejections, stale
callbacks and ordinary deactivation do not count as failed activations.
Zones within capture range of eligible on-foot players are selected first, with
distance breaking ties. Local activation limits and the 128-AI global budget
remain unchanged: occupied zones, active holds and AI within 200m of any player
are protected from budget eviction. If every possible victim is protected,
activation waits rather than exceeding limits or deleting nearby defenders.
Yellow means capture in progress. Capture completion makes one counterattack
chance roll (default 10%) for every grid zone, including camps. A failed roll
permanently secures the square (green); a successful roll commits an attack
(orange, including while waiting to spawn). Disabled counterattacks secure it
immediately. Pending attacks make up to three attempts, five seconds apart,
while players within 300m of the square centre permit spawning. Absent players,
regional cooldown, capacity and spawn-safety failures all count as attempts.
After three failures, nearby players secure it permanently; otherwise it returns
to hostile red and can populate and be captured again. The five-second polling
queue can add up to five seconds before the first attempt. The legacy attack
interval control is hidden in grid mode. No player notifications are sent.
Grid counterattack infantry receive a scenario-priority RUN movement setting
after squad initialisation, overriding walking defaults and waypoint speed
settings. Normal patrols and non-grid counterattacks are unchanged; GM movement
settings retain their higher priority. Verify the approach speed in game.
Camp spawn points stay enabled for their composition's faction while hostile,
including during an orange/pending counterattack. Players of that faction can
deploy there subject to native deployment restrictions. Permanently securing
the camp transfers spawning to the configured main player faction.
Counterattack defeat requires the complete materialized attacker roster to be
killed or captured. Unconscious attackers still count until secured or dead.
The director retains participant records across group removal, holds group
lifecycle in Manual mode, and records deaths/captures before cleaning up groups.
Abandonment, missing/despawned attackers and incomplete spawning do not award
permanence; aborted waves release the attempt for retry and log that distinction.
Permanence after a failed chance roll, exhausted attempts with nearby players,
defending or recapturing transfers the camp to the patrol manager's configured mission
player faction (m_sINFPlayerFaction, default US), never the captor's faction.
The camp objective completes only after ownership and spawn access transfer
successfully; failed transfers log an error and retry without granting credit.
Normal building-zone capture credit is unchanged. Native spawn faction/enabled
state, arsenal ownership and the composition root's ownership replicate for
current clients and late joiners. Clients apply the root's faction to all camp
affiliations, including the spawn-providing object. A five-second, owner-scoped
reconciliation covers late/relevant children without waiting for a stale total
of faction-bearing objects; unchanged affiliations are not written again.
Arsenals independently retry delayed faction initialization and cancel retries
when deleted. Ownership conversion is server-only and idempotent.
Native resource/access/spawn restrictions still apply.
Permanently secured grid zones and camp ownership are saved server-side in
$profile:INS_Saves/territory_0.dat and territory_1.dat. Alternating versioned,
world-checked snapshots retain the previous complete save if a write fails.
Permanent transitions save immediately; failed writes log errors and retry.
On restart, secured cells cannot activate hostile defenders. Saved camps regain
the configured player faction, arsenal access and deployment before final
startup readiness and capture objective totals. Ownership failures log and retry.
Unfinished captures and pending/active counterattacks restart hostile, without
restoring temporary credit, AI or timers. Markers, cache/intel saves, settings
and prisoners are unchanged. Territory never belongs in a reusable terrain index.
If one snapshot is invalid, recovery logs an error and uses the other valid
generation; if neither existing snapshot is valid, startup blocks and preserves
the files for diagnosis. Back up both files before investigating a failure.
Old versions did not save territory: already-lost captures cannot be recovered.
For a fresh campaign, stop the server and remove both territory snapshots along
with the existing campaign saves. Do not remove INF_TerrainIndexes. Removing
only both territory snapshots explicitly resets territory, not caches/markers.
Keep separate server profiles for different maps/campaigns; these are not slots.
Validate both permanent transitions, a different-faction captor, native USSR
and FIA arsenals, late joining, and deployment at a secured camp in-game.

Zone vehicle defenders
Occasional AI-crewed road-position defenders reuse the ambient manager's
m_aVehiclePrefabs and native default driver/turret crew spawning. Ambient route
vehicles retain their original routing, cap, enable switch and distance/grace
rules; their cleanup now shares the prisoner-safe vehicle cleanup path.
Both zone and ambient vehicles add a random 0-N passengers after native crew
spawning finishes. N includes only free, accessible cargo seats with a configured
default occupant. Selected seats use their own default character prefab and join
the crew's existing AI group; empty/defaultless seats never get faction fallbacks.
The count is chosen once per vehicle from the native crew-completion callback.
Native crew and successful passengers are recorded immediately, so early
dismounts, deaths and captures cannot turn a valid vehicle into a polling timeout.
Passenger spawning is server-only and uses native character/compartment replication.
Zone vehicles have their own two replicated GM/Workbench controls:
  Zone Vehicles: Chance (%)       default 15, range 0-100
  Zone Vehicles: Maximum Active   default 2, range 0-32
Vehicle numbers, not occupant AI, limit these systems. The zone crew-AI budget
has been removed. Crew and passengers do not consume the 128-AI infantry/
counterattack budget. Pending zone vehicles count against their vehicle cap;
failed initialization releases that vehicle slot. Zone and ambient caps remain
separate. Lowered limits affect future admissions rather than deleting existing
vehicles in front of players.
Only active hostile zones are eligible, once per zone per mission, with at
most one vehicle. Placement requires a nearby clear, dry, flat road position,
ambient activation/minimum player distances and concealment from all players.
The crew holds that road position with the native vehicle move order rather
than receiving infantry Defend orders that could dismount it. This is not
transport, reinforcement delivery or a replacement for ambient road patrols.
All crew groups participate in capture checks; secured prisoners do not.
Initialization has a 15-second guarded window and explicit failure logging.
Cleanup preserves players and nearby secured prisoners, including seated
prisoners. Abandoned prisoners follow their recorded cleanup rules rather than
permanently blocking vehicle/wreck cleanup; blocked wreck checks retry.
New zone admissions pause during joining without depending on ambient routes
or their private queue. Mission end cancels all custom lifecycle callbacks.
Runtime vehicles/crews are not written into terrain indexes or mission saves.
Zone vehicles prefer road placement, then try up to 32 open-ground positions
inside the zone with up to four headings per position. Open-ground placement
does not require a road; it retains player distance, concealment, exclusions,
vehicle separation, footprint and forward/reverse movement clearance. Ambient
patrols remain road-based.
Zone vehicles use infantry minimum player distances: 200m normally or 100m
in dense zones (four or more buildings), not the ambient vehicle minimum.
If zone roads and open ground fail, a bounded reinforcement search tries
outside road sites using infantry-style distance bands, at most 900m from the
zone centre, while retaining activation range and all vehicle clearance checks.
It requires a clear destination inside the zone, then uses the native vehicle
drive waypoint to send the crew there. This shares the existing zone vehicle
chance and cap; it is not an additional counterattack or ambient patrol.
Road connectivity and successful AI arrival still require live validation.
Zone vehicle placement rejects terrain-search displacement over 0.25m from its
road point, rather than accepting nearby courtyard positions. A conservative
10m-wide footprint checks terrain support and crossed obstacle traces at four
heights before spawning. Blocked locations are skipped; these checks run only
during placement, not in a recurring physics monitor.
Both inherited ambient vehicles and zone vehicles check footprint and a clear
15m forward or reverse corridor before vehicle creation, sampled across a 4m
width at three heights, plus corridors extending 10m from the centre in both
forward and reverse directions (5m beyond the reserved half-length).
Unsafe ambient positions (within 5m) are remembered for
the mission. Instead of dropping the patrol, a bounded search samples roads
around the original endpoint and accepts a clear site within 200m, preserving
player-distance, concealment, vehicle separation and original destination checks.
If no eligible site is found, that endpoint's search pauses for 60 seconds before
trying again; it does not spawn/delete vehicles while searching. Native route
selection and vehicle limits remain unchanged.
A second check before native crew creation excludes the vehicle and its children
from traces to catch settling into an obstruction. Obstructed deployments are removed through
their owning cleanup path; player-occupied vehicles are not removed.
This is conservative manoeuvring clearance, not a guarantee of AI pathfinding.
Temporary vehicle placement diagnostics and rejection counters are removed;
resource, spawn and crew initialization errors remain reported. Chance changes
do not reroll previous zones. Clearance reuses each successful footprint check
across open-ground headings and does not retrace short corridors when extending
the escape check to 15m.
Test driver/gunner operation and engagement, BRDM/APC crew variants, independent
ambient patrols, vehicle-cap saturation, zero/full passenger loads, defaultless
seats, joining, capture, theft, prisoner protection,
distance cleanup and stopping/restarting the mission in Workbench/multiplayer.

Cache victory now requires the complete selected roster, matching registry
arrays and every per-cache destroyed flag; an aggregate counter alone cannot
end the mission. Duplicate destruction callbacks are ignored. Destructions
during startup are held until recovery completes. Retry registration order is
aligned with the saved position roster before saved intel is applied.
Already-credited cache entities are removed on recovery, rather than leaving
apparently live caches that no longer contribute to the destruction objective.
Intel saves are staged/validated before applying; the aggregate is rebuilt
from per-cache flags. Corrupt, truncated, mismatched or orphaned cache/intel
saves block startup/victory and are preserved for diagnosis, never silently
replaced with fresh random objectives. A legitimately smaller recovered roster
is preserved. Logs report configured/selected/registered/destroyed counts,
duplicate callbacks, corrected saved counts and whether our objective director
authorised a mission end. Only the validated director queues cache victory.
In combined objective modes, destroying all caches no longer announces full
mission completion while capture objectives remain.

Runtime acceptance: dedicated server plus two clients and a late joiner.
Check the shared cache/task totals and existing intel hints on all clients;
destroy cache one and two of a three-or-more-cache roster, including repeated
damage/explosions on the same cache. Victory must wait for every unique cache
and the selected capture requirements. Verify one final end event, restart
with saved progress, partial spawn retries, independent GM debug toggles and
prisoner/document single-use state. Collect server logs and INS_Saves files
from an early-end incident before resetting anything. Offline source/model
checks are not Enfusion compilation or multiplayer verification.
