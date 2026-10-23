"""Offline transition models and source contracts; not Enfusion execution."""
from pathlib import Path
import unittest

from source_contracts import method_body

ROOT = Path(__file__).resolve().parents[1]
GRID = (ROOT / "Scripts/Game/INF_BuildingGrid.c").read_text()
STATE = (ROOT / "Scripts/Game/INF_ZoneStateSystem.c").read_text()
RULES = (ROOT / "Scripts/Game/INF_BlockControl.c").read_text()
MARKERS = (ROOT / "Scripts/Game/INF_GarrisonMapAreas.c").read_text()


class Attempt:
    def __init__(self):
        self.failures = 0
        self.ever_living = False
        self.exhausted = False
        self.active = False

    def activate(self, admitted=True):
        if not admitted or self.exhausted:
            return False
        self.active = True
        self.ever_living = False
        return True

    def observe(self, living):
        if living:
            self.ever_living = True
            self.failures = 0
            self.exhausted = False

    def finish(self, countable=True, pending=0, stale=False):
        if stale or not self.active or pending:
            return
        if countable and not self.ever_living:
            self.failures += 1
        self.exhausted = self.failures >= 3
        self.active = False


def inbound_wait(now, deadline, distances, materialisation_deadline=35000):
    if distances and deadline < 0:
        deadline = now + 300000
    arrived = (not distances and (deadline >= 0 or now >= materialisation_deadline)) or any(
        d <= 200 for d in distances)
    return not (arrived or (deadline >= 0 and now >= deadline)), deadline


def marker(attack=False, permanent=False, capture=False, inbound=False, active=False):
    for enabled, label in ((attack, "attack"), (permanent, "permanent"),
                           (capture, "capture"), (inbound, "inbound"), (active, "active")):
        if enabled:
            return label
    return "dormant"


class Capture:
    def __init__(self):
        self.since = -1

    def tick(self, now, *, exhausted=True, living=False, pending=0, settled=True,
             grounded=True, on_foot=True, in_range=True, enemies=False):
        eligible = ((living or exhausted) and pending == 0 and settled and
                    grounded and on_foot and in_range and not enemies)
        if not eligible:
            self.since = -1
            return False
        if self.since < 0:
            self.since = now
        return now - self.since >= 15000


class InitialDefenderRecovery(unittest.TestCase):
    def test_capture_priority_and_eviction_protection_share_radius_not_eligibility(self):
        occupied = method_body(RULES, "bool Occupied(")
        priority = method_body(RULES, "static bool CapturerOccupied(")
        capture = method_body(RULES, "bool CanCapture(")
        self.assertIn("foreach (vector player : m_Players)", occupied)
        self.assertIn("INF_BlockRules.InCaptureArea(center, player)", occupied)
        self.assertIn("foreach (vector player : presence.m_Capturers)", priority)
        self.assertIn("InCaptureArea(center, player)", priority)
        self.assertIn("foreach (vector player : m_Capturers)", capture)
        self.assertIn("INF_BlockRules.InCaptureArea(center, player)", capture)
        self.assertNotIn("INF_BuildingGrid.Contains", occupied + priority)
        for pos, eligible, expected_priority, expected_protection in (
                ((90, 0), True, True, True),
                ((90, 0), False, False, True),
                ((100, 0), True, True, True),
                ((100.01, 0), True, False, False),
                ((75, 75), True, False, False)):
            within = sum(axis * axis for axis in pos) <= 10000
            self.assertEqual(within and eligible, expected_priority)
            self.assertEqual(within, expected_protection)
        self.assertIn("INF_BuildingGrid.Contains(state.m_vCenter, attacker)",
                      method_body(STATE, "void CheckRetake("))
        self.assertIn("INF_BuildingGrid.Contains(state.m_vCenter, empty)",
                      method_body(GRID, "override protected vector FindSafeSpawnPosition("))

    def test_player_capture_is_circular_100m_with_unchanged_fifteen_second_hold(self):
        for center in ((0, 0), (-150, -50), (250, 350)):
            for offset, expected in (
                    ((0, 0), True), ((100, 0), True), ((-100, 0), True),
                    ((0, 100), True), ((0, -100), True), ((60, 80), True),
                    ((-60, -80), True), ((99.99, 0), True),
                    ((100.01, 0), False), ((60, 80.01), False),
                    ((75, 75), False), ((90, 90), False)):
                player = tuple(a + b for a, b in zip(center, offset))
                with self.subTest(center=center, offset=offset):
                    self.assertEqual(sum((a - b) ** 2 for a, b in zip(center, player)) <= 100 ** 2,
                                     expected)
        area = method_body(RULES, "static bool InCaptureArea(")
        self.assertIn("vector.DistanceSqXZ(center, player) <= PLAYER_CAPTURE_RADIUS * PLAYER_CAPTURE_RADIUS", area)
        self.assertIn("static const float PLAYER_CAPTURE_RADIUS = 100;", RULES)
        self.assertNotIn("PLAYER_CAPTURE_MARGIN", RULES)
        self.assertIn("static const float CAPTURE_HOLD_MS = 15000;", RULES)
        self.assertIn("INF_BlockRules.InCaptureArea(center, player)", method_body(RULES, "bool CanCapture("))
        retake = method_body(STATE, "void CheckRetake(")
        self.assertIn("vector.DistanceSqXZ(player, state.m_vCenter) <= 40000", retake)
        manual = (ROOT / "Configs/FieldManual/FieldManualConfigRoot.conf").read_text()
        self.assertIn("within 100m of the zone centre for 15 seconds", manual)

    def test_multiple_evictions_preserve_farthest_first_order(self):
        possible = [(900, 1), (800, 0), (700, 1), (600, 0)]
        evicted = []
        for required_group in (1, None, None):
            index = next(i for i, (_, group) in enumerate(possible)
                         if required_group is None or group == required_group)
            evicted.append(possible.pop(index)[0])
        self.assertEqual(evicted, [900, 800, 700])
        budget = method_body(GRID, "bool INF_MakeRoomForBudget(")
        self.assertIn("possible.RemoveOrdered(farthest)", budget)
        self.assertNotIn("possible.Remove(farthest)", budget)

    def test_previously_living_inbound_group_elimination_resolves_before_grace(self):
        waiting, deadline = inbound_wait(1000, -1, [500])
        self.assertTrue(waiting)
        self.assertFalse(inbound_wait(2000, deadline, [])[0])
        self.assertTrue(inbound_wait(2000, -1, [])[0])
        settled = method_body(STATE, "bool ReinforcementsSettled(")
        self.assertIn("living.IsEmpty() && (agent.m_fINFInboundDeadline >= 0", settled)

    def test_defender_maintenance_tracks_only_outstanding_observations_and_arrivals(self):
        cases = [(True, False, False), (True, True, True),
                 (True, True, False), (False, False, True)]
        tracked = [i for i, (active, observed, inbound) in enumerate(cases)
                   if active and (not observed or inbound)]
        self.assertEqual(tracked, [0, 1])
        notify = method_body(STATE, "void NotifyChanged(")
        self.assertIn("state.m_bActive && (!state.m_bEverHadLiving || state.m_bINFInboundWaiting)", notify)
        self.assertIn("m_DefenderChecks.RemoveItem(state)", notify)
        tick = method_body(STATE, "protected void CaptureTick(")
        self.assertIn("defenderChecks.Copy(m_DefenderChecks)", tick)
        self.assertNotIn("Copy(m_Active)", tick)
        self.assertEqual(tick.count("ReinforcementsSettled("), 1)
        self.assertIn("CheckCapture(state, presence, !state.m_bINFInboundWaiting)", tick)
        observed = method_body(STATE, "void ObserveDefenders(")
        self.assertIn("if (!state.m_bINFInboundWaiting)", observed)
        self.assertIn("m_DefenderChecks.RemoveItem(state)", observed)
        settled = method_body(STATE, "bool ReinforcementsSettled(")
        self.assertIn("GetPresence(false, false, 1000)", settled)
        self.assertIn("presence.GroupPositions(agent.m_Group)", settled)
        self.assertNotIn("GetPresence(true", settled)

    def test_three_complete_failures_and_no_repeat_activation(self):
        attempt = Attempt()
        for failures in (1, 2, 3):
            self.assertTrue(attempt.activate())
            attempt.finish(pending=1)
            self.assertEqual(attempt.failures, failures - 1)
            attempt.finish()
            self.assertEqual(attempt.failures, failures)
            self.assertEqual(attempt.exhausted, failures == 3)
            self.assertFalse(attempt.ever_living)
        self.assertFalse(attempt.activate())

    def test_partial_living_success_resets_failures_not_group_placeholders(self):
        attempt = Attempt()
        for _ in range(2):
            attempt.activate()
            attempt.finish()
        attempt.activate()
        attempt.observe(0)  # A group entity alone is not success.
        self.assertEqual(attempt.failures, 2)
        attempt.observe(1)
        attempt.observe(0)
        attempt.finish()
        self.assertEqual(attempt.failures, 0)
        self.assertFalse(attempt.exhausted)

    def test_rejections_stale_callbacks_and_deactivation_do_not_count(self):
        attempt = Attempt()
        self.assertFalse(attempt.activate(admitted=False))
        attempt.finish(stale=True)
        self.assertEqual(attempt.failures, 0)
        attempt.activate()
        attempt.finish(countable=False)
        self.assertEqual(attempt.failures, 0)
        attempt.activate()
        attempt.active = False
        attempt.finish()
        self.assertEqual(attempt.failures, 0)
        callback = method_body(GRID, "protected void INF_SpawnQueued(")
        self.assertLess(callback.index("state.m_iINFActivationGeneration != generation"),
                        callback.index("state.m_iINFPendingGroups--"))
        self.assertIn("state.m_bINFAttemptCountable = false", callback)
        self.assertNotIn("m_iINFFailedActivations++",
                         method_body(GRID, "override protected void DeactivateZone("))
        activation = method_body(GRID, "override protected void ActivateZone(")
        self.assertLess(activation.index("INF_MakeRoom(state)"),
                        activation.index("state.m_bINFAttemptCountable = true"))

    def test_materialisation_timeout_and_complete_failure_helper(self):
        tick = method_body(GRID, "override protected void TickProximityCheck(")
        self.assertIn("state.m_iINFPendingGroups == 0 && !state.m_bEverHadLiving", tick)
        self.assertIn("now >= state.m_fINFSpawnReadyAt + 30000", tick)
        self.assertIn("!INF_BlockRules.HasMembers(state.m_aAgents)", tick)
        self.assertIn("INF_FailedActivation(state)", tick)
        for elapsed, failed in ((29999, False), (30000, True), (30001, True)):
            self.assertEqual(elapsed >= 30000, failed)
        failure = method_body(GRID, "protected void INF_FailedActivation(")
        self.assertIn("state.m_bINFAttemptCountable && !state.m_bEverHadLiving", failure)
        self.assertIn("state.m_iINFFailedActivations >= 3", failure)
        self.assertNotIn("m_bEverHadLiving = true", failure)
        observe = method_body(STATE, "void ObserveDefenders(")
        self.assertIn("INF_BlockRules.HasMembers(state.m_aAgents, presence)", observe)
        self.assertIn("state.m_iINFFailedActivations = 0", observe)
        self.assertIn("state.m_bINFSpawnExhausted = false", observe)

    def test_exhaustion_keeps_capture_workset_and_all_normal_gates(self):
        notify = method_body(STATE, "void NotifyChanged(")
        self.assertIn("state.m_bINFSpawnExhausted", notify)
        capture = method_body(STATE, "void CheckCapture(")
        self.assertIn("(state.m_bEverHadLiving || state.m_bINFSpawnExhausted)", capture)
        self.assertIn("state.m_iINFPendingGroups == 0", capture)
        self.assertIn("settled && presence.CanCapture(state.m_vCenter)", capture)
        self.assertIn("presence = GetPresence(true, true)", capture)
        self.assertIn("CompleteCapture(state)", capture)
        self.assertIn("foreach (vector player : m_Capturers)", method_body(RULES, "bool CanCapture("))
        self.assertIn("if (!candidate.m_bINFSpawnExhausted", GRID)

    def test_normal_hold_boundary_and_each_gate_cancels_progress(self):
        capture = Capture()
        self.assertFalse(capture.tick(1000))
        self.assertFalse(capture.tick(15999))
        self.assertTrue(capture.tick(16000))
        for gate in ({"exhausted": False}, {"pending": 1}, {"settled": False},
                     {"grounded": False}, {"on_foot": False}, {"in_range": False},
                     {"enemies": True}):
            capture = Capture()
            self.assertFalse(capture.tick(0))
            self.assertFalse(capture.tick(14999, **gate))
            self.assertEqual(capture.since, -1)
            self.assertFalse(capture.tick(15000))
            self.assertFalse(capture.tick(29999))
            self.assertTrue(capture.tick(30000))
        capture = Capture()
        self.assertFalse(capture.tick(0, exhausted=False, living=True))
        self.assertTrue(capture.tick(15000, exhausted=False, living=True))

    def test_five_minute_exact_boundary_and_arrival(self):
        waiting, deadline = inbound_wait(1000, -1, [201, 500])
        self.assertTrue(waiting)
        self.assertEqual(deadline, 301000)
        for now, expected in ((300999, True), (301000, False), (301001, False)):
            self.assertEqual(inbound_wait(now, deadline, [500])[0], expected)
        self.assertFalse(inbound_wait(2000, deadline, [200, 500])[0])
        self.assertTrue(inbound_wait(2000, deadline, [200.01])[0])
        self.assertTrue(inbound_wait(34999, -1, [])[0])
        self.assertFalse(inbound_wait(35000, -1, [])[0])
        settled = method_body(STATE, "bool ReinforcementsSettled(")
        self.assertIn("agent.m_fINFInboundDeadline = now + 300000", settled)
        self.assertIn("now >= agent.m_fINFInboundDeadline", settled)
        self.assertIn("vector.DistanceSqXZ(soldier, state.m_vCenter) <= 40000", settled)
        self.assertIn("state.m_bINFInboundWaiting = waiting", settled)
        self.assertIn("NotifyChanged(state)", settled)
        self.assertNotIn("Delete", settled)
        self.assertNotIn("CallLater", settled)
        deactivate = method_body(GRID, "override protected void DeactivateZone(")
        self.assertIn("state.m_bINFInboundWaiting = false", deactivate)

    def test_marker_priority_and_appended_replication_value(self):
        self.assertEqual(marker(active=True, inbound=True), "inbound")
        self.assertEqual(marker(active=True, inbound=True, capture=True), "capture")
        self.assertEqual(marker(inbound=True, permanent=True, capture=True), "permanent")
        self.assertEqual(marker(attack=True, permanent=True, inbound=True), "attack")
        self.assertIn("CAPTURING,\n\tINBOUND", MARKERS)
        update = method_body(MARKERS, "protected void INF_UpdateZoneAreas(")
        self.assertLess(update.index("state.m_fINFCaptureSince"), update.index("state.m_bINFInboundWaiting"))
        self.assertLess(update.index("state.m_bINFInboundWaiting"), update.index("else if (state.m_bActive)"))
        self.assertNotIn("GroupPositions", update)
        self.assertIn("Color.FromRGBA(155, 90, 180, ACTIVE_ALPHA)", MARKERS)
        manual = (ROOT / "Configs/FieldManual/FieldManualConfigRoot.conf").read_text()
        self.assertIn("Defenders approaching", manual)
        self.assertIn("five minutes", manual)

    def test_occupied_priority_keeps_distance_ties_and_all_eviction_protections(self):
        zones = [(False, 10, "near"), (True, 80, "occupied-far"),
                 (True, 30, "occupied-near"), (False, 20, "next")]
        self.assertEqual([z[2] for z in sorted(zones, key=lambda z: (not z[0], z[1]))],
                         ["occupied-near", "occupied-far", "near", "next"])
        body = method_body(GRID, "bool INF_MakeRoomForBudget(")
        self.assertIn("INF_BlockRules.CapturerOccupied(center, presence)", body)
        self.assertIn("!presence.Occupied(existing.m_vCenter)", body)
        self.assertIn("existing.m_bINFMarkedCleared || existing.m_fINFCaptureSince < 0", body)
        self.assertIn("reserved + required <= INF_BuildingGrid.ACTIVE_BUDGET", body)
        self.assertIn("localActive < localLimit", body)
        final = body.split("presence = m_INFZoneStates.GetPresence(false, true)", 1)[1]
        self.assertIn("presence.Occupied(check.m_vCenter)", final)
        self.assertIn("!INF_BlockRules.SoldiersDistant(check.m_aAgents, presence.m_Players, INF_BlockRules.EVICTION_PROTECTION_RADIUS)", final)
        for occupied, hold, nearby_ai in ((True, False, False), (False, True, False),
                                         (False, False, True), (True, True, True)):
            self.assertFalse(not occupied and not hold and not nearby_ai)
        self.assertIn("if (possible.IsEmpty())\n\t\t\t\treturn false", body)

    def test_priority_never_bypasses_global_or_local_caps(self):
        for global_used, local_used, expected in (
                (124, 3, True), (125, 3, False), (128, 3, False),
                (124, 4, False), (128, 4, False)):
            required = 4
            # With no safe victim, priority cannot alter admission arithmetic.
            admitted = global_used + required <= 128 and local_used < 4
            self.assertEqual(admitted, expected)
        policy = method_body(GRID, "bool INF_MakeRoomForBudget(")
        self.assertIn("policy.GetGroup(possible[i].m_vCenter, m_fActivationRadius) != targetGroup", policy)
        self.assertIn("localActive >= localLimit", policy)
        self.assertIn("active >= INF_BlockRules.MAX_BLOCKS", policy)
        final = policy.split("presence = m_INFZoneStates.GetPresence(false, true)", 1)[1]
        self.assertLess(final.index("presence.Occupied(check.m_vCenter)"),
                        final.index("DeactivateZone(eviction)"))
        self.assertIn("!check.m_bINFMarkedCleared && check.m_fINFCaptureSince >= 0", final)


if __name__ == "__main__":
    unittest.main()
