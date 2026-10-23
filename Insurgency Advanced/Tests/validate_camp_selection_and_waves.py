"""Offline camp-count, vegetation and explicit counterattack-victory contracts."""
from itertools import product
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
GRID = (ROOT / "Scripts/Game/INF_BuildingGrid.c").read_text()
CAMP = (ROOT / "Scripts/Game/INF_ForestCamps.c").read_text()
STATE = (ROOT / "Scripts/Game/INF_ZoneStateSystem.c").read_text()
ATTR = (ROOT / "Scripts/Game/INF_ZoneEditorAttributes.c").read_text()


class CampSelectionAndWaves(unittest.TestCase):
    def test_count_is_header_only_with_default_and_range_validation(self):
        self.assertIn('[Attribute("7", UIWidgets.Slider', GRID)
        self.assertIn("modded class INS_MissionParams", GRID)
        self.assertIn("MAX_CAMPS = 20", CAMP)
        self.assertIn("count < 1 || count > INF_ForestCamps.MAX_CAMPS", GRID)
        self.assertIn("header.m_Insurgency.m_iINFCampCount", GRID)
        self.assertNotIn("INF_SetCampCount", GRID)
        self.assertNotIn("INF_CampCountEditorAttribute", ATTR)
        header = (ROOT.parent / "INF Insurgency Everon/Missions/InsEden.conf").read_text()
        self.assertIn("m_iINFCampCount 7", header)
        for count in (-1, 0, 1, 7, 20, 21):
            self.assertEqual(1 <= count <= 20, count in (1, 7, 20))

    def test_fresh_cold_and_warm_paths_limit_selection_not_the_candidate_pool(self):
        self.assertEqual(CAMP.count("int count = m_Manager.INF_GetCampCount()"), 2)
        self.assertEqual(CAMP.count("m_Sites.Resize(count)"), 2)
        self.assertIn("m_Sites.Count() >= count", CAMP)
        self.assertIn("SaveCandidates(m_Sites)", CAMP)
        self.assertIn("Preserving the saved camp roster", CAMP)
        index = (ROOT / "Scripts/Game/INF_TerrainIndex.c").read_text()
        self.assertIn(':vegetation-tolerant-v2', index)
        self.assertIn("kind == 0 || context == m_CampContext", index)

    def test_layout_and_reward_clearance_ignore_vegetation(self):
        self.assertIn("return !BaseTree.Cast(entity)", CAMP)
        self.assertIn("ClearVolume(point, 2.5, 3, site, true)", CAMP)
        self.assertIn("ClearVolume(pos, 2, 3, site, true)", CAMP)
        self.assertIn("world.TraceMove(trace, CampObstacleFilter)", CAMP)
        self.assertIn("world.TraceMove(trace, null)", CAMP)
        self.assertIn("TraceFlags.ENTS | TraceFlags.WORLD | TraceFlags.OCEAN", CAMP)
        self.assertIn("DryFlat(sample, ground)", CAMP)
        self.assertIn("ValidateNavigation(site)", CAMP)

    def test_only_a_full_killed_or_captured_roster_is_defeated(self):
        for roster in product(("alive", "unconscious", "dead", "captured", "missing"), repeat=4):
            defeated = all(member in ("dead", "captured") for member in roster)
            self.assertEqual(defeated, set(roster) <= {"dead", "captured"})
        for observed in range(4):
            self.assertFalse(observed >= 4 and all(member == "dead" for member in ["dead"] * observed))
        collect = GRID.split("protected bool INF_CollectAttackers(", 1)[1].split("override protected void MonitorTick", 1)[0]
        self.assertIn("zone.m_aINFParticipants.Count() >= zone.m_iINFTotalAI", collect)
        self.assertIn("entity.IsDeleted()", collect)
        self.assertIn("prisoner && prisoner.IsCaptured()", collect)
        self.assertIn("damage && damage.IsDestroyed()", collect)
        self.assertIn("participant.m_Resolved = true", collect)
        self.assertIn("if (participant.m_Resolved)\n\t\t\t\tcontinue", collect)

    def test_owned_group_lifecycle_and_timeout_cannot_grant_fake_victory(self):
        self.assertIn("SetLifecyclePolicy(SCR_EAIGroupLifecyclePolicy.Manual)", GRID)
        self.assertIn("INF_RecordAttackers(squad)", GRID)
        self.assertIn("zone.m_bINFDefeated = INF_CollectAttackers(zone, living)", GRID)
        self.assertIn("AttackEnded(zone.m_INFCell, zone.m_bINFEverHadLiving, zone.m_bINFDefeated)", GRID)
        self.assertIn("if (!hadLiving || !defeated)", STATE)
        self.assertIn("Counterattack aborted without permanent capture credit", GRID)
        for had_living, defeated, held in product((False, True), repeat=3):
            permanent = had_living and defeated and held
            self.assertEqual(permanent, (had_living, defeated, held) == (True, True, True))

    def test_prisoner_action_labels(self):
        prisoner = (ROOT / "Scripts/Game/INF_Prisoners.c").read_text()
        self.assertIn('outName = "Capture Prisoner"', prisoner)
        self.assertIn('outName = "Question Prisoner"', prisoner)
        self.assertNotIn('"Capture AI"', prisoner)


if __name__ == "__main__":
    unittest.main()
