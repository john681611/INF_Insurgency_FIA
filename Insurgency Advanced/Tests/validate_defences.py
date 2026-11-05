"""Defence source contracts; not engine execution."""
from pathlib import Path
import re
import unittest
from source_contracts import method_body

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "Scripts" / "Game"
DEFENCES = (SCRIPTS / "INF_Defences.c").read_text()
GRID = (SCRIPTS / "INF_BuildingGrid.c").read_text()
ZONES = (SCRIPTS / "INF_ZoneStateSystem.c").read_text()
BLOCK = (SCRIPTS / "INF_BlockControl.c").read_text()
REWARDS = (SCRIPTS / "INF_ForestCampRewards.c").read_text()
MODE = Path("Prefabs") / "MP" / "Modes" / "InsurgencyAdvanced" / "GameMode_InsurgencyAdvanced.et"


def defence(signature):
    return method_body(DEFENCES, signature)


def grid(signature):
    return method_body(GRID, signature)


class DefencePlacement(unittest.TestCase):
    def test_the_old_roadblock_name_is_gone_from_the_scripts_and_prefabs(self):
        for path in SCRIPTS.glob("*.c"):
            self.assertNotRegex(path.read_text(), r"(?i)roadblock", path.name)
        self.assertFalse((SCRIPTS / "INF_Roadblocks.c").exists())
        for addon in ("Insurgency Advanced", "Insurgency Advanced FIA"):
            self.assertNotRegex((ROOT.parent / addon / MODE).read_text(), r"(?i)roadblock", addon)

    def test_placement_uses_the_world_composition_slots(self):
        self.assertIn("SCR_SiteSlotEntity.Cast(entity)", defence("protected bool CollectSlot("))
        self.assertIn("slot.IsOccupied()", defence("protected bool CollectSlot("))
        self.assertIn("slot.SpawnEntityInSlot(resource)", defence("protected INF_Defence PlaceInSlot("))
        self.assertIn("QueryEntitiesByAABB(minimum, maximum, CollectSlot)", defence("protected void ScanTick("))

    def test_slot_type_is_the_folder_under_slotted(self):
        body = defence("static string SlotType(")
        self.assertIn('"/slotted/"', body)
        self.assertIn("path.ToLower()", body)

    def test_string_contains_is_not_used(self):
        # Enforce has no string.Contains; the project uses IndexOf.
        self.assertNotRegex(DEFENCES, r"(name|slotName)\.Contains\(")

    def test_compositions_are_configured_on_the_game_mode_with_no_built_in_sets(self):
        body = defence("protected void ResolveCompositions(")
        self.assertIn("foreach (ResourceName candidate : m_Config.m_aComposition)", body)
        self.assertIn("Defences: Compositions", body)
        self.assertIn("protected ref array<ResourceName> m_aINFDefenceCompositions;", GRID)
        self.assertIn("config.m_aComposition.Copy(m_aINFDefenceCompositions)", grid("protected INF_DefenceConfig INF_BuildDefenceConfig("))
        self.assertIn('category: "Defences"', GRID)
        self.assertNotRegex(DEFENCES, r'"\{[0-9A-F]{16}\}[^"]*Slotted')
        for removed in ("GetAllFactionEntityCatalogs", "DefaultCompositions", "m_sHostileFaction", "FALLBACK_FACTION"):
            self.assertNotIn(removed, DEFENCES)

    def test_both_game_modes_are_pre_populated_with_editable_slotted_compositions(self):
        modes = {
            "Insurgency Advanced": ("_USSR_01", "_USSR_02", "_USSR_03", "_USSR_04"),
            "Insurgency Advanced FIA": ("_FIA_01", "_FIA_02"),
        }
        for addon, faction_markers in modes.items():
            text = (ROOT.parent / addon / MODE).read_text()
            block = re.search(r"m_aINFDefenceCompositions \{(.*?)\n\s*\}", text, re.S)
            self.assertIsNotNone(block, addon)
            entries = re.findall(r'"(\{[0-9A-F]{16}\}[^"]+)"', block.group(1))
            kinds = set()
            for entry in entries:
                match = re.search(r"\}PrefabsEditable/Auto/Compositions/Slotted/(Slot\w+)/E_", entry)
                self.assertIsNotNone(match, entry)
                kinds.add(match.group(1))
            self.assertEqual(kinds, {"SlotRoadSmall", "SlotRoadMedium", "SlotRoadLarge", "SlotFlatSmall"}, addon)
            self.assertTrue(any(marker in entry for entry in entries for marker in faction_markers), addon)
        fia = (ROOT.parent / "Insurgency Advanced FIA" / MODE).read_text()
        self.assertIn("E_Bunker_S_FIA_01", fia)
        self.assertIn("E_Checkpoint_L_USSR_01", fia)

    def test_spacing_exclusions_and_camp_avoidance(self):
        viable = defence("protected bool Viable(")
        self.assertIn("INF_BuildingGrid.IsSpawnBlocked(position)", viable)
        self.assertIn("MIN_FROM_STATIC_SITE", viable)
        self.assertIn("m_Config.m_fSpacing", defence("protected bool SpacingClear("))
        self.assertIn("avoid.Insert(campSite.m_Position)", grid("protected void INF_StartDefences("))

    def test_one_percentage_of_all_viable_road_and_small_flat_slots(self):
        select = defence("protected void PlaceDefences(")
        self.assertIn("Math.Ceil(candidates.Count() * m_Config.m_iPercent / 100.0)", select)
        self.assertIn("m_Sites.Count() >= quota", select)
        self.assertNotIn("cache", select.lower().replace("cache save", ""))
        self.assertIn("protected int m_iINFDefencePercent = 15;", GRID)

    def test_no_cache_hint_remains(self):
        for source in (DEFENCES, GRID):
            self.assertNotIn("CacheHint", source)
            self.assertNotIn("NearCache", source)

    def test_every_small_flat_slot_is_a_candidate_not_just_labelled_ones(self):
        self.assertNotIn("Fortification", DEFENCES)
        self.assertNotIn("TRAIT_FORTIFICATION", DEFENCES)
        self.assertIn('FLAT_SLOT = "slotflatsmall"', DEFENCES)
        self.assertIn("slotName.IndexOf(FLAT_SLOT) != -1", defence("protected bool TryPlace("))
        self.assertNotIn("SlotFlatMedium", DEFENCES)
        self.assertNotIn("SlotFlatLarge", DEFENCES)

    def test_slots_without_prefab_data_are_typed_by_their_editor_mesh(self):
        body = defence("protected string SlotName(")
        self.assertIn("INF_BuildingGrid.PrefabNameLower(slot)", body)
        self.assertIn("child.GetVObject().GetResourceName()", body)
        self.assertIn("name.ToLower()", body)
        self.assertIn("SlotName(slot)", defence("protected bool CollectSlot("))
        self.assertIn("SlotName(slot)", defence("protected void Select("))
        self.assertNotIn("PrefabNameLower", defence("protected bool TryPlace("))

    def test_small_flat_slots_are_counted_and_logged_for_diagnosis(self):
        self.assertIn("m_iFlatOccupied++", defence("protected bool CollectSlot("))
        self.assertIn("already occupied", defence("protected void Select("))

    def test_defences_keep_their_distance_from_others_of_the_same_kind(self):
        place = defence("protected bool TryPlace(")
        self.assertIn("if (!SpacingClear(position, flat))", place)
        self.assertIn("defence.m_bFlat = flat", place)
        spacing = defence("protected bool SpacingClear(")
        self.assertIn("existing.m_bFlat == flat", spacing)
        self.assertIn("spacing = FLAT_SPACING", spacing)
        self.assertIn("static const float FLAT_SPACING = 100;", DEFENCES)

    def test_small_flat_guard_groups_are_two_to_four_road_ones_use_the_setting(self):
        body = defence("static void AppendGuards(")
        self.assertIn("FLAT_MIN_GUARDS, FLAT_MAX_GUARDS + 1", body)
        self.assertIn("int full = guards;", body)
        self.assertIn("static const int FLAT_MIN_GUARDS = 2;", DEFENCES)
        self.assertIn("static const int FLAT_MAX_GUARDS = 4;", DEFENCES)
        self.assertIn("defence.m_bFlat = SlotType(defence.m_Composition) == FLAT_SLOT", defence("protected bool Restore("))

    def test_the_percentage_is_a_mission_header_option(self):
        self.assertIn("int m_iINFDefencePercent = -1;", GRID)
        self.assertIn('INF_HeaderSettings.Int(params.m_iINFDefencePercent, 0, 100, "m_iINFDefencePercent", m_iINFDefencePercent)', GRID)
        self.assertIn("config.m_iPercent <= 0", defence("void Begin("))


class DefenceStreaming(unittest.TestCase):
    def test_streaming_uses_hysteresis_and_the_zone_deactivation_delay(self):
        self.assertIn("DESPAWN_DISTANCE = 4000", DEFENCES)
        self.assertIn("SPAWN_DISTANCE = 3000", DEFENCES)
        body = method_body(DEFENCES, "protected void MaintainOne")
        self.assertIn("INF_BlockRules.DESPAWN_DELAY_MS", body)
        self.assertIn("IsOwnerActive(defence)", body)
        self.assertIn("FullyDestroyed(defence.m_Entity)", body)

    def test_the_loop_checks_the_whole_roster_and_only_queues_the_work(self):
        body = method_body(DEFENCES, "void Maintain(")
        self.assertNotIn("System.GetTickCount", body)
        self.assertIn("MaintainOne(m_Sites[index]", body)
        self.assertIn("CallLater(RespawnQueued, INF_StaggerGate.DefenceSpawns().Reserve()", body)
        self.assertIn("CallLater(DespawnQueued, INF_StaggerGate.DefenceDespawns().Reserve()", method_body(DEFENCES, "protected void MaintainOne"))
        self.assertNotIn("Respawn(", body)
        self.assertNotIn("Despawn(", body)
        self.assertIn("m_INFDefences.Maintain(presence.m_AllPlayers, now, presence.m_PlayerIDs.IsEmpty())", GRID)

    def test_queued_despawn_rechecks_for_a_player_who_arrived_while_waiting(self):
        body = method_body(DEFENCES, "protected void DespawnQueued")
        self.assertIn("DESPAWN_DISTANCE * DESPAWN_DISTANCE", body)
        self.assertIn("IsOwnerActive(defence)", body)

    def test_start_and_empty_server_sweep_everything_far_from_a_base(self):
        sweep = method_body(DEFENCES, "void SweepToBases(")
        self.assertIn("INF_SpawnProtection.BasePositions(bases)", sweep)
        self.assertIn("SPAWN_DISTANCE * SPAWN_DISTANCE", sweep)
        self.assertNotIn("CallLater", sweep)
        self.assertIn('SweepToBases("server empty")', method_body(DEFENCES, "void Maintain("))
        self.assertEqual(DEFENCES.count('SweepToBases("start")'), 2)

    def test_respawn_shares_the_restore_spawn_path(self):
        self.assertIn("SpawnComposition(defence)", method_body(DEFENCES, "protected bool Respawn"))
        self.assertIn("SpawnComposition(defence)", method_body(DEFENCES, "protected bool Restore"))


class DefenceOwnership(unittest.TestCase):
    def test_owner_is_the_nearest_activatable_zone_in_range_with_room(self):
        body = defence("INS_AmbientPatrolZoneState FindOwner(")
        self.assertIn("m_Config.m_fMaxOwnerDistance * m_Config.m_fMaxOwnerDistance", body)
        self.assertIn("zone.m_bINFPermanent || zone.m_bINFMarkedCleared", body)
        self.assertIn("zone.m_aINFDefences.Count() >= m_Config.m_iPerZone", body)
        self.assertIn("distanceSq > bestSq", body)

    def test_a_defence_with_no_owner_is_not_placed(self):
        body = defence("protected bool TryPlace(")
        self.assertRegex(body, r"if \(!owner\)\s*\{\s*m_iOwnerRejects\+\+;\s*return false;")
        self.assertLess(body.index("FindOwner(position)"), body.index("PlaceInSlot(slot, composition)"))

    def test_defences_stay_out_of_the_camp_and_cache_systems(self):
        for forbidden in ("AddCache", "AddCamp", "RegisterSite", "AppendCandidates", "INF_CaptureObjectiveCells"):
            self.assertNotIn(forbidden, DEFENCES)
        self.assertNotIn("m_aINFDefences", method_body(GRID, "void INF_CollectCaptureObjectives("))


class DefencePersistence(unittest.TestCase):
    def test_roster_is_saved_once_and_recovered_when_the_cache_save_exists(self):
        begin = defence("void Begin(")
        self.assertIn("FileIO.FileExists(INS_CACHE_SAVE_FILE) && FileIO.FileExists(SAVE_FILE)", begin)
        self.assertLess(begin.index("Recover();"), begin.index("ResolveCompositions();"))
        self.assertIn('"$profile:INS_Saves/defence_sites.dat"', DEFENCES)
        select = defence("protected void PlaceDefences(")
        self.assertIn("!m_Sites.IsEmpty() && !Save()", select)

    def test_baked_slot_cache_is_the_fallback_when_the_world_has_no_slot_entities(self):
        select = defence("protected void Select(")
        self.assertIn("m_SeenSlotNames.IsEmpty()", select)
        self.assertIn("UseBakedSlots()", select)
        baked = defence("protected void UseBakedSlots(")
        self.assertIn("INF_DefenceSlotCache.Load(m_Config.m_sSlotCache)", baked)
        self.assertIn("m_SlotTypes.Find(type) < 0", baked)
        self.assertIn("GetSurfaceY", baked)
        self.assertIn("PlaceDefences(candidates)", baked)
        self.assertIn("No slot cache is configured", baked)
        place = defence("protected INF_Defence PlaceInSlot(")
        self.assertIn("SpawnAt(resource, candidate.m_vPosition, candidate.m_fYaw)", place)
        for removed in ("Sphere", "ProbeKnownSlot", "WatchForPlayer", "CollectEditableSlots"):
            self.assertNotIn(removed, DEFENCES)
        self.assertIn("m_sINFDefenceSlotCache", GRID)
        self.assertNotIn("params.m_sINFDefenceSlotCache", GRID)
        self.assertIn("config.m_sSlotCache = m_sINFDefenceSlotCache", GRID)

    def test_the_roster_is_deleted_on_a_clean_mission_end(self):
        self.assertIn("FileIO.DeleteFile(INF_Defences.SAVE_FILE)", REWARDS)

    def test_zero_defences_never_blocks_startup_or_changes_zone_behaviour(self):
        begin = defence("void Begin(")
        self.assertIn("config.m_iPercent <= 0", begin)
        self.assertEqual(begin.count("m_bReady"), 0)
        self.assertNotIn("m_bReady", DEFENCES)
        self.assertRegex(defence("static bool GuardsAlive("), r"foreach \(INS_PatrolAgent agent : state\.m_aAgents\)")
        self.assertIn("foreach (INF_Defence defence : state.m_aINFDefences)", defence("static void AppendGuards("))

    def test_roster_stores_everything_needed_to_respawn_and_reattach(self):
        body = defence("protected bool Save(")
        for field in ("m_sKey", "composition", "m_vPosition", "m_fYaw", "m_sOwnerKey"):
            self.assertIn(field, body)
        self.assertIn("file.Write(SAVE_VERSION)", body)

    def test_recovery_fails_closed_and_preserves_the_file(self):
        body = defence("protected void Recover(")
        self.assertIn("version != SAVE_VERSION", body)
        self.assertIn("count > MAX_SITES", body)
        self.assertIn("ValidVector(", body)
        self.assertNotIn("FileMode.WRITE", body)
        self.assertNotIn("DeleteFile", DEFENCES)

    def test_restore_respawns_at_the_saved_transform(self):
        body = defence("protected IEntity SpawnComposition(")
        self.assertIn("SpawnAt(resource, defence.m_vPosition, defence.m_fYaw)", body)
        spawn = defence("protected static IEntity SpawnAt(")
        self.assertIn("Math3D.AnglesToMatrix(Vector(yaw, 0, 0), params.Transform)", spawn)
        self.assertIn("params.Transform[3] = position", spawn)
        self.assertIn("SetSlotOccupant(defence.m_vPosition, entity)", body)
        self.assertNotIn("SCR_SiteSlotEntity", body)
        self.assertIn("SpawnComposition(defence)", defence("protected bool Restore("))

    def test_restore_reassigns_a_missing_owner_zone(self):
        body = defence("protected bool Restore(")
        self.assertIn("m_Zones.GetZone(defence.m_sOwnerKey)", body)
        self.assertIn("FindOwner(defence.m_vPosition, true)", body)

    def test_compositions_are_untracked_by_engine_persistence_and_hold_no_ai(self):
        self.assertEqual(DEFENCES.count("INF_ForestCamps.PrepareComposition(entity)"), 2)

    def test_defences_never_block_camp_startup_and_reuse_the_terrain_scan(self):
        body = grid("void INF_StaticCampLayoutsReady(")
        self.assertNotIn("INF_DefencesReady", body)
        self.assertLess(body.index("m_bINFCampsReady = true"), body.index("INF_StartDefences()"))
        self.assertIn("m_INFDefences.OfferSlot(entity)", grid("protected bool INF_IndexBuilding("))
        self.assertIn("m_INFDefences.SlotsScanned()", grid("protected void INF_ScanMapTick("))
        self.assertIn("if (m_bScanned)", defence("void Begin("))

    def test_scan_stops_when_the_mission_ends(self):
        self.assertIn("m_INFDefences.Stop()", grid("override void OnGameModeEnd("))
        self.assertIn("Remove(ScanTick)", defence("void Stop("))


class DefenceGuards(unittest.TestCase):
    def test_guards_join_the_plan_so_budget_and_reservation_include_them(self):
        body = grid("override protected void ActivateZone(")
        append = body.index("INF_Defences.AppendGuards(")
        plan = body.index("INF_BuildingGrid.Plan(limitAI - fromZone")
        group_count = body.index("state.m_iGroupCount = state.m_aINFPlan.Count()")
        planned = body.index("state.m_iINFPlannedAI += planned")
        make_room = body.index("INF_MakeRoom(state)")
        consume = body.index("INF_Defences.ConsumeMemory(state)")
        self.assertTrue(append < plan < group_count < planned < make_room < consume)
        self.assertIn("m_iINFDefenceGuards", body)

    def test_guards_are_drawn_from_the_zone_only_above_the_roaming_minimum(self):
        body = grid("override protected void ActivateZone(")
        self.assertIn("Math.Min(Math.Max(limitAI - INF_Defences.MIN_ROAMING, 0), guardAI)", body)
        self.assertIn("static const int MIN_ROAMING = 6;", DEFENCES)

        def split(zone, guards):
            taken = min(max(zone - 6, 0), guards)
            return zone - taken, guards - taken

        self.assertEqual(split(8, 4), (6, 2))
        self.assertEqual(split(2, 4), (2, 4))
        self.assertEqual(split(16, 4), (12, 0))

    def test_guards_spawn_at_the_defence_and_saved_positions_only_index_ordinary_groups(self):
        body = grid("override protected void ActivateZone(")
        self.assertIn("state.m_aINFActiveDefences[i - (state.m_iGroupCount - guardGroups)].m_vPosition", body)
        self.assertGreater(body.index("state.m_aSavedPositions[i]"), body.index("RandomPositionInRadius"))

    def test_guard_group_is_an_agent_of_the_owner_zone_standing_at_the_defence(self):
        body = grid("override protected void SpawnGroupAt(")
        self.assertIn("INF_FindDefenceGuardPosition(guardedDefence)", body)
        self.assertIn("INF_Defences.GUARD_RADIUS", body)
        self.assertIn("agent.m_INFDefence = guardedDefence", body)
        self.assertIn("state.m_aAgents.Insert(agent)", body)
        self.assertIn("INF_Defence m_INFDefence;", BLOCK)

    def test_guards_hold_the_defence_under_a_defend_waypoint_that_uses_turrets(self):
        spawn = grid("override protected void SpawnGroupAt(")
        self.assertRegex(spawn, r"if \(guardedDefence\)\s*m_CallQueue\.CallLater\(INF_DefendPosition, 1500, false, agent\);\s*else\s*m_CallQueue\.CallLater\(PatrolTick")
        defend = grid("protected void INF_DefendPosition(")
        self.assertIn("agent.m_Group.AddWaypoint(agent.m_INFDefendWaypoint)", defend)
        waypoint = defence("static AIWaypoint SpawnDefendWaypoint(")
        self.assertIn("SetUseTurrets(true)", waypoint)
        self.assertIn("SetCompletionRadius(DEFEND_RADIUS)", waypoint)
        self.assertIn("AIWaypoint_Defend.et", DEFENCES)

    def test_the_guard_waypoint_is_deleted_with_its_group(self):
        despawn = grid("protected void INF_DespawnDefenderGroup(")
        self.assertLess(despawn.index("agent.m_INFDefendWaypoint"), despawn.index("if (!group)"))
        self.assertIn("AIWaypoint m_INFDefendWaypoint;", BLOCK)

    def test_guard_spawn_does_not_force_the_cell_containment_rule(self):
        body = grid("protected vector INF_FindDefenceGuardPosition(")
        self.assertNotIn("INF_BuildingGrid.Contains", body)
        self.assertIn("INF_BlockRules.MIN_GRID_SPAWN_DISTANCE", body)

    def test_spawn_queue_maps_the_plan_index_to_its_defence(self):
        body = grid("protected void INF_SpawnQueued(")
        self.assertIn("INF_Defences.PlannedGuard(state, planIndex)", body)

    def test_guard_count_defaults_to_four_in_a_range(self):
        self.assertIn("protected int m_iINFDefenceGuards = 4;", GRID)
        self.assertIn('"2 8 1"', GRID)

    def test_guards_do_not_count_as_ordinary_survivors_but_are_remembered_separately(self):
        count = grid("protected int INF_CountDefenders(")
        self.assertIn("agent.m_INFDefence", count)
        self.assertIn("INF_Defences.OrdinaryGroups(state)", count)
        deactivate = grid("override protected void DeactivateZone(")
        self.assertIn("INF_Defences.ExtractGuards(state, guards)", deactivate)
        self.assertIn("INF_Defences.RememberGuards(", deactivate)
        self.assertIn("INF_Defences.OrdinaryPlannedAI(state)", deactivate)
        self.assertIn("INF_DespawnDefenderGroup, m_INFDespawnGate.Reserve(), false, guard", deactivate)
        self.assertLess(deactivate.index("ExtractGuards"), deactivate.index("super.DeactivateZone(state)"))

    def test_remembered_guards_keep_a_wiped_out_zone_activatable(self):
        body = grid("override protected void ActivateZone(")
        self.assertIn("state.m_iINFRememberedAI == 0 && !INF_Defences.HasRememberedGuards(state, activationTime)", body)

    def test_a_guard_group_that_never_spawned_keeps_its_previous_memory(self):
        body = defence("static void RememberGuards(")
        self.assertRegex(body, r"if \(!spawned\)\s*continue;")

    def test_zone_capture_waits_for_living_guards_wherever_they_stand(self):
        body = method_body(ZONES, "void CheckCapture(")
        self.assertIn("!INF_Defences.GuardsAlive(state, presence)", body)
        guards = defence("static bool GuardsAlive(")
        self.assertIn("agent.m_INFDefence", guards)
        self.assertNotIn("m_vCenter", guards)


if __name__ == "__main__":
    unittest.main()
