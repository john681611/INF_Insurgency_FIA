class INF_ForestCampSite
{
	string m_Key;
	vector m_Center;
	vector m_Position;
	float m_Yaw;
	string m_RewardID;
	ResourceName m_Composition;
	float m_FootprintRadius;
	bool m_LayoutReady;
	bool m_CompositionReady;
	bool m_Friendly;
	ref array<IEntity> m_Props = {};
}

[ComponentEditorProps(category: "Insurgency Advanced", description: "Replicated ownership of a permanently secured camp.")]
class INF_CampOwnershipComponentClass : ScriptComponentClass
{
}

class INF_CampOwnershipComponent : ScriptComponent
{
	[RplProp(onRplName: "QueueFactionUpdate")]
	protected string m_sFaction;

	protected bool m_bFactionWarning;

	bool CanBindFaction()
	{
		RplComponent replication = RplComponent.Cast(GetOwner().FindComponent(RplComponent));
		return Replication.IsServer() && replication && !replication.IsProxy();
	}

	bool BindFaction(Faction faction)
	{
		if (!CanBindFaction() || !faction)
		{
			Print("[INF_ForestCamps] Cannot bind camp ownership without server authority and a valid faction.", LogLevel.ERROR);
			return false;
		}
		INF_ForestCamps.SetCampFaction(GetOwner(), faction);
		m_sFaction = faction.GetFactionKey();
		QueueFactionUpdate();
		Replication.BumpMe();
		return true;
	}

	protected void QueueFactionUpdate()
	{
		if (m_sFaction.IsEmpty())
			return;
		GetGame().GetCallqueue().Remove(ApplyFaction);
		GetGame().GetCallqueue().CallLater(ApplyFaction, 0, false);
	}

	protected void ApplyFaction()
	{
		GetGame().GetCallqueue().Remove(ApplyFaction);
		Faction faction = GetGame().GetFactionManager().GetFactionByKey(m_sFaction);
		if (!faction)
		{
			if (!m_bFactionWarning)
				Print("[INF_ForestCamps] Replicated camp faction is not registered; waiting for faction initialization.", LogLevel.WARNING);
			m_bFactionWarning = true;
		}
		else
		{
			INF_ForestCamps.SetCampFaction(GetOwner(), faction);
			m_bFactionWarning = false;
		}
		// Relevance can add or remove children after the root has replicated.
		if (!Replication.IsServer() || !faction)
			GetGame().GetCallqueue().CallLater(ApplyFaction, 5000, false);
	}

	override void OnPostInit(IEntity owner)
	{
		SetEventMask(owner, EntityEvent.INIT);
	}

	override void EOnInit(IEntity owner)
	{
		QueueFactionUpdate();
	}

	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(ApplyFaction);
		super.OnDelete(owner);
	}
}

class INF_ForestCell
{
	vector m_Center;
	int m_Trees;
}

class INF_ForestCamps
{
	static const int MAX_CAMPS = 20;
	static const int MAX_CANDIDATES = 96;
	static const float MIN_SPACING = 450;
	protected INS_AmbientPatrolManager m_Manager;
	protected ref map<string, ref INF_ForestCell> m_Cells = new map<string, ref INF_ForestCell>();
	protected ref array<string> m_Keys = {};
	protected ref array<ref INF_ForestCampSite> m_Sites = {};
	protected ref array<vector> m_Buildings = {};
	protected ref array<float> m_aBuildingClearSq = {};
	protected int m_PublishedBuildings;
	protected ref array<vector> m_Settlements = {};
	protected int m_Index;
	protected int m_Attempt;
	protected float m_NavWaitSince = -1;
	protected bool m_Stopped;
	protected float m_SearchStarted;
	protected int m_iSpatialRejects;
	protected int m_iTerrainRejects;
	protected int m_iClearanceRejects;
	protected int m_iNavigationRejects;
	protected int m_iNavigationTimeouts;
	protected int m_iTestedLayouts;
	protected bool m_DiscoveryFinished;
	protected ref INF_TerrainIndex m_TerrainIndex;
	protected ref array<ref INF_ForestCampSite> m_CandidatePool = {};
	protected bool m_SelectingCached;
	protected string m_StartupFailure;

	void INF_ForestCamps(INS_AmbientPatrolManager manager)
	{
		m_Manager = manager;
		m_TerrainIndex = new INF_TerrainIndex(manager);
	}

	bool LoadMapIndex()
	{
		array<ref INF_ForestCell> cells;
		if (!m_TerrainIndex.LoadMap(m_Buildings, m_Settlements, cells))
			return false;
		foreach (INF_ForestCell cell : cells)
			m_Cells.Set(INF_BuildingGrid.Key(cell.m_Center), cell);
		return true;
	}

	void SaveMapIndex()
	{
		array<ref INF_ForestCell> cells = {};
		foreach (string key, INF_ForestCell cell : m_Cells)
			cells.Insert(cell);
		m_TerrainIndex.SaveMap(m_Buildings, m_Settlements, cells);
	}

	void AddBuildingZones(INF_ZoneStateSystem zones)
	{
		for (int i = m_PublishedBuildings; i < m_Buildings.Count(); i++)
			zones.AddBuilding(m_Buildings[i]);
		m_PublishedBuildings = m_Buildings.Count();
	}

	void Index(IEntity entity)
	{
		vector pos = entity.GetOrigin();
		int kind = INF_BuildingGrid.Classify(entity);
		if (kind != INF_BuildingGrid.KIND_NONE)
			m_Buildings.Insert(INF_BuildingGrid.Encode(pos, kind));
		MapDescriptorComponent descriptor = MapDescriptorComponent.Cast(entity.FindComponent(MapDescriptorComponent));
		if (descriptor && (descriptor.GetBaseType() == 59 || descriptor.GetBaseType() == 60 || descriptor.GetBaseType() == 61))
			m_Settlements.Insert(pos);
		if (!BaseTree.Cast(entity))
			return;
		string key = INF_BuildingGrid.Key(pos);
		INF_ForestCell cell = m_Cells.Get(key);
		if (!cell)
		{
			cell = new INF_ForestCell();
			cell.m_Center = INF_BuildingGrid.Center(pos);
			m_Cells.Set(key, cell);
		}
		cell.m_Trees++;
	}

	void Begin()
	{
		Resource composition = Resource.Load(m_Manager.INF_GetCampComposition());
		float radius = m_Manager.INF_GetCampFootprintRadius();
		if (!composition || !composition.IsValid() || radius < 3 || radius > 24)
		{
			m_StartupFailure = "Mission initialisation is blocked: invalid camp setup. An administrator needs to check the server log.";
			Print("[INF_ForestCamps] Invalid camp composition or footprint radius (expected 3-24m); holding static-site startup.", LogLevel.ERROR);
			return;
		}
		INF_ForestCampRewards.BeginDiscovery();
		array<ref INF_ForestCampRewardSite> recovered = {};
		if (INF_ForestCampRewards.GetRecoveredSites(recovered))
		{
			foreach (INF_ForestCampRewardSite saved : recovered)
			{
				INF_ForestCampSite restored = new INF_ForestCampSite();
				restored.m_Key = saved.m_Key;
				restored.m_Center = saved.m_Center;
				restored.m_Position = saved.m_Position;
				restored.m_Yaw = saved.m_Yaw;
				ConfigureSite(restored);
				m_Sites.Insert(restored);
			}
			Print(string.Format("[INF_ForestCamps] Restored %1 saved camp layouts for cache recovery.", m_Sites.Count()));
			if (m_Sites.Count() != m_Manager.INF_GetCampCount())
				Print("[INF_ForestCamps] Preserving the saved camp roster; the camp-count setting only applies to a fresh selection.", LogLevel.WARNING);
			m_Manager.INF_FinishCampDiscovery(m_Sites);
			StartLayouts();
			return;
		}
		if (m_TerrainIndex.LoadCandidates(m_CandidatePool))
		{
			ShuffleSites(m_CandidatePool);
			m_SelectingCached = true;
			m_SearchStarted = GetGame().GetWorld().GetWorldTime();
			GetGame().GetCallqueue().CallLater(SearchTick, 100, true);
			return;
		}
		m_SearchStarted = GetGame().GetWorld().GetWorldTime();
		foreach (string key, INF_ForestCell cell : m_Cells)
		{
			if (cell.m_Trees >= 8)
				m_Keys.Insert(key);
		}
		m_Keys.Sort();
		array<string> spread = {};
		int stride = Math.Ceil(m_Keys.Count() / 24.0);
		for (int offset = 0; offset < stride; offset++)
		{
			for (int band = 0; band < 24; band++)
			{
				int index = band * stride + offset;
				if (index < m_Keys.Count())
					spread.Insert(m_Keys[index]);
			}
		}
		m_Keys = spread;
		PreferInteriorCells();
		Print(string.Format("[INF_ForestCamps] Building a reusable pool from %1 woodland cells, up to %2 potential camps.", m_Keys.Count(), MAX_CANDIDATES));
		GetGame().GetCallqueue().CallLater(SearchTick, 100, true);
	}

	string GetStartupStatus()
	{
		if (!m_StartupFailure.IsEmpty())
			return m_StartupFailure;
		if (!m_DiscoveryFinished)
		{
			if (m_SelectingCached)
				return string.Format("Checking reusable camp terrain: %1/%2 areas checked. Building zones are playable while mission sites prepare.",
					m_Index, m_CandidatePool.Count());
			return string.Format("Finding safe camp terrain: %1/%2 woodland areas checked. Building zones are playable; the first search can take several minutes.",
				m_Index, m_Keys.Count());
		}
		int compositions;
		int arsenals;
		foreach (INF_ForestCampSite site : m_Sites)
		{
			if (site.m_CompositionReady)
				compositions++;
			if (site.m_LayoutReady)
				arsenals++;
		}
		if (compositions < m_Sites.Count())
			return string.Format("Preparing camps: %1/%2 placed. Unready sites are being retried; building zones remain playable.",
				compositions, m_Sites.Count());
		if (!INF_ForestCampRewards.AreCachesReady())
			return "Preparing weapon caches. Building zones are playable; intel searches and final objectives are not ready yet.";
		INS_IntelManager intel = INS_IntelManager.GetInstance();
		if (!intel || !intel.INF_IsIntelReady())
			return "Restoring intel and cache progress. Building zones are playable; intel searches are not ready yet.";
		return string.Format("Finalising camp arsenals and objectives: %1/%2 camps ready. Building zones remain playable.",
			arsenals, m_Sites.Count());
	}

	static vector Offset(INF_ForestCampSite site, vector local)
	{
		float angle = site.m_Yaw * Math.DEG2RAD;
		return site.m_Position + Vector(local[0] * Math.Cos(angle) + local[2] * Math.Sin(angle), 0,
			-local[0] * Math.Sin(angle) + local[2] * Math.Cos(angle));
	}

	static vector InfantryPosition(INF_ForestCampSite site)
	{
		vector pos = Offset(site, "0 0 3");
		pos[1] = GetGame().GetWorld().GetSurfaceY(pos[0], pos[2]);
		return pos;
	}

	static void ExcludeSite(TraceParam trace, INF_ForestCampSite site, array<IEntity> exclusions)
	{
		trace.ExcludeArray = exclusions;
		foreach (IEntity prop : site.m_Props)
		{
			if (prop)
				ExcludeHierarchy(prop, exclusions);
		}
		IEntity cache = INF_ForestCampRewards.GetCacheEntity(site.m_RewardID);
		if (cache)
			ExcludeHierarchy(cache, exclusions);
	}

	static void ExcludeHierarchy(IEntity entity, array<IEntity> exclusions)
	{
		exclusions.Insert(entity);
		IEntity child = entity.GetChildren();
		while (child)
		{
			ExcludeHierarchy(child, exclusions);
			child = child.GetSibling();
		}
	}

	static bool DryFlat(vector pos, float baseHeight)
	{
		BaseWorld world = GetGame().GetWorld();
		float ground = world.GetSurfaceY(pos[0], pos[2]);
		EWaterSurfaceType waterType;
		float lakeArea;
		float water = SCR_WorldTools.GetWaterSurfaceY(world, pos, waterType, lakeArea);
		if (ground <= Math.Max(world.GetOceanBaseHeight(), water) + 0.5 || Math.AbsFloat(ground - baseHeight) > 0.65)
			return false;
		vector normal = SCR_TerrainHelper.GetTerrainNormal(pos, world);
		return vector.Dot(normal, vector.Up) >= 0.9781476;
	}

	static bool ValidateFootprint(INF_ForestCampSite site, bool reserveRewardSlot = false)
	{
		return FootprintFailure(site, reserveRewardSlot) == 0;
	}

	// 0=valid, 1=terrain/water/slope, 2=obstructed layout.
	static int FootprintFailure(INF_ForestCampSite site, bool reserveRewardSlot = false)
	{
		BaseWorld world = GetGame().GetWorld();
		float ground = world.GetSurfaceY(site.m_Position[0], site.m_Position[2]);
		int steps = Math.Ceil(site.m_FootprintRadius / 3.0);
		float spacing = site.m_FootprintRadius / steps;
		array<vector> footprints = {};
		for (int x = -steps; x <= steps; x++)
		{
			for (int z = -steps; z <= steps; z++)
			{
				vector sample = Offset(site, Vector(x * spacing, 0, z * spacing));
				if (!DryFlat(sample, ground))
					return 1;
				footprints.Insert(sample);
			}
		}
		foreach (vector point : footprints)
		{
			point[1] = world.GetSurfaceY(point[0], point[2]);
			if (!ClearVolume(point, 2.5, 3, site, true))
				return 2;
		}
		if (reserveRewardSlot && !RewardSlotClear(site))
			return 2;
		return 0;
	}

	static bool CampObstacleFilter(notnull IEntity entity, vector start, vector direction)
	{
		return !BaseTree.Cast(entity);
	}

	static bool ClearVolume(vector pos, float radius, float height, INF_ForestCampSite site = null, bool ignoreVegetation = false)
	{
		BaseWorld world = GetGame().GetWorld();
		array<vector> directions = {"1 0 0", "-1 0 0", "0 0 1", "0 0 -1"};
		foreach (vector direction : directions)
		{
			vector start = pos + direction * radius;
			vector end = pos - direction * radius;
			if (!DryFlat(start, pos[1]) || !DryFlat(end, pos[1]))
				return false;
			// Follow the terrain at each end instead of tracing into uphill ground.
			start[1] = world.GetSurfaceY(start[0], start[2]) + height + 0.05;
			end[1] = world.GetSurfaceY(end[0], end[2]) + 0.05;
			TraceParam trace = new TraceParam();
			trace.Start = start;
			trace.End = end;
			trace.Flags = TraceFlags.ENTS | TraceFlags.WORLD | TraceFlags.OCEAN;
			// TraceParam only holds a weak reference; keep the array alive through TraceMove.
			array<IEntity> exclusions = {};
			if (site)
				ExcludeSite(trace, site, exclusions);
			float fraction;
			if (ignoreVegetation)
				fraction = world.TraceMove(trace, CampObstacleFilter);
			else
				fraction = world.TraceMove(trace, null);
			if (fraction < 1)
				return false;
		}
		return true;
	}

	static bool RewardSlotClear(INF_ForestCampSite site)
	{
		vector pos = INF_ForestCampRewards.GetCachePosition(site.m_Position, site.m_Yaw);
		if (!DryFlat(pos, site.m_Position[1]))
			return false;
		return ClearVolume(pos, 2, 3, site, true);
	}

	static int NavigationTilesReady(INF_ForestCampSite site)
	{
		AIWorld ai = GetGame().GetAIWorld();
		if (!ai)
			return -1;
		NavmeshWorldComponent nav = ai.GetNavmeshWorldComponent("Soldiers");
		if (!nav)
			return -1;
		vector pos = InfantryPosition(site);
		if (!nav.IsTileLoaded(pos))
		{
			ai.RequestNavmeshLoad(pos);
			return 0;
		}
		if (!nav.IsTileValid(pos))
			return -1;
		return 1;
	}

	static int ValidateNavigation(INF_ForestCampSite site)
	{
		int tiles = NavigationTilesReady(site);
		if (tiles != 1)
			return tiles;
		AIWorld ai = GetGame().GetAIWorld();
		if (!ai)
			return -1;
		NavmeshWorldComponent nav = ai.GetNavmeshWorldComponent("Soldiers");
		if (!nav)
			return -1;
		if (!nav.IsTileLoaded(site.m_Position))
		{
			ai.RequestNavmeshLoad(site.m_Position);
			return 0;
		}
		if (!nav.IsTileValid(site.m_Position))
			return -1;
		vector reachable;
		if (!nav.GetReachablePoint(site.m_Position, 25, reachable))
			return -1;
		float distance = vector.DistanceXZ(site.m_Position, reachable);
		if (distance < 8 || distance > 40)
			return -1;
		if (!nav.IsTileLoaded(reachable))
		{
			ai.RequestNavmeshLoad(reachable);
			return 0;
		}
		if (!nav.IsTileValid(reachable) || !DryFlat(reachable, reachable[1]))
			return -1;
		return 1;
	}

	// Squared camp clearance per indexed building; rebuilt only when the building list changes.
	protected void BuildClearances()
	{
		if (m_aBuildingClearSq.Count() == m_Buildings.Count())
			return;
		INF_ZoneStateSystem zones;
		if (m_Manager)
			zones = m_Manager.INF_GetZoneStates();
		m_aBuildingClearSq.Clear();
		foreach (vector stored : m_Buildings)
		{
			float clearSq = INF_BuildingGrid.CAMP_CLEAR_SQ;
			int kind = INF_BuildingGrid.StoredKind(stored);
			if (kind == INF_BuildingGrid.STORED_SMALL)
			{
				clearSq = INF_BuildingGrid.CAMP_CLEAR_SMALL_SQ;
			}
			else if (zones)
			{
				// A building whose zone holds only 2 AI (one building, or two ruins) may sit near a camp.
				INS_AmbientPatrolZoneState zone = zones.GetCell(INF_BuildingGrid.StoredPosition(stored));
				if (zone && zone.m_iINFTargetAI <= 2)
					clearSq = INF_BuildingGrid.CAMP_CLEAR_LIGHT_SQ;
			}
			m_aBuildingClearSq.Insert(clearSq);
		}
	}

	protected bool HasRoom(vector center)
	{
		if (INF_BuildingGrid.IsExcluded(center))
			return false;
		foreach (vector settlement : m_Settlements)
		{
			if (vector.DistanceSqXZ(center, settlement) < 90000)
				return false;
		}
		BuildClearances();
		foreach (int index, vector building : m_Buildings)
		{
			if (vector.DistanceSqXZ(center, building) < m_aBuildingClearSq[index])
				return false;
		}
		foreach (INF_ForestCampSite site : m_Sites)
		{
			if (vector.DistanceSqXZ(center, site.m_Position) < MIN_SPACING * MIN_SPACING)
				return false;
		}
		return true;
	}

	protected void SearchTick()
	{
		for (int batch = 0; batch < 8 && !m_Stopped && !m_DiscoveryFinished; batch++)
		{
			SearchStep();
			if (m_NavWaitSince >= 0)
				break;
		}
	}

	protected void ConfigureSite(INF_ForestCampSite site)
	{
		site.m_Composition = m_Manager.INF_GetCampComposition();
		site.m_FootprintRadius = m_Manager.INF_GetCampFootprintRadius();
	}

	protected void SearchStep()
	{
		if (m_Stopped)
			return;
		if (m_SelectingCached)
		{
			SelectCachedStep();
			return;
		}
		if (m_Index >= m_Keys.Count() || m_Sites.Count() >= MAX_CANDIDATES
			|| GetGame().GetWorld().GetWorldTime() - m_SearchStarted >= 480000)
		{
			GetGame().GetCallqueue().Remove(SearchTick);
			m_DiscoveryFinished = true;
			Print(string.Format("[INF_ForestCamps] Discovery complete: %1 validated potential camp sites.", m_Sites.Count()));
			Print(string.Format("[INF_ForestCamps] Search diagnostics: cells visited=%1/%2; layouts tested=%3; spatial rejects=%4; terrain rejects=%5; clearance rejects=%6; navigation rejects=%7; navigation timeouts=%8.",
				m_Index, m_Keys.Count(), m_iTestedLayouts, m_iSpatialRejects, m_iTerrainRejects,
				m_iClearanceRejects, m_iNavigationRejects, m_iNavigationTimeouts));
			if (m_Sites.IsEmpty())
				Print("[INF_ForestCamps] No safe woodland sites passed validation; camps will not spawn unchecked.", LogLevel.WARNING);
			m_TerrainIndex.SaveCandidates(m_Sites);
			ShuffleSites(m_Sites);
			int count = m_Manager.INF_GetCampCount();
			if (m_Sites.Count() > count)
				m_Sites.Resize(count);
			m_Manager.INF_FinishCampDiscovery(m_Sites);
			StartLayouts();
			return;
		}
		INF_ForestCell cell = m_Cells.Get(m_Keys[m_Index]);
		if (!HasRoom(cell.m_Center))
		{
			m_iSpatialRejects++;
			NextCell();
			return;
		}
		INF_ForestCampSite site = new INF_ForestCampSite();
		ConfigureSite(site);
		site.m_Key = m_Keys[m_Index];
		site.m_Center = cell.m_Center;
		site.m_Center[1] = GetGame().GetWorld().GetSurfaceY(cell.m_Center[0], cell.m_Center[2]);
		int row = Math.Floor(m_Attempt / 3.0);
		int offsetX = (m_Attempt - row * 3 - 1) * 15;
		int offsetZ = (row - 1) * 15;
		site.m_Position = site.m_Center + Vector(offsetX, 0, offsetZ);
		site.m_Position[1] = GetGame().GetWorld().GetSurfaceY(site.m_Position[0], site.m_Position[2]);
		if (!HasRoom(site.m_Position))
		{
			m_iSpatialRejects++;
			NextAttempt();
			return;
		}
		site.m_Yaw = (m_Index - Math.Floor(m_Index / 4.0) * 4) * 90;
		m_iTestedLayouts++;
		int footprintFailure = FootprintFailure(site, true);
		if (footprintFailure != 0)
		{
			if (footprintFailure == 1)
				m_iTerrainRejects++;
			else
				m_iClearanceRejects++;
			NextAttempt();
			return;
		}
		int navigation = ValidateNavigation(site);
		if (navigation == 0)
		{
			float now = GetGame().GetWorld().GetWorldTime();
			if (m_NavWaitSince < 0)
				m_NavWaitSince = now;
			if (now - m_NavWaitSince < 20000)
				return;
			m_iNavigationTimeouts++;
			Print("[INF_ForestCamps] Navigation streaming timeout; rejecting a candidate rather than spawning unchecked.");
		}
		if (navigation != 1)
		{
			m_iNavigationRejects++;
			NextAttempt();
			return;
		}
		m_Sites.Insert(site);
		NextCell();
	}

	protected bool IsForestInterior(vector position)
	{
		int x = INF_BuildingGrid.CellIndex(position[0]);
		int z = INF_BuildingGrid.CellIndex(position[2]);
		int woodedNeighbours = 0;
		for (int dx = -1; dx <= 1; dx++)
		{
			for (int dz = -1; dz <= 1; dz++)
			{
				int cellX = x + dx;
				int cellZ = z + dz;
				string key = cellX.ToString() + ":" + cellZ.ToString();
				INF_ForestCell cell = m_Cells.Get(key);
				if (!cell || cell.m_Trees < 8)
				{
					if (dx == 0 && dz == 0)
						return false;
					continue;
				}
				if (dx != 0 || dz != 0)
					woodedNeighbours++;
			}
		}
		return woodedNeighbours >= 6;
	}

	protected void PreferInteriorCells()
	{
		array<string> interior = {};
		array<string> edges = {};
		foreach (string key : m_Keys)
		{
			INF_ForestCell cell = m_Cells.Get(key);
			if (IsForestInterior(cell.m_Center))
				interior.Insert(key);
			else
				edges.Insert(key);
		}
		foreach (string edge : edges)
			interior.Insert(edge);
		m_Keys = interior;
	}

	protected void ShuffleSites(array<ref INF_ForestCampSite> sites)
	{
		for (int i = sites.Count() - 1; i > 0; i--)
		{
			int other = Math.RandomInt(0, i + 1);
			INF_ForestCampSite temp = sites[i];
			sites[i] = sites[other];
			sites[other] = temp;
		}
		// Keep random order within each tier, but exhaust interiors before edges.
		array<ref INF_ForestCampSite> interior = {};
		array<ref INF_ForestCampSite> edges = {};
		foreach (INF_ForestCampSite site : sites)
		{
			if (IsForestInterior(site.m_Position))
				interior.Insert(site);
			else
				edges.Insert(site);
		}
		sites.Clear();
		foreach (INF_ForestCampSite preferred : interior)
			sites.Insert(preferred);
		foreach (INF_ForestCampSite fallback : edges)
			sites.Insert(fallback);
	}

	protected void SelectCachedStep()
	{
		int count = m_Manager.INF_GetCampCount();
		if (m_Index >= m_CandidatePool.Count() || m_Sites.Count() >= count
			|| GetGame().GetWorld().GetWorldTime() - m_SearchStarted >= 120000)
		{
			GetGame().GetCallqueue().Remove(SearchTick);
			m_DiscoveryFinished = true;
			if (m_Sites.Count() > count)
				m_Sites.Resize(count);
			Print(string.Format("[INF_ForestCamps] Fresh mission selected %1 camps from %2 cached potential sites; no woodland discovery scan.",
				m_Sites.Count(), m_CandidatePool.Count()));
			if (m_Sites.IsEmpty())
				Print("[INF_ForestCamps] No cached sites currently pass safety/navigation checks; camps will not spawn unchecked. After terrain edits, increase Terrain Index Revision.", LogLevel.WARNING);
			m_Manager.INF_FinishCampDiscovery(m_Sites);
			StartLayouts();
			return;
		}
		INF_ForestCampSite site = m_CandidatePool[m_Index];
		ConfigureSite(site);
		if (!HasRoom(site.m_Position) || FootprintFailure(site, true) != 0)
		{
			NextCell();
			return;
		}
		int navigation = ValidateNavigation(site);
		if (navigation == 0)
		{
			float now = GetGame().GetWorld().GetWorldTime();
			if (m_NavWaitSince < 0)
				m_NavWaitSince = now;
			if (now - m_NavWaitSince < 20000)
				return;
			Print("[INF_ForestCamps] Cached candidate navigation timed out; skipping rather than spawning unchecked.", LogLevel.WARNING);
		}
		if (navigation == 1)
			m_Sites.Insert(site);
		NextCell();
	}

	protected void StartLayouts()
	{
		m_DiscoveryFinished = true;
		GetGame().GetCallqueue().CallLater(UpdateLayouts, 1000, true);
		UpdateLayouts();
	}

	protected void UpdateLayouts()
	{
		if (m_Stopped)
			return;
		bool compositionsReady = true;
		foreach (INF_ForestCampSite camp : m_Sites)
		{
			if (camp.m_CompositionReady)
				continue;
			if (!ValidateFootprint(camp) || !SpawnLayout(camp))
			{
				Cleanup(camp);
				Print("[INF_ForestCamps] Static composition not ready: " + camp.m_Key + "; retrying startup.", LogLevel.ERROR);
				compositionsReady = false;
				continue;
			}
			camp.m_CompositionReady = true;
		}
		if (!compositionsReady)
			return;
		INF_ForestCampRewards.FinishCampObjects();
		if (!INF_ForestCampRewards.AreCachesReady())
			return;
		INS_IntelManager intel = INS_IntelManager.GetInstance();
		if (!intel)
		{
			Print("[INF_ForestCamps] Intel manager missing; static-site startup cannot finish.", LogLevel.ERROR);
			return;
		}
		if (!intel.INF_IsIntelReady())
			return;
		bool complete = true;
		foreach (INF_ForestCampSite site : m_Sites)
		{
			if (site.m_LayoutReady)
				continue;
			if (!BindArsenalSearch(site))
			{
				Print("[INF_ForestCamps] Static camp layout not ready: " + site.m_Key + "; retrying startup placement.", LogLevel.ERROR);
				complete = false;
				continue;
			}
			site.m_LayoutReady = true;
			Print(string.Format("[INF_ForestCamps] Camp %1 persistent layout ready: %2 props; cache objective=%3.",
				site.m_Key, site.m_Props.Count(), INF_ForestCampRewards.IsCacheSite(site.m_RewardID)));
		}
		if (complete)
		{
			m_Manager.INF_StaticCampLayoutsReady();
			if (m_Manager.INF_AreCampsReady())
				GetGame().GetCallqueue().Remove(UpdateLayouts);
		}
	}

	protected void NextAttempt()
	{
		m_NavWaitSince = -1;
		m_Attempt++;
		if (m_Attempt >= 9)
			NextCell();
	}

	protected void NextCell()
	{
		m_Index++;
		m_Attempt = 0;
		m_NavWaitSince = -1;
	}

	static bool SpawnLayout(INF_ForestCampSite site)
	{
		if (!site.m_Props.IsEmpty())
			return true;
		Resource resource = Resource.Load(site.m_Composition);
		if (!resource || !resource.IsValid())
		{
			Print("[INF_ForestCamps] Cannot load configured camp composition: " + site.m_Composition, LogLevel.ERROR);
			return false;
		}
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		Math3D.AnglesToMatrix(Vector(site.m_Yaw, 0, 0), params.Transform);
		params.Transform[3] = site.m_Position;
		IEntity prop = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params);
		if (!prop)
		{
			Print("[INF_ForestCamps] Camp composition spawn failed.", LogLevel.ERROR);
			return false;
		}
		site.m_Props.Insert(prop);
		if (!PrepareComposition(prop))
		{
			Cleanup(site);
			return false;
		}
		Print("[INF_ForestCamps] Spawned persistent composition: " + site.m_Composition);
		return true;
	}

	static bool PrepareComposition(IEntity entity)
	{
		if (SCR_AIGroup.Cast(entity))
		{
			Print("[INF_ForestCamps] Camp composition contains an AI group; select a static composition so all defenders use the zone budget.", LogLevel.ERROR);
			return false;
		}
		SCR_SpawnPoint spawn = SCR_SpawnPoint.Cast(entity);
		if (spawn)
			spawn.SetSpawnPointEnabled_S(true);
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		if (persistence)
			persistence.StopTracking(entity, true);
		IEntity child = entity.GetChildren();
		while (child)
		{
			if (!PrepareComposition(child))
				return false;
			child = child.GetSibling();
		}
		return true;
	}

	static INF_ForestCampSearchComponent FindArsenal(IEntity entity)
	{
		if (!entity)
			return null;
		if (entity.FindComponent(SCR_ArsenalComponent))
		{
			INF_ForestCampSearchComponent search = INF_ForestCampSearchComponent.Cast(entity.FindComponent(INF_ForestCampSearchComponent));
			if (search)
				return search;
		}
		IEntity child = entity.GetChildren();
		while (child)
		{
			INF_ForestCampSearchComponent found = FindArsenal(child);
			if (found)
				return found;
			child = child.GetSibling();
		}
		return null;
	}

	static bool BindArsenalSearch(INF_ForestCampSite site)
	{
		foreach (IEntity root : site.m_Props)
		{
			INF_ForestCampSearchComponent search = FindArsenal(root);
			if (search)
			{
				search.BindSite(site.m_RewardID);
				return true;
			}
		}
		Print("[INF_ForestCamps] Camp composition needs an arsenal inheriting ArsenalBox_Base with the camp-search component.", LogLevel.ERROR);
		return false;
	}

	protected void CollectSpawns(IEntity entity, array<SCR_SpawnPoint> spawns)
	{
		if (!entity)
			return;
		SCR_SpawnPoint spawn = SCR_SpawnPoint.Cast(entity);
		if (spawn)
			spawns.Insert(spawn);
		IEntity child = entity.GetChildren();
		while (child)
		{
			CollectSpawns(child, spawns);
			child = child.GetSibling();
		}
	}

	static void SetCampFaction(IEntity entity, Faction faction)
	{
		if (!entity)
			return;
		FactionAffiliationComponent affiliation = FactionAffiliationComponent.Cast(entity.FindComponent(FactionAffiliationComponent));
		if (affiliation && affiliation.GetAffiliatedFaction() != faction)
			affiliation.SetAffiliatedFaction(faction);
		IEntity child = entity.GetChildren();
		while (child)
		{
			SetCampFaction(child, faction);
			child = child.GetSibling();
		}
	}

	bool MakeFriendly(INF_ForestCampSite site)
	{
		if (!Replication.IsServer())
			return false;
		if (site.m_Friendly)
			return true;
		Faction faction = m_Manager.INF_GetPlayerFaction();
		if (!faction)
		{
			Print("[INF_ForestCamps] Mission player faction is not configured; camp ownership remains unchanged.", LogLevel.ERROR);
			return false;
		}
		array<SCR_SpawnPoint> spawns = {};
		array<INF_CampOwnershipComponent> ownership = {};
		INF_ForestCampSearchComponent arsenal;
		foreach (IEntity root : site.m_Props)
		{
			INF_CampOwnershipComponent owner;
			if (root)
				owner = INF_CampOwnershipComponent.Cast(root.FindComponent(INF_CampOwnershipComponent));
			if (!owner || !owner.CanBindFaction())
			{
				Print("[INF_ForestCamps] Camp composition needs an authoritative replicated ownership component; objective and spawning remain pending.", LogLevel.ERROR);
				return false;
			}
			ownership.Insert(owner);
			CollectSpawns(root, spawns);
			if (!arsenal)
				arsenal = FindArsenal(root);
		}
		if (!arsenal || spawns.IsEmpty())
		{
			Print("[INF_ForestCamps] Permanent camp needs an arsenal and native spawn point; player spawning remains disabled.", LogLevel.ERROR);
			return false;
		}
		if (!arsenal.BindFriendlyFaction(faction.GetFactionKey()))
			return false;
		foreach (INF_CampOwnershipComponent owner : ownership)
		{
			if (!owner.BindFaction(faction))
				return false;
		}
		foreach (SCR_SpawnPoint point : spawns)
		{
			point.SetFaction(faction);
			point.SetSpawnPointName("Captured Camp");
			point.SetSpawnPointEnabled_S(true);
		}
		site.m_Friendly = true;
		Print("[INF_ForestCamps] Permanent camp now belongs to " + faction.GetFactionKey() + " with spawning enabled: " + site.m_Key);
		return true;
	}

	static void Cleanup(INF_ForestCampSite site)
	{
		foreach (IEntity prop : site.m_Props)
		{
			if (prop)
				SCR_EntityHelper.DeleteEntityAndChildren(prop);
		}
		site.m_Props.Clear();
		site.m_LayoutReady = false;
		site.m_CompositionReady = false;
		site.m_Friendly = false;
	}

	void Stop()
	{
		m_Stopped = true;
		GetGame().GetCallqueue().Remove(SearchTick);
		GetGame().GetCallqueue().Remove(UpdateLayouts);
		foreach (INF_ForestCampSite site : m_Sites)
			Cleanup(site);
	}
}
