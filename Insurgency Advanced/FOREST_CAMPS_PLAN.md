# Automatic Forest Camps

Status: Implemented 2026-10-17. Offline validation complete; in-game acceptance pending.
Saved: 2026-10-16.

## Agreed direction

- Add small forest camps as places to discover, complementing the existing
  field-threat patrols rather than adding another roaming patrol system.
- Find locations automatically. No manually placed encounter locations.
- Mark each selected camp site red like other hostile zones, including while
  dormant. Do not require discovery before showing its zone.
- Reuse the current zone palette: faint red dormant, strong red active,
  yellow while capture progresses, and faint green once captured.
- Share the existing 128-AI grid/counterattack budget rather than introducing
  another uncapped AI population.
- Make validated camps potential cache locations. Camps not selected for a
  cache must have a searchable item with a high likelihood of providing intel.
- Implementation was authorised 2026-10-17; the design below reflects the
  subsequent unified-zone decisions.

## Current implementation design

- Use a small set of reusable camp layouts, with automatic location and rotation.
  Authoring a layout is acceptable; manually positioning camps around Everon is not.
- Use a base four-/six-person group, scaled by current non-stacking site
  influence (default 2x), and a modest set of persistent camp props, including
  space for either a cache or the non-cache camp's searchable intel item.
- Give each camp a normal 100m zone marker at its selected grid location.
  Do not mark entire forests or create duplicate markers at a shared coordinate.
- Find suitable camp sites first, independently of cache selection. A camp
  exists whether or not it is selected to contain a cache.
- Retain selected sites for the session so activation does not move camps around.
  Layouts and consumed intel searches recover alongside saved cache positions.
  Camp captures now follow the unified grid's capture/one-shot counterattack
  lifecycle, not an ordinary defender respawn timer.

## Cache candidates and non-cache intel

- Register validated camp sites as additional candidates for the existing cache
  system. Keep its configured cache count; do not spawn a cache at every camp
  or silently increase the number of objectives.
- Coordinate candidate registration with cache selection and saved-cache
  recovery. Camp discovery must finish in time for selection, or the cache
  manager must explicitly support later registration. Do not assume that
  registering sites after its initial selection will work.
- Restore a saved cache together with its corresponding camp layout. Preserve
  objective identity and destruction progress; resolve site-generation/save
  ordering explicitly during implementation.
- At a camp without a cache, provide a searchable map case, documents or similar
  prop with a high chance of awarding intel through the existing intel system.
  Starting balance: 80% chance of one intel reward per non-cache camp.
- Resolve the chance once per site and record the result/search state so walking
  away, capacity eviction and reactivation cannot reroll or repeatedly award it.
  Any layout or cache-assignment persistence must preserve that state as needed.
- Camp/cache influence can strengthen future infantry allocations but does not
  stack. Camp capture removes its influence; a live cache can continue providing
  its own influence. Capture rules and normal zone colours stay consistent.
  Red marks a hostile zone, not a revealed cache. Use intel for actual cache hints.
- Capturing a camp does not automatically destroy a cache or complete its
  objective. Distance cleanup and capacity eviction must not delete a live cache,
  count it as destroyed, or regenerate an already destroyed cache.

## Automatic site search

Implemented starting balance: up to 24 sites; at least eight tree entities in
a 100m cell; sites at least 450m apart, 150m from qualifying building origins
and 300m from settlement descriptors, outside safe zones. Searches
try nine local offsets in interleaved sorted woodland cells, up to eight per 100ms,
with a 120-second discovery budget and a 20-second per-candidate nav streaming
timeout. Recovered sessions restore their manifest rather than rediscovering.
Each camp spawns one complete replicated USSR Encampment composition
(`PlayerHub_S_USSR_01.et`), not separately spawned tent props, and a base
four-person infantry team managed by the zone system.
The unified grid normally doubles that allocation through the non-stacking
camp/cache influence multiplier. Camp and cache zones use the same defender and
one-shot random counterattack director as building zones; they do not own
separate defender lifecycles. Camp capture removes its future influence.
The separate local (0,0,8) slot is reserved for a cache or searchable documents.
The game mode's Ambient Patrol Manager exposes the composition resource picker
and footprint half-width under Forest Camps. These are mission-start settings,
not live swaps of persistent camps. Default footprint radius is 9m: terrain and
clearance are sampled over a 7x7 grid, including boundaries, with at most 3m
between samples, 12 degrees slope and 0.65m height variation. Custom compositions
must have a correctly sized footprint (3-24m). Invalid configuration holds
startup with an explicit error. Recovered sites use the configured composition
and must pass its footprint; a larger layout is never forced into an old site.
Native player spawn points inside the composition are disabled. Compositions
containing AI groups are rejected; ordinary defenders retain the shared budget.
All descendants are excluded from owned-site obstruction traces and removed
from automatic persistence tracking; manifests reconstruct the composition.
The reward slot also requires clearance.
Clearance uses four diagonal traces with endpoints lifted 5cm above the local
terrain at each end, retaining entity/world/ocean collisions. This prevents
the clearance check itself intersecting uphill ground. Endpoint terrain/water
and slope checks still apply. Discovery logs spatial, terrain, clearance and
navigation rejection counts. An empty saved camp manifest permits a fresh
search on restart; nonempty and corrupt manifests retain recovery protections,
and saved cache positions are never reshuffled by this retry.
Soldiers navmesh must be loaded/valid and produce a reachable point 8-40m away.
This validates a local navigable approach, not a guaranteed route to every road
or player. Physics/navmesh behaviour must be checked in-game.
Static camps and caches are placed during startup, before managed defender
activation. Compositions/documents persist for the mission and are not gated by player
distance or visibility. Selected caches retain their own objective lifecycle.
Infantry uses the shared grid spawn/reinforcement checks (sparse zones retain
the 200m separation floor). Static compositions spawn before cache selection; non-cache
documents finalise after cache/intel recovery. Grid zones are built only after
these physical sites are ready. Discovery validates the actual infantry tile
as well as its connectivity probe; AI activation does not reroll that probe.

1. Generate scattered candidates outside settlements, safe zones and occupied
   building-grid locations. Old garrison footprints have no effect.
   Space accepted camps apart.
2. Score woodland surroundings and clearings. Verify how Everon's vegetation
   appears in runtime entity queries before relying on tree counts.
3. Reject water, including inland water, and unsuitable slopes.
4. Validate the complete layout footprint: centre, corners, each prop position
   and infantry spawn positions. Reject excessive height variation, intersecting
   trees/rocks, floating props and obstructed entrances.
5. Validate infantry navigation around the site and a usable approach. Being
   near or on navmesh alone is not proof of connectivity. Defer validation when
   required navigation tiles are not loaded; do not assume success.
6. Cache suitable candidates, but recheck physical clearance, player separation
   and visibility immediately before activation.
7. Process searches in bounded batches on the server. If a complete camp cannot
   fit safely, reject or defer that candidate rather than force a spawn.

## Verified engine facilities to investigate during implementation

- `SCR_WorldTools.FindEmptyTerrainPosition` and
  `FindAllEmptyTerrainPositions`: candidate clearance searches. These do not
  replace complete camp-footprint checks.
- `SCR_TerrainHelper.GetTerrainNormal` and terrain alignment helpers:
  slope checks and prop placement.
- `SCR_WorldTools.GetWaterSurfaceY` and `ChimeraWorldUtils.TryGetWaterSurface`:
  water rejection beyond a simple sea-level test.
- `AIWorld` / `NavmeshWorldComponent`: navigation tile status/loading and
  `GetReachablePoint`. Validate actual connectivity requirements in-game.
- `AIPathfindingComponent.GetClosestPositionOnNavmesh`: available on a
  pathfinding component; do not assume a component exists before AI is spawned.
- `RoadNetworkManager`: nearby road/track queries for suitable approaches.
- Entity queries and volume traces: local vegetation and layout clearance.

The existing field-patrol search is not a complete camp placement solution.
It has an exposed-land fallback and lacks full layout and navigation validation.
Do not copy that fallback into camp placement.

## Lifecycle integration

- Reserve camp infantry against the shared 128-AI budget before spawning.
- Include active camp sites in the active-zone accounting; the current
  32-block ceiling must not become an independent camp population allowance.
- Reuse nonlinear per-player-group zone quotas and fast-travel relevance checks,
  rather than creating a separate camp activation allowance.
- Prioritise nearby eligible sites and reuse farthest-first capacity eviction,
  retaining player-occupancy and nearby-soldier protections.
- Revalidate safety for every player, including players who move or teleport
  during queued activation. Camps must not appear beside or in view of players.
- Include camp infantry in hostile capture-contest checks.
- Reuse grounded/on-foot capture eligibility, the 15-second hold and the
  200m hostile contest radius. Capture alone must not delete survivors.
- Ordinary defender deactivation cleans up groups only. Static compositions/documents
  remain until mission end. Partial startup layouts roll back and retry with an
  explicit error, without deleting cache objectives. Cached manifests recreate
  layouts on recovery; engine tracking is stopped for ordinary camp props to
  prevent duplicate persistence ownership. Search consumption remains saved.
- Use existing shared marker replication, state precedence and cleanup.
  Orange should indicate a real counterattack only if camp counterattacks
  are explicitly implemented; that extension is not agreed scope yet.
- Keep one addon modded declaration per affected manager/state class.

## Validation before enabling

- Automatically find woodland sites without manual encounter placement.
- Check dense woods, clearings, slopes, shoreline, inland water and obstacles.
- Confirm every prop fits and infantry can move around and approach the camp.
- Confirm visibility and distance safety for multiple players and teleports.
- Confirm the shared AI budget, nearest-site admission and safe capacity eviction.
- Confirm all zone colours, capture interruption, shared-cell precedence and
  late-join replication.
- Confirm repeated activation/cleanup leaves no duplicate AI, markers or props.
- Verify camps can be selected as cache candidates without increasing the
  configured cache count. Test saved-cache recovery and already destroyed caches.
- Verify non-cache camps have the searchable high-chance intel item, with no
  duplicate rewards or probability rerolls after cleanup/reactivation.
- Verify cache presence/destruction does not alter camp zone colour or troop
  allocation, and ordinary camp cleanup never destroys a cache objective.
- Confirm camp search is paced and does not cause significant server spikes.
- Run offline checks first; do not launch the game or Workbench without permission.
