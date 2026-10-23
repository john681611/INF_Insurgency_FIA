"""Offline restart models and source contracts; requires in-engine validation too."""
from dataclasses import dataclass, field
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "Scripts" / "Game"
STORE = (SCRIPTS / "INF_TerritoryPersistence.c").read_text()
CORE = (SCRIPTS / "INF_ZoneStateSystem.c").read_text()
GRID = (SCRIPTS / "INF_BuildingGrid.c").read_text()


@dataclass
class Snapshot:
    generation: int
    cells: dict = field(default_factory=dict)
    world: str = "Everon"
    complete: bool = True


def load(slots, world="Everon"):
    valid = [s for s in slots if s is not None and s.complete and
             s.world == world and s.generation > 0]
    if valid:
        return max(valid, key=lambda s: s.generation)
    if any(s is not None for s in slots):
        raise ValueError("startup blocked")
    return Snapshot(0)


def record(cells, key, permanent, credit, camp="", friendly=False):
    if permanent and credit and (not camp or friendly):
        cells[key] = camp


class TerritoryPersistence(unittest.TestCase):
    def test_fresh_profile_and_newest_complete_generation(self):
        self.assertEqual(load([None, None]).cells, {})
        old = Snapshot(1, {"1:2": ""})
        new = Snapshot(2, {"1:2": "", "-3:4": "camp|100:200"})
        self.assertIs(load([old, new]), new)
        self.assertEqual(load([new, old]).cells, new.cells)

    def test_interrupted_write_keeps_previous_progress_and_can_retry(self):
        old = Snapshot(5, {"1:2": ""})
        failed = Snapshot(6, {"1:2": "", "2:3": ""}, complete=False)
        self.assertIs(load([old, failed]), old)
        failed.complete = True
        self.assertIs(load([old, failed]), failed)
        self.assertIn("ReadSlot(slot)", STORE.split("void Flush()", 1)[1])
        self.assertIn("m_Slot == 0", STORE)
        self.assertIn("m_Dirty = false", STORE)

    def test_invalid_existing_snapshots_block_instead_of_empty_campaign(self):
        with self.assertRaises(ValueError):
            load([Snapshot(1, world="Other"), Snapshot(2, complete=False)])
        self.assertIn("m_Ready = !exists || m_Slot >= 0", STORE)
        self.assertIn('complete == "COMPLETE"', STORE)
        self.assertIn("!result.m_Cells.Contains(key)", STORE)
        self.assertIn("count <= 100000", STORE)
        self.assertIn("ValidCellKey(key)", STORE)
        self.assertIn('world == GetGame().GetWorldFile()', STORE)

    def test_only_permanent_successful_transitions_are_saved(self):
        for permanent in (False, True):
            for credit in (False, True):
                for friendly in (False, True):
                    cells = {}
                    record(cells, "1:2", permanent, credit, "camp|100:200", friendly)
                    self.assertEqual(bool(cells), permanent and credit and friendly)
        self.assertIn("!state.m_bINFPermanent || !state.m_bINFObjectiveCaptured", STORE)
        self.assertIn("!state.m_INFCamp.m_Friendly", STORE)
        self.assertEqual(CORE.count("SavePermanent(state);"), 1)
        self.assertEqual(CORE.count("CompleteCapture(state);"), 3)
        self.assertIn("m_INFZoneStates.CommitCaptureState(state)", GRID)

    def test_progressive_registration_preserves_undiscovered_cells(self):
        saved = {"1:2": "", "3:4": "camp|123:456", "-1:-2": ""}
        loaded = dict(saved)
        record(loaded, "9:8", True, True)
        self.assertTrue(saved.items() <= loaded.items())
        ensure = CORE.split("protected INS_AmbientPatrolZoneState EnsureZone(", 1)[1]
        self.assertIn("m_Persistence.HasCell(key)", ensure)
        self.assertIn("state.m_bINFPermanent = true", ensure)
        self.assertIn("state.m_bReadyToRespawn = false", ensure)
        startup = GRID.split("override protected void DiscoverZones()", 1)[1]
        self.assertLess(startup.index("m_INFZoneStates.Start(GetOwner())"),
                        startup.index("m_INFForestCamps.LoadMapIndex()"))

    def test_camp_restore_gates_readiness_and_uses_existing_conversion(self):
        ready = GRID.split("void INF_StaticCampLayoutsReady()", 1)[1]
        self.assertLess(ready.index("RestorePermanentCamps()"),
                        ready.index("m_bINFCampsReady = true"))
        restore = CORE.split("bool RestorePermanentCamps()", 1)[1].split("void Start(", 1)[0]
        self.assertIn("MatchesCamp(key, state.m_INFCamp)", restore)
        self.assertIn("!state.m_INFCamp.m_Friendly", restore)
        self.assertIn("!state.m_bINFObjectiveCaptured", restore)
        callback = GRID.split("protected void INF_OnCampCaptured(", 1)[1].split(
            "float INF_GetCampFootprintRadius", 1)[0]
        self.assertLess(callback.index("MakeFriendly"), callback.index("CommitCaptureState"))
        self.assertNotIn("AttackStarted", restore)
        self.assertNotIn("AdvanceCapture", restore)

    def test_cache_registry_is_idempotent_during_restore_retries(self):
        add = CORE.split("void AddCache(", 1)[1].split("static bool InInfluence", 1)[0]
        self.assertIn("existing.m_Position == position", add)
        self.assertLess(add.index("existing.m_Position"), add.index("m_Caches.Insert"))

    def test_non_grid_startup_still_delegates_before_territory_load(self):
        startup = GRID.split("override protected void DiscoverZones()", 1)[1]
        self.assertLess(startup.index("super.DiscoverZones()"),
                        startup.index("m_INFZoneStates.Start(GetOwner())"))
        self.assertNotIn("INF_TerrainIndexes", STORE)
        self.assertIn("!Replication.IsServer()", STORE)


if __name__ == "__main__":
    unittest.main()
