"""Contracts for retiring authored garrisons without losing the map renderer."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "Scripts" / "Game"
GRID = (SCRIPTS / "INF_BuildingGrid.c").read_text()
MAP = (SCRIPTS / "INF_GarrisonMapAreas.c").read_text()
PRESENCE = (SCRIPTS / "INF_BlockControl.c").read_text()
INTEL = (SCRIPTS / "INF_CacheObjective.c").read_text()


class SingleDefenderSystem(unittest.TestCase):
    def test_inherited_garrison_component_is_map_only(self):
        start = MAP.split("override void OnGameModeStart()", 1)[1].split(
            "protected INF_GarrisonMapArea INF_CreateZoneArea", 1)[0]
        self.assertIn("m_bEnabled = false", start)
        self.assertIn("CallLater(INF_UpdateZoneAreas", start)
        self.assertIn("if (!INF_UsesBlockControl())", start)
        self.assertLess(start.index("super.OnGameModeStart()"), start.index("m_bEnabled = false"))
        self.assertNotIn("m_aZoneStates", MAP)
        self.assertIn("super.OnGameModeEnd(data)", MAP)
        enable = MAP.split("override void SetEnabled", 1)[1]
        self.assertIn("m_bEnabled = false", enable)
        self.assertIn("LogLevel.WARNING", enable)
        self.assertLess(enable.index("super.SetEnabled(bEnabled)"), enable.index("m_bEnabled = false"))
        for old in ("SpawnZone(", "SpawnVehicleAt(", "SaveClearedZones(", "TickProximityCheck("):
            self.assertNotIn(old, MAP)

    def test_safe_zones_remain_but_garrison_footprints_do_not_exclude(self):
        exclusion = GRID.split("static bool IsExcluded", 1)[1].split("static bool Overlaps", 1)[0]
        self.assertIn("INS_SafeZone.CollectZones(safeZones)", exclusion)
        self.assertNotIn("INS_GarrisonZone", GRID)
        self.assertNotIn("CollectGarrisonCells", GRID)
        self.assertNotIn("m_bINFGarrisonCell", GRID)
        self.assertNotIn("INF_ApplyPermanentGarrisonClearance", GRID)

    def test_only_grid_and_camps_supply_zone_objectives_and_quota(self):
        self.assertNotIn("INS_GarrisonManager", INTEL)
        self.assertNotIn("INS_GarrisonManager", PRESENCE)
        self.assertNotIn("modded class INS_ZoneState", PRESENCE)
        self.assertIn("patrols.INF_CountActivationZones(this)", PRESENCE)
        self.assertIn("patrols.INF_CollectCaptureObjectives(zones, camps)", INTEL)
        self.assertNotIn("addingGarrison", GRID)

    def test_counterattacks_include_all_grid_zone_types(self):
        targets = GRID.split("protected INS_CounterAttackZone INF_GetGridTarget(", 1)[1].split(
            '[Attribute("10"', 1)[0]
        self.assertNotIn("if (cell.m_INFCamp)", targets)
        self.assertNotIn("Garrison", targets)
        self.assertIn("m_aZones.Insert(target)", targets)

    def test_obsolete_gm_controls_are_unavailable(self):
        for attribute in ("INS_GarrisonEnabledEditorAttribute", "INS_GarrisonCacheGroupsEditorAttribute"):
            body = MAP.split("modded class " + attribute, 1)[1].split("\n}", 1)[0]
            self.assertIn("ReadVariable", body)
            self.assertIn("return null", body)

    def test_five_state_grid_renderer_still_uses_grid_records(self):
        self.assertIn("zones.GetOnChanged().Insert(INF_QueueZoneArea)", MAP)
        self.assertIn("foreach (INS_AmbientPatrolZoneState state : zones.GetZones())", MAP)
        self.assertIn("area.INF_SetZoneState(state.m_vCenter, INF_BuildingGrid.HALF, appearance, true, state.m_iINFTargetAI)", MAP)
        for state in ("DORMANT", "ACTIVE", "CAPTURING", "CLEARED", "COUNTERATTACK"):
            self.assertIn("case INF_EGarrisonMapState." + state, MAP)


if __name__ == "__main__":
    unittest.main()
