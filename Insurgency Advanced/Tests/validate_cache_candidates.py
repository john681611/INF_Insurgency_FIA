"""Cache candidate source contracts; not engine execution."""
from pathlib import Path
import unittest
from source_contracts import method_body

SCRIPTS = Path(__file__).resolve().parents[1] / "Scripts" / "Game"
CANDIDATES = (SCRIPTS / "INF_CacheCandidates.c").read_text()
REWARDS = (SCRIPTS / "INF_ForestCampRewards.c").read_text()
GRID = (SCRIPTS / "INF_BuildingGrid.c").read_text()


class CacheCandidates(unittest.TestCase):
    def test_generated_spots_are_a_layer_the_base_mod_reads_not_custom_config(self):
        self.assertNotIn("INF_GetCacheCandidates", REWARDS + GRID)
        self.assertNotIn("INF_CacheCandidateList", CANDIDATES)
        body = method_body(CANDIDATES, "protected void Finish(")
        self.assertIn("Prefabs/INS_CacheSpawnPoint.et", body)
        self.assertIn("Caches_Generated.layer", CANDIDATES)
        self.assertIn("coords %1 %2 %3", body)

    def test_export_is_off_by_default_and_server_only(self):
        self.assertIn('[Attribute("0", UIWidgets.CheckBox, "Development only.', GRID)
        self.assertIn("m_bINFExportCacheCandidates && Replication.IsServer()", GRID)

    def test_exporter_copies_each_anchor_into_same_type_buildings_on_a_floor(self):
        body = method_body(CANDIDATES, "protected bool CollectBuilding(")
        self.assertIn("anchor.m_sKey == key", body)
        self.assertIn("foreach (INF_CacheAnchor chosen : matches)", body)
        self.assertIn("entity.CoordToParent(chosen.m_vLocal)", body)
        self.assertIn("OnFloor(world)", body)
        self.assertIn("NearFixed(world)", body)

    def test_each_type_is_capped_by_a_random_sample_as_the_final_step(self):
        self.assertIn("static const int MAX_PER_TYPE = 30;", CANDIDATES)
        finish = method_body(CANDIDATES, "protected void Finish(")
        self.assertLess(finish.index("CapPerType()"), finish.index("lines.Insert"))
        cap = method_body(CANDIDATES, "protected void CapPerType(")
        self.assertIn("Math.RandomInt(0, j + 1)", cap)
        self.assertIn("Math.Min(available, MAX_PER_TYPE)", cap)

    def test_exporter_writes_cache_spawn_point_entities(self):
        body = method_body(CANDIDATES, "protected void Finish(")
        for text in ("SCR_EditableEntityComponent", "Generated_%1", "angles 0 %1 0"):
            self.assertIn(text, body)


if __name__ == "__main__":
    unittest.main()
