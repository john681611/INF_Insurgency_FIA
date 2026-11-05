"""Contracts for keeping hostile AI from spawning around friendly spawn points."""
from pathlib import Path
import unittest

from source_contracts import method_body

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "Scripts" / "Game"
GRID = (SCRIPTS / "INF_BuildingGrid.c").read_text()
VEHICLES = (SCRIPTS / "INF_ZoneVehicles.c").read_text()


class SpawnProtection(unittest.TestCase):
    def test_only_enabled_friendly_non_personal_points_protect(self):
        body = method_body(GRID, "static bool Contains(vector pos)")
        self.assertIn("SCR_SpawnPoint.GetSpawnPoints()", body)
        self.assertIn("IsBase(point, factionKey)", body)
        self.assertIn("RADIUS * RADIUS", body)
        base = method_body(GRID, "static bool IsBase(")
        self.assertIn("point.GetFactionKey() == factionKey", base)
        self.assertIn("IsSpawnPointEnabled()", base)
        self.assertIn("SCR_PlayerSpawnPoint.Cast(point)", base)

    def test_every_hostile_spawn_path_checks_protection(self):
        blocked = method_body(GRID, "static bool IsSpawnBlocked(")
        self.assertIn("INF_SpawnProtection.Contains(pos)", blocked)
        self.assertIn("IsExcluded(pos)", blocked)
        self.assertIn("INF_SpawnProtection.Contains(empty)", method_body(GRID, "protected bool INF_CellSpawnValid("))
        self.assertIn("INF_CellSpawnValid(", method_body(GRID, "protected vector INF_FindNeighbourCellPosition("))
        allowed = method_body(GRID, "static bool PositionAllowed(")
        self.assertIn("IsSpawnBlocked(pos)", allowed)
        self.assertIn("PositionAllowed(empty", method_body(GRID, "protected vector INF_FindReinforcementPosition("))
        self.assertIn("IsSpawnBlocked(pos)", method_body(GRID, "protected bool INF_GridAttackPositionSafe("))
        self.assertIn("PositionAllowed(point", method_body(VEHICLES, "protected bool INF_ZoneOpenPosition("))
        self.assertIn("PositionAllowed(point", method_body(VEHICLES, "protected bool INF_ZoneRoadPosition("))


if __name__ == "__main__":
    unittest.main()
