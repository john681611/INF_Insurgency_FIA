"""Offline models/source contracts, not Enforce compilation or multiplayer tests."""
from dataclasses import dataclass, replace
from pathlib import Path
import random
import re
import unittest
from source_contracts import method_body as body

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "Scripts" / "Game"
INDEX = (SCRIPTS / "INF_TerrainIndex.c").read_text()
CAMP = (SCRIPTS / "INF_ForestCamps.c").read_text()
GRID = (SCRIPTS / "INF_BuildingGrid.c").read_text()
REWARDS = (SCRIPTS / "INF_ForestCampRewards.c").read_text()
ZONES = (SCRIPTS / "INF_ZoneStateSystem.c").read_text()


@dataclass(frozen=True)
class Header:
    world: str = "Everon"
    version: int = 1
    revision: int = 1
    bounds: tuple = (0, 0, 12800, 12800)
    context: tuple = ("USSR camp", 9, ((500, 500, 200),))

    def accepts(self, stored, camp=False):
        return all(getattr(self, name) == getattr(stored, name)
                   for name in ("world", "version", "revision", "bounds")) and (
                       not camp or self.context == stored.context)


@dataclass
class CampState:
    captured: bool = False
    spent: bool = False
    active: bool = False
    permanent: bool = False
    friendly: str = ""
    spawn_enabled: bool = True
    conversions: int = 0

    def notify(self, configured="US", captor="FIA"):
        if self.permanent and not self.friendly:
            self.friendly = configured
            self.spawn_enabled = True
            self.conversions += 1

    def capture(self):
        self.captured = True
        self.permanent = self.spent and not self.active
        self.notify()

    def finish_attack(self, had_living=True):
        self.active = False
        if not had_living:
            self.spent = False
        elif self.captured and not self.permanent:
            self.permanent = True
            self.notify()


class TerrainIndexes(unittest.TestCase):
    def test_derived_files_are_separate_and_survive_mission_cleanup(self):
        self.assertIn('$profile:INF_TerrainIndexes', INDEX)
        self.assertNotIn('$profile:INS_Saves', INDEX)
        self.assertNotIn("DeleteFile", INDEX)
        end = body(REWARDS, "static void EndSession(")
        self.assertNotIn("INF_TerrainIndex", end)
        self.assertIn("DeleteFile(SEARCH_SAVE)", end)
        self.assertIn("DeleteFile(SITE_SAVE)", end)

    def test_world_schema_bounds_and_manual_revision_invalidate_both_indexes(self):
        current = Header()
        for changed in (replace(current, world="Arland"), replace(current, version=2),
                        replace(current, revision=2), replace(current, bounds=(0, 0, 8000, 8000))):
            for camp in (False, True):
                self.assertFalse(current.accepts(changed, camp))
        header = body(INDEX, "protected bool ReadHeader(")
        for check in ("version == VERSION", "storedKind == kind", "world == m_World",
                      "revision == m_Revision", "minimum == m_Min", "maximum == m_Max"):
            self.assertIn(check, header)
        self.assertIn("INF_GetTerrainIndexRevision()", INDEX)

    def test_composition_footprint_and_safe_zones_only_invalidate_camp_pool(self):
        current = Header()
        for context in (("FIA camp", 9, current.context[2]),
                        ("USSR camp", 12, current.context[2]),
                        ("USSR camp", 9, ((600, 500, 200),))):
            changed = replace(current, context=context)
            self.assertTrue(current.accepts(changed))
            self.assertFalse(current.accepts(changed, camp=True))
        self.assertIn("(kind == 0 || context == m_CampContext)", INDEX)
        for value in ("INF_GetCampComposition()", "INF_GetCampFootprintRadius()",
                      "zone.GetOwner().GetOrigin()", "zone.GetRadius()", "exclusions.Sort()"):
            self.assertIn(value, INDEX)

    def test_candidate_serialization_has_no_mission_or_runtime_state(self):
        save = body(INDEX, "void SaveCandidates(")
        fields = set(re.findall(r"file\.Write\(site\.(\w+)\)", save))
        self.assertEqual(fields, {"m_Key", "m_Center", "m_Position", "m_Yaw"})
        for forbidden in ("m_RewardID", "m_Props", "m_Friendly", "m_LayoutReady",
                          "s_aSearched", "m_aCacheDestroyed", "m_bINFObjectiveCaptured"):
            self.assertNotIn(forbidden, INDEX)

    def test_recovery_manifest_precedes_fresh_pool_selection(self):
        begin = body(CAMP, "void Begin()")
        self.assertLess(begin.index("GetRecoveredSites"), begin.index("LoadCandidates"))
        recovered = begin[begin.index("GetRecoveredSites"):begin.index("LoadCandidates")]
        self.assertIn("StartLayouts()", recovered)
        self.assertIn("return;", recovered)
        self.assertIn("ShuffleSites(m_CandidatePool)", begin)

    def test_whole_candidate_pool_saved_before_shuffling_and_trimming(self):
        cold = body(CAMP, "protected void SearchStep()")
        self.assertLess(cold.index("SaveCandidates(m_Sites)"), cold.index("ShuffleSites(m_Sites)"))
        self.assertLess(cold.index("SaveCandidates(m_Sites)"), cold.index("m_Sites.Resize(count)"))
        warm = body(CAMP, "protected void SelectCachedStep()")
        self.assertNotIn("SaveCandidates", warm)
        self.assertIn("MAX_CANDIDATES = 96", CAMP)
        self.assertIn("MAX_CAMPS = 20", CAMP)
        pool = list(range(96))
        selections = []
        for seed in range(5):
            shuffled = pool.copy()
            random.Random(seed).shuffle(shuffled)
            selections.append(set(shuffled[:7]))
        self.assertEqual(len(pool), 96)
        self.assertTrue(all(len(selection) == 7 for selection in selections))
        self.assertTrue(all(selection != selections[0] for selection in selections[1:]))

    def test_warm_selection_rechecks_safety_and_navigation(self):
        warm = body(CAMP, "protected void SelectCachedStep()")
        for check in ("HasRoom(site.m_Position)", "FootprintFailure(site, true)",
                      "ValidateNavigation(site)", "navigation == 1", "20000", "120000"):
            self.assertIn(check, warm)
        self.assertLess(warm.index("navigation == 1"), warm.index("m_Sites.Insert(site)"))
        self.assertIn("LogLevel.WARNING", warm)
        cold = body(CAMP, "protected void SearchStep()")
        self.assertIn("480000", cold)
        self.assertIn("HasRoom(site.m_Position)", cold)
        self.assertLess(cold.index("HasRoom(site.m_Position)"), cold.index("FootprintFailure(site, true)"))

    def test_raw_building_index_replaces_second_world_scan(self):
        discovery = body(GRID, "override protected void DiscoverZones()")
        self.assertLess(discovery.index("LoadMapIndex()"), discovery.index("CallLater(INF_ScanMapTick"))
        scan = body(GRID, "protected void INF_ScanMapTick()")
        self.assertIn("QueryEntitiesByAABB", scan)
        self.assertLess(scan.index("SaveMapIndex()"), scan.index("m_INFForestCamps.Begin()"))
        build = body(GRID, "void INF_StaticCampLayoutsReady()")
        self.assertIn("AddBuildingZones(m_INFZoneStates)", discovery)
        self.assertNotIn("QueryEntitiesByAABB", build)
        add = body(CAMP, "void AddBuildingZones(")
        self.assertIn("int i = m_PublishedBuildings", add)
        self.assertIn("zones.AddBuilding(m_Buildings[i])", add)

    def test_loads_are_staged_until_valid_footer(self):
        for signature, assignment in (("bool LoadMap(", "buildings = readBuildings"),
                                      ("bool LoadCandidates(", "candidates = readCandidates")):
            load = body(INDEX, signature)
            self.assertLess(load.index('footer != "COMPLETE"'), load.index(assignment))
            self.assertIn("Invalid(file,", load)
            self.assertIn("!Replication.IsServer()", load)
        self.assertIn("count > 1000000", INDEX)
        self.assertIn("count > INF_ForestCamps.MAX_CANDIDATES", INDEX)
        self.assertIn("keys.Contains(key)", INDEX)
        self.assertIn("map<string, bool> keys", INDEX)
        self.assertIn("site.m_Key != INF_BuildingGrid.Key(site.m_Center)", INDEX)
        self.assertIn("site.m_Yaw >= -360 && site.m_Yaw <= 360", INDEX)

    def test_saves_are_server_only_and_incomplete_writes_are_visible(self):
        for signature in ("void SaveMap(", "void SaveCandidates("):
            save = body(INDEX, signature)
            self.assertIn("!Replication.IsServer()", save)
            self.assertIn('file.Write("COMPLETE")', save)
            self.assertIn("LogLevel.ERROR", save)


class PermanentCampOwnership(unittest.TestCase):
    def test_hostile_spawning_stays_enabled_until_permanent_ownership_transfer(self):
        state = CampState()
        state.capture()
        self.assertTrue(state.spawn_enabled)
        state.spent = state.active = True
        state.capture()
        self.assertTrue(state.spawn_enabled)
        self.assertEqual(state.friendly, "")
        prepare = body(CAMP, "static bool PrepareComposition(")
        self.assertIn("spawn.SetSpawnPointEnabled_S(true)", prepare)
        self.assertNotIn("SetFaction(", prepare)
        handler = body(GRID, "protected void INF_OnCampCaptured(")
        self.assertIn("Replication.IsServer()", handler)
        self.assertIn("state.m_bINFPermanent", handler)

    def test_successful_defence_notifies_permanent_transition(self):
        state = CampState(captured=True, spent=True, active=True)
        state.finish_attack()
        self.assertTrue(state.permanent)
        self.assertTrue(state.spawn_enabled)
        ended = body(ZONES, "void AttackEnded(")
        self.assertIn("state.m_bINFMarkedCleared && !state.m_bINFPermanent", ended)
        self.assertLess(ended.index("state.m_bINFPermanent = true"),
                        ended.index("CompleteCapture(state)"))
        self.assertIn("m_OnCaptured.Invoke(state)", body(ZONES, "protected void CompleteCapture("))

    def test_lost_then_recaptured_camp_also_becomes_friendly(self):
        state = CampState(captured=False, spent=True, active=True)
        state.finish_attack()
        self.assertTrue(state.spawn_enabled)
        self.assertEqual(state.friendly, "")
        state.capture()
        self.assertTrue(state.spawn_enabled)
        capture = body(ZONES, "void CheckCapture(")
        self.assertIn("state.m_bINFAttackSpent && !state.m_bINFAttackActive", capture)
        self.assertIn("CompleteCapture(state)", capture)

    def test_failed_attack_spawn_does_not_make_capture_permanent(self):
        state = CampState(captured=True, spent=True, active=True)
        state.finish_attack(had_living=False)
        self.assertFalse(state.permanent)
        self.assertTrue(state.spawn_enabled)
        state.capture()
        self.assertTrue(state.spawn_enabled)
        self.assertEqual(state.friendly, "")

    def test_conversion_is_once_and_uses_configured_faction_not_captor(self):
        state = CampState(permanent=True)
        for _ in range(20):
            state.notify(configured="US", captor="FIA")
        self.assertEqual(state.conversions, 1)
        self.assertEqual(state.friendly, "US")
        friendly = body(CAMP, "bool MakeFriendly(")
        self.assertIn("if (site.m_Friendly)", friendly)
        self.assertIn("!Replication.IsServer()", friendly)
        self.assertIn("m_Manager.INF_GetPlayerFaction()", friendly)
        self.assertNotIn(".GetPlayerFaction(", friendly)
        self.assertIn("GetFactionByKey(m_sINFPlayerFaction)", GRID)
        for call in ("point.SetFaction(faction)", 'point.SetSpawnPointName("Captured Camp")',
                     "point.SetSpawnPointEnabled_S(true)", "site.m_Friendly = true"):
            self.assertIn(call, friendly)

    def test_missing_arsenal_or_spawn_does_not_partially_enable_camp(self):
        friendly = body(CAMP, "bool MakeFriendly(")
        guard = friendly.index("if (!arsenal || spawns.IsEmpty())")
        self.assertLess(guard, friendly.index("BindFriendlyFaction("))
        self.assertLess(guard, friendly.index("owner.BindFaction(faction)"))
        self.assertLess(guard, friendly.index("SetSpawnPointEnabled_S(true)"))
        self.assertIn("LogLevel.ERROR", friendly)

    def test_objective_credit_waits_for_successful_permanent_transfer(self):
        handler = body(GRID, "protected void INF_OnCampCaptured(")
        self.assertIn("!state.m_bINFPermanent", handler)
        self.assertLess(handler.index("!m_INFForestCamps.MakeFriendly"),
                        handler.index("state.m_bINFObjectiveCaptured = true"))
        self.assertIn("CallLater(INF_RetryCampCapture, 5000, false, state)", handler)
        collect = body(GRID, "void INF_CollectCaptureObjectives(")
        self.assertIn("captured = state.m_bINFPermanent && state.m_INFCamp.m_Friendly", collect)
        capture = body(ZONES, "void CheckCapture(")
        self.assertIn("if (!state.m_INFCamp)\n\t\t\tstate.m_bINFObjectiveCaptured = true", capture)
        end = body(GRID, "override void OnGameModeEnd(")
        self.assertIn("Remove(INF_RetryCampCapture)", end)
        self.assertLess(GRID.index("GetOnCaptured().Insert(INF_OnCampCaptured)"),
                        GRID.index("GetOnObjectivesChanged().Insert(intel.INF_QueueCampaignObjectives)"))
        for permanent, friendly in ((False, False), (False, True), (True, False), (True, True)):
            objective_done = permanent and friendly
            self.assertEqual(objective_done, (permanent, friendly) == (True, True))

    def test_composition_ownership_snapshot_reapplies_root_and_children_for_jip(self):
        prefab = (ROOT / "Prefabs/Compositions/Slotted/BuildableComposition_Base.et").read_text()
        self.assertIn('GenericEntity : "{6E9190BC2713551D}', prefab)
        self.assertIn('INF_CampOwnershipComponent "{6AA19CB200000510}"', prefab)
        component = CAMP.split("class INF_CampOwnershipComponent : ScriptComponent", 1)[1].split(
            "class INF_ForestCell", 1)[0]
        self.assertEqual(component.count('[RplProp(onRplName: "QueueFactionUpdate")]'), 1)
        bind = body(component, "bool BindFaction(")
        self.assertIn("!CanBindFaction()", bind)
        self.assertIn("Replication.BumpMe()", bind)
        self.assertIn("SetCampFaction(GetOwner(), faction)", bind)
        self.assertIn("QueueFactionUpdate()", body(component, "override void EOnInit("))
        apply = body(component, "protected void ApplyFaction(")
        self.assertIn("SetCampFaction(GetOwner(), faction)", apply)
        self.assertNotIn("m_iFactionObjects", component)
        self.assertIn("CallLater(ApplyFaction, 5000, false)", apply)
        self.assertIn("if (!m_bFactionWarning)", apply)
        self.assertIn("Remove(ApplyFaction)", apply)
        faction = body(CAMP, "static void SetCampFaction(")
        self.assertIn("affiliation.GetAffiliatedFaction() != faction", faction)
        self.assertIn("Remove(ApplyFaction)", body(component, "override void OnDelete("))
        friendly = body(CAMP, "bool MakeFriendly(")
        self.assertLess(friendly.index("!owner.CanBindFaction()"), friendly.index("BindFriendlyFaction("))
        self.assertLess(friendly.index("owner.BindFaction(faction)"), friendly.index("SetSpawnPointEnabled_S(true)"))

    def test_cleanup_clears_ownership_latch_before_a_layout_can_be_rebuilt(self):
        cleanup = body(CAMP, "static void Cleanup(")
        for assignment in ("site.m_LayoutReady = false", "site.m_CompositionReady = false",
                           "site.m_Friendly = false"):
            self.assertIn(assignment, cleanup)
        self.assertIn("site.m_Props.Clear()", cleanup)
        state = CampState(permanent=True)
        state.notify()
        state.friendly = ""
        state.spawn_enabled = False
        state.notify()
        self.assertEqual(state.conversions, 2)
        self.assertTrue(state.spawn_enabled)

    def test_arsenal_ownership_is_replicated_and_applied_on_initialization(self):
        self.assertRegex(REWARDS, r'\[RplProp\(onRplName: "ApplyFriendlyFaction"\)\]\s+'
                         r'protected string m_sFriendlyFaction;')
        bind = body(REWARDS, "bool BindFriendlyFaction(")
        self.assertIn("!Replication.IsServer()", bind)
        self.assertIn("ApplyFriendlyFaction()", bind)
        self.assertIn("Replication.BumpMe()", bind)
        self.assertIn("ApplyFriendlyFaction()", body(REWARDS, "override void EOnInit("))
        self.assertIn("SetAffiliatedFaction(faction)", body(REWARDS, "protected void ApplyFriendlyFaction("))
        apply = body(REWARDS, "protected void ApplyFriendlyFaction(")
        self.assertIn("CallLater(ApplyFriendlyFaction, 5000, false)", apply)
        self.assertIn("if (!m_bFactionWarning)", apply)
        self.assertIn("Remove(ApplyFriendlyFaction)", body(REWARDS, "override void OnDelete("))

    def test_search_uses_existing_arsenal_and_preserves_shared_persistence(self):
        bind = body(CAMP, "static bool BindArsenalSearch(")
        self.assertIn("FindArsenal(root)", bind)
        self.assertIn("search.BindSite(site.m_RewardID)", bind)
        for forbidden in ("SpawnEntityPrefab", "m_Props.Insert", "DeleteEntity"):
            self.assertNotIn(forbidden, bind)
        find = body(CAMP, "static INF_ForestCampSearchComponent FindArsenal(")
        self.assertIn("FindComponent(SCR_ArsenalComponent)", find)
        self.assertIn("FindArsenal(child)", find)
        available = body(REWARDS, "static bool CanSearch(")
        self.assertIn("IsCacheSite(id)", available)
        search = body(REWARDS, "static bool Search(")
        self.assertLess(search.index("SaveSearches"), search.index("Math.RandomFloat01"))
        self.assertIn("Math.RandomFloat01() < 0.8", search)
        self.assertNotIn("SaveSearches", body(CAMP, "bool MakeFriendly("))


if __name__ == "__main__":
    unittest.main()
