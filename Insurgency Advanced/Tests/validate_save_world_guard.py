import os
import unittest

ROOT = os.path.join(os.path.dirname(__file__), "..", "Scripts", "Game")


def read(name):
    with open(os.path.join(ROOT, name), encoding="utf-8") as f:
        return f.read()


class SaveWorldGuardTests(unittest.TestCase):
    def test_guard_clears_saves_for_a_different_world_or_missing_base_cache_save(self):
        guard = read("INF_SaveWorldGuard.c")
        ensure = guard[guard.index("static void Ensure()"):guard.index("protected static string ReadMarker()")]
        self.assertIn("!saved.IsEmpty() && saved != current", ensure)
        self.assertIn("!FileIO.FileExists(INS_CACHE_SAVE_FILE)", ensure)
        # A missing marker is added; nothing is moved, archived or flagged.
        self.assertIn("if (saved != current)\n\t\t\tWriteMarker(current);", ensure)
        for removed in ("ended.dat", "backups", "CopyFile", "MarkEnded"):
            self.assertNotIn(removed, guard)

    def test_guard_runs_before_saves_are_read(self):
        self.assertIn("INF_SaveWorldGuard.Ensure();", read("INF_PrefabManager.c"))
        forest = read("INF_ForestCamps.c")
        self.assertLess(forest.index("INF_SaveWorldGuard.Ensure();"), forest.index("GetRecoveredSites(recovered)"))

    def test_unrecoverable_cache_saves_start_fresh_instead_of_blocking(self):
        self.assertNotIn("blocking fresh selection", read("INF_ForestCampRewards.c"))


if __name__ == "__main__":
    unittest.main()
