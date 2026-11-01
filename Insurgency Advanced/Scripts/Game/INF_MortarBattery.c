// Zone-owned mortar batteries: two AI-crewed mortars placed well away from the owning zone that shell enemies
// (players and their AI squad members) hostile AI have actually seen, at the last seen position. Meant to
// distract and disperse, so rounds land wide.
enum INF_EMortarState
{
	PENDING,
	INITIALISING,
	FIRING,
	COOLDOWN
}

class INF_MortarConfig
{
	int m_iChance = 100;
	int m_iLimit = 2;
	ResourceName m_Composition;
}

// One gunner group per mortar, so each gun takes its own waypoints and aims independently.
class INF_MortarCrew
{
	SCR_AIGroup m_Group;
	ref array<AIWaypoint> m_Salvo = {};
}

class INF_MortarBattery
{
	string m_sKey;
	vector m_vPos;
	float m_fYaw;
	int m_iGuardCount;
	int m_State;
	int m_iStage;
	float m_fSpawnAt;
	float m_fDeadline;
	float m_fNextFireAt;
	float m_fNextCleanupAt;
	bool m_bCrewDeadLogged;
	string m_sWaiting;
	float m_fNextNoTargetLogAt;
	float m_fFreshestSeen = -1;
	int m_iSpotters;
	int m_iSharedTargets;
	int m_iHostileTargets;
	ref array<IEntity> m_Compositions = {};
	ref array<ref INF_MortarCrew> m_Crews = {};
	SCR_AIGroup m_GuardGroup;
	AIWaypoint m_Defend;
}

class INF_MortarBatteries
{
	static const ResourceName WAYPOINT_ARTILLERY = "{C524700A27CFECDD}Prefabs/AI/Waypoints/AIWaypoint_ArtillerySupport.et";
	static const ResourceName WAYPOINT_DEFEND = "{93291E72AC23930F}Prefabs/AI/Waypoints/AIWaypoint_Defend.et";
	static const float PLACE_MIN_FROM_ZONE = 500;
	static const float PLACE_MAX_FROM_ZONE = 1500;
	static const float PLACE_MIN_FROM_PLAYER = 300;
	static const float PLACE_MIN_FROM_BATTERY = 600;
	static const float COVER_RADIUS = 2500;
	static const float KNOWN_MAX_AGE_S = 120;
	static const float MIN_SPREAD = 30;
	static const float SPECIAL_MIN_FRACTION = 0.4;
	static const float DESPAWN_RADIUS = 2000;
	static const float MIN_FIRE_RANGE = 250;
	static const float MIN_AIM_FROM_BATTERY = 120;
	static const float MAX_FIRE_RANGE = 3500;
	static const float FOOTPRINT_RADIUS = 8;
	static const float GUN_SPACING = 14;
	static const int GUN_COUNT = 2;
	static const int CREW_PER_GUN = 2;
	static const float SPREAD_RADIUS = 100;
	static const float SMOKE_SPREAD_RADIUS = 150;
	static const float ILLUMINATION_SPREAD_RADIUS = 225;
	static const float COOLDOWN_MS = 180000;
	static const float FIRST_FIRE_DELAY_MS = 10000;
	static const int SALVO_STAGES = 2;
	static const float STAGE_TIMEOUT_MS = 240000;
	static const float INIT_TIMEOUT_MS = 45000;

	protected ref array<ref INF_MortarBattery> m_Batteries = {};
	protected ref map<string, bool> m_Rolls = new map<string, bool>();
	protected bool m_bWinCarried;
	protected bool m_bStopped;

	bool HasBatteries()
	{
		return !m_Batteries.IsEmpty();
	}

	static void Log(string message)
	{
		Print("[INF_Mortar] " + message);
	}

	static void Fail(string message)
	{
		Print("[INF_Mortar] " + message, LogLevel.WARNING);
	}

	// Called as a zone activates. A won roll that cannot be placed passes to the next activating zone.
	void OnZoneActivated(INS_AmbientPatrolManager patrols, INS_AmbientPatrolZoneState zone, INF_MortarConfig config)
	{
		string key = INF_BuildingGrid.Key(zone.m_vCenter);
		if (!Replication.IsServer())
			return;
		if (m_bStopped)
		{
			Fail("Zone " + key + ": not spawning, the game mode has ended.");
			return;
		}
		if (config.m_iChance <= 0)
		{
			Log("Zone " + key + ": not spawning, spawn chance is 0.");
			return;
		}
		if (m_Batteries.Count() >= config.m_iLimit)
		{
			Log(string.Format("Zone %1: not spawning, battery limit reached (%2/%3).", key, m_Batteries.Count(), config.m_iLimit));
			return;
		}
		if (config.m_Composition == "")
		{
			Fail("Zone " + key + ": not spawning, no mortar composition is configured on INS_AmbientPatrolManager (Mortar Batteries > Mortar Composition).");
			return;
		}
		if (m_Rolls.Contains(key))
		{
			Log("Zone " + key + ": not spawning, this zone already rolled for a battery this mission.");
			return;
		}
		m_Rolls.Set(key, true);
		bool win = m_bWinCarried || Math.RandomInt(0, 100) < config.m_iChance;
		if (!win)
		{
			Log(string.Format("Zone %1: roll lost (chance %2 of 100).", key, config.m_iChance));
			return;
		}
		INF_MortarBattery covering = Covering(zone.m_vCenter);
		if (covering)
		{
			Log("Zone " + key + ": not spawning, already covered by a battery at " + covering.m_vPos.ToString() + ".");
			m_bWinCarried = false;
			return;
		}
		array<vector> players = patrols.INF_GetZoneStates().GetPresence(false, true).m_AllPlayers;
		if (players.IsEmpty())
		{
			CarryWin(key, "no players");
			return;
		}
		vector pos;
		float yaw;
		if (!FindPosition(zone, players, patrols.INF_GetZoneStates().GetActiveZones(), pos, yaw))
		{
			CarryWin(key, "no valid placement found");
			return;
		}
		INF_MortarBattery battery = new INF_MortarBattery();
		battery.m_sKey = key;
		battery.m_vPos = pos;
		battery.m_fYaw = yaw;
		battery.m_iGuardCount = Math.RandomInt(2, 5);
		battery.m_State = INF_EMortarState.PENDING;
		battery.m_fSpawnAt = GetGame().GetWorld().GetWorldTime() + INF_StaggerGate.Spawns().Reserve();
		m_Batteries.Insert(battery);
		m_bWinCarried = false;
		Log(string.Format("Zone %1: battery queued at %2, %3m from the zone.", key, pos.ToString(), vector.DistanceXZ(pos, zone.m_vCenter)));
	}

	protected void CarryWin(string key, string reason)
	{
		m_bWinCarried = true;
		Fail("Zone " + key + ": not spawning (" + reason + "); passing the win to the next activating zone.");
	}

	protected INF_MortarBattery Covering(vector zoneCenter)
	{
		foreach (INF_MortarBattery battery : m_Batteries)
		{
			if (vector.DistanceXZ(battery.m_vPos, zoneCenter) <= COVER_RADIUS)
				return battery;
		}
		return null;
	}

	protected void Reject(map<string, int> reasons, string reason)
	{
		int count;
		reasons.Find(reason, count);
		reasons.Set(reason, count + 1);
	}

	protected bool FindPosition(INS_AmbientPatrolZoneState zone, array<vector> players,
		array<INS_AmbientPatrolZoneState> activeZones, out vector pos, out float yaw)
	{
		map<string, int> reasons = new map<string, int>();
		BaseWorld world = GetGame().GetWorld();
		for (int attempt = 0; attempt < 60; attempt++)
		{
			float angle = Math.RandomFloat(0, Math.PI * 2);
			float distance = Math.RandomFloat(PLACE_MIN_FROM_ZONE, PLACE_MAX_FROM_ZONE);
			vector point = zone.m_vCenter + Vector(Math.Sin(angle) * distance, 0, Math.Cos(angle) * distance);
			point[1] = world.GetSurfaceY(point[0], point[2]);
			float candidateYaw = vector.Direction(point, zone.m_vCenter).ToYaw();
			string reason = RejectPlacement(point, candidateYaw, players, activeZones, null);
			if (reason != "")
			{
				Reject(reasons, reason);
				continue;
			}
			pos = point;
			yaw = candidateYaw;
			return true;
		}
		string summary;
		foreach (string rejected, int count : reasons)
			summary += string.Format(" %1=%2", rejected, count);
		Fail("Placement rejected 60 candidates:" + summary);
		return false;
	}

	// Returns the reason a spot is unusable, or an empty string when it passes. Also used to re-check a queued spot.
	protected string RejectPlacement(vector point, float yaw, array<vector> players,
		array<INS_AmbientPatrolZoneState> activeZones, INF_MortarBattery ignore)
	{
		if (point[1] <= 0.5)
			return "water_or_low_ground";
		if (INF_BuildingGrid.IsSpawnBlocked(point))
			return "safe_zone_or_spawn_protection";
		float nearest = INF_BlockRules.Nearest(point, players);
		if (nearest < PLACE_MIN_FROM_PLAYER)
			return "too_close_to_player";
		if (!INF_BlockRules.IsConcealed(point, players, 3))
			return "visible_to_player";
		foreach (INS_AmbientPatrolZoneState active : activeZones)
		{
			if (INF_BuildingGrid.Contains(active.m_vCenter, point))
				return "inside_active_zone_cell";
		}
		foreach (INF_MortarBattery other : m_Batteries)
		{
			if (other != ignore && vector.DistanceXZ(point, other.m_vPos) < PLACE_MIN_FROM_BATTERY)
				return "too_close_to_other_battery";
		}
		for (int gun = 0; gun < GUN_COUNT; gun++)
		{
			string spotReason = RejectSpot(GunSpot(point, yaw, gun), point[1]);
			if (spotReason != "")
				return spotReason;
		}
		return "";
	}

	// Each composition holds one mortar, so a battery is GUN_COUNT compositions side by side.
	protected vector GunSpot(vector origin, float yaw, float index)
	{
		float angle = yaw * Math.DEG2RAD;
		vector spot = origin + Vector(Math.Cos(angle), 0, -Math.Sin(angle)) * (GUN_SPACING * index);
		spot[1] = GetGame().GetWorld().GetSurfaceY(spot[0], spot[2]);
		return spot;
	}

	protected string RejectSpot(vector spot, float baseHeight)
	{
		if (spot[1] <= 0.5 || Math.AbsFloat(spot[1] - baseHeight) > 2)
			return "gun_spot_slope_or_water";
		if (!FootprintFlat(spot))
			return "slope";
		if (!INF_ForestCamps.ClearVolume(spot, FOOTPRINT_RADIUS, 4))
			return "obstructed";
		vector empty;
		if (!SCR_WorldTools.FindEmptyTerrainPosition(empty, spot, FOOTPRINT_RADIUS, 4, 3) || vector.DistanceSqXZ(empty, spot) > 0.25)
			return "no_empty_terrain";
		return "";
	}

	protected bool FootprintFlat(vector pos)
	{
		BaseWorld world = GetGame().GetWorld();
		for (int x = -8; x <= 8; x += 4)
		{
			for (int z = -8; z <= 8; z += 4)
			{
				if (Math.AbsFloat(world.GetSurfaceY(pos[0] + x, pos[2] + z) - pos[1]) > 0.8)
					return false;
			}
		}
		return true;
	}

	// Called from the patrol manager's proximity check, so batteries need no tick of their own.
	void Maintain(INS_AmbientPatrolManager patrols, INF_BlockPresence presence, INF_MortarConfig config)
	{
		if (!Replication.IsServer() || m_bStopped || m_Batteries.IsEmpty())
			return;
		array<vector> players = presence.m_AllPlayers;
		float now = GetGame().GetWorld().GetWorldTime();
		for (int i = m_Batteries.Count() - 1; i >= 0; i--)
		{
			INF_MortarBattery battery = m_Batteries[i];
			if (battery.m_State == INF_EMortarState.PENDING)
			{
				if (now >= battery.m_fSpawnAt && !SpawnBattery(patrols, battery, players, config))
					Remove(battery, "spawn failed");
				continue;
			}
			if (battery.m_State == INF_EMortarState.INITIALISING)
			{
				if (!Initialise(battery, now))
				{
					if (now >= battery.m_fDeadline)
						Remove(battery, "failed to initialise in time: " + battery.m_sWaiting);
					continue;
				}
			}
			if (ShouldDespawn(patrols, battery, players, now))
			{
				Remove(battery, "no players within " + DESPAWN_RADIUS.ToString() + "m and no covered zone active");
				continue;
			}
			if (AllCrewsDead(battery) && GuardsDead(battery))
			{
				Remove(battery, "crew and guards eliminated");
				continue;
			}
			Fire(patrols, battery, now);
		}
	}

	protected bool SpawnBattery(INS_AmbientPatrolManager patrols, INF_MortarBattery battery,
		array<vector> players, INF_MortarConfig config)
	{
		string key = battery.m_sKey;
		if (players.IsEmpty())
		{
			Fail("Battery for zone " + key + ": spawn aborted, no players.");
			return false;
		}
		string reason = RejectPlacement(battery.m_vPos, battery.m_fYaw, players, patrols.INF_GetZoneStates().GetActiveZones(), battery);
		if (reason != "")
		{
			Fail("Battery for zone " + key + ": queued spot no longer valid (" + reason + ").");
			return false;
		}
		for (int gunIndex = 0; gunIndex < GUN_COUNT; gunIndex++)
		{
			IEntity composition = SpawnPrefab(config.m_Composition, GunSpot(battery.m_vPos, battery.m_fYaw, gunIndex), battery.m_fYaw);
			if (!composition)
			{
				Fail("Battery for zone " + key + ": mortar composition failed to load or spawn: " + config.m_Composition);
				return false;
			}
			battery.m_Compositions.Insert(composition);
			if (!INF_ForestCamps.PrepareComposition(composition))
			{
				Fail("Battery for zone " + key + ": mortar composition contains an AI group; use a static composition: " + config.m_Composition);
				return false;
			}
		}
		array<ResourceName> pool = patrols.INF_GetGroupPrefabs();
		for (int crewIndex = 0; crewIndex < GUN_COUNT; crewIndex++)
		{
			IEntity crewEntity = INF_BuildingGrid.Spawn(GunSpot(battery.m_vPos, battery.m_fYaw, crewIndex), CREW_PER_GUN, pool);
			if (!crewEntity)
			{
				Fail(string.Format("Battery for zone %1: no infantry group prefab with %2 members for gunner crew %3.", key, CREW_PER_GUN, crewIndex));
				return false;
			}
			INF_MortarCrew crew = new INF_MortarCrew();
			crew.m_Group = SCR_AIGroup.Cast(crewEntity);
			battery.m_Crews.Insert(crew);
		}
		IEntity guards = INF_BuildingGrid.Spawn(battery.m_vPos, battery.m_iGuardCount, pool);
		if (!guards)
		{
			Fail(string.Format("Battery for zone %1: no infantry group prefab with %2 members for the guards.", key, battery.m_iGuardCount));
			return false;
		}
		battery.m_GuardGroup = SCR_AIGroup.Cast(guards);
		battery.m_State = INF_EMortarState.INITIALISING;
		battery.m_fDeadline = GetGame().GetWorld().GetWorldTime() + INIT_TIMEOUT_MS;
		Log(string.Format("Battery for zone %1: spawned %2 mortar compositions, %2 gunner crews of %3 and %4 guards at %5.", key, GUN_COUNT, CREW_PER_GUN, battery.m_iGuardCount, battery.m_vPos.ToString()));
		patrols.INF_GetZoneStates().InvalidatePresence();
		return true;
	}

	// Slotted compositions may fill their slots after spawning, so the mortars are looked up while initialising.
	protected void CollectGuns(IEntity entity, array<IEntity> guns)
	{
		if (entity.FindComponent(SCR_AIVehicleUsageComponent))
			guns.Insert(entity);
		for (IEntity child = entity.GetChildren(); child; child = child.GetSibling())
			CollectGuns(child, guns);
	}

	protected IEntity SpawnPrefab(ResourceName prefab, vector pos, float yaw)
	{
		Resource resource = Resource.Load(prefab);
		if (!resource || !resource.IsValid())
			return null;
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		Math3D.AnglesToMatrix(Vector(yaw, 0, 0), params.Transform);
		pos[1] = GetGame().GetWorld().GetSurfaceY(pos[0], pos[2]);
		params.Transform[3] = pos;
		return GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params);
	}

	// Waits for each composition's mortar and every group's members, then hands each mortar to its own crew.
	protected bool Initialise(INF_MortarBattery battery, float now)
	{
		string key = battery.m_sKey;
		if (!battery.m_GuardGroup)
		{
			battery.m_sWaiting = "the guard group was deleted";
			return false;
		}
		foreach (INF_MortarCrew waitingCrew : battery.m_Crews)
		{
			if (!waitingCrew.m_Group)
			{
				battery.m_sWaiting = "a gunner group was deleted";
				return false;
			}
			if (waitingCrew.m_Group.GetAgentsCount() < CREW_PER_GUN)
			{
				battery.m_sWaiting = string.Format("gunners not spawned (%1/%2 in a crew)", waitingCrew.m_Group.GetAgentsCount(), CREW_PER_GUN);
				return false;
			}
		}
		if (battery.m_GuardGroup.GetAgentsCount() < battery.m_iGuardCount)
		{
			battery.m_sWaiting = string.Format("guards not spawned (%1/%2)", battery.m_GuardGroup.GetAgentsCount(), battery.m_iGuardCount);
			return false;
		}
		array<IEntity> guns = {};
		for (int i = 0; i < GUN_COUNT; i++)
		{
			array<IEntity> found = {};
			CollectGuns(battery.m_Compositions[i], found);
			if (found.IsEmpty())
			{
				battery.m_sWaiting = string.Format("no mortar with SCR_AIVehicleUsageComponent in composition %1 yet", i);
				return false;
			}
			guns.Insert(found[0]);
		}
		for (int crewIndex = 0; crewIndex < GUN_COUNT; crewIndex++)
		{
			INF_MortarCrew crew = battery.m_Crews[crewIndex];
			SCR_AIGroupUtilityComponent utility = crew.m_Group.GetGroupUtilityComponent();
			if (!utility)
			{
				battery.m_sWaiting = "a gunner group has no SCR_AIGroupUtilityComponent yet";
				return false;
			}
			utility.AddUsableVehicle(SCR_AIVehicleUsageComponent.Cast(guns[crewIndex].FindComponent(SCR_AIVehicleUsageComponent)));
			CheckAmmoPrefabs(key, guns[crewIndex]);
		}
		foreach (INF_MortarCrew readyCrew : battery.m_Crews)
		{
			UntrackMembers(readyCrew.m_Group);
			Activate(readyCrew.m_Group);
		}
		UntrackMembers(battery.m_GuardGroup);
		battery.m_Defend = SpawnWaypoint(WAYPOINT_DEFEND, battery.m_vPos);
		if (!battery.m_Defend)
			Fail("Battery for zone " + key + ": defend waypoint prefab failed to spawn; guards will idle.");
		else
			battery.m_GuardGroup.AddWaypoint(battery.m_Defend);
		Activate(battery.m_GuardGroup);
		battery.m_State = INF_EMortarState.COOLDOWN;
		battery.m_fNextFireAt = now + FIRST_FIRE_DELAY_MS;
		Log(string.Format("Battery for zone %1: ready with %2 mortar(s), first fire mission in %3s.", key, GUN_COUNT, (int)(FIRST_FIRE_DELAY_MS / 1000)));
		return true;
	}

	// AI finds shells through the mortar's per-type ammo prefabs, so an empty slot means that ammo is never fired.
	protected void CheckAmmoPrefabs(string key, IEntity gun)
	{
		SCR_AIStaticArtilleryVehicleUsageComponent usage = SCR_AIStaticArtilleryVehicleUsageComponent.Cast(gun.FindComponent(SCR_AIStaticArtilleryVehicleUsageComponent));
		if (!usage)
		{
			Fail("Battery for zone " + key + ": mortar has no SCR_AIStaticArtilleryVehicleUsageComponent, AI cannot fire it as artillery.");
			return;
		}
		array<SCR_EAIArtilleryAmmoType> types = {SCR_EAIArtilleryAmmoType.HIGH_EXPLOSIVE, SCR_EAIArtilleryAmmoType.SMOKE, SCR_EAIArtilleryAmmoType.ILLUMINATION};
		foreach (SCR_EAIArtilleryAmmoType type : types)
		{
			ResourceName ammo = usage.GetAmmoResourceName(type);
			if (ammo == "")
				Fail("Battery for zone " + key + ": mortar " + gun.GetPrefabData().GetPrefabName() + " has no " + typename.EnumToString(SCR_EAIArtilleryAmmoType, type) + " ammo prefab configured, that ammo cannot be fired.");
			else
				Log("Battery for zone " + key + ": mortar " + typename.EnumToString(SCR_EAIArtilleryAmmoType, type) + " ammo is " + ammo + ".");
		}
	}

	protected void UntrackMembers(SCR_AIGroup group)
	{
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		if (!persistence)
			return;
		array<IEntity> members = {};
		CollectMembers(group, members);
		foreach (IEntity member : members)
			persistence.StopTracking(member, true);
	}

	protected void Activate(SCR_AIGroup group)
	{
		group.ActivateAI();
		AIControlComponent control = group.GetControlComponent();
		if (control)
			control.ActivateAI();
	}

	protected AIWaypoint SpawnWaypoint(ResourceName prefab, vector pos)
	{
		IEntity entity = SpawnPrefab(prefab, pos, 0);
		return AIWaypoint.Cast(entity);
	}

	protected bool IsCrewAlive(INF_MortarCrew crew)
	{
		return crew.m_Group && crew.m_Group.GetAgentsCount() > 0;
	}

	protected bool AllCrewsDead(INF_MortarBattery battery)
	{
		foreach (INF_MortarCrew crew : battery.m_Crews)
		{
			if (IsCrewAlive(crew))
				return false;
		}
		return true;
	}

	// The salvo is over once every living crew has worked through its waypoints.
	protected bool SalvoDone(INF_MortarBattery battery)
	{
		foreach (INF_MortarCrew crew : battery.m_Crews)
		{
			if (IsCrewAlive(crew) && crew.m_Group.GetCurrentWaypoint())
				return false;
		}
		return true;
	}

	protected bool GuardsDead(INF_MortarBattery battery)
	{
		return !battery.m_GuardGroup || battery.m_GuardGroup.GetAgentsCount() == 0;
	}

	// A battery stays while a player is within DESPAWN_RADIUS or a zone it covers is active.
	protected bool ShouldDespawn(INS_AmbientPatrolManager patrols, INF_MortarBattery battery, array<vector> players, float now)
	{
		bool distant = players.IsEmpty() || INF_BlockRules.Nearest(battery.m_vPos, players) > DESPAWN_RADIUS;
		if (distant)
		{
			foreach (INS_AmbientPatrolZoneState zone : patrols.INF_GetZoneStates().GetActiveZones())
			{
				if (zone.m_bActive && vector.DistanceXZ(zone.m_vCenter, battery.m_vPos) <= COVER_RADIUS)
				{
					distant = false;
					break;
				}
			}
		}
		if (!distant)
		{
			battery.m_fNextCleanupAt = 0;
			return false;
		}
		if (battery.m_fNextCleanupAt == 0)
			battery.m_fNextCleanupAt = now + 10000;
		return now >= battery.m_fNextCleanupAt;
	}

	protected void Fire(INS_AmbientPatrolManager patrols, INF_MortarBattery battery, float now)
	{
		string key = battery.m_sKey;
		if (battery.m_State == INF_EMortarState.FIRING)
		{
			if (now < battery.m_fDeadline && !SalvoDone(battery))
				return;
			ClearSalvoWaypoints(battery);
			// The gap after the first HE block: the second stage aims on what the AI know now, not at the start.
			if (battery.m_iStage + 1 < SALVO_STAGES && !AllCrewsDead(battery))
			{
				vector nextTarget;
				float nextKnowledge;
				if (FindKnownTarget(patrols, battery, nextTarget, nextKnowledge))
				{
					battery.m_iStage++;
					if (StartSalvo(battery, nextTarget, nextKnowledge, battery.m_iStage))
					{
						battery.m_fDeadline = now + STAGE_TIMEOUT_MS;
						return;
					}
				}
				else
					Log(string.Format("Battery for zone %1: lost track of every enemy after stage %2, ceasing fire (spotter groups %3, hostile targets %4).", key, battery.m_iStage + 1, battery.m_iSpotters, battery.m_iHostileTargets));
			}
			battery.m_State = INF_EMortarState.COOLDOWN;
			battery.m_fNextFireAt = now + COOLDOWN_MS * Math.RandomFloat(0.7, 1.3);
			Log(string.Format("Battery for zone %1: salvo finished, next in %2s.", key, (int)((battery.m_fNextFireAt - now) / 1000)));
			return;
		}
		if (AllCrewsDead(battery))
		{
			if (!battery.m_bCrewDeadLogged)
				Log("Battery for zone " + key + ": gunners are dead, it can no longer fire.");
			battery.m_bCrewDeadLogged = true;
			return;
		}
		if (now < battery.m_fNextFireAt)
			return;
		vector target;
		float knowledge;
		if (!FindKnownTarget(patrols, battery, target, knowledge))
		{
			// Hold fire with no cooldown until something is known; log what the spotters know every 30s.
			if (now >= battery.m_fNextNoTargetLogAt)
			{
				battery.m_fNextNoTargetLogAt = now + 30000;
				Log(string.Format("Battery for zone %1: holding fire, nothing known in range. Spotter groups %2, shared targets %3, hostile %4, freshest hostile sighting %5s (-1 = none).",
					key, battery.m_iSpotters, battery.m_iSharedTargets, battery.m_iHostileTargets, battery.m_fFreshestSeen));
			}
			return;
		}
		battery.m_iStage = 0;
		if (!StartSalvo(battery, target, knowledge, 0))
			battery.m_fNextFireAt = now + 30000;
		else
		{
			battery.m_State = INF_EMortarState.FIRING;
			battery.m_fDeadline = now + STAGE_TIMEOUT_MS;
		}
	}

	// The battery only fires on what hostile AI actually know: the guards and the defenders of the active zones it
	// covers. It reads each spotter group's shared perception (members' sightings and gunfire it has heard) and
	// aims at where the best-known enemy was last seen. Knowledge runs from 0 to 1: how recently it was seen
	// times how certain it is (a known entity is certain, a bare position such as gunfire is half certain).
	protected bool FindKnownTarget(INS_AmbientPatrolManager patrols, INF_MortarBattery battery, out vector target, out float knowledge)
	{
		array<SCR_AIGroup> observers = {};
		if (battery.m_GuardGroup)
			observers.Insert(battery.m_GuardGroup);
		foreach (INS_AmbientPatrolZoneState zone : patrols.INF_GetZoneStates().GetActiveZones())
		{
			if (!zone.m_bActive || vector.DistanceXZ(zone.m_vCenter, battery.m_vPos) > COVER_RADIUS)
				continue;
			foreach (INS_PatrolAgent patrol : zone.m_aAgents)
			{
				if (patrol.m_Group)
					observers.Insert(patrol.m_Group);
			}
		}
		float now = GetGame().GetWorld().GetWorldTime();
		bool found;
		float bestAge;
		knowledge = 0;
		battery.m_iSpotters = observers.Count();
		battery.m_iSharedTargets = 0;
		battery.m_iHostileTargets = 0;
		battery.m_fFreshestSeen = -1;
		foreach (SCR_AIGroup observer : observers)
		{
			SCR_AIGroupUtilityComponent utility = observer.GetGroupUtilityComponent();
			if (!utility || !utility.m_Perception)
				continue;
			Faction ownFaction = observer.GetFaction();
			foreach (SCR_AITargetInfo info : utility.m_Perception.m_aTargets)
			{
				if (!info)
					continue;
				battery.m_iSharedTargets++;
				if (ownFaction && info.m_Faction && !ownFaction.IsFactionEnemy(info.m_Faction))
					continue;
				if (info.m_DamageManager && info.m_DamageManager.IsDestroyed())
					continue;
				battery.m_iHostileTargets++;
				float infoAge = Math.Max(0, (now - info.m_fTimestamp) * 0.001);
				if (battery.m_fFreshestSeen < 0 || infoAge < battery.m_fFreshestSeen)
					battery.m_fFreshestSeen = infoAge;
				float range = vector.DistanceXZ(info.m_vWorldPos, battery.m_vPos);
				if (infoAge >= KNOWN_MAX_AGE_S || range < MIN_FIRE_RANGE || range > MAX_FIRE_RANGE)
					continue;
				float certainty = 0.5;
				if (info.m_Entity)
					certainty = 1;
				float infoKnowledge = (1 - infoAge / KNOWN_MAX_AGE_S) * certainty;
				if (found && infoKnowledge <= knowledge)
					continue;
				found = true;
				knowledge = infoKnowledge;
				target = info.m_vWorldPos;
				bestAge = infoAge;
			}
		}
		if (found)
			Log(string.Format("Battery for zone %1: best known enemy was seen %2s ago, knowledge %3 (0 to 1), from %4 spotter groups.", battery.m_sKey, bestAge, knowledge, battery.m_iSpotters));
		return found;
	}

	// Full knowledge gives the minimum spread and none gives the ammo's full radius. The minimum is never below
	// MIN_SPREAD, and smoke and illumination keep a larger share of their radius.
	protected float KnowledgeSpread(float fullRadius, float knowledge, float minFraction)
	{
		float minRadius = Math.Min(Math.Max(MIN_SPREAD, fullRadius * minFraction), fullRadius);
		return minRadius + (fullRadius - minRadius) * (1 - Math.Clamp(knowledge, 0, 1));
	}

	protected bool IsNight()
	{
		ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
		if (!world)
			return false;
		TimeAndWeatherManagerEntity time = world.GetTimeAndWeatherManager();
		return time && time.IsNightHour(time.GetTimeOfTheDay());
	}

	// One stage of the 18-round salvo (12 HE, 6 smoke by day or illumination by night), 9 rounds per stage.
	protected bool StartSalvo(INF_MortarBattery battery, vector target, float knowledge, int stage)
	{
		string key = battery.m_sKey;
		bool night = IsNight();
		SCR_EAIArtilleryAmmoType HE = SCR_EAIArtilleryAmmoType.HIGH_EXPLOSIVE;
		SCR_EAIArtilleryAmmoType special = SCR_EAIArtilleryAmmoType.SMOKE;
		string specialName = "smoke";
		float specialRadius = KnowledgeSpread(SMOKE_SPREAD_RADIUS, knowledge, SPECIAL_MIN_FRACTION);
		if (night)
		{
			special = SCR_EAIArtilleryAmmoType.ILLUMINATION;
			specialName = "illumination";
			specialRadius = KnowledgeSpread(ILLUMINATION_SPREAD_RADIUS, knowledge, SPECIAL_MIN_FRACTION);
		}
		float heRadius = KnowledgeSpread(SPREAD_RADIUS, knowledge, 0);
		ClearSalvoWaypoints(battery);
		int placed;
		// Each stage is one special run then one HE run: stage 0 is 1 special and 3 HE per crew, stage 1 is 2 special
		// and 3 HE (2 special, 6 HE, 4 special, 6 HE across both crews). Every crew works through its own waypoints,
		// so each gun rolls its own aim points. An HE block is one waypoint (all its rounds at one aim point, fired
		// without re-aiming); special rounds are one per waypoint.
		array<SCR_EAIArtilleryAmmoType> types = {special, HE, special, HE};
		array<int> counts = {1, 3, 2, 3};
		array<int> roundsPerWaypoint = {1, 3, 1, 3};
		array<float> radii = {specialRadius, heRadius, specialRadius, heRadius};
		foreach (INF_MortarCrew crew : battery.m_Crews)
		{
			if (!IsCrewAlive(crew))
				continue;
			for (int run = stage * 2; run < stage * 2 + 2; run++)
			{
				for (int leg = 0; leg < counts[run] / roundsPerWaypoint[run]; leg++)
				{
					vector aim;
					bool clear;
					for (int tries = 0; tries < 8 && !clear; tries++)
					{
						// The square root spreads aim points evenly over the area rather than bunching them at the centre.
						float distance = radii[run] * Math.Sqrt(Math.RandomFloat01());
						float angle = Math.RandomFloat(0, Math.PI * 2);
						aim = target + Vector(Math.Sin(angle) * distance, 0, Math.Cos(angle) * distance);
						clear = vector.DistanceXZ(aim, battery.m_vPos) >= MIN_AIM_FROM_BATTERY;
					}
					if (!clear)
					{
						Fail("Battery for zone " + key + ": waypoint skipped, every aim point was within " + MIN_AIM_FROM_BATTERY.ToString() + "m of the battery.");
						continue;
					}
					aim[1] = GetGame().GetWorld().GetSurfaceY(aim[0], aim[2]);
					AIWaypoint waypoint = SpawnWaypoint(WAYPOINT_ARTILLERY, aim);
					if (!waypoint)
					{
						Fail("Battery for zone " + key + ": artillery waypoint prefab failed to spawn.");
						continue;
					}
					SCR_AIWaypointArtillerySupport artillery = SCR_AIWaypointArtillerySupport.Cast(waypoint);
					if (!artillery)
					{
						Fail("Battery for zone " + key + ": spawned waypoint is not an SCR_AIWaypointArtillerySupport.");
						SCR_EntityHelper.DeleteEntityAndChildren(waypoint);
						continue;
					}
					artillery.SetTargetShotCount(roundsPerWaypoint[run]);
					artillery.SetAmmoType(types[run]);
					artillery.SetActive(true);
					crew.m_Group.AddWaypoint(waypoint);
					crew.m_Salvo.Insert(waypoint);
					placed++;
				}
			}
		}
		if (placed == 0)
		{
			Fail("Battery for zone " + key + ": salvo not started, no artillery waypoints could be created; retrying in 30s.");
			return false;
		}
		foreach (INF_MortarCrew activeCrew : battery.m_Crews)
		{
			if (IsCrewAlive(activeCrew))
				Activate(activeCrew.m_Group);
		}
		Log(string.Format("Battery for zone %1: stage %2 of %3, %4 waypoints (HE blocks + %5 singles) at the last seen position %6, knowledge %7, aim scatter HE %8m, %5 %9m.", key,
			stage + 1, SALVO_STAGES, placed, specialName, target.ToString(), knowledge, heRadius, specialRadius));
		return true;
	}

	protected void ClearSalvoWaypoints(INF_MortarBattery battery)
	{
		foreach (INF_MortarCrew crew : battery.m_Crews)
		{
			foreach (AIWaypoint waypoint : crew.m_Salvo)
			{
				if (!waypoint)
					continue;
				if (crew.m_Group)
					crew.m_Group.RemoveWaypoint(waypoint);
				SCR_EntityHelper.DeleteEntityAndChildren(waypoint);
			}
			crew.m_Salvo.Clear();
		}
	}

	protected void Remove(INF_MortarBattery battery, string reason, bool immediate = false)
	{
		Log("Battery for zone " + battery.m_sKey + " removed: " + reason + ".");
		array<IEntity> doomed = {};
		foreach (IEntity composition : battery.m_Compositions)
			doomed.Insert(composition);
		foreach (INF_MortarCrew crew : battery.m_Crews)
		{
			foreach (AIWaypoint waypoint : crew.m_Salvo)
				doomed.Insert(waypoint);
			CollectMembers(crew.m_Group, doomed);
			if (crew.m_Group)
				doomed.Insert(crew.m_Group);
		}
		if (battery.m_Defend)
			doomed.Insert(battery.m_Defend);
		CollectMembers(battery.m_GuardGroup, doomed);
		if (battery.m_GuardGroup)
			doomed.Insert(battery.m_GuardGroup);
		m_Batteries.RemoveItem(battery);
		if (immediate)
			DeleteParts(doomed);
		else
			GetGame().GetCallqueue().CallLater(DeleteParts, INF_StaggerGate.Despawns().Reserve(), false, doomed);
	}

	protected void CollectMembers(SCR_AIGroup group, array<IEntity> into)
	{
		if (!group)
			return;
		array<AIAgent> agents = {};
		group.GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			IEntity entity = agent.GetControlledEntity();
			if (entity)
				into.Insert(entity);
		}
	}

	// Captured gunners and guards follow the same abandonment rules as zone defenders.
	bool BindPrisonerCleanup(AIGroup group, INF_PrisonerComponent prisoner)
	{
		SCR_AIGroup owner = SCR_AIGroup.Cast(group);
		if (!owner)
			return false;
		foreach (INF_MortarBattery battery : m_Batteries)
		{
			bool claimed;
			if (owner == battery.m_GuardGroup)
				claimed = true;
			foreach (INF_MortarCrew crew : battery.m_Crews)
			{
				if (owner == crew.m_Group)
					claimed = true;
			}
			if (!claimed)
				continue;
			prisoner.INF_SetCleanupRules(DESPAWN_RADIUS, INF_BlockRules.DESPAWN_DELAY_MS, false, battery.m_vPos, true);
			return true;
		}
		return false;
	}

	// Re-checked at deletion time because a player may have taken over a mortar or character, and secured
	// prisoners are kept until their own cleanup rules release them.
	protected void DeleteParts(array<IEntity> entities)
	{
		foreach (IEntity entity : entities)
		{
			if (!entity || GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(entity) > 0
				|| INF_PrisonerComponent.INF_Preserve(entity))
				continue;
			SCR_EntityHelper.DeleteEntityAndChildren(entity);
		}
	}

	void Stop()
	{
		m_bStopped = true;
		for (int i = m_Batteries.Count() - 1; i >= 0; i--)
			Remove(m_Batteries[i], "game mode ended", true);
	}
}
