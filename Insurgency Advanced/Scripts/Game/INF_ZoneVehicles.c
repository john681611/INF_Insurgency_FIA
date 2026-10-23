modded class INS_AmbientVehicleAgent
{
	bool m_bINFPassengersStarted;
	bool m_bINFPassengersReady;
	int m_iINFPassengers;
	int m_iINFSpawnedCrew;
	ref array<IEntity> m_aINFMembers = {};
	SCR_BaseCompartmentManagerComponent m_INFCompartmentManager;
	INS_AmbientVehicleManager m_INFPassengerManager;

	void INF_StopCrewListener()
	{
		if (m_INFCompartmentManager)
			m_INFCompartmentManager.GetOnDoneSpawningDefaultOccupants().Remove(INF_OnCrewSpawned);
		m_INFCompartmentManager = null;
		m_INFPassengerManager = null;
	}

	void INF_OnCrewSpawned(SCR_BaseCompartmentManagerComponent compartments, array<IEntity> crew, bool cancelled)
	{
		INS_AmbientVehicleManager manager = m_INFPassengerManager;
		INF_StopCrewListener();
		if (crew)
		{
			m_iINFSpawnedCrew = crew.Count();
			foreach (IEntity member : crew)
			{
				if (member && !m_aINFMembers.Contains(member))
					m_aINFMembers.Insert(member);
			}
		}
		if (manager)
			manager.INF_SpawnPassengers(this, cancelled);
	}
}

class INF_ZoneVehicle
{
	ref INS_AmbientVehicleAgent m_Agent;
	INS_AmbientPatrolZoneState m_Zone;
	ref array<IEntity> m_Crew = {};
	ref array<SCR_AIGroup> m_Groups = {};
	int m_ExpectedCrew;
	float m_Deadline;
	float m_NextCleanupAt;
	bool m_Ready;
}

modded class INS_AmbientVehicleManager
{
	[Attribute("15", UIWidgets.Slider, "Chance per eligible zone per mission. Zero prevents new zone vehicles.", "0 100 1", category: "Zone Vehicles"), RplProp()]
	protected int m_iINFZoneVehicleChance = 15;
	[Attribute("2", UIWidgets.Slider, "Zone vehicle cap, independent of ambient route vehicles. Lowering affects future admissions.", "0 32 1", category: "Zone Vehicles"), RplProp()]
	protected int m_iINFZoneVehicleLimit = 2;
	protected ref array<ref INF_ZoneVehicle> m_INFZoneVehicles = {};
	protected ref map<string, bool> m_INFZoneVehicleRolls = new map<string, bool>();
	protected bool m_INFZoneVehiclesStopped;
	protected float m_fINFZonePauseUntil;
	protected bool m_bINFZoneVehicleTickQueued;
	protected ref array<vector> m_INFRejectedVehiclePositions = {};
	protected ref map<string, float> m_INFVehicleSearchRetry = new map<string, float>();

	void INF_ScheduleZoneVehicles()
	{
		if (!Replication.IsServer() || m_INFZoneVehiclesStopped || m_bINFZoneVehicleTickQueued
			|| !INF_ZoneEditorContext.Grid(GetOwner()))
			return;
		m_bINFZoneVehicleTickQueued = true;
		GetGame().GetCallqueue().CallLater(INF_ZoneVehicleTick, 5000, false);
	}

	bool INF_IsTrackedAmbientVehicle(INS_AmbientVehicleAgent agent)
	{
		return m_aActiveVehicles.Contains(agent);
	}

	protected bool INF_OwnsPrisoner(INS_AmbientVehicleAgent agent, AIGroup group, INF_PrisonerComponent prisoner)
	{
		if ((group && agent.m_CrewGroup == group) || agent.m_aINFMembers.Contains(prisoner.GetOwner()))
			return true;
		if (!agent.m_VehicleEntity)
			return false;
		SCR_BaseCompartmentManagerComponent compartments = SCR_BaseCompartmentManagerComponent.Cast(
			agent.m_VehicleEntity.FindComponent(SCR_BaseCompartmentManagerComponent));
		array<IEntity> occupants = {};
		if (compartments)
			compartments.GetOccupants(occupants);
		return occupants.Contains(prisoner.GetOwner());
	}

	bool INF_BindPrisonerCleanup(AIGroup group, INF_PrisonerComponent prisoner)
	{
		foreach (INF_ZoneVehicle unit : m_INFZoneVehicles)
		{
			if (INF_OwnsPrisoner(unit.m_Agent, group, prisoner))
			{
				prisoner.INF_SetCleanupRules(m_fDeactivationRadius, 0, true);
				return true;
			}
		}
		foreach (INS_AmbientVehicleAgent agent : m_aActiveVehicles)
		{
			if (INF_OwnsPrisoner(agent, group, prisoner))
			{
				prisoner.INF_SetCleanupRules(m_fDeactivationRadius, 0, false);
				prisoner.INF_SetVehicleGraceSource(agent);
				return true;
			}
		}
		return false;
	}

	int INF_GetZoneVehicleSetting(int setting)
	{
		if (setting == 0)
			return m_iINFZoneVehicleChance;
		if (setting == 1)
			return m_iINFZoneVehicleLimit;
		Print("[INF_ZoneVehicles] Unknown zone vehicle setting.", LogLevel.ERROR);
		return 0;
	}

	void INF_SetZoneVehicleSetting(int setting, int value)
	{
		if (!Replication.IsServer())
			return;
		int maximum = 100;
		if (setting == 1)
			maximum = 32;
		if (setting < 0 || setting > 1 || value < 0 || value > maximum)
		{
			Print("[INF_ZoneVehicles] Invalid zone vehicle setting.", LogLevel.ERROR);
			return;
		}
		if (setting == 0)
			m_iINFZoneVehicleChance = value;
		else
			m_iINFZoneVehicleLimit = value;
		Replication.BumpMe();
		INF_ScheduleZoneVehicles();
	}

	override void OnGameModeStart()
	{
		super.OnGameModeStart();
		if (Replication.IsServer() && INF_ZoneEditorContext.Grid(GetOwner()))
			INF_ScheduleZoneVehicles();
	}

	override void OnGameModeEnd(SCR_GameModeEndData data)
	{
		m_INFZoneVehiclesStopped = true;
		GetGame().GetCallqueue().Remove(INF_ZoneVehicleTick);
		GetGame().GetCallqueue().Remove(INF_ZoneSpawnCrew);
		GetGame().GetCallqueue().Remove(INF_ZoneWreckCheck);
		GetGame().GetCallqueue().Remove(WreckPlayerExitPoll);
		m_CallQueue.Remove(WreckCleanupCheck);
		m_CallQueue.Remove(WreckPlayerExitPoll);
		foreach (INS_AmbientVehicleAgent ambient : m_aActiveVehicles)
			ambient.INF_StopCrewListener();
		if (Replication.IsServer())
		{
			for (int i = m_INFZoneVehicles.Count() - 1; i >= 0; i--)
				INF_RemoveZoneVehicle(m_INFZoneVehicles[i], true);
		}
		super.OnGameModeEnd(data);
	}

	override void OnPlayerConnected(int playerId)
	{
		super.OnPlayerConnected(playerId);
		if (Replication.IsServer())
		{
			m_fINFZonePauseUntil = GetGame().GetWorld().GetWorldTime() + 15000;
			INF_ScheduleZoneVehicles();
		}
	}

	override protected void SpawnVehicle(vector spawnPos, vector destPos, int routeIndex, bool forward)
	{
		if (!Replication.IsServer() || m_INFZoneVehiclesStopped)
			return;
		if (INF_ZoneEditorContext.Grid(GetOwner()))
		{
			string key = spawnPos.ToString();
			float retryAt;
			float now = GetGame().GetWorld().GetWorldTime();
			if (m_INFVehicleSearchRetry.Find(key, retryAt) && now < retryAt)
				return;
			bool blocked;
			foreach (vector rejected : m_INFRejectedVehiclePositions)
			{
				if (vector.DistanceSqXZ(spawnPos, rejected) <= 25)
					blocked = true;
			}
			if (blocked || !INF_VehiclePositionClear(spawnPos, destPos - spawnPos, null))
			{
				if (!blocked)
					m_INFRejectedVehiclePositions.Insert(spawnPos);
				vector replacement;
				if (!INF_FindAmbientRoadPosition(spawnPos, destPos, replacement))
				{
					m_INFVehicleSearchRetry.Set(key, now + 60000);
					return;
				}
				spawnPos = replacement;
			}
		}
		super.SpawnVehicle(spawnPos, destPos, routeIndex, forward);
	}

	protected bool INF_FindAmbientRoadPosition(vector origin, vector destination, out vector position)
	{
		array<int> playerIds = {};
		array<vector> players = {};
		GetGame().GetPlayerManager().GetPlayers(playerIds);
		foreach (int playerId : playerIds)
		{
			IEntity controlled = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
			if (controlled)
				players.Insert(controlled.GetOrigin());
		}
		if (players.IsEmpty())
			return false;
		for (int sample = 0; sample < 9; sample++)
		{
			vector center = origin;
			if (sample > 0)
			{
				float angle = (sample - 1) * Math.PI / 4;
				center += Vector(Math.Sin(angle) * 100, 0, Math.Cos(angle) * 100);
			}
			float yaw;
			vector candidate;
			if (!INF_ZoneRoadPosition(center, players, candidate, yaw))
				continue;
			if (vector.DistanceSqXZ(candidate, origin) > 40000
				|| !INF_VehiclePositionClear(candidate, destination - candidate, null))
				continue;
			position = candidate;
			return true;
		}
		return false;
	}

	override protected void SpawnCrew(INS_AmbientVehicleAgent agent)
	{
		if (!Replication.IsServer() || m_INFZoneVehiclesStopped
			|| !agent.m_bActive || !agent.m_VehicleEntity || agent.m_bINFPassengersStarted)
			return;
		if (INF_ZoneEditorContext.Grid(GetOwner()) && !HasPlayerOccupant(agent.m_VehicleEntity)
			&& !INF_VehicleDeploymentClear(agent.m_VehicleEntity))
		{
			foreach (INF_ZoneVehicle unit : m_INFZoneVehicles)
			{
				if (unit.m_Agent == agent)
				{
					INF_RemoveZoneVehicle(unit, true);
					return;
				}
			}
			m_INFRejectedVehiclePositions.Insert(agent.m_VehicleEntity.GetOrigin());
			DespawnVehicle(agent);
			return;
		}
		agent.m_bINFPassengersStarted = true;
		agent.m_INFCompartmentManager = SCR_BaseCompartmentManagerComponent.Cast(
			agent.m_VehicleEntity.FindComponent(SCR_BaseCompartmentManagerComponent));
		if (agent.m_INFCompartmentManager)
		{
			agent.m_INFPassengerManager = this;
			agent.m_INFCompartmentManager.GetOnDoneSpawningDefaultOccupants().Insert(agent.INF_OnCrewSpawned);
		}
		super.SpawnCrew(agent);
	}

	protected void INF_ExcludeVehicle(IEntity entity, array<IEntity> exclusions)
	{
		exclusions.Insert(entity);
		IEntity child = entity.GetChildren();
		while (child)
		{
			INF_ExcludeVehicle(child, exclusions);
			child = child.GetSibling();
		}
	}

	protected bool INF_VehicleDeploymentClear(IEntity vehicle)
	{
		array<IEntity> exclusions = {};
		INF_ExcludeVehicle(vehicle, exclusions);
		vector transform[4];
		vehicle.GetTransform(transform);
		return INF_VehiclePositionClear(vehicle.GetOrigin(), transform[2], exclusions);
	}

	protected bool INF_VehiclePositionClear(vector pos, vector forward, array<IEntity> exclusions, bool footprintChecked = false)
	{
		pos[1] = GetGame().GetWorld().GetSurfaceY(pos[0], pos[2]);
		if (!footprintChecked && !INF_ZoneVehicleFootprintClear(pos, exclusions))
			return false;
		forward[1] = 0;
		forward.Normalize();
		if (forward.LengthSq() < 0.5)
			return false;
		if (!INF_VehicleExitClear(pos, forward, exclusions, 2)
			|| !INF_VehicleExitClear(pos, -forward, exclusions, 2))
			return false;
		return INF_VehicleExitClear(pos, forward, exclusions, 3, 3)
			|| INF_VehicleExitClear(pos, -forward, exclusions, 3, 3);
	}

	protected bool INF_VehicleExitClear(vector pos, vector direction, array<IEntity> exclusions, int steps = 3, int firstStep = 1)
	{
		BaseWorld world = GetGame().GetWorld();
		vector side = Vector(direction[2], 0, -direction[0]);
		for (int step = firstStep; step <= steps; step++)
		{
			float distance = step * 5.0;
			vector point = pos + direction * distance;
			float ground = world.GetSurfaceY(point[0], point[2]);
			if (Math.AbsFloat(ground - pos[1]) > step * 1.5)
				return false;
			for (int lane = -2; lane <= 2; lane++)
			{
				float laneOffset = lane;
				vector lateralOffset = side * laneOffset;
				for (int level = 0; level < 3; level++)
				{
					TraceParam trace = new TraceParam();
					trace.Start = pos + lateralOffset + Vector(0, 0.5 + level, 0);
					vector end = point + lateralOffset;
					end[1] = ground + 0.5 + level;
					trace.End = end;
					trace.Flags = TraceFlags.ENTS | TraceFlags.WORLD | TraceFlags.OCEAN;
					trace.ExcludeArray = exclusions;
					if (world.TraceMove(trace, null) < 1)
						return false;
				}
			}
		}
		return true;
	}

	void INF_SpawnPassengers(INS_AmbientVehicleAgent agent, bool cancelled)
	{
		if (!Replication.IsServer() || m_INFZoneVehiclesStopped || !agent.m_bActive || !agent.m_VehicleEntity)
			return;
		if (cancelled)
		{
			Print("[INF_VehiclePassengers] Native crew spawning was cancelled; passengers were not spawned.", LogLevel.WARNING);
			return;
		}
		SCR_BaseCompartmentManagerComponent compartments = SCR_BaseCompartmentManagerComponent.Cast(
			agent.m_VehicleEntity.FindComponent(SCR_BaseCompartmentManagerComponent));
		if (!compartments)
		{
			Print("[INF_VehiclePassengers] Vehicle is missing its compartment manager.", LogLevel.ERROR);
			return;
		}
		if (HasPlayerOccupant(agent.m_VehicleEntity))
		{
			Print("[INF_VehiclePassengers] Skipping passengers: a player has taken the vehicle.", LogLevel.WARNING);
			agent.m_bINFPassengersReady = true;
			return;
		}
		AIGroup group;
		bool livingCrew;
		foreach (IEntity occupant : agent.m_aINFMembers)
		{
			if (!occupant)
				continue;
			livingCrew = livingCrew || INF_ZoneHostileCrew(occupant);
			AIControlComponent control = AIControlComponent.Cast(occupant.FindComponent(AIControlComponent));
			if (control && control.GetAIAgent())
				group = control.GetAIAgent().GetParentGroup();
			if (group)
				break;
		}
		if (!group)
		{
			if (!livingCrew && agent.m_iINFSpawnedCrew > 0)
			{
				agent.m_bINFPassengersReady = true;
				return;
			}
			Print("[INF_VehiclePassengers] Native crew completed without a usable AI group.", LogLevel.ERROR);
			return;
		}
		array<BaseCompartmentSlot> slots = {};
		array<BaseCompartmentSlot> eligible = {};
		compartments.GetCompartmentsOfTypes(slots, SCR_BaseCompartmentManagerComponent.PASSENGER_COMPARTMENT_TYPES);
		foreach (BaseCompartmentSlot slot : slots)
		{
			if (slot && !slot.IsOccupied() && slot.IsCompartmentAccessible()
				&& !slot.GetDefaultOccupantPrefab().IsEmpty())
				eligible.Insert(slot);
		}
		int count = Math.RandomInt(0, eligible.Count() + 1);
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		if (persistence)
		{
			foreach (IEntity nativeCrew : agent.m_aINFMembers)
			{
				if (nativeCrew)
					persistence.StopTracking(nativeCrew, true);
			}
		}
		for (int i = 0; i < count; i++)
		{
			int index = Math.RandomInt(0, eligible.Count());
			BaseCompartmentSlot selected = eligible[index];
			eligible.Remove(index);
			IEntity passenger = selected.SpawnDefaultCharacterInCompartment(group);
			if (!passenger)
			{
				Print("[INF_VehiclePassengers] Failed to spawn a selected seat's default passenger.", LogLevel.ERROR);
				continue;
			}
			if (persistence)
				persistence.StopTracking(passenger, true);
			agent.m_aINFMembers.Insert(passenger);
			agent.m_iINFPassengers++;
		}
		if (persistence)
			persistence.StopTracking(group, true);
		agent.m_bINFPassengersReady = true;
	}

	void INF_CollectGroups(array<SCR_AIGroup> groups)
	{
		foreach (INS_AmbientVehicleAgent vehicle : m_aActiveVehicles)
			groups.Insert(vehicle.m_CrewGroup);
		foreach (INF_ZoneVehicle unit : m_INFZoneVehicles)
		{
			foreach (SCR_AIGroup group : unit.m_Groups)
			{
				if (group && !groups.Contains(group))
					groups.Insert(group);
			}
		}
	}

	protected bool INF_ZoneHostileCrew(IEntity entity)
	{
		if (!entity || GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(entity) > 0)
			return false;
		INF_PrisonerComponent prisoner = INF_PrisonerComponent.Find(entity);
		if (prisoner && prisoner.IsCaptured())
			return false;
		DamageManagerComponent damage = DamageManagerComponent.Cast(entity.FindComponent(DamageManagerComponent));
		return damage && !damage.IsDestroyed();
	}

	protected int INF_ZoneCrewCount(INF_ZoneVehicle unit)
	{
		int count;
		foreach (IEntity member : unit.m_Crew)
		{
			if (INF_ZoneHostileCrew(member))
				count++;
		}
		return count;
	}

	protected bool INF_ZoneHidden(vector pos, array<vector> players)
	{
		return INF_BlockRules.IsConcealed(pos, players, 3);
	}

	protected bool INF_ZoneRoadPosition(vector center, array<vector> players, out vector pos, out float yaw, float minimumDistance = -1)
	{
		if (minimumDistance < 0)
			minimumDistance = m_fMinSpawnDistance;
		ChimeraAIWorld ai = ChimeraAIWorld.Cast(GetGame().GetAIWorld());
		if (!ai || !ai.GetRoadNetworkManager())
		{
			return false;
		}
		BaseRoad road;
		float distance;
		ai.GetRoadNetworkManager().GetClosestRoad(center, road, distance);
		if (!road || distance > 100)
		{
			return false;
		}
		array<vector> points = {};
		road.GetPoints(points);
		if (points.Count() < 2)
		{
			return false;
		}
		for (int i = 0; i < points.Count(); i++)
		{
			vector point = points[i];
			float nearest = INF_BlockRules.Nearest(point, players);
			if (vector.DistanceSqXZ(point, center) > 10000
				|| nearest < minimumDistance || nearest > m_fActivationRadius)
			{
				continue;
			}
			point[1] = GetGame().GetWorld().GetSurfaceY(point[0], point[2]);
			vector empty;
			if (!SCR_WorldTools.FindEmptyTerrainPosition(empty, point, 5.1, 5, 4))
			{
				continue;
			}
			if (vector.DistanceSqXZ(empty, point) > 0.0625)
			{
				continue;
			}
			point = empty;
			nearest = INF_BlockRules.Nearest(point, players);
			if (vector.DistanceSqXZ(point, center) > 10000
				|| nearest < minimumDistance || nearest > m_fActivationRadius)
			{
				continue;
			}
			if (INF_BuildingGrid.IsExcluded(point))
			{
				continue;
			}
			if (!INF_ZoneHidden(point, players))
			{
				continue;
			}
			if (!INF_ForestCamps.ClearVolume(point, 5, 4))
			{
				continue;
			}
			if (INF_VehiclePositionOccupied(point))
			{
				continue;
			}
			int next = i + 1;
			if (next >= points.Count())
				next = i - 1;
			if (vector.DistanceSqXZ(points[i], points[next]) < 1)
			{
				continue;
			}
			if (!INF_VehiclePositionClear(point, vector.Direction(points[i], points[next]), null))
			{
				continue;
			}
			pos = point;
			yaw = vector.Direction(points[i], points[next]).ToYaw();
			return true;
		}
		return false;
	}

	protected bool INF_VehiclePositionOccupied(vector point)
	{
		foreach (vector rejected : m_INFRejectedVehiclePositions)
		{
			if (vector.DistanceSqXZ(point, rejected) <= 25)
				return true;
		}
		foreach (INS_AmbientVehicleAgent ambient : m_aActiveVehicles)
		{
			if (ambient.m_VehicleEntity && vector.DistanceSqXZ(point, ambient.m_VehicleEntity.GetOrigin()) < 2500)
				return true;
		}
		foreach (INF_ZoneVehicle unit : m_INFZoneVehicles)
		{
			if (unit.m_Agent.m_VehicleEntity && vector.DistanceSqXZ(point, unit.m_Agent.m_VehicleEntity.GetOrigin()) < 2500)
				return true;
		}
		return false;
	}

	protected bool INF_ZoneOpenPosition(vector center, array<vector> players, out vector pos, out float yaw, float minimumDistance)
	{
		float extent = INF_BuildingGrid.SIZE * 0.5 - 6;
		for (int attempt = 0; attempt < 32; attempt++)
		{
			vector point = center + Vector(Math.RandomFloat(-extent, extent), 0, Math.RandomFloat(-extent, extent));
			point[1] = GetGame().GetWorld().GetSurfaceY(point[0], point[2]);
			float nearest = INF_BlockRules.Nearest(point, players);
			if (!INF_BuildingGrid.Contains(center, point)
				|| nearest < minimumDistance || nearest > m_fActivationRadius)
			{
				continue;
			}
			if (point[1] <= 0.5 || INF_BuildingGrid.IsExcluded(point)
				|| INF_VehiclePositionOccupied(point) || !INF_ZoneHidden(point, players)
				|| !INF_ForestCamps.ClearVolume(point, 5, 4))
			{
				continue;
			}
			float initialYaw = Math.RandomFloat(0, 360);
			if (!INF_ZoneVehicleFootprintClear(point))
				continue;
			for (int heading = 0; heading < 4; heading++)
			{
				float candidateYaw = initialYaw + heading * 45;
				float angle = candidateYaw * Math.DEG2RAD;
				vector direction = Vector(Math.Sin(angle), 0, Math.Cos(angle));
				if (!INF_VehiclePositionClear(point, direction, null, true))
					continue;
				pos = point;
				yaw = candidateYaw;
				return true;
			}
		}
		return false;
	}

	protected bool INF_ZoneVehicleFootprintClear(vector pos, array<IEntity> exclusions = null)
	{
		BaseWorld world = GetGame().GetWorld();
		for (int x = -5; x <= 5; x++)
		{
			for (int z = -5; z <= 5; z++)
			{
				float ground = world.GetSurfaceY(pos[0] + x, pos[2] + z);
				if (Math.AbsFloat(ground - pos[1]) > 0.65)
					return false;
			}
		}
		for (int level = 0; level < 4; level++)
		{
			float height = 0.25 + level;
			for (int offset = -5; offset <= 5; offset++)
			{
				TraceParam across = new TraceParam();
				across.Start = pos + Vector(-5, height, offset);
				across.End = pos + Vector(5, height, offset);
				across.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
				across.ExcludeArray = exclusions;
				if (world.TraceMove(across, null) < 1)
					return false;
				TraceParam along = new TraceParam();
				along.Start = pos + Vector(offset, height, -5);
				along.End = pos + Vector(offset, height, 5);
				along.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
				along.ExcludeArray = exclusions;
				if (world.TraceMove(along, null) < 1)
					return false;
			}
		}
		return true;
	}

	protected bool INF_ZoneVehicleReinforcement(vector center, array<vector> players, float minimumDistance,
		out vector pos, out float yaw, out vector destination)
	{
		bool targetFound;
		for (int targetAttempt = 0; targetAttempt < 16; targetAttempt++)
		{
			destination = center + Vector(Math.RandomFloat(-40, 40), 0, Math.RandomFloat(-40, 40));
			destination[1] = GetGame().GetWorld().GetSurfaceY(destination[0], destination[2]);
			if (destination[1] > 0.5 && !INF_BuildingGrid.IsExcluded(destination)
				&& !INF_VehiclePositionOccupied(destination)
				&& INF_ZoneVehicleFootprintClear(destination))
			{
				targetFound = true;
				break;
			}
		}
		if (!targetFound)
		{
			return false;
		}
		for (int attempt = 0; attempt < 16; attempt++)
		{
			float angle = Math.RandomFloat(0, Math.PI * 2);
			float distance = Math.RandomFloat(minimumDistance + 50, minimumDistance + 300);
			if (attempt >= 8)
				distance = Math.RandomFloat(550, 900);
			vector search = center + Vector(Math.Sin(angle) * distance, 0, Math.Cos(angle) * distance);
			if (!INF_ZoneRoadPosition(search, players, pos, yaw, minimumDistance))
				continue;
			float fromZone = vector.DistanceXZ(pos, center);
			if (INF_BuildingGrid.Contains(center, pos) || fromZone > 900
				|| fromZone < minimumDistance + 50
				|| !INF_VehiclePositionClear(pos, destination - pos, null))
				continue;
			yaw = vector.Direction(pos, destination).ToYaw();
			return true;
		}
		return false;
	}

	protected void INF_TryZoneVehicle(INS_AmbientPatrolZoneState zone, array<vector> players)
	{
		vector pos;
		float yaw;
		vector destination;
		float minimumDistance = INF_BlockRules.MIN_GRID_SPAWN_DISTANCE;
		if (zone.m_iINFBuildings >= INF_BlockRules.DENSE_GRID_BUILDINGS)
			minimumDistance = INF_BlockRules.MIN_DENSE_GRID_SPAWN_DISTANCE;
		if (!INF_ZoneRoadPosition(zone.m_vCenter, players, pos, yaw, minimumDistance)
			&& !INF_ZoneOpenPosition(zone.m_vCenter, players, pos, yaw, minimumDistance))
		{
			if (!INF_ZoneVehicleReinforcement(zone.m_vCenter, players, minimumDistance, pos, yaw, destination))
				return;
		}
		else
			destination = pos;
		ResourceName prefab = m_aVehiclePrefabs[Math.RandomInt(0, m_aVehiclePrefabs.Count())];
		Resource resource = Resource.Load(prefab);
		if (!resource || !resource.IsValid())
		{
			Print("[INF_ZoneVehicles] Cannot load configured vehicle: " + prefab, LogLevel.ERROR);
			return;
		}
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		Math3D.AnglesToMatrix(Vector(yaw, 0, 0), params.Transform);
		params.Transform[3] = pos;
		IEntity entity = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params);
		if (!entity)
		{
			Print("[INF_ZoneVehicles] Vehicle spawn failed.", LogLevel.ERROR);
			return;
		}
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		if (persistence)
			persistence.StopTracking(entity, true);
		SCR_BaseCompartmentManagerComponent compartments = SCR_BaseCompartmentManagerComponent.Cast(entity.FindComponent(SCR_BaseCompartmentManagerComponent));
		array<BaseCompartmentSlot> slots = {};
		if (compartments)
			compartments.GetCompartmentsOfTypes(slots, SCR_BaseCompartmentManagerComponent.CREW_COMPARTMENT_TYPES);
		int crew;
		foreach (BaseCompartmentSlot slot : slots)
		{
			if (slot && !slot.IsOccupied() && slot.IsCompartmentAccessible() && !slot.GetDefaultOccupantPrefab().IsEmpty())
				crew++;
		}
		if (crew == 0)
		{
			Print("[INF_ZoneVehicles] Vehicle has no configured default crew: " + prefab, LogLevel.ERROR);
			SCR_EntityHelper.DeleteEntityAndChildren(entity);
			return;
		}
		INF_ZoneVehicle unit = new INF_ZoneVehicle();
		unit.m_Agent = new INS_AmbientVehicleAgent(entity, destination, -1, false);
		unit.m_Zone = zone;
		unit.m_ExpectedCrew = crew;
		unit.m_Deadline = GetGame().GetWorld().GetWorldTime() + 15000;
		zone.m_fINFSpawnReadyAt = Math.Max(zone.m_fINFSpawnReadyAt, unit.m_Deadline);
		m_INFZoneVehicles.Insert(unit);
		GetGame().GetCallqueue().CallLater(INF_ZoneSpawnCrew, 500, false, unit);
		INS_AmbientPatrolManager patrols = INF_ZoneEditorContext.Grid(GetOwner());
		if (patrols && patrols.INF_GetZoneStates())
			patrols.INF_GetZoneStates().InvalidatePresence();
	}

	protected void INF_ZoneSpawnCrew(INF_ZoneVehicle unit)
	{
		if (!Replication.IsServer() || m_INFZoneVehiclesStopped || !unit.m_Agent.m_bActive)
			return;
		if (!unit.m_Zone.m_bActive || unit.m_Zone.m_bINFMarkedCleared
			|| HasPlayerOccupant(unit.m_Agent.m_VehicleEntity))
		{
			INF_RemoveZoneVehicle(unit, true);
			return;
		}
		SpawnCrew(unit.m_Agent);
	}

	protected bool INF_ResolveZoneCrew(INF_ZoneVehicle unit)
	{
		if (!unit.m_Agent.m_bINFPassengersReady || unit.m_Agent.m_iINFSpawnedCrew < unit.m_ExpectedCrew)
			return false;
		foreach (IEntity occupant : unit.m_Agent.m_aINFMembers)
		{
			if (occupant && !unit.m_Crew.Contains(occupant))
				unit.m_Crew.Insert(occupant);
		}
		unit.m_Groups.Clear();
		bool resolved = true;
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		foreach (IEntity member : unit.m_Crew)
		{
			if (!member)
				continue;
			if (persistence)
				persistence.StopTracking(member, true);
			AIControlComponent control = AIControlComponent.Cast(member.FindComponent(AIControlComponent));
			if (!INF_ZoneHostileCrew(member))
				continue;
			if (!control)
			{
				resolved = false;
				continue;
			}
			AIAgent agent = control.GetAIAgent();
			SCR_AIGroup group;
			if (agent)
				group = SCR_AIGroup.Cast(agent.GetParentGroup());
			if (!group)
				resolved = false;
			else if (!unit.m_Groups.Contains(group))
			{
				unit.m_Groups.Insert(group);
				if (persistence)
					persistence.StopTracking(group, true);
			}
		}
		if (INF_ZoneCrewCount(unit) == 0)
		{
			unit.m_Ready = true;
			return true;
		}
		if (!resolved || unit.m_Groups.IsEmpty())
			return false;
		unit.m_Agent.m_CrewGroup = unit.m_Groups[0];
		unit.m_Agent.m_bEverHadCrew = true;
		// Local vehicles hold position; reinforcements drive to their checked in-zone destination.
		// Infantry Defend waypoints can instead make them dismount and seek cover.
		AssignDriveWaypoint(unit.m_Agent);
		if (!unit.m_Agent.m_bActive || !unit.m_Agent.m_CurrentWaypoint)
			return false;
		unit.m_Ready = true;
		unit.m_Zone.m_bEverHadLiving = true;
		INS_AmbientPatrolManager patrols = INF_ZoneEditorContext.Grid(GetOwner());
		if (patrols && patrols.INF_GetZoneStates())
		{
			patrols.INF_GetZoneStates().InvalidatePresence();
			patrols.INF_GetZoneStates().NotifyChanged(unit.m_Zone);
		}
		return true;
	}

	protected void INF_CleanupVehicle(INS_AmbientVehicleAgent agent, array<IEntity> members, bool removeVehicle)
	{
		agent.m_bActive = false;
		agent.INF_StopCrewListener();
		if (agent.m_CurrentWaypoint)
		{
			AIWaypoint waypoint = AIWaypoint.Cast(agent.m_CurrentWaypoint);
			if (waypoint && agent.m_CrewGroup)
				agent.m_CrewGroup.RemoveWaypoint(waypoint);
			SCR_EntityHelper.DeleteEntityAndChildren(agent.m_CurrentWaypoint);
			agent.m_CurrentWaypoint = null;
		}
		foreach (IEntity tracked : agent.m_aINFMembers)
		{
			if (tracked && !members.Contains(tracked))
				members.Insert(tracked);
		}
		IEntity vehicle = agent.m_VehicleEntity;
		bool playerAboard = HasPlayerOccupant(vehicle);
		bool preserve = playerAboard;
		if (vehicle)
		{
			SCR_BaseCompartmentManagerComponent compartments = SCR_BaseCompartmentManagerComponent.Cast(vehicle.FindComponent(SCR_BaseCompartmentManagerComponent));
			array<IEntity> occupants = {};
			if (compartments)
			{
				compartments.CancelSpawningCharacters();
				compartments.GetOccupants(occupants);
			}
			foreach (IEntity occupant : occupants)
			{
				if (INF_PrisonerComponent.INF_Preserve(occupant))
					preserve = true;
				if (!members.Contains(occupant))
					members.Insert(occupant);
			}
		}
		if (removeVehicle && !playerAboard)
		{
			foreach (IEntity member : members)
			{
				if (member && !INF_PrisonerComponent.INF_Preserve(member)
					&& GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(member) <= 0)
					SCR_EntityHelper.DeleteEntityAndChildren(member);
			}
			if (vehicle && !preserve)
				SCR_EntityHelper.DeleteEntityAndChildren(vehicle);
		}
		if (vehicle && !m_INFZoneVehiclesStopped && m_fWreckLifetime > 0
			&& (!removeVehicle || (preserve && !playerAboard)))
			GetGame().GetCallqueue().CallLater(INF_ZoneWreckCheck, (int)(m_fWreckLifetime * 1000), false, vehicle);
	}

	override protected void DespawnVehicle(INS_AmbientVehicleAgent agent)
	{
		array<IEntity> members = {};
		INF_CleanupVehicle(agent, members, true);
		m_aActiveVehicles.RemoveItem(agent);
	}

	protected void INF_RemoveZoneVehicle(INF_ZoneVehicle unit, bool removeVehicle)
	{
		INF_CleanupVehicle(unit.m_Agent, unit.m_Crew, removeVehicle);
		m_INFZoneVehicles.RemoveItem(unit);
		INS_AmbientPatrolManager patrols = INF_ZoneEditorContext.Grid(GetOwner());
		if (patrols && patrols.INF_GetZoneStates())
			patrols.INF_GetZoneStates().InvalidatePresence();
	}

	override protected void WreckCleanupCheck(IEntity wreck)
	{
		INF_ZoneWreckCheck(wreck);
	}

	override protected void WreckPlayerExitPoll(IEntity wreck)
	{
		if (!wreck || m_INFZoneVehiclesStopped || !Replication.IsServer())
			return;
		if (HasPlayerOccupant(wreck))
			GetGame().GetCallqueue().CallLater(WreckPlayerExitPoll, 60000, false, wreck);
		else
			GetGame().GetCallqueue().CallLater(INF_ZoneWreckCheck, (int)(m_fWreckLifetime * 1000), false, wreck);
	}

	protected void INF_ZoneWreckCheck(IEntity vehicle)
	{
		if (!Replication.IsServer() || !vehicle || m_INFZoneVehiclesStopped)
			return;
		if (HasPlayerOccupant(vehicle))
		{
			GetGame().GetCallqueue().CallLater(WreckPlayerExitPoll, 60000, false, vehicle);
			return;
		}
		SCR_BaseCompartmentManagerComponent compartments = SCR_BaseCompartmentManagerComponent.Cast(vehicle.FindComponent(SCR_BaseCompartmentManagerComponent));
		array<IEntity> occupants = {};
		if (compartments)
			compartments.GetOccupants(occupants);
		foreach (IEntity occupant : occupants)
		{
			if (INF_PrisonerComponent.INF_Preserve(occupant))
			{
				GetGame().GetCallqueue().CallLater(INF_ZoneWreckCheck, 30000, false, vehicle);
				return;
			}
		}
		SCR_EntityHelper.DeleteEntityAndChildren(vehicle);
	}

	protected void INF_ZoneVehicleTick()
	{
		m_bINFZoneVehicleTickQueued = false;
		if (!Replication.IsServer() || m_INFZoneVehiclesStopped)
			return;
		INS_AmbientPatrolManager patrols = INF_ZoneEditorContext.Grid(GetOwner());
		if (!patrols || !patrols.INF_GetZoneStates())
		{
			INF_ScheduleZoneVehicles();
			return;
		}
		INF_BlockPresence presence = patrols.INF_GetZoneStates().GetPresence();
		array<vector> players = presence.m_AllPlayers;
		if ((!presence.m_PlayerIDs.IsEmpty() && m_iINFZoneVehicleChance > 0 && m_iINFZoneVehicleLimit > 0
			&& !m_aVehiclePrefabs.IsEmpty()) || !m_INFZoneVehicles.IsEmpty())
			INF_ScheduleZoneVehicles();
		float now = GetGame().GetWorld().GetWorldTime();
		for (int i = m_INFZoneVehicles.Count() - 1; i >= 0; i--)
		{
			INF_ZoneVehicle unit = m_INFZoneVehicles[i];
			IEntity vehicle = unit.m_Agent.m_VehicleEntity;
			if (!unit.m_Agent.m_bActive || (!vehicle && !unit.m_Ready))
			{
				INF_RemoveZoneVehicle(unit, true);
				continue;
			}
			if (!unit.m_Ready)
			{
				if (!INF_ResolveZoneCrew(unit) && now >= unit.m_Deadline)
				{
					Print("[INF_ZoneVehicles] Occupant initialization failed/timed out; releasing the vehicle slot.", LogLevel.ERROR);
					INF_RemoveZoneVehicle(unit, true);
				}
				continue;
			}
			if (INF_ZoneCrewCount(unit) == 0)
			{
				INF_RemoveZoneVehicle(unit, false);
				continue;
			}
			if (unit.m_Zone.m_bActive)
				unit.m_Zone.m_bEverHadLiving = true;
			vector position = unit.m_Zone.m_vCenter;
			if (vehicle)
				position = vehicle.GetOrigin();
			bool distant = INF_BlockRules.Nearest(position, players) > m_fDeactivationRadius;
			if (!distant)
			{
				unit.m_NextCleanupAt = 0;
				continue;
			}
			if (unit.m_NextCleanupAt == 0)
				unit.m_NextCleanupAt = now + 10000;
			if (now < unit.m_NextCleanupAt)
				continue;
			unit.m_NextCleanupAt = now + 10000;
			players = patrols.INF_GetZoneStates().GetPresence(false, true).m_AllPlayers;
			distant = INF_BlockRules.Nearest(position, players) > m_fDeactivationRadius;
			foreach (IEntity member : unit.m_Crew)
			{
				if (INF_ZoneHostileCrew(member) && (INF_BlockRules.Nearest(member.GetOrigin(), players) <= m_fDeactivationRadius
					|| !INF_ZoneHidden(member.GetOrigin(), players)))
					distant = false;
			}
			if ((players.IsEmpty() || (distant && INF_ZoneHidden(position, players)))
				&& !HasPlayerOccupant(vehicle))
				INF_RemoveZoneVehicle(unit, true);
		}
		if (!patrols || !patrols.INF_AreZonesReady() || now < m_fINFZonePauseUntil || players.IsEmpty()
			|| m_iINFZoneVehicleChance == 0
			|| m_INFZoneVehicles.Count() >= m_iINFZoneVehicleLimit || m_aVehiclePrefabs.IsEmpty())
			return;
		foreach (INS_AmbientPatrolZoneState zone : patrols.INF_GetZoneStates().GetActiveZones())
		{
			if (!zone.m_bActive || zone.m_bINFMarkedCleared || zone.m_bINFPermanent || zone.m_bINFAttackActive)
				continue;
			string key = INF_BuildingGrid.Key(zone.m_vCenter);
			if (m_INFZoneVehicleRolls.Contains(key))
				continue;
			m_INFZoneVehicleRolls.Set(key, true);
			if (Math.RandomInt(0, 100) < m_iINFZoneVehicleChance)
			{
				players = patrols.INF_GetZoneStates().GetPresence(false, true).m_AllPlayers;
				INF_TryZoneVehicle(zone, players);
				break;
			}
		}
	}
}
