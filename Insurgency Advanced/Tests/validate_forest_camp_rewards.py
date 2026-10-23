"""Offline source-contract/model checks; does not compile Enforce or launch Reforger."""
import math
from pathlib import Path
import random
import unittest
from source_contracts import method_body


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "Scripts" / "Game" / "INF_ForestCampRewards.c").read_text()


def body(signature):
    return method_body(SOURCE, signature)


def slot(position, yaw):
    angle = math.radians(yaw)
    return position[0] + 8 * math.sin(angle), position[1] + 8 * math.cos(angle)


def select(towns, camps, count, saved=None):
    if saved is not None:
        return list(saved)
    candidates = list(dict.fromkeys(towns + camps))
    random.Random(9).shuffle(candidates)
    return candidates[:min(count, len(candidates))]


class ForestCampRewardContracts(unittest.TestCase):
    def test_camp_candidate_offsets_without_modulo_operator(self):
        import re
        camp = (ROOT / "Scripts" / "Game" / "INF_ForestCamps.c").read_text()
        code = re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/', "", camp, flags=re.S)
        self.assertNotIn("%", code)
        self.assertIn("m_Attempt - row * 3 - 1", code)
        self.assertIn("m_Index - Math.Floor(m_Index / 4.0) * 4", code)
        offsets = []
        for attempt in range(9):
            row = math.floor(attempt / 3.0)
            offsets.append(((attempt - row * 3 - 1) * 15, (row - 1) * 15))
        self.assertEqual(set(offsets), {(x, z) for x in (-15, 0, 15) for z in (-15, 0, 15)})
        for index in range(1000):
            self.assertEqual((index - math.floor(index / 4.0) * 4) * 90,
                             (index % 4) * 90)

    def test_action_override_matches_engine_argument_names(self):
        signature = "override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)"
        self.assertIn(signature, SOURCE)
        self.assertIn("component.Search(pUserEntity)", body(signature))

    def test_shared_slot_offsets(self):
        method = body("static vector GetCachePosition(")
        self.assertIn("8 * Math.Sin(angle)", method)
        self.assertIn("8 * Math.Cos(angle)", method)
        self.assertIn("GetSurfaceY", method)
        for yaw, expected in [(0, (100, 208)), (90, (108, 200)),
                              (180, (100, 192)), (270, (92, 200))]:
            with self.subTest(yaw=yaw):
                self.assertLess(math.dist(slot((100, 200), yaw), expected), 1e-10)

    def test_layout_identity_is_not_slot_identity(self):
        method = body("static string RegisterSite(")
        self.assertIn("SiteID(layoutPosition)", method)
        self.assertIn("layout.m_Position = layoutPosition", method)
        self.assertIn("GetCachePosition(site.m_Position, site.m_Yaw)",
                      body("static bool IsCacheSite("))

    def test_selection_and_recovery_count(self):
        for camps in ([], [(0, 0)], [(n * 450, n * 450) for n in range(24)]):
            for count in (0, 1, 3, 30):
                with self.subTest(camps=len(camps), count=count):
                    chosen = select([(1, 1), (2, 2)], camps, count)
                    self.assertEqual(len(chosen), min(count, len(camps) + 2))
                    self.assertEqual(select([], [], 0, saved=chosen), chosen)
        method = body("override protected void SpawnCaches(")
        self.assertLess(method.index("LoadCachePositions"), method.index("AppendCandidates"))
        self.assertIn("Math.Min(m_iCacheCount, all.Count())", method)
        self.assertLess(method.index("SetCachePositions"), method.index("INF_VerifyStaticCaches()"))
        verify = body("protected void INF_VerifyStaticCaches(")
        self.assertLess(verify.index("SpawnCacheAt"), verify.index("FinishCaches()"))
        self.assertIn("if (!cache)", verify)
        self.assertIn("CallLater(INF_VerifyStaticCaches, 5000, false)", verify)

    def test_manifest_round_trip_contract(self):
        save = body("protected static bool SaveSites(")
        load = body("static bool GetRecoveredSites(")
        for field in ("m_Key", "m_Center", "m_Position", "m_Yaw"):
            self.assertIn("file.Write(site." + field + ")", save)
            self.assertIn("file.Read(site." + field + ")", load)
        self.assertIn("INS_CACHE_SAVE_FILE", load)

    def test_async_gate_and_legacy_bypass(self):
        start = body("override void OnGameModeStart(")
        self.assertIn("patrols && patrols.INF_UsesBuildingGrid()", start)
        self.assertIn("BeginDiscovery()", start)
        self.assertIn("SkipDiscovery()", start)
        self.assertLess(start.index("SkipDiscovery()"), start.index("super.OnGameModeStart()"))
        skip = body("static void SkipDiscovery(")
        self.assertIn("s_bDiscoveryReady = true", skip)
        self.assertNotIn("SaveSites", skip)
        self.assertNotIn("DeleteFile", skip)
        self.assertIn("!s_bSkipped", body("static void CompleteDiscovery("))
        spawn = body("override protected void SpawnCaches(")
        self.assertLess(spawn.index("IsDiscoveryReady"), spawn.index("LoadCachePositions"))
        self.assertIn("CallLater(SpawnCaches, 250, false)", spawn)

    def test_consumption_before_roll_and_fail_closed(self):
        method = body("static bool Search(")
        self.assertLess(method.index("s_aSearched.Insert"), method.index("SaveSearches"))
        self.assertLess(method.index("SaveSearches"), method.index("Math.RandomFloat01"))
        self.assertIn("Math.RandomFloat01() < 0.8", method)
        self.assertIn("s_bSearchStateValid = false", method)
        self.assertIn("s_aSearched.Contains(id)", body("static bool CanSearch("))
        consumed = set()
        rolls = 0
        for _ in range(100):
            if "100:200" not in consumed:
                consumed.add("100:200")
                rolls += 1
            consumed = set(consumed)  # Model reload/reinstantiation of persistent state.
        self.assertEqual(rolls, 1)

    def test_load_failures_are_logged(self):
        load = body("protected static bool LoadSearches(")
        for message in ("Cannot open saved searches", "Invalid saved search count",
                        "Cannot read saved search entry"):
            self.assertIn(message, load)
        recover = body("static bool GetRecoveredSites(")
        for message in ("without a camp manifest", "Cannot open saved camp manifest",
                        "Invalid camp manifest count", "Cannot read camp manifest entry"):
            self.assertIn(message, recover)

    def test_corrupt_manifest_is_recovered_empty_not_rediscovered(self):
        recover = body("static bool GetRecoveredSites(")
        self.assertIn("count > 24", recover)
        self.assertIn("keys.Contains(site.m_Key)", recover)
        self.assertIn("coordinates.Contains(coordinate)", recover)
        self.assertIn("sites.Clear()", recover)
        failure = body("protected static bool UnavailableManifest(")
        self.assertIn("s_bManifestValid = false", failure)
        self.assertIn("s_aLayouts.Clear()", failure)
        self.assertIn("return true", failure)
        complete = body("static void CompleteDiscovery(")
        guard = complete[complete.index("if (!s_bManifestValid)"):complete.index("if (!SaveSites())")]
        self.assertIn("s_bDiscoveryReady = true", guard)
        self.assertIn("return;", guard)
        self.assertIn("!s_bManifestValid", body("static string RegisterSite("))
        # Missing legacy sidecar permits discovery; corrupt sidecar counts as recovery.
        for cache_exists, sidecar_exists, valid in [
            (False, False, True), (True, False, True), (True, True, True),
            (True, True, False),
        ]:
            recovered = cache_exists and sidecar_exists
            writable = valid or not sidecar_exists
            if cache_exists and sidecar_exists and not valid:
                self.assertTrue(recovered)
                self.assertFalse(writable)
            elif cache_exists and not sidecar_exists:
                self.assertFalse(recovered)
                self.assertTrue(writable)

    def test_persisted_numbers_are_bounded_and_finite(self):
        check = body("protected static bool ValidSavedVector(")
        self.assertIn("value[i] >= -1000000 && value[i] <= 1000000", check)
        for value in (float("nan"), float("inf"), -float("inf"), 1e20, -1e20):
            self.assertFalse(-1000000 <= value <= 1000000)
        for value in (0, 12345, -100):
            self.assertTrue(-1000000 <= value <= 1000000)
        recover = body("static bool GetRecoveredSites(")
        self.assertIn("site.m_Yaw >= -360 && site.m_Yaw <= 360", recover)
        self.assertIn("site.m_Key.IsEmpty()", recover)
        self.assertIn("site.m_Key.Length() > 64", recover)
        self.assertIn("ValidSavedVector(site.m_Center)", recover)
        self.assertIn("ValidSavedVector(site.m_Position)", recover)

    def test_end_removes_callbacks_and_reports_delete_failures(self):
        end = body("override void OnGameModeEnd(")
        self.assertLess(end.index("Remove(SpawnCaches)"), end.index("super.OnGameModeEnd"))
        self.assertIn("m_bINFCacheSelectionStarted = true", end)
        cleanup = body("static void EndSession(")
        self.assertIn("!FileIO.DeleteFile(SEARCH_SAVE)", cleanup)
        self.assertIn("!FileIO.DeleteFile(SITE_SAVE)", cleanup)
        self.assertIn("Cannot delete saved searches", cleanup)
        self.assertIn("Cannot delete camp manifest", cleanup)
        self.assertIn("s_bBegun = false", cleanup)
        self.assertIn("s_bSkipped = false", cleanup)

    def test_objective_entities_are_not_owned_by_cleanup(self):
        self.assertNotIn("DeleteEntity", SOURCE)
        self.assertEqual(SOURCE.count("modded class INS_CacheSpawnManager"), 1)
        self.assertNotIn("modded class INS_IntelManager", SOURCE)
        self.assertIn("s_aCachePositions", body("static bool IsCacheSite("))

    def test_arsenal_resource_and_action_binding(self):
        path = ROOT / "Prefabs" / "Props" / "Military" / "Arsenal" / "ArsenalBoxes" / "ArsenalBox_Base.et"
        prefab = path.read_text()
        meta = path.with_suffix(".et.meta").read_text()
        self.assertIn("INF_ForestCampSearchComponent", prefab)
        self.assertIn("INF_ForestCampSearchAction", prefab)
        self.assertIn('"default"', prefab)
        self.assertIn('Name "Search camp for intel"', prefab)
        self.assertIn('outName = "Search camp for intel"', SOURCE)
        self.assertIn("{54986385A6AF77B2}Prefabs/Props/Military/Arsenal/ArsenalBoxes/ArsenalBox_Base.et", meta)


if __name__ == "__main__":
    unittest.main()
