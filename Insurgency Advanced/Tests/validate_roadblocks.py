"""Roadblock source contracts; not engine execution."""
from pathlib import Path
import re
import unittest
from source_contracts import method_body

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "Scripts" / "Game"
ROADBLOCKS = (SCRIPTS / "INF_Roadblocks.c").read_text()
GRID = (SCRIPTS / "INF_BuildingGrid.c").read_text()
ZONES = (SCRIPTS / "INF_ZoneStateSystem.c").read_text()
BLOCK = (SCRIPTS / "INF_BlockControl.c").read_text()


def roadblock(signature):
    return method_body(ROADBLOCKS, signature)


def grid(signature):
    return method_body(GRID, signature)


class RoadblockPlacement(unittest.TestCase):
    def test_placement_uses_the_world_composition_slots(self):
        self.assertIn("SCR_SiteSlotEntity.Cast(entity)", roadblock("protected bool CollectSlot("))
        self.assertIn("slot.IsOccupied()", roadblock("protected bool CollectSlot("))
        self.assertIn("slot.SpawnEntityInSlot(resource)", roadblock("protected INF_Roadblock PlaceInSlot("))
        self.assertIn("QueryEntitiesByAABB(minimum, maximum, CollectSlot)", roadblock("protected void ScanTick("))

    def test_slot_type_is_the_folder_under_slotted(self):
        body = roadblock("static string SlotType(")
        self.assertIn('"/slotted/"', body)
        self.assertIn("path.ToLower()", body)

    def test_string_contains_is_not_used(self):
        # Enforce has no string.Contains; the project uses IndexOf.
        self.assertNotRegex(ROADBLOCKS, r"(name|slotName)\.Contains\(")

    def test_compositions_are_configured_on_the_game_mode_with_no_built_in_faction_sets(self):
        body = roadblock("protected void ResolveCompositions(")
        self.assertIn("foreach (ResourceName candidate : m_Config.m_aComposition)", body)
        self.assertIn("Roadblocks: Compositions", body)
        self.assertIn("protected ref array<ResourceName> m_aINFRoadblockCompositions;", GRID)
        self.assertIn("config.m_aComposition.Copy(m_aINFRoadblockCompositions)", grid("protected INF_RoadblockConfig INF_BuildRoadblockConfig("))
        self.assertIn('category: "Roadblocks"', GRID)
        self.assertNotRegex(ROADBLOCKS, r'"\{[0-9A-F]{16}\}[^"]*Slotted')
        for removed in ("GetAllFactionEntityCatalogs", "DefaultCompositions", "m_sHostileFaction", "FALLBACK_FACTION"):
            self.assertNotIn(removed, ROADBLOCKS)
        self.assertNotIn("INF_ResolveHostileFaction", GRID)

    def test_both_game_modes_are_pre_populated_with_editable_slotted_compositions(self):
        modes = {
            "Insurgency Advanced": ("_USSR_01", "_USSR_02", "_USSR_03", "_USSR_04"),
            "Insurgency Advanced FIA": ("_FIA_01", "_FIA_02"),
        }
        for addon, faction_markers in modes.items():
            path = ROOT.parent / addon / "Prefabs" / "MP" / "Modes" / "InsurgencyAdvanced" / "GameMode_InsurgencyAdvanced.et"
            block = re.search(r"m_aINFRoadblockCompositions \{(.*?)\n\s*\}", path.read_text(), re.S)
            self.assertIsNotNone(block, addon)
            entries = re.findall(r'"(\{[0-9A-F]{16}\}[^"]+)"', block.group(1))
            kinds = set()
            for entry in entries:
                match = re.search(r"\}PrefabsEditable/Auto/Compositions/Slotted/(Slot\w+)/E_", entry)
                self.assertIsNotNone(match, entry)
                kinds.add(match.group(1))
            self.assertEqual(kinds, {"SlotRoadSmall", "SlotRoadMedium", "SlotRoadLarge", "SlotFlatSmall"}, addon)
            self.assertTrue(any(marker in entry for entry in entries for marker in faction_markers), addon)
        fia = (ROOT.parent / "Insurgency Advanced FIA" / "Prefabs" / "MP" / "Modes" / "InsurgencyAdvanced" / "GameMode_InsurgencyAdvanced.et").read_text()
        self.assertIn("E_Bunker_S_FIA_01", fia)
        self.assertIn("E_Checkpoint_L_USSR_01", fia)

    def test_spacing_exclusions_and_camp_avoidance(self):
        viable = roadblock("protected bool Viable(")
        self.assertIn("INF_BuildingGrid.IsSpawnBlocked(position)", viable)
        self.assertIn("MIN_FROM_STATIC_SITE", viable)
        self.assertIn("m_Config.m_fSpacing", roadblock("protected bool SpacingClear("))
        self.assertIn("avoid.Insert(campSite.m_Position)", grid("protected bool INF_RoadblocksReady("))

    def test_one_percentage_of_all_viable_road_and_defence_slots(self):
        select = roadblock("protected void Select(")
        self.assertIn("Math.Ceil(candidates.Count() * m_Config.m_iPercent / 100.0)", select)
        self.assertIn("m_Sites.Count() >= quota", select)
        self.assertNotIn("cache", select.lower().replace("cache save", ""))
        self.assertIn("protected int m_iINFRoadblockPercent = 25;", GRID)

    def test_no_cache_hint_remains(self):
        for source in (ROADBLOCKS, GRID):
            self.assertNotIn("CacheHint", source)
            self.assertNotIn("NearCache", source)

    def test_defences_use_every_small_flat_slot_not_just_labelled_ones(self):
        collect = roadblock("protected bool CollectSlot(")
        self.assertNotIn("Fortification", collect)
        self.assertNotIn("Fortification", ROADBLOCKS)
        self.assertNotIn("TRAIT_FORTIFICATION", ROADBLOCKS)
        self.assertIn('DEFENCE_SLOT = "slotflatsmall"', ROADBLOCKS)
        self.assertIn("slotName.IndexOf(DEFENCE_SLOT) != -1", roadblock("protected bool TryPlace("))
        self.assertNotIn("SlotFlatMedium", ROADBLOCKS)
        self.assertNotIn("SlotFlatLarge", ROADBLOCKS)

    def test_defences_are_not_spaced_and_roadblocks_ignore_defences_when_spacing(self):
        place = roadblock("protected bool TryPlace(")
        self.assertIn("if (!defence && !SpacingClear(position))", place)
        self.assertIn("block.m_bDefence = defence", place)
        self.assertIn("!existing.m_bDefence &&", roadblock("protected bool SpacingClear("))

    def test_defence_guard_groups_are_two_to_four_roadblocks_use_the_setting(self):
        body = roadblock("static void AppendGuards(")
        self.assertIn("DEFENCE_MIN_GUARDS, DEFENCE_MAX_GUARDS + 1", body)
        self.assertIn("int full = guards;", body)
        self.assertIn("static const int DEFENCE_MIN_GUARDS = 2;", ROADBLOCKS)
        self.assertIn("static const int DEFENCE_MAX_GUARDS = 4;", ROADBLOCKS)
        self.assertIn("block.m_bDefence = SlotType(block.m_Composition) == DEFENCE_SLOT", roadblock("protected bool Restore("))

    def test_the_percentage_is_a_mission_header_option(self):
        self.assertIn("int m_iINFRoadblockPercent;", GRID)
        self.assertIn('INF_HeaderSettings.Int(params.m_iINFRoadblockPercent, 0, 100, "m_iINFRoadblockPercent", m_iINFRoadblockPercent)', GRID)
        self.assertIn("config.m_iPercent <= 0", roadblock("void Begin("))


class RoadblockOwnership(unittest.TestCase):
    def test_owner_is_the_nearest_activatable_zone_in_range_with_room(self):
        body = roadblock("INS_AmbientPatrolZoneState FindOwner(")
        self.assertIn("m_Config.m_fMaxOwnerDistance * m_Config.m_fMaxOwnerDistance", body)
        self.assertIn("zone.m_bINFPermanent || zone.m_bINFMarkedCleared", body)
        self.assertIn("zone.m_aINFRoadblocks.Count() >= m_Config.m_iPerZone", body)
        self.assertIn("distanceSq > bestSq", body)

    def test_a_site_with_no_owner_is_not_placed(self):
        body = roadblock("protected bool TryPlace(")
        self.assertRegex(body, r"if \(!owner\)\s*\{\s*m_iOwnerRejects\+\+;\s*return false;")
        self.assertLess(body.index("FindOwner(position)"), body.index("PlaceInSlot(slot, composition)"))

    def test_roadblocks_stay_out_of_the_camp_and_cache_systems(self):
        for forbidden in ("AddCache", "AddCamp", "RegisterSite", "AppendCandidates", "INF_CaptureObjectiveCells"):
            self.assertNotIn(forbidden, ROADBLOCKS)
        self.assertNotIn("m_aINFRoadblocks", method_body(GRID, "void INF_CollectCaptureObjectives("))


class RoadblockPersistence(unittest.TestCase):
    def test_roster_is_saved_once_and_recovered_when_the_cache_save_exists(self):
        begin = roadblock("void Begin(")
        self.assertIn("FileIO.FileExists(INS_CACHE_SAVE_FILE) && FileIO.FileExists(SAVE_FILE)", begin)
        self.assertLess(begin.index("Recover();"), begin.index("ResolveCompositions();"))
        self.assertIn('"$profile:INS_Saves/roadblock_sites.dat"', ROADBLOCKS)
        select = roadblock("protected void Select(")
        self.assertIn("!m_Sites.IsEmpty() && !Save()", select)

    def test_zero_roadblocks_never_blocks_startup_or_changes_zone_behaviour(self):
        begin = roadblock("void Begin(")
        self.assertIn("config.m_iPercent <= 0", begin)
        self.assertEqual(begin.count("m_bReady = true;"), 4)
        self.assertRegex(roadblock("static bool GuardsAlive("), r"foreach \(INS_PatrolAgent agent : state\.m_aAgents\)")
        self.assertIn("foreach (INF_Roadblock block : state.m_aINFRoadblocks)", roadblock("static void AppendGuards("))

    def test_roster_stores_everything_needed_to_respawn_and_reattach(self):
        body = roadblock("protected bool Save(")
        for field in ("m_sKey", "composition", "m_vPosition", "m_fYaw", "m_sOwnerKey"):
            self.assertIn(field, body)
        self.assertIn("file.Write(SAVE_VERSION)", body)

    def test_recovery_fails_closed_and_preserves_the_file(self):
        body = roadblock("protected void Recover(")
        self.assertIn("version != SAVE_VERSION", body)
        self.assertIn("count > MAX_SITES", body)
        self.assertIn("ValidVector(", body)
        self.assertNotIn("FileMode.WRITE", body)
        self.assertNotIn("DeleteFile", ROADBLOCKS)

    def test_restore_respawns_at_the_saved_transform(self):
        body = roadblock("protected bool Restore(")
        self.assertIn("Math3D.AnglesToMatrix(Vector(block.m_fYaw, 0, 0), params.Transform)", body)
        self.assertIn("params.Transform[3] = block.m_vPosition", body)
        self.assertNotIn("SCR_SiteSlotEntity", body)

    def test_restore_reassigns_a_missing_owner_zone(self):
        body = roadblock("protected bool Restore(")
        self.assertIn("m_Zones.GetZone(block.m_sOwnerKey)", body)
        self.assertIn("FindOwner(block.m_vPosition, true)", body)

    def test_compositions_are_untracked_by_engine_persistence_and_hold_no_ai(self):
        self.assertEqual(ROADBLOCKS.count("INF_ForestCamps.PrepareComposition(entity)"), 2)

    def test_startup_waits_for_the_roster_before_camps_are_ready(self):
        body = grid("void INF_StaticCampLayoutsReady(")
        self.assertIn("if (!INF_RoadblocksReady())", body)
        self.assertLess(body.index("INF_RoadblocksReady()"), body.index("m_INFZoneStates.AddCamp(site)"))
        self.assertLess(body.index("INF_RoadblocksReady()"), body.index("m_bINFCampsReady = true"))

    def test_scan_stops_when_the_mission_ends(self):
        self.assertIn("m_INFRoadblocks.Stop()", grid("override void OnGameModeEnd("))
        self.assertIn("Remove(ScanTick)", roadblock("void Stop("))


class RoadblockGuards(unittest.TestCase):
    def test_guards_join_the_plan_so_budget_and_reservation_include_them(self):
        body = grid("override protected void ActivateZone(")
        plan = body.index("INF_BuildingGrid.Plan(limitAI")
        append = body.index("INF_Roadblocks.AppendGuards(")
        group_count = body.index("state.m_iGroupCount = state.m_aINFPlan.Count()")
        planned = body.index("state.m_iINFPlannedAI += planned")
        make_room = body.index("INF_MakeRoom(state)")
        consume = body.index("INF_Roadblocks.ConsumeMemory(state)")
        self.assertTrue(plan < append < group_count < planned < make_room < consume)
        self.assertIn("m_iINFRoadblockGuards", body)

    def test_guards_spawn_at_the_roadblock_and_saved_positions_only_index_ordinary_groups(self):
        body = grid("override protected void ActivateZone(")
        self.assertIn("state.m_aINFActiveRoadblocks[i - (state.m_iGroupCount - guardGroups)].m_vPosition", body)
        self.assertGreater(body.index("state.m_aSavedPositions[i]"), body.index("RandomPositionInRadius"))

    def test_guard_group_is_an_agent_of_the_owner_zone_standing_at_the_roadblock(self):
        body = grid("override protected void SpawnGroupAt(")
        self.assertIn("INF_FindRoadblockGuardPosition(guardedRoadblock)", body)
        self.assertIn("INF_Roadblocks.GUARD_RADIUS", body)
        self.assertIn("agent.m_INFRoadblock = guardedRoadblock", body)
        self.assertIn("state.m_aAgents.Insert(agent)", body)
        self.assertIn("INF_Roadblock m_INFRoadblock;", BLOCK)

    def test_guards_hold_the_roadblock_under_a_defend_waypoint_that_uses_turrets(self):
        spawn = grid("override protected void SpawnGroupAt(")
        self.assertRegex(spawn, r"if \(guardedRoadblock\)\s*m_CallQueue\.CallLater\(INF_DefendRoadblock, 1500, false, agent\);\s*else\s*m_CallQueue\.CallLater\(PatrolTick")
        defend = grid("protected void INF_DefendRoadblock(")
        self.assertIn("agent.m_Group.AddWaypointAt(agent.m_INFDefend, 0)", defend)
        waypoint = roadblock("static AIWaypoint SpawnDefendWaypoint(")
        self.assertIn("SetUseTurrets(true)", waypoint)
        self.assertIn("SetCompletionRadius(DEFEND_RADIUS)", waypoint)
        self.assertIn("AIWaypoint_Defend.et", ROADBLOCKS)

    def test_the_guard_waypoint_is_deleted_with_its_group(self):
        despawn = grid("protected void INF_DespawnDefenderGroup(")
        self.assertLess(despawn.index("agent.m_INFDefend"), despawn.index("if (!group)"))
        self.assertIn("AIWaypoint m_INFDefend;", BLOCK)

    def test_guard_spawn_does_not_force_the_cell_containment_rule(self):
        body = grid("protected vector INF_FindRoadblockGuardPosition(")
        self.assertNotIn("INF_BuildingGrid.Contains", body)
        self.assertIn("INF_BlockRules.MIN_GRID_SPAWN_DISTANCE", body)

    def test_spawn_queue_maps_the_plan_index_to_its_roadblock(self):
        body = grid("protected void INF_SpawnQueued(")
        self.assertIn("INF_Roadblocks.PlannedGuard(state, planIndex)", body)

    def test_guard_count_defaults_to_four_in_a_range(self):
        self.assertIn("protected int m_iINFRoadblockGuards = 4;", GRID)
        self.assertIn('"2 8 1"', GRID)

    def test_guards_do_not_count_as_ordinary_survivors_but_are_remembered_separately(self):
        count = grid("protected int INF_CountDefenders(")
        self.assertIn("agent.m_INFRoadblock", count)
        self.assertIn("INF_Roadblocks.OrdinaryGroups(state)", count)
        deactivate = grid("override protected void DeactivateZone(")
        self.assertIn("INF_Roadblocks.ExtractGuards(state, guards)", deactivate)
        self.assertIn("INF_Roadblocks.RememberGuards(", deactivate)
        self.assertIn("INF_Roadblocks.OrdinaryPlannedAI(state)", deactivate)
        self.assertIn("INF_DespawnDefenderGroup, m_INFDespawnGate.Reserve(), false, guard", deactivate)
        self.assertLess(deactivate.index("ExtractGuards"), deactivate.index("super.DeactivateZone(state)"))

    def test_remembered_guards_keep_a_wiped_out_zone_activatable(self):
        body = grid("override protected void ActivateZone(")
        self.assertIn("state.m_iINFRememberedAI == 0 && !INF_Roadblocks.HasRememberedGuards(state, activationTime)", body)

    def test_a_guard_group_that_never_spawned_keeps_its_previous_memory(self):
        body = roadblock("static void RememberGuards(")
        self.assertRegex(body, r"if \(!spawned\)\s*continue;")

    def test_zone_capture_waits_for_living_guards_wherever_they_stand(self):
        body = method_body(ZONES, "void CheckCapture(")
        self.assertIn("!INF_Roadblocks.GuardsAlive(state, presence)", body)
        guards = roadblock("static bool GuardsAlive(")
        self.assertIn("agent.m_INFRoadblock", guards)
        self.assertNotIn("m_vCenter", guards)


if __name__ == "__main__":
    unittest.main()
