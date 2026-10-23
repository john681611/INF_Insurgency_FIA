class INF_PresenceBucket
{
	ref array<vector> m_Positions = {};
}

class INF_BlockPresence
{
	ref array<int> m_PlayerIDs = {};
	ref array<vector> m_AllPlayers = {};
	ref array<vector> m_Players = {};
	ref array<vector> m_Capturers = {};
	ref array<vector> m_Enemies = {};
	protected ref map<string, ref INF_PresenceBucket> m_EnemyCells = new map<string, ref INF_PresenceBucket>();
	protected ref map<SCR_AIGroup, ref INF_PresenceBucket> m_GroupPositions = new map<SCR_AIGroup, ref INF_PresenceBucket>();
	protected ref INF_ActivationPolicy m_ActivationPolicy;

	void Invalidate()
	{
		m_GroupPositions.Clear();
		m_ActivationPolicy = null;
	}

	INF_ActivationPolicy ActivationPolicy(IEntity owner)
	{
		if (!m_ActivationPolicy)
		{
			m_ActivationPolicy = new INF_ActivationPolicy();
			m_ActivationPolicy.Collect(owner, this);
		}
		return m_ActivationPolicy;
	}

	static array<vector> PlayerPositions(bool fresh = false)
	{
		IEntity mode = GetGame().GetGameMode();
		INS_AmbientPatrolManager patrols;
		if (mode)
			patrols = INS_AmbientPatrolManager.Cast(mode.FindComponent(INS_AmbientPatrolManager));
		if (patrols && patrols.INF_GetZoneStates())
			return patrols.INF_GetZoneStates().GetPresence(false, fresh).m_AllPlayers;
		INF_BlockPresence presence = new INF_BlockPresence();
		presence.CollectPlayers();
		return presence.m_AllPlayers;
	}

	static void InvalidateWorld()
	{
		IEntity mode = GetGame().GetGameMode();
		if (!mode || !Replication.IsServer())
			return;
		INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(mode.FindComponent(INS_AmbientPatrolManager));
		if (patrols && patrols.INF_GetZoneStates())
			patrols.INF_GetZoneStates().InvalidatePresence();
	}

	void Collect(IEntity owner)
	{
		CollectPlayers();
		CollectEnemies(owner);
	}

	void CollectPlayers()
	{
		Invalidate();
		m_PlayerIDs.Clear();
		m_AllPlayers.Clear();
		m_Players.Clear();
		m_Capturers.Clear();
		GetGame().GetPlayerManager().GetPlayers(m_PlayerIDs);
		foreach (int id : m_PlayerIDs)
		{
			IEntity player = GetGame().GetPlayerManager().GetPlayerControlledEntity(id);
			if (!player)
				continue;
			m_AllPlayers.Insert(player.GetOrigin());
			DamageManagerComponent damage = DamageManagerComponent.Cast(player.FindComponent(DamageManagerComponent));
			if (!damage || !damage.IsDestroyed())
			{
				m_Players.Insert(player.GetOrigin());
				CompartmentAccessComponent access = CompartmentAccessComponent.Cast(player.FindComponent(CompartmentAccessComponent));
				CharacterControllerComponent controller = CharacterControllerComponent.Cast(player.FindComponent(CharacterControllerComponent));
				vector pos = player.GetOrigin();
				float ground = GetGame().GetWorld().GetSurfaceY(pos[0], pos[2]);
				if (ChimeraCharacter.Cast(player) && damage && controller && !controller.IsUnconscious()
					&& controller.GetLifeState() == ECharacterLifeState.ALIVE
					&& (!access || !access.IsInCompartment()) && ground > 0.5
					&& pos[1] - ground >= -1 && pos[1] - ground <= 5)
					m_Capturers.Insert(pos);
			}
		}
	}

	void CollectEnemies(IEntity owner)
	{
		m_Enemies.Clear();
		m_EnemyCells.Clear();
		m_GroupPositions.Clear();
		array<SCR_AIGroup> groups = {};
		INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(owner.FindComponent(INS_AmbientPatrolManager));
		if (patrols)
			patrols.INF_CollectGroups(groups);
		INS_CounterAttackManager attacks = INS_CounterAttackManager.Cast(owner.FindComponent(INS_CounterAttackManager));
		if (attacks)
			attacks.INF_CollectGroups(groups);
		INS_FieldThreatManager threats = INS_FieldThreatManager.Cast(owner.FindComponent(INS_FieldThreatManager));
		if (threats)
			threats.INF_CollectGroups(groups);
		INS_AmbientVehicleManager vehicles = INS_AmbientVehicleManager.Cast(owner.FindComponent(INS_AmbientVehicleManager));
		if (vehicles)
			vehicles.INF_CollectGroups(groups);
		map<SCR_AIGroup, bool> seen = new map<SCR_AIGroup, bool>();
		foreach (SCR_AIGroup group : groups)
		{
			if (!group || seen.Contains(group))
				continue;
			seen.Set(group, true);
			array<vector> positions = GroupPositions(group);
			foreach (vector soldier : positions)
				m_Enemies.Insert(soldier);
		}

		foreach (vector enemy : m_Enemies)
		{
			string key = INF_BuildingGrid.Key(enemy);
			INF_PresenceBucket bucket = m_EnemyCells.Get(key);
			if (!bucket)
			{
				bucket = new INF_PresenceBucket();
				m_EnemyCells.Set(key, bucket);
			}
			bucket.m_Positions.Insert(enemy);
		}
	}

	array<vector> GroupPositions(SCR_AIGroup group)
	{
		INF_PresenceBucket bucket = m_GroupPositions.Get(group);
		if (!bucket)
		{
			bucket = new INF_PresenceBucket();
			INF_BlockRules.CollectLiving(group, bucket.m_Positions);
			m_GroupPositions.Set(group, bucket);
		}
		return bucket.m_Positions;
	}

	bool Occupied(vector center)
	{
		foreach (vector player : m_Players)
		{
			if (INF_BlockRules.InCaptureArea(center, player))
				return true;
		}
		return false;
	}

	bool CanCapture(vector center)
	{
		bool grounded = false;
		foreach (vector player : m_Capturers)
		{
			if (INF_BlockRules.InCaptureArea(center, player))
				grounded = true;
		}
		if (!grounded)
			return false;
		int minimumX = INF_BuildingGrid.CellIndex(center[0] - 200);
		int maximumX = INF_BuildingGrid.CellIndex(center[0] + 200);
		int minimumZ = INF_BuildingGrid.CellIndex(center[2] - 200);
		int maximumZ = INF_BuildingGrid.CellIndex(center[2] + 200);
		for (int x = minimumX; x <= maximumX; x++)
		{
			for (int z = minimumZ; z <= maximumZ; z++)
			{
				INF_PresenceBucket bucket = m_EnemyCells.Get(x.ToString() + ":" + z.ToString());
				if (!bucket)
					continue;
				foreach (vector enemy : bucket.m_Positions)
				{
					if (vector.DistanceSqXZ(center, enemy) <= 40000)
						return false;
				}
			}
		}
		return true;
	}
}

class INF_ActivationPlayer
{
	int m_Id;
	vector m_Position;
	vector m_Direction;
	float m_Speed;
	bool m_Fast;
}

class INF_ActivationGroup
{
	ref array<ref INF_ActivationPlayer> m_Players = {};
	int m_Limit;
	int m_Active;
	bool m_FastOnly;

	float Distance(vector center)
	{
		float nearest = float.MAX;
		foreach (INF_ActivationPlayer player : m_Players)
			nearest = Math.Min(nearest, vector.DistanceXZ(center, player.m_Position));
		return nearest;
	}

	bool Allows(vector center, float radius)
	{
		foreach (INF_ActivationPlayer player : m_Players)
		{
			vector delta = center - player.m_Position;
			delta[1] = 0;
			if (delta.LengthSq() > radius * radius)
				continue;
			if (!player.m_Fast)
				return true;
			float along = vector.Dot(delta, player.m_Direction);
			float length = Math.Clamp(player.m_Speed * 8, 250, 600);
			if (along >= 100 && along <= length && delta.LengthSq() - along * along <= 40000)
				return true;
		}
		return false;
	}
}

class INF_ActivationPressure
{
	int m_Limit;
	float m_ObservedAt;
}

class INF_TravelState
{
	IEntity m_Character;
	bool m_Fast;
	float m_AboveSince = -1;
	float m_BelowSince = -1;
}

class INF_ActivationPolicy
{
	static const float GROUP_RADIUS = 200;
	static const float MAX_ALTITUDE = 50;
	static const float PRESSURE_HOLD_MS = 180000;
	static const float PRESSURE_STEP_MS = 60000;
	protected static ref map<int, ref INF_TravelState> s_FastPlayers = new map<int, ref INF_TravelState>();
	protected static ref map<int, ref INF_ActivationPressure> s_Pressure = new map<int, ref INF_ActivationPressure>();
	ref array<ref INF_ActivationGroup> m_Groups = {};

	static void Reset()
	{
		ResetTravel();
		s_Pressure.Clear();
	}

	static void ResetTravel()
	{
		s_FastPlayers.Clear();
	}

	static int LowerTier(int limit)
	{
		if (limit > 18)
			return 18;
		if (limit > 10)
			return 10;
		if (limit > 6)
			return 6;
		return 4;
	}

	static int RetainedLimit(INF_ActivationPressure pressure, float now)
	{
		int limit = pressure.m_Limit;
		float elapsed = now - pressure.m_ObservedAt - PRESSURE_HOLD_MS;
		while (elapsed >= PRESSURE_STEP_MS && limit > 4)
		{
			limit = LowerTier(limit);
			elapsed -= PRESSURE_STEP_MS;
		}
		return limit;
	}

	static int GroupLimit(INF_ActivationGroup group, float now)
	{
		int actual = ZoneLimit(group.m_Players.Count());
		int retained = actual;
		float observedAt = now;
		foreach (INF_ActivationPlayer member : group.m_Players)
		{
			if (!s_Pressure.Contains(member.m_Id))
				continue;
			INF_ActivationPressure previous = s_Pressure.Get(member.m_Id);
			int inherited = RetainedLimit(previous, now);
			if (inherited > retained || (inherited == retained && inherited > actual && previous.m_ObservedAt > observedAt))
			{
				retained = inherited;
				observedAt = previous.m_ObservedAt;
			}
		}
		foreach (INF_ActivationPlayer player : group.m_Players)
		{
			INF_ActivationPressure pressure = new INF_ActivationPressure();
			// Store the effective tier with its original remaining decay schedule.
			pressure.m_Limit = retained;
			pressure.m_ObservedAt = observedAt;
			if (retained > actual)
			{
				float elapsed = now - observedAt - PRESSURE_HOLD_MS;
				if (elapsed >= PRESSURE_STEP_MS)
					pressure.m_ObservedAt += Math.Floor(elapsed / PRESSURE_STEP_MS) * PRESSURE_STEP_MS;
			}
			s_Pressure.Set(player.m_Id, pressure);
		}
		return retained;
	}

	static bool IsFast(int playerId, float speed, float now, INS_AmbientPatrolManager patrols, IEntity character)
	{
		if (!patrols)
			return false;
		INF_TravelState state = s_FastPlayers.Get(playerId);
		if (!state || state.m_Character != character)
		{
			state = new INF_TravelState();
			state.m_Character = character;
			s_FastPlayers.Set(playerId, state);
		}
		if (!state.m_Fast)
		{
			state.m_BelowSince = -1;
			if (speed * 3.6 >= patrols.INF_GetTravelSetting(0))
			{
				if (state.m_AboveSince < 0)
					state.m_AboveSince = now;
				if (now - state.m_AboveSince >= patrols.INF_GetTravelSetting(2) * 1000)
				{
					state.m_Fast = true;
					state.m_AboveSince = -1;
				}
			}
			else
				state.m_AboveSince = -1;
		}
		else
		{
			state.m_AboveSince = -1;
			if (speed * 3.6 < patrols.INF_GetTravelSetting(1))
			{
				if (state.m_BelowSince < 0)
					state.m_BelowSince = now;
				if (now - state.m_BelowSince >= patrols.INF_GetTravelSetting(3) * 1000)
				{
					state.m_Fast = false;
					state.m_BelowSince = -1;
				}
			}
			else
				state.m_BelowSince = -1;
		}
		return state.m_Fast;
	}

	static int ZoneLimit(int players)
	{
		if (players <= 1)
			return 4;
		if (players == 2)
			return 6;
		if (players <= 4)
			return 10;
		if (players <= 8)
			return 18;
		return 32;
	}

	void Collect(IEntity owner, INF_BlockPresence presence = null)
	{
		m_Groups.Clear();
		INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(owner.FindComponent(INS_AmbientPatrolManager));
		array<ref INF_ActivationPlayer> pending = {};
		array<int> ids = {};
		if (presence)
			ids = presence.m_PlayerIDs;
		else
			GetGame().GetPlayerManager().GetPlayers(ids);
		for (int stale = s_FastPlayers.Count() - 1; stale >= 0; stale--)
		{
			if (ids.Find(s_FastPlayers.GetKey(stale)) < 0)
				s_FastPlayers.Remove(s_FastPlayers.GetKey(stale));
		}
		for (int disconnected = s_Pressure.Count() - 1; disconnected >= 0; disconnected--)
		{
			if (ids.Find(s_Pressure.GetKey(disconnected)) < 0)
				s_Pressure.Remove(s_Pressure.GetKey(disconnected));
		}
		foreach (int id : ids)
		{
			IEntity entity = GetGame().GetPlayerManager().GetPlayerControlledEntity(id);
			if (!ChimeraCharacter.Cast(entity))
			{
				s_FastPlayers.Remove(id);
				continue;
			}
			DamageManagerComponent damage = DamageManagerComponent.Cast(entity.FindComponent(DamageManagerComponent));
			if (!damage || damage.IsDestroyed())
			{
				s_FastPlayers.Remove(id);
				continue;
			}
			vector pos = entity.GetOrigin();
			if (pos[1] - GetGame().GetWorld().GetSurfaceY(pos[0], pos[2]) > MAX_ALTITUDE)
			{
				s_FastPlayers.Remove(id);
				continue;
			}
			IEntity motionEntity = CompartmentAccessComponent.GetVehicleIn(entity);
			if (!motionEntity)
				motionEntity = entity;
			vector velocity;
			Physics physics = motionEntity.GetPhysics();
			if (physics)
				velocity = physics.GetVelocity();
			velocity[1] = 0;
			INF_ActivationPlayer player = new INF_ActivationPlayer();
			player.m_Id = id;
			player.m_Position = pos;
			player.m_Speed = velocity.Length();
			player.m_Fast = IsFast(id, player.m_Speed, GetGame().GetWorld().GetWorldTime(), patrols, entity);
			if (player.m_Speed > 0)
				player.m_Direction = velocity / player.m_Speed;
			pending.Insert(player);
		}
		while (!pending.IsEmpty())
		{
			INF_ActivationGroup group = new INF_ActivationGroup();
			group.m_Players.Insert(pending[0]);
			pending.Remove(0);
			bool expanded = true;
			while (expanded)
			{
				expanded = false;
				for (int i = pending.Count() - 1; i >= 0; i--)
				{
					if (group.Distance(pending[i].m_Position) > GROUP_RADIUS)
						continue;
					group.m_Players.Insert(pending[i]);
					pending.Remove(i);
					expanded = true;
				}
			}
			group.m_FastOnly = true;
			foreach (INF_ActivationPlayer member : group.m_Players)
			{
				if (!member.m_Fast)
					group.m_FastOnly = false;
			}
			group.m_Limit = GroupLimit(group, GetGame().GetWorld().GetWorldTime());
			if (group.m_FastOnly)
				group.m_Limit = 2;
			m_Groups.Insert(group);
		}
		if (patrols)
			patrols.INF_CountActivationZones(this);
	}

	int GetGroup(vector center, float radius)
	{
		int nearest = -1;
		float distance = radius;
		for (int i = 0; i < m_Groups.Count(); i++)
		{
			float candidate = m_Groups[i].Distance(center);
			if (candidate <= distance)
			{
				nearest = i;
				distance = candidate;
			}
		}
		return nearest;
	}

	bool Allows(vector center, float radius)
	{
		int group = GetGroup(center, radius);
		return group >= 0 && m_Groups[group].Allows(center, radius);
	}

	void Count(vector center, float radius)
	{
		int group = GetGroup(center, radius);
		if (group >= 0)
			m_Groups[group].m_Active++;
	}
}

class INF_BlockRules
{
	static const int MAX_BLOCKS = 32;
	static const float EVICTION_PROTECTION_RADIUS = 200;
	static const float MIN_GRID_SPAWN_DISTANCE = 200;
	static const int DENSE_GRID_BUILDINGS = 4;
	static const float MIN_DENSE_GRID_SPAWN_DISTANCE = 100;
	static const float DESPAWN_DELAY_MS = 60000;
	static const float COUNTERATTACK_DESPAWN_RADIUS = 800;
	static const float CAPTURE_HOLD_MS = 15000;
	static const float PLAYER_CAPTURE_RADIUS = 100;

	static bool IsConcealed(vector position, array<vector> players, float height)
	{
		BaseWorld world = GetGame().GetWorld();
		foreach (vector player : players)
		{
			TraceParam trace = new TraceParam();
			trace.Start = position + Vector(0, height, 0);
			trace.End = player + Vector(0, 1.5, 0);
			trace.Flags = TraceFlags.WORLD;
			if (world.TraceMove(trace, null) >= 0.98)
				return false;
		}
		return true;
	}

	static bool InCaptureArea(vector center, vector player)
	{
		return vector.DistanceSqXZ(center, player) <= PLAYER_CAPTURE_RADIUS * PLAYER_CAPTURE_RADIUS;
	}

	static bool AdvanceCapture(bool eligible, float now, inout float since)
	{
		if (!eligible)
		{
			since = -1;
			return false;
		}
		if (since < 0)
			since = now;
		return now - since >= CAPTURE_HOLD_MS;
	}

	static bool SoldiersDistant(array<ref INS_PatrolAgent> patrols, array<vector> players, float distance, INF_BlockPresence presence = null)
	{
		foreach (INS_PatrolAgent patrol : patrols)
		{
			if (!patrol.m_Group)
				continue;
			array<vector> living = {};
			if (presence)
				living = presence.GroupPositions(patrol.m_Group);
			else
				CollectLiving(patrol.m_Group, living);
			foreach (vector enemy : living)
			{
				if (Nearest(enemy, players) <= distance)
					return false;
			}
		}
		return true;
	}

	static void CollectLiving(SCR_AIGroup group, array<vector> positions)
	{
		array<AIAgent> agents = {};
		group.GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			IEntity entity = agent.GetControlledEntity();
			if (!entity)
				continue;
			INF_PrisonerComponent prisoner = INF_PrisonerComponent.Find(entity);
			if (prisoner && prisoner.IsCaptured())
				continue;
			DamageManagerComponent damage = DamageManagerComponent.Cast(entity.FindComponent(DamageManagerComponent));
			if (damage && !damage.IsDestroyed())
				positions.Insert(entity.GetOrigin());
		}
	}

	static bool HasMembers(array<ref INS_PatrolAgent> patrols, INF_BlockPresence presence = null)
	{
		foreach (INS_PatrolAgent patrol : patrols)
		{
			if (!patrol.m_Group)
				continue;
			array<vector> living = {};
			if (presence)
				living = presence.GroupPositions(patrol.m_Group);
			else
				CollectLiving(patrol.m_Group, living);
			if (!living.IsEmpty())
				return true;
		}
		return false;
	}

	static float Nearest(vector center, array<vector> players)
	{
		float nearest = float.MAX;
		foreach (vector player : players)
			nearest = Math.Min(nearest, vector.DistanceXZ(center, player));
		return nearest;
	}

	static bool CapturerOccupied(vector center, INF_BlockPresence presence)
	{
		foreach (vector player : presence.m_Capturers)
		{
			if (InCaptureArea(center, player))
				return true;
		}
		return false;
	}

	static void SortActivationZones(array<INS_AmbientPatrolZoneState> zones, INF_BlockPresence presence)
	{
		SortZonesByDistance(zones, presence.m_Players);
		array<INS_AmbientPatrolZoneState> nearby = {};
		array<INS_AmbientPatrolZoneState> occupied = {};
		foreach (INS_AmbientPatrolZoneState state : zones)
		{
			if (CapturerOccupied(state.m_vCenter, presence))
				occupied.Insert(state);
			else
				nearby.Insert(state);
		}
		zones.Clear();
		zones.Copy(occupied);
		foreach (INS_AmbientPatrolZoneState other : nearby)
			zones.Insert(other);
	}

	static void SortZonesByDistance(array<INS_AmbientPatrolZoneState> zones, array<vector> players, bool descending = false)
	{
		int count = zones.Count();
		array<float> distances = {};
		foreach (INS_AmbientPatrolZoneState state : zones)
			distances.Insert(Nearest(state.m_vCenter, players));
		array<INS_AmbientPatrolZoneState> merged = {};
		array<float> mergedDistances = {};
		merged.Resize(count);
		mergedDistances.Resize(count);
		// Stable bottom-up merge sort keeps registration order for equal distances.
		for (int width = 1; width < count; width *= 2)
		{
			for (int start = 0; start < count; start += width * 2)
			{
				int middle = Math.Min(start + width, count);
				int end = Math.Min(start + width * 2, count);
				int left = start;
				int right = middle;
				for (int output = start; output < end; output++)
				{
					bool takeLeft = right >= end;
					if (left < middle && right < end)
					{
						if (descending)
							takeLeft = distances[left] >= distances[right];
						else
							takeLeft = distances[left] <= distances[right];
					}
					if (left < middle && takeLeft)
					{
						merged[output] = zones[left];
						mergedDistances[output] = distances[left++];
					}
					else
					{
						merged[output] = zones[right];
						mergedDistances[output] = distances[right++];
					}
				}
			}
			zones.Copy(merged);
			distances.Copy(mergedDistances);
		}
	}

	static bool IsDistant(vector center, array<ref INS_PatrolAgent> patrols, array<vector> players, float distance, INF_BlockPresence presence = null)
	{
		if (Nearest(center, players) <= distance)
			return false;
		return SoldiersDistant(patrols, players, distance, presence);
	}
}



modded class INS_PatrolAgent
{
	bool m_bINFInbound;
	float m_fINFInboundDeadline = -1;
}








modded class INS_FieldThreatManager
{
	bool INF_BindPrisonerCleanup(AIGroup group, INF_PrisonerComponent prisoner)
	{
		foreach (INS_FieldThreatEntry entry : m_aEntries)
		{
			if (group && entry.m_Patrol && entry.m_Patrol.m_Group == group)
			{
				prisoner.INF_SetCleanupRules(m_fAbandonRange, 0, false);
				return true;
			}
		}
		return false;
	}

	void INF_CollectGroups(array<SCR_AIGroup> groups)
	{
		foreach (INS_FieldThreatEntry entry : m_aEntries)
		{
			if (entry.m_Patrol)
				groups.Insert(entry.m_Patrol.m_Group);
		}
	}
}
