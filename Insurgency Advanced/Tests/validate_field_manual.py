"""Offline Field Manual registration and navigation checks; not UI execution."""
from pathlib import Path
import re
import unittest

from source_contracts import method_body

ROOT = Path(__file__).resolve().parents[1]
MANUAL_PATH = ROOT / "Configs" / "FieldManual" / "FieldManualConfigRoot.conf"
MANUAL = MANUAL_PATH.read_text()
MENU = (ROOT / "Scripts" / "Game" / "INF_FieldManualUI.c").read_text()


class FieldManual(unittest.TestCase):
    def test_manual_is_registered_as_the_shared_native_resource(self):
        self.assertRegex(MANUAL, r"^SCR_FieldManualConfigRoot\b")
        self.assertIn(
            'Name "{17295EF80DC38D53}Configs/FieldManual/FieldManualConfigRoot.conf"',
            MANUAL_PATH.with_suffix(".conf.meta").read_text(),
        )

    def test_categories_and_documents_have_a_navigable_native_hierarchy(self):
        depth = 0
        sections = subcategories = pages = 0
        ids = []
        for line in MANUAL.splitlines():
            if "SCR_FieldManualConfigCategory " in line:
                self.assertIn(depth, (2, 4), "Categories must be sections or subcategories")
                sections += depth == 2
                subcategories += depth == 4
            if "m_aEntries {" in line:
                self.assertEqual(depth, 5, "Documents must belong to a selectable subcategory")
            if "SCR_FieldManualConfigEntry_Standard " in line:
                self.assertEqual(depth, 6)
                pages += 1
                opening = MANUAL.index(line) + line.rfind("{")
                page = method_body(MANUAL[opening:], "{")
                self.assertRegex(page, r'm_sTitle "[^"]+"')
                self.assertRegex(page, r'm_sText "[^"]+"')
            identifier = re.search(r'\w+ "\{([0-9A-F]{16})\}"\s*(?::[^{]+)?\s*\{', line)
            if identifier:
                ids.append(identifier.group(1))
            clean = re.sub(r'"(?:\\.|[^"\\])*"', '""', line)
            depth += clean.count("{") - clean.count("}")
            self.assertGreaterEqual(depth, 0)
        self.assertEqual(depth, 0)
        self.assertGreater(sections, 0)
        self.assertGreater(subcategories, 0)
        self.assertGreater(pages, 0)
        self.assertEqual(len(ids), len(set(ids)), "Manual objects must have distinct IDs")

    def test_advanced_category_is_available_to_the_native_menu(self):
        self.assertIn('m_sTitle "Insurgency Advanced"', MANUAL)
        self.assertIn('category.m_sTitle != "Insurgency Advanced"', MENU)
        self.assertIn("override protected void CreateCategoryMenuWidgets()", MENU)
        self.assertIn("m_ConfigRoot.m_aCategories.InsertAt(category, 0)", MENU)
        self.assertLess(MENU.index("InsertAt(category, 0)"),
                        MENU.index("super.CreateCategoryMenuWidgets()"))
        self.assertNotIn("m_ConfigRoot.m_aCategories.Clear()", MENU)


if __name__ == "__main__":
    unittest.main()
