class INF_PrisonerDialogue
{
	static string Reply(bool helpful)
	{
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

class INF_PrisonerComponent : ScriptComponent
{
	[RplProp(onRplName: "OnCapturedChanged")]
	protected bool m_bCaptured;

	[RplProp()]
	protected bool m_bQuestioned;
	protected bool m_bWeaponsDropped;
	protected ELoiteringType m_PrisonerLoiter = ELoiteringType.NONE;
	protected float m_fNextHandlingWarning;
	protected float m_fNextLoiterAttempt;
	protected float m_fNextACESurrenderAttempt;
	protected SCR_CharacterDamageManagerComponent m_DamageManager;
	protected bool m_bDeathHandled;
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
		return m_bQuestioned;
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
		if (!m_bCaptured || m_bQuestioned || !IsUsableBy(user))
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
		GetGame().GetCallqueue().CallLater(MaintainRestraints, 1000, true);
		Respond(user, "Prisoner secured. Stop their bleeding and treat their wounds; they must be conscious before questioning.");
		Print(string.Format("[INF_Prisoners] Player %1 secured hostile AI.", GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(user)));
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
		m_bQuestioned = true;
		Replication.BumpMe();
		bool helpful = intel.INF_HasQuestioningIntel() && Math.RandomFloat01() < intel.INF_GetQuestioningChance();
		if (helpful)
			intel.AddIntelAndBroadcast(1, playerID);
		player.INF_SendPrisonerResponse(INF_PrisonerDialogue.Reply(helpful));
	}

	protected void OnCapturedDamage(notnull BaseDamageContext context)
	{
		if (!Replication.IsServer() || !m_bCaptured || m_bDeathHandled || !m_DamageManager.IsDestroyed())
			return;
		m_bDeathHandled = true;
		if (context.damageValue <= 0 || !context.instigator)
			return;
		if (context.damageType != EDamageType.KINETIC && context.damageType != EDamageType.EXPLOSIVE
			&& context.damageType != EDamageType.FRAGMENTATION && context.damageType != EDamageType.PROCESSED_FRAGMENTATION)
			return;
		int killerID = context.instigator.GetInstigatorPlayerID();
		if (killerID <= 0)
			return;
		PlayerManager players = GetGame().GetPlayerManager();
		IEntity offender = players.GetPlayerControlledEntity(killerID);
		if (!offender)
			return;
		SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(offender.FindComponent(SCR_CharacterDamageManagerComponent));
		if (!damage || damage.IsDestroyed() || Math.RandomFloat01() >= 0.30)
			return;

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
		string message = string.Format("The Flying Spaghetti Monster is displeased with %1 for killing a prisoner. R'amen.", players.GetPlayerName(killerID));
		Print("[INF_Prisoners] " + message);
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
			StopPrisonerLoiter(controller);
			GetGame().GetCallqueue().Remove(MaintainRestraints);
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
				SCR_EntityHelper.DeleteEntityAndChildren(GetOwner());
		}
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
				&& type != EWeaponType.WT_AUTOCANNON)
				continue;
			foundWeapon = true;
			if (!inventory.TryRemoveItemFromInventory(item))
				WarnHandling("Prisoner weapon drop is not ready; retrying while the prisoner remains restrained.");
		}
		// Native inventory operations may finish later; verify empty on the next tick.
		if (!foundWeapon)
			m_bWeaponsDropped = true;
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
			data = SCR_LoiterCustomAnimData.CreateInstance(itemPreset: SCR_ELoiterItemID.CIGAR);
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
			m_DamageManager.GetOnDamage().Remove(OnCapturedDamage);
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
		return intel && intel.INF_IsIntelReady();
	}

	override bool CanBeShownScript(IEntity user)
	{
		if (!INF_IntelSearchReady() || !super.CanBePerformedScript(user))
			return false;
		INF_PrisonerComponent prisoner = INF_PrisonerComponent.Find(GetOwner());
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
		super.PerformAction(pOwnerEntity, pUserEntity);
	}
}
