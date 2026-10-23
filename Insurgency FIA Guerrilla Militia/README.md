# Insurgency FIA Guerrilla Militia

Compatibility addon for **Insurgency FIA** and **Guerrilla Militia**.
Enable both dependencies alongside this addon.

Adds these groups to the ambient-patrol pool used by Advanced for zone and camp defenders:

- Guerrilla Fireteam (4)
- Guerrilla Squad (8)

Retains these additions to the legacy base-garrison infantry pool:

- Guerrilla Pair (2)
- Guerrilla Fireteam (4)

Existing garrison groups remain available. This addon appends entries to
`INS_GarrisonManager.m_aInfantryGroupPrefabs` and
`INS_AmbientPatrolManager.m_aGroupPrefabs`; it does not change selection
weights, garrison limits, or respawn timing.

Counterattack, field-threat, and vehicle pools are not changed.
Existing spawned garrisons are not replaced; the additions apply when the
garrison system next selects groups.

## Implementation

The [game-mode override](Prefabs/MP/Modes/Editor/GameMode_Editor_Full.et)
follows the resource override convention used by Insurgency FIA and its vehicle
extension. It targets their shared game-mode resource and appends only two
legacy garrison references and two patrol references.

Advanced uses its existing exact-size-first selection: the four-person fireteam
is eligible for four-person allocations. The eight-person squad can supply a
four- or six-person allocation only when the full configured pool has no exact
match, and is trimmed to the reserved size. No extra rolls, weighting, or rarity
controls are introduced; militia frequency depends on the resolved pool.

Changes apply to scenarios using that game-mode prefab. Scenarios that replace
the garrison pool in their own overrides may require an additional compatibility
patch. Other addons replacing this same pool may also affect the final list.

## Validation

```powershell
python -B -m unittest discover -s Tests -v
```

Static tests check dependency IDs, append-only scope, group references, and that
unrelated managers are not overridden. Engine-side validation should confirm the
resolved patrol pool includes its original FIA entries plus the fireteam and
eight-person squad, and that the counterattack pool is unchanged.

For gameplay validation, start a fresh Insurgency FIA session and inspect newly
generated zone/camp defenders in Advanced, or base garrisons in legacy mode.
Random selection does not guarantee a Guerrilla group in every location.
