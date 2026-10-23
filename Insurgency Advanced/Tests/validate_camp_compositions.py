"""Composition startup and editor configuration contracts; not engine compilation."""
import math
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
CAMP = (ROOT / "Scripts" / "Game" / "INF_ForestCamps.c").read_text()
GRID = (ROOT / "Scripts" / "Game" / "INF_BuildingGrid.c").read_text()
MODE = (ROOT / "Prefabs" / "MP" / "Modes" / "InsurgencyAdvanced" / "GameMode_InsurgencyAdvanced.et").read_text()
EDIT = (ROOT / "Configs" / "Editor" / "AttributeLists" / "Edit.conf").read_text()
INTEL = (ROOT / "Scripts" / "Game" / "INF_CacheObjective.c").read_text()
ENCAMPMENT = "{8809D070C43CD78C}Prefabs/Compositions/Slotted/SlotFlatSmall/PlayerHub_S_USSR_01.et"


class CampCompositions(unittest.TestCase):
    def test_manager_and_mission_select_the_actual_ussr_encampment(self):
        self.assertIn(ENCAMPMENT, GRID)
        self.assertIn('m_sINFCampComposition "' + ENCAMPMENT + '"', MODE)
        self.assertIn('UIWidgets.ResourceNamePicker', GRID)
        self.assertIn('category: "Forest Camps"', GRID)
        self.assertIn('m_fINFCampFootprintRadius 9', MODE)
        self.assertIn("m_Manager.INF_GetCampComposition()", CAMP)
        self.assertIn("m_Manager.INF_GetCampFootprintRadius()", CAMP)
        self.assertIn("ConfigureSite(restored)", CAMP)
        self.assertIn("ConfigureSite(site)", CAMP)

    def test_one_complete_composition_is_spawned_at_the_site_not_individual_tents(self):
        spawn = CAMP.split("static bool SpawnLayout", 1)[1].split("static bool PrepareComposition", 1)[0]
        self.assertIn("Resource.Load(site.m_Composition)", spawn)
        self.assertEqual(spawn.count("SpawnEntityPrefab("), 1)
        self.assertIn("params.Transform[3] = site.m_Position", spawn)
        self.assertIn("Vector(site.m_Yaw, 0, 0)", spawn)
        self.assertNotIn("foreach", spawn)
        self.assertNotIn("INF_CampTent.et", CAMP)
        self.assertFalse((ROOT / "Prefabs" / "ForestCamps" / "INF_CampTent.et").exists())
        self.assertFalse((ROOT / "Prefabs" / "ForestCamps" / "INF_CampTent.et.meta").exists())
        self.assertIn("site.m_Props.Insert(prop)", spawn)
        self.assertIn("if (!PrepareComposition(prop))", spawn)
        self.assertIn("Cleanup(site)", spawn)

    def test_composition_enables_authored_faction_spawning_without_unbudgeted_ai(self):
        prepare = CAMP.split("static bool PrepareComposition", 1)[1].split("static INF_ForestCampSearchComponent FindArsenal", 1)[0]
        self.assertIn("SCR_AIGroup.Cast(entity)", prepare)
        self.assertIn("LogLevel.ERROR", prepare)
        self.assertIn("return false", prepare)
        self.assertIn("spawn.SetSpawnPointEnabled_S(true)", prepare)
        self.assertNotIn("SetFaction(", prepare)
        self.assertIn("persistence.StopTracking(entity, true)", prepare)
        self.assertIn("if (!PrepareComposition(child))", prepare)
        self.assertIn("child.GetSibling()", prepare)

    def test_footprint_samples_cover_configured_extents_without_gaps(self):
        for radius in (3, 7.5, 9, 10, 24):
            steps = math.ceil(radius / 3)
            spacing = radius / steps
            samples = [i * spacing for i in range(-steps, steps + 1)]
            self.assertAlmostEqual(samples[0], -radius)
            self.assertAlmostEqual(samples[-1], radius)
            self.assertLessEqual(spacing, 3)
            self.assertIn(0, samples)
        self.assertEqual((2 * math.ceil(9 / 3) + 1) ** 2, 49)
        footprint = CAMP.split("static int FootprintFailure", 1)[1].split("static bool ClearVolume", 1)[0]
        self.assertIn("Math.Ceil(site.m_FootprintRadius / 3.0)", footprint)
        self.assertIn("footprints.Insert(sample)", footprint)
        self.assertIn("foreach (vector point : footprints)", footprint)
        self.assertIn("ClearVolume(point, 2.5, 3, site, true)", footprint)
        self.assertIn("radius < 3 || radius > 24", CAMP)
        self.assertIn("!composition.IsValid()", CAMP)

    def test_gm_attributes_are_siblings_not_nested_inside_another_control(self):
        depth = 0
        attributes = []
        for line in EDIT.splitlines():
            match = re.match(r'\s*(\w+EditorAttribute)\s+"([^"]+)"\s*\{', line)
            if match:
                self.assertEqual(depth, 2, match.group(1))
                entry = (match.group(1), match.group(2))
                self.assertNotIn(entry, attributes)
                attributes.append(entry)
            clean = re.sub(r'"(?:\\.|[^"\\])*"', '""', line)
            depth += clean.count("{") - clean.count("}")
            self.assertGreaterEqual(depth, 0)
        self.assertEqual(depth, 0)
        expected = {
            "INF_QuestioningChanceEditorAttribute": "{6AA19CB200000311}",
            "INS_PatrolsEnabledEditorAttribute": "{494E530000000001}",
            "INS_ZoneCooldownEditorAttribute": "{494E530000000028}",
            "INS_MaxConcurrentCounterAttacksEditorAttribute": "{494E530000000010}",
            "INS_CounterAttackIntervalEditorAttribute": "{494E530000000014}",
            "INF_SiteStrengthEditorAttribute": "{6AA19CB200000305}",
            "INF_CounterAttackChanceEditorAttribute": "{6AA19CB200000307}",
            "INF_DebugCampMarkersEditorAttribute": "{6AA19CB200000303}",
            "INF_MissionEndModeEditorAttribute": "{6AA19CB200000301}",
            "INS_DebugCacheMarkersEditorAttribute": "{494E53000000001C}",
        }
        travel = {("INF_TravelSettingEditorAttribute", guid) for guid in (
            "{6AA19CB200000309}", "{6AA19CB20000030B}",
            "{6AA19CB20000030D}", "{6AA19CB20000030F}")}
        vehicles = {("INF_ZoneVehicleSettingEditorAttribute", guid) for guid in (
            "{6AA19CB200000610}", "{6AA19CB200000612}")}
        self.assertEqual(set(attributes), set(expected.items()) | travel | vehicles)

    def test_custom_runtime_settings_also_have_workbench_attributes(self):
        for source, field in ((GRID, "m_iINFSiteStrengthMultiplier"),
                              (GRID, "m_iINFChance"),
                              (INTEL, "m_iINFMissionEndMode"),
                              (GRID, "m_fINFTravelEnterSpeed"),
                              (GRID, "m_fINFTravelExitSpeed"),
                              (GRID, "m_fINFTravelEnterSeconds"),
                              (GRID, "m_fINFTravelExitSeconds")):
            declaration = re.search(
                r'(\[Attribute\([^\n]+\), RplProp\([^\n)]*\)\]\s*protected\s+\w+\s+' +
                field + r'\b)', source)
            self.assertIsNotNone(declaration, field)

    def test_script_annotations_use_one_combined_bracket_block(self):
        for path in (ROOT / "Scripts").rglob("*.c"):
            source = path.read_text()
            self.assertNotRegex(source, r'\]\s*\[\s*(?:Attribute|RplProp)\s*\(', str(path))


if __name__ == "__main__":
    unittest.main()
