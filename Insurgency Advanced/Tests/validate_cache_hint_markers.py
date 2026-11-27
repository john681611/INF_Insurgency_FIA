"""Offline cache-hint marker presentation contracts."""
from pathlib import Path
import unittest

from source_contracts import method_body


ROOT = Path(__file__).resolve().parents[1]
MANAGER = (ROOT / "Scripts/Game/INF_CacheObjective.c").read_text()


def hint_colour(distance):
    if distance < 250:
        return "red"
    if distance < 500:
        return "orange"
    if distance < 1000:
        return "yellow"
    return "blue"


class CacheHintMarkerContracts(unittest.TestCase):
    def test_colour_bands_are_coarse_and_ordered_cold_to_hot(self):
        self.assertEqual(
            [hint_colour(value) for value in (0, 249, 250, 499, 500, 999, 1000, 5000)],
            ["red", "red", "orange", "orange", "yellow", "yellow", "blue", "blue"],
        )
        colour = method_body(MANAGER, "protected static int INF_CacheHintColorEntry(")
        for threshold in ("distance < 250", "distance < 500", "distance < 1000"):
            self.assertIn(threshold, colour)
        self.assertEqual(colour.count("return "), 4)
        creation = method_body(MANAGER, "override protected void CreateCacheMarker(")
        self.assertIn("super.CreateCacheMarker(x, z, distance, cacheIndex)", creation)
        self.assertIn("marker.SetColorEntry(INF_CacheHintColorEntry(distance))", creation)
        self.assertIn("widget.SetTextVisible(true)", creation)
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
