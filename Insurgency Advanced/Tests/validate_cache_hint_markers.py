"""Offline cache-hint marker presentation contracts."""
from pathlib import Path
import unittest

from source_contracts import method_body


ROOT = Path(__file__).resolve().parents[1]
MANAGER = (ROOT / "Scripts/Game/INF_CacheObjective.c").read_text()
MARKERS = (ROOT / "Scripts/Game/INF_CacheHintMarkers.c").read_text()


def hint_colour(distance):
    if distance < 250:
        return "red"
    if distance < 500:
        return "orange"
    if distance < 1000:
        return "yellow"
    return "blue"


class CacheHintMarkerContracts(unittest.TestCase):
    def test_marker_lookup_reuses_native_cache_registry(self):
        lookup = method_body(MANAGER, "bool INF_TryGetCacheMarkerDistance(")
        self.assertIn("m_aCacheMarkers", lookup)
        self.assertIn("m_aCacheMarkerDists", lookup)
        self.assertIn("m_aCacheMarkers[cacheIndex][markerIndex] != marker", lookup)
        self.assertIn("m_aCacheMarkerDists[cacheIndex][markerIndex]", lookup)
        self.assertNotIn("CreateCacheMarker", lookup)
        self.assertNotIn("SetCustomText", lookup)
        self.assertNotIn("CallLater", lookup)

    def test_widget_preserves_question_mark_and_only_restyles_cache_hints(self):
        attach = method_body(MARKERS, "override void SetMarkerObject(")
        self.assertIn("super.SetMarkerObject(marker)", attach)
        self.assertIn("INS_IntelManager.GetInstance()", attach)
        self.assertIn("INF_TryGetCacheMarkerDistance(marker, distance)", attach)
        self.assertIn("SetTextVisible(true)", attach)
        for forbidden in ("SetIconEntry", "SetImage", "SetTypeIcon", "SetRotation", "SetWorldPos"):
            self.assertNotIn(forbidden, MARKERS)

    def test_distance_text_stays_visible_without_changing_native_text(self):
        visible = method_body(MARKERS, "override void SetTextVisible(")
        self.assertIn("m_bINFCacheHint", visible)
        self.assertIn("state = true", visible)
        self.assertIn("super.SetTextVisible(state)", visible)
        self.assertNotIn("SetText(", MARKERS)
        self.assertNotIn("string.Format", MARKERS)

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

    def test_marker_style_has_no_runtime_polling_or_visual_geometry(self):
        forbidden = ("EOnFrame", "OnUpdate", "CallLater", "CanvasWidget", "TessellateCircle",
                     "PolygonDrawCommand", "LineDrawCommand", "SetSize", "SetIconEntry")
        for token in forbidden:
            self.assertNotIn(token, MARKERS)
        self.assertIn("override void HandlerDeattached", MARKERS)

    def test_player_and_gm_maps_share_native_marker_widget_component(self):
        gm = (ROOT / "Scripts/Game/INF_GMMapMarkersUI.c").read_text()
        config = (ROOT / "Configs/Map/INF_MapEditor.conf").read_text()
        self.assertIn("class INF_GMMapMarkersUI : SCR_MapMarkersUI", gm)
        self.assertIn("super.OnMapOpen(config)", gm)
        self.assertIn('SCR_MapConfig : "{19C76194B21EC3E1}Configs/Map/MapEditor.conf"', config)
        self.assertNotIn("INF_GMMapMarkersUI", MARKERS)


if __name__ == "__main__":
    unittest.main()
