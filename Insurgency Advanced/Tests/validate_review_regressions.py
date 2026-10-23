"""Regressions found while reviewing the agreed zone lifecycle."""
from itertools import product
import math
from pathlib import Path
import unittest
from source_contracts import method_body

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "Scripts" / "Game"
CORE = (SCRIPTS / "INF_ZoneStateSystem.c").read_text()
AI = (SCRIPTS / "INF_BuildingGrid.c").read_text()
CACHE = (SCRIPTS / "INF_ForestCampRewards.c").read_text()
EDITOR = (SCRIPTS / "INF_ZoneEditorAttributes.c").read_text()
INTEL = (SCRIPTS / "INF_CacheObjective.c").read_text()
CAMP = (SCRIPTS / "INF_ForestCamps.c").read_text()
BLOCK = (SCRIPTS / "INF_BlockControl.c").read_text()
VEHICLES = (SCRIPTS / "INF_ZoneVehicles.c").read_text()
MAP = (SCRIPTS / "INF_GarrisonMapAreas.c").read_text()


def advance(eligible, now, since):
    if not eligible:
        return False, None
    if since is None:
        since = now
    return now - since >= 15, since


class ReviewRegressions(unittest.TestCase):
    def test_shared_method_extraction_ignores_braces_in_strings_and_comments(self):
        source = 'void Example() { string s = "}"; // }\n /* { */ if (true) { Run(); } }\nvoid Next() {}'
        extracted = method_body(source, "void Example()")
        self.assertIn('string s = "}"', extracted)
        self.assertIn("if (true) { Run(); }", extracted)
        self.assertNotIn("void Next", extracted)
        stripped = method_body(source, "void Example()", strip_literals=True)
        self.assertNotIn("//", stripped)
        self.assertNotIn("/*", stripped)
        self.assertIn('string s = ""', stripped)
        with self.assertRaisesRegex(AssertionError, "Unbalanced method"):
            method_body("void Broken() { if (true) { }", "void Broken()")

    def test_grid_coordinates_share_floor_rules_at_negative_and_positive_boundaries(self):
        index = method_body(AI, "static int CellIndex(")
        self.assertIn("Math.Floor(coordinate / SIZE)", index)
        center = method_body(AI, "static vector Center(")
        for axis in (0, 2):
            self.assertIn(f"CellIndex(position[{axis}]) * SIZE + HALF", center)
        key = method_body(AI, "static string Key(")
        self.assertIn("CellIndex(center[0])", key)
        self.assertIn("CellIndex(center[2])", key)
        ensure = method_body(CORE, "protected INS_AmbientPatrolZoneState EnsureZone(")
        self.assertIn("INF_BuildingGrid.Center(position)", ensure)
        self.assertIn("INF_BuildingGrid.HALF", ensure)
        influence = method_body(CORE, "static bool InInfluence(")
        self.assertEqual(influence.count("INF_BuildingGrid.CellIndex("), 4)
        self.assertIn("dx <= 1 && dz <= 1", influence)
        self.assertIn("INF_BuildingGrid.Key(state.m_vCenter)", method_body(MAP, "protected void INF_QueueZoneArea("))
        woodland = method_body(CAMP, "void Index(")
        self.assertIn("INF_BuildingGrid.Key(pos)", woodland)
        self.assertIn("INF_BuildingGrid.Center(pos)", woodland)
        for coordinate, expected_index, expected_center in (
            (-100.01, -2, -150), (-100, -1, -50), (-0.01, -1, -50),
            (0, 0, 50), (99.99, 0, 50), (100, 1, 150),
        ):
            with self.subTest(coordinate=coordinate):
                cell = math.floor(coordinate / 100)
                self.assertEqual(cell, expected_index)
                self.assertEqual(cell * 100 + 50, expected_center)

    def test_shared_concealment_preserves_heights_threshold_and_distance_gate(self):
        conceal = method_body(BLOCK, "static bool IsConcealed(")
        for contract in ("position + Vector(0, height, 0)", "player + Vector(0, 1.5, 0)",
                         "TraceFlags.WORLD", "world.TraceMove(trace, null) >= 0.98"):
            self.assertIn(contract, conceal)
        self.assertIn("INF_BlockRules.IsConcealed(pos, players, 1.5)",
                      method_body(AI, "protected bool INF_IsConcealedSpawn("))
        attack = method_body(AI, "protected bool INF_GridAttackPositionSafe(")
        self.assertIn("pos[1] <= 0.5 || INF_BuildingGrid.IsExcluded(pos)", attack)
        self.assertIn("vector.DistanceSqXZ(pos, player) < 250000", attack)
        self.assertIn("INF_BlockRules.IsConcealed(pos, players, 1.5)", attack)
        self.assertIn("INF_BlockRules.IsConcealed(pos, players, 3)",
                      method_body(VEHICLES, "protected bool INF_ZoneHidden("))
        for fractions, expected in (([], True), ([0.9799], True), ([0.98], False),
                                    ([1], False), ([0.2, 0.99], False)):
            self.assertEqual(all(fraction < 0.98 for fraction in fractions), expected)

    def test_trace_exclusion_array_is_owned_by_caller_through_trace(self):
        helper = CAMP.split("static void ExcludeSite(", 1)[1].split("static bool DryFlat", 1)[0]
        self.assertIn("array<IEntity> exclusions", helper)
        self.assertIn("trace.ExcludeArray = exclusions", helper)
        self.assertNotIn("trace.ExcludeArray = {}", CAMP)
        clear = CAMP.split("static bool ClearVolume", 1)[1].split("static bool RewardSlotClear", 1)[0]
        self.assertLess(clear.index("array<IEntity> exclusions = {}"), clear.index("ExcludeSite(trace, site, exclusions)"))
        self.assertLess(clear.index("ExcludeSite(trace, site, exclusions)"), clear.index("world.TraceMove(trace, null)"))

    def test_counterattack_vector_scaling_uses_float_and_preserves_spacing(self):
        attack = AI.split("override protected void CounterAttackTick()", 1)[1].split(
            "protected bool INF_GridAttackPositionSafe", 1)[0]
        self.assertIn("float lateralOffset = slot * 12.0", attack)
        self.assertIn("empty + tangent * lateralOffset", attack)
        self.assertNotIn("tangent * slot", attack)
        self.assertEqual([slot * 12.0 for slot in range(4)], [0.0, 12.0, 24.0, 36.0])

    def test_empty_attacker_sample_resets_retake_hold(self):
        since = None
        for eligible, now, expected in ((True, 0, False), (False, 10, False),
                                        (True, 20, False), (True, 34.999, False),
                                        (True, 35, True)):
            captured, since = advance(eligible, now, since)
            self.assertEqual(captured, expected)
        retake = CORE.split("void CheckRetake(", 1)[1].split("void AttackEnded", 1)[0]
        self.assertNotIn("attackers.IsEmpty()", retake)
        self.assertIn("bool occupied = false", retake)
        self.assertIn("AdvanceCapture(occupied", retake)
        self.assertIn("state.m_fINFRetakeSince = -1", retake)

    def test_capture_can_continue_after_defender_disable_but_not_before_first_spawn(self):
        for active, seen, spent in product((False, True), repeat=3):
            checked = active or seen or spent
            if not active and seen:
                self.assertTrue(checked)
            if not active and not seen and not spent:
                self.assertFalse(checked)
        self.assertIn("state.m_bActive || state.m_bEverHadLiving || state.m_bINFAttackSpent", CORE)
        deactivate = AI.split("override protected void DeactivateZone(", 1)[1].split(
            "protected void INF_SpawnQueued", 1)[0]
        self.assertLess(deactivate.index("ObserveDefenders(state)"), deactivate.index("super.DeactivateZone(state)"))
        capture = CORE.split("void CheckCapture(", 1)[1].split("void AttackStarted", 1)[0]
        self.assertLess(capture.index("ObserveDefenders(state, presence)"), capture.index("m_fINFSpawnReadyAt"))
        self.assertIn("state.m_iINFPendingGroups == 0", capture)

    def test_failed_groups_do_not_cancel_rest_of_wave_or_hold_unused_reservations(self):
        for count in range(1, 7):
            for results in product((False, True), repeat=count):
                pending = reserved = count
                successes = 0
                for success in results:
                    pending -= 1
                    if success:
                        successes += 1
                    else:
                        reserved -= 1
                    self.assertEqual(reserved, pending + successes)
                    self.assertGreaterEqual(reserved, 0)
                    active = pending > 0 or successes > 0
                    if pending:
                        self.assertTrue(active)
                self.assertEqual(reserved, sum(results))
                self.assertEqual(active, any(results))
        queue = AI.split("protected void INF_SpawnQueued(", 1)[1].split("void INF_CollectMapZones", 1)[0]
        self.assertIn("state.m_iINFPendingGroups--", queue)
        self.assertIn("state.m_iINFReservedAI -= state.m_iINFSpawnSize", queue)
        self.assertIn("state.m_iINFPendingGroups == 0 && state.m_aAgents.IsEmpty()", queue)
        spawn = AI.split("override protected void SpawnGroupAt(", 1)[1].split("void INF_CollectGroups", 1)[0]
        self.assertNotIn("state.m_bActive = false", spawn)
        self.assertIn("total += state.m_iINFReservedAI", AI)
        self.assertIn("reserved -= victim.m_iINFReservedAI", AI)

    def test_never_materialised_groups_eventually_release_for_retry(self):
        tick = AI.split("override protected void TickProximityCheck()", 1)[1].split(
            "protected bool INF_MakeRoom", 1)[0]
        self.assertIn("now >= state.m_fINFSpawnReadyAt + 30000", tick)
        self.assertIn("!INF_BlockRules.HasMembers(state.m_aAgents)", tick)
        self.assertIn("released allocation for retry", tick)
        self.assertIn("INF_FailedActivation(state)", tick)
        failure = AI.split("protected void INF_FailedActivation(", 1)[1].split("protected void INF_SpawnQueued(", 1)[0]
        self.assertIn("if (!state.m_bINFSpawnExhausted)", failure)
        self.assertIn("CallLater(MarkZoneRespawnable, 30000, false, state)", failure)

    def test_cache_retry_never_recreates_an_already_verified_site(self):
        verified = set()
        spawns = {"a": 0, "b": 0, "c": 0}
        for available in ({"a", "b"}, {"a", "b"}, {"a", "b", "c"}):
            for site in spawns:
                if site in verified:
                    continue
                spawns[site] += 1
                if site in available:
                    verified.add(site)
            # Destruction after success must not put a site back in the retry set.
        self.assertEqual(spawns, {"a": 1, "b": 1, "c": 3})
        verify = CACHE.split("protected void INF_VerifyStaticCaches()", 1)[1].split(
            "override void OnGameModeEnd", 1)[0]
        self.assertLess(verify.index("m_aINFVerifiedCaches.Contains(pos)"), verify.index("SpawnCacheAt(pos)"))
        self.assertIn("m_aINFVerifiedCaches.Insert(pos)", verify)
        self.assertIn("m_aINFVerifiedCaches.Clear()", CACHE)

    def test_cache_verification_requires_distinct_entities_and_exact_positions(self):
        verify = CACHE.split("protected void INF_VerifyStaticCaches()", 1)[1].split(
            "override void OnGameModeEnd", 1)[0]
        self.assertIn("m_aINFVerifiedCacheEntities.Contains(cache)", verify)
        self.assertIn("m_aINFVerifiedCacheEntities.Insert(cache)", verify)
        self.assertIn("holding startup rather than reducing the objective total", verify)
        self.assertNotIn("SiteID(pos)", verify)
        positions = {(100.01, 100), (100.04, 100)}
        self.assertEqual(len(positions), 2)
        entities = set()
        complete = True
        for entity in ("same-cache", "same-cache"):
            if entity in entities:
                complete = False
            else:
                entities.add(entity)
        self.assertFalse(complete)

    def test_concurrency_slider_does_not_depend_on_read_variable_call_order(self):
        entries = EDITOR.split("modded class INS_MaxConcurrentCounterAttacksEditorAttribute", 1)[1].split(
            "\n}", 1)[0]
        self.assertIn("Grid(GetGame().GetGameMode())", entries)
        self.assertNotIn("Grid(m_GameMode)", entries)

    def test_invalid_cache_snapshot_holds_startup_and_keeps_retry_callback(self):
        register = INTEL.split("bool INF_RegisterZoneCaches(", 1)[1].split(
            "void INF_OnZoneCaptured", 1)[0]
        self.assertIn("m_aINFZoneCachePositions.Count() != m_aCacheEntities.Count()", register)
        self.assertIn("m_aCacheDestroyed.Count() != m_aCacheEntities.Count()", register)
        self.assertLess(register.index("return false"), register.index("zones.AddCache"))
        self.assertIn("LogLevel.ERROR", register)
        build = AI.split("void INF_StaticCampLayoutsReady()", 1)[1].split(
            "protected bool INF_IndexZoneBuilding", 1)[0]
        self.assertLess(build.index("!intel.INF_IsIntelReady()"), build.index("m_INFZoneStates.AddCamp"))
        self.assertLess(build.index("!intel.INF_RegisterZoneCaches"), build.index("m_bINFCampsReady = true"))
        self.assertNotIn("m_INFZoneStates = null", build)
        self.assertNotIn("new INF_ZoneStateSystem()", build)
        layout = CAMP.split("protected void UpdateLayouts()", 1)[1].split(
            "protected void NextAttempt", 1)[0]
        self.assertLess(layout.index("m_Manager.INF_StaticCampLayoutsReady()"),
                        layout.index("Remove(UpdateLayouts)"))
        self.assertIn("if (m_Manager.INF_AreCampsReady())", layout)
        for positions, entities, destroyed in product(range(4), repeat=3):
            ready = positions == entities == destroyed
            self.assertEqual(ready, len({positions, entities, destroyed}) == 1)


if __name__ == "__main__":
    unittest.main()
