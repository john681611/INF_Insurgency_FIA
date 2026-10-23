"""Capture-time decisions, pending attack bounds and map-state contracts."""
from dataclasses import dataclass
from pathlib import Path
import unittest
from source_contracts import method_body

ROOT = Path(__file__).resolve().parents[1]
GRID = (ROOT / "Scripts/Game/INF_BuildingGrid.c").read_text()
STATE = (ROOT / "Scripts/Game/INF_ZoneStateSystem.c").read_text()
MAP = (ROOT / "Scripts/Game/INF_GarrisonMapAreas.c").read_text()


@dataclass
class Capture:
    pending: bool = False
    permanent: bool = False
    cleared: bool = True
    active: bool = False
    attempts: int = 0
    retry_at: int = 0
    rolls: int = 0

    def capture(self, roll, chance=25, enabled=True):
        self.rolls += int(enabled)
        self.pending = enabled and roll < chance
        self.permanent = not self.pending
        self.retry_at = 5000

    def tick(self, now, distance, spawn_ok=False):
        if not self.pending or now < self.retry_at:
            return
        self.attempts += 1
        self.retry_at = now + 5000
        if distance <= 300 and spawn_ok:
            self.pending = False
            self.active = True
        elif self.attempts >= 3:
            self.pending = False
            self.permanent = self.cleared = distance <= 300


class CounterattackCaptureFlow(unittest.TestCase):
    def test_capture_rolls_once_and_failed_roll_is_permanent(self):
        for chance in (0, 25, 100):
            outcomes = []
            for roll in range(100):
                zone = Capture()
                zone.capture(roll, chance)
                outcomes.append(zone.pending)
                for now in (5000, 10000, 15000, 20000):
                    zone.tick(now, 300)
                self.assertEqual(zone.rolls, 1)
                self.assertTrue(zone.permanent)
            self.assertEqual(sum(outcomes), chance)
        commit = GRID.split("bool INF_CommitCapture(", 1)[1].split("override void OnGameModeStart", 1)[0]
        self.assertEqual(commit.count("Math.RandomInt"), 1)
        capture = STATE.split("void CheckCapture(", 1)[1].split("void AttackStarted", 1)[0]
        self.assertIn("!state.m_bINFAttackSpent && !state.m_bINFAttackPending", capture)
        self.assertIn("!attacks || !attacks.INF_CommitCapture(state)", capture)
        tick = GRID.split("override protected void CounterAttackTick()", 1)[1].split("protected bool INF_TrySpawnAttack", 1)[0]
        self.assertNotIn("Math.RandomInt", tick)

    def test_three_attempts_five_seconds_apart(self):
        zone = Capture()
        zone.capture(0)
        for now, attempts in ((4999, 0), (5000, 1), (9999, 1), (10000, 2), (14999, 2)):
            zone.tick(now, 300)
            self.assertEqual(zone.attempts, attempts)
            self.assertTrue(zone.pending)
        zone.tick(15000, 300)
        self.assertEqual(zone.attempts, 3)
        self.assertTrue(zone.permanent)
        self.assertFalse(zone.pending)
        self.assertIn("cell.m_fINFAttackRetryAt = now + 5000", GRID)
        self.assertIn("cell.m_iINFAttackAttempts >= 3", GRID)
        self.assertIn("CallLater(CounterAttackTick, 5000, false)", GRID)

    def test_presence_boundary_and_absence_count(self):
        for distance, secure in ((0, True), (300, True), (300.01, False), (1000, False)):
            zone = Capture()
            zone.capture(0)
            for now in (5000, 10000, 15000):
                zone.tick(now, distance)
            self.assertEqual(zone.permanent, secure)
            self.assertEqual(zone.cleared, secure)
            self.assertEqual(zone.attempts, 3)
        self.assertIn("INF_BlockRules.Nearest(zone.m_vCenter, players) <= 300", GRID)
        self.assertIn("INF_ResetAbandonedZone(cell)", GRID)
        reset = GRID.split("void INF_ResetAbandonedZone(", 1)[1].split("bool INF_MakeRoomForBudget", 1)[0]
        self.assertIn("DeactivateZone(state)", reset)
        self.assertIn("state.m_bEverHadLiving = false", reset)
        self.assertIn("state.m_bReadyToRespawn = true", reset)

    def test_successful_spawn_stays_orange_without_timeout_security(self):
        for attempt in (1, 2, 3):
            zone = Capture()
            zone.capture(0)
            for number in range(1, attempt + 1):
                zone.tick(number * 5000, 300, spawn_ok=number == attempt)
            zone.tick(60000, 300)
            self.assertTrue(zone.active)
            self.assertFalse(zone.pending)
            self.assertFalse(zone.permanent)
        self.assertIn("state.m_bINFAttackPending = false", STATE.split("void AttackStarted", 1)[1])

    def test_green_requires_permanence_and_pending_is_orange(self):
        self.assertIn("state.m_bINFAttackPending || state.m_bINFAttackActive", MAP)
        lookup = GRID.split("bool INF_HasCounterAttackInArea", 1)[1]
        self.assertIn("cell.m_bINFAttackPending || cell.m_bINFAttackActive", lookup)
        self.assertIn("patrols.INF_GetZoneStates().GetCell(center)", lookup)
        self.assertLess(MAP.index("if (state.m_bINFAttackPending ||"), MAP.index("else if (state.m_bINFPermanent)"))

    def test_disabled_attacks_secure_immediately_and_aborts_keep_attempt_count(self):
        zone = Capture()
        zone.capture(0, enabled=False)
        self.assertTrue(zone.permanent)
        self.assertFalse(zone.pending)
        end = STATE.split("void AttackEnded", 1)[1]
        self.assertIn("state.m_bINFAttackPending = true", end)
        self.assertNotIn("state.m_iINFAttackAttempts = 0", end)
        self.assertIn("m_iMaxConcurrentCounterAttacks <= 0", GRID)
        editor = (ROOT / "Scripts/Game/INF_ZoneEditorAttributes.c").read_text()
        interval = editor.split("modded class INS_CounterAttackIntervalEditorAttribute", 1)[1].split("\n}", 1)[0]
        self.assertIn("INF_ZoneEditorContext.Grid(m_GameMode)", interval)
        self.assertIn("return null", interval)
        self.assertIn("super.ReadVariable(item, manager)", interval)

    def test_grid_counterattacks_and_inbound_defenders_receive_running_setting(self):
        manager = GRID.split("modded class INS_CounterAttackManager", 1)[1]
        init = manager.split("override protected void InitSquad(", 1)[1].split(
            "protected void INF_RecordAttackers", 1)[0]
        self.assertLess(init.index("super.InitSquad(squad)"), init.index("settings.AddSetting("))
        self.assertIn("if (squad.m_Zone && squad.m_Zone.m_INFCell)\n\t\t{", init)
        self.assertIn("if (squad.m_Group)", init)
        self.assertIn("SCR_AIGroupSettingsComponent.Cast(squad.m_Group.FindComponent", init)
        self.assertIn(
            "SCR_AIGroupCharactersMovementSpeedSetting.Create(SCR_EAISettingOrigin.SCENARIO, EMovementType.RUN), false, true",
            init)
        self.assertIn("if (!settings)", init)
        self.assertIn("else if (!settings.AddSetting", init)
        self.assertEqual(init.count("LogLevel.ERROR"), 2)
        patrols = GRID.split("modded class INS_CounterAttackManager", 1)[0]
        defender = method_body(patrols, "protected void INF_InitDefenderGroup(")
        self.assertLess(defender.index("InitGroup(agent)"), defender.index("settings.AddSetting("))
        self.assertIn("!Replication.IsServer()", defender)
        self.assertIn("if (!agent.m_bINFInbound || !agent.m_Group)", defender)
        self.assertIn("EMovementType.RUN", defender)
        self.assertEqual(defender.count("LogLevel.ERROR"), 2)
        self.assertIn("CallLater(INF_InitDefenderGroup, 500, false, agent)", patrols)


if __name__ == "__main__":
    unittest.main()
