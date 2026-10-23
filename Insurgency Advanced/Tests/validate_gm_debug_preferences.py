"""Per-GM debug ownership, access and cleanup contracts; no engine execution."""
from pathlib import Path
import unittest
from source_contracts import method_body

ROOT = Path(__file__).resolve().parents[1]
INTEL = (ROOT / "Scripts" / "Game" / "INF_CacheObjective.c").read_text()
CONTROLLER = (ROOT / "Scripts" / "Game" / "INF_GMDebugMarkers.c").read_text()
ATTRIBUTES = (ROOT / "Scripts" / "Game" / "INF_DebugCampMarkersEditorAttribute.c").read_text()


class GMDebugPreferences(unittest.TestCase):
    def test_access_requires_edit_mode_not_admin_or_unlimited_status(self):
        self.assertIn("core.GetEditorManager(playerId)", CONTROLLER)
        self.assertIn("editor && editor.HasMode(EEditorMode.EDIT)", CONTROLLER)
        self.assertNotIn("IsLimited", CONTROLLER)
        self.assertNotIn("IsAdmin", CONTROLLER)
        self.assertIn("Rejected debug request without GM edit-mode access", INTEL)
        for admin in (False, True):
            for limited in (False, True):
                for edit_mode in (False, True):
                    allowed = edit_mode
                    self.assertEqual(allowed, edit_mode, (admin, limited))

    def test_snapshots_target_controller_owner_and_never_broadcast(self):
        self.assertIn("RplRcver.Owner", CONTROLLER)
        self.assertNotIn("RplRcver.Broadcast", CONTROLLER)
        self.assertIn("GetPlayerId() != SCR_PlayerController.GetLocalPlayerId()", CONTROLLER)
        self.assertIn("if (!INF_DebugMarkerAccess.IsGM(GetPlayerId()))", CONTROLLER)
        self.assertNotIn("Rpc(RpcPlaceDebugMarker", INTEL)
        self.assertNotIn("Rpc(RpcClearDebugMarkers", INTEL)
        self.assertNotIn("INF_RpcPlaceCampDebugMarker", INTEL)
        snapshot = INTEL.split("protected void INF_SendDebugMarkerSnapshot(", 1)[1].split(
            "void INF_ApplyDebugMarkerSnapshot(", 1)[0]
        self.assertLess(snapshot.index("layers = 0"), snapshot.index("m_aINFDebugCachePositions.Insert"))
        self.assertIn("controller.INF_SendDebugMarkerSnapshot(layers, cachePositions, campPositions)", snapshot)

    def test_attributes_read_and_write_requesting_players_preferences(self):
        for camps in ("true", "false"):
            self.assertIn(f"INF_GetDebugMarkersForPlayer(editor.GetPlayerID(), {camps})", ATTRIBUTES)
            self.assertIn(f"INF_SetDebugMarkersForPlayer(playerID, {camps}, var.GetBool())", ATTRIBUTES)
        self.assertIn("editor.HasMode(EEditorMode.EDIT)", ATTRIBUTES)
        self.assertIn("layers = layers | layer", INTEL)
        self.assertIn("layers = layers & ~layer", INTEL)
        self.assertNotIn('RplProp(onRplName: "INF_OnDebugVisibilityChanged")', INTEL)

    def test_disable_and_lost_access_clear_both_maps_without_forgetting_handles(self):
        self.assertIn("CallLater(INF_OnDebugVisibilityChanged, 2000, false)", INTEL)
        self.assertIn("if (System.IsConsoleApp())", INTEL)
        self.assertIn("Remove(INF_OnDebugVisibilityChanged)", INTEL)
        callback = INTEL.split("protected void INF_OnDebugVisibilityChanged()", 1)[1].split(
            "protected void INF_RefreshDebugMarkers()", 1)[0]
        self.assertIn("!m_bINFDebugCachesEnabled || !allowed", callback)
        self.assertIn("!m_bINFDebugCampMarkers || !allowed", callback)
        helper = INTEL.split("protected void INF_ClearLocalDebugMarkers(", 1)[1].split(
            "protected void INF_PlaceCampDebugMarker", 1)[0]
        missing = helper.split("if (!markerMgr)", 1)[1].split("m_bINFDebugClearWarning = false", 1)[0]
        self.assertIn("return;", missing)
        self.assertIn("LogLevel.WARNING", missing)
        self.assertNotIn("markers.Clear()", missing)
        self.assertLess(helper.index("INF_RemoveLocalDebugMarker(marker)"), helper.index("markers.Clear()"))

    def test_local_cleanup_removes_offscreen_markers_before_native_removal(self):
        cleanup = method_body(CONTROLLER, "void INF_RemoveLocalDebugMarker(")
        self.assertIn("marker.GetMarkerID() != -1", cleanup)
        self.assertIn("LogLevel.ERROR", cleanup)
        self.assertLess(cleanup.index("m_aDisabledMarkers.RemoveItem(marker)"),
                        cleanup.index("RemoveStaticMarker(marker)"))
        for offscreen in (False, True):
            with self.subTest(offscreen=offscreen):
                camp = object()
                cache = object()
                active = [cache] + ([] if offscreen else [camp])
                disabled = [camp] if offscreen else []
                if camp in disabled:
                    disabled.remove(camp)
                if camp in active:
                    active.remove(camp)
                self.assertNotIn(camp, active + disabled)
                self.assertIn(cache, active)
                active.extend(disabled)
                self.assertNotIn(camp, active)

    def test_two_gms_and_both_layers_remain_independent(self):
        preferences = {1: 0, 2: 0}
        preferences[1] |= 1
        self.assertEqual(preferences, {1: 1, 2: 0})
        preferences[2] |= 2
        preferences[1] |= 2
        preferences[1] &= ~1
        self.assertEqual(preferences, {1: 2, 2: 2})
        preferences[1] &= ~2
        self.assertEqual(preferences, {1: 0, 2: 2})
        preferences[2] = 0  # Losing EDIT access clears this controller's subscription.
        self.assertEqual(preferences, {1: 0, 2: 0})

    def test_new_controllers_default_off_and_non_grid_delegates(self):
        self.assertIn("protected int m_iINFDebugMarkerLayers;", CONTROLLER)
        self.assertNotIn("static int m_iINFDebugMarkerLayers", CONTROLLER)
        self.assertIn("super.SetDebugCacheMarkers(value)", INTEL)
        self.assertIn("super.GetDebugCacheMarkers()", INTEL)
        self.assertIn("super.BroadcastDebugMarkers()", INTEL)
        self.assertIn("super.ReadVariable(item, manager)", ATTRIBUTES)


if __name__ == "__main__":
    unittest.main()
