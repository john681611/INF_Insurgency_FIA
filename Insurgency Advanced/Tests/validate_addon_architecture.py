"""Standalone Advanced resource ownership and opt-in lifecycle contracts."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "Scripts" / "Game"
MODE = ROOT / "Prefabs" / "MP" / "Modes" / "InsurgencyAdvanced" / "GameMode_InsurgencyAdvanced.et"


class AdvancedArchitecture(unittest.TestCase):
    def test_core_has_only_upstream_dependencies_and_no_terrain(self):
        project = (ROOT / "addon.gproj").read_text()
        dependencies = project.split("Dependencies", 1)[1]
        self.assertEqual(set(re.findall(r'"([0-9A-F]{16})"', dependencies)),
                         {"E55A9FA78E5FCFE4", "58D0FB3206B6F859"})
        self.assertNotIn("6A96C1C49DDE20B1", project)
        self.assertFalse((ROOT / "Worlds").exists())
        self.assertFalse((ROOT / "Missions").exists())
        for path in SCRIPTS.glob("*.c"):
            self.assertNotRegex(path.read_text(), r"InsEden|Worlds/|WCS_\w+")

    def test_opt_in_prefab_does_not_override_stock_mode_or_faction_pools(self):
        prefab = MODE.read_text()
        self.assertTrue(prefab.startswith(
            'SCR_GameModeEditor : "{DA3986AF1B5032B8}Prefabs/MP/Modes/Editor/GameMode_Editor_Full.et"'))
        for setting in ("m_bINFBuildingGrid 1", "m_bINFShowZoneAreas 1", "m_bINFCacheObjective 1"):
            self.assertIn(setting, prefab)
        for setting in ("m_aGroupPrefabs", "m_aSquadPrefabs", "m_aVehiclePrefabs", "coords", "m_Welcome"):
            self.assertNotIn(setting, prefab)
        self.assertFalse((ROOT / "Prefabs" / "MP" / "Modes" / "Editor" / "GameMode_Editor_Full.et").exists())

    def test_resource_guids_survive_the_move(self):
        expected = {
            "Configs/Editor/AttributeLists/Edit.conf": "F3D6C6D25642352C",
            "Configs/Map/INF_MapEditor.conf": "6AA19CB200000200",
            "Prefabs/Markers/INF_GarrisonMapArea.et": "6AA19CB200000002",
            "Prefabs/Props/INF_ForestCampDocuments.et": "6AA19CB200000100",
            "Prefabs/MP/Modes/InsurgencyAdvanced/GameMode_InsurgencyAdvanced.et": "CC46DF266B89439D",
        }
        for path, guid in expected.items():
            self.assertTrue((ROOT / path).is_file(), path)
            self.assertIn('Name "{' + guid + "}" + path + '"',
                          (ROOT / (path + ".meta")).read_text())
        rewards = (SCRIPTS / "INF_ForestCampRewards.c").read_text()
        for path in ("$profile:INS_Saves/forest_camp_sites.dat",
                     "$profile:INS_Saves/forest_camp_searches.dat"):
            self.assertIn(path, rewards)

    def test_non_grid_lifecycle_delegates_to_upstream_before_advanced_work(self):
        garrison = (SCRIPTS / "INF_GarrisonMapAreas.c").read_text()
        for signature, upstream in (
            ("override void OnGameModeStart()", "super.OnGameModeStart()"),
            ("override void OnGameModeEnd(SCR_GameModeEndData data)", "super.OnGameModeEnd(data)"),
            ("override void SetEnabled(bool bEnabled)", "super.SetEnabled(bEnabled)"),
        ):
            body = garrison.split(signature, 1)[1].split("\n\t}", 1)[0]
            self.assertIn("if (!INF_UsesBlockControl())", body)
            self.assertLess(body.index(upstream), body.index("return;"))
        caches = (SCRIPTS / "INF_ForestCampRewards.c").read_text().split(
            "override protected void SpawnCaches()", 1)[1]
        self.assertLess(caches.index("super.SpawnCaches()"),
                        caches.index("m_bINFCacheSelectionStarted"))
        intel = (SCRIPTS / "INF_CacheObjective.c").read_text()
        for method in ("TriggerMissionComplete", "BroadcastDebugMarkers", "LoadIntelState"):
            body = intel.split("override protected void " + method + "()", 1)[1].split("\n\t}", 1)[0]
            self.assertIn("if (!INF_ZoneEditorContext.Grid(GetOwner()))", body)
            delegation = body.split("if (!INF_ZoneEditorContext.Grid(GetOwner()))", 1)[1]
            self.assertLess(delegation.index("super." + method + "()"), delegation.index("return;"))

    def test_no_duplicate_class_definitions_or_rank_bypasses(self):
        definitions = []
        for path in SCRIPTS.glob("*.c"):
            source = path.read_text()
            definitions.extend(re.findall(r"(?m)^(?:modded\s+)?class\s+(\w+)", source))
            self.assertNotRegex(source, r"modded class SCR_(?:BudgetEditorComponent|EntityCatalogSpawnerData|CampaignBuildingProviderComponent)\b")
        self.assertEqual(len(definitions), len(set(definitions)))


if __name__ == "__main__":
    unittest.main()
