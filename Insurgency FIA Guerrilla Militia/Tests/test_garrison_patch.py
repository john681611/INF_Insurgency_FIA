import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / "Prefabs" / "MP" / "Modes" / "Editor" / "GameMode_Editor_Full.et"


class GarrisonPatchTests(unittest.TestCase):
    def test_dependencies(self):
        project = (ROOT / "addon.gproj").read_text()
        self.assertIn('"6A9686E2AB28E681"', project)
        self.assertIn('"6AA910478FB3EBEB"', project)
        self.assertIn('TITLE "Insurgency FIA Guerrilla Militia"', project)

    def test_patrol_and_legacy_garrison_infantry_are_appended(self):
        text = PATCH.read_text()
        self.assertEqual(re.findall(r'\bINS_\w+', text),
                         ["INS_AmbientPatrolManager", "INS_GarrisonManager"])
        self.assertEqual(re.findall(r'\bm_a\w+', text),
                         ["m_aGroupPrefabs", "m_aInfantryGroupPrefabs"])
        self.assertIn("m_aGroupPrefabs + {", text)
        self.assertIn("m_aInfantryGroupPrefabs + {", text)
        refs = re.findall(r'"\{(A864CDF02E7A110[234])\}([^"]+)"', text)
        self.assertEqual(len(refs), 4)
        self.assertEqual({guid for guid, _ in refs},
                         {"A864CDF02E7A1102", "A864CDF02E7A1103", "A864CDF02E7A1104"})
        militia = ROOT.parent / "FIA Extended Militia"
        for guid, relative in refs:
            path = militia.joinpath(*relative.split("/"))
            self.assertTrue(path.is_file())
            self.assertIn("{" + guid + "}" + relative, Path(str(path) + ".meta").read_text())

    def test_patrol_groups_fit_existing_allocation_selection_without_new_weights(self):
        text = PATCH.read_text().split("INS_GarrisonManager", 1)[0]
        self.assertNotIn("Group_FIA_Militia_Pair.et", text)
        sizes = []
        for relative in re.findall(r'"\{A864CDF02E7A110[34]\}([^"]+)"', text):
            source = (ROOT.parent / "FIA Extended Militia").joinpath(*relative.split("/")).read_text()
            sizes.append(source.count('"{A864CDF02E7A1101}Prefabs/Characters/'))
        self.assertEqual(sizes, [4, 8])
        for required in (4, 6):
            exact = [size for size in sizes if size == required]
            fallback = [size for size in sizes if size > required]
            self.assertTrue(exact or fallback)
        self.assertNotIn("m_aSquadPrefabs", PATCH.read_text())

    def test_override_matches_insurgency_fia_resource(self):
        base = ROOT.parent / "Insurgency FIA" / PATCH.relative_to(ROOT)
        self.assertEqual(
            re.search(r'Name "([^"]+)"', Path(str(PATCH) + ".meta").read_text()).group(1),
            re.search(r'Name "([^"]+)"', Path(str(base) + ".meta").read_text()).group(1),
        )
        self.assertIn('INS_GarrisonManager "{68B2B72E61E88AD0}"', base.read_text())


if __name__ == "__main__":
    unittest.main()
