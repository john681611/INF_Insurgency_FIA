"""Offline GM-map wiring/regression checks, not an Enfusion compile or UI test."""
from pathlib import Path
import re
import unittest
from source_contracts import method_body


ROOT = Path(__file__).resolve().parents[1]


class GMMarkerContracts(unittest.TestCase):
    def test_failed_zone_marker_spawn_retries_without_stopping_map_updates(self):
        source = (ROOT / "Scripts/Game/INF_GarrisonMapAreas.c").read_text()
        create = method_body(source, "protected INF_GarrisonMapArea INF_CreateZoneArea(")
        self.assertNotIn("Remove(INF_UpdateZoneAreas)", create)
        self.assertIn("now < m_fINFNextAreaSpawnAttempt", create)
        self.assertIn("m_fINFNextAreaSpawnAttempt = now + 5000", create)
        self.assertIn("now >= m_fINFNextAreaSpawnWarning", create)
        self.assertIn("m_fINFNextAreaSpawnWarning = now + 30000", create)
        attempts, warnings, retry_at, warn_at = [], [], 0, 0
        for now in range(0, 35000, 1000):
            for _ in range(995):
                if now < retry_at:
                    continue
                attempts.append(now)
                retry_at = now + 5000
                if now >= warn_at:
                    warnings.append(now)
                    warn_at = now + 30000
        self.assertEqual(attempts, list(range(0, 35000, 5000)))
        self.assertEqual(warnings, [0, 30000])

    def test_mission_uses_inherited_gm_map(self):
        config = (ROOT / "Configs/Map/INF_MapEditor.conf").read_text()
        meta = (ROOT / "Configs/Map/INF_MapEditor.conf.meta").read_text()
        mission = (ROOT / "Prefabs/MP/Modes/InsurgencyAdvanced/GameMode_InsurgencyAdvanced.et").read_text()
        self.assertIn('SCR_MapConfig : "{19C76194B21EC3E1}Configs/Map/MapEditor.conf"', config)
        self.assertIn('INF_GMMapMarkersUI "', config)
        self.assertIn('SCR_MapConfigComponent "{5C669498E250A34B}"', mission)
        resource = "{6AA19CB200000200}Configs/Map/INF_MapEditor.conf"
        self.assertIn(resource, meta)
        self.assertIn('m_sEditorMapConfigPath "' + resource + '"', mission)
        self.assertIn("CONFResourceClass PC", meta)
        self.assertNotIn("m_sGadgetMapConfigPath", mission)

    def test_shared_renderer_preserves_gm_input(self):
        source = (ROOT / "Scripts/Game/INF_GMMapMarkersUI.c").read_text()
        self.assertIn("class INF_GMMapMarkersUI : SCR_MapMarkersUI", source)
        self.assertIn("override void OnMapOpen(MapConfiguration config)", source)
        self.assertIn("super.OnMapOpen(config)", source)
        callbacks = (
            "OnInputQuickMarkerMenu", "OnInputMarkerDelete", "OnInputMapSelect",
            "OnInputMenuConfirm", "OnInputMenuConfirmAlter", "OnInputMenuBack",
            "OnInputMenuDown", "OnInputMenuUp", "OnInputMenuRight", "OnInputMenuLeft",
        )
        for callback in callbacks:
            self.assertIn("EActionTrigger.DOWN, " + callback + ")", source)
        self.assertEqual(source.count("RemoveActionListener("), len(callbacks))

    def test_cache_debug_waits_for_discovery_and_refreshes(self):
        source = (ROOT / "Scripts/Game/INF_CacheObjective.c").read_text()
        self.assertIn("CallLater(INF_RefreshDebugMarkers, 2000, false)", source)
        self.assertIn("controller.INF_DebugSnapshotChanged(layers, m_iINFDebugRevision)", source)
        snapshot = source.split("protected void INF_SendDebugMarkerSnapshot(", 1)[1].split(
            "void INF_ApplyDebugMarkerSnapshot(", 1)[0]
        self.assertIn("INF_ForestCampRewards.AreCachesReady()", snapshot)
        self.assertIn("controller.INF_SendDebugMarkerSnapshot(", snapshot)
        self.assertNotIn("Rpc(RpcPlaceDebugMarker", source)
        for callback in ("INF_RefreshDebugMarkers", "BroadcastDebugMarkers", "LoadIntelState"):
            self.assertIn("Remove(" + callback + ")", source)
        for debug, ready, expected in ((False, False, False), (False, True, False),
                                       (True, False, False), (True, True, True)):
            self.assertEqual(debug and ready, expected)

    def test_gm_root_gets_searchable_marker_child_before_creation(self):
        source = (ROOT / "Scripts/Game/INF_GMMapMarkersUI.c").read_text()
        ensure = source.split("protected bool INF_EnsureMarkerFrame", 1)[1].split(
            "override void OnMapOpen", 1)[0]
        self.assertIn("config.RootWidgetRef", ensure)
        self.assertIn("root.FindAnyWidget(SCR_MapConstants.MAP_FRAME_NAME)", ensure)
        self.assertLess(ensure.index("root.FindAnyWidget"), ensure.index("CreateWidget"))
        self.assertIn("WidgetType.FrameWidgetTypeID", ensure)
        self.assertIn("frame.SetName(SCR_MapConstants.MAP_FRAME_NAME)", ensure)
        self.assertIn("FrameSlot.SetAnchorMin(frame, 0, 0)", ensure)
        self.assertIn("FrameSlot.SetAnchorMax(frame, 1, 1)", ensure)
        self.assertIn("FrameSlot.SetOffsets(frame, 0, 0, 0, 0)", ensure)
        opened = source.split("override void OnMapOpen", 1)[1].split("override void OnMapClose", 1)[0]
        self.assertLess(opened.index("!INF_EnsureMarkerFrame(config)"), opened.index("super.OnMapOpen(config)"))
        self.assertLess(opened.index("EnableUpdate(false)"), opened.index("super.OnMapOpen(config)"))
        self.assertIn("LogLevel.ERROR", ensure)
        self.assertIn("if (!m_bINFMarkerLayerOpen)", source)
        closed = source.split("override void OnMapClose", 1)[1]
        self.assertLess(closed.index("super.OnMapClose(config)"), closed.index("child.RemoveFromHierarchy()"))
        self.assertIn("m_INFMarkerFrame.GetChildren()", closed)
        self.assertIn("m_INFMarkerFrame = frame", ensure)

    def test_descendant_only_lookup_and_repeated_map_switches(self):
        # Engine FindAnyWidget searches descendants, excluding the root itself.
        root = {"name": "MapFrame", "children": []}

        def find(node, name):
            for child in node["children"]:
                if child["name"] == name:
                    return child
                match = find(child, name)
                if match is not None:
                    return match
            return None

        self.assertIsNone(find(root, "MapFrame"))
        for _ in range(20):
            if find(root, "MapFrame") is None:
                root["children"].append({"name": "MapFrame", "children": []})
            self.assertIsNotNone(find(root, "MapFrame"))
            self.assertEqual(len(root["children"]), 1)
            frame = find(root, "MapFrame")
            frame["children"].extend({"name": "Marker", "children": []} for _ in range(30))
            self.assertEqual(len(frame["children"]), 30)
            frame["children"].clear()
        fullscreen = {"name": "MapMenu", "children": [root]}
        self.assertIs(find(fullscreen, "MapFrame"), root)
        self.assertEqual(len(fullscreen["children"]), 1)

    def test_cache_debug_permissions(self):
        source = (ROOT / "Scripts/Game/INF_GMDebugMarkers.c").read_text()
        self.assertIn("editor && editor.HasMode(EEditorMode.EDIT)", source)
        self.assertNotIn("IsLimited()", source)
        self.assertNotIn("IsAdminRole", source)
        self.assertIn("RplRcver.Owner", source)
        self.assertNotIn("RplRcver.Broadcast", source)

    def test_debug_types_resolve_distinct_icons_from_active_palette(self):
        source = (ROOT / "Scripts/Game/INF_CacheObjective.c").read_text()
        self.assertIn('m_aINFCampDebugMarkers, "fortification")', source)
        self.assertIn('m_aDebugMarkers, "objective-marker")', source)
        self.assertIn("placed.GetIconEntries()", source)
        self.assertIn("entry.GetIconResource(imageset, glow, quad)", source)
        self.assertIn("if (quad == icon)", source)
        self.assertIn("marker.SetIconEntry(iconIndex)", source)
        self.assertNotIn("marker.SetIconEntry(18)", source)
        self.assertIn("Cannot resolve debug marker icon:", source)

    def test_camp_checkbox_is_independent_and_defaults_off(self):
        config = (ROOT / "Configs/Editor/AttributeLists/Edit.conf").read_text()
        attribute = (ROOT / "Scripts/Game/INF_DebugCampMarkersEditorAttribute.c").read_text()
        source = (ROOT / "Scripts/Game/INF_CacheObjective.c").read_text()
        self.assertIn('Name "Debug: Show Camp Locations"', config)
        self.assertIn("INF_DebugCampMarkersEditorAttribute", config)
        self.assertIn("AttributePrefab_Checkbox.layout", config)
        self.assertIn("intel.INF_GetDebugMarkersForPlayer(editor.GetPlayerID(), true)", attribute)
        self.assertIn("intel.INF_SetDebugMarkersForPlayer(playerID, true, var.GetBool())", attribute)
        self.assertIn("protected bool m_bINFDebugCampMarkers;", source)
        setter = source.split("void INF_SetDebugMarkersForPlayer(", 1)[1].split(
            "protected void INF_SendDebugMarkerSnapshot", 1)[0]
        self.assertIn("!Replication.IsServer()", setter)
        self.assertIn("INF_DebugMarkerAccess.IsGM(playerId)", setter)
        self.assertNotIn("Replication.BumpMe()", setter)
        clear = source.split("protected void INF_ClearCampDebugMarkers()", 1)[1].split(
            "override protected void ClearDebugMarkers()", 1)[0]
        self.assertIn("INF_ClearLocalDebugMarkers(m_aINFCampDebugMarkers)", clear)
        self.assertNotIn("m_aDebugMarkers", clear)

    def test_camp_debug_includes_dormant_sites_without_cache_assignment(self):
        source = (ROOT / "Scripts/Game/INF_CacheObjective.c").read_text()
        broadcast = source.split("protected void INF_SendDebugMarkerSnapshot(", 1)[1].split(
            "void INF_ApplyDebugMarkerSnapshot(", 1)[0]
        self.assertIn("patrols.INF_AreCampsReady()", broadcast)
        self.assertIn("CollectCampPositions(m_aINFDebugCampPositions)", broadcast)
        core = (ROOT / "Scripts/Game/INF_ZoneStateSystem.c").read_text()
        positions = method_body(core, "void CollectCampPositions(")
        self.assertIn("positions.Insert(state.m_INFCamp.m_Position)", positions)
        self.assertIn(": m_Camps", positions)
        self.assertNotIn("m_bActive", broadcast)
        self.assertNotIn("m_bINFMarkedCleared", broadcast)
        self.assertNotIn("IsCacheSite", broadcast)
        self.assertIn('"[DBG CAMP %1]"', source)
        self.assertIn('"[DBG CACHE %1]"', source)
        refresh = source.split("protected void INF_RefreshDebugMarkers()", 1)[1].split(
            "bool INF_GetDebugCampMarkers()", 1)[0]
        self.assertIn("m_aINFDebugSubscribers", refresh)
        self.assertIn("controller.INF_GetDebugMarkerLayers() == 0", refresh)
        self.assertIn("INF_SendDebugMarkerSnapshot(controller", refresh)
        self.assertIn("INF_ClearCampDebugMarkers();\n\t\tsuper.OnGameModeEnd(data)", source)

    def test_balanced_sources_and_single_intel_extension(self):
        declarations = 0
        for path in (ROOT / "Scripts/Game").glob("*.c"):
            code = re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/', "", path.read_text(), flags=re.S)
            declarations += len(re.findall(r"\bmodded class INS_IntelManager\b", code))
            for left, right in (("{", "}"), ("(", ")"), ("[", "]")):
                self.assertEqual(code.count(left), code.count(right), str(path))
        self.assertEqual(declarations, 1)


if __name__ == "__main__":
    unittest.main()
