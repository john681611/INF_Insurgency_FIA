"""Offline models and source contracts for the unified zone architecture."""
from dataclasses import dataclass
from math import floor
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "Scripts" / "Game"
CORE = (SCRIPTS / "INF_ZoneStateSystem.c").read_text()
AI = (SCRIPTS / "INF_BuildingGrid.c").read_text()
CAMP = (SCRIPTS / "INF_ForestCamps.c").read_text()
CACHE = (SCRIPTS / "INF_ForestCampRewards.c").read_text()
INTEL = (SCRIPTS / "INF_CacheObjective.c").read_text()
EDITOR = (SCRIPTS / "INF_ZoneEditorAttributes.c").read_text()


def cell(position):
    return tuple(floor(axis / 100) for axis in position)


def influences(zone, site):
    return all(abs(a - b) <= 1 for a, b in zip(cell(zone), cell(site)))


@dataclass
class Territory:
    captured: bool = True
    credit: bool = True
    spent: bool = False
    active: bool = False
    permanent: bool = False

    def launch(self, roll, chance=25, spawn_ok=True):
        if self.spent or self.permanent or not self.captured:
            return False
        if roll >= chance:
            self.permanent = True
            return False
        if not spawn_ok:
            return False
        self.spent = self.active = True
        return True

    def retake(self):
        if self.active:
            self.captured = False

    def finish(self, had_living=True):
        self.active = False
        if not had_living:
            self.spent = False
        elif self.captured:
            self.permanent = True

    def capture(self):
        self.captured = self.credit = True
        self.permanent = self.spent and not self.active


class ZoneArchitecture(unittest.TestCase):
    def test_exact_nine_square_influence_and_negative_coordinates(self):
        for origin in ((50, 50), (-50, -50), (-150, 150), (150, -150)):
            hits = []
            for dx in range(-3, 4):
                for dz in range(-3, 4):
                    zone = (origin[0] + dx * 100, origin[1] + dz * 100)
                    if influences(zone, origin):
                        hits.append((dx, dz))
            self.assertEqual(set(hits), {(x, z) for x in (-1, 0, 1) for z in (-1, 0, 1)})
        self.assertFalse(influences((200, 0), (99.99, 0)))
        self.assertTrue(influences((199.99, 0), (0, 0)))
        self.assertIn("return dx <= 1 && dz <= 1", CORE)

    def test_influence_does_not_stack_and_changes_only_future_allocations(self):
        base, multiplier = 12, 2
        for count in range(1, 30):
            active_allocation = base * (multiplier if count else 1)
            self.assertEqual(active_allocation, 24)
            next_allocation = base  # Captured/destroyed influence sources.
            self.assertEqual(active_allocation, 24)
            self.assertEqual(next_allocation, 12)
        self.assertIn("!source.m_bINFObjectiveCaptured", CORE)
        self.assertIn("!cache.m_Destroyed", CORE)
        self.assertIn("m_iINFTargetAI * INF_ZoneMultiplier(state)", AI)
        self.assertIn("selected.m_iINFTotalAI", AI)
        self.assertNotIn("SpawnEntity", CORE)
        self.assertNotIn("DeleteEntity", CORE)

    def test_random_thresholds_zero_twenty_five_and_hundred_percent(self):
        for chance in range(101):
            launches = sum(Territory().launch(roll, chance) for roll in range(100))
            self.assertEqual(launches, chance)
        self.assertIn("m_iINFChance = 10", AI)
        self.assertIn('[Attribute("10", UIWidgets.Slider, "One counterattack chance roll', AI)
        self.assertIn("Math.RandomInt(0, 100) < m_iINFChance", AI)
        self.assertLess(AI.index("Math.RandomInt(0, 100) < m_iINFChance"), AI.index(".AttackStarted("))

    def test_roll_and_spawn_failures_do_not_spend_attack(self):
        for roll, spawn_ok in ((25, True), (99, True), (0, False)):
            zone = Territory()
            self.assertFalse(zone.launch(roll, spawn_ok=spawn_ok))
            self.assertFalse(zone.spent)
            self.assertEqual(zone.permanent, roll >= 25)
        zone = Territory()
        zone.launch(0)
        zone.finish(had_living=False)
        self.assertFalse(zone.spent)
        self.assertTrue(zone.launch(0))
        self.assertIn("DespawnSquad(partial)", AI)
        self.assertIn("if (!hadLiving || !defeated)", CORE)
        self.assertIn("state.m_bINFAttackSpent = false", CORE)

    def test_defeated_counterattack_makes_capture_permanent(self):
        zone = Territory()
        self.assertTrue(zone.launch(0))
        zone.finish()
        self.assertTrue(zone.permanent)
        self.assertFalse(zone.launch(0))

    def test_retake_then_player_capture_is_permanent_and_credit_retained(self):
        zone = Territory()
        zone.launch(0)
        zone.retake()
        self.assertFalse(zone.captured)
        self.assertTrue(zone.credit)
        zone.finish()
        self.assertFalse(zone.permanent)
        zone.capture()
        self.assertTrue(zone.permanent)
        self.assertFalse(zone.launch(0))
        self.assertIn("state.m_bINFMarkedCleared = false", CORE)
        self.assertNotIn("state.m_bINFObjectiveCaptured = false", CORE)

    def test_capture_owner_is_independent_of_ai_enabled_setting(self):
        tick = CORE.split("protected void CaptureTick()", 1)[1].split("bool ReinforcementsSettled", 1)[0]
        self.assertIn("CheckCapture(state, presence", tick)
        self.assertNotIn("m_bEnabled", tick)
        self.assertIn("CallLater(CaptureTick, 1000, false)", CORE)
        self.assertIn("Remove(CaptureTick)", CORE)
        self.assertIn("presence.CanCapture(state.m_vCenter)", CORE)
        self.assertIn("INF_BlockRules.AdvanceCapture", CORE)
        self.assertIn("vector.DistanceSqXZ(player, state.m_vCenter) <= 40000", CORE)

    def test_progressive_registry_retains_buildings_and_adds_isolated_caches(self):
        build = AI.split("void INF_StaticCampLayoutsReady()", 1)[1].split("bool INF_AreCampsReady", 1)[0]
        self.assertNotIn("new INF_ZoneStateSystem()", build)
        self.assertNotIn("QueryEntitiesByAABB", build)
        self.assertIn("m_INFZoneStates.AddCamp(site)", build)
        self.assertIn("intel.INF_RegisterZoneCaches(m_INFZoneStates)", build)
        self.assertIn("if (state)\n\t\t\treturn state", CORE)
        self.assertIn("EnsureZone(position)", CORE.split("void AddCache", 1)[1])
        discovery = AI.split("override protected void DiscoverZones()", 1)[1].split(
            "void INF_FinishCampDiscovery", 1)[0]
        self.assertLess(discovery.index("new INF_ZoneStateSystem()"), discovery.index("m_INFForestCamps.Begin()"))
        self.assertIn("zones.AddBuilding(m_Buildings[i])", CAMP)
        layout = CAMP.split("protected void UpdateLayouts()", 1)[1].split("protected void NextAttempt", 1)[0]
        self.assertLess(layout.index("SpawnLayout(camp)"), layout.index("FinishCampObjects()"))
        self.assertLess(layout.index("AreCachesReady()"), layout.index("INF_StaticCampLayoutsReady()"))
        self.assertIn("!INF_ForestCampRewards.AreCampObjectsReady()", CACHE)

    def test_event_driven_cache_and_camp_progress_with_recovery_snapshot(self):
        self.assertIn("m_OnCaptured.Invoke(state)", CORE)
        self.assertIn("GetOnObjectivesChanged().Insert(intel.INF_QueueCampaignObjectives)", AI)
        self.assertIn("GetOnObjectivesChanged().Remove(intel.INF_QueueCampaignObjectives)", AI)
        self.assertIn("INF_GetZoneStates().CacheDestroyed(cacheEntity)", INTEL)
        self.assertIn("m_aCacheDestroyed[i]", INTEL.split("bool INF_RegisterZoneCaches", 1)[1])
        self.assertIn("m_aINFZoneCachePositions.Insert(pos)", INTEL)

    def test_attack_wave_preflights_separate_positions_before_eviction(self):
        attack = AI.split("override protected void CounterAttackTick()", 1)[1].split(
            "protected bool INF_GridAttackPositionSafe", 1)[0]
        self.assertLess(attack.index("positions.Insert(groupPosition)"), attack.index("INF_MakeRoomForBudget"))
        self.assertIn("float lateralOffset = slot * 12.0", attack)
        self.assertIn("empty + tangent * lateralOffset", attack)
        self.assertIn("INF_BuildingGrid.Spawn(positions[group]", attack)
        self.assertLess(attack.index("spawned.Insert(squad)"), attack.index(".AttackStarted("))
        self.assertIn("activeAttacks < Math.Min(m_iMaxConcurrentCounterAttacks, 2)", attack)

    def test_registered_zone_markers_match_objective_registry(self):
        markers = (SCRIPTS / "INF_GarrisonMapAreas.c").read_text()
        self.assertIn("foreach (INS_AmbientPatrolZoneState state : zones.GetZones())", markers)
        self.assertIn("zones.GetOnChanged().Insert(INF_QueueZoneArea)", markers)
        self.assertNotIn("INF_IsViableCell", markers)
        self.assertNotIn("IsViableMarkerCell", AI)
        self.assertIn("else if (state.m_fINFCaptureSince >= 0)", markers)

    def test_grid_bootstrap_does_not_depend_on_legacy_location_discovery(self):
        for name in ("INS_AmbientPatrolManager", "INS_CounterAttackManager"):
            body = AI.split("modded class " + name + "\n", 1)[1].split("\nmodded class ", 1)[0]
            start = body.split("override void OnGameModeStart()", 1)[1].split("\n\t}", 1)[0]
            self.assertIn("CallLater(TickCallQueue, 100, true)", start)
            self.assertNotIn("m_aGroupPrefabs.IsEmpty", start)
            self.assertNotIn("m_aZones.IsEmpty", start)

    def test_relevant_gm_settings_only_and_live_defaults(self):
        obsolete = ("INS_TownPatrolCountEditorAttribute", "INS_VillagePatrolCountEditorAttribute",
                    "INS_SettlementPatrolCountEditorAttribute", "INS_PatrolRespawnDelayEditorAttribute",
                    "INS_MinSquadsPerAttackEditorAttribute", "INS_MaxSquadsPerAttackEditorAttribute")
        for name in obsolete:
            body = EDITOR.split("modded class " + name, 1)[1].split("\n}", 1)[0]
            self.assertIn("INF_ZoneEditorContext.Grid(m_GameMode)", body)
            self.assertIn("return null", body)
            self.assertIn("super.ReadVariable(item, manager)", body)
        self.assertIn("m_iINFSiteStrengthMultiplier = 2", AI)
        self.assertIn("INF_SetSiteStrengthMultiplier(var.GetInt() + 1)", EDITOR)
        self.assertIn("INF_SetChance((int)var.GetFloat())", EDITOR)
        config = (ROOT / "Configs/Editor/AttributeLists/Edit.conf").read_text()
        for name in ("INF_SiteStrengthEditorAttribute", "INF_CounterAttackChanceEditorAttribute"):
            self.assertIn(name, config)
        for untouched in ("INS_FieldThreat", "INS_Vehicles", "INS_MaxVehicles"):
            self.assertNotIn("modded class " + untouched, EDITOR)


if __name__ == "__main__":
    unittest.main()
