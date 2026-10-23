"""FIA compatibility resource and dependency contracts; not engine execution."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
ADVANCED = ROOT.parent / "Insurgency Advanced"
FIA = ROOT.parent / "Insurgency FIA"
MODE = Path("Prefabs") / "MP" / "Modes" / "InsurgencyAdvanced" / "GameMode_InsurgencyAdvanced.et"
USSR_CAMP = "{8809D070C43CD78C}Prefabs/Compositions/Slotted/SlotFlatSmall/PlayerHub_S_USSR_01.et"
FIA_CAMP = "{B7C71CFC27B3EE24}Prefabs/Compositions/Slotted/SlotFlatSmall/PlayerHub_S_FIA_01.et"


class AdvancedFIAIntegration(unittest.TestCase):
    def test_depends_on_advanced_and_existing_fia_not_a_map(self):
        project = (ROOT / "addon.gproj").read_text()
        dependencies = set(re.findall(r'"([0-9A-F]{16})"', project.split("Dependencies", 1)[1]))
        expected = set()
        for addon in (ADVANCED, FIA):
            expected.add(re.search(r'GUID "([0-9A-F]{16})"', (addon / "addon.gproj").read_text()).group(1))
        self.assertEqual(dependencies, expected)
        self.assertNotIn("6A96C1C49DDE20B1", project)
        self.assertNotIn("61D57616CAFBB23D", project)

    def test_only_change_to_advanced_prefab_is_fia_camp(self):
        original = (ADVANCED / MODE).read_text()
        override = (ROOT / MODE).read_text()
        self.assertEqual(original.count(USSR_CAMP), 1)
        self.assertEqual(override.count(FIA_CAMP), 1)
        self.assertEqual(override.splitlines(),
                         original.replace(USSR_CAMP, FIA_CAMP).splitlines())
        self.assertNotIn(USSR_CAMP, override)

    def test_overrides_same_resource_identity_without_self_inheritance(self):
        meta_path = Path(str(MODE) + ".meta")
        self.assertEqual((ROOT / meta_path).read_text().splitlines(),
                         (ADVANCED / meta_path).read_text().splitlines())
        self.assertIn('Name "{CC46DF266B89439D}' + MODE.as_posix() + '"',
                      (ROOT / meta_path).read_text())
        self.assertNotIn('SCR_GameModeEditor : "{CC46DF266B89439D}', (ROOT / MODE).read_text())

    def test_does_not_duplicate_fia_pools_or_gameplay(self):
        self.assertFalse(any((ROOT / "Scripts").rglob("*.c")))
        self.assertFalse((ROOT / "Worlds").exists())
        self.assertFalse((ROOT / "Missions").exists())
        override = (ROOT / MODE).read_text()
        for field in ("m_aGroupPrefabs", "m_aSquadPrefabs", "m_aVehiclePrefabs", "m_sCachePrefab", "m_aFriendlyFactionsIds"):
            self.assertNotIn(field, override)
            original_fia = (FIA / "Prefabs" / "MP" / "Modes" / "Editor" / "GameMode_Editor_Full.et").read_text()
            self.assertIn(field, original_fia)

    def test_preserves_footprint_and_runtime_composition_safeguards(self):
        self.assertIn("m_fINFCampFootprintRadius 9", (ROOT / MODE).read_text())
        source = (ADVANCED / "Scripts" / "Game" / "INF_ForestCamps.c").read_text()
        self.assertIn("spawn.SetSpawnPointEnabled_S(false)", source)
        self.assertIn("SCR_AIGroup.Cast(entity)", source)
        self.assertIn("if (!PrepareComposition(child))", source)

    def test_fia_soldier_base_keeps_original_search_and_attaches_prisoner_actions(self):
        path = Path("Prefabs/Characters/Factions/INDFOR/FIA/Character_FIA_base.et")
        original = (FIA / path).read_text()
        prefab = (ROOT / path).read_text()
        self.assertEqual(prefab.splitlines()[0], original.splitlines()[0])
        self.assertEqual((ROOT / Path(str(path) + ".meta")).read_text().splitlines(),
                         (FIA / Path(str(path) + ".meta")).read_text().splitlines())
        for identity in ('ID "520EC961A090B1EE"',
                         'INS_BodySearchedComponent "{6A968899BFAA15FC}"',
                         'ActionsManagerComponent "{520EA1D2F659CE02}"',
                         'INS_BodySearchAction "{6A968899BFAA1A17}"',
                         'UIInfo UIInfo "{6A968899BFAA1A15}"'):
            self.assertIn(identity, original)
            self.assertIn(identity, prefab)
        for name in ("INS_BodySearchedComponent", "INF_PrisonerComponent",
                     "INS_BodySearchAction", "INF_CaptureAIAction", "INF_QuestionAIAction"):
            self.assertEqual(prefab.count(name + " "), 1)
        self.assertEqual(prefab.count('"LowerTorso"'), 3)
        body_search = prefab.split("INS_BodySearchAction ", 1)[1].split("INF_CaptureAIAction ", 1)[0]
        self.assertIn("Duration 1", body_search)
        self.assertIn("VisibilityRange 2", body_search)
        self.assertNotIn("FactionAffiliationComponent", prefab)


if __name__ == "__main__":
    unittest.main()
