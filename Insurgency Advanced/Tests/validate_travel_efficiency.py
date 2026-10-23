"""Timed travel activation and live GM settings contracts; no engine execution."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
RULES = (ROOT / "Scripts" / "Game" / "INF_BlockControl.c").read_text()
GRID = (ROOT / "Scripts" / "Game" / "INF_BuildingGrid.c").read_text()
EDITOR = (ROOT / "Scripts" / "Game" / "INF_ZoneEditorAttributes.c").read_text()
CONFIG = (ROOT / "Configs" / "Editor" / "AttributeLists" / "Edit.conf").read_text()
DEFAULTS = {
    field: float(re.search(r"m_fINFTravel" + field + r" = ([\d.]+);", GRID).group(1))
    for field in ("EnterSpeed", "ExitSpeed", "EnterSeconds", "ExitSeconds")
}


class TravelModel:
    def __init__(self):
        self.fast = False
        self.above = None
        self.below = None

    def sample(self, kph, now):
        if not self.fast:
            self.below = None
            if kph >= DEFAULTS["EnterSpeed"]:
                if self.above is None:
                    self.above = now
                if now - self.above >= DEFAULTS["EnterSeconds"]:
                    self.fast = True
                    self.above = None
            else:
                self.above = None
        else:
            self.above = None
            if kph < DEFAULTS["ExitSpeed"]:
                if self.below is None:
                    self.below = now
                if now - self.below >= DEFAULTS["ExitSeconds"]:
                    self.fast = False
                    self.below = None
            else:
                self.below = None
        return self.fast


class TravelEfficiency(unittest.TestCase):
    def test_defaults_and_exact_sustained_entry_boundary(self):
        self.assertEqual(DEFAULTS, {"EnterSpeed": 50, "ExitSpeed": 30, "EnterSeconds": 20, "ExitSeconds": 15})
        model = TravelModel()
        self.assertFalse(model.sample(50, 0))
        for _ in range(50):
            self.assertFalse(model.sample(50, 0))
        self.assertFalse(model.sample(50, 19.999))
        self.assertTrue(model.sample(50, 20))

    def test_interrupted_entry_is_not_accumulated(self):
        model = TravelModel()
        for speed, now, expected in ((60, 0, False), (60, 19, False),
                                     (49, 19.5, False), (60, 20, False),
                                     (60, 39.999, False), (60, 40, True)):
            self.assertEqual(model.sample(speed, now), expected)

    def test_exit_requires_fifteen_continuous_seconds_strictly_below_thirty(self):
        model = TravelModel()
        model.sample(60, 0)
        self.assertTrue(model.sample(60, 20))
        self.assertTrue(model.sample(29, 21))
        self.assertTrue(model.sample(29, 35.999))
        self.assertTrue(model.sample(30, 36))  # Resets rather than completes.
        self.assertTrue(model.sample(29, 37))
        self.assertTrue(model.sample(29, 51.999))
        self.assertFalse(model.sample(29, 52))

    def test_middle_band_preserves_mode_but_resets_pending_transitions(self):
        model = TravelModel()
        self.assertFalse(model.sample(40, 0))
        model.sample(60, 1)
        self.assertTrue(model.sample(60, 21))
        self.assertTrue(model.sample(40, 200))
        self.assertTrue(model.sample(29, 201))
        self.assertTrue(model.sample(40, 214))
        self.assertTrue(model.sample(29, 215))
        self.assertFalse(model.sample(29, 230))

    def test_engine_logic_uses_world_time_and_clears_stale_identity(self):
        body = RULES.split("static bool IsFast(", 1)[1].split("static int ZoneLimit(", 1)[0]
        self.assertIn("state.m_Character != character", body)
        self.assertIn("speed * 3.6 >= patrols.INF_GetTravelSetting(0)", body)
        self.assertIn("speed * 3.6 < patrols.INF_GetTravelSetting(1)", body)
        self.assertIn("now - state.m_AboveSince >= patrols.INF_GetTravelSetting(2) * 1000", body)
        self.assertIn("now - state.m_BelowSince >= patrols.INF_GetTravelSetting(3) * 1000", body)
        self.assertIn("GetGame().GetWorld().GetWorldTime(), patrols, entity", RULES)
        self.assertIn("s_FastPlayers.Remove(id)", RULES)
        self.assertIn("s_FastPlayers.Remove(s_FastPlayers.GetKey(stale))", RULES)

    def test_all_four_settings_are_wired_and_reset_only_travel_state(self):
        entries = re.findall(r'INF_TravelSettingEditorAttribute\s+"[^"]+"\s*\{\s*m_iSetting (\d)', CONFIG)
        self.assertEqual(entries, ["0", "1", "2", "3"])
        self.assertIn("patrols.INF_GetTravelSetting(m_iSetting)", EDITOR)
        self.assertIn("patrols.INF_SetTravelSetting(m_iSetting, var.GetFloat())", EDITOR)
        setter = GRID.split("void INF_SetTravelSetting(", 1)[1].split("override void OnGameModeStart()", 1)[0]
        self.assertIn("!Replication.IsServer()", setter)
        self.assertIn("value <= m_fINFTravelExitSpeed", setter)
        self.assertIn("value >= m_fINFTravelEnterSpeed", setter)
        self.assertIn("!(value >= minimum && value <= maximum)", setter)
        self.assertIn("LogLevel.ERROR", setter)
        self.assertIn("Replication.BumpMe()", setter)
        self.assertIn("INF_ActivationPolicy.ResetTravel()", setter)
        self.assertNotIn("INF_ActivationPolicy.Reset()", setter)


if __name__ == "__main__":
    unittest.main()
