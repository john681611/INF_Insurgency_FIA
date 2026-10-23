INSURGENCY ADVANCED FIA

Optional FIA compatibility addon.
Project GUID: 52B0C39E793547A2

Requires:
- Insurgency Advanced (78926100260C48E5)
- Existing Insurgency FIA (6A9686E2AB28E681)

This addon overrides Advanced's existing game-mode prefab resource:
  {CC46DF266B89439D}Prefabs/MP/Modes/InsurgencyAdvanced/GameMode_InsurgencyAdvanced.et
It preserves the Advanced settings and replaces only the camp composition:
  USSR: {8809D070C43CD78C}Prefabs/Compositions/Slotted/SlotFlatSmall/PlayerHub_S_USSR_01.et
  FIA:  {B7C71CFC27B3EE24}Prefabs/Compositions/Slotted/SlotFlatSmall/PlayerHub_S_FIA_01.et
The latter is the native FIA Encampment composition, not a standalone tent.
The 9m placement footprint and Advanced's composition preparation remain:
embedded spawn points are disabled and unbudgeted embedded groups rejected.

It also extends Insurgency FIA's Character_FIA_base using the original
faction-base attachment method. The original body-search action, tracker IDs,
LowerTorso context and one-second search remain; capture/question actions and
the replicated prisoner component are added alongside them. No shared
Character_Base override is required. Reload the world/project to apply these
attachments to newly spawned soldiers. Use this compatibility addon, not just
Insurgency FIA, when Advanced capture/question interactions are required.

No gameplay scripts, infantry/vehicle pools, cache prefabs or faction relations
are duplicated here. The existing Insurgency FIA addon remains their owner.
The optional Insurgency FIA Ext Veh addon can still be loaded separately.
No map dependency is introduced, and Advanced's USSR default is untouched
when this compatibility addon is not loaded.

Load this addon alongside a scenario inheriting the Advanced mode, such as
the updated INF Insurgency Everon. Scenarios that explicitly override the
camp composition must remove that override to inherit the FIA composition.

Existing saved camp sites are revalidated against the FIA composition.
Do not delete live saves automatically. Test with a separate server profile
first; previously valid USSR sites are not guaranteed to fit the new layout.

Open in Workbench to index and compile with both dependencies available.
Publish Advanced before publishing this addon; existing Insurgency FIA must
also be available to the server. Restart after changing the loaded mod set.
No generated resource database, saves or server configuration are changed.

Offline validation from this addon:
  python -B -m unittest discover -s .\Tests -p "validate_*.py"
Also run Advanced and Everon tests. In-engine checks remain necessary:
verify FIA camp placement, disabled embedded spawn points, FIA defenders,
counterattacks, cache selection and compatibility with extended vehicles.
