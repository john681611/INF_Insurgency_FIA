"""Operation-count models and wiring contracts, not engine profiling."""
from collections import defaultdict
from math import ceil, floor, log2
from pathlib import Path
import random
import unittest

from source_contracts import method_body

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "Scripts" / "Game"
CORE = (SCRIPTS / "INF_ZoneStateSystem.c").read_text()
GRID = (SCRIPTS / "INF_BuildingGrid.c").read_text()
PRESENCE = (SCRIPTS / "INF_BlockControl.c").read_text()
MAP = (SCRIPTS / "INF_GarrisonMapAreas.c").read_text()
INTEL = (SCRIPTS / "INF_CacheObjective.c").read_text()
STORE = (SCRIPTS / "INF_TerritoryPersistence.c").read_text()
VEHICLES = (SCRIPTS / "INF_ZoneVehicles.c").read_text()
CONTROLLER = (SCRIPTS / "INF_GMDebugMarkers.c").read_text()
PRISONERS = (SCRIPTS / "INF_Prisoners.c").read_text()


def key(position):
    return tuple(floor(axis / 100) for axis in position)


def nearby(cells, players, radius):
    visited = 0
    found = set()
    for x, z in players:
        for ix in range(floor((x - radius) / 100), floor((x + radius) / 100) + 1):
            for iz in range(floor((z - radius) / 100), floor((z + radius) / 100) + 1):
                visited += 1
                position = cells.get((ix, iz))
                if position and (position[0] - x) ** 2 + (position[1] - z) ** 2 <= radius ** 2:
                    found.add((ix, iz))
    return found, visited


class Samples:
    def __init__(self):
        self.player_at = self.enemy_at = -1
        self.players = self.enemies = 0

    def get(self, now, enemies=False, fresh=False, age=2000):
        if self.player_at < 0 or now - self.player_at >= age or (fresh and self.player_at != now):
            self.players += 1
            self.player_at = now
        if enemies and (self.enemy_at < 0 or now - self.enemy_at >= 1000
                        or (fresh and self.enemy_at != now)):
            self.enemies += 1
            self.enemy_at = now

    def invalidate(self):
        self.player_at = self.enemy_at = -1


class Changes:
    def __init__(self):
        self.dirty = set()
        self.objective_queued = False
        self.marker_updates = self.recounts = 0

    def notify(self, zone, objective=False):
        self.dirty.add(zone)
        self.objective_queued |= objective

    def drain(self):
        self.marker_updates += len(self.dirty)
        self.dirty.clear()
        self.recounts += self.objective_queued
        self.objective_queued = False


def merge_order(zones, players, descending=False):
    distances = [min((sum((a - b) ** 2 for a, b in zip(zone, player)) ** 0.5
                      for player in players), default=float("inf")) for zone in zones]
    count = len(zones)
    zones = list(zones)
    merged = [None] * count
    merged_distances = [None] * count
    comparisons = 0
    width = 1
    while width < count:
        for start in range(0, count, width * 2):
            middle = min(start + width, count)
            end = min(start + width * 2, count)
            left, right = start, middle
            for output in range(start, end):
                take_left = right >= end
                if left < middle and right < end:
                    comparisons += 1
                    take_left = (distances[left] >= distances[right] if descending
                                 else distances[left] <= distances[right])
                if left < middle and take_left:
                    merged[output], merged_distances[output] = zones[left], distances[left]
                    left += 1
                else:
                    merged[output], merged_distances[output] = zones[right], distances[right]
                    right += 1
        zones[:], distances[:] = merged, merged_distances
        width *= 2
    return zones, count * len(players), comparisons


class RuntimeEfficiency(unittest.TestCase):
    def test_cached_merge_order_matches_reference_including_ties_and_empty_inputs(self):
        rng = random.Random(32)
        for count in (0, 1, 2, 3, 31, 32, 127, 128, 257):
            zones = [(rng.randint(-5, 5), rng.randint(-5, 5)) for _ in range(count)]
            for players in ([], [(0, 0)], [(1, 2), (-3, 4)]):
                for descending in (False, True):
                    expected = sorted(zones, key=lambda zone: min(
                        (sum((a - b) ** 2 for a, b in zip(zone, player))
                         for player in players), default=float("inf")), reverse=descending)
                    actual, _, comparisons = merge_order(zones, players, descending)
                    self.assertEqual(actual, expected)
                    self.assertLessEqual(comparisons, count * ceil(log2(max(1, count))))

    def test_distance_work_is_linear_in_candidates_at_32_and_128_players(self):
        zones = [(i * 100, 50) for i in range(256)]
        for player_count in (32, 128):
            players = [(i * 200, 0) for i in range(player_count)]
            _, distances, comparisons = merge_order(zones, players)
            self.assertEqual(distances, len(zones) * player_count)
            self.assertLessEqual(comparisons, len(zones) * 8)
            self.assertGreater(len(zones) * (len(zones) - 1) * player_count, distances * 100)
        sort = method_body(PRESENCE, "static void SortZonesByDistance(")
        self.assertEqual(sort.count("Nearest("), 1)
        self.assertLess(sort.index("distances.Insert(Nearest("), sort.index("for (int width"))
        tick = method_body(GRID, "override protected void TickProximityCheck(")
        self.assertIn("SortActivationZones(candidates, presence)", tick)
        priority = method_body(PRESENCE, "static void SortActivationZones(")
        self.assertIn("SortZonesByDistance(zones, presence.m_Players)", priority)
        self.assertIn("CapturerOccupied(state.m_vCenter, presence)", priority)
        self.assertNotIn("while (!candidates.IsEmpty())", tick)
        self.assertIn("SortZonesByDistance(possible, presence.m_Players, true)",
                      method_body(GRID, "protected void INF_TrimActivationZones("))

    def test_budget_fast_path_is_exact_inverse_of_eviction_pressure(self):
        for total in (127, 128, 129):
            for active in (31, 32, 33):
                for local_active in (3, 4, 5):
                    for adding in (False, True):
                        fits = total <= 128 and (not adding or active < 32) and (
                            not adding or local_active < 4)
                        pressure = total > 128 or (adding and active >= 32) or (
                            adding and local_active >= 4)
                        self.assertEqual(fits, not pressure)
        budget = method_body(GRID, "bool INF_MakeRoomForBudget(")
        fast = budget.index("if (reserved + required <= INF_BuildingGrid.ACTIVE_BUDGET")
        scan = budget.index("array<INS_AmbientPatrolZoneState> possible")
        self.assertIn("&& (!addingBlock || active < INF_BlockRules.MAX_BLOCKS)", budget[fast:scan])
        self.assertIn("&& (!addingZone || localActive < localLimit)", budget[fast:scan])
        self.assertIn("return true;", budget[fast:scan])
        self.assertLess(scan, budget.index("SoldiersDistant("))
        self.assertLess(scan, budget.index("GetPresence(false, true)"))
        self.assertIn("INF_ReservedGridAI()", budget[:fast])
        self.assertIn("INF_CountActivationZones(policy)", budget[:fast])
        self.assertIn("SortZonesByDistance(possible, presence.m_Players, true)", budget)

    def test_preliminary_checks_share_groups_but_destruction_checks_are_direct(self):
        soldiers = method_body(PRESENCE, "static bool SoldiersDistant(")
        self.assertIn("presence.GroupPositions(patrol.m_Group)", soldiers)
        self.assertIn("CollectLiving(patrol.m_Group, living)", soldiers)
        for method in ("protected void INF_TrimActivationZones(",
                       "bool INF_MakeRoomForBudget("):
            body = method_body(GRID, method)
            self.assertIn("INF_BlockRules.EVICTION_PROTECTION_RADIUS, presence)", body)
            fresh = body.index("GetPresence(false, true)")
            self.assertIn("INF_BlockRules.EVICTION_PROTECTION_RADIUS)", body[fresh:])
        tick = method_body(GRID, "override protected void TickProximityCheck(")
        self.assertIn("m_fDeactivationRadius, presence)", tick)
        self.assertIn("m_fDeactivationRadius)", tick[tick.index("GetPresence(false, true)"):])
        self.assertIn("ObserveDefenders(state, presence)", method_body(CORE, "void CheckCapture("))

    def test_policy_reuse_invalidates_with_samples_world_changes_and_live_settings(self):
        policy = method_body(PRESENCE, "INF_ActivationPolicy ActivationPolicy(")
        self.assertIn("if (!m_ActivationPolicy)", policy)
        self.assertEqual(policy.count(".Collect(owner, this)"), 1)
        invalid = method_body(PRESENCE, "void Invalidate(")
        self.assertIn("m_ActivationPolicy = null", invalid)
        self.assertIn("m_GroupPositions.Clear()", invalid)
        self.assertIn("Invalidate()", method_body(PRESENCE, "void CollectPlayers("))
        self.assertIn("m_Presence.Invalidate()", method_body(CORE, "void InvalidatePresence("))
        self.assertIn("InvalidatePresence()", method_body(GRID, "void INF_SetTravelSetting("))
        queued = method_body(GRID, "protected void INF_SpawnQueued(")
        self.assertIn("GetPresence().ActivationPolicy(GetOwner())", queued)
        self.assertNotIn("policy.Collect(", queued)
        count = method_body(GRID, "void INF_CountActivationZones(")
        self.assertLess(count.index("group.m_Active = 0"), count.index("policy.Count("))

    def test_capture_finalization_notifies_once_and_retry_preserves_credit_gate(self):
        complete = method_body(CORE, "protected void CompleteCapture(")
        self.assertLess(complete.index("m_OnCaptured.Invoke(state)"), complete.index("CommitCaptureState(state)"))
        commit = method_body(CORE, "void CommitCaptureState(")
        self.assertEqual(commit.count("SavePermanent(state)"), 1)
        self.assertEqual(commit.count("NotifyChanged(state, true)"), 1)
        callback = method_body(GRID, "protected void INF_OnCampCaptured(")
        self.assertNotIn("CommitCaptureState", callback)
        self.assertNotIn("NotifyChanged", callback)
        self.assertLess(callback.index("MakeFriendly"), callback.index("state.m_bINFObjectiveCaptured = true"))
        retry = method_body(GRID, "protected void INF_RetryCampCapture(")
        self.assertLess(retry.index("INF_OnCampCaptured(state)"), retry.index("CommitCaptureState(state)"))
        self.assertIn("state.m_bINFPermanent && state.m_bINFObjectiveCaptured", retry)
        self.assertIn("state.m_INFCamp.m_Friendly", retry)
        self.assertIn("Remove(INF_RetryCampCapture)", method_body(GRID, "override void OnGameModeEnd("))

    def test_cache_task_uses_shared_setup_and_retains_initialising_title(self):
        cache = method_body(INTEL, "protected void INF_UpdateCacheObjective(")
        self.assertIn('"Caches - initialising"', cache)
        self.assertIn('INF_UpdateCaptureTask(m_INFCacheTask, "INF_DestroyHostileCaches", title, description)', cache)
        self.assertNotIn("CreateTask(", cache)
        self.assertEqual(INTEL.count("CreateTask("), 1)
        self.assertNotIn("INF_OnZoneCaptured(", INTEL)
        self.assertNotIn("INF_AssignCacheObjective(", INTEL)

    def test_idle_world_size_does_not_increase_marker_or_objective_work(self):
        for size in (1, 1000, 10000):
            changes = Changes()
            for zone in range(size):
                changes.notify(zone, objective=True)
            changes.drain()
            self.assertEqual(changes.recounts, 1)
            changes.marker_updates = changes.recounts = 0
            for _ in range(100):
                changes.drain()
            self.assertEqual((changes.marker_updates, changes.recounts), (0, 0))
            for _ in range(50):
                changes.notify(0, objective=True)
            changes.drain()
            self.assertEqual((changes.marker_updates, changes.recounts), (1, 1))

    def test_marker_work_is_dirty_only_and_has_no_presence_or_attack_scan(self):
        update = method_body(MAP, "protected void INF_UpdateZoneAreas(")
        self.assertIn("m_INFDirtyAreas.Count()", update)
        self.assertIn("processed < 64", update)
        self.assertNotIn("CollectMapZones", update)
        self.assertNotIn("INF_HasCounterAttackInArea", update)
        self.assertNotIn("INF_BlockPresence", MAP)
        self.assertIn("GetOnChanged().Insert(INF_QueueZoneArea)", MAP)
        self.assertIn("GetOnChanged().Remove(INF_QueueZoneArea)", MAP)
        self.assertNotIn("CallLater(INF_UpdateZoneAreas, 1000, true)", MAP)

    def test_five_visual_states_preserve_attack_priority_and_cancelled_holds(self):
        update = method_body(MAP, "protected void INF_UpdateZoneAreas(")
        labels = ["state.m_bINFAttackPending || state.m_bINFAttackActive",
                  "state.m_bINFPermanent", "state.m_fINFCaptureSince >= 0", "state.m_bActive"]
        offsets = [update.index(label) for label in labels]
        self.assertEqual(offsets, sorted(offsets))
        capture = method_body(CORE, "void CheckCapture(")
        self.assertIn("(since >= 0) != (previousSince >= 0)", capture)
        self.assertIn("NotifyChanged(state)", capture)

    def test_spatial_queries_match_full_scan_across_boundaries(self):
        rng = random.Random(217)
        cells = {(x, z): (x * 100 + 50, z * 100 + 50)
                 for x in range(-15, 16) for z in range(-15, 16)}
        for radius in (0, 75, 200, 500, 801):
            for player in [(0, 0), (-100, 100), (-0.01, 99.99)] + [
                    (rng.uniform(-1200, 1200), rng.uniform(-1200, 1200)) for _ in range(10)]:
                expected = {cell for cell, p in cells.items()
                            if sum((a - b) ** 2 for a, b in zip(p, player)) <= radius ** 2}
                actual, _ = nearby(cells, [player], radius)
                self.assertEqual(actual, expected)
        self.assertIn("INF_BuildingGrid.CellIndex(player[0] - radius)", CORE)
        self.assertIn("vector.DistanceSqXZ(player, state.m_vCenter) > radius * radius", CORE)

    def test_local_query_cost_is_independent_of_dormant_registry_size(self):
        counts = []
        for size in (10, 1000, 10000):
            cells = {(x, 0): (x * 100 + 50, 50) for x in range(size)}
            _, visited = nearby(cells, [(50, 50)], 200)
            counts.append(visited)
        self.assertEqual(counts, [25, 25, 25])
        capture = method_body(CORE, "protected void CaptureTick(")
        self.assertNotIn(": m_Zones", capture)
        self.assertNotIn("Copy(m_Candidates)", capture)
        self.assertIn("CollectNearby(presence.m_Capturers, 200, nearby)", capture)
        self.assertIn(": m_Holds", capture)  # Departing players must still cancel holds.

    def test_shared_sampler_deduplicates_consumers_and_skips_unused_hostiles(self):
        samples = Samples()
        for now in (0, 500, 1000, 1500):
            for _ in range(10):
                samples.get(now)
        self.assertEqual((samples.players, samples.enemies), (1, 0))
        for _ in range(10):
            samples.get(2000, enemies=True, age=1000)
        self.assertEqual((samples.players, samples.enemies), (2, 1))
        samples.get(3000, enemies=True, age=1000)
        self.assertEqual((samples.players, samples.enemies), (3, 2))
        collect = method_body(PRESENCE, "void CollectEnemies(")
        self.assertIn("seen.Contains(group)", collect)
        self.assertIn("GroupPositions(group)", collect)
        self.assertIn("m_GroupPositions.Clear()", collect)

    def test_invalidation_and_fresh_commit_inputs_are_not_hidden_by_cache_age(self):
        samples = Samples()
        samples.get(0, enemies=True)
        samples.invalidate()
        samples.get(0, enemies=True)
        self.assertEqual((samples.players, samples.enemies), (2, 2))
        samples.get(1, enemies=True, fresh=True)
        self.assertEqual((samples.players, samples.enemies), (3, 3))
        samples.get(1, enemies=True, fresh=True)
        self.assertEqual((samples.players, samples.enemies), (3, 3))
        capture = method_body(CORE, "void CheckCapture(")
        self.assertIn("INF_BlockRules.CAPTURE_HOLD_MS", capture)
        self.assertIn("GetPresence(true, true)", capture)
        self.assertIn("GetPresence(false, true).m_AllPlayers",
                      method_body(GRID, "override protected vector FindSafeSpawnPosition("))
        self.assertIn("INF_BlockPresence.PlayerPositions(true)", PRISONERS)

    def test_hostile_bucket_query_includes_exact_radius_and_neighbor_cells(self):
        points = [(-200, 0), (200, 0), (0, -200), (0, 200), (200.01, 0), (-100, -100)]
        buckets = defaultdict(list)
        for point in points:
            buckets[key(point)].append(point)
        inspected = [point for x in range(-2, 3) for z in range(-2, 3)
                     for point in buckets[(x, z)] if point[0] ** 2 + point[1] ** 2 <= 40000]
        self.assertEqual(set(inspected), set(points) - {(200.01, 0)})
        query = method_body(PRESENCE, "bool CanCapture(")
        self.assertIn("center[0] - 200", query)
        self.assertIn("x <= maximumX", query)
        self.assertIn("vector.DistanceSqXZ(center, enemy) <= 40000", query)

    def test_objective_events_coalesce_and_missing_dependencies_retry_only(self):
        queue = method_body(INTEL, "void INF_QueueCampaignObjectives(")
        self.assertIn("!Replication.IsServer()", queue)
        self.assertIn("m_bINFObjectiveUpdateQueued", queue)
        self.assertIn("CallLater(INF_UpdateCampaignObjectives, 0, false)", queue)
        self.assertNotIn("CallLater(INF_UpdateCampaignObjectives, 2000, true)", INTEL)
        self.assertIn("GetOnObjectivesChanged().Insert(intel.INF_QueueCampaignObjectives)", GRID)
        retry = method_body(INTEL, "protected void INF_RetryMissingTasks(")
        self.assertIn("!m_INFCacheTask || !m_INFCampTask || !m_INFZoneTask", retry)
        self.assertIn("5000, false", retry)

    def test_persistence_has_no_registry_poll_and_only_dirty_failures_retry(self):
        capture = method_body(CORE, "protected void CaptureTick(")
        self.assertNotIn("m_Persistence.Record", capture)
        self.assertNotIn("m_Persistence.Flush", capture)
        save = method_body(CORE, "void SavePermanent(")
        self.assertIn("if (m_Persistence.Record(state))", save)
        self.assertIn("FlushPermanent()", save)
        flush = method_body(CORE, "protected void FlushPermanent(")
        self.assertIn("m_Persistence.IsDirty() && !m_Stopped", flush)
        self.assertIn("Math.Min(m_SaveRetryMS * 2, 30000)", flush)
        retry = 5000
        observed = []
        for _ in range(5):
            observed.append(retry)
            retry = min(retry * 2, 30000)
        self.assertEqual(observed, [5000, 10000, 20000, 30000, 30000])
        self.assertIn("bool Record(", STORE)

    def test_every_authoritative_transition_notifies_consumers(self):
        for method in ("void CheckCapture(", "void AttackStarted(", "void ResolvePendingAttack(",
                       "void CheckRetake(", "void AttackEnded(", "void AddCamp("):
            self.assertIn("NotifyChanged(state", method_body(CORE, method), method)
        for method in ("override protected void ActivateZone(", "override protected void DeactivateZone(",
                       "void INF_ResetAbandonedZone(", "override protected void MarkZoneRespawnable("):
            self.assertIn("NotifyChanged(state", method_body(GRID, method), method)
        self.assertIn("CommitCaptureState(state)", method_body(GRID, "protected void INF_RetryCampCapture("))
        self.assertIn("NotifyChanged(state, true)", method_body(CORE, "void CommitCaptureState("))
        ready = method_body(GRID, "void INF_StaticCampLayoutsReady(")
        self.assertIn("intel.INF_QueueCampaignObjectives()", ready)
        self.assertIn("intel.INF_DebugContentChanged()", ready)

    def test_idle_timers_sleep_and_player_connections_wake_capture_and_vehicles(self):
        capture = method_body(CORE, "protected void CaptureTick(")
        self.assertIn("!presence.m_PlayerIDs.IsEmpty()", capture)
        self.assertIn("interval = 2000", capture)
        self.assertIn("interval = 1000", capture)
        self.assertIn("WakeCapture()", method_body(GRID, "override void OnPlayerConnected("))
        self.assertIn("INF_ScheduleZoneVehicles()", method_body(VEHICLES, "override void OnPlayerConnected("))
        self.assertNotIn("CallLater(INF_ZoneVehicleTick, 1000, true)", VEHICLES)

    def test_slow_maintenance_keeps_fresh_cleanup_inputs_and_elapsed_thresholds(self):
        tick = method_body(GRID, "override protected void TickProximityCheck(")
        self.assertIn("Math.Max(5000, m_fCheckInterval * 1000)", tick)
        self.assertIn("now - state.m_fINFDistantSince >= INF_BlockRules.DESPAWN_DELAY_MS", tick)
        self.assertIn("GetPresence(false, true)", tick)
        self.assertIn("CallLater(INF_ZoneVehicleTick, 5000, false)", VEHICLES)
        self.assertIn("unit.m_NextCleanupAt = now + 10000", VEHICLES)
        self.assertIn("m_fNextCleanupCheck = now + 10000", PRISONERS)

    def test_lazy_attack_targets_retain_regional_cooldowns_without_propagation(self):
        launches = [((0, 0), 10000), ((100, 0), -1)]
        for center, expected in (((300, 0), 70000), ((400, 0), 0)):
            cooldown = max([0] + [at + 60000 for pos, at in launches
                                  if at >= 0 and sum((a - b) ** 2 for a, b in zip(pos, center)) <= 90000])
            self.assertEqual(cooldown, expected)
        target = method_body(GRID, "protected INS_CounterAttackZone INF_GetGridTarget(")
        self.assertIn("previous.m_fINFLastLaunchAt >= 0", target)
        self.assertIn("previous.m_fINFLastLaunchAt + m_fZoneCooldown * 1000", target)
        self.assertIn("CallLater(CounterAttackTick, 5000, false)", GRID)
        self.assertNotIn("CallLater(CounterAttackTick, 5000, true)", GRID)

    def test_debug_checks_only_subscribers_and_skips_unchanged_snapshot_building(self):
        send = method_body(INTEL, "protected void INF_SendDebugMarkerSnapshot(")
        self.assertLess(send.index("INF_DebugSnapshotChanged"), send.index("m_aINFDebugCachePositions.Insert"))
        self.assertIn("m_iINFDebugPositionRevision != m_iINFDebugRevision", send)
        self.assertIn("CollectCampPositions(m_aINFDebugCampPositions)", send)
        refresh = method_body(INTEL, "protected void INF_RefreshDebugMarkers(")
        self.assertNotIn("GetPlayers(", refresh)
        self.assertIn("m_aINFDebugSubscribers", refresh)
        self.assertIn("m_aINFDebugSubscribers.Remove(i)", refresh)
        self.assertIn("m_iINFDebugSnapshotLayers == layers && m_iINFDebugSnapshotRevision == revision", CONTROLLER)
        local = method_body(INTEL, "protected void INF_OnDebugVisibilityChanged(")
        self.assertLess(local.index("System.IsConsoleApp()"), local.index("INF_CanSeeDebugMarkers()"))
        self.assertIn("m_bINFDebugApplyPending = m_iINFLocalDebugLayers != 0", local)
        self.assertIn("m_fINFNextDebugApplyAt", local)

    def test_lifecycle_removes_listeners_and_retry_queues(self):
        stop = method_body(CORE, "void Stop(")
        self.assertIn("m_Stopped = true", stop)
        self.assertIn("Remove(FlushPermanent)", stop)
        self.assertIn("Remove(CaptureTick)", stop)
        self.assertIn("GetOnChanged().Remove(INF_QueueZoneArea)", MAP)
        self.assertIn("m_INFDirtyAreas.Clear()", MAP)
        self.assertIn("m_aINFDebugSubscribers.Clear()", INTEL)
        self.assertNotIn("INF_LogPopulation", GRID)
        self.assertNotIn("[INF_Population]", GRID)


if __name__ == "__main__":
    unittest.main()
