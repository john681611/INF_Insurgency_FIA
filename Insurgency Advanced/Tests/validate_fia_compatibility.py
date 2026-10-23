"""Configured-faction selection and fixed-size budget regression contracts."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "Scripts" / "Game" / "INF_BuildingGrid.c").read_text()


def candidates(pool, members):
    exact = [name for name, roster in pool if len(roster) == members]
    larger = [name for name, roster in pool if len(roster) > members]
    return exact or larger


class FIACompatibility(unittest.TestCase):
    def test_both_spawn_paths_use_their_own_configured_pools(self):
        self.assertIn("INF_BuildingGrid.Spawn(pos, members, m_aGroupPrefabs)", SOURCE)
        self.assertIn("INF_BuildingGrid.Spawn(positions[group], selected.m_aINFPlan[group], m_aSquadPrefabs)", SOURCE)
        self.assertNotIn("Prefabs/Groups/OPFOR/", SOURCE)
        self.assertNotIn("Group_USSR_", SOURCE)

    def test_exact_size_preferred_and_larger_rosters_can_fill_six(self):
        pool = [("FIA recon", ["scout", "rto"]),
                ("FIA light", ["rifleman"] * 4),
                ("FIA squad", ["leader", "mg", "medic"] + ["rifleman"] * 5)]
        self.assertEqual(candidates(pool, 4), ["FIA light"])
        self.assertEqual(candidates(pool, 6), ["FIA squad"])
        six = pool[2][1][:6]
        self.assertEqual(len(six), 6)
        self.assertEqual(six[:3], ["leader", "mg", "medic"])
        for size in (4, 6):
            self.assertLessEqual(len(pool[2][1][:size]), size)
        self.assertIn('source.Get("m_aUnitPrefabSlots", units)', SOURCE)
        self.assertIn("group.m_aUnitPrefabSlots.Resize(members)", SOURCE)
        self.assertIn("group.SetMaxUnitsToSpawn(members)", SOURCE)

    def test_empty_and_undersized_pools_never_revert_to_soviet_groups(self):
        self.assertEqual(candidates([], 4), [])
        self.assertEqual(candidates([("FIA recon", ["scout", "rto"])], 4), [])
        self.assertIn("!configuredPrefabs || configuredPrefabs.IsEmpty()", SOURCE)
        self.assertIn("refusing an unconfigured faction fallback", SOURCE)
        self.assertIn("group.GetAgentsCount() > members", SOURCE)
        spawn = SOURCE.split("static IEntity Spawn(", 1)[1].split(
            "modded class INS_AmbientPatrolZoneState", 1)[0]
        self.assertIn("SCR_EntityHelper.DeleteEntityAndChildren(entity)", spawn)
        self.assertIn("LogLevel.ERROR", spawn)

    def test_other_fia_integration_surfaces_are_not_overridden_by_mission(self):
        prefab = (ROOT / "Prefabs" / "MP" / "Modes" / "InsurgencyAdvanced" / "GameMode_InsurgencyAdvanced.et").read_text()
        self.assertNotIn("m_aVehiclePrefabs", prefab)
        self.assertNotIn("m_aGroupPrefabs", prefab)
        self.assertNotIn("m_aSquadPrefabs", prefab)
        self.assertNotIn("m_sCachePrefab", prefab)
        self.assertNotIn("m_aInfantryGroupPrefabs", SOURCE)


if __name__ == "__main__":
    unittest.main()
