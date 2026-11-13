"""Mortar battery source contracts; not engine execution."""
from pathlib import Path
import re
import unittest
from source_contracts import method_body

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "Scripts" / "Game"
MORTAR = (SCRIPTS / "INF_MortarBattery.c").read_text()
GRID = (SCRIPTS / "INF_BuildingGrid.c").read_text()
MODE = (ROOT / "Prefabs" / "MP" / "Modes" / "InsurgencyAdvanced" / "GameMode_InsurgencyAdvanced.et").read_text()
COMPOSITION = "{28D63631873AA636}Prefabs/Compositions/Slotted/SlotFlatSmall/MortarPlacement_S_USSR_01.et"


def body(signature):
    return method_body(MORTAR, signature)


class MortarBattery(unittest.TestCase):
    def test_composition_is_configured_on_the_game_mode_next_to_camps(self):
        self.assertIn('m_sMortar "' + COMPOSITION + '"', MODE)
        self.assertIn("protected ResourceName m_sINFMortarComposition;", GRID)
        self.assertNotIn("SpawnPrefab(config.m_Prefab", MORTAR)

    def test_settings_default_to_play_values(self):
        self.assertIn("protected int m_iINFMortarChance = INF_MortarConfig.DEFAULT_CHANCE;", GRID)
        self.assertIn('[Attribute("10", UIWidgets.Slider', GRID)
        self.assertIn("protected int m_iINFMortarLimit = 2;", GRID)
        self.assertIn('"0 4 1"', GRID)

    def test_failed_spawns_are_logged_but_dice_rolls_are_silent(self):
        start = body("void OnZoneActivated(")
        self.assertIn("Fail(", start)
        self.assertIn("CarryWin(", start)
        self.assertNotIn("roll lost", start)
        self.assertIn("m_bWinCarried || Math.RandomInt(0, 100) < INF_Officers.Boost(config.m_iChance, zone)", start)
        self.assertIn("Placement rejected", body("protected bool FindPosition("))

    def test_win_is_carried_to_the_next_zone_when_unplaceable(self):
        self.assertIn("m_bWinCarried = true", body("protected void CarryWin("))

    def test_salvo_order_is_2_special_6_he_4_special_6_he_he_blocks_are_one_waypoint(self):
        salvo = body("protected bool StartSalvo(")
        self.assertIn("{special, HE, special, HE}", salvo)
        self.assertIn("{1, 3, 2, 3}", salvo)
        self.assertIn("{1, 3, 1, 3}", salvo)
        self.assertIn("SetTargetShotCount(roundsPerWaypoint[run])", salvo)
        self.assertIn("counts[run] / roundsPerWaypoint[run]", salvo)
        self.assertIn("foreach (INF_MortarCrew crew : battery.m_Crews)", salvo)
        self.assertIn("crew.m_Group.AddWaypoint(waypoint)", salvo)
        self.assertIn("{specialRadius, heRadius, specialRadius, heRadius}", salvo)
        self.assertIn("Math.Sqrt(Math.RandomFloat01())", salvo)
        self.assertIn("MIN_AIM_FROM_BATTERY", salvo)
        self.assertNotIn("SetCompletionRadius", salvo)
        self.assertIn("KnowledgeSpread(ILLUMINATION_SPREAD_RADIUS, knowledge, SPECIAL_MIN_FRACTION)", salvo)
        self.assertIn("KnowledgeSpread(SMOKE_SPREAD_RADIUS, knowledge, SPECIAL_MIN_FRACTION)", salvo)
        self.assertIn("KnowledgeSpread(SPREAD_RADIUS, knowledge, 0)", salvo)
        self.assertEqual("150", re.search(r"SMOKE_SPREAD_RADIUS = (\d+)", MORTAR).group(1))
        self.assertIn("SCR_EAIArtilleryAmmoType.SMOKE", salvo)
        self.assertIn("SCR_EAIArtilleryAmmoType.ILLUMINATION", salvo)
        self.assertIn("SCR_EAIArtilleryAmmoType.HIGH_EXPLOSIVE", salvo)
        self.assertIn("IsNight()", salvo)

    def test_salvo_runs_in_two_stages_with_a_gap_and_a_correction(self):
        fire = body("protected void Fire(")
        salvo = body("protected bool StartSalvo(")
        self.assertIn("static const int SALVO_STAGES = 2;", MORTAR)
        self.assertIn("static const float STAGE_GAP_MIN_MS = 8000;", MORTAR)
        self.assertIn("static const float STAGE_GAP_MAX_MS = 20000;", MORTAR)
        self.assertIn("run = stage * 2; run < stage * 2 + 2", salvo)
        self.assertIn("battery.m_iStage + 1 < SALVO_STAGES", fire)
        self.assertIn("Math.RandomFloat(STAGE_GAP_MIN_MS, STAGE_GAP_MAX_MS)", fire)
        self.assertIn("FindKnownTarget(patrols, battery, nextTarget, nextKnowledge)", fire)
        self.assertIn("battery.m_vLockedTarget = nextTarget;", fire)
        self.assertIn("StartSalvo(battery, nextTarget, nextKnowledge, battery.m_iStage)", fire)
        self.assertNotIn("corrected", fire)
        self.assertIn("ceasing fire", fire)
        self.assertNotIn("SALVO_TIMEOUT_MS", MORTAR)

    def test_battery_only_fires_on_what_hostile_ai_have_seen(self):
        find = body("protected bool FindKnownTarget(")
        fire = body("protected void Fire(")
        self.assertIn("observer.GetGroupUtilityComponent()", find)
        self.assertIn("utility.m_Perception.m_aTargets", find)
        self.assertIn("ownFaction.IsFactionEnemy(info.m_Faction)", find)
        self.assertIn("info.m_DamageManager.IsDestroyed()", find)
        self.assertIn("info.m_fTimestamp", find)
        self.assertIn("GetGame().GetPerceptionManager()", find)
        self.assertIn("perceptionManager.GetTime()", find)
        self.assertNotIn("GetWorldTime", find)
        self.assertNotIn("* 0.001", find)
        self.assertIn("info.m_vWorldPos", find)
        self.assertIn("(1 - infoAge / KNOWN_MAX_AGE_S) * certainty", find)
        self.assertIn("infoAge >= KNOWN_MAX_AGE_S", find)
        self.assertIn("battery.m_GuardGroup", find)
        self.assertIn("COVER_RADIUS", find)
        self.assertNotIn("m_AllPlayers", find)
        self.assertNotIn("GetPlayerIdFromControlledEntity", find)
        self.assertIn("FindKnownTarget(patrols, battery, target, knowledge)", fire)
        self.assertIn("FindKnownTarget(patrols, battery, nextTarget, nextKnowledge)", fire)
        self.assertNotIn("players", fire)

    def test_a_decision_delay_runs_from_no_knowledge_to_knowledge(self):
        fire = body("protected void Fire(")
        self.assertIn("static const float DECISION_DELAY_MIN_MS = 20000;", MORTAR)
        self.assertIn("static const float DECISION_DELAY_MAX_MS = 60000;", MORTAR)
        lost = fire.split("FindKnownTarget(patrols, battery, target, knowledge))", 1)[1].split("// Going from no knowledge", 1)[0]
        self.assertIn("battery.m_bKnowledge = false;", lost)
        self.assertIn("battery.m_fOrderAt = 0;", lost)
        gained = fire.split("// Going from no knowledge", 1)[1]
        cooldown_gate = fire.split("// The cooldown takes no orders", 1)[1].split("// Going from no knowledge", 1)[0]
        self.assertIn("if (now < battery.m_fNextFireAt)", cooldown_gate)
        gained = gained.split("if (now < battery.m_fOrderAt)", 1)[0]
        self.assertIn("Math.RandomFloat(DECISION_DELAY_MIN_MS, DECISION_DELAY_MAX_MS)", gained)
        self.assertIn("battery.m_bKnowledge = true;", gained)

    def test_the_order_locks_the_target_and_only_the_first_mission_waits_for_the_crews(self):
        fire = body("protected void Fire(")
        begin = body("protected void BeginSalvo(")
        self.assertIn("static const float READY_DELAY_MIN_MS = 15000;", MORTAR)
        self.assertIn("static const float READY_DELAY_MAX_MS = 30000;", MORTAR)
        order = fire.split("// The order: the point is locked in", 1)[1]
        self.assertLess(order.index("battery.m_vLockedTarget = target;"), order.index("BeginSalvo(battery, now)"))
        self.assertIn("if (!battery.m_bHasFired)", order)
        self.assertIn("Math.RandomFloat(READY_DELAY_MIN_MS, READY_DELAY_MAX_MS)", order)
        self.assertIn("battery.m_bHasFired = true;", begin)
        self.assertIn("StartSalvo(battery, battery.m_vLockedTarget, battery.m_fLockedKnowledge, 0)", begin)

    def test_the_target_is_only_corrected_between_the_stages(self):
        fire = body("protected void Fire(")
        begin = body("protected void BeginSalvo(")
        firing = fire.split("if (AllCrewsDead(battery))", 1)[0]
        self.assertEqual(firing.count("FindKnownTarget("), 1)
        self.assertLess(firing.index("battery.m_fStageAt = now + "), firing.index("FindKnownTarget("))
        self.assertIn("battery.m_fStageAt = 0;", begin)

    def test_knowledge_held_through_the_cooldown_orders_the_next_mission_at_once(self):
        fire = body("protected void Fire(")
        cooldown = fire.split("battery.m_State = INF_EMortarState.COOLDOWN;", 1)[1].split("return;", 1)[0]
        self.assertIn("now + COOLDOWN_MS", cooldown)
        self.assertIn("battery.m_fOrderAt = 0;", cooldown)
        self.assertNotIn("battery.m_bKnowledge = true;", cooldown)
        self.assertIn("battery.m_bKnowledge = false;", fire.split("ceasing fire", 1)[0])

    def test_spread_never_drops_below_the_floor_and_widens_as_knowledge_falls(self):
        spread = body("protected float KnowledgeSpread(")
        self.assertIn("static const float MIN_SPREAD = 30;", MORTAR)
        self.assertIn("static const float SPECIAL_MIN_FRACTION = 0.4;", MORTAR)
        self.assertIn("Math.Max(MIN_SPREAD, fullRadius * minFraction)", spread)
        self.assertIn("(fullRadius - minRadius) * (1 - Math.Clamp(knowledge, 0, 1))", spread)

    def test_each_mortar_has_its_own_gunner_crew(self):
        self.assertIn("static const int CREW_PER_GUN = 2;", MORTAR)
        self.assertIn("class INF_MortarCrew", MORTAR)
        spawn = body("protected bool SpawnBattery(")
        self.assertIn("crewIndex < GUN_COUNT", spawn)
        init = body("protected bool Initialise(")
        self.assertIn("guns[crewIndex].FindComponent(SCR_AIVehicleUsageComponent)", init)
        self.assertIn("utility.AddUsableVehicle(", init)

    def test_secured_prisoners_are_preserved_and_bound_to_the_battery_cleanup_rules(self):
        self.assertIn("INF_PrisonerComponent.INF_Preserve(entity)", body("protected void DeleteParts("))
        bind = body("bool BindPrisonerCleanup(")
        self.assertIn("prisoner.INF_SetCleanupRules(DESPAWN_RADIUS", bind)
        self.assertIn("if (owner == battery.m_GuardGroup)", bind)
        self.assertIn("if (owner == crew.m_Group)", bind)
        self.assertIn("m_INFMortars.BindPrisonerCleanup(group, prisoner)", GRID)

    def test_battery_units_never_block_zone_capture(self):
        # Capture counts only groups the managers collect: zone agents, counterattack squads, threats, vehicles.
        collect = method_body(GRID, "void INF_CollectGroups(array<SCR_AIGroup> groups)")
        self.assertNotIn("m_INFMortars", collect)
        self.assertNotIn("INF_MortarBatteries", collect)
        # Reading zone.m_aAgents to find spotters is fine; registering battery units as zone agents is not.
        for forbidden in ("m_aAgents.Insert", "new INS_PatrolAgent", "INF_CollectGroups", "m_bEverHadLiving",
                          "m_iINFReservedAI", "CollectEnemies", "CanCapture"):
            self.assertNotIn(forbidden, MORTAR)
        self.assertIn("static const float PLACE_MIN_FROM_ZONE = 500;", MORTAR)

    def test_battery_despawns_by_distance_and_stops_with_the_game_mode(self):
        self.assertIn("DESPAWN_RADIUS = 2000", MORTAR)
        self.assertIn("INF_StaggerGate.Despawns()", body("protected void Remove("))
        self.assertIn("m_INFMortars.Stop()", GRID)
        self.assertIn("m_INFMortars.Maintain(", GRID)
        self.assertIn("INF_GetMortars().OnZoneActivated(", GRID)

    def test_cleared_battery_keeps_its_mortars_until_the_defence_despawn_rules_release_them(self):
        self.assertIn('"crew and guards eliminated", false, true)', body("void Update(") if "void Update(" in MORTAR else MORTAR)
        remove = body("protected void Remove(")
        self.assertIn("if (keepCompositions)", remove)
        remains = body("protected void MaintainRemains(")
        self.assertIn("INF_Defences.DESPAWN_DISTANCE", remains)
        self.assertIn("INF_BlockRules.DESPAWN_DELAY_MS", remains)
        self.assertIn("INF_StaggerGate.DefenceDespawns()", remains)
        self.assertIn("MaintainRemains(players, now)", MORTAR)

    def test_nothing_is_persisted(self):
        spawn = body("protected bool SpawnBattery(")
        self.assertIn("PrepareComposition(composition)", spawn)
        self.assertIn("gunIndex < GUN_COUNT", spawn)
        self.assertIn("static const int GUN_COUNT = 2;", MORTAR)
        self.assertIn("StopTracking", body("protected void UntrackMembers("))


if __name__ == "__main__":
    unittest.main()
