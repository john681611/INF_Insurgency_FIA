"""Offline scan models/source contracts; engine and multiplayer checks are separate."""
import math
from pathlib import Path
import unittest
from source_contracts import method_body as body

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "Scripts" / "Game"
GRID = (SCRIPTS / "INF_BuildingGrid.c").read_text()
CAMP = (SCRIPTS / "INF_ForestCamps.c").read_text()
INTEL = (SCRIPTS / "INF_CacheObjective.c").read_text()
VEHICLES = (SCRIPTS / "INF_ZoneVehicles.c").read_text()


def tiles(low, high):
    return [(x * 400, z * 400)
            for x in range(math.floor(low[0] / 400), math.ceil(high[0] / 400))
            for z in range(math.floor(low[1] / 400), math.ceil(high[1] / 400))]


class ProgressiveStartup(unittest.TestCase):
    def test_startup_status_is_server_authored_replicated_and_stops_when_ready(self):
        self.assertRegex(GRID, r"\[RplProp\(\)\]\s+protected string m_sINFStartupStatus")
        self.assertRegex(GRID, r"\[RplProp\(\)\]\s+protected bool m_bINFCampsReady")
        publish = body(GRID, "protected void INF_PublishStartupProgress()")
        self.assertLess(publish.index("!Replication.IsServer()"), publish.index("m_sINFStartupStatus = status"))
        self.assertIn("Replication.BumpMe()", publish)
        self.assertIn("Remove(INF_PublishStartupProgress)", publish)
        self.assertIn("m_iINFScanTileTotal - m_aINFScanTiles.Count()", publish)
        self.assertIn("GetStartupStatus()", publish)
        self.assertIn("patrols.INF_GetStartupStatus()", INTEL)

    def test_local_notifications_support_join_in_progress_and_do_not_spam(self):
        start = body(GRID, "override void OnGameModeStart()")
        self.assertIn("if (!System.IsConsoleApp())", start)
        self.assertLess(start.index("CallLater(INF_NotifyStartupProgress"),
                        start.index("!Replication.IsServer()"))
        notify = body(GRID, "protected void INF_NotifyStartupProgress()")
        self.assertIn("!local.GetControlledEntity()", notify)
        self.assertIn("if (now < m_fINFNextStartupHint)", notify)
        self.assertIn("now + 30000", notify)
        self.assertIn("ShowCustomHint", notify)
        self.assertIn("Remove(INF_NotifyStartupProgress)", notify)
        self.assertLess(notify.index("if (m_bINFCampsReady)"),
                        notify.index("GetGame().GetPlayerController()"))
        ready_guard = notify.split("PlayerController local", 1)[0]
        self.assertIn("Remove(INF_NotifyStartupProgress)", ready_guard)
        self.assertIn("return;", ready_guard)
        # Completion and already-ready late joins never display a startup hint.
        next_hint, shown = 0, []
        for now, ready in ((1000, False), (2000, False), (31000, False), (32000, True)):
            if ready or now < next_hint:
                continue
            shown.append(now)
            next_hint = now + 30000
        self.assertEqual(shown, [1000, 31000])
        for has_character in (False, True):
            ready, timer_running = True, True
            late_join_hints = []
            if ready:
                timer_running = False
            elif has_character:
                late_join_hints.append("startup")
            self.assertFalse(timer_running)
            self.assertEqual(late_join_hints, [])

    def test_progress_reports_distinct_work_stages_and_failures(self):
        status = body(CAMP, "string GetStartupStatus()")
        for text in ("Checking reusable camp terrain", "Finding safe camp terrain",
                     "Preparing camps", "Preparing weapon caches", "Restoring intel",
                     "Finalising camp arsenals", "m_StartupFailure"):
            self.assertIn(text, status)
        self.assertIn("Mission initialisation is blocked", GRID)
        self.assertIn("Mission initialisation is blocked", CAMP)
        self.assertNotIn("%", status.replace("%1", "").replace("%2", ""))
        stop = body(GRID, "override void OnGameModeEnd(")
        self.assertIn("Remove(INF_NotifyStartupProgress)", stop)
        self.assertIn("Remove(INF_PublishStartupProgress)", stop)

    def test_aligned_tiles_cover_negative_and_non_aligned_world_bounds_once(self):
        scan = tiles((-150, -850), (899, 1299))
        self.assertEqual(len(scan), len(set(scan)))
        for x in (-150, -100, -0.01, 0, 399.99, 400, 899):
            for z in (-850, -800, -0.01, 0, 400, 1200, 1299):
                owners = [(a, b) for a, b in scan if a <= x < a + 400 and b <= z < b + 400]
                self.assertEqual(len(owners), 1, (x, z))
        for a, b in scan:
            self.assertEqual(a % 100, 0)
            self.assertEqual(b % 100, 0)
        callback = body(GRID, "protected bool INF_IndexBuilding(")
        for check in ("pos[0] <", "pos[0] >=", "pos[2] <", "pos[2] >="):
            self.assertIn(check, callback)

    def test_one_world_query_per_tick_and_nearest_player_priority(self):
        discover = body(GRID, "override protected void DiscoverZones()")
        scan = body(GRID, "protected void INF_ScanMapTick()")
        self.assertNotIn("QueryEntitiesByAABB", discover)
        self.assertEqual(scan.count("QueryEntitiesByAABB"), 1)
        self.assertIn("CallLater(INF_ScanMapTick, 50, true)", discover)
        self.assertIn("m_INFZoneStates.GetPresence().m_AllPlayers", scan)
        self.assertIn("players.Insert(GetOwner().GetOrigin())", scan)
        self.assertIn("INF_BlockRules.Nearest", scan)
        pending = tiles((0, 0), (1200, 1200))
        for player, expected in (((100, 100), (0, 0)), ((1100, 1100), (800, 800))):
            nearest = min(pending, key=lambda p: (p[0] + 200 - player[0]) ** 2
                          + (p[1] + 200 - player[1]) ** 2)
            self.assertEqual(nearest, expected)

    def test_cache_discovery_held_before_background_callbacks(self):
        discover = body(GRID, "override protected void DiscoverZones()")
        self.assertLess(discover.index("INF_ForestCampRewards.BeginDiscovery()"),
                        discover.index("CallLater(INF_ScanMapTick"))
        self.assertIn("if (!Replication.IsServer())", body(GRID, "override void OnGameModeStart()"))

    def test_publish_once_without_replacing_active_or_captured_state(self):
        add = body(CAMP, "void AddBuildingZones(")
        self.assertIn("int i = m_PublishedBuildings", add)
        self.assertIn("m_PublishedBuildings = m_Buildings.Count()", add)
        build = body(GRID, "void INF_StaticCampLayoutsReady()")
        self.assertNotIn("new INF_ZoneStateSystem()", build)
        self.assertNotIn("m_INFZoneStates = null", build)
        self.assertNotIn("m_INFZoneStates.Start", build)
        positions, published, counts = [], 0, {}
        state = {"active": True, "captured": True, "reserved": 8}
        for batch in ([(1, 1), (2, 2)], [], [(401, 1)]):
            positions.extend(batch)
            for x, z in positions[published:]:
                key = (x // 100, z // 100)
                counts[key] = counts.get(key, 0) + 1
            published = len(positions)
        self.assertEqual(counts, {(0, 0): 2, (4, 0): 1})
        self.assertEqual(state, {"active": True, "captured": True, "reserved": 8})

    def test_only_complete_scan_is_saved_and_stop_cancels_it(self):
        scan = body(GRID, "protected void INF_ScanMapTick()")
        done = body(scan, "if (m_aINFScanTiles.IsEmpty())")
        self.assertIn("Remove(INF_ScanMapTick)", done)
        self.assertLess(done.index("SaveMapIndex()"), done.index("m_INFForestCamps.Begin()"))
        self.assertEqual(scan.count("SaveMapIndex()"), 1)
        stop = body(GRID, "override void OnGameModeEnd(")
        self.assertIn("Remove(INF_ScanMapTick)", stop)
        self.assertIn("m_aINFScanTiles.Clear()", stop)
        self.assertNotIn("SaveMapIndex", stop)

    def test_partial_zones_playable_but_final_objectives_and_victory_wait(self):
        tick = body(GRID, "override protected void TickProximityCheck()")
        self.assertIn("!INF_AreZonesReady()", tick)
        self.assertNotIn("AreCachesReady()", tick)
        self.assertIn("!patrols.INF_AreZonesReady()", VEHICLES)
        objectives = body(INTEL, "protected void INF_UpdateCampaignObjectives()")
        self.assertIn('!patrols || patrols.INF_AreCampsReady()', objectives)
        self.assertIn("if (!discoveryReady)", objectives)
        victory = body(INTEL, "protected bool INF_CanCompleteMission()")
        self.assertIn("!patrols.INF_AreCampsReady()", victory)


if __name__ == "__main__":
    unittest.main()
