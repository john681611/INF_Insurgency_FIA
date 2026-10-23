class INF_ZoneCacheSite
{
	IEntity m_Entity;
	vector m_Position;
	bool m_Destroyed;
}

// Authoritative territory and site state; does not spawn or delete AI.
class INF_ZoneStateSystem
{
	protected ref map<string, INS_AmbientPatrolZoneState> m_Cells = new map<string, INS_AmbientPatrolZoneState>();
	protected ref array<ref INS_AmbientPatrolZoneState> m_Zones = {};
	protected ref array<ref INF_ZoneCacheSite> m_Caches = {};
	protected ref array<INS_AmbientPatrolZoneState> m_Camps = {};
	protected ref ScriptInvoker m_OnCaptured = new ScriptInvoker();
	protected ref ScriptInvoker m_OnChanged = new ScriptInvoker();
	protected ref ScriptInvoker m_OnObjectivesChanged = new ScriptInvoker();
	protected ref array<INS_AmbientPatrolZoneState> m_Active = {};
	protected ref array<INS_AmbientPatrolZoneState> m_DefenderChecks = {};
	protected ref array<INS_AmbientPatrolZoneState> m_Candidates = {};
	protected ref array<INS_AmbientPatrolZoneState> m_Holds = {};
	protected ref array<INS_AmbientPatrolZoneState> m_Attacks = {};
	protected ref INF_BlockPresence m_Presence;
	protected float m_PlayerSampleAt = -1;
	protected float m_EnemySampleAt = -1;
	protected bool m_CaptureQueued;
	protected bool m_Stopped;
	protected int m_SaveRetryMS = 5000;
	protected IEntity m_Owner;
	protected ref INF_TerritoryPersistence m_Persistence = new INF_TerritoryPersistence();

	bool IsPersistenceReady()
	{
		return m_Persistence.IsReady();
	}

	void SavePermanent(INS_AmbientPatrolZoneState state)
	{
		if (m_Persistence.Record(state))
			FlushPermanent();
	}

	void CommitCaptureState(INS_AmbientPatrolZoneState state)
	{
		SavePermanent(state);
		NotifyChanged(state, true);
	}

	protected void CompleteCapture(INS_AmbientPatrolZoneState state)
	{
		m_OnCaptured.Invoke(state);
		CommitCaptureState(state);
	}

	protected void FlushPermanent()
	{
		m_Persistence.Flush();
		GetGame().GetCallqueue().Remove(FlushPermanent);
		if (m_Persistence.IsDirty() && !m_Stopped)
		{
			GetGame().GetCallqueue().CallLater(FlushPermanent, m_SaveRetryMS, false);
			m_SaveRetryMS = Math.Min(m_SaveRetryMS * 2, 30000);
		}
		else
			m_SaveRetryMS = 5000;
	}

	ScriptInvoker GetOnChanged()
	{
		return m_OnChanged;
	}

	ScriptInvoker GetOnObjectivesChanged()
	{
		return m_OnObjectivesChanged;
	}

	array<INS_AmbientPatrolZoneState> GetActiveZones()
	{
		return m_Active;
	}

	array<INS_AmbientPatrolZoneState> GetAttackZones()
	{
		return m_Attacks;
	}

	void CollectCampPositions(array<vector> positions)
	{
		foreach (INS_AmbientPatrolZoneState state : m_Camps)
		{
			if (state.m_INFCamp)
				positions.Insert(state.m_INFCamp.m_Position);
		}
	}

	INS_AmbientPatrolZoneState GetCell(vector position)
	{
		return m_Cells.Get(INF_BuildingGrid.Key(position));
	}

	void CollectNearby(array<vector> players, float radius, array<INS_AmbientPatrolZoneState> zones)
	{
		map<string, bool> seen = new map<string, bool>();
		foreach (vector player : players)
		{
			int minX = INF_BuildingGrid.CellIndex(player[0] - radius);
			int maxX = INF_BuildingGrid.CellIndex(player[0] + radius);
			int minZ = INF_BuildingGrid.CellIndex(player[2] - radius);
			int maxZ = INF_BuildingGrid.CellIndex(player[2] + radius);
			for (int x = minX; x <= maxX; x++)
			{
				for (int z = minZ; z <= maxZ; z++)
				{
					string key = x.ToString() + ":" + z.ToString();
					INS_AmbientPatrolZoneState state = m_Cells.Get(key);
					if (!state || seen.Contains(key) || vector.DistanceSqXZ(player, state.m_vCenter) > radius * radius)
						continue;
					seen.Set(key, true);
					zones.Insert(state);
				}
			}
		}
	}

	INF_BlockPresence GetPresence(bool enemies = false, bool fresh = false, int maxAgeMS = 2000)
	{
		float now = GetGame().GetWorld().GetWorldTime();
		if (!m_Presence)
			m_Presence = new INF_BlockPresence();
		if (m_PlayerSampleAt < 0 || now - m_PlayerSampleAt >= maxAgeMS || (fresh && m_PlayerSampleAt != now))
		{
			m_Presence.CollectPlayers();
			m_PlayerSampleAt = now;
		}
		if (enemies && (m_EnemySampleAt < 0 || now - m_EnemySampleAt >= 1000 || (fresh && m_EnemySampleAt != now)))
		{
			m_Presence.CollectEnemies(m_Owner);
			m_EnemySampleAt = now;
		}
		return m_Presence;
	}

	void InvalidatePresence()
	{
		m_PlayerSampleAt = -1;
		m_EnemySampleAt = -1;
		if (m_Presence)
			m_Presence.Invalidate();
	}

	void WakeCapture()
	{
		if (!m_Stopped && !m_Candidates.IsEmpty() && !m_CaptureQueued)
		{
			m_CaptureQueued = true;
			GetGame().GetCallqueue().CallLater(CaptureTick, 2000, false);
		}
	}

	void NotifyChanged(INS_AmbientPatrolZoneState state, bool objective = false)
	{
		if (!Replication.IsServer() || m_Stopped)
			return;
		m_DefenderChecks.RemoveItem(state);
		if (state.m_bActive && (!state.m_bEverHadLiving || state.m_bINFInboundWaiting))
			m_DefenderChecks.Insert(state);
		m_Active.RemoveItem(state);
		if (state.m_bActive)
		{
			m_Active.Insert(state);
			INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(m_Owner.FindComponent(INS_AmbientPatrolManager));
			if (patrols)
				patrols.INF_ScheduleProximity();
			INS_AmbientVehicleManager vehicles = INS_AmbientVehicleManager.Cast(m_Owner.FindComponent(INS_AmbientVehicleManager));
			if (vehicles)
				vehicles.INF_ScheduleZoneVehicles();
		}
		m_Attacks.RemoveItem(state);
		if (state.m_bINFAttackPending || state.m_bINFAttackActive)
			m_Attacks.Insert(state);
		if (state.m_bINFAttackPending)
		{
			INS_CounterAttackManager attacks = INS_CounterAttackManager.Cast(m_Owner.FindComponent(INS_CounterAttackManager));
			if (attacks)
				attacks.INF_ScheduleGridAttacks();
		}
		m_Candidates.RemoveItem(state);
		state.m_bINFCaptureCandidate = !state.m_bINFPermanent && !state.m_bINFMarkedCleared
			&& (state.m_bActive || state.m_bEverHadLiving || state.m_bINFAttackSpent || state.m_bINFSpawnExhausted);
		if (state.m_bINFCaptureCandidate)
			m_Candidates.Insert(state);
		m_Holds.RemoveItem(state);
		if (state.m_bINFCaptureCandidate && state.m_fINFCaptureSince >= 0)
			m_Holds.Insert(state);
		if (!m_Candidates.IsEmpty() && !m_CaptureQueued)
		{
			m_CaptureQueued = true;
			GetGame().GetCallqueue().CallLater(CaptureTick, 1000, false);
		}
		m_OnChanged.Invoke(state);
		if (objective)
			m_OnObjectivesChanged.Invoke();
	}

	bool RestorePermanentCamps()
	{
		array<string> savedOnly = {};
		if (!m_Persistence.CollectSavedOnlyCells(m_Zones, savedOnly))
			return false;
		bool ready = IsPersistenceReady();
		foreach (INS_AmbientPatrolZoneState state : m_Zones)
		{
			string key = INF_BuildingGrid.Key(state.m_vCenter);
			if (!m_Persistence.HasCell(key))
				continue;
			if (!m_Persistence.MatchesCamp(key, state.m_INFCamp))
			{
				Print("[INF_Territory] Saved territory disagrees with the camp roster: " + key, LogLevel.ERROR);
				ready = false;
				continue;
			}
			if (state.m_INFCamp && !state.m_INFCamp.m_Friendly)
				m_OnCaptured.Invoke(state);
			if (!state.m_INFCamp)
				state.m_bINFObjectiveCaptured = true;
			if (!state.m_bINFObjectiveCaptured)
				ready = false;
			NotifyChanged(state);
		}
		return ready;
	}

	void CollectSavedOnlyCells(array<string> keys)
	{
		m_Persistence.CollectSavedOnlyCells(m_Zones, keys);
	}

	void Start(IEntity owner)
	{
		if (!Replication.IsServer())
			return;
		m_Owner = owner;
		m_Persistence.Load();
	}

	void Stop()
	{
		m_Stopped = true;
		GetGame().GetCallqueue().Remove(CaptureTick);
		GetGame().GetCallqueue().Remove(FlushPermanent);
		m_Persistence.Flush();
		m_Presence = null;
	}

	protected void CaptureTick()
	{
		m_CaptureQueued = false;
		if (m_Stopped || !IsPersistenceReady() || m_Candidates.IsEmpty())
			return;
		INF_BlockPresence presence = GetPresence(false, false, 1000);
		array<INS_AmbientPatrolZoneState> defenderChecks = {};
		defenderChecks.Copy(m_DefenderChecks);
		foreach (INS_AmbientPatrolZoneState active : defenderChecks)
		{
			ObserveDefenders(active, presence);
			ReinforcementsSettled(active);
		}
		array<INS_AmbientPatrolZoneState> candidates = {};
		array<INS_AmbientPatrolZoneState> nearby = {};
		CollectNearby(presence.m_Capturers, 200, nearby);
		foreach (INS_AmbientPatrolZoneState local : nearby)
		{
			if (local.m_bINFCaptureCandidate)
				candidates.Insert(local);
		}
		foreach (INS_AmbientPatrolZoneState holding : m_Holds)
		{
			if (!candidates.Contains(holding))
				candidates.Insert(holding);
		}
		bool sensitive;
		foreach (INS_AmbientPatrolZoneState state : candidates)
		{
			if (state.m_bINFPermanent || state.m_bINFMarkedCleared)
				continue;
			bool capturerNearby = INF_BlockRules.Nearest(state.m_vCenter, presence.m_Capturers) <= 200;
			if (!capturerNearby && state.m_fINFCaptureSince < 0)
				continue;
			sensitive = true;
			presence = GetPresence(true, false, 1000);
			CheckCapture(state, presence, !state.m_bINFInboundWaiting);
		}
		if (!m_Candidates.IsEmpty() && !m_CaptureQueued && !presence.m_PlayerIDs.IsEmpty())
		{
			int interval = 2000;
			if (sensitive)
				interval = 1000;
			m_CaptureQueued = true;
			GetGame().GetCallqueue().CallLater(CaptureTick, interval, false);
		}
	}

	bool ReinforcementsSettled(INS_AmbientPatrolZoneState state)
	{
		bool waiting;
		float now = GetGame().GetWorld().GetWorldTime();
		INF_BlockPresence presence = GetPresence(false, false, 1000);
		foreach (INS_PatrolAgent agent : state.m_aAgents)
		{
			if (!agent.m_bINFInbound || !agent.m_Group)
				continue;
			array<vector> living = presence.GroupPositions(agent.m_Group);
			if (!living.IsEmpty() && agent.m_fINFInboundDeadline < 0)
				agent.m_fINFInboundDeadline = now + 300000;
			bool arrived = living.IsEmpty() && (agent.m_fINFInboundDeadline >= 0
				|| now >= state.m_fINFSpawnReadyAt + 30000);
			foreach (vector soldier : living)
			{
				if (vector.DistanceSqXZ(soldier, state.m_vCenter) <= 40000)
					arrived = true;
			}
			if (arrived || (agent.m_fINFInboundDeadline >= 0 && now >= agent.m_fINFInboundDeadline))
				agent.m_bINFInbound = false;
			else
				waiting = true;
		}
		if (state.m_bINFInboundWaiting != waiting)
		{
			state.m_bINFInboundWaiting = waiting;
			NotifyChanged(state);
		}
		return !waiting;
	}

	ScriptInvoker GetOnCaptured()
	{
		return m_OnCaptured;
	}

	array<ref INS_AmbientPatrolZoneState> GetZones()
	{
		return m_Zones;
	}

	protected INS_AmbientPatrolZoneState EnsureZone(vector position)
	{
		string key = INF_BuildingGrid.Key(position);
		INS_AmbientPatrolZoneState state = m_Cells.Get(key);
		if (state)
			return state;
		vector center = INF_BuildingGrid.Center(position);
		center[1] = GetGame().GetWorld().GetSurfaceY(center[0], center[2]);
		if (INF_BuildingGrid.IsExcluded(center))
			return null;
		state = new INS_AmbientPatrolZoneState(center, INF_BuildingGrid.HALF, 1);
		state.m_bINFBuildingCell = true;
		// Camp and cache cells have no counted buildings but keep one fireteam; AddBuilding replaces this.
		state.m_iINFTargetAI = 4;
		state.m_iINFBaseGroups = 1;
		if (m_Persistence.HasCell(key))
		{
			state.m_bINFMarkedCleared = true;
			state.m_bINFPermanent = true;
			state.m_bReadyToRespawn = false;
		}
		m_Cells.Set(key, state);
		m_Zones.Insert(state);
		NotifyChanged(state, true);
		return state;
	}

	// A ruin (flagged by a height offset in the index) adds RUIN_AI; a building adds BUILDING_AI.
	void AddBuilding(vector position)
	{
		int kind = INF_BuildingGrid.StoredKind(position);
		if (kind == INF_BuildingGrid.STORED_SMALL)
			return;
		INS_AmbientPatrolZoneState state = EnsureZone(position);
		if (!state)
			return;
		bool ruin = kind == INF_BuildingGrid.STORED_RUIN;
		position = INF_BuildingGrid.StoredPosition(position);
		// Stacked or split models of one building share an origin; count each spot once.
		foreach (vector counted : state.m_aINFBuildingPositions)
		{
			if (vector.DistanceSqXZ(counted, position) < 9)
				return;
		}
		state.m_aINFBuildingPositions.Insert(position);
		if (ruin)
			state.m_iINFRuins++;
		else
			state.m_iINFBuildings++;
		int previousTarget = state.m_iINFTargetAI;
		int weighted = state.m_iINFBuildings * INF_BuildingGrid.BUILDING_AI + state.m_iINFRuins * INF_BuildingGrid.RUIN_AI;
		// Odd totals (from ruins) round up so no group is a lone soldier.
		state.m_iINFTargetAI = Math.Min(weighted + (weighted - (weighted / 2) * 2), 16);
		state.m_iGroupCount = state.m_iINFBaseGroups;
		// Zone map areas are shaded by target AI, so they need to hear about budget changes during the scan.
		if (state.m_iINFTargetAI != previousTarget)
			m_OnChanged.Invoke(state);
	}

	void AddCamp(INF_ForestCampSite camp)
	{
		INS_AmbientPatrolZoneState state = EnsureZone(camp.m_Position);
		if (state)
		{
			state.m_INFCamp = camp;
			if (!m_Camps.Contains(state))
				m_Camps.Insert(state);
			NotifyChanged(state, true);
		}
	}

	void AddCache(IEntity entity, vector position, bool destroyed)
	{
		foreach (INF_ZoneCacheSite existing : m_Caches)
		{
			if (existing.m_Position == position)
				return;
		}
		INS_AmbientPatrolZoneState state = EnsureZone(position);
		if (!state)
		{
			Print("[INF_Zones] Cache overlaps a safe zone; no hostile zone created.", LogLevel.WARNING);
			return;
		}
		INF_ZoneCacheSite cache = new INF_ZoneCacheSite();
		cache.m_Entity = entity;
		cache.m_Position = position;
		cache.m_Destroyed = destroyed;
		m_Caches.Insert(cache);
	}

	static bool InInfluence(vector zone, vector site)
	{
		int zoneX = INF_BuildingGrid.CellIndex(zone[0]);
		int zoneZ = INF_BuildingGrid.CellIndex(zone[2]);
		int siteX = INF_BuildingGrid.CellIndex(site[0]);
		int siteZ = INF_BuildingGrid.CellIndex(site[2]);
		int dx = Math.AbsInt(zoneX - siteX);
		int dz = Math.AbsInt(zoneZ - siteZ);
		return dx <= 1 && dz <= 1;
	}

	bool IsInfluenced(INS_AmbientPatrolZoneState state)
	{
		foreach (INS_AmbientPatrolZoneState source : m_Camps)
		{
			if (source.m_INFCamp && !source.m_bINFObjectiveCaptured
				&& InInfluence(state.m_vCenter, source.m_INFCamp.m_Position))
				return true;
		}
		foreach (INF_ZoneCacheSite cache : m_Caches)
		{
			if (!cache.m_Destroyed && InInfluence(state.m_vCenter, cache.m_Position))
				return true;
		}
		return false;
	}

	void CacheDestroyed(IEntity entity)
	{
		foreach (INF_ZoneCacheSite cache : m_Caches)
		{
			if (cache.m_Entity == entity)
			{
				cache.m_Destroyed = true;
				return;
			}
		}
		Print("[INF_Zones] Destroyed cache was not in the static zone registry.", LogLevel.WARNING);
	}

	void ObserveDefenders(INS_AmbientPatrolZoneState state, INF_BlockPresence presence = null)
	{
		if (state.m_bEverHadLiving || INF_BlockRules.HasMembers(state.m_aAgents, presence))
		{
			state.m_bEverHadLiving = true;
			state.m_iINFFailedActivations = 0;
			state.m_bINFSpawnExhausted = false;
			if (!state.m_bINFInboundWaiting)
				m_DefenderChecks.RemoveItem(state);
		}
	}

	void CheckCapture(INS_AmbientPatrolZoneState state, INF_BlockPresence presence, bool settled)
	{
		ObserveDefenders(state, presence);
		if (state.m_bINFMarkedCleared || GetGame().GetWorld().GetWorldTime() < state.m_fINFSpawnReadyAt)
			return;
		bool eligible = (state.m_bEverHadLiving || state.m_bINFSpawnExhausted) && state.m_iINFPendingGroups == 0
			&& settled && presence.CanCapture(state.m_vCenter);
		float since = state.m_fINFCaptureSince;
		float previousSince = since;
		if (eligible && since >= 0 && GetGame().GetWorld().GetWorldTime() - since >= INF_BlockRules.CAPTURE_HOLD_MS)
		{
			presence = GetPresence(true, true);
			eligible = presence.CanCapture(state.m_vCenter);
		}
		bool captured = INF_BlockRules.AdvanceCapture(eligible, GetGame().GetWorld().GetWorldTime(), since);
		state.m_fINFCaptureSince = since;
		if ((since >= 0) != (previousSince >= 0))
			NotifyChanged(state);
		if (!captured)
			return;
		state.m_bINFMarkedCleared = true;
		if (!state.m_INFCamp)
			state.m_bINFObjectiveCaptured = true;
		state.m_bReadyToRespawn = false;
		state.m_bINFPermanent = state.m_bINFAttackSpent && !state.m_bINFAttackActive;
		if (!state.m_bINFAttackSpent && !state.m_bINFAttackPending)
		{
			INS_CounterAttackManager attacks = INS_CounterAttackManager.Cast(m_Owner.FindComponent(INS_CounterAttackManager));
			state.m_bINFPermanent = !attacks || !attacks.INF_CommitCapture(state);
		}
		CompleteCapture(state);
	}

	void AttackStarted(INS_AmbientPatrolZoneState state)
	{
		state.m_bINFAttackPending = false;
		state.m_bINFAttackSpent = true;
		state.m_bINFAttackActive = true;
		state.m_bReadyToRespawn = false;
		state.m_fINFRetakeSince = -1;
		NotifyChanged(state);
	}

	void ResolvePendingAttack(INS_AmbientPatrolZoneState state, bool playersNearby)
	{
		state.m_bINFAttackPending = false;
		state.m_bINFAttackActive = false;
		state.m_fINFCaptureSince = -1;
		if (playersNearby)
		{
			state.m_bINFMarkedCleared = true;
			state.m_bINFPermanent = true;
			state.m_bReadyToRespawn = false;
			CompleteCapture(state);
			return;
		}
		else
		{
			state.m_bINFMarkedCleared = false;
			state.m_bINFAttackSpent = false;
			state.m_bReadyToRespawn = true;
		}
		NotifyChanged(state, true);
	}

	void CheckRetake(INS_AmbientPatrolZoneState state, array<vector> attackers, array<vector> players)
	{
		if (!state.m_bINFAttackActive || !state.m_bINFMarkedCleared)
		{
			state.m_fINFRetakeSince = -1;
			return;
		}
		bool occupied = false;
		foreach (vector attacker : attackers)
		{
			if (INF_BuildingGrid.Contains(state.m_vCenter, attacker))
				occupied = true;
		}
		foreach (vector player : players)
		{
			if (vector.DistanceSqXZ(player, state.m_vCenter) <= 40000)
				occupied = false;
		}
		float since = state.m_fINFRetakeSince;
		bool retaken = INF_BlockRules.AdvanceCapture(occupied, GetGame().GetWorld().GetWorldTime(), since);
		state.m_fINFRetakeSince = since;
		if (!retaken)
			return;
		state.m_bINFMarkedCleared = false;
		state.m_bEverHadLiving = true;
		state.m_fINFCaptureSince = -1;
		NotifyChanged(state);
	}

	void AttackEnded(INS_AmbientPatrolZoneState state, bool hadLiving, bool defeated)
	{
		state.m_bINFAttackActive = false;
		state.m_fINFRetakeSince = -1;
		if (!hadLiving || !defeated)
		{
			state.m_bINFAttackSpent = false;
			state.m_bINFAttackPending = true;
			state.m_fINFAttackRetryAt = GetGame().GetWorld().GetWorldTime() + 5000;
		}
		else if (state.m_bINFMarkedCleared && !state.m_bINFPermanent)
		{
			state.m_bINFPermanent = true;
			CompleteCapture(state);
			return;
		}
		NotifyChanged(state, true);
	}
}
