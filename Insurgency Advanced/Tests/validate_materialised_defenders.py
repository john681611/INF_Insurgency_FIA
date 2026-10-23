"""Living-character and driving-activation contracts; not engine execution."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
RULES = (ROOT / "Scripts" / "Game" / "INF_BlockControl.c").read_text()
AI = (ROOT / "Scripts" / "Game" / "INF_BuildingGrid.c").read_text()
ZONES = (ROOT / "Scripts" / "Game" / "INF_ZoneStateSystem.c").read_text()


class MaterialisedDefenders(unittest.TestCase):
    def test_agent_placeholders_are_not_defender_evidence(self):
        body = RULES.split("static bool HasMembers(", 1)[1].split("static float Nearest", 1)[0]
        self.assertIn("CollectLiving(patrol.m_Group, living)", body)
        self.assertIn("if (!living.IsEmpty())", body)
        self.assertNotIn("GetAgents", body)
        collector = RULES.split("static void CollectLiving(", 1)[1].split("static bool HasMembers", 1)[0]
        self.assertIn("agent.GetControlledEntity()", collector)
        self.assertIn("if (!entity)", collector)
        self.assertIn("damage && !damage.IsDestroyed()", collector)
        for entries, expected in (
            ([], False),
            ([(False, False, False)], False),
            ([(True, False, False)], False),
            ([(True, True, True)], False),
            ([(True, True, False)], True),
        ):
            self.assertEqual(any(entity and damage and not dead for entity, damage, dead in entries), expected)

    def test_capture_and_retry_use_live_character_evidence(self):
        observe = ZONES.split("void ObserveDefenders(", 1)[1].split("void CheckCapture(", 1)[0]
        self.assertIn("INF_BlockRules.HasMembers(state.m_aAgents, presence)", observe)
        capture = ZONES.split("void CheckCapture(", 1)[1].split("void AttackStarted(", 1)[0]
        self.assertIn("(state.m_bEverHadLiving || state.m_bINFSpawnExhausted) && state.m_iINFPendingGroups == 0", capture)
        tick = AI.split("override protected void TickProximityCheck()", 1)[1].split("protected bool INF_MakeRoom", 1)[0]
        self.assertIn("!INF_BlockRules.HasMembers(state.m_aAgents)", tick)
        self.assertIn("Defender groups never materialised", tick)

    def test_attack_monitor_and_cleanup_do_not_promote_placeholders(self):
        monitor = AI.split("override protected void MonitorTick()", 1)[1].split("override protected void DespawnSquad(", 1)[0]
        cleanup = AI.split("override protected void DespawnSquad(", 1)[1].split("void INF_CollectGroups(", 1)[0]
        self.assertNotIn("GetAgents(", monitor)
        self.assertIn("INF_RecordAttackers(squad)", monitor)
        self.assertIn("INF_CollectAttackers(zone, living)", monitor)
        record = AI.split("protected void INF_RecordAttackers(", 1)[1].split(
            "protected bool INF_CollectAttackers(", 1)[0]
        self.assertIn("damage && !damage.IsDestroyed()", record)
        self.assertIn("squad.m_Zone.m_bINFEverHadLiving = true", record)
        self.assertIn("if (!living.IsEmpty())", cleanup)
        self.assertIn("zone.m_bINFEverHadLiving = true", cleanup)
        self.assertIn("now - zone.m_fINFStartedAt >= 30000", monitor)
        end = ZONES.split("void AttackEnded(", 1)[1]
        self.assertIn("if (!hadLiving || !defeated)", end)
        self.assertIn("state.m_bINFAttackSpent = false", end)

    def test_empty_wave_retries_but_living_inbound_wave_does_not_end(self):
        for defeated, living_now, elapsed, failed in (
            (False, 0, 1000, False),
            (False, 0, 29999, False),
            (False, 0, 30000, True),
            (False, 4, 60000, False),
            (False, 0, 1000, False),
            (True, 0, 1000, True),
            (True, 0, 60000, True),
        ):
            self.assertEqual(defeated or (living_now == 0 and elapsed >= 30000), failed)

    def test_fast_travel_uses_configured_thresholds_and_world_time(self):
        body = RULES.split("static bool IsFast(", 1)[1].split("\n\t}", 1)[0]
        for setting in range(4):
            self.assertIn(f"INF_GetTravelSetting({setting})", body)
        self.assertIn("now - state.m_AboveSince", body)
        self.assertIn("now - state.m_BelowSince", body)
        for field, value in (("EnterSpeed", 50), ("ExitSpeed", 30),
                             ("EnterSeconds", 20), ("ExitSeconds", 15)):
            self.assertRegex(AI, rf"m_fINFTravel{field} = {value};")


if __name__ == "__main__":
    unittest.main()
