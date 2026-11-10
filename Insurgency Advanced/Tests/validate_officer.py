"""Officer feature source contracts; not engine execution."""
from pathlib import Path
import unittest
from source_contracts import method_body

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "Scripts" / "Game"
OFFICERS = (SCRIPTS / "INF_Officers.c").read_text()
GRID = (SCRIPTS / "INF_BuildingGrid.c").read_text()
DEFENCES = (SCRIPTS / "INF_Defences.c").read_text()
PERSISTENCE = (SCRIPTS / "INF_TerritoryPersistence.c").read_text()
ZONES = (SCRIPTS / "INF_ZoneStateSystem.c").read_text()
PRISONERS = (SCRIPTS / "INF_Prisoners.c").read_text()
MAP = (SCRIPTS / "INF_GarrisonMapAreas.c").read_text()
MORTAR = (SCRIPTS / "INF_MortarBattery.c").read_text()
VEHICLES = (SCRIPTS / "INF_ZoneVehicles.c").read_text()
MODE = (ROOT / "Prefabs" / "MP" / "Modes" / "InsurgencyAdvanced" / "GameMode_InsurgencyAdvanced.et").read_text()
MANUAL = (ROOT / "Configs" / "FieldManual" / "FieldManualConfigRoot.conf").read_text()
PREFAB = "{5117311FB822FD1F}Prefabs/Characters/Factions/OPFOR/USSR_Army/Character_USSR_Officer.et"


class Officers(unittest.TestCase):
    def test_officer_prefab_is_configured_on_the_game_mode(self):
        self.assertIn('m_sINFOfficerPrefab "' + PREFAB + '"', MODE)
        self.assertIn("protected ResourceName m_sINFOfficerPrefab;", GRID)
        self.assertIn("AssignOfficers(!m_sINFOfficerPrefab.IsEmpty())", GRID)

    def test_rates_and_odds(self):
        for text in ("ZONES_PER_OFFICER = 50", "SQUAD_SIZE = 4", "CHANCE_MULTIPLIER = 3",
                     "QUESTION_LIMIT = 3", "QUESTION_CHANCE = 0.75", "SEARCH_CHANCE = 0.5"):
            self.assertIn(text, OFFICERS)
        select = method_body(OFFICERS, "static void Select(")
        self.assertIn("Math.Max(1, pool.Count() / ZONES_PER_OFFICER)", select)
        self.assertIn("INF_ArrayHelper<INS_AmbientPatrolZoneState>.Shuffle(pool)", select)
        self.assertIn("!state.m_bINFPermanent", select)

    def test_selection_is_saved_and_restored_once_the_registry_is_complete(self):
        assign = method_body(ZONES, "void AssignOfficers(")
        self.assertIn("!m_Persistence.OfficersAssigned()", assign)
        self.assertIn("m_Persistence.MarkOfficersAssigned()", assign)
        self.assertIn("m_Persistence.AddOfficer(", assign)
        self.assertIn("m_Persistence.IsOfficer(key)", assign)
        self.assertIn("m_Persistence.RemoveOfficer(key)", method_body(ZONES, "void OfficerDown("))
        self.assertIn("state.m_bINFOfficer = false", method_body(ZONES, "void OfficerDown("))
        self.assertLess(GRID.index("RestorePermanentCamps()"), GRID.index("AssignOfficers("))
        self.assertIn("file.Write(3)", PERSISTENCE)
        self.assertIn("version >= 1 && version <= 3", PERSISTENCE)
        self.assertIn("verified.m_OfficersAssigned != m_State.m_OfficersAssigned", PERSISTENCE)
        self.assertIn("verified.m_Officers.Count() != m_State.m_Officers.Count()", PERSISTENCE)

    def test_squad_reuses_the_defence_guard_path_and_is_additional_to_the_budget(self):
        guards = method_body(DEFENCES, "static void AppendGuards(")
        self.assertLess(guards.index("INF_Officers.AppendPost("), guards.index("foreach (INF_Defence defence"))
        self.assertIn("guardAI -= guardPlan[0]", GRID)
        spawn = method_body(GRID, "override protected void SpawnGroupAt(")
        self.assertIn("guardedDefence.m_bOfficerPost && state.m_bINFOfficer)", spawn)
        self.assertIn("INF_Officers.AddToSquad(", spawn)
        self.assertIn("group.m_aUnitPrefabSlots.Insert(prefab)", method_body(OFFICERS, "static void AddToSquad("))
        self.assertIn("INF_Officers.Claim(owner)", PRISONERS)
        self.assertIn("m_bOfficerPost", DEFENCES)
        self.assertIn("INF_Officers.HasRememberedSquad(", DEFENCES)

    def test_boosts_and_map_border(self):
        self.assertIn("INF_Officers.Boost(config.m_iChance, zone)", MORTAR)
        self.assertIn("INF_Officers.Boost(m_iINFZoneVehicleChance, zone)", VEHICLES)
        self.assertIn("Math.Min(100, chance * CHANCE_MULTIPLIER)", OFFICERS)
        self.assertIn("Color.FromRGBA(255, 200, 40, 255)", MAP)
        self.assertIn("state.m_bINFOfficer);", MAP)
        self.assertIn("border.m_fWidth = Math.Clamp(m_fActualRadius * 0.02, 1.5, 5)", MAP)

    def test_intel_odds_and_captured_or_dead_officer_is_gone(self):
        self.assertIn("chance = INF_Officers.QUESTION_CHANCE", PRISONERS)
        self.assertIn("return INF_Officers.QUESTION_LIMIT;", PRISONERS)
        self.assertIn("Math.RandomFloat01() < INF_Officers.SEARCH_CHANCE", PRISONERS)
        capture = method_body(PRISONERS, "void Capture(")
        self.assertIn("OfficerOut()", capture)
        self.assertIn("m_DamageManager.IsDestroyed()", method_body(PRISONERS, "protected void OnOfficerDamage("))
        self.assertIn("officer.SearchOfficer(pUserEntity)", PRISONERS)

    def test_officer_objective_switch_is_wired_to_the_saved_set(self):
        cleared = method_body(ZONES, "bool OfficersCleared(")
        self.assertIn("!m_bOfficersEnabled", cleared)
        self.assertIn("m_Persistence.OfficersAssigned() && m_Persistence.OfficerCount() == 0", cleared)
        self.assertIn("NotifyChanged(state, true)", method_body(ZONES, "void OfficerDown("))
        intel = (SCRIPTS / "INF_CacheObjective.c").read_text()
        self.assertIn("patrols.INF_GetZoneStates().OfficersCleared()", intel)
        task = method_body(intel, "protected void INF_UpdateOfficerTask(")
        self.assertIn("!states.OfficersEnabled()", task)
        self.assertIn('"INF_NeutraliseOfficers"', task)
        self.assertIn("INF_UpdateOfficerTask(patrols)", intel)

    def test_no_new_timers(self):
        self.assertNotIn("CallLater", OFFICERS)
        self.assertNotIn("EOnFrame", OFFICERS)

    def test_field_manual_describes_officers_without_numbers(self):
        for text in ("Enemy officers", "gold border", "Gold border - An enemy officer commands the area.",
                     "interrogated more deeply", "keep him alive"):
            self.assertIn(text, MANUAL)
        for text in ("50%", "75%", "three times", "fifty", "four-man", "mortar", "vehicle"):
            self.assertNotIn(text, MANUAL.split("Enemy officers", 1)[1].split("Read the map", 1)[0])


if __name__ == "__main__":
    unittest.main()
