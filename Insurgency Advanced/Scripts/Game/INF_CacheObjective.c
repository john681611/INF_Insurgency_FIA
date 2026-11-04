class INF_CacheIntelRecord
{
	bool m_Destroyed;
	int m_Intel;
	ref array<int> m_X = {};
	ref array<int> m_Z = {};
	ref array<int> m_Distance = {};
}

enum INF_EIntelSource
{
	SEARCH_AND_QUESTIONING,
	QUESTIONING_ONLY
}

modded class INS_IntelManager
{
	[Attribute("-1", UIWidgets.EditBox, "Questioning intel probability (0 to 1). -1 initialises to twice the body-search chance, capped at 1.", category: "Prisoners"), RplProp()]
	protected float m_fINFQuestioningChance = -1;

	float INF_GetQuestioningChance()
	{
		return Math.Clamp(m_fINFQuestioningChance, 0, 1);
	}

	void INF_SetQuestioningChance(float chance)
	{
		if (!Replication.IsServer())
			return;
		if (!(chance >= 0 && chance <= 1))
		{
			Print("[INF_Prisoners] Invalid questioning chance; expected 0 to 1.", LogLevel.ERROR);
			return;
		}
		m_fINFQuestioningChance = chance;
		Replication.BumpMe();
	}

	[Attribute("0", UIWidgets.ComboBox, "Intel sources: 0 = body search and questioning, 1 = questioning only.", enums: ParamEnumArray.FromEnum(INF_EIntelSource), category: "Prisoners"), RplProp()]
	protected int m_iINFIntelSource;

	int INF_GetIntelSource()
	{
		return m_iINFIntelSource;
	}

	bool INF_IsBodySearchEnabled()
	{
		return m_iINFIntelSource != INF_EIntelSource.QUESTIONING_ONLY;
	}

	void INF_SetIntelSource(int source)
	{
		if (!Replication.IsServer())
			return;
		if (source < INF_EIntelSource.SEARCH_AND_QUESTIONING || source > INF_EIntelSource.QUESTIONING_ONLY)
		{
			Print("[INF_Prisoners] Invalid intel source; expected 0 or 1.", LogLevel.ERROR);
			return;
		}
		m_iINFIntelSource = source;
		Replication.BumpMe();
	}

	void INF_ApplyHeaderSettings(INS_MissionParams params)
	{
		INF_HeaderSettings.Float(params.m_fINFQuestioningChance, 0, 1, "m_fINFQuestioningChance", m_fINFQuestioningChance);
		INF_HeaderSettings.Int(params.m_iINFIntelSource, INF_EIntelSource.SEARCH_AND_QUESTIONING, INF_EIntelSource.QUESTIONING_ONLY, "m_iINFIntelSource", m_iINFIntelSource);
		INF_HeaderSettings.Int(params.m_iINFMissionEndMode, INF_EMissionEndMode.CACHES, INF_EMissionEndMode.CACHES_CAMPS_ZONES, "m_iINFMissionEndMode", m_iINFMissionEndMode);
		int cacheObjective;
		if (INF_HeaderSettings.Int(params.m_iINFCacheObjective, 0, 1, "m_iINFCacheObjective", cacheObjective))
			m_bINFCacheObjective = cacheObjective == 1;
	}

	bool INF_HasQuestioningIntel()
	{
		for (int i = 0; i < m_aCacheEntities.Count(); i++)
		{
			if (!m_aCacheDestroyed[i] && !m_aCacheRevealed[i])
				return true;
		}
		return false;
	}

	[Attribute("0", UIWidgets.CheckBox, "Show a shared objective to destroy hostile caches using intel map hints.")]
	protected bool m_bINFCacheObjective;

	protected SCR_Task m_INFCacheTask;
	protected IEntity m_INFFireSoundEntity;
	protected AudioHandle m_INFFireSoundHandle = AudioHandle.Invalid;
	[RplProp()]
	protected bool m_bINFIntelLoaded;
	protected bool m_bINFIntelLoadFailed;
	protected bool m_bINFMarkerRecoveryWaiting;
	protected int m_iINFSelectedCacheCount;
	protected ref array<vector> m_aINFPendingCacheDestructions = {};
	protected ref array<vector> m_aINFZoneCachePositions = {};

	[Attribute("0", UIWidgets.ComboBox, "Objectives required for victory.", enums: ParamEnumArray.FromEnum(INF_EMissionEndMode), category: "Objectives"), RplProp()]
	protected int m_iINFMissionEndMode = INF_EMissionEndMode.CACHES;
	protected SCR_Task m_INFCampTask;
	protected SCR_Task m_INFZoneTask;
	protected int m_iINFCampsCaptured;
	protected int m_iINFCampsTotal;
	protected int m_iINFZonesCaptured;
	protected int m_iINFZonesTotal;
	protected bool m_bINFCaptureCountsReady;
	protected bool m_bINFMissionEndQueued;
	protected bool m_bINFMissionEnded;
	protected bool m_bINFDebugCampMarkers;
	protected bool m_bINFDebugCachesEnabled;
	protected ref array<SCR_MapMarkerBase> m_aINFCampDebugMarkers = {};
	protected bool m_bINFDebugClearWarning;
	protected ref array<SCR_MapMarkerBase> m_aINFBaseMarkers = {};
	protected ref array<vector> m_aINFBasePositions = {};
	protected ref array<string> m_aINFBaseNames = {};
	protected ref array<int> m_aINFBaseKinds = {};

	// 0 = player-built base, 1 = captured camp, 2 = command post (a campaign military base).
	protected int INF_GetBaseKind(IEntity entity)
	{
		int kind;
		while (entity)
		{
			if (entity.FindComponent(SCR_CampaignMilitaryBaseComponent))
				return 2;
			if (entity.FindComponent(INF_CampOwnershipComponent))
				kind = 1;
			entity = entity.GetParent();
		}
		return kind;
	}
	protected bool m_bINFObjectiveUpdateQueued;
	protected int m_iINFDebugRevision;
	protected bool m_bINFDebugRefreshQueued;
	protected ref array<SCR_PlayerController> m_aINFDebugSubscribers = {};
	protected ref array<vector> m_aINFLocalDebugCaches = {};
	protected ref array<vector> m_aINFLocalDebugCamps = {};
	protected int m_iINFLocalDebugLayers;
	protected bool m_bINFDebugApplyPending;
	protected float m_fINFNextDebugApplyAt;
	protected int m_iINFDebugPositionRevision = -1;
	protected ref array<vector> m_aINFDebugCachePositions = {};
	protected ref array<vector> m_aINFDebugCampPositions = {};

	void INF_QueueCampaignObjectives()
	{
		if (!Replication.IsServer() || m_bINFMissionEnded || m_bINFObjectiveUpdateQueued)
			return;
		m_bINFObjectiveUpdateQueued = true;
		GetGame().GetCallqueue().CallLater(INF_UpdateCampaignObjectives, 0, false);
	}

	void INF_DebugContentChanged()
	{
		if (!Replication.IsServer())
			return;
		m_iINFDebugRevision++;
		if (!m_aINFDebugSubscribers.IsEmpty())
			INF_RefreshDebugMarkers();
	}

	protected void INF_RetryMissingTasks()
	{
		if (m_bINFCacheObjective && (!m_INFCacheTask || !m_INFCampTask || !m_INFZoneTask)
			&& !m_bINFObjectiveUpdateQueued && !m_bINFMissionEnded)
		{
			m_bINFObjectiveUpdateQueued = true;
			GetGame().GetCallqueue().CallLater(INF_UpdateCampaignObjectives, 5000, false);
		}
	}

	bool INF_RegisterZoneCaches(INF_ZoneStateSystem zones)
	{
		if (m_aINFZoneCachePositions.Count() != m_aCacheEntities.Count()
			|| m_aCacheDestroyed.Count() != m_aCacheEntities.Count())
		{
			Print("[INF_Zones] Cache snapshot arrays disagree; static-site startup cannot finish.", LogLevel.ERROR);
			return false;
		}
		for (int i = 0; i < m_aCacheEntities.Count(); i++)
			zones.AddCache(m_aCacheEntities[i], m_aINFZoneCachePositions[i], m_aCacheDestroyed[i]);
		return true;
	}

	override void OnGameModeStart()
	{
		super.OnGameModeStart();
		if (!INF_ZoneEditorContext.Grid(GetOwner()))
			return;
		if (Replication.IsServer())
		{
			if (m_fINFQuestioningChance == -1)
				INF_SetQuestioningChance(Math.Clamp(GetIntelDropChance() * 2, 0, 1));
			else
				INF_SetQuestioningChance(m_fINFQuestioningChance);
			INF_QueueCampaignObjectives();
		}
		GetGame().GetCallqueue().CallLater(INF_RefreshBaseMarkers, 3000, false);
	}

	int INF_GetMissionEndMode()
	{
		return m_iINFMissionEndMode;
	}

	bool INF_IsIntelReady()
	{
		return m_bINFIntelLoaded && !m_bINFMissionEnded;
	}

	void INF_SetMissionEndMode(int value)
	{
		if (!Replication.IsServer())
			return;
		if (value < INF_EMissionEndMode.CACHES || value > INF_EMissionEndMode.CACHES_CAMPS_ZONES)
		{
			Print("[INF_Objectives] Invalid mission-end mode; keeping the current setting.", LogLevel.ERROR);
			return;
		}
		m_iINFMissionEndMode = value;
		Replication.BumpMe();
		GetGame().GetCallqueue().Remove(TriggerMissionComplete);
		m_bINFMissionEndQueued = false;
		INF_QueueCampaignObjectives();
	}

	protected void INF_UpdateCampaignObjectives()
	{
		GetGame().GetCallqueue().Remove(INF_UpdateCampaignObjectives);
		m_bINFObjectiveUpdateQueued = false;
		if (!Replication.IsServer() || m_bINFMissionEnded)
			return;
		INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(GetOwner().FindComponent(INS_AmbientPatrolManager));
		bool discoveryReady = !patrols || patrols.INF_AreCampsReady();
		if (m_bINFCacheObjective)
		{
			INF_UpdateCacheObjective();
			if (!discoveryReady)
			{
				m_bINFCaptureCountsReady = false;
				m_INFCampTask = INF_UpdateCaptureTask(m_INFCampTask, "INF_CaptureHostileCamps",
					"Camps - initialising", patrols.INF_GetStartupStatus());
				m_INFZoneTask = INF_UpdateCaptureTask(m_INFZoneTask, "INF_CaptureHostileZones",
					"Zones - initialising", "Discovered building zones are playable while terrain discovery continues. Final capture-zone counts appear after camp/cache placement finishes.");
			}
		}
		if (!discoveryReady)
		{
			INF_RetryMissingTasks();
			return;
		}

		map<string, bool> zones = new map<string, bool>();
		map<string, bool> camps = new map<string, bool>();
		if (patrols)
			patrols.INF_CollectCaptureObjectives(zones, camps);
		m_iINFCampsTotal = camps.Count();
		m_iINFCampsCaptured = INF_CaptureObjectiveCells.Captured(camps);
		m_iINFZonesTotal = zones.Count();
		m_iINFZonesCaptured = INF_CaptureObjectiveCells.Captured(zones);
		m_bINFCaptureCountsReady = true;

		if (m_bINFCacheObjective)
		{
			m_INFCampTask = INF_UpdateCaptureTask(m_INFCampTask, "INF_CaptureHostileCamps",
				string.Format("Captured %1/%2 Camps", m_iINFCampsCaptured, m_iINFCampsTotal),
				"Capture woodland camp squares on foot. Yellow is capturing; orange means a counterattack is pending or active. Secure the square permanently (green) to transfer camp ownership and complete its objective.");
			m_INFZoneTask = INF_UpdateCaptureTask(m_INFZoneTask, "INF_CaptureHostileZones",
				string.Format("Captured %1/%2 Zones", m_iINFZonesCaptured, m_iINFZonesTotal),
				"Capture every non-camp 100m building/cache square. Each square counts once. Capture credit is retained for this mission even if a counterattack retakes the zone.");
		}
		if (INF_CanCompleteMission() && !m_bINFMissionEndQueued)
		{
			m_bINFMissionEndQueued = true;
			GetGame().GetCallqueue().CallLater(TriggerMissionComplete, 5000, false);
		}
		INF_RetryMissingTasks();
	}

	protected SCR_Task INF_UpdateCaptureTask(SCR_Task task, string id, string title, string description)
	{
		SCR_TaskSystem taskSystem = SCR_TaskSystem.GetInstance();
		if (!taskSystem)
		{
			Print("[INF_Objectives] Cannot update objective " + id + ": task system unavailable.", LogLevel.ERROR);
			return task;
		}
		bool assignPlayers = !task;
		if (!task)
			task = SCR_TaskSystem.GetTaskFromTaskID(id, false);
		if (!task)
			task = taskSystem.CreateTask("{1D0F815858EE24AD}Prefabs/Tasks/BaseTask.et", id, title, description);
		if (!task)
		{
			Print("[INF_Objectives] Cannot create objective: " + id, LogLevel.ERROR);
			return null;
		}
		if (task.GetTaskName() != title)
			task.SetTaskName(title);
		if (task.GetTaskDescription() != description)
			task.SetTaskDescription(description);
		if (assignPlayers)
		{
			taskSystem.SetTaskOwnership(task, SCR_ETaskOwnership.EVERYONE);
			taskSystem.SetTaskVisibility(task, SCR_ETaskVisibility.EVERYONE);
			taskSystem.SetTaskUIVisibility(task, SCR_ETaskUIVisibility.LIST_ONLY);
			array<int> players = {};
			GetGame().GetPlayerManager().GetPlayers(players);
			foreach (int playerId : players)
				INF_AssignObjective(taskSystem, task, playerId);
		}
		return task;
	}

	protected bool INF_CanCompleteMission()
	{
		if (!Replication.IsServer() || !m_bINFIntelLoaded || !INF_CacheRosterComplete()
			|| m_iCachesDestroyed != m_aCacheEntities.Count())
			return false;
		foreach (bool destroyed : m_aCacheDestroyed)
		{
			if (!destroyed)
				return false;
		}
		INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(GetOwner().FindComponent(INS_AmbientPatrolManager));
		if (patrols && !patrols.INF_AreCampsReady())
			return false;
		if (m_iINFMissionEndMode == INF_EMissionEndMode.CACHES)
			return true;
		if (!m_bINFCaptureCountsReady || m_iINFCampsCaptured < m_iINFCampsTotal)
			return false;
		return m_iINFMissionEndMode != INF_EMissionEndMode.CACHES_CAMPS_ZONES
			|| m_iINFZonesCaptured >= m_iINFZonesTotal;
	}

	override protected void TriggerMissionComplete()
	{
		if (!Replication.IsServer())
			return;
		if (!INF_ZoneEditorContext.Grid(GetOwner()))
		{
			super.TriggerMissionComplete();
			return;
		}
		if (m_bINFMissionEnded)
			return;
		INF_UpdateCampaignObjectives();
		m_bINFMissionEndQueued = false;
		if (!INF_CanCompleteMission())
			return;
		m_bINFMissionEnded = true;
		GetGame().GetCallqueue().Remove(TriggerMissionComplete);
		Print(string.Format("[INF_Objectives] Victory authorised: caches=%1/%2, selected=%3, camps=%4/%5, zones=%6/%7, mode=%8.",
			m_iCachesDestroyed, m_aCacheEntities.Count(), INF_ForestCampRewards.CacheCount(),
			m_iINFCampsCaptured, m_iINFCampsTotal, m_iINFZonesCaptured, m_iINFZonesTotal, m_iINFMissionEndMode));
		super.TriggerMissionComplete();
	}

	override bool GetDebugCacheMarkers()
	{
		if (!INF_ZoneEditorContext.Grid(GetOwner()))
			return super.GetDebugCacheMarkers();
		return INF_GetDebugMarkersForPlayer(SCR_PlayerController.GetLocalPlayerId(), false);
	}

	override void SetDebugCacheMarkers(bool value)
	{
		if (!INF_ZoneEditorContext.Grid(GetOwner()))
		{
			super.SetDebugCacheMarkers(value);
			return;
		}
		INF_SetDebugMarkersForPlayer(SCR_PlayerController.GetLocalPlayerId(), false, value);
	}

	protected bool INF_CanSeeDebugMarkers()
	{
		return INF_DebugMarkerAccess.IsGM(SCR_PlayerController.GetLocalPlayerId());
	}

	protected void INF_OnDebugVisibilityChanged()
	{
		GetGame().GetCallqueue().Remove(INF_OnDebugVisibilityChanged);
		if (System.IsConsoleApp())
			return;
		bool allowed = INF_CanSeeDebugMarkers();
		if (!m_bINFDebugCachesEnabled || !allowed)
			ClearDebugMarkers();
		if (!m_bINFDebugCampMarkers || !allowed)
			INF_ClearCampDebugMarkers();
		if (!allowed)
		{
			m_bINFDebugCachesEnabled = false;
			m_bINFDebugCampMarkers = false;
			m_bINFDebugApplyPending = m_iINFLocalDebugLayers != 0;
		}
		if (m_bINFDebugApplyPending && allowed && GetGame().GetWorld().GetWorldTime() >= m_fINFNextDebugApplyAt)
			INF_ApplyDebugMarkerSnapshot(m_iINFLocalDebugLayers, m_aINFLocalDebugCaches, m_aINFLocalDebugCamps);
		GetGame().GetCallqueue().Remove(INF_OnDebugVisibilityChanged);
		if (m_bINFDebugCachesEnabled || m_bINFDebugCampMarkers || !m_aDebugMarkers.IsEmpty()
			|| !m_aINFCampDebugMarkers.IsEmpty() || m_bINFDebugApplyPending)
			GetGame().GetCallqueue().CallLater(INF_OnDebugVisibilityChanged, 2000, false);
	}

	protected void INF_RefreshDebugMarkers()
	{
		GetGame().GetCallqueue().Remove(INF_RefreshDebugMarkers);
		m_bINFDebugRefreshQueued = false;
		if (!Replication.IsServer())
			return;
		for (int i = m_aINFDebugSubscribers.Count() - 1; i >= 0; i--)
		{
			SCR_PlayerController controller = m_aINFDebugSubscribers[i];
			if (!controller || GetGame().GetPlayerManager().GetPlayerController(controller.GetPlayerId()) != controller)
			{
				m_aINFDebugSubscribers.Remove(i);
				continue;
			}
			INF_SendDebugMarkerSnapshot(controller, controller.INF_GetDebugMarkerLayers());
			if (controller.INF_GetDebugMarkerLayers() == 0)
				m_aINFDebugSubscribers.Remove(i);
		}
		if (!m_aINFDebugSubscribers.IsEmpty())
		{
			m_bINFDebugRefreshQueued = true;
			GetGame().GetCallqueue().CallLater(INF_RefreshDebugMarkers, 2000, false);
		}
	}

	bool INF_GetDebugCampMarkers()
	{
		return INF_GetDebugMarkersForPlayer(SCR_PlayerController.GetLocalPlayerId(), true);
	}

	void INF_SetDebugCampMarkers(bool value)
	{
		INF_SetDebugMarkersForPlayer(SCR_PlayerController.GetLocalPlayerId(), true, value);
	}

	bool INF_GetDebugMarkersForPlayer(int playerId, bool camps)
	{
		if (!INF_DebugMarkerAccess.IsGM(playerId))
			return false;
		SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
		if (!controller)
			return false;
		int layer = INF_EDebugMarkerLayers.CACHES;
		if (camps)
			layer = INF_EDebugMarkerLayers.CAMPS;
		return (controller.INF_GetDebugMarkerLayers() & layer) != 0;
	}

	void INF_SetDebugMarkersForPlayer(int playerId, bool camps, bool enabled)
	{
		if (!Replication.IsServer() || !INF_ZoneEditorContext.Grid(GetOwner()))
			return;
		if (!INF_DebugMarkerAccess.IsGM(playerId))
		{
			Print("[INF_DebugMarkers] Rejected debug request without GM edit-mode access.", LogLevel.WARNING);
			return;
		}
		SCR_PlayerController controller = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
		if (!controller)
		{
			Print("[INF_DebugMarkers] Cannot update GM debug preference: player controller unavailable.", LogLevel.ERROR);
			return;
		}
		int layer = INF_EDebugMarkerLayers.CACHES;
		if (camps)
			layer = INF_EDebugMarkerLayers.CAMPS;
		int layers = controller.INF_GetDebugMarkerLayers();
		if (enabled)
			layers = layers | layer;
		else
			layers = layers & ~layer;
		INF_SendDebugMarkerSnapshot(controller, layers);
		if (layers != 0 && !m_aINFDebugSubscribers.Contains(controller))
			m_aINFDebugSubscribers.Insert(controller);
		if (layers == 0)
			m_aINFDebugSubscribers.RemoveItem(controller);
		if (!m_bINFDebugRefreshQueued && !m_aINFDebugSubscribers.IsEmpty())
		{
			m_bINFDebugRefreshQueued = true;
			GetGame().GetCallqueue().CallLater(INF_RefreshDebugMarkers, 2000, false);
		}
	}

	protected void INF_SendDebugMarkerSnapshot(SCR_PlayerController controller, int layers)
	{
		if (!INF_DebugMarkerAccess.IsGM(controller.GetPlayerId()))
			layers = 0;
		if (!controller.INF_DebugSnapshotChanged(layers, m_iINFDebugRevision))
			return;
		if (layers != 0 && m_iINFDebugPositionRevision != m_iINFDebugRevision)
		{
			m_aINFDebugCachePositions.Clear();
			m_aINFDebugCampPositions.Clear();
			if (INF_ForestCampRewards.AreCachesReady())
			{
				foreach (IEntity cache : m_aCacheEntities)
				{
					if (cache)
						m_aINFDebugCachePositions.Insert(cache.GetOrigin());
				}
			}
			INS_AmbientPatrolManager patrols = INF_ZoneEditorContext.Grid(GetOwner());
			if (patrols && patrols.INF_AreCampsReady() && patrols.INF_GetZoneStates())
				patrols.INF_GetZoneStates().CollectCampPositions(m_aINFDebugCampPositions);
			m_iINFDebugPositionRevision = m_iINFDebugRevision;
		}
		array<vector> cachePositions = {};
		array<vector> campPositions = {};
		if ((layers & INF_EDebugMarkerLayers.CACHES) != 0)
			cachePositions = m_aINFDebugCachePositions;
		if ((layers & INF_EDebugMarkerLayers.CAMPS) != 0)
			campPositions = m_aINFDebugCampPositions;
		controller.INF_SendDebugMarkerSnapshot(layers, cachePositions, campPositions);
	}

	void INF_ApplyDebugMarkerSnapshot(int layers, array<vector> cachePositions, array<vector> campPositions)
	{
		if (System.IsConsoleApp() || !INF_ZoneEditorContext.Grid(GetOwner()))
			return;
		m_iINFLocalDebugLayers = layers;
		if (cachePositions != m_aINFLocalDebugCaches)
			m_aINFLocalDebugCaches.Copy(cachePositions);
		if (campPositions != m_aINFLocalDebugCamps)
			m_aINFLocalDebugCamps.Copy(campPositions);
		m_bINFDebugApplyPending = true;
		m_fINFNextDebugApplyAt = GetGame().GetWorld().GetWorldTime() + 5000;
		ClearDebugMarkers();
		INF_ClearCampDebugMarkers();
		bool allowed = INF_CanSeeDebugMarkers();
		m_bINFDebugCachesEnabled = allowed && (layers & INF_EDebugMarkerLayers.CACHES) != 0;
		m_bINFDebugCampMarkers = allowed && (layers & INF_EDebugMarkerLayers.CAMPS) != 0;
		if (layers == 0 && m_aDebugMarkers.IsEmpty() && m_aINFCampDebugMarkers.IsEmpty())
			m_bINFDebugApplyPending = false;
		if ((allowed || layers == 0) && SCR_MapMarkerManagerComponent.GetInstance()
			&& m_aDebugMarkers.IsEmpty() && m_aINFCampDebugMarkers.IsEmpty())
		{
			foreach (int cacheIndex, vector cachePos : m_aINFLocalDebugCaches)
				PlaceDebugMarker((int)cachePos[0], (int)cachePos[2], cacheIndex);
			foreach (int campIndex, vector campPos : m_aINFLocalDebugCamps)
				INF_PlaceCampDebugMarker((int)campPos[0], (int)campPos[2], campIndex);
			m_bINFDebugApplyPending = (m_bINFDebugCachesEnabled && m_aDebugMarkers.Count() != m_aINFLocalDebugCaches.Count())
				|| (m_bINFDebugCampMarkers && m_aINFCampDebugMarkers.Count() != m_aINFLocalDebugCamps.Count());
		}
		GetGame().GetCallqueue().Remove(INF_OnDebugVisibilityChanged);
		if (layers != 0 || m_bINFDebugApplyPending || !m_aDebugMarkers.IsEmpty() || !m_aINFCampDebugMarkers.IsEmpty())
			GetGame().GetCallqueue().CallLater(INF_OnDebugVisibilityChanged, 2000, false);
	}

	protected void INF_ClearCampDebugMarkers()
	{
		INF_ClearLocalDebugMarkers(m_aINFCampDebugMarkers);
	}

	override protected void ClearDebugMarkers()
	{
		if (!INF_ZoneEditorContext.Grid(GetOwner()))
		{
			super.ClearDebugMarkers();
			return;
		}
		INF_ClearLocalDebugMarkers(m_aDebugMarkers);
	}

	protected void INF_ClearLocalDebugMarkers(array<SCR_MapMarkerBase> markers)
	{
		if (markers.IsEmpty())
			return;
		SCR_MapMarkerManagerComponent markerMgr = SCR_MapMarkerManagerComponent.GetInstance();
		if (!markerMgr)
		{
			if (!m_bINFDebugClearWarning)
				Print("[INF_DebugMarkers] Cannot clear debug markers: map marker manager unavailable; retaining handles for retry.", LogLevel.WARNING);
			m_bINFDebugClearWarning = true;
			return;
		}
		m_bINFDebugClearWarning = false;
		foreach (SCR_MapMarkerBase marker : markers)
		{
			if (marker)
				markerMgr.INF_RemoveLocalDebugMarker(marker);
		}
		markers.Clear();
	}

	protected void INF_PlaceCampDebugMarker(int worldX, int worldZ, int campIndex)
	{
		if (!m_bINFDebugCampMarkers)
			return;
		INF_PlaceDebugMarker(worldX, worldZ, string.Format("[DBG CAMP %1]", campIndex + 1), m_aINFCampDebugMarkers, "fortification");
	}

	override protected void BroadcastDebugMarkers()
	{
		if (!INF_ZoneEditorContext.Grid(GetOwner()))
		{
			super.BroadcastDebugMarkers();
			return;
		}
		INF_RefreshDebugMarkers();
	}

	override protected void PlaceDebugMarker(int worldX, int worldZ, int cacheIndex)
	{
		if (!INF_ZoneEditorContext.Grid(GetOwner()))
		{
			super.PlaceDebugMarker(worldX, worldZ, cacheIndex);
			return;
		}
		if (!m_bINFDebugCachesEnabled)
			return;
		INF_PlaceDebugMarker(worldX, worldZ, string.Format("[DBG CACHE %1]", cacheIndex + 1), m_aDebugMarkers, "objective-marker");
	}

	protected void INF_RefreshBaseMarkers()
	{
		GetGame().GetCallqueue().Remove(INF_RefreshBaseMarkers);
		if (m_bINFMissionEnded)
			return;
		GetGame().GetCallqueue().CallLater(INF_RefreshBaseMarkers, 3000, false);
		if (SCR_PlayerController.GetLocalPlayerId() <= 0 || !SCR_MapMarkerManagerComponent.GetInstance())
			return;
		Faction faction = SCR_FactionManager.SGetLocalPlayerFaction();
		array<SCR_SpawnPoint> points = SCR_SpawnPoint.GetSpawnPoints();
		array<vector> positions = {};
		array<string> names = {};
		array<int> kinds = {};
		if (faction && points)
		{
			string factionKey = faction.GetFactionKey();
			foreach (SCR_SpawnPoint point : points)
			{
				// Personal spawn points follow a player and are not a base.
				if (!point || !point.IsSpawnPointEnabled() || point.GetFactionKey() != factionKey || SCR_PlayerSpawnPoint.Cast(point))
					continue;
				vector pos = point.GetOrigin();
				int kind = INF_GetBaseKind(point);
				bool duplicate;
				foreach (int knownIndex, vector known : positions)
				{
					// A command post beside a camp is a separate base, so only merge spawn points of the same kind.
					if (kinds[knownIndex] == kind && vector.DistanceSqXZ(pos, known) < 400)
						duplicate = true;
				}
				if (duplicate)
				{
					duplicate = false;
					continue;
				}
				positions.Insert(pos);
				string name = point.GetSpawnPointName();
				if (name.IsEmpty())
					name = "Base";
				names.Insert(name);
				kinds.Insert(kind);
			}
		}
		bool changed = positions.Count() != m_aINFBasePositions.Count() || m_aINFBaseMarkers.Count() != positions.Count();
		if (!changed)
		{
			foreach (int index, vector current : positions)
			{
				if (vector.DistanceSqXZ(current, m_aINFBasePositions[index]) > 1 || names[index] != m_aINFBaseNames[index] || kinds[index] != m_aINFBaseKinds[index])
					changed = true;
			}
		}
		if (!changed)
			return;
		INF_ClearLocalDebugMarkers(m_aINFBaseMarkers);
		if (!m_aINFBaseMarkers.IsEmpty())
			return;
		m_aINFBasePositions.Copy(positions);
		m_aINFBaseNames.Copy(names);
		m_aINFBaseKinds.Copy(kinds);
		foreach (int placeIndex, vector placePos : positions)
			INF_PlaceBaseMarker((int)placePos[0], (int)placePos[2], names[placeIndex], kinds[placeIndex]);
		if (m_aINFBaseMarkers.Count() != positions.Count())
		{
			// Retry next tick if any marker failed to place.
			INF_ClearLocalDebugMarkers(m_aINFBaseMarkers);
			m_aINFBasePositions.Clear();
			m_aINFBaseNames.Clear();
			m_aINFBaseKinds.Clear();
		}
	}

	protected void INF_PlaceBaseMarker(int worldX, 	int worldZ, string label, int kind)
	{
		SCR_MapMarkerManagerComponent markerMgr = SCR_MapMarkerManagerComponent.GetInstance();
		if (!markerMgr)
			return;
			// Camps carry an arsenal (supply symbol); command posts add the signal symbol.
			EMilitarySymbolIcon icons = EMilitarySymbolIcon.RESPAWN;
			if (kind == 1)
				icons = icons | EMilitarySymbolIcon.SUPPLY;
			else if (kind == 2)
				icons = icons | EMilitarySymbolIcon.SIGNAL;
			SCR_MapMarkerBase marker = markerMgr.PrepareMilitaryMarker(EMilitarySymbolIdentity.BLUFOR, EMilitarySymbolDimension.INSTALLATION, icons);
		if (!marker)
		{
			Print("[INF_BaseMarkers] NATO installation symbol unavailable in the marker config.", LogLevel.ERROR);
			return;
		}
		marker.SetWorldPos(worldX, worldZ);
		marker.SetCustomText(label);
		marker.SetMarkerOwnerID(-1);
		marker.SetCanBeRemovedByOwner(false);
		markerMgr.InsertStaticMarker(marker, true, false);
		// A local insert assigns the local player as owner, which would make the marker editable and removable.
		marker.SetMarkerOwnerID(-1);
		m_aINFBaseMarkers.Insert(marker);
	}

	protected void INF_PlaceDebugMarker(int worldX, int worldZ, string label, array<SCR_MapMarkerBase> markers, string icon)
	{
		if (!INF_CanSeeDebugMarkers())
			return;

		SCR_MapMarkerManagerComponent markerMgr = SCR_MapMarkerManagerComponent.GetInstance();
		if (!markerMgr)
		{
			Print("[INF_DebugMarkers] Cannot display debug markers: map marker manager unavailable.", LogLevel.ERROR);
			return;
		}
		SCR_MapMarkerBase marker = new SCR_MapMarkerBase();
		marker.SetType(ResolveMarkerType());
		SCR_MapMarkerEntryPlaced placed = SCR_MapMarkerEntryPlaced.Cast(
			markerMgr.GetMarkerConfig().GetMarkerEntryConfigByType(ResolveMarkerType()));
		int iconIndex = -1;
		if (placed)
		{
			array<ref SCR_MarkerIconEntry> entries = placed.GetIconEntries();
			foreach (int index, SCR_MarkerIconEntry entry : entries)
			{
				ResourceName imageset, glow;
				string quad;
				entry.GetIconResource(imageset, glow, quad);
				if (quad == icon)
				{
					iconIndex = index;
					break;
				}
			}
		}
		if (iconIndex < 0)
		{
			Print("[INF_DebugMarkers] Cannot resolve debug marker icon: " + icon, LogLevel.ERROR);
			return;
		}
		marker.SetIconEntry(iconIndex);
		marker.SetWorldPos(worldX, worldZ);
		marker.SetCustomText(label);
		marker.SetMarkerOwnerID(-1);
		marker.SetCanBeRemovedByOwner(false);
		markerMgr.InsertStaticMarker(marker, true, false);
		// A local insert assigns the local player as owner, which would make the marker editable and removable.
		marker.SetMarkerOwnerID(-1);
		markers.Insert(marker);
	}

	override protected void LoadIntelState()
	{
		if (!Replication.IsServer())
			return;
		if (!INF_ZoneEditorContext.Grid(GetOwner()))
		{
			super.LoadIntelState();
			return;
		}
		if (m_bINFIntelLoaded || m_bINFIntelLoadFailed)
			return;
		if (!INF_ForestCampRewards.AreCachesReady())
		{
			GetGame().GetCallqueue().CallLater(LoadIntelState, 250, false);
			return;
		}
		m_iINFSelectedCacheCount = INF_ForestCampRewards.CacheCount();
		if (!INF_CacheRosterComplete())
		{
			m_bINFIntelLoadFailed = true;
			Print("[INF_Objectives] Cache roster is incomplete; blocking intel recovery and victory without altering saves.", LogLevel.ERROR);
			return;
		}
		array<vector> positions;
		INF_ForestCampRewards.GetCachePositions(positions);
		array<IEntity> ordered = {};
		foreach (vector pos : positions)
		{
			int index = m_aINFZoneCachePositions.Find(pos);
			if (index < 0)
			{
				m_bINFIntelLoadFailed = true;
				Print("[INF_Objectives] Registered cache positions disagree with the selected roster; preserving saves and blocking victory.", LogLevel.ERROR);
				return;
			}
			ordered.Insert(m_aCacheEntities[index]);
		}
		// Retries can register A,C,B; saved intel is indexed by the selected A,B,C roster.
		m_aCacheEntities = ordered;
		m_aINFZoneCachePositions = positions;
		array<ref INF_CacheIntelRecord> records = {};
		int savedDestroyed;
		if (!INF_ReadCacheIntel(records, savedDestroyed))
		{
			m_bINFIntelLoadFailed = true;
			Print("[INF_Objectives] Intel save is invalid or belongs to a different cache roster; preserving it and blocking victory.", LogLevel.ERROR);
			return;
		}
		bool needsMarkers = false;
		foreach (int recordIndex, INF_CacheIntelRecord savedRecord : records)
		{
			if (!savedRecord.m_Destroyed && !m_aINFPendingCacheDestructions.Contains(positions[recordIndex])
				&& !savedRecord.m_X.IsEmpty())
				needsMarkers = true;
		}
		if (needsMarkers)
		{
			if (!SCR_MapMarkerManagerComponent.GetInstance())
			{
				if (!m_bINFMarkerRecoveryWaiting)
					Print("[INF_Objectives] Waiting for the map marker manager before restoring saved intel hints.", LogLevel.WARNING);
				m_bINFMarkerRecoveryWaiting = true;
				GetGame().GetCallqueue().CallLater(LoadIntelState, 250, false);
				return;
			}
			ResolveMarkerType();
			if (!m_bMarkerTypeCached)
			{
				m_bINFIntelLoadFailed = true;
				Print("[INF_Objectives] Intel marker type unavailable; preserving saved hints and blocking recovery.", LogLevel.ERROR);
				return;
			}
		}
		m_bINFMarkerRecoveryWaiting = false;
		m_iCachesDestroyed = 0;
		foreach (int cacheIndex, INF_CacheIntelRecord record : records)
		{
			bool destroyed = record.m_Destroyed || m_aINFPendingCacheDestructions.Contains(positions[cacheIndex]);
			m_aCacheDestroyed[cacheIndex] = destroyed;
			m_aCacheIntelCounts[cacheIndex] = record.m_Intel;
			m_aCacheRevealed[cacheIndex] = !destroyed && record.m_Intel >= m_iIntelThreshold;
			if (destroyed)
			{
				m_iCachesDestroyed++;
				// Recovery must not leave a fresh, apparently live cache that is already credited as destroyed.
				IEntity spentCache = m_aCacheEntities[cacheIndex];
				if (spentCache)
					SCR_EntityHelper.DeleteEntityAndChildren(spentCache);
				continue;
			}
			foreach (int markerIndex, int x : record.m_X)
				CreateCacheMarker(x, record.m_Z[markerIndex], record.m_Distance[markerIndex], cacheIndex);
		}
		if (savedDestroyed != m_iCachesDestroyed)
			Print(string.Format("[INF_Objectives] Rebuilt destroyed count from cache flags: saved=%1, actual=%2.", savedDestroyed, m_iCachesDestroyed), LogLevel.WARNING);
		m_aINFPendingCacheDestructions.Clear();
		m_bINFIntelLoaded = true;
		INF_QueueCampaignObjectives();
		INF_DebugContentChanged();
		Replication.BumpMe();
		SaveIntelState();
		Print(string.Format("[INF_Objectives] Cache roster ready: selected=%1, registered=%2, destroyed=%3.",
			INF_ForestCampRewards.CacheCount(), m_aCacheEntities.Count(), m_iCachesDestroyed));

		if (m_bINFCacheObjective)
			INF_UpdateCacheObjective();
	}

	protected bool INF_CacheRosterComplete()
	{
		int count = m_aCacheEntities.Count();
		return INF_ForestCampRewards.AreCachesReady() && count > 0
			&& count == INF_ForestCampRewards.CacheCount()
			&& m_aINFZoneCachePositions.Count() == count && m_aCacheDestroyed.Count() == count
			&& m_aCacheIntelCounts.Count() == count && m_aCacheRevealed.Count() == count
			&& m_aCacheMarkers.Count() == count && m_aCacheMarkerDists.Count() == count
			&& m_aCacheMarkerWorldX.Count() == count && m_aCacheMarkerWorldZ.Count() == count;
	}

	protected bool INF_ReadCacheIntel(array<ref INF_CacheIntelRecord> records, out int savedDestroyed)
	{
		savedDestroyed = 0;
		int count = m_aCacheEntities.Count();
		if (!FileIO.FileExists(INS_INTEL_SAVE_FILE))
		{
			for (int fresh = 0; fresh < count; fresh++)
				records.Insert(new INF_CacheIntelRecord());
			return true;
		}
		FileSerializer file = new FileSerializer();
		if (!file.Open(INS_INTEL_SAVE_FILE, FileMode.READ))
			return false;
		int savedCount;
		if (!file.Read(savedDestroyed) || !file.Read(savedCount) || savedCount != count)
		{
			file.Close();
			return false;
		}
		for (int i = 0; i < count; i++)
		{
			INF_CacheIntelRecord record = new INF_CacheIntelRecord();
			int destroyed, markers;
			if (!file.Read(destroyed) || !file.Read(record.m_Intel) || !file.Read(markers)
				|| (destroyed != 0 && destroyed != 1) || record.m_Intel < 0 || markers < 0 || markers > 100000)
			{
				file.Close();
				return false;
			}
			record.m_Destroyed = destroyed == 1;
			for (int marker = 0; marker < markers; marker++)
			{
				int x, z, distance;
				if (!file.Read(x) || !file.Read(z) || !file.Read(distance) || distance < 0)
				{
					file.Close();
					return false;
				}
				record.m_X.Insert(x);
				record.m_Z.Insert(z);
				record.m_Distance.Insert(distance);
			}
			records.Insert(record);
		}
		file.Close();
		return true;
	}

	override protected void SaveIntelState()
	{
		if (!Replication.IsServer())
			return;
		if (INF_ZoneEditorContext.Grid(GetOwner()) && (m_bINFMissionEnded || !m_bINFIntelLoaded || !INF_CacheRosterComplete()))
			return;
		super.SaveIntelState();
	}

	override void AddIntelAndBroadcast(int amount, int searcherID = -1)
	{
		if (!Replication.IsServer())
			return;
		if (m_bINFMissionEnded)
			return;
		if (INF_ZoneEditorContext.Grid(GetOwner()) && !m_bINFIntelLoaded)
		{
			Print("[INF_Objectives] Intel award rejected before recovery is complete.", LogLevel.WARNING);
			return;
		}
		super.AddIntelAndBroadcast(amount, searcherID);
	}

	override void OnGameModeEnd(SCR_GameModeEndData data)
	{
		if (!INF_ZoneEditorContext.Grid(GetOwner()))
		{
			super.OnGameModeEnd(data);
			return;
		}
		if (Replication.IsServer())
		{
			Print(string.Format("[INF_Objectives] Mission ending: cacheVictoryAuthorised=%1, caches=%2/%3, selected=%4, mode=%5.",
				m_bINFMissionEnded, m_iCachesDestroyed, m_aCacheEntities.Count(), m_iINFSelectedCacheCount, m_iINFMissionEndMode));
			m_bINFMissionEnded = true;
		}
		GetGame().GetCallqueue().Remove(LoadIntelState);
		GetGame().GetCallqueue().Remove(INF_OnDebugVisibilityChanged);
		GetGame().GetCallqueue().Remove(INF_RefreshDebugMarkers);
		GetGame().GetCallqueue().Remove(INF_RefreshBaseMarkers);
		INF_ClearLocalDebugMarkers(m_aINFBaseMarkers);
		GetGame().GetCallqueue().Remove(BroadcastDebugMarkers);
		GetGame().GetCallqueue().Remove(INF_UpdateCampaignObjectives);
		m_aINFDebugSubscribers.Clear();
		GetGame().GetCallqueue().Remove(TriggerMissionComplete);
		ClearDebugMarkers();
		INF_ClearCampDebugMarkers();
		super.OnGameModeEnd(data);
	}

	override void RegisterCache(IEntity cacheEntity, vector pos)
	{
		if (!Replication.IsServer())
			return;
		if (INF_ZoneEditorContext.Grid(GetOwner()))
		{
			if (!cacheEntity)
			{
				Print("[INF_Objectives] Rejected null cache registration.", LogLevel.ERROR);
				return;
			}
			if (!m_aCacheEntities.Contains(cacheEntity))
			{
				array<vector> selectedPositions;
				INF_ForestCampRewards.GetCachePositions(selectedPositions);
				if (m_bINFIntelLoaded || m_bINFIntelLoadFailed || !selectedPositions.Contains(pos)
					|| m_aINFZoneCachePositions.Contains(pos))
				{
					Print("[INF_Objectives] Rejected cache registration outside the selected roster or after intel recovery.", LogLevel.ERROR);
					return;
				}
			}
		}
		if (Replication.IsServer() && !m_aCacheEntities.Contains(cacheEntity))
			m_aINFZoneCachePositions.Insert(pos);
		super.RegisterCache(cacheEntity, pos);

		if (Replication.IsServer() && m_bINFCacheObjective && m_INFCacheTask)
			INF_UpdateCacheObjective();
	}

	override void OnCacheDestroyed(IEntity cacheEntity)
	{
		if (!Replication.IsServer() || m_bINFMissionEnded)
			return;
		if (INF_ZoneEditorContext.Grid(GetOwner()))
		{
			int index = m_aCacheEntities.Find(cacheEntity);
			if (!cacheEntity || index < 0)
			{
				Print("[INF_Objectives] Ignored destruction of an unregistered cache.", LogLevel.WARNING);
				return;
			}
			if (!m_bINFIntelLoaded)
			{
				vector pos = m_aINFZoneCachePositions[index];
				if (!m_aINFPendingCacheDestructions.Contains(pos))
					m_aINFPendingCacheDestructions.Insert(pos);
				return;
			}
			if (m_aCacheDestroyed[index])
			{
				Print(string.Format("[INF_Objectives] Ignored duplicate destruction callback for cache %1.", index), LogLevel.WARNING);
				return;
			}
			m_iCachesDestroyed = 0;
			foreach (bool destroyed : m_aCacheDestroyed)
			{
				if (destroyed)
					m_iCachesDestroyed++;
			}
		}
		vector explosionPos = cacheEntity.GetOrigin();
		IEntity standIn = INF_SpawnCacheStandIn(cacheEntity);
		super.OnCacheDestroyed(cacheEntity);
		INF_SpawnCacheExplosion(explosionPos, standIn);
		INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(GetOwner().FindComponent(INS_AmbientPatrolManager));
		if (patrols && patrols.INF_GetZoneStates())
			patrols.INF_GetZoneStates().CacheDestroyed(cacheEntity);

		if (Replication.IsServer() && m_bINFCacheObjective)
			INF_UpdateCacheObjective();
		if (INF_ZoneEditorContext.Grid(GetOwner()))
		{
			// Upstream also queues victory. Keep the validated objective director as its sole scheduler.
			GetGame().GetCallqueue().Remove(TriggerMissionComplete);
			m_bINFMissionEndQueued = false;
			INF_DebugContentChanged();
			INF_QueueCampaignObjectives();
		}
	}

	protected void INF_SpawnCacheExplosion(vector pos, IEntity standIn)
	{
		// Vanilla explosives detonated on the spot give blast damage, effects and sound; spawned on the server they replicate.
		if (!Replication.IsServer())
			return;
		ResourceName grenade = "{645C73791ECA1698}Prefabs/Weapons/Grenades/Grenade_RGD5.et";
		ResourceName shell81 = "{38BAE094333E31BF}Prefabs/Weapons/Ammo/Ammo_Shell_81mm_HE_M821.et";
		ResourceName shell82 = "{98EC9C526AFBA282}Prefabs/Weapons/Ammo/Ammo_Shell_82mm_HE_O832DU.et";
		ResourceName demoBlock = "{33CBDE73AB48172A}Prefabs/Weapons/Explosives/DemoBlock_M112/DemoBlock_M112.et";
		ResourceName fuelBlast = "{8BC4C95E01FC9BD3}Prefabs/Weapons/Warheads/Explosions/Explosion_Fuel_Large.et";
		ResourceName ammoBlastSmall = "{6BB2F747B4896E02}Prefabs/Weapons/Warheads/Explosions/Explosion_AmmoRack_Small.et";
		ResourceName ammoBlast = "{7E091BB73AC18ED0}Prefabs/Weapons/Warheads/Explosions/Explosion_AmmoRack_Medium.et";

		// A cook-off that escalates so anyone nearby can get clear: a campfire-sized fire smoulders for 10 s, then grenades, rising fire and
		// blasts build to the finale. The vanilla vehicle ammo-fire sound grows with the fire and is put out shortly after the finale.
		int t = 10000;
		if (standIn)
			GetGame().GetCallqueue().CallLater(INF_DeleteSpentCharge, t + 11000, false, standIn);
		INF_ScheduleFire(false, pos, 0);
		INF_ScheduleFireSound(1, pos, 0);
		INF_ScheduleCharge(grenade, pos + "0.5 0.5 0", t + 3500);
		INF_ScheduleFire(true, pos, t + 3500);
		INF_ScheduleFireSound(2, pos, t + 5000);
		INF_ScheduleCharge(grenade, pos + "-0.5 0.5 0.5", t + 6000);
		INF_ScheduleCharge(ammoBlastSmall, pos + "0 0.5 0", t + 7000);
		INF_ScheduleCharge(grenade, pos + "0.5 0.5 -0.5", t + 7800);
		INF_ScheduleCharge(shell82, pos + "0 0.5 0", t + 8500);
		INF_ScheduleCharge(demoBlock, pos + "0 0.5 0", t + 9500);
		INF_ScheduleCharge(ammoBlast, pos + "0 0.5 0", t + 10000);
		INF_ScheduleCharge(demoBlock, pos + "0 0.5 0", t + 10500);
		INF_ScheduleCharge(fuelBlast, pos + "0 0.5 0", t + 11000);
		INF_ScheduleCharge(ammoBlast, pos + "0 0.5 0", t + 11000);
		GetGame().GetCallqueue().CallLater(INF_BroadcastBlastSound, t + 11000, false, pos);
		INF_ScheduleCharge(shell82, pos + "0 0.5 0", t + 11000);
		INF_ScheduleFireSound(0, pos, t + 15000);
	}
	// The destroyed cache vanishes at once, so an invincible copy of its prop stands in until the finale.
	protected IEntity INF_SpawnCacheStandIn(IEntity cacheEntity)
	{
		if (!Replication.IsServer() || !cacheEntity)
			return null;
		EntityPrefabData prefabData = cacheEntity.GetPrefabData();
		if (!prefabData)
			return null;
		Resource res = Resource.Load(prefabData.GetPrefabName());
		if (!res || !res.IsValid())
			return null;
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		cacheEntity.GetWorldTransform(params.Transform);
		IEntity standIn = GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), params);
		if (!standIn)
			return null;
		DamageManagerComponent damage = DamageManagerComponent.Cast(standIn.FindComponent(DamageManagerComponent));
		if (damage)
			damage.EnableDamageHandling(false);
		return standIn;
	}

	protected void INF_BroadcastBlastSound(vector pos)
	{
		Rpc(INF_RpcBlastSound, pos);
		INF_RpcBlastSound(pos);
	}

	// Plays the vanilla fuel-explosion sound in 3D at the cache; the entity is local to each machine and removed afterwards.
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void INF_RpcBlastSound(vector pos)
	{
		Resource res = Resource.Load("{6AA19CB200000544}Prefabs/Effects/INF_CacheFireSound.et");
		if (!res || !res.IsValid())
			return;
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		params.Transform[3] = pos;
		IEntity blast = GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), params);
		if (!blast)
			return;
		SoundComponent sound = SoundComponent.Cast(blast.FindComponent(SoundComponent));
		if (sound)
			sound.SoundEvent("SOUND_EXPLOSION");
		GetGame().GetCallqueue().CallLater(INF_DeleteSpentCharge, 8000, false, blast);
	}

	protected void INF_ScheduleFireSound(int scale, vector pos, int delayMs)
	{
		GetGame().GetCallqueue().CallLater(INF_BroadcastFireSound, delayMs, false, scale, pos);
	}

	protected void INF_BroadcastFireSound(int scale, vector pos)
	{
		// Sound entities are local to each machine, so every machine runs its own fire sound.
		Rpc(INF_RpcFireSound, scale, pos);
		INF_RpcFireSound(scale, pos);
	}

	// Drives the vanilla vehicle ammo-rack fire sound: scale 1-3 is small to large, 0 puts it out.
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void INF_RpcFireSound(int scale, vector pos)
	{
		if (scale <= 0)
		{
			if (m_INFFireSoundEntity)
			{
				AudioSystem.TerminateSoundFadeOut(m_INFFireSoundHandle, true, 1.5);
				SCR_EntityHelper.DeleteEntityAndChildren(m_INFFireSoundEntity);
			}
			m_INFFireSoundEntity = null;
			m_INFFireSoundHandle = AudioHandle.Invalid;
			return;
		}

		if (!m_INFFireSoundEntity)
		{
			Resource res = Resource.Load("{6AA19CB200000544}Prefabs/Effects/INF_CacheFireSound.et");
			if (!res || !res.IsValid())
			{
				Print("[INF_Objectives] Cache fire sound prefab failed to load.", LogLevel.ERROR);
				return;
			}
			EntitySpawnParams params = new EntitySpawnParams();
			params.TransformMode = ETransformMode.WORLD;
			params.Transform[3] = pos;
			m_INFFireSoundEntity = GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), params);
			if (!m_INFFireSoundEntity)
				return;
		}

		SignalsManagerComponent signals = SignalsManagerComponent.Cast(m_INFFireSoundEntity.FindComponent(SignalsManagerComponent));
		if (signals)
			signals.SetSignalValue(signals.AddOrFindSignal("SuppliesFireState"), scale);

		SoundComponent sound = SoundComponent.Cast(m_INFFireSoundEntity.FindComponent(SoundComponent));
		if (sound && m_INFFireSoundHandle == AudioHandle.Invalid)
			m_INFFireSoundHandle = sound.SoundEvent("SOUND_VEHICLE_AMMORACKFIRE_LP");
	}
	protected void INF_ScheduleFire(bool medium, vector pos, int delayMs)
	{
		GetGame().GetCallqueue().CallLater(INF_BroadcastFire, delayMs, false, medium, pos);
	}

	protected void INF_BroadcastFire(bool medium, vector pos)
	{
		// Particle entities are not replicated, so every machine spawns its own fire.
		Rpc(INF_RpcFire, medium, pos);
		INF_RpcFire(medium, pos);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void INF_RpcFire(bool medium, vector pos)
	{
		ResourceName particle = "{C0E2E7DC28B71E2C}Particles/Enviroment/Campfire_medium_normal.ptc";
		int burnMs = 13500;
		if (medium)
		{
			particle = "{B073169BAA0BDD62}Particles/Logistics/Fire/Ammunition_Fire/Ammunition_Fire_Medium.ptc";
			burnMs = 11500;
		}		ParticleEffectEntitySpawnParams spawnParams = new ParticleEffectEntitySpawnParams();
		spawnParams.TransformMode = ETransformMode.WORLD;
		spawnParams.Transform[3] = pos;
		ParticleEffectEntity fire = ParticleEffectEntity.SpawnParticleEffect(particle, spawnParams);
		if (!fire)
		{
			Print("[INF_Objectives] Cache fire particle failed to spawn.", LogLevel.ERROR);
			return;
		}
		GetGame().GetCallqueue().CallLater(fire.StopEmission, burnMs);
	}

	protected void INF_ScheduleCharge(ResourceName charge, vector pos, int delayMs)
	{
		GetGame().GetCallqueue().CallLater(INF_DetonateCharge, delayMs, false, charge, pos);
	}

	protected void INF_DetonateCharge(ResourceName charge, vector pos)
	{
		Resource res = Resource.Load(charge);
		if (!res || !res.IsValid())
		{
			Print(string.Format("[INF_Objectives] Cache explosion prefab failed to load: %1", charge), LogLevel.ERROR);
			return;
		}
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		params.Transform[3] = pos;
		IEntity entity = GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), params);
		if (!entity)
			return;
		// Effect-only prefabs may fire on spawn without a trigger component.
		BaseTriggerComponent trigger = BaseTriggerComponent.Cast(entity.FindComponent(BaseTriggerComponent));
		if (trigger)
		{
			// Mortar shells are inert until armed, so arm before detonating.
			trigger.SetLive();
			trigger.OnUserTrigger(entity);
		}
		// Fire prefabs manage their own burn time, so only spent explosives are cleaned up.
		if (!charge.Contains("/Fires/"))
			GetGame().GetCallqueue().CallLater(INF_DeleteSpentCharge, 3000, false, entity);
	}

	protected void INF_DeleteSpentCharge(IEntity entity)
	{
		if (entity)
			SCR_EntityHelper.DeleteEntityAndChildren(entity);
	}
	override protected void ShowCacheDestroyedHint(int destroyed, int total)
	{
		if (INF_ZoneEditorContext.Grid(GetOwner()) && m_iINFMissionEndMode != INF_EMissionEndMode.CACHES && destroyed >= total)
		{
			SCR_HintManagerComponent.ShowCustomHint("All weapon caches destroyed. Complete the remaining capture objectives.",
				"Caches Destroyed", 8);
			return;
		}
		super.ShowCacheDestroyedHint(destroyed, total);
	}

	override void OnPlayerRegistered(int playerId)
	{
		super.OnPlayerRegistered(playerId);

		if (!Replication.IsServer() || !m_bINFCacheObjective)
			return;

		SCR_TaskSystem taskSystem = SCR_TaskSystem.GetInstance();
		if (!taskSystem)
		{
			Print("[INF_CacheObjective] Cannot assign objective: SCR_TaskSystem is unavailable.", LogLevel.ERROR);
			return;
		}

		if (m_INFCacheTask)
			INF_AssignObjective(taskSystem, m_INFCacheTask, playerId);
		if (m_INFCampTask)
			INF_AssignObjective(taskSystem, m_INFCampTask, playerId);
		if (m_INFZoneTask)
			INF_AssignObjective(taskSystem, m_INFZoneTask, playerId);
	}

	protected void INF_AssignObjective(SCR_TaskSystem taskSystem, SCR_Task task, int playerId)
	{
		SCR_TaskExecutorPlayer executor = new SCR_TaskExecutorPlayer();
		executor.SetPlayerID(playerId);
		taskSystem.AssignTask(task, executor);
	}

	protected void INF_UpdateCacheObjective()
	{
		string title = "Caches - initialising";
		if (m_bINFIntelLoaded)
			title = string.Format("Destroyed %1/%2 Caches", m_iCachesDestroyed, m_aCacheEntities.Count());
		string description = "Search hostile bodies for intel. Use the marker hints on the map to locate and destroy hostile weapon caches. Gathering more intel narrows down their locations.";

		m_INFCacheTask = INF_UpdateCaptureTask(m_INFCacheTask, "INF_DestroyHostileCaches", title, description);
	}
}
