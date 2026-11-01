modded class INS_MissionParams
{
	[Attribute("7", UIWidgets.Slider, "Camps randomly selected at startup for a fresh mission. Saved missions retain their roster.", "1 20 1")]
	int m_iINFCampCount;

	// Every field below overrides the matching game mode attribute. -1 (or empty) keeps the game mode value.
	[Attribute("-1", UIWidgets.EditBox, "Terrain-index revision (1 or more).", category: "Terrain Index")]
	int m_iINFTerrainIndexRevision;
	[Attribute("", UIWidgets.ResourceNamePicker, "Camp composition prefab.", "et", category: "Forest Camps")]
	ResourceName m_sINFCampComposition;
	[Attribute("", UIWidgets.EditBox, "Player faction key.", category: "Forest Camps")]
	string m_sINFPlayerFaction;
	[Attribute("-1", UIWidgets.EditBox, "Camp footprint half-width in metres (3 to 24).", category: "Forest Camps")]
	float m_fINFCampFootprintRadius;
	[Attribute("-1", UIWidgets.EditBox, "Site strength multiplier (1 to 4).", category: "Grid Zones")]
	int m_iINFSiteStrengthMultiplier;
	[Attribute("-1", UIWidgets.EditBox, "Counterattack chance percent (0 to 100).", category: "Grid Zones")]
	int m_iINFCounterAttackChance;
	[Attribute("-1", UIWidgets.EditBox, "Mortar battery chance percent (0 to 100).", category: "Mortar Batteries")]
	int m_iINFMortarChance;
	[Attribute("-1", UIWidgets.EditBox, "Maximum mortar batteries (0 to 4).", category: "Mortar Batteries")]
	int m_iINFMortarLimit;
	[Attribute("", UIWidgets.ResourceNamePicker, "Mortar battery composition prefab.", "et", category: "Mortar Batteries")]
	ResourceName m_sINFMortarComposition;
	[Attribute("-1", UIWidgets.EditBox, "Percentage of viable roadblock and defence slots that receive a site (0 to 100). 0 disables roadblocks and defences.", category: "Roadblocks")]
	int m_iINFRoadblockPercent;
	[Attribute("-1", UIWidgets.EditBox, "Minimum roadblock spacing in metres (200 to 3000).", category: "Roadblocks")]
	float m_fINFRoadblockSpacing;
	[Attribute("-1", UIWidgets.EditBox, "Roadblock owner-zone distance in metres (200 to 3000).", category: "Roadblocks")]
	float m_fINFRoadblockOwnerDistance;
	[Attribute("-1", UIWidgets.EditBox, "Maximum roadblocks per zone (1 to 4).", category: "Roadblocks")]
	int m_iINFRoadblocksPerZone;
	[Attribute("-1", UIWidgets.EditBox, "Soldiers per roadblock guard group (2 to 8).", category: "Roadblocks")]
	int m_iINFRoadblockGuards;
	[Attribute("-1", UIWidgets.EditBox, "Travel efficiency entry speed in km/h (1 to 200).", category: "Travel Efficiency")]
	float m_fINFTravelEnterSpeed;
	[Attribute("-1", UIWidgets.EditBox, "Travel efficiency exit speed in km/h (0 to 199).", category: "Travel Efficiency")]
	float m_fINFTravelExitSpeed;
	[Attribute("-1", UIWidgets.EditBox, "Seconds at entry speed before travel efficiency starts (1 to 180).", category: "Travel Efficiency")]
	float m_fINFTravelEnterSeconds;
	[Attribute("-1", UIWidgets.EditBox, "Seconds below exit speed before it ends (1 to 180).", category: "Travel Efficiency")]
	float m_fINFTravelExitSeconds;
	[Attribute("-1", UIWidgets.EditBox, "Zone vehicle chance percent (0 to 100).", category: "Zone Vehicles")]
	int m_iINFZoneVehicleChance;
	[Attribute("-1", UIWidgets.EditBox, "Zone vehicle cap (0 to 32).", category: "Zone Vehicles")]
	int m_iINFZoneVehicleLimit;
	[Attribute("-1", UIWidgets.EditBox, "Questioning intel probability (0 to 1).", category: "Prisoners")]
	float m_fINFQuestioningChance;
	[Attribute("-1", UIWidgets.EditBox, "Intel sources: 0 = body search and questioning, 1 = questioning only.", category: "Prisoners")]
	int m_iINFIntelSource;
	[Attribute("-1", UIWidgets.EditBox, "Objectives required for victory: 0 = caches, 1 = caches and camps, 2 = caches, camps and zones.", category: "Objectives")]
	int m_iINFMissionEndMode;
	[Attribute("-1", UIWidgets.EditBox, "Cache destruction objective: 0 = off, 1 = on.", category: "Objectives")]
	int m_iINFCacheObjective;
}

// Applies one mission header override, rejecting values outside the attribute's range.
class INF_HeaderSettings
{
	static bool Int(int value, int minimum, int maximum, string name, out int target)
	{
		if (value == -1)
			return false;
		if (value < minimum || value > maximum)
		{
			Print(string.Format("[INF_Header] %1 must be %2 to %3; keeping the game mode value.", name, minimum, maximum), LogLevel.ERROR);
			return false;
		}
		target = value;
		return true;
	}

	static bool Float(float value, float minimum, float maximum, string name, out float target)
	{
		if (value == -1)
			return false;
		if (!(value >= minimum && value <= maximum))
		{
			Print(string.Format("[INF_Header] %1 must be %2 to %3; keeping the game mode value.", name, minimum, maximum), LogLevel.ERROR);
			return false;
		}
		target = value;
		return true;
	}
}

// Hands out evenly spaced time slots so spawns or deletes never land in the same moment.
// All AI managers share one spawn gate and one despawn gate.
class INF_StaggerGate
{
	static const int GAP_MS = 2000;
	protected static ref INF_StaggerGate s_Spawns = new INF_StaggerGate();
	protected static ref INF_StaggerGate s_Despawns = new INF_StaggerGate();
	protected float m_fNextAt;

	static INF_StaggerGate Spawns()
	{
		return s_Spawns;
	}

	static INF_StaggerGate Despawns()
	{
		return s_Despawns;
	}

	int Reserve()
	{
		float now = GetGame().GetWorld().GetWorldTime();
		float at = Math.Max(now, m_fNextAt);
		m_fNextAt = at + GAP_MS;
		return (int)(at - now);
	}

	float Backlog()
	{
		return Math.Max(0, m_fNextAt - GetGame().GetWorld().GetWorldTime());
	}
}

class INF_SpawnProtection
{
	static const float RADIUS = 150;

	// Hostile AI never spawn near an enabled friendly spawn point, such as a player-built base.
	static bool Contains(vector pos)
	{
		BaseGameMode mode = GetGame().GetGameMode();
		if (!mode)
			return false;
		INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(mode.FindComponent(INS_AmbientPatrolManager));
		if (!patrols)
			return false;
		Faction faction = patrols.INF_GetPlayerFaction();
		if (!faction)
			return false;
		array<SCR_SpawnPoint> points = SCR_SpawnPoint.GetSpawnPoints();
		if (!points)
			return false;
		string factionKey = faction.GetFactionKey();
		foreach (SCR_SpawnPoint point : points)
		{
			// Personal spawn points follow a player and are not a base.
			if (!point || !point.IsSpawnPointEnabled() || point.GetFactionKey() != factionKey || SCR_PlayerSpawnPoint.Cast(point))
				continue;
			if (vector.DistanceSqXZ(pos, point.GetOrigin()) <= RADIUS * RADIUS)
				return true;
		}
		return false;
	}
}

class INF_BuildingGrid
{
	static const float SIZE = 100;
	static const float HALF = 50;
	static const int ACTIVE_BUDGET = 128;
	static const float SKIP_CHANCE = 25;
	static const float MIN_FOOTPRINT_AREA = 30;
	static const float MIN_HEIGHT = 2.5;
	static const float MIN_TALL_HEIGHT = 8;
	static const float MIN_TALL_FOOTPRINT = 6;
	// Ruins and small buildings are stored in the building index with a height offset, so the cached format stays unchanged.
	static const float RUIN_Y_OFFSET = 100000;
	static const int BUILDING_AI = 2;
	static const int RUIN_AI = 1;
	// A zone that despawns keeps its surviving defender count this long before regenerating to full strength.
	static const float DEFENDER_MEMORY_MS = 1800000;
	// Camp clearance: normal 150 m; around a 2 AI building 40 m; around small buildings 25 m.
	static const float CAMP_CLEAR_SQ = 22500;
	static const float CAMP_CLEAR_LIGHT_SQ = 1600;
	static const float CAMP_CLEAR_SMALL_SQ = 625;
	static const int KIND_NONE = 0;
	static const int KIND_BUILDING = 1;
	static const int KIND_RUIN = 2;
	static const int KIND_SMALL = 3;

	static string PrefabNameLower(IEntity entity)
	{
		EntityPrefabData prefabData = entity.GetPrefabData();
		if (!prefabData)
			return string.Empty;
		string name = prefabData.GetPrefabName();
		name.ToLower();
		return name;
	}

	// Stored kind: 0 building, 1 ruin, 2 small building.
	static const int STORED_BUILDING = 0;
	static const int STORED_RUIN = 1;
	static const int STORED_SMALL = 2;

	static int StoredKind(vector pos)
	{
		return (int)((pos[1] + RUIN_Y_OFFSET * 0.5) / RUIN_Y_OFFSET);
	}

	static vector StoredPosition(vector pos)
	{
		pos[1] = pos[1] - StoredKind(pos) * RUIN_Y_OFFSET;
		return pos;
	}

	// Encodes a classified building for the cached index.
	static vector Encode(vector pos, int kind)
	{
		if (kind == KIND_RUIN)
			pos[1] = pos[1] + RUIN_Y_OFFSET * STORED_RUIN;
		else if (kind == KIND_SMALL)
			pos[1] = pos[1] + RUIN_Y_OFFSET * STORED_SMALL;
		return pos;
	}

	// Garrison buildings count toward zone strength; small ones only keep camps at a distance.
	static int Classify(IEntity entity)
	{
		if (!Building.Cast(entity))
			return KIND_NONE;
		IEntity parent = entity.GetParent();
		while (parent)
		{
			if (Building.Cast(parent))
				return KIND_NONE;
			parent = parent.GetParent();
		}
		string name = PrefabNameLower(entity);
		if (name.IsEmpty())
			return KIND_NONE;
		if (name.IndexOf("rock") >= 0 || name.IndexOf("wall") >= 0
			|| name.IndexOf("pier") >= 0 || name.IndexOf("jetty") >= 0
			|| name.IndexOf("breakwater") >= 0 || name.IndexOf("quay") >= 0
			|| name.IndexOf("bridge") >= 0 || name.IndexOf("fence") >= 0
			|| name.IndexOf("revetment") >= 0)
			return KIND_NONE;
		// Sheds, kiosks and outhouses should not each add garrison strength; low ruined walls still count as ruins.
		// Tall narrow structures (lighthouses, towers, silos) qualify on height instead of footprint.
		vector mins, maxs;
		entity.GetBounds(mins, maxs);
		float footprint = (maxs[0] - mins[0]) * (maxs[2] - mins[2]);
		float height = maxs[1] - mins[1];
		bool ruin = name.IndexOf("ruin") >= 0;
		bool tall = height >= MIN_TALL_HEIGHT && footprint >= MIN_TALL_FOOTPRINT
			&& name.IndexOf("chimney") < 0 && name.IndexOf("mast") < 0 && name.IndexOf("pole") < 0
			&& name.IndexOf("antenna") < 0 && name.IndexOf("pylon") < 0 && name.IndexOf("crane") < 0;
		if (!tall && (footprint < MIN_FOOTPRINT_AREA || (height < MIN_HEIGHT && !ruin)))
			return KIND_SMALL;
		if (ruin)
			return KIND_RUIN;
		return KIND_BUILDING;
	}

	static bool Contains(vector center, vector pos)
	{
		return pos[0] >= center[0] - HALF && pos[0] < center[0] + HALF
			&& pos[2] >= center[2] - HALF && pos[2] < center[2] + HALF;
	}

	static int CellIndex(float coordinate)
	{
		return Math.Floor(coordinate / SIZE);
	}

	static vector Center(vector position)
	{
		return Vector(CellIndex(position[0]) * SIZE + HALF, 0,
			CellIndex(position[2]) * SIZE + HALF);
	}

	static string Key(vector center)
	{
		int x = CellIndex(center[0]);
		int z = CellIndex(center[2]);
		return x.ToString() + ":" + z.ToString();
	}

	static bool IsExcluded(vector center)
	{
		array<INS_SafeZone> safeZones = {};
		INS_SafeZone.CollectZones(safeZones);
		foreach (INS_SafeZone safeZone : safeZones)
		{
			if (safeZone.GetOwner() && Overlaps(center, safeZone.GetOwner().GetOrigin(), safeZone.GetRadius()))
				return true;
		}

		return false;
	}

	static bool IsSpawnBlocked(vector pos)
	{
		return IsExcluded(pos) || INF_SpawnProtection.Contains(pos);
	}

	// Position-only spawn gate shared by vehicle and infantry placement; run before any traces.
	static bool PositionAllowed(vector pos, array<vector> players, float minDistance, float maxDistance = float.MAX)
	{
		if (IsSpawnBlocked(pos))
			return false;
		float nearest = INF_BlockRules.Nearest(pos, players);
		return nearest >= minDistance && nearest <= maxDistance;
	}

	static bool Overlaps(vector center, vector circle, float radius)
	{
		float dx = Math.Max(Math.AbsFloat(center[0] - circle[0]) - HALF, 0);
		float dz = Math.Max(Math.AbsFloat(center[2] - circle[2]) - HALF, 0);
		return dx * dx + dz * dz <= radius * radius;
	}

	// Distinct group sizes of the pool, largest first; filled into cache once and reused.
	static array<int> Buckets(array<ResourceName> pool, array<int> cache)
	{
		if (!cache.IsEmpty() || !pool)
			return cache;
		foreach (ResourceName candidate : pool)
		{
			Resource resource = Resource.Load(candidate);
			if (!resource || !resource.IsValid())
				continue;
			IEntitySource source = resource.GetResource().ToEntitySource();
			if (!source || !source.GetClassName().ToType().IsInherited(SCR_AIGroup))
				continue;
			array<ResourceName> units = {};
			source.Get("m_aUnitPrefabSlots", units);
			int count = units.Count();
			if (count <= 0 || cache.Find(count) >= 0)
				continue;
			int at = cache.Count();
			for (int i = 0; i < cache.Count(); i++)
			{
				if (cache[i] < count)
				{
					at = i;
					break;
				}
			}
			cache.InsertAt(count, at);
		}
		return cache;
	}

	// Splits a target AI count into groups, filling from the largest bucket down.
	static void Plan(int target, array<int> buckets, array<int> sizes)
	{
		sizes.Clear();
		int remaining = target;
		while (remaining > 0)
		{
			int pick = remaining;
			foreach (int rank, int bucket : buckets)
			{
				if (bucket > remaining)
					continue;
				// Chance to skip a bucket falls linearly from SKIP_CHANCE at the largest to 0 at the smallest.
				float skipChance = 0;
				if (buckets.Count() > 1)
					skipChance = SKIP_CHANCE * (buckets.Count() - 1 - rank) / (buckets.Count() - 1);
				if (Math.RandomFloat(0, 100) < skipChance)
					continue;
				pick = bucket;
				break;
			}
			sizes.Insert(pick);
			remaining -= pick;
		}
	}

	static IEntity Spawn(vector pos, int members, array<ResourceName> configuredPrefabs)
	{
		if (!configuredPrefabs || configuredPrefabs.IsEmpty())
		{
			Print("[INF_BuildingGrid] Infantry prefab pool is empty; configure the owning patrol/counterattack manager.", LogLevel.ERROR);
			return null;
		}
		array<ResourceName> exact = {};
		array<ResourceName> larger = {};
		foreach (ResourceName candidate : configuredPrefabs)
		{
			Resource candidateResource = Resource.Load(candidate);
			if (!candidateResource || !candidateResource.IsValid())
			{
				Print("[INF_BuildingGrid] Cannot load configured infantry prefab: " + candidate, LogLevel.ERROR);
				continue;
			}
			IEntitySource source = candidateResource.GetResource().ToEntitySource();
			if (!source || !source.GetClassName().ToType().IsInherited(SCR_AIGroup))
			{
				Print("[INF_BuildingGrid] Configured infantry prefab is not an SCR_AIGroup: " + candidate, LogLevel.ERROR);
				continue;
			}
			array<ResourceName> units = {};
			source.Get("m_aUnitPrefabSlots", units);
			if (units.Count() == members)
				exact.Insert(candidate);
			else if (units.Count() > members)
				larger.Insert(candidate);
		}
		if (exact.IsEmpty())
			exact = larger;
		if (exact.IsEmpty())
		{
			Print(string.Format("[INF_BuildingGrid] Configured infantry pool has no group with at least %1 members; refusing an unconfigured faction fallback.", members), LogLevel.ERROR);
			return null;
		}
		ResourceName prefab = exact[Math.RandomInt(0, exact.Count())];
		Resource resource = Resource.Load(prefab);
		if (!resource || !resource.IsValid())
		{
			Print("[INF_BuildingGrid] Cannot load infantry prefab: " + prefab, LogLevel.ERROR);
			return null;
		}
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		Math3D.MatrixIdentity4(params.Transform);
		params.Transform[3] = pos;
		IEntity entity = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params);
		if (!entity)
		{
			Print("[INF_BuildingGrid] Failed to spawn infantry group.", LogLevel.ERROR);
			return null;
		}
		SCR_AIGroup group = SCR_AIGroup.Cast(entity);
		if (!group || !group.m_aUnitPrefabSlots || group.m_aUnitPrefabSlots.Count() < members
			|| group.GetAgentsCount() > members)
		{
			Print("[INF_BuildingGrid] Configured infantry group cannot satisfy its reserved size: " + prefab, LogLevel.ERROR);
			SCR_EntityHelper.DeleteEntityAndChildren(entity);
			return null;
		}
		// OnInit queues expansion; trim the instance roster before queued members spawn.
		group.m_aUnitPrefabSlots.Resize(members);
		group.SetMaxUnitsToSpawn(members);
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		if (persistence)
			persistence.StopTracking(entity, true);
		return entity;
	}
}

modded class INS_AmbientPatrolZoneState
{
	bool m_bINFBuildingCell;
	ref INF_ForestCampSite m_INFCamp;
	bool m_bINFObjectiveCaptured;
	int m_iINFBuildings;
	int m_iINFRuins;
	ref array<vector> m_aINFBuildingPositions = {};
	int m_iINFTargetAI;
	ref array<int> m_aINFPlan = {};
	int m_iINFPlannedAI;
	int m_iINFReservedAI;
	int m_iINFRememberedAI = -1;
	float m_fINFRememberedAt = -1;
	int m_iINFSpawnSize;
	int m_iINFBaseGroups = 1;
	int m_iINFPendingGroups;
	int m_iINFFailedActivations;
	bool m_bINFSpawnExhausted;
	bool m_bINFAttemptCountable;
	bool m_bINFInboundWaiting;
	bool m_bINFAttackSpent;
	bool m_bINFAttackActive;
	bool m_bINFAttackPending;
	int m_iINFAttackAttempts;
	float m_fINFAttackRetryAt;
	bool m_bINFPermanent;
	float m_fINFRetakeSince = -1;

	float m_fINFSpawnReadyAt;
	float m_fINFDistantSince = -1;
	int m_iINFActivationGeneration;
	float m_fINFCaptureProgress;
	float m_fINFCaptureUpdatedAt = -1;

	bool m_bINFMarkedCleared;
	bool m_bINFCaptureCandidate;
	// Roadblocks whose guards belong to this zone, and the ones planned into the current activation (the plan's tail).
	ref array<INF_Roadblock> m_aINFRoadblocks = {};
	ref array<INF_Roadblock> m_aINFActiveRoadblocks = {};
	INF_Roadblock m_INFSpawnRoadblock;

	bool INF_IsCapturing()
	{
		return m_fINFCaptureProgress > 0;
	}

	void INF_ResetCapture()
	{
		m_fINFCaptureProgress = 0;
		m_fINFCaptureUpdatedAt = -1;
	}
}

modded class INS_AmbientPatrolManager
{
	protected ref array<int> m_aINFGroupBuckets = {};
	[Attribute("0", UIWidgets.CheckBox, "Replace ambient location patrols with building-based 100m cells.")]
	protected bool m_bINFBuildingGrid;
	[Attribute("1", UIWidgets.EditBox, "Reusable terrain-index revision. Increment after changing terrain/static map content without changing the world resource. Does not reset mission progress.", category: "Terrain Index")]
	protected int m_iINFTerrainIndexRevision = 1;

	int INF_GetTerrainIndexRevision()
	{
		return m_iINFTerrainIndexRevision;
	}
	[Attribute("{8809D070C43CD78C}Prefabs/Compositions/Slotted/SlotFlatSmall/PlayerHub_S_USSR_01.et", UIWidgets.ResourceNamePicker, "Persistent camp composition spawned at mission startup. Default: USSR Encampment. Use a static composition without AI groups; zone defenders are managed separately.", "et", category: "Forest Camps")]
	protected ResourceName m_sINFCampComposition;
	[Attribute("US", UIWidgets.EditBox, "Mission player faction key. Permanently secured camps and their spawn points transfer to this faction, not the captor's faction.", category: "Forest Camps")]
	protected string m_sINFPlayerFaction = "US";
	[Attribute("9", UIWidgets.Slider, "Camp footprint half-width in metres. Set this to cover the selected composition, including its children. Mission-start setting; recovered sites must also pass this footprint.", "3 24 1", category: "Forest Camps")]
	protected float m_fINFCampFootprintRadius;
	protected int m_iINFCampCount = 7;
	protected ref INF_ActivationPolicy m_INFDecisionPolicy;
	protected float m_fINFNextProximityAt;
	protected bool m_bINFPatrolStopped;

	// Wakes the shared zone loop; the proximity work itself runs from INF_StepProximity.
	void INF_ScheduleProximity()
	{
		if (!Replication.IsServer() || !m_bINFBuildingGrid || !m_bEnabled
			|| m_bINFPatrolStopped || !m_INFZoneStates)
			return;
		m_INFZoneStates.WakeLoop();
	}
	protected ref INF_ForestCamps m_INFForestCamps;
	[RplProp()]
	protected bool m_bINFCampsReady;
	[RplProp()]
	protected string m_sINFStartupStatus;
	protected string m_sINFStartupFailure;
	protected int m_iINFScanTileTotal;
	protected float m_fINFNextStartupHint;
	protected ref array<vector> m_aINFScanTiles = {};
	protected vector m_vINFScanMin;
	protected vector m_vINFScanMax;
	protected vector m_vINFScanTile;
	static const int INF_SCAN_TILE_SIZE = 400;
	protected ref INF_ZoneStateSystem m_INFZoneStates;
	protected ref array<ref INF_ForestCampSite> m_INFCampSites = {};
	[Attribute("2", UIWidgets.Slider, "Non-stacking camp/cache influence multiplier for future zone defenders and counterattacks.", "1 4 1", category: "Grid Zones"), RplProp()]
	protected int m_iINFSiteStrengthMultiplier = 2;

	ResourceName INF_GetCampComposition()
	{
		return m_sINFCampComposition;
	}

	array<ResourceName> INF_GetGroupPrefabs()
	{
		return m_aGroupPrefabs;
	}

	[Attribute("15", UIWidgets.Slider, "Chance per activating zone that it owns a mortar battery. Zero disables batteries. Neighbouring zones roll independently, so the chance per cluster is higher.", "0 100 1", category: "Mortar Batteries"), RplProp()]
	protected int m_iINFMortarChance = 15;
	[Attribute("2", UIWidgets.Slider, "Maximum mortar batteries alive at once. Zero disables batteries.", "0 4 1", category: "Mortar Batteries"), RplProp()]
	protected int m_iINFMortarLimit = 2;
	[Attribute("{28D63631873AA636}Prefabs/Compositions/Slotted/SlotFlatSmall/MortarPlacement_S_USSR_01.et", UIWidgets.ResourceNamePicker, "Static composition holding the battery's mortars. Each mortar needs an AI vehicle usage component. Do not include AI groups; gunners and guards are spawned separately.", "et", category: "Mortar Batteries")]
	protected ResourceName m_sINFMortarComposition;
	protected ref INF_MortarBatteries m_INFMortars;
	protected ref INF_MortarConfig m_INFMortarConfig;

	[Attribute("25", UIWidgets.Slider, "Percentage of viable roadblock (road slot) and defence (small flat slot) positions that receive a site at a fresh mission start, then saved and restored. Zero disables both.", "0 100 5", category: "Roadblocks")]
	protected int m_iINFRoadblockPercent = 25;
	[Attribute("800", UIWidgets.Slider, "Minimum distance in metres between roadblocks (defences are not spaced).", "200 3000 50", category: "Roadblocks")]
	protected float m_fINFRoadblockSpacing = 800;
	[Attribute("1000", UIWidgets.Slider, "A site's guards belong to the nearest zone within this many metres. Sites with no zone in range are not placed.", "200 3000 50", category: "Roadblocks")]
	protected float m_fINFRoadblockOwnerDistance = 1000;
	[Attribute("2", UIWidgets.Slider, "Maximum roadblocks and defences guarded by one zone.", "1 4 1", category: "Roadblocks")]
	protected int m_iINFRoadblocksPerZone = 2;
	[Attribute("4", UIWidgets.Slider, "Soldiers in each roadblock guard group (defences use 2 to 4). They count as defenders of the owning zone.", "2 8 1", category: "Roadblocks")]
	protected int m_iINFRoadblockGuards = 4;
	[Attribute("", UIWidgets.ResourceNamePicker, "Compositions to choose from, under Prefabs/Compositions/Slotted: SlotRoad* for roadblocks, SlotFlatSmall for defences (use the editable PrefabsEditable E_ variants so a Game Master can edit them). Empty disables roadblocks and defences.", "et", category: "Roadblocks")]
	protected ref array<ResourceName> m_aINFRoadblockCompositions;
	protected ref INF_Roadblocks m_INFRoadblocks;

	protected INF_RoadblockConfig INF_BuildRoadblockConfig()
	{
		INF_RoadblockConfig config = new INF_RoadblockConfig();
		config.m_iPercent = m_iINFRoadblockPercent;
		config.m_fSpacing = m_fINFRoadblockSpacing;
		config.m_fMaxOwnerDistance = m_fINFRoadblockOwnerDistance;
		config.m_iPerZone = m_iINFRoadblocksPerZone;
		if (m_aINFRoadblockCompositions)
			config.m_aComposition.Copy(m_aINFRoadblockCompositions);
		return config;
	}

	// Polled by the static-site startup until the roadblock roster is placed or restored.
	protected bool INF_RoadblocksReady()
	{
		if (!m_INFRoadblocks)
		{
			m_INFRoadblocks = new INF_Roadblocks();
			array<vector> avoid = {};
			foreach (INF_ForestCampSite campSite : m_INFCampSites)
				avoid.Insert(campSite.m_Position);
			m_INFRoadblocks.Begin(m_INFZoneStates, INF_BuildRoadblockConfig(), avoid);
		}
		return m_INFRoadblocks.IsReady();
	}

	INF_MortarConfig INF_GetMortarConfig()
	{
		if (!m_INFMortarConfig)
			m_INFMortarConfig = new INF_MortarConfig();
		m_INFMortarConfig.m_iChance = m_iINFMortarChance;
		m_INFMortarConfig.m_iLimit = m_iINFMortarLimit;
		m_INFMortarConfig.m_Composition = m_sINFMortarComposition;
		return m_INFMortarConfig;
	}

	INF_MortarBatteries INF_GetMortars()
	{
		if (!m_INFMortars)
			m_INFMortars = new INF_MortarBatteries();
		return m_INFMortars;
	}

	int INF_GetCampCount()
	{
		return m_iINFCampCount;
	}

	override protected void ApplyMissionHeader()
	{
		super.ApplyMissionHeader();
		if (!m_bINFBuildingGrid)
			return;
		SCR_MissionHeader header = SCR_MissionHeader.Cast(GetGame().GetMissionHeader());
		if (!header || !header.m_Insurgency)
		{
			Print("[INF_ForestCamps] Mission header is unavailable; using the default of 7 camps.", LogLevel.WARNING);
			return;
		}
		int count = header.m_Insurgency.m_iINFCampCount;
		if (count < 1 || count > INF_ForestCamps.MAX_CAMPS)
			Print("[INF_ForestCamps] Header m_Insurgency.m_iINFCampCount must be 1-20; using the default of 7 camps.", LogLevel.ERROR);
		else
			m_iINFCampCount = count;
		INF_ApplyHeaderSettings(header.m_Insurgency);
	}

	protected void INF_ApplyHeaderSettings(INS_MissionParams params)
	{
		INF_HeaderSettings.Int(params.m_iINFTerrainIndexRevision, 1, 1000000, "m_iINFTerrainIndexRevision", m_iINFTerrainIndexRevision);
		if (!params.m_sINFCampComposition.IsEmpty())
			m_sINFCampComposition = params.m_sINFCampComposition;
		if (!params.m_sINFPlayerFaction.IsEmpty())
			m_sINFPlayerFaction = params.m_sINFPlayerFaction;
		INF_HeaderSettings.Float(params.m_fINFCampFootprintRadius, 3, 24, "m_fINFCampFootprintRadius", m_fINFCampFootprintRadius);
		INF_HeaderSettings.Int(params.m_iINFSiteStrengthMultiplier, 1, 4, "m_iINFSiteStrengthMultiplier", m_iINFSiteStrengthMultiplier);
		INF_HeaderSettings.Int(params.m_iINFMortarChance, 0, 100, "m_iINFMortarChance", m_iINFMortarChance);
		INF_HeaderSettings.Int(params.m_iINFMortarLimit, 0, 4, "m_iINFMortarLimit", m_iINFMortarLimit);
		if (!params.m_sINFMortarComposition.IsEmpty())
			m_sINFMortarComposition = params.m_sINFMortarComposition;
		INF_HeaderSettings.Int(params.m_iINFRoadblockPercent, 0, 100, "m_iINFRoadblockPercent", m_iINFRoadblockPercent);
		INF_HeaderSettings.Float(params.m_fINFRoadblockSpacing, 200, 3000, "m_fINFRoadblockSpacing", m_fINFRoadblockSpacing);
		INF_HeaderSettings.Float(params.m_fINFRoadblockOwnerDistance, 200, 3000, "m_fINFRoadblockOwnerDistance", m_fINFRoadblockOwnerDistance);
		INF_HeaderSettings.Int(params.m_iINFRoadblocksPerZone, 1, 4, "m_iINFRoadblocksPerZone", m_iINFRoadblocksPerZone);
		INF_HeaderSettings.Int(params.m_iINFRoadblockGuards, 2, 8, "m_iINFRoadblockGuards", m_iINFRoadblockGuards);

		float enterSpeed = m_fINFTravelEnterSpeed;
		float exitSpeed = m_fINFTravelExitSpeed;
		float enterSeconds = m_fINFTravelEnterSeconds;
		float exitSeconds = m_fINFTravelExitSeconds;
		INF_HeaderSettings.Float(params.m_fINFTravelEnterSpeed, 1, 200, "m_fINFTravelEnterSpeed", enterSpeed);
		INF_HeaderSettings.Float(params.m_fINFTravelExitSpeed, 0, 199, "m_fINFTravelExitSpeed", exitSpeed);
		INF_HeaderSettings.Float(params.m_fINFTravelEnterSeconds, 1, 180, "m_fINFTravelEnterSeconds", enterSeconds);
		INF_HeaderSettings.Float(params.m_fINFTravelExitSeconds, 1, 180, "m_fINFTravelExitSeconds", exitSeconds);
		if (enterSpeed <= exitSpeed)
			Print("[INF_Header] Travel entry speed must exceed exit speed; keeping the game mode travel settings.", LogLevel.ERROR);
		else
		{
			m_fINFTravelEnterSpeed = enterSpeed;
			m_fINFTravelExitSpeed = exitSpeed;
			m_fINFTravelEnterSeconds = enterSeconds;
			m_fINFTravelExitSeconds = exitSeconds;
		}

		INS_AmbientVehicleManager vehicles = INS_AmbientVehicleManager.Cast(GetOwner().FindComponent(INS_AmbientVehicleManager));
		if (vehicles)
			vehicles.INF_ApplyHeaderSettings(params);
		INS_IntelManager intel = INS_IntelManager.Cast(GetOwner().FindComponent(INS_IntelManager));
		if (intel)
			intel.INF_ApplyHeaderSettings(params);
	}

	Faction INF_GetPlayerFaction()
	{
		return GetGame().GetFactionManager().GetFactionByKey(m_sINFPlayerFaction);
	}

	protected void INF_OnCampCaptured(INS_AmbientPatrolZoneState state)
	{
		if (!Replication.IsServer() || !state.m_INFCamp || !state.m_bINFPermanent)
			return;
		if (!m_INFForestCamps.MakeFriendly(state.m_INFCamp))
		{
			m_CallQueue.CallLater(INF_RetryCampCapture, 5000, false, state);
			return;
		}
		state.m_bINFObjectiveCaptured = true;
	}

	protected void INF_RetryCampCapture(INS_AmbientPatrolZoneState state)
	{
		INF_OnCampCaptured(state);
		if (state.m_bINFPermanent && state.m_bINFObjectiveCaptured && state.m_INFCamp && state.m_INFCamp.m_Friendly)
			m_INFZoneStates.CommitCaptureState(state);
	}

	float INF_GetCampFootprintRadius()
	{
		return m_fINFCampFootprintRadius;
	}

	INF_ZoneStateSystem INF_GetZoneStates()
	{
		return m_INFZoneStates;
	}

	int INF_GetSiteStrengthMultiplier()
	{
		return m_iINFSiteStrengthMultiplier;
	}

	void INF_SetSiteStrengthMultiplier(int value)
	{
		if (!Replication.IsServer())
			return;
		if (value < 1 || value > 4)
		{
			Print("[INF_Zones] Site strength multiplier must be between 1 and 4.", LogLevel.ERROR);
			return;
		}
		m_iINFSiteStrengthMultiplier = value;
		Replication.BumpMe();
	}

	int INF_ZoneMultiplier(INS_AmbientPatrolZoneState state)
	{
		if (m_INFZoneStates && m_INFZoneStates.IsInfluenced(state))
			return m_iINFSiteStrengthMultiplier;
		return 1;
	}

	bool INF_UsesBuildingGrid()
	{
		return m_bINFBuildingGrid;
	}

	[Attribute("50", UIWidgets.Slider, "Sustained speed to enter travel efficiency (km/h).", "1 200 1", category: "Travel Efficiency"), RplProp()]
	protected float m_fINFTravelEnterSpeed = 50;
	[Attribute("30", UIWidgets.Slider, "Speed below which normal activation can resume (km/h).", "0 199 1", category: "Travel Efficiency"), RplProp()]
	protected float m_fINFTravelExitSpeed = 30;
	[Attribute("20", UIWidgets.Slider, "Continuous seconds at or above the entry speed.", "1 180 1", category: "Travel Efficiency"), RplProp()]
	protected float m_fINFTravelEnterSeconds = 20;
	[Attribute("15", UIWidgets.Slider, "Continuous seconds below the exit speed.", "1 180 1", category: "Travel Efficiency"), RplProp()]
	protected float m_fINFTravelExitSeconds = 15;

	float INF_GetTravelSetting(int setting)
	{
		switch (setting)
		{
			case 0: return m_fINFTravelEnterSpeed;
			case 1: return m_fINFTravelExitSpeed;
			case 2: return m_fINFTravelEnterSeconds;
			case 3: return m_fINFTravelExitSeconds;
		}
		Print("[INF_Activation] Unknown travel-efficiency setting.", LogLevel.ERROR);
		return 0;
	}

	void INF_SetTravelSetting(int setting, float value)
	{
		if (!Replication.IsServer())
			return;
		float minimum = 1;
		float maximum = 180;
		if (setting == 0)
			maximum = 200;
		if (setting == 1)
		{
			minimum = 0;
			maximum = 199;
		}
		if (setting < 0 || setting > 3 || !(value >= minimum && value <= maximum)
			|| (setting == 0 && value <= m_fINFTravelExitSpeed)
			|| (setting == 1 && value >= m_fINFTravelEnterSpeed))
		{
			Print("[INF_Activation] Invalid travel setting: entry must exceed exit speed; speeds 1-200/0-199 km/h and durations 1-180 seconds.", LogLevel.ERROR);
			return;
		}
		switch (setting)
		{
			case 0: m_fINFTravelEnterSpeed = value; break;
			case 1: m_fINFTravelExitSpeed = value; break;
			case 2: m_fINFTravelEnterSeconds = value; break;
			case 3: m_fINFTravelExitSeconds = value; break;
		}
		INF_ActivationPolicy.ResetTravel();
		if (m_INFZoneStates)
			m_INFZoneStates.InvalidatePresence();
		Replication.BumpMe();
		Print(string.Format("[INF_Activation] Travel efficiency: enter at %1km/h for %2s; exit below %3km/h for %4s. Travel timers reset.",
			m_fINFTravelEnterSpeed, m_fINFTravelEnterSeconds, m_fINFTravelExitSpeed, m_fINFTravelExitSeconds));
	}

	override void OnGameModeStart()
	{
		if (!m_bINFBuildingGrid)
		{
			super.OnGameModeStart();
			return;
		}
		if (!System.IsConsoleApp())
			GetGame().GetCallqueue().CallLater(INF_NotifyStartupProgress, 5000, true);
		if (!Replication.IsServer())
			return;
		ApplyMissionHeader();
		m_fCheckInterval = Math.Max(m_fCheckInterval, 5);
		DiscoverZones();
		INF_PublishStartupProgress();
		GetGame().GetCallqueue().CallLater(INF_PublishStartupProgress, 5000, true);
		GetGame().GetCallqueue().CallLater(TickCallQueue, 100, true);
		if (m_bEnabled)
			INF_ScheduleProximity();
	}

	override void OnPlayerConnected(int playerId)
	{
		super.OnPlayerConnected(playerId);
		if (Replication.IsServer() && m_INFZoneStates)
		{
			m_INFZoneStates.InvalidatePresence();
			m_INFZoneStates.WakeLoop();
		}
	}

	override protected void DiscoverZones()
	{
		if (!m_bINFBuildingGrid)
		{
			super.DiscoverZones();
			return;
		}
		m_INFForestCamps = new INF_ForestCamps(this);
		INF_ForestCampRewards.BeginDiscovery();
		INS_IntelManager intel = INS_IntelManager.Cast(GetOwner().FindComponent(INS_IntelManager));
		if (!intel)
		{
			m_sINFStartupFailure = "Mission initialisation is blocked: intel manager missing. An administrator needs to check the server log.";
			Print("[INF_Zones] Intel manager missing; cannot initialise progressive zones.", LogLevel.ERROR);
			return;
		}
		m_INFZoneStates = new INF_ZoneStateSystem();
		m_INFZoneStates.GetOnCaptured().Insert(INF_OnCampCaptured);
		m_INFZoneStates.GetOnObjectivesChanged().Insert(intel.INF_QueueCampaignObjectives);
		m_aZones = m_INFZoneStates.GetZones();
		m_INFZoneStates.Start(GetOwner());
		INS_GarrisonManager areas = INS_GarrisonManager.Cast(GetOwner().FindComponent(INS_GarrisonManager));
		if (areas)
			areas.INF_BindZoneStates(m_INFZoneStates);
		if (!m_INFZoneStates.IsPersistenceReady())
		{
			m_sINFStartupFailure = "Territory save recovery failed. An administrator needs to check the server log and saved snapshots.";
			return;
		}
		if (m_INFForestCamps.LoadMapIndex())
		{
			m_INFForestCamps.AddBuildingZones(m_INFZoneStates);
			Print(string.Format("[INF_Zones] Terrain index reused; %1 building zones available while mission sites initialise.", m_aZones.Count()));
			m_INFForestCamps.Begin();
			return;
		}
		GetGame().GetWorld().GetBoundBox(m_vINFScanMin, m_vINFScanMax);
		int firstX = Math.Floor(m_vINFScanMin[0] / INF_SCAN_TILE_SIZE);
		int firstZ = Math.Floor(m_vINFScanMin[2] / INF_SCAN_TILE_SIZE);
		int endX = Math.Ceil(m_vINFScanMax[0] / INF_SCAN_TILE_SIZE);
		int endZ = Math.Ceil(m_vINFScanMax[2] / INF_SCAN_TILE_SIZE);
		for (int x = firstX; x < endX; x++)
		{
			for (int z = firstZ; z < endZ; z++)
				m_aINFScanTiles.Insert(Vector(x * INF_SCAN_TILE_SIZE, 0, z * INF_SCAN_TILE_SIZE));
		}
		Print(string.Format("[INF_Zones] Background terrain scan started: %1 tiles; discovered building zones become playable progressively.", m_aINFScanTiles.Count()));
		m_iINFScanTileTotal = m_aINFScanTiles.Count();
		GetGame().GetCallqueue().CallLater(INF_ScanMapTick, 50, true);
	}

	string INF_GetStartupStatus()
	{
		return m_sINFStartupStatus;
	}

	protected void INF_PublishStartupProgress()
	{
		if (!Replication.IsServer())
			return;
		string status;
		if (m_bINFCampsReady)
		{
			status = "Mission ready. Camps, caches and objectives are initialised. Check your tasks.";
			GetGame().GetCallqueue().Remove(INF_PublishStartupProgress);
		}
		else if (!m_sINFStartupFailure.IsEmpty())
			status = m_sINFStartupFailure;
		else if (!m_aINFScanTiles.IsEmpty())
		{
			int scanned = m_iINFScanTileTotal - m_aINFScanTiles.Count();
			status = string.Format("Surveying terrain: %1/%2 areas checked. Discovered building zones are playable; camps and caches are still preparing.",
				scanned, m_iINFScanTileTotal);
		}
		else if (m_INFForestCamps)
			status = m_INFForestCamps.GetStartupStatus();
		else
			status = "Preparing mission systems. Startup progress will appear shortly.";
		if (status == m_sINFStartupStatus)
			return;
		m_sINFStartupStatus = status;
		Replication.BumpMe();
		INS_IntelManager intel = INS_IntelManager.Cast(GetOwner().FindComponent(INS_IntelManager));
		if (intel)
			intel.INF_QueueCampaignObjectives();
	}

	protected void INF_NotifyStartupProgress()
	{
		if (m_bINFCampsReady)
		{
			GetGame().GetCallqueue().Remove(INF_NotifyStartupProgress);
			return;
		}
		PlayerController local = GetGame().GetPlayerController();
		if (!local || !local.GetControlledEntity() || m_sINFStartupStatus.IsEmpty())
			return;
		float now = GetGame().GetWorld().GetWorldTime();
		if (now < m_fINFNextStartupHint)
			return;
		SCR_HintManagerComponent.ShowCustomHint(m_sINFStartupStatus, "Mission initialisation", 8);
		m_fINFNextStartupHint = now + 30000;
	}

	protected void INF_ScanMapTick()
	{
		if (!Replication.IsServer())
			return;
		if (m_aINFScanTiles.IsEmpty())
		{
			GetGame().GetCallqueue().Remove(INF_ScanMapTick);
			m_INFForestCamps.SaveMapIndex();
			Print(string.Format("[INF_Zones] Background terrain scan complete; %1 building zones. Selecting mission sites.", m_aZones.Count()));
			m_INFForestCamps.Begin();
			return;
		}
		array<vector> players = {};
		players.Copy(m_INFZoneStates.GetPresence().m_AllPlayers);
		if (players.IsEmpty())
			players.Insert(GetOwner().GetOrigin());
		int nearest;
		vector offset = Vector(INF_SCAN_TILE_SIZE * 0.5, 0, INF_SCAN_TILE_SIZE * 0.5);
		float nearestDistance = INF_BlockRules.Nearest(m_aINFScanTiles[0] + offset, players);
		for (int i = 1; i < m_aINFScanTiles.Count(); i++)
		{
			float tileDistance = INF_BlockRules.Nearest(m_aINFScanTiles[i] + offset, players);
			if (tileDistance < nearestDistance)
			{
				nearest = i;
				nearestDistance = tileDistance;
			}
		}
		m_vINFScanTile = m_aINFScanTiles[nearest];
		vector minimum = Vector(m_vINFScanTile[0], m_vINFScanMin[1], m_vINFScanTile[2]);
		vector maximum = Vector(m_vINFScanTile[0] + INF_SCAN_TILE_SIZE, m_vINFScanMax[1], m_vINFScanTile[2] + INF_SCAN_TILE_SIZE);
		GetGame().GetWorld().QueryEntitiesByAABB(minimum, maximum, INF_IndexBuilding);
		m_aINFScanTiles.Remove(nearest);
		m_INFForestCamps.AddBuildingZones(m_INFZoneStates);
	}

	void INF_FinishCampDiscovery(array<ref INF_ForestCampSite> sites)
	{
		foreach (INF_ForestCampSite site : sites)
		{
			site.m_RewardID = INF_ForestCampRewards.RegisterSite(site.m_Position, site.m_Center, site.m_Yaw, site.m_Key);
			m_INFCampSites.Insert(site);
		}
		INF_ForestCampRewards.CompleteDiscovery();
	}

	void INF_StaticCampLayoutsReady()
	{
		if (m_bINFCampsReady)
			return;
		INS_IntelManager intel = INS_IntelManager.GetInstance();
		if (!intel)
		{
			Print("[INF_Zones] Intel manager missing; static-site startup cannot finish.", LogLevel.ERROR);
			return;
		}
		if (!intel.INF_IsIntelReady())
			return;
		if (!INF_RoadblocksReady())
			return;
		foreach (INF_ForestCampSite site : m_INFCampSites)
			m_INFZoneStates.AddCamp(site);
		if (!intel.INF_RegisterZoneCaches(m_INFZoneStates))
		{
			return;
		}
		if (!m_INFZoneStates.RestorePermanentCamps())
		{
			m_sINFStartupFailure = "Restoring secured territory is pending. An administrator needs to check camp ownership and territory recovery errors in the server log.";
			return;
		}
		m_sINFStartupFailure = string.Empty;
		INS_GarrisonManager areas = INS_GarrisonManager.Cast(GetOwner().FindComponent(INS_GarrisonManager));
		if (areas)
		{
			array<string> savedOnly = {};
			m_INFZoneStates.CollectSavedOnlyCells(savedOnly);
			areas.INF_ShowSavedCells(savedOnly);
			areas.INF_ShowSafeZones();
		}
		Print(string.Format("[INF_Zones] Static camps/caches ready; created %1 unique grid zones; shared AI budget=%2.",
			m_aZones.Count(), INF_BuildingGrid.ACTIVE_BUDGET));
		m_bINFCampsReady = true;
		INF_PublishStartupProgress();
		intel.INF_QueueCampaignObjectives();
		intel.INF_DebugContentChanged();
	}

	bool INF_AreZonesReady()
	{
		return !m_bINFBuildingGrid || m_INFZoneStates != null;
	}

	bool INF_AreCampsReady()
	{
		return !m_bINFBuildingGrid || m_bINFCampsReady;
	}

	override void OnGameModeEnd(SCR_GameModeEndData data)
	{
		m_bINFPatrolStopped = true;
		if (m_CallQueue)
		{
			m_CallQueue.Remove(INF_OnCampCaptured);
			m_CallQueue.Remove(INF_RetryCampCapture);
		}
		GetGame().GetCallqueue().Remove(INF_NotifyStartupProgress);
		GetGame().GetCallqueue().Remove(INF_PublishStartupProgress);
		GetGame().GetCallqueue().Remove(INF_ScanMapTick);
		m_aINFScanTiles.Clear();
		if (m_INFZoneStates)
		{
			m_INFZoneStates.Stop();
			m_INFZoneStates.GetOnCaptured().Remove(INF_OnCampCaptured);
			INS_IntelManager intel = INS_IntelManager.GetInstance();
			if (intel)
				m_INFZoneStates.GetOnObjectivesChanged().Remove(intel.INF_QueueCampaignObjectives);
		}
		if (m_INFForestCamps)
			m_INFForestCamps.Stop();
		if (m_INFMortars)
			m_INFMortars.Stop();
		if (m_INFRoadblocks)
			m_INFRoadblocks.Stop();
		super.OnGameModeEnd(data);
	}

	protected bool INF_IndexBuilding(IEntity entity)
	{
		// AABB queries can return the same large entity in adjacent tiles.
		vector pos = entity.GetOrigin();
		if (pos[0] < m_vINFScanTile[0] || pos[0] >= m_vINFScanTile[0] + INF_SCAN_TILE_SIZE
			|| pos[2] < m_vINFScanTile[2] || pos[2] >= m_vINFScanTile[2] + INF_SCAN_TILE_SIZE)
			return true;
		if (m_INFForestCamps)
			m_INFForestCamps.Index(entity);
		return true;
	}

	int INF_ReservedAI()
	{
		int total = 0;
		if (!m_INFZoneStates)
			return total;
		foreach (INS_AmbientPatrolZoneState state : m_INFZoneStates.GetActiveZones())
		{
			if (state.m_bINFBuildingCell && state.m_bActive)
				total += state.m_iINFReservedAI;
		}
		return total;
	}

	override protected void ActivateZone(INS_AmbientPatrolZoneState state)
	{
		if (state.m_bINFBuildingCell)
		{
			if (!m_INFZoneStates || !m_INFZoneStates.IsPersistenceReady())
				return;
			if (state.m_bINFSpawnExhausted || state.m_bINFPermanent || state.m_bINFMarkedCleared || state.m_bINFAttackActive || state.m_bINFAttackSpent || state.m_bINFAttackPending)
				return;
			if (state.m_INFCamp && !state.m_INFCamp.m_LayoutReady)
				return;
			int fullAI = state.m_iINFTargetAI * INF_ZoneMultiplier(state);
			int limitAI = fullAI;
			float activationTime = GetGame().GetWorld().GetWorldTime();
			// Recently despawned zones come back with only their survivors; old memory regenerates to full.
			if (state.m_iINFRememberedAI >= 0
				&& activationTime - state.m_fINFRememberedAt < INF_BuildingGrid.DEFENDER_MEMORY_MS)
			{
				// A wiped-out zone stays empty (but capturable) until the memory expires, unless roadblock guards survive.
				if (state.m_iINFRememberedAI == 0 && !INF_Roadblocks.HasRememberedGuards(state, activationTime))
				{
					if (!state.m_bEverHadLiving)
					{
						state.m_bEverHadLiving = true;
						m_INFZoneStates.NotifyChanged(state);
					}
					return;
				}
				limitAI = Math.Min(fullAI, state.m_iINFRememberedAI);
			}
			INF_BuildingGrid.Plan(limitAI,
				INF_BuildingGrid.Buckets(m_aGroupPrefabs, m_aINFGroupBuckets), state.m_aINFPlan);
			INF_Roadblocks.AppendGuards(state, m_iINFRoadblockGuards, activationTime);
			state.m_iGroupCount = state.m_aINFPlan.Count();
			state.m_iINFPlannedAI = 0;
			foreach (int planned : state.m_aINFPlan)
				state.m_iINFPlannedAI += planned;
			if (!INF_MakeRoom(state))
			{
				return;
			}
			INF_Roadblocks.ConsumeMemory(state);
			state.m_fINFDistantSince = -1;
			state.INF_ResetCapture();
			state.m_iINFRememberedAI = -1;
			state.m_fINFSpawnReadyAt = GetGame().GetWorld().GetWorldTime() + Math.Max(5000, state.m_iGroupCount * INF_StaggerGate.GAP_MS + 2000 + m_INFSpawnGate.Backlog());
			state.m_iINFActivationGeneration++;
			state.m_bActive = true;
			state.m_bEverHadLiving = false;
			state.m_bINFAttemptCountable = true;
			state.m_iINFPendingGroups = state.m_iGroupCount;
			state.m_iINFReservedAI = state.m_iINFPlannedAI;
			m_INFZoneStates.NotifyChanged(state);
			// Vehicle first so it takes an earlier spawn slot than the zone's infantry.
			INS_AmbientVehicleManager zoneVehicles = INS_AmbientVehicleManager.Cast(GetOwner().FindComponent(INS_AmbientVehicleManager));
			if (zoneVehicles)
				zoneVehicles.INF_OnZoneActivated(state);
			INF_GetMortars().OnZoneActivated(this, state, INF_GetMortarConfig());
			int guardGroups = state.m_aINFActiveRoadblocks.Count();
			for (int i = 0; i < state.m_iGroupCount; i++)
			{
				vector pos;
				if (i >= state.m_iGroupCount - guardGroups)
				{
					pos = state.m_aINFActiveRoadblocks[i - (state.m_iGroupCount - guardGroups)].m_vPosition;
				}
				else
				{
					pos = INS_ZoneHelpers.RandomPositionInRadius(state.m_vCenter, state.m_fRadius);
					if (i < state.m_aSavedPositions.Count())
						pos = state.m_aSavedPositions[i];
				}
				m_CallQueue.CallLater(INF_SpawnQueued, m_INFSpawnGate.Reserve(), false,
					pos, state, state.m_iINFActivationGeneration);
			}
			state.m_aSavedPositions.Clear();
			return;
		}
		super.ActivateZone(state);
	}

	override bool IsPositionInActiveZone(vector pos)
	{
		if (!m_bINFBuildingGrid)
			return super.IsPositionInActiveZone(pos);
		foreach (INS_AmbientPatrolZoneState state : m_INFZoneStates.GetActiveZones())
		{
			if (state.m_bActive && INF_BuildingGrid.Contains(state.m_vCenter, pos))
				return true;
		}
		return false;
	}

	override protected vector FindSafeSpawnPosition(vector candidate, INS_AmbientPatrolZoneState state)
	{
		if (!state.m_bINFBuildingCell)
			return super.FindSafeSpawnPosition(candidate, state);
		float minDistance = INF_BlockRules.MIN_GRID_SPAWN_DISTANCE;
		if (state.m_iINFBuildings >= INF_BlockRules.DENSE_GRID_BUILDINGS)
			minDistance = INF_BlockRules.MIN_DENSE_GRID_SPAWN_DISTANCE;
		array<vector> players = m_INFZoneStates.GetPresence(false, true).m_AllPlayers;
		for (int i = 0; i < 24; i++)
		{
			vector attempt = candidate;
			if (i > 0)
				attempt = state.m_vCenter + Vector(Math.RandomFloat(-49, 49), 0, Math.RandomFloat(-49, 49));
			vector empty;
			if (!SCR_WorldTools.FindEmptyTerrainPosition(empty, attempt, 5, 2, 2))
				continue;
			float playerDistance = INF_BlockRules.Nearest(empty, players);
			if (!INF_BuildingGrid.Contains(state.m_vCenter, empty) || empty[1] <= 0.5
				|| INF_SpawnProtection.Contains(empty) || playerDistance < minDistance)
				continue;
			if (playerDistance < m_fMinSpawnDistFromPlayer && !INF_IsConcealedSpawn(empty, players))
				continue;
			return empty;
		}
		return INF_FindReinforcementPosition(state.m_vCenter, players, minDistance);
	}

	protected bool INF_IsConcealedSpawn(vector pos, array<vector> players)
	{
		return INF_BlockRules.IsConcealed(pos, players, 1.5);
	}

	protected vector INF_FindReinforcementPosition(vector center, array<vector> players, float minDistance)
	{
		for (int i = 0; i < 80; i++)
		{
			float angle = Math.RandomFloat(0, Math.PI * 2);
			float distance;
			if (i < 40)
				distance = Math.RandomFloat(minDistance + 50, minDistance + 300);
			else
				distance = Math.RandomFloat(550, 900);
			vector candidate = center + Vector(Math.Cos(angle) * distance, 0, Math.Sin(angle) * distance);
			vector empty;
			if (!SCR_WorldTools.FindEmptyTerrainPosition(empty, candidate, 10, 2, 2)
				|| empty[1] <= 0.5 || !INF_BuildingGrid.PositionAllowed(empty, players, minDistance))
				continue;
			if (INF_IsConcealedSpawn(empty, players))
				return empty;
		}
		return vector.Zero;
	}

	// Roadblock guards stand at their roadblock, which may be outside the zone cell; the cell-containment rule does not apply.
	protected vector INF_FindRoadblockGuardPosition(INF_Roadblock roadblock)
	{
		float minDistance = INF_BlockRules.MIN_GRID_SPAWN_DISTANCE;
		array<vector> players = m_INFZoneStates.GetPresence(false, true).m_AllPlayers;
		for (int attempt = 0; attempt < 16; attempt++)
		{
			vector candidate = roadblock.m_vPosition + Vector(Math.RandomFloat(-8, 8), 0, Math.RandomFloat(-8, 8));
			vector empty;
			if (!SCR_WorldTools.FindEmptyTerrainPosition(empty, candidate, 5, 2, 2) || empty[1] <= 0.5 || INF_SpawnProtection.Contains(empty))
				continue;
			float playerDistance = INF_BlockRules.Nearest(empty, players);
			if (playerDistance < minDistance)
				continue;
			if (playerDistance < m_fMinSpawnDistFromPlayer && !INF_IsConcealedSpawn(empty, players))
				continue;
			return empty;
		}
		return vector.Zero;
	}

	override protected void SpawnGroupAt(vector pos, INS_AmbientPatrolZoneState state)
	{
		if (!state.m_bINFBuildingCell)
		{
			super.SpawnGroupAt(pos, state);
			return;
		}
		if (!state.m_bActive)
			return;
		INF_Roadblock guardedRoadblock = state.m_INFSpawnRoadblock;
		state.m_INFSpawnRoadblock = null;
		if (guardedRoadblock)
			pos = INF_FindRoadblockGuardPosition(guardedRoadblock);
		else
			pos = FindSafeSpawnPosition(pos, state);
		IEntity entity;
		int members = state.m_iINFSpawnSize;
		if (pos != vector.Zero && members > 0)
			entity = INF_BuildingGrid.Spawn(pos, members, m_aGroupPrefabs);
		if (!entity)
		{
			Print("[INF_BuildingGrid] Cell group spawn unavailable; retaining cooldown/retry semantics.", LogLevel.WARNING);
			return;
		}
		INS_PatrolAgent agent;
		if (guardedRoadblock)
		{
			agent = new INS_PatrolAgent(entity, guardedRoadblock.m_vPosition, INF_Roadblocks.GUARD_RADIUS);
			agent.m_INFRoadblock = guardedRoadblock;
		}
		else
		{
			agent = new INS_PatrolAgent(entity, state.m_vCenter, state.m_fRadius);
			agent.m_bINFInbound = !INF_BuildingGrid.Contains(state.m_vCenter, pos);
		}
		state.m_aAgents.Insert(agent);
		m_INFZoneStates.InvalidatePresence();
		m_CallQueue.CallLater(INF_InitDefenderGroup, 500, false, agent);
		// Guards hold the roadblock under a defend waypoint (which also mans turrets) instead of patrolling the cell.
		if (guardedRoadblock)
			m_CallQueue.CallLater(INF_DefendRoadblock, 1500, false, agent);
		else
			m_CallQueue.CallLater(PatrolTick, 1500, false, agent);
	}

	protected void INF_DefendRoadblock(INS_PatrolAgent agent)
	{
		if (!Replication.IsServer() || !agent.m_Group || !agent.m_INFRoadblock)
			return;
		agent.m_INFDefend = INF_Roadblocks.SpawnDefendWaypoint(agent.m_INFRoadblock.m_vPosition);
		if (!agent.m_INFDefend)
			return;
		// Waypoints already on the group (from group setup) would be walked first; the defend waypoint goes to the front.
		array<AIWaypoint> existing = {};
		agent.m_Group.GetWaypoints(existing);
		agent.m_Group.AddWaypointAt(agent.m_INFDefend, 0);
		INF_Roadblocks.Log(string.Format("Guard group at %1 had %2 waypoints before the defend waypoint was added.", agent.m_INFRoadblock.m_sKey, existing.Count()));
	}

	protected void INF_InitDefenderGroup(INS_PatrolAgent agent)
	{
		if (!Replication.IsServer() || !agent.m_Group)
			return;
		InitGroup(agent);
		if (!agent.m_bINFInbound || !agent.m_Group)
			return;
		SCR_AIGroupSettingsComponent settings = SCR_AIGroupSettingsComponent.Cast(agent.m_Group.FindComponent(SCR_AIGroupSettingsComponent));
		if (!settings)
			Print("[INF_Zones] Cannot set approaching defenders' running speed: group settings component missing.", LogLevel.ERROR);
		else if (!settings.AddSetting(SCR_AIGroupCharactersMovementSpeedSetting.Create(SCR_EAISettingOrigin.SCENARIO, EMovementType.RUN), false, true))
			Print("[INF_Zones] Failed to apply approaching defenders' running speed.", LogLevel.ERROR);
	}

	void INF_CollectGroups(array<SCR_AIGroup> groups)
	{
		array<INS_AmbientPatrolZoneState> zones = {};
		if (m_INFZoneStates)
			zones = m_INFZoneStates.GetActiveZones();
		else
		{
			foreach (INS_AmbientPatrolZoneState legacy : m_aZones)
				zones.Insert(legacy);
		}
		foreach (INS_AmbientPatrolZoneState state : zones)
		{
			foreach (INS_PatrolAgent agent : state.m_aAgents)
				groups.Insert(agent.m_Group);
		}

	}

	bool INF_BindPrisonerCleanup(AIGroup group, INF_PrisonerComponent prisoner)
	{
		foreach (INS_AmbientPatrolZoneState state : m_aZones)
		{
			foreach (INS_PatrolAgent agent : state.m_aAgents)
			{
				if (!group || agent.m_Group != group)
					continue;
				prisoner.INF_SetCleanupRules(m_fDeactivationRadius, INF_BlockRules.DESPAWN_DELAY_MS, false, state.m_vCenter, true);
				return true;
			}
		}
		return m_INFMortars && m_INFMortars.BindPrisonerCleanup(group, prisoner);
	}

	void INF_CountActivationZones(INF_ActivationPolicy policy)
	{
		foreach (INF_ActivationGroup group : policy.m_Groups)
			group.m_Active = 0;
		if (!m_bINFBuildingGrid || !m_bEnabled)
			return;
		foreach (INS_AmbientPatrolZoneState state : m_INFZoneStates.GetActiveZones())
		{
			if (state.m_bActive)
				policy.Count(state.m_vCenter, m_fActivationRadius);
		}
	}

	protected void INF_TrimActivationZones(INF_ActivationPolicy policy, INF_BlockPresence presence)
	{
		array<INS_AmbientPatrolZoneState> possible = {};
		foreach (INS_AmbientPatrolZoneState state : m_INFZoneStates.GetActiveZones())
		{
			int stateGroup = policy.GetGroup(state.m_vCenter, m_fActivationRadius);
			if (stateGroup < 0 || (policy.m_Groups[stateGroup].m_Active <= policy.m_Groups[stateGroup].m_Limit
				&& policy.m_Groups[stateGroup].Allows(state.m_vCenter, m_fActivationRadius)))
				continue;
			if (state.m_bActive && !presence.Occupied(state.m_vCenter)
				&& (state.m_bINFMarkedCleared || !state.INF_IsCapturing())
				&& INF_BlockRules.SoldiersDistant(state.m_aAgents, presence.m_Players, INF_BlockRules.EVICTION_PROTECTION_RADIUS, presence))
				possible.Insert(state);
		}
		INF_BlockRules.SortZonesByDistance(possible, presence.m_Players, true);
		foreach (INS_AmbientPatrolZoneState victim : possible)
		{
			int group = policy.GetGroup(victim.m_vCenter, m_fActivationRadius);
			if (group < 0)
				continue;
			if (policy.m_Groups[group].m_Active <= policy.m_Groups[group].m_Limit
				&& policy.m_Groups[group].Allows(victim.m_vCenter, m_fActivationRadius))
				continue;
			presence = m_INFZoneStates.GetPresence(false, true);
			if (presence.Occupied(victim.m_vCenter)
				|| !INF_BlockRules.SoldiersDistant(victim.m_aAgents, presence.m_Players, INF_BlockRules.EVICTION_PROTECTION_RADIUS))
				continue;
			DeactivateZone(victim);
			policy.m_Groups[group].m_Active--;
		}
	}

	override protected void TickProximityCheck()
	{
		if (!m_bINFBuildingGrid)
		{
			super.TickProximityCheck();
			return;
		}
		INF_ScheduleProximity();
	}

	// One step of the proximity work, called from the zone loop. Returns true while the loop is still needed.
	bool INF_StepProximity()
	{
		if (!m_bINFBuildingGrid || !Replication.IsServer() || !m_bEnabled || m_bINFPatrolStopped)
			return false;
		if (!INF_AreZonesReady() || !m_INFZoneStates.IsPersistenceReady())
			return false;
		float now = GetGame().GetWorld().GetWorldTime();
		if (now < m_fINFNextProximityAt)
			return true;
		m_fINFNextProximityAt = now + Math.Max(5000, m_fCheckInterval * 1000);
		INF_BlockPresence presence = m_INFZoneStates.GetPresence();
		INS_AmbientVehicleManager zoneVehicles = INS_AmbientVehicleManager.Cast(GetOwner().FindComponent(INS_AmbientVehicleManager));
		bool keepAlive = !presence.m_PlayerIDs.IsEmpty() || !m_INFZoneStates.GetActiveZones().IsEmpty()
			|| (zoneVehicles && zoneVehicles.INF_HasZoneVehicles()) || (m_INFMortars && m_INFMortars.HasBatteries());
		if (zoneVehicles)
			zoneVehicles.INF_MaintainZoneVehicles(presence);
		if (m_INFMortars)
			m_INFMortars.Maintain(this, presence, INF_GetMortarConfig());
		array<INS_AmbientPatrolZoneState> activeZones = {};
		activeZones.Copy(m_INFZoneStates.GetActiveZones());
		foreach (INS_AmbientPatrolZoneState state : activeZones)
		{
			if (!state.m_bActive)
				continue;
			m_INFZoneStates.ObserveDefenders(state);
			if (state.m_iINFPendingGroups == 0 && !state.m_bEverHadLiving
				&& now >= state.m_fINFSpawnReadyAt + 30000 && !INF_BlockRules.HasMembers(state.m_aAgents))
			{
				INF_FailedActivation(state);
				Print("[INF_Zones] Defender groups never materialised; released allocation for retry.", LogLevel.ERROR);
				continue;
			}
			if (INF_BlockRules.IsDistant(state.m_vCenter, state.m_aAgents, presence.m_Players, m_fDeactivationRadius, presence))
			{
				if (state.m_fINFDistantSince < 0)
					state.m_fINFDistantSince = now;
				if (now - state.m_fINFDistantSince >= INF_BlockRules.DESPAWN_DELAY_MS)
				{
					presence = m_INFZoneStates.GetPresence(false, true);
					if (INF_BlockRules.IsDistant(state.m_vCenter, state.m_aAgents, presence.m_Players, m_fDeactivationRadius))
						INF_QueueZoneDespawn(state);
					else
						state.m_fINFDistantSince = -1;
				}
			}
			else
				state.m_fINFDistantSince = -1;
		}
		INF_ActivationPolicy policy = presence.ActivationPolicy(GetOwner());
		INF_CountActivationZones(policy);
		if (!m_bJIPPause)
			INF_TrimActivationZones(policy, presence);
		if (m_bJIPPause || presence.m_Players.IsEmpty())
		{
			return keepAlive;
		}
		array<INS_AmbientPatrolZoneState> candidates = {};
		array<INS_AmbientPatrolZoneState> nearbyZones = {};
		m_INFZoneStates.CollectNearby(presence.m_Players, m_fActivationRadius, nearbyZones);
		foreach (INS_AmbientPatrolZoneState candidate : nearbyZones)
		{
			float distance = INF_BlockRules.Nearest(candidate.m_vCenter, presence.m_Players);
			if (!candidate.m_bINFSpawnExhausted && !candidate.m_bActive && candidate.m_bReadyToRespawn
				&& distance <= m_fActivationRadius && policy.Allows(candidate.m_vCenter, m_fActivationRadius))
				candidates.Insert(candidate);
		}
		m_INFDecisionPolicy = policy;
		INF_BlockRules.SortActivationZones(candidates, presence);
		foreach (INS_AmbientPatrolZoneState next : candidates)
			ActivateZone(next);
		m_INFDecisionPolicy = null;
		return keepAlive;
	}

	protected ref array<INS_AmbientPatrolZoneState> m_aINFDespawningZones = {};
	protected INF_StaggerGate m_INFSpawnGate = INF_StaggerGate.Spawns();
	protected INF_StaggerGate m_INFDespawnGate = INF_StaggerGate.Despawns();

	// Distance-based despawns share one queue so zones are removed a few seconds apart.
	protected void INF_QueueZoneDespawn(INS_AmbientPatrolZoneState state)
	{
		if (m_aINFDespawningZones.Find(state) >= 0)
			return;
		int delay = m_INFDespawnGate.Reserve();
		if (delay <= 0)
		{
			DeactivateZone(state);
			return;
		}
		m_aINFDespawningZones.Insert(state);
		m_CallQueue.CallLater(INF_DespawnZoneQueued, delay, false, state, state.m_iINFActivationGeneration);
	}

	protected void INF_DespawnZoneQueued(INS_AmbientPatrolZoneState state, int generation)
	{
		m_aINFDespawningZones.RemoveItem(state);
		if (!state.m_bActive || state.m_iINFActivationGeneration != generation)
			return;
		INF_BlockPresence presence = m_INFZoneStates.GetPresence(false, true);
		if (INF_BlockRules.IsDistant(state.m_vCenter, state.m_aAgents, presence.m_Players, m_fDeactivationRadius))
			DeactivateZone(state);
		else
			state.m_fINFDistantSince = -1;
	}

	protected bool INF_MakeRoom(INS_AmbientPatrolZoneState candidate)
	{
		return INF_MakeRoomForBudget(candidate.m_iINFPlannedAI, candidate.m_vCenter, true);
	}

	void INF_ResetAbandonedZone(INS_AmbientPatrolZoneState state)
	{
		DeactivateZone(state);
		// An abandoned zone is a fresh start: forget any remembered survivors.
		state.m_iINFRememberedAI = -1;
		state.m_bEverHadLiving = false;
		state.m_iINFFailedActivations = 0;
		state.m_bINFSpawnExhausted = false;
		state.m_bReadyToRespawn = true;
		m_INFZoneStates.NotifyChanged(state);
	}

	bool INF_MakeRoomForBudget(int required, vector center, bool addingBlock)
	{
		INF_BlockPresence presence = m_INFZoneStates.GetPresence();
		float candidateDistance = INF_BlockRules.Nearest(center, presence.m_Players);
		float activationRadius = m_fActivationRadius;
		bool addingZone = addingBlock;
		INF_ActivationPolicy policy;
		int targetGroup = -1;
		int localActive = 0;
		int localLimit = INF_BlockRules.MAX_BLOCKS;
		if (addingZone)
		{
			if (m_INFDecisionPolicy)
				policy = m_INFDecisionPolicy;
			else
				policy = presence.ActivationPolicy(GetOwner());
			INF_CountActivationZones(policy);
			targetGroup = policy.GetGroup(center, activationRadius);
			if (targetGroup < 0 || !policy.m_Groups[targetGroup].Allows(center, activationRadius))
				return false;
			localActive = policy.m_Groups[targetGroup].m_Active;
			localLimit = policy.m_Groups[targetGroup].m_Limit;
		}
		int reserved = INF_ReservedAI();
		INS_CounterAttackManager attacks = INS_CounterAttackManager.Cast(GetOwner().FindComponent(INS_CounterAttackManager));
		if (attacks)
			reserved += attacks.INF_ReservedGridAI();
		int active = 0;
		foreach (INS_AmbientPatrolZoneState state : m_INFZoneStates.GetActiveZones())
		{
			if (state.m_bActive)
				active++;
		}
		if (reserved + required <= INF_BuildingGrid.ACTIVE_BUDGET
			&& (!addingBlock || active < INF_BlockRules.MAX_BLOCKS)
			&& (!addingZone || localActive < localLimit))
			return true;
		array<INS_AmbientPatrolZoneState> possible = {};
		foreach (INS_AmbientPatrolZoneState existing : m_INFZoneStates.GetActiveZones())
		{
			if (existing.m_bActive && !presence.Occupied(existing.m_vCenter)
				&& (existing.m_bINFMarkedCleared || !existing.INF_IsCapturing())
				&& (INF_BlockRules.CapturerOccupied(center, presence) || INF_BlockRules.Nearest(existing.m_vCenter, presence.m_Players) > candidateDistance)
				&& INF_BlockRules.SoldiersDistant(existing.m_aAgents, presence.m_Players, INF_BlockRules.EVICTION_PROTECTION_RADIUS, presence))
				possible.Insert(existing);
		}
		INF_BlockRules.SortZonesByDistance(possible, presence.m_Players, true);
		array<INS_AmbientPatrolZoneState> evictions = {};
		while (reserved + required > INF_BuildingGrid.ACTIVE_BUDGET || (addingBlock && active >= INF_BlockRules.MAX_BLOCKS)
			|| (addingZone && localActive >= localLimit))
		{
			if (possible.IsEmpty())
				return false;
			int farthest = -1;
			for (int i = 0; i < possible.Count(); i++)
			{
				if (addingZone && localActive >= localLimit && policy.GetGroup(possible[i].m_vCenter, m_fActivationRadius) != targetGroup)
					continue;
				farthest = i;
				break;
			}
			if (farthest < 0)
				return false;
			INS_AmbientPatrolZoneState victim = possible[farthest];
			if (addingZone && policy.GetGroup(victim.m_vCenter, m_fActivationRadius) == targetGroup)
				localActive--;
			reserved -= victim.m_iINFReservedAI;
			active--;
			evictions.Insert(victim);
			possible.RemoveOrdered(farthest);
		}
		presence = m_INFZoneStates.GetPresence(false, true);
		foreach (INS_AmbientPatrolZoneState check : evictions)
		{
			if (presence.Occupied(check.m_vCenter)
				|| (!check.m_bINFMarkedCleared && check.INF_IsCapturing())
				|| (!INF_BlockRules.CapturerOccupied(center, presence)
					&& INF_BlockRules.Nearest(check.m_vCenter, presence.m_Players) <= INF_BlockRules.Nearest(center, presence.m_Players))
				|| !INF_BlockRules.SoldiersDistant(check.m_aAgents, presence.m_Players, INF_BlockRules.EVICTION_PROTECTION_RADIUS))
				return false;
		}
		foreach (INS_AmbientPatrolZoneState eviction : evictions)
		{
			DeactivateZone(eviction);
		}
		return true;
	}

	// Surviving infantry of a zone, plus groups planned but not yet spawned.
	protected int INF_CountDefenders(INS_AmbientPatrolZoneState state)
	{
		int total = 0;
		foreach (INS_PatrolAgent agent : state.m_aAgents)
		{
			if (!agent.m_Group || agent.m_INFRoadblock)
				continue;
			array<vector> living = {};
			INF_BlockRules.CollectLiving(agent.m_Group, living);
			total += living.Count();
		}
		if (state.m_bActive)
		{
			int ordinaryGroups = INF_Roadblocks.OrdinaryGroups(state);
			for (int i = state.m_aINFPlan.Count() - state.m_iINFPendingGroups; i >= 0 && i < ordinaryGroups; i++)
				total += state.m_aINFPlan[i];
		}
		return total;
	}

	override protected void DeactivateZone(INS_AmbientPatrolZoneState state)
	{
		if (m_INFZoneStates)
			m_INFZoneStates.ObserveDefenders(state);
		array<ref INS_PatrolAgent> guards = {};
		INF_Roadblocks.ExtractGuards(state, guards);
		if (state.m_bINFBuildingCell && state.m_bActive && !state.m_bINFMarkedCleared && !state.m_bINFPermanent
			&& !state.m_bINFAttackActive && !state.m_bINFAttackPending)
		{
			float deactivationTime = GetGame().GetWorld().GetWorldTime();
			INF_Roadblocks.RememberGuards(state, guards, deactivationTime < state.m_fINFSpawnReadyAt, deactivationTime);
			int survivors = INF_CountDefenders(state);
			// Members of freshly spawned groups may not exist yet; remember the planned strength instead.
			if (GetGame().GetWorld().GetWorldTime() < state.m_fINFSpawnReadyAt)
				survivors = Math.Max(survivors, INF_Roadblocks.OrdinaryPlannedAI(state));
			if (survivors > 0 || state.m_bEverHadLiving)
			{
				state.m_iINFRememberedAI = survivors;
				state.m_fINFRememberedAt = GetGame().GetWorld().GetWorldTime();
			}
		}
		state.m_iINFActivationGeneration++;
		state.m_iINFPendingGroups = 0;
		state.m_iINFReservedAI = 0;
		state.INF_ResetCapture();
		state.m_bINFInboundWaiting = false;
		state.m_bINFAttemptCountable = false;
		array<ref INS_PatrolAgent> staggered = {};
		if (state.m_bINFBuildingCell)
		{
			// The base despawns every group at once, so hand all but the first group to the despawn queue.
			while (state.m_aAgents.Count() > 1)
			{
				staggered.Insert(state.m_aAgents[1]);
				state.m_aAgents.Remove(1);
			}
		}
		super.DeactivateZone(state);
		foreach (INS_PatrolAgent held : staggered)
		{
			if (held.m_Group)
				state.m_aSavedPositions.Insert(held.m_Group.GetOrigin());
			m_CallQueue.CallLater(INF_DespawnDefenderGroup, m_INFDespawnGate.Reserve(), false, held);
		}
		state.m_fINFDistantSince = -1;
		foreach (INS_PatrolAgent guard : guards)
			m_CallQueue.CallLater(INF_DespawnDefenderGroup, m_INFDespawnGate.Reserve(), false, guard);
		state.m_aINFActiveRoadblocks.Clear();
		state.m_INFSpawnRoadblock = null;
		if (state.m_bINFBuildingCell && state.m_bINFMarkedCleared)
			state.m_aSavedPositions.Clear();
		if (m_INFZoneStates)
		{
			m_INFZoneStates.InvalidatePresence();
			m_INFZoneStates.NotifyChanged(state);
		}
	}

	protected void INF_DespawnDefenderGroup(INS_PatrolAgent agent)
	{
		if (agent.m_INFDefend)
			SCR_EntityHelper.DeleteEntityAndChildren(agent.m_INFDefend);
		SCR_AIGroup group = agent.m_Group;
		if (!group)
			return;
		array<AIAgent> members = {};
		group.GetAgents(members);
		bool preserve;
		foreach (AIAgent member : members)
		{
			IEntity entity = member.GetControlledEntity();
			if (!entity)
				continue;
			if (INF_PrisonerComponent.INF_Preserve(entity) || GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(entity) > 0)
				preserve = true;
			else
				SCR_EntityHelper.DeleteEntityAndChildren(entity);
		}
		if (!preserve)
			SCR_EntityHelper.DeleteEntityAndChildren(group);
		m_INFZoneStates.InvalidatePresence();
	}

	protected void INF_FailedActivation(INS_AmbientPatrolZoneState state)
	{
		m_INFZoneStates.ObserveDefenders(state);
		if (state.m_bINFAttemptCountable && !state.m_bEverHadLiving)
			state.m_iINFFailedActivations++;
		state.m_bINFSpawnExhausted = state.m_iINFFailedActivations >= 3;
		DeactivateZone(state);
		state.m_bReadyToRespawn = false;
		if (!state.m_bINFSpawnExhausted)
			m_CallQueue.CallLater(MarkZoneRespawnable, 30000, false, state);
	}

	protected void INF_SpawnQueued(vector pos, INS_AmbientPatrolZoneState state, int generation)
	{
		if (!state.m_bActive || state.m_iINFActivationGeneration != generation)
			return;
		state.m_iINFPendingGroups--;
		int planIndex = state.m_aINFPlan.Count() - 1 - state.m_iINFPendingGroups;
		state.m_iINFSpawnSize = 0;
		state.m_INFSpawnRoadblock = null;
		if (planIndex >= 0 && planIndex < state.m_aINFPlan.Count())
		{
			state.m_iINFSpawnSize = state.m_aINFPlan[planIndex];
			state.m_INFSpawnRoadblock = INF_Roadblocks.PlannedGuard(state, planIndex);
		}
		int previousGroups = state.m_aAgents.Count();
		INF_ActivationPolicy policy = m_INFZoneStates.GetPresence().ActivationPolicy(GetOwner());
		if (policy.Allows(state.m_vCenter, m_fActivationRadius))
			SpawnGroupAt(pos, state);
		else
			state.m_bINFAttemptCountable = false;
		if (state.m_aAgents.Count() == previousGroups)
			state.m_iINFReservedAI -= state.m_iINFSpawnSize;
		if (state.m_iINFPendingGroups == 0 && state.m_aAgents.IsEmpty())
		{
			INF_FailedActivation(state);
		}
		else
		{
			m_INFZoneStates.ObserveDefenders(state);
			m_INFZoneStates.ReinforcementsSettled(state);
		}
	}

	void INF_CollectMapZones(out array<INS_AmbientPatrolZoneState> zones)
	{
		foreach (INS_AmbientPatrolZoneState state : m_aZones)
		{
			if (state && state.m_iGroupCount > 0)
				zones.Insert(state);
		}
	}

	void INF_CollectCaptureObjectives(map<string, bool> zones, map<string, bool> camps)
	{
		foreach (INS_AmbientPatrolZoneState state : m_aZones)
		{
			if (!state || !state.m_bINFBuildingCell || state.m_iGroupCount <= 0)
				continue;
			bool captured = state.m_bINFObjectiveCaptured || state.m_bINFMarkedCleared;
			if (state.m_INFCamp)
			{
				captured = state.m_bINFPermanent && state.m_INFCamp.m_Friendly;
				INF_CaptureObjectiveCells.Add(camps, state.m_INFCamp.m_Key, captured);
			}
			else
				INF_CaptureObjectiveCells.Add(zones, INF_BuildingGrid.Key(state.m_vCenter), captured);
		}
	}

	override protected void CheckClearance(INS_AmbientPatrolZoneState state)
	{
		if (state.m_bINFBuildingCell)
			return;
		bool hadLiving = state.m_bActive && state.m_bEverHadLiving;
		super.CheckClearance(state);
		if (hadLiving && !state.m_bActive && !state.m_bReadyToRespawn)
			state.m_bINFMarkedCleared = true;
	}

	override protected void MarkZoneRespawnable(INS_AmbientPatrolZoneState state)
	{
		if (state.m_bINFBuildingCell && (state.m_bINFSpawnExhausted || state.m_bINFMarkedCleared || state.m_bINFAttackSpent || state.m_bINFPermanent || state.m_bINFAttackPending))
			return;
		super.MarkZoneRespawnable(state);
		if (!state.m_bINFBuildingCell)
			state.m_bINFMarkedCleared = false;
		if (m_INFZoneStates)
			m_INFZoneStates.NotifyChanged(state);
	}
}

class INF_CounterAttackParticipant
{
	IEntity m_Entity;
	bool m_Resolved;
}

modded class INS_CounterAttackZone
{
	INS_AmbientPatrolZoneState m_INFCell;
	int m_iINFMembers;
	int m_iINFGroupCount = 1;
	int m_iINFTotalAI;
	ref array<int> m_aINFPlan = {};
	bool m_bINFEverHadLiving;
	bool m_bINFDefeated;
	ref array<ref INF_CounterAttackParticipant> m_aINFParticipants = {};
	float m_fINFStartedAt;
	float m_fINFNextAttackAt;
	float m_fINFLastLaunchAt = -1;
}

modded class INS_CounterAttackManager
{
	protected ref array<int> m_aINFSquadBuckets = {};
	protected float m_fINFNextAttackStepAt;
	protected float m_fINFNextMonitorAt;
	protected float m_fINFLastMonitorAt = -1;
	protected bool m_bINFGridStopped;
	protected ref map<string, ref INS_CounterAttackZone> m_INFGridTargets = new map<string, ref INS_CounterAttackZone>();

	// Wakes the shared zone loop; the attack work itself runs from INF_StepAttacks.
	void INF_ScheduleGridAttacks()
	{
		INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(GetOwner().FindComponent(INS_AmbientPatrolManager));
		if (!Replication.IsServer() || m_bINFGridStopped || !patrols || !patrols.INF_GetZoneStates())
			return;
		patrols.INF_GetZoneStates().WakeLoop();
	}

	protected INS_CounterAttackZone INF_GetGridTarget(INS_AmbientPatrolZoneState cell)
	{
		string key = INF_BuildingGrid.Key(cell.m_vCenter);
		INS_CounterAttackZone target = m_INFGridTargets.Get(key);
		if (target)
			return target;
		target = new INS_CounterAttackZone(cell.m_vCenter, 71, "Building grid");
		target.m_INFCell = cell;
		target.m_iINFMembers = 4;
		if (cell.m_iINFTargetAI >= 12)
			target.m_iINFMembers = 6;
		foreach (INS_CounterAttackZone previous : m_aZones)
		{
			if (previous.m_fINFLastLaunchAt >= 0 && vector.DistanceSqXZ(previous.m_vCenter, cell.m_vCenter) <= 90000)
				target.m_fINFNextAttackAt = Math.Max(target.m_fINFNextAttackAt, previous.m_fINFLastLaunchAt + m_fZoneCooldown * 1000);
		}
		m_INFGridTargets.Set(key, target);
		m_aZones.Insert(target);
		return target;
	}
	[Attribute("10", UIWidgets.Slider, "One counterattack chance roll when a zone is captured. Failed rolls permanently secure the zone.", "0 100 1", category: "Grid Zones"), RplProp()]
	protected int m_iINFChance = 10;

	bool INF_CommitCapture(INS_AmbientPatrolZoneState state)
	{
		if (!m_bEnabled || m_iMaxConcurrentCounterAttacks <= 0 || !(Math.RandomInt(0, 100) < m_iINFChance))
			return false;
		state.m_bINFAttackPending = true;
		state.m_iINFAttackAttempts = 0;
		state.m_fINFAttackRetryAt = GetGame().GetWorld().GetWorldTime() + 5000;
		return true;
	}

	override protected void ApplyMissionHeader()
	{
		super.ApplyMissionHeader();
		SCR_MissionHeader header = SCR_MissionHeader.Cast(GetGame().GetMissionHeader());
		if (header && header.m_Insurgency)
			INF_HeaderSettings.Int(header.m_Insurgency.m_iINFCounterAttackChance, 0, 100, "m_iINFCounterAttackChance", m_iINFChance);
	}

	override void OnGameModeStart()
	{
		INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(GetOwner().FindComponent(INS_AmbientPatrolManager));
		if (!patrols || !patrols.INF_UsesBuildingGrid())
		{
			super.OnGameModeStart();
			return;
		}
		if (!Replication.IsServer())
			return;
		ApplyMissionHeader();
		GetGame().GetCallqueue().CallLater(TickCallQueue, 100, true);
	}

	override void OnGameModeEnd(SCR_GameModeEndData data)
	{
		m_bINFGridStopped = true;
		m_INFGridTargets.Clear();
		super.OnGameModeEnd(data);
	}

	int INF_GetChance()
	{
		return m_iINFChance;
	}

	void INF_SetChance(int value)
	{
		if (!Replication.IsServer())
			return;
		if (value < 0 || value > 100)
		{
			Print("[INF_Zones] Counterattack chance must be between 0 and 100.", LogLevel.ERROR);
			return;
		}
		m_iINFChance = value;
		Replication.BumpMe();
	}

	override void SetCounterAttackInterval(float seconds)
	{
		INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(GetOwner().FindComponent(INS_AmbientPatrolManager));
		if (!patrols || !patrols.INF_UsesBuildingGrid())
		{
			super.SetCounterAttackInterval(seconds);
			return;
		}
		if (!(seconds >= 1))
		{
			Print("[INF_Zones] Counterattack attempt interval must be at least one second.", LogLevel.ERROR);
			return;
		}
		super.SetCounterAttackInterval(seconds);
		if (!Replication.IsServer() || !patrols || !patrols.INF_UsesBuildingGrid() || !m_bEnabled)
			return;
		if (patrols.INF_GetZoneStates() && !patrols.INF_GetZoneStates().GetAttackZones().IsEmpty())
			INF_ScheduleGridAttacks();
	}

	int INF_ReservedGridAI()
	{
		int total = 0;
		foreach (INS_CounterAttackSquad squad : m_aActiveSquads)
		{
			if (squad.m_bActive && squad.m_Zone && squad.m_Zone.m_INFCell)
				total += squad.m_Zone.m_iINFTotalAI / Math.Max(1, squad.m_Zone.m_iINFGroupCount);
		}
		return total;
	}

	override protected void CounterAttackTick()
	{
		INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(GetOwner().FindComponent(INS_AmbientPatrolManager));
		if (!patrols || !patrols.INF_UsesBuildingGrid())
		{
			super.CounterAttackTick();
			return;
		}
		INF_ScheduleGridAttacks();
	}

	// Attack work for the shared zone loop. Pending launches and squad monitoring each keep their own due time.
	// Returns true while the loop is still needed.
	bool INF_StepAttacks()
	{
		INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(GetOwner().FindComponent(INS_AmbientPatrolManager));
		if (!Replication.IsServer() || m_bINFGridStopped || !patrols || !patrols.INF_UsesBuildingGrid()
			|| !patrols.INF_GetZoneStates())
			return false;
		float now = GetGame().GetWorld().GetWorldTime();
		if (now >= m_fINFNextAttackStepAt)
		{
			m_fINFNextAttackStepAt = now + 5000;
			INF_StepPendingAttacks(patrols, now);
		}
		if (now >= m_fINFNextMonitorAt)
		{
			m_fINFNextMonitorAt = now + m_fCheckInterval * 1000;
			INF_StepMonitor(patrols, now);
		}
		if (!m_aActiveSquads.IsEmpty())
			return true;
		foreach (INS_AmbientPatrolZoneState pending : patrols.INF_GetZoneStates().GetAttackZones())
		{
			if (pending.m_bINFAttackPending)
				return true;
		}
		return false;
	}

	protected void INF_StepPendingAttacks(INS_AmbientPatrolManager patrols, float now)
	{
		if (m_bJIPPause || !patrols.INF_AreCampsReady())
			return;
		array<INS_AmbientPatrolZoneState> attackCells = {};
		attackCells.Copy(patrols.INF_GetZoneStates().GetAttackZones());
		int activeAttacks;
		foreach (INS_AmbientPatrolZoneState attack : attackCells)
		{
			if (attack.m_bINFAttackActive)
				activeAttacks++;
		}
		array<vector> players = patrols.INF_GetZoneStates().GetPresence(false, true).m_Players;
		foreach (INS_AmbientPatrolZoneState cell : attackCells)
		{
			if (!cell.m_bINFAttackPending || cell.m_fINFAttackRetryAt > now)
				continue;
			INS_CounterAttackZone zone = INF_GetGridTarget(cell);
			bool nearby = INF_BlockRules.Nearest(zone.m_vCenter, players) <= 300;
			if (cell.m_iINFAttackAttempts >= 3)
			{
				patrols.INF_GetZoneStates().ResolvePendingAttack(cell, nearby);
				if (!nearby)
					patrols.INF_ResetAbandonedZone(cell);
				continue;
			}
			cell.m_iINFAttackAttempts++;
			cell.m_fINFAttackRetryAt = now + 5000;
			bool contested = zone.m_fINFNextAttackAt > now;
			foreach (INS_CounterAttackSquad active : m_aActiveSquads)
			{
				if (active.m_bActive && vector.DistanceSqXZ(active.m_Zone.m_vCenter, zone.m_vCenter) <= 90000)
					contested = true;
			}
			if (m_bEnabled && nearby && !contested && activeAttacks < Math.Min(m_iMaxConcurrentCounterAttacks, 2)
				&& INF_TrySpawnAttack(zone, patrols, players, now))
			{
				activeAttacks++;
				continue;
			}
			if (cell.m_iINFAttackAttempts >= 3)
			{
				patrols.INF_GetZoneStates().ResolvePendingAttack(cell, nearby);
				if (!nearby)
					patrols.INF_ResetAbandonedZone(cell);
			}
		}
	}

	protected ref array<INS_CounterAttackSquad> m_aINFDespawning = {};
	protected INF_StaggerGate m_INFLaunchGate = INF_StaggerGate.Spawns();
	protected INF_StaggerGate m_INFDespawnGate = INF_StaggerGate.Despawns();

	protected bool INF_TrySpawnAttack(INS_CounterAttackZone selected, INS_AmbientPatrolManager patrols, array<vector> players, float now)
	{
		INF_BuildingGrid.Plan(selected.m_iINFMembers * patrols.INF_ZoneMultiplier(selected.m_INFCell),
			INF_BuildingGrid.Buckets(m_aSquadPrefabs, m_aINFSquadBuckets), selected.m_aINFPlan);
		selected.m_iINFGroupCount = selected.m_aINFPlan.Count();
		selected.m_iINFTotalAI = 0;
		foreach (int planned : selected.m_aINFPlan)
			selected.m_iINFTotalAI += planned;
		vector spawn = FindSpawnPosition(selected.m_vCenter, m_fSpawnDistance);
		if (spawn == vector.Zero || !INF_GridAttackPositionSafe(spawn, players))
			return false;
		vector empty;
		if (!SCR_WorldTools.FindEmptyTerrainPosition(empty, spawn, 10, 2, 2)
			|| !INF_GridAttackPositionSafe(empty, players))
			return false;
		vector direction = empty - selected.m_vCenter;
		direction[1] = 0;
		direction.Normalize();
		vector tangent = Vector(-direction[2], 0, direction[0]);
		array<vector> positions = {};
		for (int slot = 0; slot < selected.m_iINFGroupCount; slot++)
		{
			float lateralOffset = slot * 12.0;
			vector groupPosition;
			if (!SCR_WorldTools.FindEmptyTerrainPosition(groupPosition, empty + tangent * lateralOffset, 5, 2, 2)
				|| !INF_GridAttackPositionSafe(groupPosition, players))
				return false;
			positions.Insert(groupPosition);
		}
		if (!patrols.INF_MakeRoomForBudget(selected.m_iINFTotalAI, selected.m_vCenter, false))
			return false;
		selected.m_bINFEverHadLiving = false;
		selected.m_bINFDefeated = false;
		selected.m_aINFParticipants.Clear();
		int lastDelay;
		for (int group = 0; group < selected.m_iINFGroupCount; group++)
		{
			lastDelay = m_INFLaunchGate.Reserve();
			m_CallQueue.CallLater(INF_SpawnAttackSquad, lastDelay, false, selected, positions[group], selected.m_aINFPlan[group]);
		}
		// The 30 s "nobody alive" failure check must not start before the last squad exists.
		selected.m_fINFStartedAt = now + lastDelay;
		selected.m_fINFLastLaunchAt = now;
		patrols.INF_GetZoneStates().AttackStarted(selected.m_INFCell);
		patrols.INF_GetZoneStates().InvalidatePresence();
		foreach (INS_CounterAttackZone neighbour : m_aZones)
		{
			if (vector.DistanceSqXZ(neighbour.m_vCenter, selected.m_vCenter) <= 90000)
				neighbour.m_fINFNextAttackAt = Math.Max(neighbour.m_fINFNextAttackAt, now + m_fZoneCooldown * 1000);
		}
		return true;
	}

	protected void INF_SpawnAttackSquad(INS_CounterAttackZone zone, vector pos, int members)
	{
		if (!zone.m_INFCell || !zone.m_INFCell.m_bINFAttackActive)
			return;
		IEntity entity = INF_BuildingGrid.Spawn(pos, members, m_aSquadPrefabs);
		if (!entity)
		{
			// Keeps the defeat check (participants >= total) reachable for the squads that did spawn.
			zone.m_iINFTotalAI -= members;
			Print("[INF_Zones] Counterattack squad spawn failed; the wave continues without it.", LogLevel.ERROR);
			return;
		}
		INS_CounterAttackSquad squad = new INS_CounterAttackSquad(entity, zone);
		m_aActiveSquads.Insert(squad);
		INF_RecordAttackers(squad);
		m_CallQueue.CallLater(InitSquad, 500, false, squad);
		m_CallQueue.CallLater(AssignWaypoint, 1500, false, squad, zone.m_vCenter);
	}

	protected bool INF_GridAttackPositionSafe(vector pos, array<vector> players)
	{
		if (pos[1] <= 0.5 || INF_BuildingGrid.IsSpawnBlocked(pos))
			return false;
		foreach (vector player : players)
		{
			if (vector.DistanceSqXZ(pos, player) < 250000)
				return false;
		}
		return INF_BlockRules.IsConcealed(pos, players, 1.5);
	}

	override protected void InitSquad(INS_CounterAttackSquad squad)
	{
		if (squad.m_Zone && squad.m_Zone.m_INFCell && squad.m_Group)
			squad.m_Group.SetLifecyclePolicy(SCR_EAIGroupLifecyclePolicy.Manual);
		super.InitSquad(squad);
		if (squad.m_Zone && squad.m_Zone.m_INFCell)
		{
			if (squad.m_Group)
			{
				SCR_AIGroupSettingsComponent settings = SCR_AIGroupSettingsComponent.Cast(squad.m_Group.FindComponent(SCR_AIGroupSettingsComponent));
				if (!settings)
					Print("[INF_Zones] Cannot set counterattack running speed: group settings component missing.", LogLevel.ERROR);
				else if (!settings.AddSetting(SCR_AIGroupCharactersMovementSpeedSetting.Create(SCR_EAISettingOrigin.SCENARIO, EMovementType.RUN), false, true))
					Print("[INF_Zones] Failed to apply counterattack running speed.", LogLevel.ERROR);
			}
			INF_RecordAttackers(squad);
		}
	}

	protected void INF_RecordAttackers(INS_CounterAttackSquad squad)
	{
		if (!squad.m_bActive || !squad.m_Group)
			return;
		array<AIAgent> agents = {};
		squad.m_Group.GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			IEntity entity = agent.GetControlledEntity();
			if (!entity)
				continue;
			bool recorded = false;
			foreach (INF_CounterAttackParticipant existing : squad.m_Zone.m_aINFParticipants)
			{
				if (existing.m_Entity == entity)
					recorded = true;
			}
			if (recorded)
				continue;
			INF_CounterAttackParticipant participant = new INF_CounterAttackParticipant();
			participant.m_Entity = entity;
			squad.m_Zone.m_aINFParticipants.Insert(participant);
			DamageManagerComponent damage = DamageManagerComponent.Cast(entity.FindComponent(DamageManagerComponent));
			if (damage && !damage.IsDestroyed())
				squad.m_Zone.m_bINFEverHadLiving = true;
		}
	}

	protected bool INF_CollectAttackers(INS_CounterAttackZone zone, array<vector> living)
	{
		bool defeated = zone.m_bINFEverHadLiving
			&& zone.m_aINFParticipants.Count() >= zone.m_iINFTotalAI;
		foreach (INF_CounterAttackParticipant participant : zone.m_aINFParticipants)
		{
			if (participant.m_Resolved)
				continue;
			IEntity entity = participant.m_Entity;
			if (!entity || entity.IsDeleted())
			{
				defeated = false;
				continue;
			}
			INF_PrisonerComponent prisoner = INF_PrisonerComponent.Find(entity);
			DamageManagerComponent damage = DamageManagerComponent.Cast(entity.FindComponent(DamageManagerComponent));
			if ((prisoner && prisoner.IsCaptured()) || (damage && damage.IsDestroyed()))
			{
				participant.m_Resolved = true;
				continue;
			}
			defeated = false;
			living.Insert(entity.GetOrigin());
		}
		return defeated;
	}

	override protected void MonitorTick()
	{
		INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(GetOwner().FindComponent(INS_AmbientPatrolManager));
		if (!patrols || !patrols.INF_UsesBuildingGrid())
		{
			super.MonitorTick();
			return;
		}
		INF_ScheduleGridAttacks();
	}

	protected void INF_StepMonitor(INS_AmbientPatrolManager patrols, float now)
	{
		if (m_aActiveSquads.IsEmpty())
		{
			m_fINFLastMonitorAt = -1;
			return;
		}
		// Abandon timers advance by real elapsed time, so the loop beat does not change their length.
		float elapsed = m_fCheckInterval;
		if (m_fINFLastMonitorAt >= 0)
			elapsed = Math.Min((now - m_fINFLastMonitorAt) / 1000, m_fCheckInterval * 2);
		m_fINFLastMonitorAt = now;
		INF_BlockPresence presence = patrols.INF_GetZoneStates().GetPresence(false, true);
		array<vector> players = presence.m_Players;
		array<INS_AmbientPatrolZoneState> attackCells = {};
		attackCells.Copy(patrols.INF_GetZoneStates().GetAttackZones());
		foreach (INS_AmbientPatrolZoneState cell : attackCells)
		{
			if (!cell.m_bINFAttackActive)
				continue;
			INS_CounterAttackZone zone = INF_GetGridTarget(cell);
			array<vector> living = {};
			foreach (INS_CounterAttackSquad squad : m_aActiveSquads)
			{
				if (squad.m_bActive && squad.m_Zone == zone && squad.m_Group)
				{
					INF_RecordAttackers(squad);
				}
			}
			zone.m_bINFDefeated = INF_CollectAttackers(zone, living);
			patrols.INF_GetZoneStates().CheckRetake(zone.m_INFCell, living, players);
			bool failed = zone.m_bINFDefeated || (living.IsEmpty() && now - zone.m_fINFStartedAt >= 30000);
			for (int i = m_aActiveSquads.Count() - 1; i >= 0; i--)
			{
				INS_CounterAttackSquad member = m_aActiveSquads[i];
				if (member.m_Zone != zone)
					continue;
				if (!member.m_GroupEntity)
				{
					Print("[INF_Zones] Counterattack group entity missing; releasing its reservation.", LogLevel.WARNING);
					DespawnSquad(member);
					continue;
				}
				bool distant = INF_BlockRules.Nearest(zone.m_vCenter, players) > INF_BlockRules.COUNTERATTACK_DESPAWN_RADIUS
					&& INF_BlockRules.Nearest(member.m_GroupEntity.GetOrigin(), players) > INF_BlockRules.COUNTERATTACK_DESPAWN_RADIUS;
				if (distant)
					member.m_fNoPlayerTimer += elapsed;
				else
					member.m_fNoPlayerTimer = 0;
				if ((failed || member.m_fNoPlayerTimer >= m_fAbandonDelay) && m_aINFDespawning.Find(member) < 0)
				{
					m_aINFDespawning.Insert(member);
					m_CallQueue.CallLater(INF_DespawnStaggered, m_INFDespawnGate.Reserve(), false, member);
				}
			}
		}
	}

	protected void INF_DespawnStaggered(INS_CounterAttackSquad squad)
	{
		m_aINFDespawning.RemoveItem(squad);
		if (squad.m_bActive && m_aActiveSquads.Find(squad) >= 0)
			DespawnSquad(squad);
	}

	override protected void DespawnSquad(INS_CounterAttackSquad squad)
	{
		INS_CounterAttackZone zone = squad.m_Zone;
		if (zone && zone.m_INFCell && squad.m_Group)
		{
			array<vector> living = {};
			INF_BlockRules.CollectLiving(squad.m_Group, living);
			if (!living.IsEmpty())
				zone.m_bINFEverHadLiving = true;
		}
		super.DespawnSquad(squad);
		if (!zone || !zone.m_INFCell || !zone.m_INFCell.m_bINFAttackActive)
			return;
		foreach (INS_CounterAttackSquad remaining : m_aActiveSquads)
		{
			if (remaining.m_bActive && remaining.m_Zone == zone)
				return;
		}
		INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(GetOwner().FindComponent(INS_AmbientPatrolManager));
		if (patrols && patrols.INF_GetZoneStates())
		{
			if (!zone.m_bINFDefeated)
				Print("[INF_Zones] Counterattack aborted without permanent capture credit; not every attacker was confirmed killed or captured at " + INF_BuildingGrid.Key(zone.m_vCenter), LogLevel.WARNING);
			patrols.INF_GetZoneStates().InvalidatePresence();
			patrols.INF_GetZoneStates().AttackEnded(zone.m_INFCell, zone.m_bINFEverHadLiving, zone.m_bINFDefeated);
		}
	}

	void INF_CollectGroups(array<SCR_AIGroup> groups)
	{
		foreach (INS_CounterAttackSquad squad : m_aActiveSquads)
			groups.Insert(squad.m_Group);
	}

	bool INF_BindPrisonerCleanup(AIGroup group, INF_PrisonerComponent prisoner)
	{
		foreach (INS_CounterAttackSquad squad : m_aActiveSquads)
		{
			if (group && squad.m_Group == group)
			{
				prisoner.INF_SetCleanupRules(INF_BlockRules.COUNTERATTACK_DESPAWN_RADIUS, m_fAbandonDelay * 1000, false, squad.m_Zone.m_vCenter, true);
				return true;
			}
		}
		return false;
	}

	bool INF_HasCounterAttackInArea(vector center, float radius)
	{
		INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(GetOwner().FindComponent(INS_AmbientPatrolManager));
		if (patrols && patrols.INF_UsesBuildingGrid() && patrols.INF_GetZoneStates())
		{
			INS_AmbientPatrolZoneState cell = patrols.INF_GetZoneStates().GetCell(center);
			if (cell && (cell.m_bINFAttackPending || cell.m_bINFAttackActive)
				&& vector.DistanceSqXZ(center, cell.m_vCenter) < 1)
				return true;
		}
		foreach (INS_CounterAttackSquad squad : m_aActiveSquads)
		{
			if (!squad || !squad.m_bActive || !squad.m_Zone)
				continue;

			if (squad.m_Zone.m_INFCell)
			{
				if (vector.DistanceSqXZ(center, squad.m_Zone.m_vCenter) < 1)
					return true;
				continue;
			}

			float coverage = radius + squad.m_Zone.m_fRadius;
			if (vector.DistanceSqXZ(center, squad.m_Zone.m_vCenter) <= coverage * coverage)
				return true;
		}

		return false;
	}
}
