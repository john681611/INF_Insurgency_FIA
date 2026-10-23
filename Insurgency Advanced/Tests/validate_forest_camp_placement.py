"""Terrain-aware clearance and empty-manifest recovery models; no engine execution."""
from pathlib import Path
import math
import unittest


ROOT = Path(__file__).resolve().parents[1]
CAMP = (ROOT / "Scripts/Game/INF_ForestCamps.c").read_text()
REWARDS = (ROOT / "Scripts/Game/INF_ForestCampRewards.c").read_text()


class ForestCampPlacement(unittest.TestCase):
    def test_interior_requires_wooded_center_and_six_neighbours(self):
        def interior(position, cells):
            x, z = (math.floor(axis / 100) for axis in position)
            if cells.get((x, z), 0) < 8:
                return False
            return sum(
                cells.get((x + dx, z + dz), 0) >= 8
                for dx in (-1, 0, 1) for dz in (-1, 0, 1)
                if dx or dz
            ) >= 6

        for position in ((50, 50), (-50, -50), (100, 0)):
            x, z = (math.floor(axis / 100) for axis in position)
            neighbours = [(x + dx, z + dz)
                          for dx in (-1, 0, 1) for dz in (-1, 0, 1)
                          if dx or dz]
            for count in range(9):
                cells = {cell: 8 for cell in neighbours[:count]}
                self.assertFalse(interior(position, cells))
                cells[x, z] = 7
                self.assertFalse(interior(position, cells))
                cells[x, z] = 8
                self.assertEqual(interior(position, cells), count >= 6)
        check = CAMP.split("protected bool IsForestInterior", 1)[1].split(
            "protected void PreferInteriorCells", 1)[0]
        self.assertIn("INF_BuildingGrid.CellIndex(position[0])", check)
        self.assertIn("INF_BuildingGrid.CellIndex(position[2])", check)
        self.assertIn("cell.m_Trees < 8", check)
        self.assertIn("woodedNeighbours >= 6", check)

    def test_discovery_and_selection_prefer_interiors_with_safe_edge_fallback(self):
        begin = CAMP.split("void Begin()", 1)[1].split("string GetStartupStatus", 1)[0]
        self.assertIn("PreferInteriorCells()", begin)
        self.assertLess(begin.index("GetRecoveredSites"), begin.index("PreferInteriorCells()"))
        self.assertIn("ShuffleSites(m_CandidatePool)", begin)
        shuffle = CAMP.split("protected void ShuffleSites", 1)[1].split(
            "protected void SelectCachedStep", 1)[0]
        self.assertLess(shuffle.index("Math.RandomInt"), shuffle.index("IsForestInterior"))
        self.assertLess(shuffle.index("sites.Insert(preferred)"), shuffle.index("sites.Insert(fallback)"))
        self.assertIn("ShuffleSites(m_Sites)", CAMP)
        for shuffled in ([], [False], [True], [False, True, False, True]):
            ordered = [v for v in shuffled if v] + [v for v in shuffled if not v]
            self.assertEqual(len(ordered), len(shuffled))
            for count in range(len(ordered) + 1):
                self.assertEqual(sum(ordered[:count]), min(count, sum(shuffled)))
        index = (ROOT / "Scripts/Game/INF_TerrainIndex.c").read_text()
        self.assertIn(":forest-interior-v1", index)
        self.assertIn("kind == 0 || context == m_CampContext", index)

    def test_empty_manifest_retries_without_relocating_saved_caches(self):
        recover = REWARDS.split("static bool GetRecoveredSites", 1)[1].split(
            "protected static bool ValidSavedVector", 1)[0]
        empty = recover.split("if (count == 0)", 1)[1].split("array<string> keys", 1)[0]
        self.assertIn("file.Close()", empty)
        self.assertIn("return false", empty)
        self.assertIn("retrying discovery without changing saved cache positions", empty)
        self.assertNotIn("DeleteFile", recover)
        self.assertNotIn("SaveCachePositions", recover)
        self.assertIn("return UnavailableManifest", recover)
        for manifest_count, corrupt, fresh in ((0, False, True), (1, False, False),
                                               (24, False, False), (-1, True, False),
                                               (25, True, False)):
            self.assertEqual(not corrupt and manifest_count == 0, fresh)

    def test_diagonal_endpoints_stay_above_flat_and_sloped_terrain(self):
        height = 3
        clearance = 0.05
        for sx in (-0.1, 0, 0.1):
            for sz in (-0.1, 0, 0.1):
                for dx, dz in ((2.5, 0), (-2.5, 0), (0, 2.5), (0, -2.5)):
                    start_ground = sx * dx + sz * dz
                    end_ground = -start_ground
                    start_y = start_ground + height + clearance
                    end_y = end_ground + clearance
                    for step in range(101):
                        t = step / 100
                        ground = (1 - t) * start_ground + t * end_ground
                        ray = (1 - t) * start_y + t * end_y
                        self.assertGreaterEqual(ray - ground, clearance - 1e-10)
        # Old constant-bottom endpoints enter uphill ground.
        self.assertLess(0 - 0.1 * 2.5, 0)
        clear = CAMP.split("static bool ClearVolume", 1)[1].split("static bool RewardSlotClear", 1)[0]
        self.assertIn("world.GetSurfaceY(start[0], start[2]) + height + 0.05", clear)
        self.assertIn("world.GetSurfaceY(end[0], end[2]) + 0.05", clear)
        self.assertIn('{"1 0 0", "-1 0 0", "0 0 1", "0 0 -1"}', clear)

    def test_clearance_allows_vegetation_but_checks_obstacles_water_and_terrain(self):
        clear = CAMP.split("static bool ClearVolume", 1)[1].split("static bool RewardSlotClear", 1)[0]
        self.assertIn("!DryFlat(start, pos[1]) || !DryFlat(end, pos[1])", clear)
        self.assertIn("TraceFlags.ENTS | TraceFlags.WORLD | TraceFlags.OCEAN", clear)
        self.assertIn("fraction = world.TraceMove(trace, null)", clear)
        self.assertIn("fraction = world.TraceMove(trace, CampObstacleFilter)", clear)
        self.assertIn("if (fraction < 1)", clear)
        self.assertIn("ClearVolume(point, 2.5, 3, site, true)", CAMP)
        reward = CAMP.split("static bool RewardSlotClear", 1)[1].split(
            "static int NavigationTilesReady", 1)[0]
        self.assertIn("DryFlat(pos, site.m_Position[1])", reward)
        self.assertIn("ClearVolume(pos, 2, 3, site, true)", reward)
        self.assertIn("return !BaseTree.Cast(entity)", CAMP)
        index = (ROOT / "Scripts/Game/INF_TerrainIndex.c").read_text()
        self.assertIn(":reward-vegetation-v1", index)
        self.assertIn("Math.AbsFloat(ground - baseHeight) > 0.65", CAMP)
        self.assertIn("vector.Dot(normal, vector.Up) >= 0.9781476", CAMP)
        self.assertIn("static bool ValidateFootprint", CAMP)
        self.assertIn("return FootprintFailure(site, reserveRewardSlot) == 0", CAMP)

    def test_search_logs_rejection_stage_without_forced_fallback(self):
        for name in ("SpatialRejects", "TerrainRejects", "ClearanceRejects",
                     "NavigationRejects", "NavigationTimeouts", "TestedLayouts"):
            self.assertIn("m_i" + name, CAMP)
        self.assertIn("Search diagnostics: cells visited=", CAMP)
        tick = CAMP.split("protected void SearchTick()", 1)[1].split("protected void NextAttempt", 1)[0]
        self.assertIn("int footprintFailure = FootprintFailure(site, true)", tick)
        self.assertIn("int navigation = ValidateNavigation(site)", tick)
        self.assertLess(tick.index("if (navigation != 1)"), tick.index("m_Sites.Insert(site)"))
        self.assertIn("m_SearchStarted >= 120000", tick)
        self.assertIn("No safe woodland sites passed validation", tick)


if __name__ == "__main__":
    unittest.main()
