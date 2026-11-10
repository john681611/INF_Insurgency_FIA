class INF_PrisonerDialogue
{
	static string Reply(bool helpful, bool officer = false)
	{
		if (officer)
		{
			array<string> officerHelpful = {
				"Very well. The orders came through my hands. I will show you where to look.",
				"You have made your point. Our stores are kept where the road bends.",
				"I will tell you what I know, but you did not hear it from me."
			};
			array<string> officerRefusals = {
				"I outrank everything you can ask me.",
				"You will get nothing from me. Ask again if you enjoy disappointment.",
				"Name, rank and a firm refusal. That is all I have for you.",
				"Those orders were above even my clearance. Irritating, is it not?"
			};
			if (helpful)
				return officerHelpful[Math.RandomInt(0, officerHelpful.Count())];
			return officerRefusals[Math.RandomInt(0, officerRefusals.Count())];
		}
		array<string> helpfulLines = {
				"All right. There is a weapons stash. I will show you the area.",
				"We moved supplies through there. That should help narrow your search.",
				"I saw where they took the weapons. Mark this on your map.",
				"I will talk. Just do not tell them who pointed you in the right direction."
		};
		array<string> refusals = {
				"I have nothing useful to tell you.",
				"They never told me where the supplies were kept.",
				"I know nothing. It is my most consistent skill.",
				"My map was upside down. I am not your best source.",
				"Ask logistics. They lost it before you found me.",
				"I was told there would be lunch, not questions.",
				"That sounds above my pay grade. Most things are.",
				"I am saving my best information for the complaint form.",
				"I followed the man in front. He looked confident.",
				"I could give you directions, but we would both end up at the wrong farm.",
				"No. You have already had a better day than me.",
				"I heard rumours. Nothing you could put on a map."
		};
		if (helpful)
			return helpfulLines[Math.RandomInt(0, helpfulLines.Count())];
		return refusals[Math.RandomInt(0, refusals.Count())];
	}
}

[ComponentEditorProps(category: "Insurgency", description: "Secures unconscious hostile AI and allows one conscious questioning attempt.")]
class INF_PrisonerComponentClass : ScriptComponentClass
{
}

// Records one weapon being deleted from the prisoner's inventory; a clean copy is spawned at the pile once the server has finished.
class INF_PrisonerDropCallback : ScriptedInventoryOperationCallback
{
	IEntity m_Item;
	ResourceName m_Prefab;
	float m_fLength;
	bool m_bDone;
	bool m_bFailed;

	override protected void OnComplete()
	{
		m_bDone = true;
	}

	override protected void OnFailed()
	{
		m_bFailed = true;
	}
}

class INF_PrisonerComponent : ScriptComponent
{
	[RplProp(onRplName: "OnCapturedChanged")]
	protected bool m_bCaptured;

	[RplProp()]
	protected int m_iQuestions;
	// Officers are flagged by the zone that spawns them; they take three questioning attempts and one search.
	[RplProp()]
	protected bool m_bOfficer;
	[RplProp()]
	protected bool m_bOfficerSearched;
	protected string m_sOfficerKey;
	protected bool m_bWeaponsDropped;
	protected ref array<ref INF_PrisonerDropCallback> m_aPendingDrops = {};
	protected vector m_vPile;
	protected bool m_bPileSet;
	protected static const float PILE_SCATTER_SMALL = 0.10;
	protected static const float PILE_SCATTER_LARGE = 0.30;
	protected static const float PILE_LARGE_ITEM = 0.5;
	// Spawned a little above the ground so PlaceOnGround's downward trace starts clear of the surface.
	protected static const float PILE_SPAWN_HEIGHT = 0.3;
	protected ELoiteringType m_PrisonerLoiter = ELoiteringType.NONE;
	protected float m_fNextHandlingWarning;
	protected float m_fNextLoiterAttempt;
	protected float m_fNextACESurrenderAttempt;
	protected SCR_CharacterDamageManagerComponent m_DamageManager;
	protected float m_fNextPenaltyAt;
	protected bool m_bDeathSeen;
	protected float m_fNextIgnoredLog;
	protected static const float PENALTY_COOLDOWN_MS = 3000;
	// Glancing bumps and scrapes do not count as abuse.
	protected static const float MIN_COUNTED_DAMAGE = 5;

	// Bleeding and its fatal "true" damage carry the original attacker, so a prisoner dying of wounds from before capture must not blame the captor.
	protected static bool IsExemptDamage(EDamageType type)
	{
		return type == EDamageType.BLEEDING || type == EDamageType.TRUE
			|| type == EDamageType.REGENERATION || type == EDamageType.HEALING;
	}
	protected float m_fCleanupRadius = -1;
	protected float m_fCleanupDelay;
	protected float m_fDistantSince = -1;
	protected bool m_bCleanupConcealed;
	protected bool m_bCleanupAnchor;
	protected vector m_vCleanupAnchor;
	protected ref INS_AmbientVehicleAgent m_CleanupVehicle;
	protected float m_fCleanupGraceUntil;
	protected float m_fNextCleanupCheck;

	void INF_SetVehicleGraceSource(INS_AmbientVehicleAgent vehicle)
	{
		m_CleanupVehicle = vehicle;
		m_fCleanupGraceUntil = GetGame().GetWorld().GetWorldTime() + Math.Max(0, vehicle.m_fPlayerGraceRemaining) * 1000;
	}

	void INF_SetCleanupRules(float radius, float delay, bool concealed, vector anchor = vector.Zero, bool useAnchor = false)
	{
		m_fCleanupRadius = radius;
		m_fCleanupDelay = delay;
		m_bCleanupConcealed = concealed;
		m_vCleanupAnchor = anchor;
		m_bCleanupAnchor = useAnchor;
	}

	protected void BindCleanupRules(AIGroup group)
	{
		IEntity mode = GetGame().GetGameMode();
		INS_AmbientVehicleManager vehicles = INS_AmbientVehicleManager.Cast(mode.FindComponent(INS_AmbientVehicleManager));
		if (vehicles && vehicles.INF_BindPrisonerCleanup(group, this))
			return;
		INS_CounterAttackManager attacks = INS_CounterAttackManager.Cast(mode.FindComponent(INS_CounterAttackManager));
		if (attacks && attacks.INF_BindPrisonerCleanup(group, this))
			return;
		INS_FieldThreatManager threats = INS_FieldThreatManager.Cast(mode.FindComponent(INS_FieldThreatManager));
		if (threats && threats.INF_BindPrisonerCleanup(group, this))
			return;
		INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(mode.FindComponent(INS_AmbientPatrolManager));
		if (patrols && patrols.INF_BindPrisonerCleanup(group, this))
			return;
		Print("[INF_Prisoners] Prisoner has no managed spawn source; preserving it rather than inventing cleanup rules.", LogLevel.WARNING);
	}

	bool INF_CanCleanup()
	{
		if (!Replication.IsServer() || !m_bCaptured || m_fCleanupRadius < 0)
			return false;
		float now = GetGame().GetWorld().GetWorldTime();
		if (m_CleanupVehicle)
		{
			INS_AmbientVehicleManager manager = INS_AmbientVehicleManager.Cast(
				GetGame().GetGameMode().FindComponent(INS_AmbientVehicleManager));
			if (manager && manager.INF_IsTrackedAmbientVehicle(m_CleanupVehicle))
				m_fCleanupGraceUntil = now + Math.Max(0, m_CleanupVehicle.m_fPlayerGraceRemaining) * 1000;
		}
		array<vector> players = INF_BlockPresence.PlayerPositions(true);
		vector position = GetOwner().GetOrigin();
		bool distant = INF_BlockRules.Nearest(position, players) > m_fCleanupRadius;
		if (m_bCleanupAnchor)
			distant = distant && INF_BlockRules.Nearest(m_vCleanupAnchor, players) > m_fCleanupRadius;
		if (m_bCleanupConcealed)
			distant = distant && INF_BlockRules.IsConcealed(position, players, 3);
		if (!distant)
		{
			m_fDistantSince = -1;
			return false;
		}
		if (m_fDistantSince < 0)
			m_fDistantSince = now;
		return now >= m_fCleanupGraceUntil && now - m_fDistantSince >= m_fCleanupDelay;
	}

	static bool INF_Preserve(IEntity entity)
	{
		INF_PrisonerComponent prisoner = Find(entity);
		if (!prisoner || !prisoner.IsCaptured())
			return false;
		DamageManagerComponent damage = DamageManagerComponent.Cast(entity.FindComponent(DamageManagerComponent));
		return (!damage || !damage.IsDestroyed()) && !prisoner.INF_CanCleanup();
	}

	static INF_PrisonerComponent Find(IEntity entity)
	{
		if (!entity)
			return null;
		return INF_PrisonerComponent.Cast(entity.FindComponent(INF_PrisonerComponent));
	}

	bool IsCaptured()
	{
		return m_bCaptured;
	}

	bool IsQuestioned()
	{
		return m_iQuestions >= QuestionLimit();
	}

	int QuestionLimit()
	{
		if (m_bOfficer)
			return INF_Officers.QUESTION_LIMIT;
		return 1;
	}

	bool IsOfficer()
	{
		return m_bOfficer;
	}

	bool IsOfficerSearched()
	{
		return m_bOfficerSearched;
	}

	void INF_MarkOfficer(string key)
	{
		if (!Replication.IsServer())
			return;
		m_bOfficer = true;
		m_sOfficerKey = key;
		Replication.BumpMe();
		m_DamageManager = SCR_CharacterDamageManagerComponent.Cast(GetOwner().FindComponent(SCR_CharacterDamageManagerComponent));
		if (m_DamageManager)
			m_DamageManager.GetOnDamage().Insert(OnOfficerDamage);
		else
			Print("[INF_Officers] Officer has no character damage manager; his death will not be recorded.", LogLevel.ERROR);
	}

	protected void OnOfficerDamage(notnull BaseDamageContext context)
	{
		if (m_bOfficer && m_DamageManager && m_DamageManager.IsDestroyed())
			OfficerOut();
	}

	protected void OfficerOut()
	{
		if (!m_bOfficer || m_sOfficerKey.IsEmpty())
			return;
		INF_Officers.Down(m_sOfficerKey);
		m_sOfficerKey = string.Empty;
	}

	void SearchOfficer(IEntity user)
	{
		if (!Replication.IsServer() || !m_bOfficer || m_bOfficerSearched)
			return;
		m_bOfficerSearched = true;
		Replication.BumpMe();
		INS_IntelManager intel = INS_IntelManager.GetInstance();
		int playerID = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(user);
		bool helpful = intel && intel.INF_HasQuestioningIntel() && Math.RandomFloat01() < INF_Officers.SEARCH_CHANCE;
		if (helpful)
			intel.AddIntelAndBroadcast(1, playerID);
		if (helpful)
			Respond(user, "The officer carried orders that point to a weapons cache. Check your map.");
		else
			Respond(user, "The officer's papers hold nothing useful.");
	}

	protected bool IsUsableBy(IEntity user)
	{
		if (!GetOwner() || !user || user == GetOwner()
			|| !INF_ZoneEditorContext.Grid(GetGame().GetGameMode()))
			return false;
		PlayerManager players = GetGame().GetPlayerManager();
		if (players.GetPlayerIdFromControlledEntity(user) <= 0
			|| players.GetPlayerIdFromControlledEntity(GetOwner()) > 0)
			return false;
		CharacterControllerComponent controller = CharacterControllerComponent.Cast(user.FindComponent(CharacterControllerComponent));
		if (!controller || controller.GetLifeState() != ECharacterLifeState.ALIVE || controller.IsUnconscious())
			return false;
		CompartmentAccessComponent access = CompartmentAccessComponent.Cast(user.FindComponent(CompartmentAccessComponent));
		return (!access || !access.IsInCompartment())
			&& vector.DistanceSq(user.GetOrigin(), GetOwner().GetOrigin()) <= 9;
	}

	bool CanCapture(IEntity user)
	{
		if (m_bCaptured || !IsUsableBy(user))
			return false;
		ChimeraCharacter target = ChimeraCharacter.Cast(GetOwner());
		if (!target || !target.GetAIControlComponent())
			return false;
		CharacterControllerComponent controller = target.GetCharacterController();
		if (!controller || controller.GetLifeState() != ECharacterLifeState.INCAPACITATED)
			return false;
		FactionAffiliationComponent targetFaction = FactionAffiliationComponent.Cast(target.FindComponent(FactionAffiliationComponent));
		FactionAffiliationComponent userFaction = FactionAffiliationComponent.Cast(user.FindComponent(FactionAffiliationComponent));
		if (!targetFaction || !userFaction)
			return false;
		Faction hostile = targetFaction.GetAffiliatedFaction();
		Faction friendly = userFaction.GetAffiliatedFaction();
		return hostile && friendly && friendly.IsFactionEnemy(hostile);
	}

	bool CanQuestion(IEntity user, bool includeBleeding = false)
	{
		if (!m_bCaptured || IsQuestioned() || !IsUsableBy(user))
			return false;
		CharacterControllerComponent controller = CharacterControllerComponent.Cast(GetOwner().FindComponent(CharacterControllerComponent));
		if (!controller || controller.GetLifeState() != ECharacterLifeState.ALIVE || controller.IsUnconscious())
			return false;
		SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(GetOwner().FindComponent(SCR_CharacterDamageManagerComponent));
		return damage && (includeBleeding || !damage.IsBleeding());
	}

	bool CanSearch(IEntity user)
	{
		return m_bCaptured && IsUsableBy(user);
	}

	void Capture(IEntity user)
	{
		if (!Replication.IsServer())
			return;
		if (!CanCapture(user))
		{
			Respond(user, "Capture is no longer available. Only unconscious hostile AI can be secured.");
			return;
		}
		ChimeraCharacter target = ChimeraCharacter.Cast(GetOwner());
		AIControlComponent control = target.GetAIControlComponent();
		AIAgent agent = control.GetControlAIAgent();
		AIGroup group;
		if (agent)
			group = agent.GetParentGroup();

		m_DamageManager = SCR_CharacterDamageManagerComponent.Cast(target.FindComponent(SCR_CharacterDamageManagerComponent));
		if (!m_DamageManager)
		{
			Print("[INF_Prisoners] Cannot secure prisoner: character damage manager is missing.", LogLevel.ERROR);
			Respond(user, "This enemy cannot be secured: their medical component is missing.");
			return;
		}
		m_bCaptured = true;
		OfficerOut();
		INF_BlockPresence.InvalidateWorld();
		BindCleanupRules(group);
		m_DamageManager.GetOnDamage().Insert(OnCapturedDamage);
		DropCarriedWeapons();
		if (!GetGame().GetFactionManager().GetFactionByKey("CIV"))
			Print("[INF_Prisoners] No CIV faction configured; securing prisoner as an unaffiliated noncombatant.");
		ApplyRestraints();
		// Detach before managed squad cleanup so a secured prisoner is not despawned with it.
		if (group)
			group.RemoveAgent(agent);
		Replication.BumpMe();
		GetGame().GetCallqueue().CallLater(MaintainRestraints, 1000, false);
		Respond(user, "Prisoner secured. Stop their bleeding and treat their wounds; they must be conscious before questioning.");
	}

	void Question(IEntity user)
	{
		if (!Replication.IsServer())
			return;
		if (!CanQuestion(user))
		{
			Respond(user, "Questioning is unavailable. The prisoner must be secured, conscious, no longer bleeding and not already questioned.");
			return;
		}
		INS_IntelManager intel = INS_IntelManager.GetInstance();
		if (!intel || !intel.INF_IsIntelReady())
		{
			Print("[INF_Prisoners] Questioning deferred: intel system not ready.", LogLevel.WARNING);
			Respond(user, "Intel reporting is not ready yet. Try questioning again shortly.");
			return;
		}
		int playerID = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(user);
		SCR_PlayerController player = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerID));
		if (!player)
		{
			Print("[INF_Prisoners] Cannot deliver questioning result: player controller missing.", LogLevel.ERROR);
			return;
		}

		// Questioning and physical searching each have their own team-wide attempt.
		m_iQuestions++;
		Replication.BumpMe();
		float chance = intel.INF_GetQuestioningChance();
		if (m_bOfficer)
			chance = INF_Officers.QUESTION_CHANCE;
		bool helpful = intel.INF_HasQuestioningIntel() && Math.RandomFloat01() < chance;
		if (helpful)
			intel.AddIntelAndBroadcast(1, playerID);
		player.INF_SendPrisonerResponse(INF_PrisonerDialogue.Reply(helpful, m_bOfficer));
	}

	// Any player-attributed injury after capture counts, not only the killing blow; a burst of hits is one offence.
	protected void OnCapturedDamage(notnull BaseDamageContext context)
	{
		if (!Replication.IsServer() || !m_bCaptured || m_bDeathSeen)
			return;
		// Only the blow that kills can count; anything landing on a body afterwards (or its bleed-out) is not abuse.
		if (m_DamageManager && m_DamageManager.IsDestroyed())
			m_bDeathSeen = true;
		float now = GetGame().GetWorld().GetWorldTime();
		if (now < m_fNextPenaltyAt)
			return;
		int attackerID;
		if (context.instigator)
			attackerID = context.instigator.GetInstigatorPlayerID();
		if (context.damageValue < MIN_COUNTED_DAMAGE || attackerID <= 0 || IsExemptDamage(context.damageType))
		{
			// Bleeding, its final kill and other non-attack damage are exempt; other ignored damage is logged to spot an attack that slips through.
			if (now >= m_fNextIgnoredLog && context.damageType != EDamageType.BLEEDING)
			{
				m_fNextIgnoredLog = now + 5000;
				Print(string.Format("[INF_Prisoners] Prisoner damage not attributed to a player (damage %1, type %2, attacker player %3).", context.damageValue, typename.EnumToString(EDamageType, context.damageType), attackerID));
			}
			return;
		}
		PlayerManager players = GetGame().GetPlayerManager();
		IEntity offender = players.GetPlayerControlledEntity(attackerID);
		if (!offender)
		{
			Print(string.Format("[INF_Prisoners] Prisoner injured; attacker player %1 has no controlled entity.", attackerID), LogLevel.WARNING);
			return;
		}
		SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(offender.FindComponent(SCR_CharacterDamageManagerComponent));
		if (!damage || damage.IsDestroyed())
			return;
		m_fNextPenaltyAt = now + PENALTY_COOLDOWN_MS;
		Print(string.Format("[INF_Prisoners] Prisoner abuse counted (damage %1, type %2, attacker player %3).", context.damageValue, typename.EnumToString(EDamageType, context.damageType), attackerID));

		string offenderName = players.GetPlayerName(attackerID);
		if (Math.RandomFloat01() >= 0.20)
		{
			BroadcastPrisonerWarning(string.Format("The Flying Spaghetti Monster saw what %1 did to a prisoner. He is watching, and His patience is not endless.", offenderName));
			return;
		}

		Resource lightning = Resource.Load("{6AA19CB200000520}Prefabs/Effects/Pastafarian/PrisonerRebuke.et");
		if (!lightning)
		{
			Print("[INF_Prisoners] Cannot deliver prisoner execution penalty: lightning resource failed to load.", LogLevel.ERROR);
			return;
		}
		EntitySpawnParams parameters = new EntitySpawnParams();
		parameters.TransformMode = ETransformMode.WORLD;
		parameters.Transform[3] = offender.GetOrigin();
		if (!GetGame().SpawnEntityPrefab(lightning, GetGame().GetWorld(), parameters))
		{
			Print("[INF_Prisoners] Cannot deliver prisoner execution penalty: lightning failed to spawn.", LogLevel.ERROR);
			return;
		}
		// Native lightning visuals and thunder, but lethal damage only to the offender.
		damage.Kill(Instigator.CreateInstigator(null));
		BroadcastPrisonerWarning(string.Format("The Flying Spaghetti Monster is displeased with %1 for harming a prisoner. R'amen.", offenderName));
	}

	protected void BroadcastPrisonerWarning(string message)
	{
		Print("[INF_Prisoners] " + message);
		PlayerManager players = GetGame().GetPlayerManager();
		array<int> recipients = {};
		players.GetPlayers(recipients);
		foreach (int playerID : recipients)
		{
			SCR_PlayerController player = SCR_PlayerController.Cast(players.GetPlayerController(playerID));
			if (player)
				player.INF_SendPrisonerResponse(message);
		}
	}

	protected void Respond(IEntity user, string message)
	{
		if (!user)
			return;
		int playerID = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(user);
		SCR_PlayerController player = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerID));
		if (player)
			player.INF_SendPrisonerResponse(message);
		else
			Print("[INF_Prisoners] Cannot notify interacting player: player controller missing.", LogLevel.WARNING);
	}

	protected void ApplyRestraints()
	{
		ChimeraCharacter target = ChimeraCharacter.Cast(GetOwner());
		if (!m_bCaptured || !target)
			return;
		CharacterControllerComponent controller = target.GetCharacterController();
		if (controller)
		{
			controller.SetWeaponRaised(false);
			controller.SetDisableWeaponControls(true);
			controller.SetDisableMovementControls(true);
		}
		if (!Replication.IsServer())
			return;
		AIControlComponent control = target.GetAIControlComponent();
		if (control)
			control.DeactivateAI();
		FactionAffiliationComponent affiliation = FactionAffiliationComponent.Cast(target.FindComponent(FactionAffiliationComponent));
		if (affiliation)
		{
			// Missions without a civilian faction use an explicitly unaffiliated noncombatant.
			Faction civilian = GetGame().GetFactionManager().GetFactionByKey("CIV");
			if (affiliation.GetAffiliatedFaction() != civilian)
				affiliation.SetAffiliatedFaction(civilian);
		}
	}

	protected void MaintainRestraints()
	{
		ChimeraCharacter target = ChimeraCharacter.Cast(GetOwner());
		CharacterControllerComponent controller;
		if (target)
			controller = target.GetCharacterController();
		if (!controller || controller.GetLifeState() == ECharacterLifeState.DEAD)
		{
			m_bDeathSeen = true;
			StopPrisonerLoiter(controller);
			return;
		}
		DropCarriedWeapons();
		ApplyRestraints();
		UpdatePrisonerLoiter(controller);
		float now = GetGame().GetWorld().GetWorldTime();
		if (now >= m_fNextCleanupCheck)
		{
			m_fNextCleanupCheck = now + 10000;
			if (INF_CanCleanup())
			{
				SCR_EntityHelper.DeleteEntityAndChildren(GetOwner());
				return;
			}
		}
		// Restraints are re-applied each tick; poll quickly only until the loiter pose holds.
		SCR_CharacterControllerComponent character = SCR_CharacterControllerComponent.Cast(controller);
		int interval = 1000;
		if (m_bWeaponsDropped && character && character.IsLoitering())
			interval = 10000;
		GetGame().GetCallqueue().CallLater(MaintainRestraints, interval, false);
	}

	protected void WarnHandling(string message)
	{
		float now = GetGame().GetWorld().GetWorldTime();
		if (now < m_fNextHandlingWarning)
			return;
		m_fNextHandlingWarning = now + 30000;
		Print("[INF_Prisoners] " + message, LogLevel.WARNING);
	}

	protected void DropCarriedWeapons()
	{
		if (!Replication.IsServer() || !m_bCaptured || m_bWeaponsDropped)
			return;
		SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(GetOwner().FindComponent(SCR_InventoryStorageManagerComponent));
		if (!inventory)
		{
			WarnHandling("Prisoner inventory manager missing; cannot drop carried weapons. Weapon controls remain disabled.");
			return;
		}
		array<IEntity> items = {};
		inventory.GetItems(items);
		bool foundWeapon;
		if (!m_bPileSet)
		{
			// Single pile ~2 m beside the body so items stay out of the medical UI's reach.
			m_vPile = GetOwner().GetOrigin() + GetOwner().GetTransformAxis(0) * 2;
			m_vPile[1] = GetGame().GetWorld().GetSurfaceY(m_vPile[0], m_vPile[2]) + 0.1;
			m_bPileSet = true;
		}
		CollectFinishedDrops();
		foreach (IEntity item : items)
		{
			if (!item)
				continue;
			BaseWeaponComponent weapon = BaseWeaponComponent.Cast(item.FindComponent(BaseWeaponComponent));
			if (!weapon)
				continue;
			EWeaponType type = weapon.GetWeaponType();
			if (type != EWeaponType.WT_RIFLE && type != EWeaponType.WT_SNIPERRIFLE
				&& type != EWeaponType.WT_MACHINEGUN && type != EWeaponType.WT_HANDGUN
				&& type != EWeaponType.WT_ROCKETLAUNCHER && type != EWeaponType.WT_GRENADELAUNCHER
				&& type != EWeaponType.WT_AUTOCANNON
				&& type != EWeaponType.WT_FRAGGRENADE && type != EWeaponType.WT_SMOKEGRENADE)
				continue;
			foundWeapon = true;
			if (IsDropPending(item))
				continue;
			// The deletion completes later on a dedicated server; the copy is only spawned once the callback reports success.
			INF_PrisonerDropCallback callback = new INF_PrisonerDropCallback();
			callback.m_Item = item;
			callback.m_Prefab = item.GetPrefabData().GetPrefabName();
			vector mins;
			vector maxs;
			item.GetBounds(mins, maxs);
			callback.m_fLength = Math.Max(maxs[0] - mins[0], Math.Max(maxs[1] - mins[1], maxs[2] - mins[2]));
			if (!inventory.TryDeleteItem(item, callback))
			{
				WarnHandling("Prisoner weapon drop is not ready; retrying while the prisoner remains restrained.");
				continue;
			}
			m_aPendingDrops.Insert(callback);
		}
		// Native inventory operations may finish later; verify empty (and every copy spawned) on the next tick.
		if (!foundWeapon && m_aPendingDrops.IsEmpty())
			m_bWeaponsDropped = true;
	}

	protected bool IsDropPending(IEntity item)
	{
		foreach (INF_PrisonerDropCallback pending : m_aPendingDrops)
		{
			if (pending.m_Item == item)
				return true;
		}
		return false;
	}

	// Handled here rather than inside the callback, which must not be released while it is still running.
	protected void CollectFinishedDrops()
	{
		for (int index = m_aPendingDrops.Count() - 1; index >= 0; index--)
		{
			INF_PrisonerDropCallback pending = m_aPendingDrops[index];
			if (!pending.m_bDone && !pending.m_bFailed)
				continue;
			m_aPendingDrops.Remove(index);
			if (pending.m_bDone)
				SpawnAtPile(pending.m_Prefab, pending.m_fLength);
		}
	}

	// A clean copy is spawned at the pile instead of moving the dropped item: a spawn replicates its position to every client,
	// whereas moving an already-replicated item did not reach clients on a dedicated server. Attachments and loaded ammo are not kept.
	// Small items land 5-10 cm from the pile centre and rifle-sized ones 15-30 cm, so nothing sits exactly on top of another.
	protected void SpawnAtPile(ResourceName prefab, float length)
	{
		Resource resource = Resource.Load(prefab);
		if (!resource || !resource.IsValid())
		{
			WarnHandling("Dropped prisoner weapon prefab cannot load: " + prefab);
			return;
		}
		float maxRadius = PILE_SCATTER_SMALL;
		if (length >= PILE_LARGE_ITEM)
			maxRadius = PILE_SCATTER_LARGE;
		float radius = Math.RandomFloat(maxRadius * 0.5, maxRadius);
		float angle = Math.RandomFloat(0, Math.PI2);
		vector position = m_vPile;
		position[0] = position[0] + Math.Cos(angle) * radius;
		position[2] = position[2] + Math.Sin(angle) * radius;
		position[1] = GetGame().GetWorld().GetSurfaceY(position[0], position[2]) + PILE_SPAWN_HEIGHT;
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		Math3D.AnglesToMatrix(Vector(Math.RandomFloat(0, 360), 0, 0), params.Transform);
		params.Transform[3] = position;
		IEntity copy = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params);
		if (!copy)
		{
			WarnHandling("Dropped prisoner weapon copy failed to spawn: " + prefab);
			return;
		}
		// Lays the item flat on whatever surface is below it (roads, concrete, objects) instead of leaving it upright or hovering.
		InventoryItemComponent inventoryItem = InventoryItemComponent.Cast(copy.FindComponent(InventoryItemComponent));
		if (inventoryItem)
			inventoryItem.PlaceOnGround();
		Print(string.Format("[INF_Prisoners] Spawned dropped weapon at the pile: %1 at %2, body at %3.", prefab, copy.GetOrigin(), GetOwner().GetOrigin()));
	}

	protected void StopPrisonerLoiter(CharacterControllerComponent controller)
	{
		SCR_CharacterControllerComponent character = SCR_CharacterControllerComponent.Cast(controller);
		if (character && character.IsLoitering())
			character.StopLoitering(true);
		m_fNextLoiterAttempt = 0;
		m_fNextACESurrenderAttempt = 0;
	}

	protected bool GetACESurrenderState(SCR_CharacterControllerComponent character, out bool surrendered)
	{
		surrendered = false;
		// Detect the optional module without introducing an ACE script dependency.
		typename surrenderHelper = "ACE_Captives_SurrenderHelperCompartment".ToType();
		if (!surrenderHelper)
			return false;
		if (!GetGame().GetScriptModule().Call(character, "ACE_Captives_HasSurrendered", false, surrendered))
		{
			WarnHandling("ACE Captives is loaded but its surrender state could not be read; using native loiter.");
			return false;
		}
		return true;
	}

	protected void UpdatePrisonerLoiter(CharacterControllerComponent controller)
	{
		if (!Replication.IsServer() || !m_bCaptured)
			return;
		if (controller.GetLifeState() != ECharacterLifeState.ALIVE || controller.IsUnconscious())
		{
			StopPrisonerLoiter(controller);
			return;
		}
		if (!m_bWeaponsDropped)
			return;
		SCR_CharacterControllerComponent character = SCR_CharacterControllerComponent.Cast(controller);
		if (!character)
		{
			WarnHandling("Prisoner lacks the native character controller; loiter pose unavailable. Restraints remain active.");
			return;
		}
		bool surrendered;
		bool aceSurrenderAvailable = GetACESurrenderState(character, surrendered);
		// ACE surrender occupies an animation helper compartment, not a vehicle.
		if (surrendered)
			return;
		CompartmentAccessComponent access = CompartmentAccessComponent.Cast(GetOwner().FindComponent(CompartmentAccessComponent));
		if ((access && access.IsInCompartment()) || character.IsFalling() || character.IsSwimming() || character.IsClimbing())
			return;
		if (character.GetStance() != ECharacterStance.STAND)
		{
			if (!character.IsChangingStance() && character.CanChangeStance(ECharacterStanceChange.STANCECHANGE_TOERECTED))
				character.SetStanceChange(ECharacterStanceChange.STANCECHANGE_TOERECTED);
			return;
		}
		if (character.IsLoitering())
			return;
		if (m_PrisonerLoiter == ELoiteringType.NONE)
		{
			m_PrisonerLoiter = ELoiteringType.LOITERING;
			if (Math.RandomFloat01() < 0.2)
				m_PrisonerLoiter = ELoiteringType.SMOKING;
		}
		if (m_PrisonerLoiter == ELoiteringType.LOITERING && aceSurrenderAvailable)
		{
			float surrenderNow = GetGame().GetWorld().GetWorldTime();
			if (surrenderNow < m_fNextACESurrenderAttempt)
				return;
			if (m_fNextACESurrenderAttempt > 0)
				WarnHandling("ACE surrender has not remained active; retrying without releasing restraints.");
			m_fNextACESurrenderAttempt = surrenderNow + 5000;
			if (GetGame().GetScriptModule().Call(character, "ACE_Captives_SetSurrender", false, null, true))
				return;
			WarnHandling("ACE surrender could not be requested; using native loiter while restraints remain active.");
		}
		if (!character.CanPlayLoiterAnimation(m_PrisonerLoiter))
			return;
		float now = GetGame().GetWorld().GetWorldTime();
		if (now < m_fNextLoiterAttempt)
			return;
		if (m_fNextLoiterAttempt > 0)
			WarnHandling("Prisoner loiter animation has not started; retrying without releasing restraints.");
		m_fNextLoiterAttempt = now + 5000;
		SCR_LoiterCustomAnimData data = SCR_LoiterCustomAnimData.Default;
		if (m_PrisonerLoiter == ELoiteringType.SMOKING)
			data = SCR_LoiterCustomAnimData.CreateInstance(itemPreset: SCR_EAnimationItemID.CIGAR);
		vector transform[4];
		GetOwner().GetWorldTransform(transform);
		character.StartLoitering(null, m_PrisonerLoiter, false, false, false, transform, true, data);
	}

	protected void OnCapturedChanged()
	{
		ApplyRestraints();
	}

	override void OnPostInit(IEntity owner)
	{
		SetEventMask(owner, EntityEvent.INIT);
	}

	override void EOnInit(IEntity owner)
	{
		if (m_bCaptured)
			ApplyRestraints();
	}

	override void OnDelete(IEntity owner)
	{
		if (m_DamageManager)
		{
			m_DamageManager.GetOnDamage().Remove(OnCapturedDamage);
			m_DamageManager.GetOnDamage().Remove(OnOfficerDamage);
		}
		if (GetGame())
			GetGame().GetCallqueue().Remove(MaintainRestraints);
	}
}

class INF_CaptureAIAction : ScriptedUserAction
{
	override bool GetActionNameScript(out string outName)
	{
		outName = "Capture Prisoner";
		return true;
	}

	override bool CanBeShownScript(IEntity user)
	{
		INF_PrisonerComponent prisoner = INF_PrisonerComponent.Find(GetOwner());
		return prisoner && prisoner.CanCapture(user);
	}

	override bool CanBePerformedScript(IEntity user)
	{
		return CanBeShownScript(user);
	}

	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		INF_PrisonerComponent prisoner = INF_PrisonerComponent.Find(pOwnerEntity);
		if (prisoner)
			prisoner.Capture(pUserEntity);
		else
			Print("[INF_Prisoners] Capture action is missing its prisoner component.", LogLevel.ERROR);
	}
}

class INF_QuestionAIAction : ScriptedUserAction
{
	override bool GetActionNameScript(out string outName)
	{
		outName = "Question Prisoner";
		return true;
	}

	override bool CanBeShownScript(IEntity user)
	{
		INF_PrisonerComponent prisoner = INF_PrisonerComponent.Find(GetOwner());
		return prisoner && prisoner.CanQuestion(user, true);
	}

	override bool CanBePerformedScript(IEntity user)
	{
		SetCannotPerformReason("");
		INF_PrisonerComponent prisoner = INF_PrisonerComponent.Find(GetOwner());
		if (prisoner && prisoner.CanQuestion(user))
			return true;
		if (prisoner && prisoner.CanQuestion(user, true))
			SetCannotPerformReason("Prisoner is bleeding. Bandage them before questioning.");
		return false;
	}

	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		INF_PrisonerComponent prisoner = INF_PrisonerComponent.Find(pOwnerEntity);
		if (prisoner)
			prisoner.Question(pUserEntity);
		else
			Print("[INF_Prisoners] Questioning action is missing its prisoner component.", LogLevel.ERROR);
	}
}

modded class INS_BodySearchAction
{
	protected bool INF_IntelSearchReady()
	{
		if (!INF_ZoneEditorContext.Grid(GetGame().GetGameMode()))
			return true;
		INS_IntelManager intel = INS_IntelManager.GetInstance();
		return intel && intel.INF_IsIntelReady() && intel.INF_IsBodySearchEnabled();
	}

	override bool CanBeShownScript(IEntity user)
	{
		if (!INF_IntelSearchReady() || !super.CanBePerformedScript(user))
			return false;
		INF_PrisonerComponent prisoner = INF_PrisonerComponent.Find(GetOwner());
		if (prisoner && prisoner.IsOfficer() && prisoner.IsOfficerSearched())
			return false;
		if (prisoner && prisoner.IsCaptured())
			return prisoner.CanSearch(user);
		return super.CanBeShownScript(user);
	}

	override bool CanBePerformedScript(IEntity user)
	{
		return CanBeShownScript(user);
	}

	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (!Replication.IsServer())
			return;
		if (!INF_IntelSearchReady())
		{
			Print("[INF_Prisoners] Body search deferred until intel recovery is ready.", LogLevel.WARNING);
			return;
		}
		if (!CanBePerformedScript(pUserEntity))
		{
			Print("[INF_Prisoners] Intel search is no longer available or the prisoner is out of reach.", LogLevel.WARNING);
			return;
		}
		// An officer's papers have their own odds, and replace the ordinary search result.
		INF_PrisonerComponent officer = INF_PrisonerComponent.Find(pOwnerEntity);
		if (officer && officer.IsOfficer())
		{
			officer.SearchOfficer(pUserEntity);
			return;
		}
		super.PerformAction(pOwnerEntity, pUserEntity);
	}
}
