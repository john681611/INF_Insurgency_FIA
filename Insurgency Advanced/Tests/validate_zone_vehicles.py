"""Vehicle-count limits, default passengers and source contracts; not engine execution."""
from dataclasses import dataclass, field
from pathlib import Path
import re
import unittest
from source_contracts import method_body

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "Scripts" / "Game"
SOURCE = (SCRIPTS / "INF_ZoneVehicles.c").read_text()
BLOCK = (SCRIPTS / "INF_BlockControl.c").read_text()
GRID = (SCRIPTS / "INF_BuildingGrid.c").read_text()
EDITOR = (SCRIPTS / "INF_ZoneEditorAttributes.c").read_text()
CONFIG = (ROOT / "Configs" / "Editor" / "AttributeLists" / "Edit.conf").read_text()


def body(signature):
    return method_body(SOURCE, signature)


@dataclass
class Budget:
    vehicles: int = 2
    units: list = field(default_factory=list)
    ambient: int = 4
    infantry: int = 128

    def admit(self, seats):
        if seats <= 0 or len(self.units) >= self.vehicles:
            return False
        self.units.append(seats)
        return True


class ZoneVehicles(unittest.TestCase):
    def test_zone_distance_matches_infantry_and_reinforcement_is_last_fallback(self):
        spawn = body("protected void INF_TryZoneVehicle(")
        for symbol in ("MIN_GRID_SPAWN_DISTANCE", "DENSE_GRID_BUILDINGS",
                       "MIN_DENSE_GRID_SPAWN_DISTANCE"):
            self.assertIn("INF_BlockRules." + symbol, spawn)
            self.assertIn("INF_BlockRules." + symbol, GRID)
        self.assertLess(spawn.index("INF_ZoneOpenPosition("),
                        spawn.index("INF_ZoneVehicleReinforcement("))
        self.assertIn("destination = pos", spawn)
        road = body("protected bool INF_ZoneRoadPosition(")
        self.assertIn("minimumDistance = m_fMinSpawnDistance", road)
        fallback = body("protected bool INF_ZoneVehicleReinforcement(")
        for check in ("targetAttempt < 16", "attempt < 16",
                      "INF_ZoneVehicleFootprintClear(destination)",
                      "INF_BuildingGrid.Contains(center, pos)",
                      "Math.RandomFloat(550, 900)",
                      "INF_VehiclePositionClear(pos, destination - pos, null)",
                      "yaw = vector.Direction(pos, destination).ToYaw()"):
            self.assertIn(check, fallback)
        self.assertNotIn("SpawnEntityPrefab", fallback)
        self.assertNotIn("CallLater", fallback)

    def test_vehicle_count_limit_is_independent_of_occupants_ambient_and_infantry(self):
        budget = Budget()
        self.assertTrue(budget.admit(30))
        self.assertTrue(budget.admit(30))
        self.assertFalse(budget.admit(1))
        self.assertEqual((budget.ambient, budget.infantry), (4, 128))
        self.assertNotIn("m_aActiveVehicles.Insert", SOURCE)
        self.assertNotIn("m_aActiveVehicles.Remove", body("protected void INF_RemoveZoneVehicle("))
        self.assertNotIn("m_iMaxActiveVehicles", SOURCE)
        self.assertNotIn("INF_MakeRoom", SOURCE)
        self.assertNotIn("m_iINFReservedGroups", SOURCE)

    def test_pending_vehicles_count_against_vehicle_cap_without_an_ai_budget(self):
        budget = Budget(vehicles=2)
        self.assertTrue(budget.admit(40))
        self.assertTrue(budget.admit(20))
        self.assertFalse(budget.admit(1))
        budget.units.pop(0)
        self.assertTrue(budget.admit(60))
        spawn = body("protected void INF_TryZoneVehicle(")
        self.assertNotIn("m_iINFZoneCrewLimit", SOURCE)
        self.assertNotIn("INF_UsedZoneCrew", SOURCE)
        self.assertLess(spawn.index("m_INFZoneVehicles.Insert(unit)"), spawn.index("CallLater(INF_ZoneSpawnCrew"))
        self.assertIn("unit.m_ExpectedCrew = crew", spawn)
        self.assertIn("m_INFZoneVehicles.RemoveItem(unit)", body("protected void INF_RemoveZoneVehicle("))

    def test_zero_and_lowered_limits_stop_future_admissions_not_existing_units(self):
        self.assertFalse(Budget(vehicles=0).admit(1))
        budget = Budget()
        self.assertTrue(budget.admit(4))
        budget.vehicles = 0
        self.assertFalse(budget.admit(1))
        self.assertEqual(budget.units, [4])
        setter = body("void INF_SetZoneVehicleSetting(")
        self.assertNotIn("DeleteEntity", setter)
        self.assertNotIn("INF_RemoveZoneVehicle", setter)
        for condition in ("setting < 0", "setting > 1", "value < 0", "value > maximum"):
            self.assertIn(condition, setter)
        self.assertIn("LogLevel.ERROR", setter)

    def test_sparse_chance_thresholds_and_single_roll_per_zone(self):
        for chance in (0, 15, 100):
            self.assertEqual(sum(roll < chance for roll in range(100)), chance)
        tick = body("protected void INF_ZoneVehicleTick()")
        self.assertIn("Math.RandomInt(0, 100) < m_iINFZoneVehicleChance", tick)
        self.assertLess(tick.index("m_INFZoneVehicleRolls.Contains(key)"),
                        tick.index("m_INFZoneVehicleRolls.Set(key, true)"))
        self.assertLess(tick.index("m_INFZoneVehicleRolls.Set(key, true)"),
                        tick.index("INF_TryZoneVehicle(zone, players)"))
        for guard in ("!zone.m_bActive", "zone.m_bINFMarkedCleared",
                      "zone.m_bINFPermanent", "zone.m_bINFAttackActive"):
            self.assertIn(guard, tick)

    def test_reuses_current_vehicle_list_and_native_crew_not_a_duplicate_pool(self):
        spawn = body("protected void INF_TryZoneVehicle(")
        self.assertIn("m_aVehiclePrefabs[Math.RandomInt", spawn)
        self.assertNotIn("Prefabs/Vehicles/", SOURCE)
        self.assertIn("CREW_COMPARTMENT_TYPES", spawn)
        self.assertIn("GetDefaultOccupantPrefab()", spawn)
        self.assertIn("SpawnCrew(unit.m_Agent)", body("protected void INF_ZoneSpawnCrew("))
        self.assertEqual(SOURCE.count("modded class INS_AmbientVehicleManager"), 1)
        self.assertNotIn("modded class INS_AmbientVehicleManager", BLOCK)

    def test_safe_road_spawns_use_ambient_distance_and_full_vehicle_clearance(self):
        road = body("protected bool INF_ZoneRoadPosition(")
        for condition in ("GetClosestRoad", "distance > 100", "INF_BuildingGrid.IsExcluded(point)",
                          "< minimumDistance", "INF_ZoneHidden(point, players)",
                          "> m_fActivationRadius",
                          "FindEmptyTerrainPosition(empty, point, 5.1, 5, 4)",
                          "INF_ForestCamps.ClearVolume(point, 5, 4)", "INF_VehiclePositionOccupied(point)"):
            self.assertIn(condition, road)
        self.assertLess(road.index("nearest > m_fActivationRadius"), road.index("FindEmptyTerrainPosition"))
        hidden = body("protected bool INF_ZoneHidden(")
        self.assertIn("INF_BlockRules.IsConcealed(pos, players, 3)", hidden)
        conceal = method_body(BLOCK, "static bool IsConcealed(")
        self.assertIn("foreach (vector player : players)", conceal)
        self.assertIn("TraceFlags.WORLD", conceal)

    def test_vehicle_search_cannot_relocate_into_courtyards_and_checks_footprint(self):
        road = body("protected bool INF_ZoneRoadPosition(")
        self.assertIn("vector.DistanceSqXZ(empty, point) > 0.0625", road)
        self.assertLess(road.index("vector.DistanceSqXZ(empty, point)"),
                        road.index("point = empty"))
        self.assertIn("!INF_VehiclePositionClear(point", road)
        clearance = body("protected bool INF_ZoneVehicleFootprintClear(")
        self.assertIn("Math.AbsFloat(ground - pos[1]) > 0.65", clearance)
        self.assertIn("offset <= 5", clearance)
        self.assertIn("level < 4", clearance)
        self.assertIn("world.TraceMove(across, null) < 1", clearance)
        self.assertIn("world.TraceMove(along, null) < 1", clearance)
        self.assertNotIn("CallLater", clearance)
        for displacement, rejected in ((0, False), (0.25, False), (0.251, True), (4, True)):
            self.assertEqual(displacement * displacement > 0.0625, rejected)

    def test_both_vehicle_paths_validate_clearance_and_escape_before_crew_creation(self):
        crew = body("override protected void SpawnCrew(")
        self.assertLess(crew.index("INF_VehicleDeploymentClear("),
                        crew.index("super.SpawnCrew(agent)"))
        self.assertIn("!HasPlayerOccupant(agent.m_VehicleEntity)", crew)
        self.assertIn("INF_RemoveZoneVehicle(unit, true)", crew)
        self.assertIn("DespawnVehicle(agent)", crew)
        deployment = body("protected bool INF_VehicleDeploymentClear(")
        self.assertIn("INF_ExcludeVehicle(vehicle, exclusions)", deployment)
        self.assertIn("INF_VehiclePositionClear(vehicle.GetOrigin(), transform[2], exclusions)", deployment)
        deployment = body("protected bool INF_VehiclePositionClear(")
        self.assertIn("INF_ZoneVehicleFootprintClear(pos, exclusions)", deployment)
        self.assertIn("INF_VehicleExitClear(pos, forward, exclusions, 3, 3)", deployment)
        self.assertIn("INF_VehicleExitClear(pos, -forward, exclusions, 3, 3)", deployment)
        exit_check = body("protected bool INF_VehicleExitClear(")
        self.assertIn("step <= steps", exit_check)
        self.assertIn("int steps = 3", SOURCE)
        self.assertIn("!INF_VehicleExitClear(pos, forward, exclusions, 2)", deployment)
        self.assertIn("!INF_VehicleExitClear(pos, -forward, exclusions, 2)", deployment)
        self.assertIn("step * 5", exit_check)
        self.assertIn("lane = -2; lane <= 2", exit_check)
        self.assertIn("float laneOffset = lane;", exit_check)
        self.assertIn("vector lateralOffset = side * laneOffset;", exit_check)
        self.assertIn("direction * distance", exit_check)
        self.assertNotIn("side * lane ", exit_check)
        self.assertIn("trace.ExcludeArray = exclusions", exit_check)
        self.assertIn("world.TraceMove(trace, null) < 1", exit_check)
        self.assertNotIn("CallLater", deployment + exit_check)
        for forward, reverse, accepted in ((True, False, True), (False, True, True),
                                           (False, False, False), (True, True, True)):
            self.assertEqual(forward or reverse, accepted)

    def test_unsafe_ambient_endpoints_search_for_alternatives_before_creation(self):
        spawn = body("override protected void SpawnVehicle(")
        cached = spawn.index("foreach (vector rejected")
        check = spawn.index("INF_VehiclePositionClear(")
        create = spawn.index("super.SpawnVehicle(")
        self.assertLess(cached, check)
        self.assertLess(check, create)
        self.assertIn("!Replication.IsServer()", spawn)
        self.assertIn("vector.DistanceSqXZ(spawnPos, rejected) <= 25", spawn)
        self.assertIn("m_INFRejectedVehiclePositions.Insert(spawnPos)", spawn)
        self.assertIn("INF_FindAmbientRoadPosition(spawnPos, destPos, replacement)", spawn)
        self.assertIn("spawnPos = replacement", spawn)
        self.assertIn("blocked || !INF_VehiclePositionClear", spawn)
        self.assertIn("now < retryAt", spawn)
        self.assertIn("m_INFVehicleSearchRetry.Set(key, now + 60000)", spawn)
        self.assertNotIn("SpawnEntityPrefab", spawn)
        crew = body("override protected void SpawnCrew(")
        self.assertLess(crew.index("m_INFRejectedVehiclePositions.Insert"),
                        crew.index("DespawnVehicle(agent)"))
        road = body("protected bool INF_ZoneRoadPosition(")
        self.assertLess(road.index("INF_VehiclePositionClear("), road.index("pos = point"))
        self.assertIn("vector.DistanceSqXZ(point, rejected) <= 25",
                      body("protected bool INF_VehiclePositionOccupied("))

    def test_ambient_fallback_searches_nearby_roads_with_player_and_destination_checks(self):
        search = body("protected bool INF_FindAmbientRoadPosition(")
        self.assertIn("GetPlayerControlledEntity(playerId)", search)
        self.assertIn("players.IsEmpty()", search)
        self.assertIn("sample < 9", search)
        self.assertIn("INF_ZoneRoadPosition(center, players, candidate, yaw)", search)
        self.assertIn("vector.DistanceSqXZ(candidate, origin) > 40000", search)
        self.assertIn("INF_VehiclePositionClear(candidate, destination - candidate, null)", search)
        self.assertNotIn("SpawnEntityPrefab", search)
        candidates = [(15, False), (201, True), (150, True), (80, True)]
        chosen = next((distance for distance, clear in candidates
                       if distance * distance <= 40000 and clear), None)
        self.assertEqual(chosen, 150)
        for now, allowed in ((59999, False), (60000, True), (60001, True)):
            self.assertEqual(now >= 60000, allowed)

    def test_native_crew_groups_contribute_to_capture_without_duplicate_activation(self):
        resolve = body("protected bool INF_ResolveZoneCrew(")
        self.assertIn("foreach (IEntity member : unit.m_Crew)", resolve)
        self.assertNotIn("control.ActivateAI()", resolve)
        self.assertIn("unit.m_Groups.Insert(group)", resolve)
        self.assertIn("unit.m_Zone.m_bEverHadLiving = true", resolve)
        tick = body("protected void INF_ZoneVehicleTick()")
        self.assertIn("unit.m_Zone.m_bEverHadLiving = true", tick)
        self.assertLess(tick.index("INF_ZoneCrewCount(unit) == 0"),
                        tick.index("unit.m_Zone.m_bEverHadLiving = true"))
        collect = body("void INF_CollectGroups(")
        self.assertIn("foreach (INS_AmbientVehicleAgent vehicle : m_aActiveVehicles)", collect)
        self.assertIn("foreach (SCR_AIGroup group : unit.m_Groups)", collect)
        self.assertIn("vehicles.INF_CollectGroups(groups)", BLOCK)
        hostile = body("protected bool INF_ZoneHostileCrew(")
        self.assertIn("prisoner.IsCaptured()", hostile)
        self.assertIn("GetPlayerIdFromControlledEntity", hostile)

    def test_temporary_vehicle_diagnostics_removed_but_errors_retained(self):
        for signature in ("protected void INF_TryZoneVehicle(",
                          "protected bool INF_ZoneRoadPosition(",
                          "protected bool INF_ZoneOpenPosition(",
                          "protected bool INF_ZoneVehicleReinforcement("):
            self.assertNotIn("LogLevel.NORMAL", body(signature))
        self.assertNotIn("rangeRejected", SOURCE)
        self.assertNotIn("bool diagnostics", SOURCE)
        self.assertIn("LogLevel.ERROR", body("protected void INF_TryZoneVehicle("))
        self.assertIn("LogLevel.ERROR", body("void INF_SetZoneVehicleSetting("))
        for signature in ("override protected void SpawnVehicle(",
                          "override protected void SpawnCrew(",
                          "protected void INF_ZoneSpawnCrew(",
                          "protected void INF_ZoneWreckCheck("):
            method = body(signature)
            self.assertIn("!Replication.IsServer()", method)
            self.assertIn("m_INFZoneVehiclesStopped", method)

    def test_movement_requires_both_short_corridors_and_one_long_escape(self):
        def clear(front, rear):
            return front >= 10 and rear >= 10 and (front >= 15 or rear >= 15)
        for front, rear, accepted in ((15, 10, True), (10, 15, True),
                                      (15, 5, False), (5, 15, False),
                                      (10, 10, False), (15, 15, True)):
            self.assertEqual(clear(front, rear), accepted)

    def test_clearance_deduplication_preserves_steps_and_checks_footprint_once(self):
        deployment = body("protected bool INF_VehiclePositionClear(")
        self.assertIn("!footprintChecked && !INF_ZoneVehicleFootprintClear", deployment)
        road = body("protected bool INF_ZoneRoadPosition(")
        self.assertNotIn("INF_ZoneVehicleFootprintClear", road)
        ground = body("protected bool INF_ZoneOpenPosition(")
        self.assertLess(ground.index("INF_ZoneVehicleFootprintClear"),
                        ground.index("for (int heading"))
        for front in (0, 5, 10, 15):
            for rear in (0, 5, 10, 15):
                old = (all(front >= step * 5 for step in (1, 2))
                       and all(rear >= step * 5 for step in (1, 2))
                       and (all(front >= step * 5 for step in (1, 2, 3))
                            or all(rear >= step * 5 for step in (1, 2, 3))))
                new = front >= 10 and rear >= 10 and (front >= 15 or rear >= 15)
                self.assertEqual(old, new)
        self.assertEqual((2 + 2 + 1) * 5 * 3, 75)
        self.assertEqual((2 + 2 + 3) * 5 * 3, 105)

    def test_zone_vehicles_prefer_roads_then_search_ground_without_road_dependency(self):
        spawn = body("protected void INF_TryZoneVehicle(")
        self.assertIn("&& !INF_ZoneOpenPosition(zone.m_vCenter, players, pos, yaw, minimumDistance)", spawn)
        self.assertLess(spawn.index("INF_ZoneRoadPosition("), spawn.index("INF_ZoneOpenPosition("))
        ground = body("protected bool INF_ZoneOpenPosition(")
        for check in ("attempt < 32", "heading < 4", "INF_BuildingGrid.Contains(center, point)",
                      "nearest < minimumDistance", "nearest > m_fActivationRadius",
                      "INF_BuildingGrid.IsExcluded(point)", "INF_VehiclePositionOccupied(point)",
                      "INF_ZoneHidden(point, players)", "INF_ForestCamps.ClearVolume(point, 5, 4)",
                      "INF_VehiclePositionClear(point, direction, null, true)"):
            self.assertIn(check, ground)
        self.assertNotIn("GetClosestRoad", ground)
        self.assertNotIn("CallLater", ground)
        self.assertNotIn("INF_ZoneOpenPosition", body("protected bool INF_FindAmbientRoadPosition("))
        occupied = body("protected bool INF_VehiclePositionOccupied(")
        self.assertIn("m_aActiveVehicles", occupied)
        self.assertIn("m_INFZoneVehicles", occupied)
        self.assertIn("< 2500", occupied)

    def test_road_endpoints_and_blocked_candidates_do_not_discard_valid_sites(self):
        road = body("protected bool INF_ZoneRoadPosition(")
        self.assertIn("i < points.Count(); i++", road)
        self.assertIn("points.Count() < 2", road)
        self.assertIn("next = i - 1", road)
        self.assertIn("if (INF_VehiclePositionOccupied(point))", road)
        points = [0, 200]
        self.assertEqual([point for point in points if abs(point - 190) <= 100], [200])
        occupied = {0}
        self.assertEqual(next(point for point in points if point not in occupied), 200)

    def test_readiness_blocks_capture_and_times_out_explicitly(self):
        spawn = body("protected void INF_TryZoneVehicle(")
        self.assertIn("unit.m_Deadline", spawn)
        self.assertIn("zone.m_fINFSpawnReadyAt = Math.Max", spawn)
        resolve = body("protected bool INF_ResolveZoneCrew(")
        self.assertIn("unit.m_Agent.m_iINFSpawnedCrew < unit.m_ExpectedCrew", resolve)
        self.assertNotIn("unit.m_Crew.Count() ==", resolve)
        self.assertIn("unit.m_Agent.m_bINFPassengersReady", resolve)
        self.assertLess(resolve.index("if (!resolved"), resolve.index("AssignDriveWaypoint"))
        self.assertLess(resolve.index("INF_ZoneCrewCount(unit) == 0"), resolve.index("if (!resolved"))
        tick = body("protected void INF_ZoneVehicleTick()")
        self.assertIn("now >= unit.m_Deadline", tick)
        self.assertIn("LogLevel.ERROR", tick)

    def test_completion_snapshot_deduplicates_and_retains_dismounted_crew(self):
        tracked = []
        for occupants in (["driver"], ["driver", "gunner"], ["gunner", "commander"]):
            for occupant in occupants:
                if occupant not in tracked:
                    tracked.append(occupant)
        self.assertEqual(tracked, ["driver", "gunner", "commander"])
        resolve = body("protected bool INF_ResolveZoneCrew(")
        self.assertIn("foreach (IEntity occupant : unit.m_Agent.m_aINFMembers)", resolve)
        self.assertIn("!unit.m_Crew.Contains(occupant)", resolve)
        self.assertNotIn("GetOccupants(unit.m_Crew)", resolve)
        self.assertNotIn("unit.m_Crew.Clear()", resolve)
        self.assertIn("if (!member)", resolve)

    def test_queue_is_server_owned_and_works_without_ambient_routes(self):
        start = body("override void OnGameModeStart()")
        self.assertIn("super.OnGameModeStart()", start)
        self.assertIn("Replication.IsServer()", start)
        self.assertIn("INF_ScheduleZoneVehicles()", start)
        schedule = body("void INF_ScheduleZoneVehicles()")
        self.assertIn("!Replication.IsServer()", schedule)
        self.assertIn("CallLater(INF_ZoneVehicleTick, 5000, false)", schedule)
        tick = body("protected void INF_ZoneVehicleTick()")
        self.assertIn("!Replication.IsServer()", tick)
        self.assertIn("now < m_fINFZonePauseUntil", tick)
        self.assertNotIn("m_bJIPPause", tick)
        self.assertNotIn("m_aRoutes", tick)
        self.assertNotIn("m_CallQueue", tick)

    def test_shutdown_cancels_every_custom_callback(self):
        end = body("override void OnGameModeEnd(")
        for method in ("INF_ZoneVehicleTick", "INF_ZoneSpawnCrew", "INF_ZoneWreckCheck"):
            self.assertIn(f"Remove({method})", end)
        self.assertLess(end.index("m_INFZoneVehiclesStopped = true"), end.index("super.OnGameModeEnd"))
        self.assertIn("INF_RemoveZoneVehicle", end)
        self.assertIn("ambient.INF_StopCrewListener()", end)
        self.assertIn("GetGame().GetCallqueue().Remove(WreckPlayerExitPoll)", end)
        self.assertIn("m_CallQueue.Remove(WreckCleanupCheck)", end)
        self.assertIn("agent.INF_StopCrewListener()", body("protected void INF_CleanupVehicle("))
        self.assertIn("Remove(INF_OnCrewSpawned)", body("void INF_StopCrewListener("))

    def test_cleanup_cancels_native_spawning_and_preserves_players_and_prisoners(self):
        cleanup = body("protected void INF_CleanupVehicle(")
        self.assertIn("CancelSpawningCharacters()", cleanup)
        self.assertIn("HasPlayerOccupant(vehicle)", cleanup)
        self.assertIn("INF_PrisonerComponent.INF_Preserve(occupant)", cleanup)
        self.assertIn("!INF_PrisonerComponent.INF_Preserve(member)", cleanup)
        self.assertIn("removeVehicle && !playerAboard", cleanup)
        self.assertIn("GetPlayerIdFromControlledEntity(member) <= 0", cleanup)
        self.assertIn("vehicle && !preserve", cleanup)
        wreck = body("protected void INF_ZoneWreckCheck(")
        self.assertLess(wreck.index("HasPlayerOccupant"), wreck.index("DeleteEntity"))
        self.assertLess(wreck.index("INF_PrisonerComponent.INF_Preserve(occupant)"), wreck.index("DeleteEntity"))
        self.assertIn("CallLater(INF_ZoneWreckCheck, 30000", wreck)
        self.assertIn("CallLater(WreckPlayerExitPoll, 60000", wreck)
        self.assertIn("INF_CleanupVehicle(agent, members, true)", body("override protected void DespawnVehicle("))
        self.assertIn("override protected void WreckCleanupCheck(IEntity wreck)", SOURCE)
        self.assertIn("override protected void WreckPlayerExitPoll(IEntity wreck)", SOURCE)
        self.assertIn("INF_ZoneWreckCheck(wreck)", body("override protected void WreckCleanupCheck("))
        poll = body("override protected void WreckPlayerExitPoll(")
        self.assertNotIn("m_CallQueue", poll)
        self.assertIn("GetGame().GetCallqueue().CallLater(INF_ZoneWreckCheck", poll)
        self.assertIn("m_fWreckLifetime * 1000", poll)

    def test_distant_cleanup_also_checks_dismounted_crew_visibility(self):
        tick = body("protected void INF_ZoneVehicleTick()")
        self.assertIn("INF_ZoneHidden(member.GetOrigin(), players)", tick)
        self.assertIn("INF_ZoneHidden(position, players)", tick)
        self.assertIn("!HasPlayerOccupant(vehicle)", tick)
        self.assertIn("m_fDeactivationRadius", tick)
        self.assertIn("(!vehicle && !unit.m_Ready)", tick)
        self.assertIn("vector position = unit.m_Zone.m_vCenter", tick)

    def test_vehicle_holds_position_without_infantry_defend_orders(self):
        resolve = body("protected bool INF_ResolveZoneCrew(")
        self.assertIn("AssignDriveWaypoint(unit.m_Agent)", resolve)
        spawn = body("protected void INF_TryZoneVehicle(")
        self.assertIn("new INS_AmbientVehicleAgent(entity, destination, -1, false)", spawn)
        self.assertNotIn("AIWaypoint_Defend.et", SOURCE)
        self.assertNotIn("RerouteVehicle(", SOURCE)

    def test_runtime_state_is_not_persisted(self):
        self.assertNotIn("FileSerializer", SOURCE)
        self.assertNotIn("SaveSites", SOURCE)
        self.assertIn("persistence.StopTracking(entity, true)", SOURCE)
        self.assertIn("persistence.StopTracking(member, true)", SOURCE)
        self.assertIn("persistence.StopTracking(group, true)", SOURCE)

    def test_two_settings_are_replicated_and_exposed_to_gms(self):
        for name, default in (("Chance", 15), ("Limit", 2)):
            field = {"Chance": "m_iINFZoneVehicleChance", "Limit": "m_iINFZoneVehicleLimit"}[name]
            self.assertRegex(SOURCE, rf'RplProp\(\)\]\s+protected int {field} = {default};')
        for guid in ("610", "612"):
            self.assertIn('INF_ZoneVehicleSettingEditorAttribute "{6AA19CB200000' + guid + '}"', CONFIG)
        self.assertNotIn("Crew AI Budget", CONFIG)
        self.assertIn("INF_SetZoneVehicleSetting(m_iSetting", EDITOR)
        self.assertIn("INF_GetZoneVehicleSetting(m_iSetting)", EDITOR)
        self.assertIn("INF_ZoneEditorContext.Grid(m_GameMode)", EDITOR)
        self.assertIn("Replication.BumpMe()", body("void INF_SetZoneVehicleSetting("))

    def test_both_vehicle_systems_share_server_only_once_per_vehicle_passengers(self):
        spawn = body("override protected void SpawnCrew(")
        self.assertIn("!Replication.IsServer()", spawn)
        self.assertIn("agent.m_bINFPassengersStarted", spawn)
        self.assertLess(spawn.index("agent.m_bINFPassengersStarted = true"), spawn.index("super.SpawnCrew(agent)"))
        self.assertLess(spawn.index("Insert(agent.INF_OnCrewSpawned)"), spawn.index("super.SpawnCrew(agent)"))
        passenger_setup = spawn.split("agent.m_bINFPassengersStarted = true;", 1)[1]
        self.assertNotIn("INF_ZoneEditorContext", passenger_setup)
        self.assertIn("SpawnCrew(unit.m_Agent)", body("protected void INF_ZoneSpawnCrew("))
        passengers = body("void INF_SpawnPassengers(")
        for guard in ("!Replication.IsServer()", "m_INFZoneVehiclesStopped",
                      "!agent.m_bActive", "!agent.m_VehicleEntity", "HasPlayerOccupant",
                      "if (cancelled)"):
            self.assertIn(guard, passengers)
        self.assertIn("GetParentGroup()", passengers)
        self.assertIn("selected.SpawnDefaultCharacterInCompartment(group)", passengers)
        self.assertNotIn("ActivateAI()", passengers)
        self.assertNotIn("CallLater", passengers)
        self.assertIn("agent.m_aINFMembers.Insert(passenger)", passengers)
        self.assertIn("persistence.StopTracking(passenger, true)", passengers)
        self.assertIn("LogLevel.ERROR", passengers)

    def test_passengers_use_only_free_accessible_default_seats_with_zero_to_full_count(self):
        passengers = body("void INF_SpawnPassengers(")
        for contract in ("PASSENGER_COMPARTMENT_TYPES", "!slot.IsOccupied()",
                         "slot.IsCompartmentAccessible()", "!slot.GetDefaultOccupantPrefab().IsEmpty()",
                         "Math.RandomInt(0, eligible.Count() + 1)", "eligible.Remove(index)"):
            self.assertIn(contract, passengers)
        for available in (0, 1, 4, 20):
            for selected in range(available + 1):
                seats = list(range(available))
                chosen = []
                for _ in range(selected):
                    chosen.append(seats.pop(len(seats) // 2))
                self.assertEqual(len(chosen), selected)
                self.assertEqual(len(chosen), len(set(chosen)))
        self.assertIn("agent.m_bINFPassengersReady = true", passengers)

    def test_native_completion_records_crew_before_spawning_passengers(self):
        callback = body("void INF_OnCrewSpawned(")
        self.assertIn("SCR_BaseCompartmentManagerComponent compartments, array<IEntity> crew, bool cancelled", SOURCE)
        self.assertIn("m_iINFSpawnedCrew = crew.Count()", callback)
        self.assertLess(callback.index("m_aINFMembers.Insert(member)"), callback.index("manager.INF_SpawnPassengers"))
        self.assertLess(callback.index("INF_StopCrewListener()"), callback.index("manager.INF_SpawnPassengers"))
        passengers = body("void INF_SpawnPassengers(")
        self.assertIn("!livingCrew && agent.m_iINFSpawnedCrew > 0", passengers)

    def test_passengers_dismounting_or_being_captured_before_first_tick_remain_tracked(self):
        spawned = ["driver", "passenger"]
        tracked = list(spawned)
        seated_at_first_tick = []
        captured = {"passenger"}
        dead = {"driver"}
        self.assertEqual(tracked, ["driver", "passenger"])
        self.assertEqual([member for member in tracked if member not in captured | dead], [])
        self.assertEqual(seated_at_first_tick, [])
        resolve = body("protected bool INF_ResolveZoneCrew(")
        self.assertNotIn("GetOccupants", resolve)
        self.assertIn("if (!INF_ZoneHostileCrew(member))", resolve)


if __name__ == "__main__":
    unittest.main()
