"""Offline capture-objective models and wiring checks; no Enfusion compilation."""
from itertools import permutations, product
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
INTEL = (ROOT / "Scripts/Game/INF_CacheObjective.c").read_text()
GRID = (ROOT / "Scripts/Game/INF_BuildingGrid.c").read_text()
ZONES = (ROOT / "Scripts/Game/INF_ZoneStateSystem.c").read_text()
GARRISON = (ROOT / "Scripts/Game/INF_GarrisonMapAreas.c").read_text()
POLICY = (ROOT / "Scripts/Game/INF_CaptureObjectives.c").read_text()


def can_end(required, cache_total, cache_done, camps, zones, officers_cleared=True, intel_ready=True, counts_ready=True):
    """required is (camps, zones, officers) switches; caches are always required."""
    if not intel_ready or cache_total == 0 or cache_done < cache_total:
        return False
    if not any(required):
        return True
    if not counts_ready:
        return False
    if required[0] and camps[0] < camps[1]:
        return False
    if required[1] and zones[0] < zones[1]:
        return False
    return not required[2] or officers_cleared


def merge(entries):
    cells = {}
    for key, captured in entries:
        cells[key] = captured and cells.get(key, True)
    return cells


class CaptureObjectives(unittest.TestCase):
    def test_independent_objective_switches(self):
        for required, cache_done, camp_done, zone_done, cleared in product(
                product((False, True), repeat=3), (0, 2, 3), (0, 1), (0, 2), (False, True)):
            with self.subTest(required=required, caches=cache_done, camps=camp_done, zones=zone_done, officers=cleared):
                expected = (cache_done >= 3 and (not required[0] or camp_done == 1)
                            and (not required[1] or zone_done == 2) and (not required[2] or cleared))
                self.assertEqual(can_end(required, 3, cache_done, (camp_done, 1), (zone_done, 2), cleared), expected)
        self.assertIn("protected bool m_bINFRequireCamps;", INTEL)
        self.assertIn("m_iINFCampsCaptured < m_iINFCampsTotal", INTEL)
        self.assertIn("m_iINFZonesCaptured < m_iINFZonesTotal", INTEL)
        self.assertIn("!m_bINFRequireOfficers || m_bINFOfficersCleared", INTEL)
        for name in ("m_bINFRequireCamps", "m_bINFRequireZones", "m_bINFRequireOfficers"):
            self.assertRegex(INTEL, r'\[Attribute\("0", UIWidgets.CheckBox[^\n]+RplProp\(\)\]\s+protected bool ' + name)

    def test_readiness_zero_sites_and_no_cache_objectives(self):
        for required in product((False, True), repeat=3):
            self.assertFalse(can_end(required, 0, 0, (0, 0), (0, 0)))
            self.assertFalse(can_end(required, 3, 3, (0, 0), (0, 0), intel_ready=False))
            self.assertTrue(can_end(required, 3, 3, (0, 0), (0, 0)))
        self.assertTrue(can_end((False, False, False), 3, 3, (0, 0), (0, 0), counts_ready=False))
        for required in ((True, False, False), (False, True, False), (False, False, True)):
            self.assertFalse(can_end(required, 3, 3, (0, 0), (0, 0), counts_ready=False))
        update = INTEL.split("protected void INF_UpdateCampaignObjectives()", 1)[1].split(
            "protected SCR_Task INF_UpdateCaptureTask", 1)[0]
        self.assertLess(update.index("bool discoveryReady ="), update.index("m_iINFCampsTotal ="))
        self.assertLess(update.index("m_iINFZonesTotal ="), update.index("m_bINFCaptureCountsReady = true"))

    def test_unique_overlapping_cells_need_all_sources(self):
        entries = [("1:1", True), ("1:1", False), ("2:1", True), ("3:1", False)]
        for order in permutations(entries):
            cells = merge(order)
            self.assertEqual(len(cells), 3)
            self.assertEqual(sum(cells.values()), 1)
        self.assertEqual(merge([("1:1", True)] * 8), {"1:1": True})
        self.assertIn("captured = captured && cells.Get(key)", POLICY)
        self.assertNotIn("INF_CollectCaptureObjectives", GARRISON)
        self.assertIn("INF_CaptureObjectiveCells.Add(zones, INF_BuildingGrid.Key(state.m_vCenter), captured)", GRID)

    def test_camps_are_separate_from_zones(self):
        collect = GRID.split("void INF_CollectCaptureObjectives(", 1)[1].split(
            "override protected void CheckClearance", 1)[0]
        self.assertIn("if (state.m_INFCamp)", collect)
        self.assertIn("INF_CaptureObjectiveCells.Add(camps, state.m_INFCamp.m_Key, captured)", collect)
        self.assertIn("else\n", collect)
        self.assertNotIn("INS_GarrisonManager", collect)
        self.assertNotIn("garrisons", INTEL)

    def test_credit_survives_respawn(self):
        ever = False
        credits = []
        for currently_captured in (False, True, False, False, True, False):
            ever |= currently_captured
            credits.append(ever)
        self.assertEqual(credits, [False, True, True, True, True, True])
        self.assertIn("state.m_bINFObjectiveCaptured || state.m_bINFMarkedCleared", GRID)
        capture = ZONES.split("void CheckCapture(", 1)[1].split("void AttackStarted(", 1)[0]
        self.assertEqual(capture.count("state.m_bINFObjectiveCaptured = true;"), 1)
        restore = ZONES.split("bool RestorePermanentCamps()", 1)[1].split("void Start(", 1)[0]
        self.assertIn("state.m_bINFObjectiveCaptured = true;", restore)
        self.assertNotIn("m_bINFObjectiveCaptured = false", GRID)
        self.assertNotIn("m_INFCapturedCells", GARRISON)

    def test_task_style_and_late_join_assignment(self):
        self.assertIn('"INF_CaptureHostileCamps"', INTEL)
        self.assertIn('"INF_CaptureHostileZones"', INTEL)
        self.assertIn('"Captured %1/%2 Camps"', INTEL)
        self.assertIn('"Captured %1/%2 Zones"', INTEL)
        helper = INTEL.split("protected SCR_Task INF_UpdateCaptureTask", 1)[1].split(
            "protected bool INF_CanCompleteMission", 1)[0]
        self.assertIn("SCR_TaskSystem.GetTaskFromTaskID(id, false)", helper)
        self.assertIn("{1D0F815858EE24AD}Prefabs/Tasks/BaseTask.et", helper)
        self.assertIn("SCR_ETaskOwnership.EVERYONE", helper)
        self.assertIn("SCR_ETaskVisibility.EVERYONE", helper)
        self.assertIn("SCR_ETaskUIVisibility.LIST_ONLY", helper)
        self.assertIn("task.GetTaskName() != title", helper)
        join = INTEL.split("override void OnPlayerRegistered(int playerId)", 1)[1].split(
            "protected void INF_AssignObjective", 1)[0]
        for task in ("m_INFCacheTask", "m_INFCampTask", "m_INFZoneTask", "m_INFOfficerTask"):
            self.assertIn("if (" + task + ")", join)

    def test_switch_changes_recheck_delayed_victory(self):
        self.assertTrue(can_end((False, False, False), 3, 3, (0, 4), (0, 200)))
        self.assertFalse(can_end((True, False, False), 3, 3, (0, 4), (0, 200)))
        self.assertTrue(can_end((True, False, False), 3, 3, (4, 4), (0, 200)))
        self.assertFalse(can_end((True, True, False), 3, 3, (4, 4), (0, 200)))
        self.assertFalse(can_end((False, False, True), 3, 3, (4, 4), (200, 200), officers_cleared=False))
        setter = INTEL.split("void INF_SetObjectiveRequired(", 1)[1].split(
            "protected void INF_UpdateCampaignObjectives", 1)[0]
        self.assertIn("!Replication.IsServer()", setter)
        self.assertIn("Invalid mission objective", setter)
        self.assertIn("Replication.BumpMe()", setter)
        self.assertIn("Remove(TriggerMissionComplete)", setter)
        self.assertIn("m_bINFMissionEndQueued = false", setter)
        self.assertIn("INF_QueueCampaignObjectives()", setter)
        trigger = INTEL.split("override protected void TriggerMissionComplete()", 1)[1].split(
            "protected void INF_RefreshDebugMarkers", 1)[0]
        self.assertLess(trigger.index("INF_UpdateCampaignObjectives()"), trigger.index("!INF_CanCompleteMission()"))
        self.assertLess(trigger.index("!INF_CanCompleteMission()"), trigger.rindex("super.TriggerMissionComplete()"))
        self.assertIn("m_bINFMissionEnded = true", trigger)
        self.assertIn("CallLater(TriggerMissionComplete, 5000, false)", INTEL)

    def test_gm_checkboxes_preserve_inherited_options(self):
        config = (ROOT / "Configs/Editor/AttributeLists/Edit.conf").read_text()
        meta = (ROOT / "Configs/Editor/AttributeLists/Edit.conf.meta").read_text()
        inherited = "{F3D6C6D25642352C}Configs/Editor/AttributeLists/Edit.conf"
        self.assertIn('SCR_EditorAttributeList : "' + inherited + '"', config)
        self.assertIn(inherited, meta)
        self.assertNotIn("INF_MissionEndModeEditorAttribute", config)
        for name, guid, objective in (("INF_RequireCampsEditorAttribute", "{6AA19CB200000320}", 0),
                                      ("INF_RequireZonesEditorAttribute", "{6AA19CB200000322}", 1),
                                      ("INF_RequireOfficersEditorAttribute", "{6AA19CB200000324}", 2)):
            entry = config.split(name + ' "' + guid + '" {', 1)[1].split("m_Layout", 1)[0]
            self.assertIn("m_iObjective " + str(objective), entry)
            self.assertIn("class " + name + " : INF_MissionObjectiveEditorAttribute", POLICY)
        self.assertNotIn("AttributePrefab_Dropdown", config.split("INF_RequireCampsEditorAttribute", 1)[1].split("INF_Travel", 1)[0])
        self.assertIn("SCR_BaseEditorAttributeVar.CreateBool(intel.INF_IsObjectiveRequired(m_iObjective))", POLICY)
        self.assertIn("intel.INF_SetObjectiveRequired(m_iObjective, var.GetBool())", POLICY)

    def test_event_refresh_and_queued_end_cleanup(self):
        self.assertIn("CallLater(INF_UpdateCampaignObjectives, 0, false)", INTEL)
        self.assertNotIn("CallLater(INF_UpdateCampaignObjectives, 2000, true)", INTEL)
        end = INTEL.split("override void OnGameModeEnd", 1)[1].split("override void RegisterCache", 1)[0]
        self.assertIn("Remove(INF_UpdateCampaignObjectives)", end)
        self.assertIn("Remove(TriggerMissionComplete)", end)

    def test_tasks_visible_before_discovery_and_intel_recovery(self):
        update = INTEL.split("protected void INF_UpdateCampaignObjectives()", 1)[1].split(
            "protected SCR_Task INF_UpdateCaptureTask", 1)[0]
        self.assertNotIn("|| !m_bINFIntelLoaded", update)
        self.assertLess(update.index("INF_UpdateCacheObjective()"), update.index("if (!discoveryReady)"))
        self.assertIn('"Camps - initialising"', update)
        self.assertIn('"Zones - initialising"', update)
        self.assertIn('string title = "Caches - initialising"', INTEL)
        self.assertIn("if (m_bINFIntelLoaded)", INTEL)
        self.assertIn("m_bINFCaptureCountsReady = false", update)
        self.assertIn("!m_bINFIntelLoaded || !INF_CacheRosterComplete()", INTEL)


if __name__ == "__main__":
    unittest.main()
