"""Prefab manager source contracts; not engine execution."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / "Scripts" / "Game"
MANAGER = (SCRIPTS / "INF_PrefabManager.c").read_text()
GRID = (SCRIPTS / "INF_BuildingGrid.c").read_text()
BLOCK = (SCRIPTS / "INF_BlockControl.c").read_text()
VEHICLES = (SCRIPTS / "INF_ZoneVehicles.c").read_text()
MODE = (ROOT / "Prefabs" / "MP" / "Modes" / "InsurgencyAdvanced" / "GameMode_InsurgencyAdvanced.et").read_text()


class PrefabManager(unittest.TestCase):
    def test_component_holds_every_definition_with_plain_attribute_names(self):
        for name in ("m_aLightVehicles", "m_aHeavyVehicles", "m_aLightCrew", "m_aHeavyCrew", "m_aPassengers",
                     "m_aDefenderGroups", "m_aCounterAttackSquads", "m_aFieldThreatGroups", "m_sOfficer",
                     "m_sCamp", "m_sMortar", "m_aDefences"):
            self.assertRegex(MANAGER, r"protected (ref array<ResourceName>|ResourceName) " + name + ";")
        self.assertNotRegex(MANAGER, r"protected [^\n]* m_\w*INF\w*;")
        self.assertNotIn("SlotCache", MANAGER)

    def test_every_manager_receives_the_definitions_at_init(self):
        init = MANAGER.split("override void EOnInit(", 1)[1]
        for manager in ("INS_AmbientPatrolManager", "INS_CounterAttackManager", "INS_FieldThreatManager",
                        "INS_AmbientVehicleManager"):
            self.assertIn(manager + ".Cast(owner.FindComponent(" + manager + "))", init)
        self.assertEqual(init.count("INF_ApplyPrefabs(this)"), 4)

    def test_manager_is_the_only_source_and_empty_base_lists_keep_the_base_mod_values(self):
        self.assertIn("if (!source || source.IsEmpty())\n\t\t\treturn fallback;", MANAGER.replace("\r\n", "\n"))
        patrols = GRID.split("void INF_ApplyPrefabs(INF_PrefabManagerComponent prefabs)", 1)[1].split("}", 1)[0]
        for field, getter in (("m_sINFCampComposition", "GetCamp"), ("m_sINFMortarComposition", "GetMortar"),
                              ("m_sINFOfficerPrefab", "GetOfficer")):
            self.assertIn(field + " = prefabs." + getter + "();", patrols)
        self.assertNotIn("m_sINFDefenceSlotCache", patrols)
        self.assertIn("m_aGroupPrefabs = INF_PrefabManagerComponent.PreferList(", patrols)
        self.assertIn("m_aSquadPrefabs = INF_PrefabManagerComponent.PreferList(", GRID)
        self.assertIn("m_aGroupPrefabs = INF_PrefabManagerComponent.PreferList(", BLOCK)

    def test_compositions_are_not_mission_header_settings(self):
        self.assertNotIn("params.m_sINFCampComposition", GRID)
        self.assertNotIn("params.m_sINFMortarComposition", GRID)
        self.assertNotRegex(GRID, r"modded class INS_MissionParams[^}]*m_sINF(Camp|Mortar)Composition")

    def test_legacy_attributes_on_the_patrol_manager_are_gone(self):
        for field in ("m_sINFCampComposition", "m_sINFMortarComposition", "m_sINFOfficerPrefab",
                      "m_aINFDefenceCompositions"):
            self.assertRegex(GRID, r"\n\tprotected (ref array<ResourceName>|ResourceName) " + field + ";")
            self.assertNotRegex(GRID, r"\[Attribute\([^\n]*\)\]\r?\n\tprotected [^\n]* " + field + ";")

    def test_slot_cache_is_mission_map_data_not_a_prefab_manager_setting(self):
        self.assertRegex(GRID, r"\[Attribute\([^\n]*\)\]\r?\n\tprotected ResourceName m_sINFDefenceSlotCache;")
        self.assertNotIn("GetDefenceSlotCache", GRID + MANAGER)
    def test_vehicle_manager_reads_crews_and_heavy_list_from_the_prefab_manager(self):
        for old in ("m_aINFHeavyVehiclePrefabs", "m_aINFCrewPrefabs", "m_aINFHeavyCrewPrefabs", "m_aINFPassengerPrefabs"):
            self.assertNotIn(old, VEHICLES)
        for getter in ("GetHeavyVehicles()", "GetHeavyCrew()", "GetLightCrew()", "GetPassengers()", "GetLightVehicles()"):
            self.assertIn(getter, VEHICLES)

    def test_game_mode_prefab_defines_units_on_the_manager_only(self):
        self.assertRegex(MODE, r'INF_PrefabManagerComponent "\{[0-9A-F]{16}\}" \{')
        patrols = re.search(r'INS_AmbientPatrolManager "[^"]+" \{(.*?)\n  \}', MODE, re.S).group(1)
        self.assertNotRegex(patrols, r"m_s(Camp|Mortar|Officer)\w*Prefab|Composition|m_aINFDefence")
        for manager, field in (("INS_AmbientPatrolManager", "m_aGroupPrefabs"), ("INS_AmbientVehicleManager", "m_aVehiclePrefabs"),
                               ("INS_CounterAttackManager", "m_aSquadPrefabs"), ("INS_FieldThreatManager", "m_aGroupPrefabs")):
            self.assertRegex(MODE, manager + r' "[^"]+" \{[^\n]*\n(?:   m_\w+ [^\n]*\n)*   '+ field + r' \{\s*\n   \}')
        for groups in ("m_aDefenderGroups", "m_aCounterAttackSquads", "m_aFieldThreatGroups"):
            self.assertRegex(MODE, groups + r' \{\n?\r?\n?\s*"\{[0-9A-F]{16}\}Prefabs/Groups/')


if __name__ == "__main__":
    unittest.main()
