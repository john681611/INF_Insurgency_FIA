"""Integrated static-site/defender lifecycle contracts; not an engine simulation."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
CAMP = (ROOT / "Scripts/Game/INF_ForestCamps.c").read_text()
GRID = (ROOT / "Scripts/Game/INF_BuildingGrid.c").read_text()
INTEL = (ROOT / "Scripts/Game/INF_CacheObjective.c").read_text()
GARRISON = (ROOT / "Scripts/Game/INF_GarrisonMapAreas.c").read_text()


class IntegratedSites(unittest.TestCase):
    def test_static_layouts_not_gated_by_players_or_ai(self):
        layout = CAMP.split("protected void UpdateLayouts()", 1)[1].split("protected void NextAttempt", 1)[0]
        self.assertIn("!INF_ForestCampRewards.AreCachesReady()", layout)
        self.assertIn("!intel.INF_IsIntelReady()", layout)
        self.assertNotIn("PlayerSafe", layout)
        self.assertNotIn("m_Players", layout)
        self.assertIn("!ValidateFootprint(camp) || !SpawnLayout(camp)", layout)
        self.assertIn("Cleanup(camp)", layout)
        self.assertIn("!BindArsenalSearch(site)", layout)
        self.assertLess(layout.index("FinishCampObjects()"), layout.index("!INF_ForestCampRewards.AreCachesReady()"))
        self.assertIn("LogLevel.ERROR", layout)
        self.assertLess(layout.index("site.m_LayoutReady = true"), layout.index("INF_StaticCampLayoutsReady()"))

    def test_static_props_survive_capture_eviction_and_spawn_failure(self):
        self.assertNotIn("INF_ForestCamps.Cleanup(", GRID)
        self.assertNotIn("INF_ForestCamps.SpawnLayout(", GRID)
        self.assertNotIn("INF_ForestCamps.BindArsenalSearch(", GRID)
        self.assertIn("Cleanup(site)", CAMP.split("void Stop()", 1)[1])
        state = {"props": 3, "ai": 0, "searched": False, "cache": True}
        for action in ("activate", "capture", "evict", "activate", "spawn-failure", "deactivate"):
            state["ai"] = 4 if action == "activate" else 0
            self.assertEqual(state["props"], 3)
            self.assertTrue(state["cache"])

    def test_building_defenders_can_admit_before_static_sites_but_victory_cannot(self):
        tick = GRID.split("override protected void TickProximityCheck()", 1)[1].split(
            "protected bool INF_MakeRoom", 1)[0]
        self.assertLess(tick.index("!INF_AreZonesReady()"), tick.index("m_INFZoneStates.GetPresence()"))
        self.assertNotIn("!INF_AreCampsReady()", tick)
        activate = GRID.split("override protected void ActivateZone", 1)[1].split(
            "override bool IsPositionInActiveZone", 1)[0]
        self.assertLess(activate.index("!state.m_INFCamp.m_LayoutReady"), activate.index("!INF_MakeRoom(state)"))
        self.assertIn("if (!INF_UsesBlockControl())", GARRISON)
        self.assertIn("!patrols.INF_AreCampsReady()", GRID.split("override protected void CounterAttackTick", 1)[1])
        complete = INTEL.split("protected bool INF_CanCompleteMission()", 1)[1].split(
            "override protected void TriggerMissionComplete", 1)[0]
        self.assertLess(complete.index("!patrols.INF_AreCampsReady()"), complete.index("INF_EMissionEndMode.CACHES"))

    def test_owned_site_objects_are_not_obstructions_or_concealment(self):
        exclude = CAMP.split("static void ExcludeSite", 1)[1].split("static bool DryFlat", 1)[0]
        self.assertIn("ExcludeHierarchy(prop, exclusions)", exclude)
        self.assertIn("GetCacheEntity(site.m_RewardID)", exclude)
        self.assertIn("ExcludeHierarchy(cache, exclusions)", exclude)
        self.assertIn("exclusions.Insert(entity)", exclude)
        self.assertIn("ExcludeHierarchy(child, exclusions)", exclude)
        self.assertNotIn("DeleteEntity", exclude)
        self.assertNotIn("static bool PlayerSafe", CAMP)
        self.assertNotIn("INF_ForestCamps.PlayerSafe", GRID)
        clear = CAMP.split("static bool ClearVolume", 1)[1].split("static bool RewardSlotClear", 1)[0]
        self.assertIn("ExcludeSite(trace, site, exclusions)", clear)

    def test_no_random_nav_probe_during_activation(self):
        spawn = GRID.split("override protected void SpawnGroupAt", 1)[1].split(
            "protected void INF_TrimActivationZones", 1)[0]
        self.assertIn("pos = FindSafeSpawnPosition(pos, state)", spawn)
        self.assertNotIn("InfantryPosition(state.m_INFCamp)", spawn)
        self.assertNotIn("ValidateNavigation", spawn)
        nav = CAMP.split("static int NavigationTilesReady", 1)[1].split("static int ValidateNavigation", 1)[0]
        self.assertIn("InfantryPosition(site)", nav)
        self.assertIn("IsTileLoaded(pos)", nav)
        self.assertIn("IsTileValid(pos)", nav)
        self.assertNotIn("GetReachablePoint", nav)

    def test_batched_search_remains_bounded_and_stops(self):
        batch = CAMP.split("protected void SearchTick()", 1)[1].split("protected void SearchStep()", 1)[0]
        self.assertIn("batch < 8", batch)
        self.assertIn("!m_DiscoveryFinished", batch)
        self.assertIn("if (m_NavWaitSince >= 0)", batch)
        self.assertIn("m_SearchStarted >= 120000", CAMP)
        self.assertIn("Remove(UpdateLayouts)", CAMP.split("void Stop()", 1)[1])


if __name__ == "__main__":
    unittest.main()
