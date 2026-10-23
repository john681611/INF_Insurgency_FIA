"""Authority contracts and cache event/recovery models; not multiplayer execution."""
from itertools import permutations
from pathlib import Path
import unittest
from source_contracts import method_body

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / "Scripts" / "Game"
INTEL = (GAME / "INF_CacheObjective.c").read_text()
REWARDS = (GAME / "INF_ForestCampRewards.c").read_text()
PRISONERS = (GAME / "INF_Prisoners.c").read_text()
CONTROLLER = (GAME / "INF_GMDebugMarkers.c").read_text()


def method(source, signature):
    return method_body(source, signature, strip_literals=True)


class CacheState:
    def __init__(self, selected, registered=None):
        self.selected = list(selected)
        self.registered = list(selected if registered is None else registered)
        self.flags = dict.fromkeys(selected, False)
        self.pending = set()
        self.ready = False
        self.ended = False
        self.awards = 0

    def restore(self, saved_counter, saved_flags):
        if len(self.registered) != len(self.selected) or len(saved_flags) != len(self.selected):
            return False
        if set(self.registered) != set(self.selected):
            return False
        self.registered = list(self.selected)
        self.flags = {key: flag or key in self.pending
                      for key, flag in zip(self.selected, saved_flags)}
        self.ready = True
        return True

    def destroy(self, key, server=True):
        if not server or self.ended or key not in self.registered:
            return
        if not self.ready:
            self.pending.add(key)
        elif not self.flags[key]:
            self.flags[key] = True
            self.awards += 1

    def can_end(self, mode=0, camps=(0, 0), zones=(0, 0)):
        return (self.ready and bool(self.selected)
                and len(self.registered) == len(self.selected)
                and all(self.flags.values())
                and (mode == 0 or camps[0] >= camps[1])
                and (mode != 2 or zones[0] >= zones[1]))


class ReplicationAndCacheVictory(unittest.TestCase):
    def test_selected_cache_registration_can_finish_after_spawner_readiness(self):
        for ready in (False, True):
            selected = ["A", "B", "C"]
            registered = {}
            for pos in ("C", "A", "B"):
                allowed = pos in selected and pos not in registered
                self.assertTrue(allowed, (ready, pos))
                registered[pos] = "entity-" + pos
            self.assertEqual(set(registered), set(selected))
        register = method(INTEL, "override void RegisterCache(")
        self.assertNotIn("AreCachesReady()", register)
        for guard in ("!cacheEntity", "m_bINFIntelLoaded", "m_bINFIntelLoadFailed",
                      "!selectedPositions.Contains(pos)", "m_aINFZoneCachePositions.Contains(pos)"):
            self.assertIn(guard, register)
        self.assertLess(register.index("!selectedPositions.Contains(pos)"),
                        register.index("super.RegisterCache(cacheEntity, pos)"))
        for loaded, failed, selected, duplicate, expected in (
                (False, False, True, False, True),
                (True, False, True, False, False),
                (False, True, True, False, False),
                (False, False, False, False, False),
                (False, False, True, True, False)):
            self.assertEqual(not loaded and not failed and selected and not duplicate, expected)

    def test_duplicate_second_cache_callbacks_cannot_finish_three_cache_mission(self):
        for events in (("A", "A", "B"), ("A", "B", "B"), ("A", "B") * 6):
            state = CacheState("ABC")
            state.restore(0, [False] * 3)
            for key in events:
                state.destroy(key)
            self.assertFalse(state.can_end())
            self.assertEqual(state.awards, 2)
            state.destroy("C")
            self.assertTrue(state.can_end())

    def test_inflated_saved_aggregate_is_not_victory_evidence(self):
        for stale_count in (2, 3, 100, -5):
            state = CacheState("ABC")
            state.restore(stale_count, [True, False, False])
            state.destroy("B")
            self.assertFalse(state.can_end())
            state.destroy("C")
            self.assertTrue(state.can_end())
        gate = method(INTEL, "protected bool INF_CanCompleteMission()")
        self.assertIn("!Replication.IsServer()", gate)
        self.assertIn("!INF_CacheRosterComplete()", gate)
        self.assertIn("m_iCachesDestroyed != m_aCacheEntities.Count()", gate)
        self.assertIn("foreach (bool destroyed : m_aCacheDestroyed)", gate)
        self.assertIn("if (!destroyed)", gate)

    def test_partial_or_mismatched_roster_cannot_restore_or_end(self):
        for registered, saved in (("AB", [True] * 3), ("ABC", [True] * 2),
                                  ("ABD", [True] * 3)):
            state = CacheState("ABC", registered)
            self.assertFalse(state.restore(3, saved))
            self.assertFalse(state.can_end())
        roster = method(INTEL, "protected bool INF_CacheRosterComplete()")
        for field in ("m_aINFZoneCachePositions", "m_aCacheDestroyed", "m_aCacheIntelCounts",
                      "m_aCacheRevealed", "m_aCacheMarkers", "m_aCacheMarkerDists",
                      "m_aCacheMarkerWorldX", "m_aCacheMarkerWorldZ"):
            self.assertIn(field + ".Count() == count", roster)
        self.assertIn("count == INF_ForestCampRewards.CacheCount()", roster)

    def test_spawn_retry_order_does_not_reassign_saved_cache_identity(self):
        for registration in permutations("ABC"):
            state = CacheState("ABC", registration)
            self.assertTrue(state.restore(1, [False, True, False]))
            self.assertTrue(state.flags["B"])
            self.assertFalse(state.flags["A"])
            self.assertFalse(state.flags["C"])
        load = method(INTEL, "override protected void LoadIntelState()")
        self.assertLess(load.index("GetCachePositions(positions)"), load.index("INF_ReadCacheIntel(records"))
        self.assertIn("m_aINFZoneCachePositions.Find(pos)", load)
        self.assertIn("m_aCacheEntities = ordered", load)

    def test_preload_destruction_is_deduplicated_and_merged_after_recovery(self):
        state = CacheState("ABC", "ACB")
        for key in "BBB":
            state.destroy(key)
        self.assertFalse(state.can_end())
        self.assertEqual(state.pending, {"B"})
        state.restore(1, [True, False, False])
        self.assertEqual(state.flags, {"A": True, "B": True, "C": False})
        self.assertFalse(state.can_end())
        load = method(INTEL, "override protected void LoadIntelState()")
        self.assertIn("record.m_Destroyed || m_aINFPendingCacheDestructions.Contains", load)
        self.assertLess(load.index("INF_ReadCacheIntel(records"), load.index("m_bINFIntelLoaded = true"))

    def test_destroy_is_authoritative_idempotent_and_has_one_victory_scheduler(self):
        body = method(INTEL, "override void OnCacheDestroyed(")
        self.assertLess(body.index("!Replication.IsServer()"), body.index("super.OnCacheDestroyed"))
        self.assertLess(body.index("m_aCacheDestroyed[index]"), body.index("super.OnCacheDestroyed"))
        self.assertLess(body.index("!m_bINFIntelLoaded"), body.index("super.OnCacheDestroyed"))
        self.assertGreater(body.index("Remove(TriggerMissionComplete)"), body.index("super.OnCacheDestroyed"))
        self.assertLess(body.index("Remove(TriggerMissionComplete)"), body.index("INF_QueueCampaignObjectives()"))
        for server in (False, True):
            state = CacheState("ABC")
            state.restore(0, [False] * 3)
            state.destroy("A", server=server)
            self.assertEqual(state.awards, int(server))

    def test_saved_data_is_staged_and_checked_before_any_live_apply(self):
        reader = method(INTEL, "protected bool INF_ReadCacheIntel(")
        self.assertIn("savedCount != count", reader)
        self.assertIn("destroyed != 0 && destroyed != 1", reader)
        self.assertIn("record.m_Intel < 0", reader)
        self.assertIn("markers < 0 || markers > 100000", reader)
        self.assertIn("!file.Read(x) || !file.Read(z) || !file.Read(distance)", reader)
        self.assertNotIn("CreateCacheMarker(", reader)
        self.assertNotIn("m_aCacheDestroyed[", reader)
        load = method(INTEL, "override protected void LoadIntelState()")
        self.assertIn("m_bINFIntelLoadFailed = true", load)
        self.assertLess(load.index("INF_ReadCacheIntel(records"), load.index("m_aCacheDestroyed[cacheIndex] ="))
        self.assertLess(load.index("m_bINFIntelLoaded = true"), load.index("SaveIntelState()"))

    def test_saved_hints_are_not_overwritten_before_marker_system_is_available(self):
        load = method(INTEL, "override protected void LoadIntelState()")
        self.assertLess(load.index("!SCR_MapMarkerManagerComponent.GetInstance()"),
                        load.index("m_aCacheDestroyed[cacheIndex] ="))
        self.assertIn("CallLater(LoadIntelState, 250, false)", load)
        self.assertIn("if (!m_bMarkerTypeCached)", load)
        self.assertLess(load.index("if (!m_bMarkerTypeCached)"), load.index("m_bINFIntelLoaded = true"))

    def test_already_credited_cache_is_not_left_as_live_recovery_objective(self):
        load = method(INTEL, "override protected void LoadIntelState()")
        branch = load.split("if (destroyed)", 1)[1].split("CreateCacheMarker(", 1)[0]
        self.assertIn("m_iCachesDestroyed++", branch)
        self.assertIn("SCR_EntityHelper.DeleteEntityAndChildren(spentCache)", branch)
        self.assertIn("continue;", branch)
        self.assertIn("m_aINFZoneCachePositions = positions", load)

    def test_intel_readiness_is_replicated_but_rewards_and_save_are_server_only(self):
        self.assertRegex(INTEL, r"\[RplProp\(\)\]\s+protected bool m_bINFIntelLoaded")
        self.assertIn("Replication.BumpMe()", method(INTEL, "override protected void LoadIntelState()"))
        for signature in ("override protected void LoadIntelState()", "override protected void SaveIntelState()",
                          "override void RegisterCache(", "override void AddIntelAndBroadcast(",
                          "override protected void TriggerMissionComplete()"):
            self.assertIn("!Replication.IsServer()", method(INTEL, signature), signature)
        body_action = PRISONERS.split("modded class INS_BodySearchAction", 1)[1]
        perform = method(body_action, "override void PerformAction(")
        self.assertLess(perform.index("!INF_IntelSearchReady()"), perform.index("super.PerformAction("))
        search = method(REWARDS, "static bool Search(")
        self.assertLess(search.index("!intel.INF_IsIntelReady()"), search.index("s_aSearched.Insert"))

    def test_invalid_or_orphaned_saves_never_fall_back_to_fresh_random_objectives(self):
        spawn = method(REWARDS, "override protected void SpawnCaches()")
        self.assertIn("FileIO.FileExists(INS_CACHE_SAVE_FILE) || FileIO.FileExists(INS_INTEL_SAVE_FILE)", spawn)
        self.assertLess(spawn.index("FileIO.FileExists(INS_INTEL_SAVE_FILE)"), spawn.index("AppendCandidates"))
        positions = method(REWARDS, "override protected bool LoadCachePositions(")
        self.assertIn("!file.Read(count)", positions)
        self.assertIn("!file.Read(x) || !file.Read(y) || !file.Read(z)", positions)
        self.assertIn("outPos.Contains(pos)", positions)
        self.assertIn("x >= -1000000", positions)

    def test_mission_end_runs_once_and_stops_later_mutation(self):
        trigger = method(INTEL, "override protected void TriggerMissionComplete()")
        self.assertLess(trigger.index("m_bINFMissionEnded = true"), trigger.rindex("super.TriggerMissionComplete()"))
        self.assertIn("if (m_bINFMissionEnded)", trigger)
        end = method(INTEL, "override void OnGameModeEnd(")
        self.assertIn("m_bINFMissionEnded = true", end)
        self.assertIn("Remove(TriggerMissionComplete)", end)
        save = method(INTEL, "override protected void SaveIntelState()")
        self.assertIn("m_bINFMissionEnded", save)
        self.assertIn("m_bINFMissionEnded", method(INTEL, "override void OnCacheDestroyed("))
        for marker in ("cacheVictoryAuthorised=", "Cache roster ready:"):
            self.assertIn(marker, INTEL)
        self.assertIn("INF_DebugContentChanged()", method(INTEL, "override void OnCacheDestroyed("))

    def test_capture_modes_and_actual_selected_roster_are_preserved(self):
        state = CacheState("ABC")
        state.restore(1, [True, False, False])
        state.destroy("B")
        self.assertFalse(state.can_end())
        state.destroy("C")
        self.assertTrue(state.can_end(0))
        self.assertFalse(state.can_end(1, camps=(1, 2)))
        self.assertFalse(state.can_end(2, camps=(2, 2), zones=(2, 3)))
        self.assertTrue(state.can_end(2, camps=(2, 2), zones=(3, 3)))
        # A genuinely two-cache recovered roster is not silently expanded or reset.
        smaller = CacheState("AB")
        smaller.restore(0, [True, True])
        self.assertTrue(smaller.can_end())
        hint = method(INTEL, "override protected void ShowCacheDestroyedHint(")
        self.assertIn("m_iINFMissionEndMode != INF_EMissionEndMode.CACHES", hint)

    def test_private_rpc_results_stay_private_and_persistent_state_is_not_rpc_only(self):
        self.assertEqual(CONTROLLER.count("RplRcver.Owner"), 2)
        self.assertNotIn("RplRcver.Broadcast", CONTROLLER)
        for field in ("m_bCaptured", "m_bQuestioned"):
            self.assertRegex(PRISONERS, r"\[RplProp[^\]]*\]\s+protected bool " + field)
        self.assertRegex(REWARDS, r"\[RplProp\(\)\]\s+protected bool m_bUnavailable")
        self.assertRegex(REWARDS, r"\[RplProp\(\)\]\s+protected string m_sSiteID")


if __name__ == "__main__":
    unittest.main()
