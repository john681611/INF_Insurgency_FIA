"""Offline execution-policy models and native effect/notification wiring checks."""
from itertools import product
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "Scripts/Game/INF_Prisoners.c").read_text()
EFFECTS = ROOT / "Prefabs/Effects/Pastafarian"
EXEMPT = {"BLEEDING"}


def penalty(server=True, captured=True, cooling_down=False, damage=1,
            kind="KINETIC", player=1, offender_alive=True, roll=0):
    return (server and captured and not cooling_down and damage > 0
            and kind not in EXEMPT and player > 0 and offender_alive and roll < .30)


class PrisonerAccountability(unittest.TestCase):
    def test_exact_thirty_percent_threshold(self):
        for roll, expected in ((0, True), (.299999, True), (.30, False), (.999999, False)):
            self.assertEqual(penalty(roll=roll), expected)
        self.assertIn("Math.RandomFloat01() >= 0.30", SOURCE)

    def test_any_player_injury_except_bleeding_counts_once_per_cooldown(self):
        for server, captured, cooling_down in product((False, True), repeat=3):
            self.assertEqual(penalty(server=server, captured=captured, cooling_down=cooling_down),
                             (server, captured, cooling_down) == (True, True, False))
        for kind in ("KINETIC", "EXPLOSIVE", "FRAGMENTATION", "PROCESSED_FRAGMENTATION", "MELEE",
                     "COLLISION", "FIRE", "INCENDIARY", "TRUE"):
            self.assertTrue(penalty(kind=kind))
        self.assertFalse(penalty(kind="BLEEDING"))
        self.assertIn("context.damageType == EDamageType.BLEEDING", SOURCE)
        self.assertFalse(penalty(player=0))
        self.assertFalse(penalty(damage=0))
        self.assertFalse(penalty(offender_alive=False))
        self.assertIn("now < m_fNextPenaltyAt", SOURCE)
        self.assertIn("m_fNextPenaltyAt = now + PENALTY_COOLDOWN_MS", SOURCE)
        self.assertIn("PENALTY_COOLDOWN_MS = 3000", SOURCE)

    def test_native_damage_context_is_bound_only_after_capture_and_unbound_on_delete(self):
        self.assertIn("m_DamageManager.GetOnDamage().Insert(OnCapturedDamage)", SOURCE)
        self.assertLess(SOURCE.index("m_bCaptured = true"),
                        SOURCE.index("GetOnDamage().Insert(OnCapturedDamage)"))
        self.assertIn("GetOnDamage().Remove(OnCapturedDamage)", SOURCE)
        callback = SOURCE.split("protected void OnCapturedDamage(", 1)[1].split(
            "protected void Respond(", 1)[0]
        self.assertNotIn("m_DamageManager.IsDestroyed()", callback)  # injuries count, not only the killing blow
        self.assertIn("context.instigator.GetInstigatorPlayerID()", callback)
        self.assertNotIn("GetInstigator()", callback)  # never blame a stale pre-capture attacker
        self.assertLess(callback.index("m_fNextPenaltyAt = now"), callback.index("Math.RandomFloat01()"))
        self.assertLess(callback.index("context.damageType"), callback.index("Math.RandomFloat01()"))

    def test_lethal_damage_targets_only_the_current_offender(self):
        self.assertIn("players.GetPlayerControlledEntity(attackerID)", SOURCE)
        self.assertIn("offender.FindComponent(SCR_CharacterDamageManagerComponent)", SOURCE)
        self.assertIn("parameters.Transform[3] = offender.GetOrigin()", SOURCE)
        self.assertIn("damage.Kill(Instigator.CreateInstigator(null))", SOURCE)
        effect = (EFFECTS / "PrisonerRebukeEffect.et").read_text()
        for forbidden in ("ExplosionDamageContainer", "DamageValue", "ExplosionImpulseEffect"):
            self.assertNotIn(forbidden, effect)
        self.assertIn("Editor_LightningStrike.acp", effect)

    def test_effect_inherits_native_replicated_lightning_and_preserves_resource_identities(self):
        bolt = (EFFECTS / "PrisonerRebuke.et").read_text()
        self.assertIn('Projectile : "{A8FFCD3F3697D77F}Prefabs/Editor/Lightning/LightningStrike.et"', bolt)
        self.assertIn('ExplosionEffect "{58AAA3E2D7F13E11}"', bolt)
        self.assertIn('ProjectileDamage "{56829D01651CE9A7}" {\n     DamageValue 0', bolt)
        self.assertIn("InitSpeed 0", bolt)
        self.assertIn("ThrustForce 0", bolt)
        self.assertIn("{6AA19CB200000521}Prefabs/Effects/Pastafarian/PrisonerRebukeEffect.et", bolt)
        for name, guid in (("PrisonerRebuke", "6AA19CB200000520"),
                           ("PrisonerRebukeEffect", "6AA19CB200000521")):
            meta = (EFFECTS / (name + ".et.meta")).read_text()
            self.assertIn('Name "{' + guid + '}Prefabs/Effects/Pastafarian/' + name + '.et"', meta)
            for platform in ("PC", "XBOX_ONE", "XBOX_SERIES", "PS4", "PS5", "HEADLESS"):
                self.assertIn("EntityTemplateResourceClass " + platform, meta)

    def test_global_message_names_the_offender_and_only_follows_an_actual_strike(self):
        callback = SOURCE.split("protected void OnCapturedDamage(", 1)[1].split(
            "protected void Respond(", 1)[0]
        self.assertIn("The Flying Spaghetti Monster is displeased with %1 for harming a prisoner. R'amen.", callback)
        self.assertIn("players.GetPlayerName(attackerID)", callback)
        self.assertIn("players.GetPlayers(recipients)", callback)
        self.assertIn("foreach (int playerID : recipients)", callback)
        self.assertIn("player.INF_SendPrisonerResponse(message)", callback)
        self.assertLess(callback.index("SpawnEntityPrefab("), callback.index("damage.Kill("))
        self.assertLess(callback.index("damage.Kill("), callback.index("players.GetPlayers("))
        self.assertIn("lightning resource failed to load", callback)
        self.assertIn("lightning failed to spawn", callback)


if __name__ == "__main__":
    unittest.main()
