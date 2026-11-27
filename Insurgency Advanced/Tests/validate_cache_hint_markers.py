"""Offline cache-hint marker presentation contracts."""
from pathlib import Path
import unittest

from source_contracts import method_body


ROOT = Path(__file__).resolve().parents[1]
MANAGER = (ROOT / "Scripts/Game/INF_CacheObjective.c").read_text()


def hint_colour(distance):
    if distance < 300:
        return "dark red"
    if distance < 750:
        return "red"
    if distance < 1250:
        return "pink red"
    if distance < 2000:
        return "pale red"
    return "white"


class CacheHintMarkerContracts(unittest.TestCase):
    def test_colour_bands_are_coarse_and_ordered_cold_to_hot(self):
        self.assertEqual(
            [hint_colour(value) for value in (0, 299, 300, 749, 750, 1249, 1250, 1999, 2000, 5000)],
            ["dark red", "dark red", "red", "red", "pink red", "pink red", "pale red", "pale red", "white", "white"],
        )
        colour = method_body(MANAGER, "protected int INF_CacheHintColorEntry(")
        for threshold in ("distMetres < 300", "distMetres < 750", "distMetres < 1250", "distMetres < 2000"):
            self.assertIn(threshold, colour)
        self.assertIn("placed.GetColorEntries()", colour)
        self.assertIn("entry.GetColor()", colour)
        creation = method_body(MANAGER, "override protected void CreateCacheMarker(")
        self.assertIn("override protected void CreateCacheMarker(int worldX, int worldZ, int distMetres, int cacheIndex)", MANAGER)
        self.assertIn("super.CreateCacheMarker(worldX, worldZ, distMetres, cacheIndex)", creation)
        self.assertIn("marker.SetColorEntry(INF_CacheHintColorEntry(distMetres))", creation)
        self.assertNotIn("SetTextVisible", creation)
        self.assertNotIn("SetText(", creation)
        self.assertNotIn("SetIconEntry", creation)
        self.assertNotIn("SetWorldPos", creation)

    def test_marker_style_has_no_runtime_polling_or_visual_geometry(self):
        creation = method_body(MANAGER, "override protected void CreateCacheMarker(")
        forbidden = ("EOnFrame", "OnUpdate", "CallLater", "CanvasWidget", "TessellateCircle",
                     "PolygonDrawCommand", "LineDrawCommand", "SetSize", "SetIconEntry")
        for token in forbidden:
            self.assertNotIn(token, creation)

    def test_player_and_gm_maps_share_native_marker_widget_component(self):
        gm = (ROOT / "Scripts/Game/INF_GMMapMarkersUI.c").read_text()
        config = (ROOT / "Configs/Map/INF_MapEditor.conf").read_text()
        self.assertIn("class INF_GMMapMarkersUI : SCR_MapMarkersUI", gm)
        self.assertIn("super.OnMapOpen(config)", gm)
        self.assertIn('SCR_MapConfig : "{19C76194B21EC3E1}Configs/Map/MapEditor.conf"', config)
        self.assertNotIn("INF_GMMapMarkersUI", MANAGER)


if __name__ == "__main__":
    unittest.main()
