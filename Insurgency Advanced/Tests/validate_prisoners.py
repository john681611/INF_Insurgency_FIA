"""Prisoner source contracts and boundary models, not Enfusion execution."""
from itertools import product
from dataclasses import dataclass
from pathlib import Path
import re
import unittest
from source_contracts import method_body

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "Scripts" / "Game" / "INF_Prisoners.c").read_text()
INTEL = (ROOT / "Scripts" / "Game" / "INF_CacheObjective.c").read_text()
CONTROLLER = (ROOT / "Scripts" / "Game" / "INF_GMDebugMarkers.c").read_text()
RULES = (ROOT / "Scripts" / "Game" / "INF_BlockControl.c").read_text()
ATTRIBUTES = (ROOT / "Scripts" / "Game" / "INF_ZoneEditorAttributes.c").read_text()
GRID = (ROOT / "Scripts" / "Game" / "INF_BuildingGrid.c").read_text()
VEHICLES = (ROOT / "Scripts" / "Game" / "INF_ZoneVehicles.c").read_text()
PREFAB = ROOT / "Prefabs/Characters/Factions/OPFOR/USSR_Army/Character_USSR_Base.et"


def method(source, start, end):
    return source.split(start, 1)[1].split(end, 1)[0]


@dataclass
class Cleanup:
    radius: float
    delay: int = 0
    concealment: bool = False
    anchor: bool = False
    since: int = -1
    grace_until: int = 0

    def tick(self, now, distance, visible=False, anchor_distance=float("inf")):
        distant = distance > self.radius
        distant &= not self.anchor or anchor_distance > self.radius
        distant &= not self.concealment or not visible
        if not distant:
            self.since = -1
            return False
        if self.since < 0:
            self.since = now
        return now >= self.grace_until and now - self.since >= self.delay


class Prisoners(unittest.TestCase):
    def test_cleanup_retains_spawn_rules_before_detaching_from_the_group(self):
        capture = method_body(SOURCE, "void Capture(")
        self.assertLess(capture.index("BindCleanupRules(group)"), capture.index("group.RemoveAgent(agent)"))
        bind = method_body(SOURCE, "protected void BindCleanupRules(")
        for manager in ("vehicles", "attacks", "threats", "patrols"):
            self.assertIn(f"{manager}.INF_BindPrisonerCleanup(group, this)", bind)
        self.assertIn("LogLevel.WARNING", bind)
        patrols, attacks = GRID.split("modded class INS_CounterAttackManager", 1)
        self.assertNotIn("INF_BindPrisonerCleanup(", method_body(patrols, "void INF_CollectGroups("))
        self.assertNotIn("INF_BindPrisonerCleanup(", method_body(attacks, "void INF_CollectGroups("))
        patrol = method_body(patrols, "bool INF_BindPrisonerCleanup(")
        self.assertIn("m_fDeactivationRadius, INF_BlockRules.DESPAWN_DELAY_MS, false, state.m_vCenter, true", patrol)
        attack = method_body(attacks, "bool INF_BindPrisonerCleanup(")
        self.assertIn("INF_BlockRules.COUNTERATTACK_DESPAWN_RADIUS, m_fAbandonDelay * 1000", attack)
        threat = method_body(RULES, "bool INF_BindPrisonerCleanup(")
        self.assertIn("m_fAbandonRange, 0, false", threat)
        vehicle = method_body(VEHICLES, "bool INF_BindPrisonerCleanup(")
        self.assertIn("m_fDeactivationRadius, 0, true", vehicle)
        self.assertIn("m_fDeactivationRadius, 0, false", vehicle)
        self.assertIn("INF_SetVehicleGraceSource(agent)", vehicle)

    def test_questioning_does_not_delete_immediately_or_prevent_abandoned_cleanup(self):
        question = method_body(SOURCE, "void Question(")
        self.assertNotIn("DeleteEntity", question)
        cleanup = method_body(SOURCE, "bool INF_CanCleanup(")
        self.assertNotIn("m_bQuestioned", cleanup)
        for condition in ("!Replication.IsServer()", "!m_bCaptured", "m_fCleanupRadius < 0",
                          "> m_fCleanupRadius", "m_fDistantSince = -1",
                          "now - m_fDistantSince >= m_fCleanupDelay",
                          "now >= m_fCleanupGraceUntil", "INF_BlockRules.IsConcealed"):
            self.assertIn(condition, cleanup)
        maintain = method_body(SOURCE, "void MaintainRestraints(")
        self.assertIn("if (INF_CanCleanup())", maintain)
        self.assertIn("DeleteEntityAndChildren(GetOwner())", maintain)
        preserve = method_body(SOURCE, "static bool INF_Preserve(")
        self.assertIn("!damage.IsDestroyed()", preserve)

    def test_normal_distance_delay_resets_when_players_return_and_checks_anchor(self):
        cleanup = Cleanup(1000, delay=60000, anchor=True)
        self.assertFalse(cleanup.tick(0, 1000))
        self.assertFalse(cleanup.tick(1000, 1001, anchor_distance=1000))
        self.assertFalse(cleanup.tick(2000, 1001, anchor_distance=1001))
        self.assertFalse(cleanup.tick(61999, 1001))
        self.assertFalse(cleanup.tick(62000, 999))
        self.assertFalse(cleanup.tick(63000, 1001))
        self.assertFalse(cleanup.tick(122999, 1001))
        self.assertTrue(cleanup.tick(123000, 1001))

    def test_vehicle_visibility_grace_and_no_player_boundaries(self):
        zone = Cleanup(1000, concealment=True)
        self.assertFalse(zone.tick(0, 1001, visible=True))
        self.assertFalse(zone.tick(1000, 1000))
        self.assertTrue(zone.tick(2000, 1001))
        ambient = Cleanup(1000, grace_until=300000)
        self.assertFalse(ambient.tick(299999, 1001))
        self.assertTrue(ambient.tick(300000, 1001))
        infantry = Cleanup(1000, delay=60000)
        self.assertFalse(infantry.tick(0, float("inf")))
        self.assertTrue(infantry.tick(60000, float("inf")))

    def test_failed_ace_request_falls_through_to_native_pose(self):
        pose = method_body(SOURCE, "protected void UpdatePrisonerLoiter(")
        request = pose.split('if (GetGame().GetScriptModule().Call(character, "ACE_Captives_SetSurrender"', 1)[1]
        self.assertLess(request.index("return;"), request.index("WarnHandling("))
        fallback = request.split("WarnHandling(", 1)[1].split("if (!character.CanPlayLoiterAnimation", 1)[0]
        self.assertNotIn("return;", fallback)
        self.assertIn("character.StartLoitering(", request)

    def test_unconscious_players_are_not_capturers_but_still_protect_cleanup(self):
        collect = method_body(RULES, "void CollectPlayers(")
        self.assertIn("!controller.IsUnconscious()", collect)
        self.assertIn("controller.GetLifeState() == ECharacterLifeState.ALIVE", collect)
        self.assertLess(collect.index("m_Players.Insert"), collect.index("!controller.IsUnconscious()"))

    def test_weapon_drops_use_native_inventory_and_preserve_grenades_and_supplies(self):
        drop = method(SOURCE, "void DropCarriedWeapons(", "void StopPrisonerLoiter(")
        for text in ("!Replication.IsServer()", "!m_bCaptured", "m_bWeaponsDropped",
                     "inventory.GetItems(items)", "weapon.GetWeaponType()",
                     "inventory.TryRemoveItemFromInventory(item)", "WarnHandling"):
            self.assertIn(text, drop)
        for kind in ("RIFLE", "SNIPERRIFLE", "MACHINEGUN", "HANDGUN",
                     "ROCKETLAUNCHER", "GRENADELAUNCHER", "AUTOCANNON"):
            self.assertIn("EWeaponType.WT_" + kind, drop)
        for kind in ("FRAGGRENADE", "SMOKEGRENADE"):
            self.assertNotIn("EWeaponType.WT_" + kind, drop)
        self.assertNotIn("DeleteEntity", drop)
        self.assertIn("if (!foundWeapon)", drop)
        capture = method(SOURCE, "void Capture(", "void Question(")
        self.assertLess(capture.index("m_bCaptured = true"), capture.index("DropCarriedWeapons()"))
        self.assertLess(capture.index("DropCarriedWeapons()"), capture.index("ApplyRestraints()"))
        tick = method(SOURCE, "void MaintainRestraints(", "void WarnHandling(")
        self.assertIn("DropCarriedWeapons()", tick)

    def test_standing_and_loiter_wait_for_conscious_disarmed_prisoner(self):
        pose = method(SOURCE, "void UpdatePrisonerLoiter(", "void OnCapturedChanged(")
        for guard in ("!Replication.IsServer()", "!m_bCaptured", "ECharacterLifeState.ALIVE",
                      "controller.IsUnconscious()", "!m_bWeaponsDropped", "access.IsInCompartment()",
                      "character.IsFalling()", "character.IsSwimming()", "character.IsClimbing()"):
            self.assertLess(pose.index(guard), pose.index("character.SetStanceChange"))
        self.assertIn("ECharacterStanceChange.STANCECHANGE_TOERECTED", pose)
        self.assertIn("CanPlayLoiterAnimation(m_PrisonerLoiter)", pose)
        self.assertIn("ELoiteringType.LOITERING", pose)
        self.assertIn("ELoiteringType.SMOKING", pose)
        self.assertIn("SCR_ELoiterItemID.CIGAR", pose)
        self.assertIn("false, false, false, transform, true, data", pose)
        self.assertIn("StopPrisonerLoiter(controller)", pose)
        self.assertNotRegex(pose, r"\b(?:FullHeal|SetHealth|SetUnconscious|ActivateAI)\s*\(")

    def test_smoking_is_selected_once_and_retry_does_not_reroll(self):
        pose = method(SOURCE, "void UpdatePrisonerLoiter(", "void OnCapturedChanged(")
        self.assertLess(pose.index("m_PrisonerLoiter == ELoiteringType.NONE"), pose.index("Math.RandomFloat01()"))
        self.assertEqual(pose.count("Math.RandomFloat01()"), 1)
        self.assertIn("now + 5000", pose)
        for roll, expected in ((0, "smoking"), (0.1999, "smoking"),
                               (0.2, "loiter"), (0.9999, "loiter")):
            choice = "smoking" if roll < 0.2 else "loiter"
            for _ in range(5):
                self.assertEqual(choice, expected)

    def test_optional_ace_surrender_replaces_only_idle_branch(self):
        detect = method(SOURCE, "bool GetACESurrenderState(", "void UpdatePrisonerLoiter(")
        self.assertIn('"ACE_Captives_SurrenderHelperCompartment".ToType()', detect)
        self.assertLess(detect.index("if (!surrenderHelper)"), detect.index('Call(character,'))
        self.assertIn('"ACE_Captives_HasSurrendered", false, surrendered', detect)
        self.assertIn("WarnHandling", detect)
        pose = method(SOURCE, "void UpdatePrisonerLoiter(", "void OnCapturedChanged(")
        self.assertLess(pose.index("controller.IsUnconscious()"), pose.index("GetACESurrenderState"))
        self.assertLess(pose.index("!m_bWeaponsDropped"), pose.index("GetACESurrenderState"))
        self.assertLess(pose.index("if (surrendered)"), pose.index("access.IsInCompartment()"))
        self.assertIn("m_PrisonerLoiter == ELoiteringType.LOITERING && aceSurrenderAvailable", pose)
        self.assertIn('"ACE_Captives_SetSurrender", false, null, true', pose)
        self.assertIn("m_fNextACESurrenderAttempt = surrenderNow + 5000", pose)
        self.assertIn("ACE surrender could not be requested", pose)
        self.assertNotIn("ACE_Captives_SetCaptive", SOURCE)
        for ace, roll in product((False, True), (0.0, 0.1999, 0.2, 0.9999)):
            smoking = roll < 0.2
            selected = "smoking" if smoking else ("surrender" if ace else "loiter")
            self.assertEqual(selected == "surrender", ace and not smoking)
            self.assertEqual(selected == "smoking", smoking)

    def test_capture_only_accepts_hostile_incapacitated_nonplayer_ai(self):
        capture = method(SOURCE, "bool CanCapture(", "bool CanQuestion(")
        for check in ("m_bCaptured", "IsUsableBy(user)", "ChimeraCharacter.Cast",
                      "GetAIControlComponent()", "ECharacterLifeState.INCAPACITATED",
                      "targetFaction.GetAffiliatedFaction()", "friendly.IsFactionEnemy(hostile)"):
            self.assertIn(check, capture)
        usable = method(SOURCE, "bool IsUsableBy(", "bool CanCapture(")
        for check in ("INF_ZoneEditorContext.Grid", "GetPlayerIdFromControlledEntity(user) <= 0",
                      "GetPlayerIdFromControlledEntity(GetOwner()) > 0", "ECharacterLifeState.ALIVE",
                      "controller.IsUnconscious()", "!access.IsInCompartment()", "<= 9"):
            self.assertIn(check, usable)
        for ai, unconscious, hostile, player_target, captured in product((False, True), repeat=5):
            allowed = ai and unconscious and hostile and not player_target and not captured
            self.assertEqual(allowed, (ai, unconscious, hostile, player_target, captured)
                             == (True, True, True, False, False))

    def test_restraint_is_neutral_immobile_noncombatant_without_free_healing(self):
        apply = method(SOURCE, "void ApplyRestraints(", "void MaintainRestraints(")
        for call in ("SetWeaponRaised(false)", "SetDisableWeaponControls(true)",
                     "SetDisableMovementControls(true)", "DeactivateAI()",
                     'GetFactionByKey("CIV")', "SetAffiliatedFaction(civilian)"):
            self.assertIn(call, apply)
        self.assertNotIn("SetUnconscious(", SOURCE)
        self.assertNotRegex(SOURCE, r"\b(?:FullHeal|SetHealth|SetDamage|ForceRevive)\s*\(")
        self.assertIn("unaffiliated noncombatant", SOURCE)

    def test_capture_is_server_validated_and_detaches_from_cleanup(self):
        capture = method(SOURCE, "void Capture(", "void Question(")
        self.assertLess(capture.index("!Replication.IsServer()"), capture.index("!CanCapture(user)"))
        self.assertLess(capture.index("m_bCaptured = true"), capture.index("group.RemoveAgent(agent)"))
        self.assertIn("control.GetControlAIAgent()", capture)
        self.assertIn("agent.GetParentGroup()", capture)
        self.assertIn("Replication.BumpMe()", capture)
        self.assertIn("CallLater(MaintainRestraints, 1000, true)", capture)
        collector = method(RULES, "static void CollectLiving(", "static bool HasMembers(")
        self.assertIn("prisoner && prisoner.IsCaptured()", collector)
        self.assertIn("continue;", collector.split("prisoner.IsCaptured()", 1)[1])

    def test_question_requires_captured_conscious_and_unspent(self):
        check = method(SOURCE, "bool CanQuestion(", "bool CanSearch(")
        for text in ("!m_bCaptured", "m_bQuestioned", "!IsUsableBy(user)",
                     "ECharacterLifeState.ALIVE", "controller.IsUnconscious()", "!damage.IsBleeding()"):
            self.assertIn(text, check)
        for captured, awake, questioned, searched in product((False, True), repeat=4):
            allowed = captured and awake and not questioned
            self.assertEqual(allowed, (captured, awake, questioned) == (True, True, False))
        self.assertNotIn("IsSearched", check)
        for captured, awake, questioned, bleeding in product((False, True), repeat=4):
            allowed = captured and awake and not questioned and not bleeding
            self.assertEqual(allowed, (captured, awake, questioned, bleeding)
                             == (True, True, False, False))

    def test_intel_readiness_and_response_channel_are_checked_before_consuming(self):
        question = method(SOURCE, "void Question(", "void Respond(")
        for check in ("!Replication.IsServer()", "!CanQuestion(user)",
                      "!intel.INF_IsIntelReady()", "if (!player)"):
            self.assertLess(question.index(check), question.index("m_bQuestioned = true"))
        self.assertLess(question.index("m_bQuestioned = true"), question.index("Math.RandomFloat01()"))
        self.assertNotIn("MarkSearched", question)
        self.assertIn("AddIntelAndBroadcast(1, playerID)", question)
        self.assertIn("INF_HasQuestioningIntel()", question)

    def test_bleeding_keeps_question_visible_but_disabled_with_bandaging_reason(self):
        action = SOURCE.split("class INF_QuestionAIAction", 1)[1].split(
            "modded class INS_BodySearchAction", 1)[0]
        shown = method(action, "override bool CanBeShownScript(", "override bool CanBePerformedScript(")
        perform = method(action, "override bool CanBePerformedScript(", "override void PerformAction(")
        self.assertIn("prisoner.CanQuestion(user, true)", shown)
        self.assertIn("prisoner.CanQuestion(user)", perform)
        self.assertIn('SetCannotPerformReason("")', perform)
        self.assertIn('SetCannotPerformReason("Prisoner is bleeding. Bandage them before questioning.")', perform)
        question = method(SOURCE, "void Question(", "void Respond(")
        self.assertIn("!CanQuestion(user)", question)
        self.assertNotIn("CanQuestion(user, true)", question)
        for eligible, bleeding in product((False, True), repeat=2):
            visible = eligible
            enabled = eligible and not bleeding
            self.assertEqual(visible and not enabled, eligible and bleeding)

    def test_one_attempt_survives_failure_other_players_and_death(self):
        # Each attempt is team-wide, independent and spent even on failure.
        for roll in (0.0, 0.05, 0.1, 0.99):
            captured, questioned, searched = True, False, False
            attempts, awards = 0, 0
            for _player in range(4):
                if captured and not questioned:
                    questioned = True
                    attempts += 1
                    awards += int(roll < 0.1)
            self.assertEqual(attempts, 1)
            self.assertLessEqual(awards, 1)
            self.assertFalse(searched)
            self.assertTrue(captured)  # death does not reset the replicated capture flag
        body = SOURCE.split("modded class INS_BodySearchAction", 1)[1]
        self.assertIn("return prisoner.CanSearch(user)", body)
        self.assertIn("!super.CanBePerformedScript(user)", body)
        self.assertNotIn("Blocked body search of a secured prisoner", body)
        perform = body.split("override void PerformAction(", 1)[1]
        self.assertLess(perform.index("!CanBePerformedScript(pUserEntity)"), perform.index("super.PerformAction("))

    def test_search_and_question_each_once_in_either_order(self):
        for order in (("search", "question"), ("question", "search")):
            spent = {"search": False, "question": False}
            rolls = {"search": 0, "question": 0}
            for action in order * 4:
                if not spent[action]:
                    spent[action] = True
                    rolls[action] += 1
            self.assertEqual(rolls, {"search": 1, "question": 1})
        search = method(SOURCE, "bool CanSearch(", "void Capture(")
        self.assertIn("m_bCaptured && IsUsableBy(user)", search)
        self.assertNotIn("m_bQuestioned", search)
        self.assertNotIn("GetLifeState", search)

    def test_chance_defaults_to_twice_effective_body_chance_and_is_configurable(self):
        self.assertIn("m_fINFQuestioningChance = -1", INTEL)
        self.assertIn("Math.Clamp(GetIntelDropChance() * 2, 0, 1)", INTEL)
        self.assertIn("if (m_fINFQuestioningChance == -1)", INTEL)
        setter = method(INTEL, "void INF_SetQuestioningChance(", "bool INF_HasQuestioningIntel(")
        self.assertIn("!(chance >= 0 && chance <= 1)", setter)
        self.assertIn("Replication.BumpMe()", setter)
        for body, expected in ((0, 0), (0.05, 0.1), (0.3, 0.6), (0.75, 1), (1, 1)):
            self.assertAlmostEqual(min(1, max(0, body * 2)), expected)
        question = method(SOURCE, "void Question(", "void Respond(")
        self.assertIn("Math.RandomFloat01() < intel.INF_GetQuestioningChance()", question)
        for chance in (0, 0.1, 1):
            self.assertEqual(0 < chance, chance != 0)
            self.assertEqual(0.999999 < chance, chance == 1)

    def test_gm_percentage_slider_uses_probability_units(self):
        attribute = method(ATTRIBUTES, "class INF_QuestioningChanceEditorAttribute", "class INF_TravelSettingEditorAttribute")
        for text in ("INF_ZoneEditorContext.Grid", "INF_GetQuestioningChance() * 100",
                     "INF_SetQuestioningChance(var.GetFloat() / 100)",
                     "new INS_SliderValues(0, 100, 1)"):
            self.assertIn(text, attribute)
        edit = (ROOT / "Configs" / "Editor" / "AttributeLists" / "Edit.conf").read_text()
        self.assertIn('INF_QuestioningChanceEditorAttribute "{6AA19CB200000311}"', edit)
        self.assertIn("Prisoner Questioning Intel Chance (%)", edit)

    def test_replication_join_in_progress_and_ticker_cleanup(self):
        self.assertIn('[RplProp(onRplName: "OnCapturedChanged")]', SOURCE)
        self.assertRegex(SOURCE, r"\[RplProp\(\)\]\s+protected bool m_bQuestioned")
        init = method(SOURCE, "override void EOnInit(", "override void OnDelete(")
        self.assertIn("if (m_bCaptured)", init)
        self.assertIn("ApplyRestraints()", init)
        self.assertEqual(SOURCE.count("Remove(MaintainRestraints)"), 2)
        maintenance = method(SOURCE, "void MaintainRestraints(", "void OnCapturedChanged(")
        self.assertIn("ECharacterLifeState.DEAD", maintenance)

    def test_reliable_dialogue_only_goes_to_questioning_player(self):
        response = method(CONTROLLER, "void INF_SendPrisonerResponse(", "protected int m_iINFDebugMarkerLayers")
        self.assertIn("!Replication.IsServer()", response)
        self.assertIn("RplChannel.Reliable, RplRcver.Owner", response)
        self.assertIn("GetPlayerId() != SCR_PlayerController.GetLocalPlayerId()", response)
        self.assertIn('ShowCustomHint(message, "Prisoner", 10)', response)
        self.assertNotIn("RplRcver.Broadcast", response)
        dialogue = SOURCE.split("[ComponentEditorProps", 1)[0]
        helpful, denials = dialogue.split("array<string> refusals", 1)
        helpful_lines = re.findall(r'^\s+"[^"]+"', helpful, re.M)
        refusal_lines = re.findall(r'^\s+"[^"]+"', denials, re.M)
        self.assertEqual(len(helpful_lines), 4)
        self.assertEqual(len(refusal_lines), 12)

    def test_actions_attach_to_upstream_soldier_base_with_original_body_search(self):
        prefab = PREFAB.read_text()
        self.assertTrue(prefab.startswith(
            'SCR_ChimeraCharacter : "{37578B1666981FCE}Prefabs/Characters/Core/Character_Base.et"'))
        self.assertIn('Name "{5346CF7E39A65A6B}Prefabs/Characters/Factions/OPFOR/USSR_Army/Character_USSR_Base.et"',
                      PREFAB.with_suffix(".et.meta").read_text())
        self.assertIn('ID "520EC961A090BE1E"', prefab)
        self.assertIn('ActionsManagerComponent "{520EA1D2F659CE02}"', prefab)
        self.assertEqual(prefab.count("INF_PrisonerComponent "), 1)
        for action in ("INS_BodySearchAction", "INF_CaptureAIAction", "INF_QuestionAIAction"):
            self.assertEqual(prefab.count(action + " "), 1)
        self.assertEqual(prefab.count('"LowerTorso"'), 3)
        self.assertEqual(prefab.count("INS_BodySearchedComponent "), 1)
        self.assertIn('INS_BodySearchedComponent "{69969400B0F8067B}"', prefab)
        self.assertIn('INS_BodySearchAction "{68B0FED869B08251}"', prefab)
        self.assertIn('UIInfo UIInfo "{68B0FED87E66D35A}"', prefab)
        self.assertFalse((ROOT / "Prefabs/Characters/Core/Character_Base.et").exists())
        self.assertNotIn("Faction", prefab)


if __name__ == "__main__":
    unittest.main()
