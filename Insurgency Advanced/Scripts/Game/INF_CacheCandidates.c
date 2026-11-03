// Development tool that grows a mission's cache pool. Hand-placed INS_CacheSpawnPoint entities sit inside buildings;
// the exporter records each one in its building's space, copies every such spot into every other building of the same
// prefab (floor-checked), keeps a random sample of at most MAX_PER_TYPE per prefab, and writes them as a world layer of
// INS_CacheSpawnPoint entities. The base mod then picks them up like the hand-placed ones, and they can be checked in
// World Editor. See "Cache pool generator" in the README for the workflow.

// A hand-placed point expressed in its host building's space.
class INF_CacheAnchor
{
	string m_sKey;
	vector m_vLocal;
}

// Development tool, off by default. Never runs on a normal mission.
class INF_CacheCandidateExporter
{
	static const string OUTPUT = "$profile:INF/Caches_Generated.layer";
	static const int TILE_SIZE = 400;
	static const int BUDGET_MS = 250;
	static const float HOST_SEARCH_RADIUS = 25;
	static const float FLOOR_TOLERANCE = 0.6;
	static const float DUPLICATE_DISTANCE = 1;
	// Most candidates any one building type may contribute; the rest of that type's pool is dropped at random.
	static const int MAX_PER_TYPE = 30;

	protected ref array<vector> m_aTiles = {};
	protected ref array<ref INF_CacheAnchor> m_aAnchors = {};
	protected ref array<vector> m_aFixed = {};
	protected ref array<vector> m_aGenerated = {};
	protected ref map<string, ref array<vector>> m_mPool = new map<string, ref array<vector>>();
	protected vector m_vMin;
	protected vector m_vMax;
	protected vector m_vTile;
	protected vector m_vProbe;
	protected IEntity m_BestHost;
	protected float m_fBestDistance;
	protected int m_iRejected;
	protected int m_iDuplicates;

	void Start()
	{
		INS_CacheSpawnPoint.CollectPositions(m_aFixed);
		foreach (vector point : m_aFixed)
			AddAnchor(point);
		Print(string.Format("[INF_CacheExport] %1 hand-placed points, %2 anchored to a building.", m_aFixed.Count(), m_aAnchors.Count()), LogLevel.NORMAL);
		if (m_aAnchors.IsEmpty())
			return;
		GetGame().GetWorld().GetBoundBox(m_vMin, m_vMax);
		int firstX = Math.Floor(m_vMin[0] / TILE_SIZE);
		int firstZ = Math.Floor(m_vMin[2] / TILE_SIZE);
		int endX = Math.Ceil(m_vMax[0] / TILE_SIZE);
		int endZ = Math.Ceil(m_vMax[2] / TILE_SIZE);
		for (int x = firstX; x < endX; x++)
		{
			for (int z = firstZ; z < endZ; z++)
				m_aTiles.Insert(Vector(x * TILE_SIZE, 0, z * TILE_SIZE));
		}
		GetGame().GetCallqueue().CallLater(Tick, 0, true);
	}

	void Stop()
	{
		GetGame().GetCallqueue().Remove(Tick);
	}

	// Keys buildings by prefab, falling back to the model when a terrain object has no prefab.
	static string HostKey(IEntity entity)
	{
		string key = INF_BuildingGrid.PrefabNameLower(entity);
		if (!key.IsEmpty())
			return key;
		VObject model = entity.GetVObject();
		if (!model)
			return string.Empty;
		key = model.GetResourceName();
		key.ToLower();
		return key;
	}

	protected static bool IsTopLevelBuilding(IEntity entity)
	{
		if (!Building.Cast(entity))
			return false;
		IEntity parent = entity.GetParent();
		while (parent)
		{
			if (Building.Cast(parent))
				return false;
			parent = parent.GetParent();
		}
		return true;
	}

	protected void AddAnchor(vector point)
	{
		m_vProbe = point;
		m_BestHost = null;
		m_fBestDistance = 0;
		GetGame().GetWorld().QueryEntitiesBySphere(point, HOST_SEARCH_RADIUS, CollectHost);
		if (!m_BestHost)
		{
			Print(string.Format("[INF_CacheExport] No building contains the point at %1; skipped as an anchor.", point), LogLevel.WARNING);
			return;
		}
		INF_CacheAnchor anchor = new INF_CacheAnchor();
		anchor.m_sKey = HostKey(m_BestHost);
		anchor.m_vLocal = m_BestHost.CoordToLocal(point);
		m_aAnchors.Insert(anchor);
	}

	// The host is the building whose bounds hold the point; the closest origin wins when several do.
	protected bool CollectHost(IEntity entity)
	{
		if (!IsTopLevelBuilding(entity) || HostKey(entity).IsEmpty())
			return true;
		vector mins, maxs;
		entity.GetWorldBounds(mins, maxs);
		if (m_vProbe[0] < mins[0] || m_vProbe[0] > maxs[0] || m_vProbe[2] < mins[2] || m_vProbe[2] > maxs[2]
			|| m_vProbe[1] < mins[1] - 1 || m_vProbe[1] > maxs[1] + 1)
			return true;
		float distance = vector.DistanceXZ(entity.GetOrigin(), m_vProbe);
		if (!m_BestHost || distance < m_fBestDistance)
		{
			m_BestHost = entity;
			m_fBestDistance = distance;
		}
		return true;
	}

	protected void Tick()
	{
		int started = System.GetTickCount();
		while (!m_aTiles.IsEmpty() && System.GetTickCount() - started < BUDGET_MS)
		{
			int last = m_aTiles.Count() - 1;
			m_vTile = m_aTiles[last];
			m_aTiles.Remove(last);
			vector minimum = Vector(m_vTile[0], m_vMin[1], m_vTile[2]);
			vector maximum = Vector(m_vTile[0] + TILE_SIZE, m_vMax[1], m_vTile[2] + TILE_SIZE);
			GetGame().GetWorld().QueryEntitiesByAABB(minimum, maximum, CollectBuilding);
		}
		if (m_aTiles.IsEmpty())
			Finish();
	}

	protected bool CollectBuilding(IEntity entity)
	{
		// AABB queries can return the same large entity in adjacent tiles.
		vector origin = entity.GetOrigin();
		if (origin[0] < m_vTile[0] || origin[0] >= m_vTile[0] + TILE_SIZE
			|| origin[2] < m_vTile[2] || origin[2] >= m_vTile[2] + TILE_SIZE)
			return true;
		if (!IsTopLevelBuilding(entity))
			return true;
		string key = HostKey(entity);
		if (key.IsEmpty())
			return true;
		array<INF_CacheAnchor> matches = {};
		foreach (INF_CacheAnchor anchor : m_aAnchors)
		{
			if (anchor.m_sKey == key)
				matches.Insert(anchor);
		}
		if (matches.IsEmpty())
			return true;
		array<vector> pool = m_mPool.Get(key);
		if (!pool)
		{
			pool = {};
			m_mPool.Set(key, pool);
		}
		// Every spot the type has goes in; the per-type cap later picks a random sample across buildings and spots.
		foreach (INF_CacheAnchor chosen : matches)
		{
			vector world = entity.CoordToParent(chosen.m_vLocal);
			if (NearFixed(world))
			{
				m_iDuplicates++;
				continue;
			}
			if (!OnFloor(world))
			{
				m_iRejected++;
				continue;
			}
			pool.Insert(world);
		}
		return true;
	}

	// Common buildings (garages, barns) would otherwise dominate the pool, so each type contributes a random sample.
	protected void CapPerType()
	{
		for (int i = 0; i < m_mPool.Count(); i++)
		{
			array<vector> pool = m_mPool.GetElement(i);
			int available = pool.Count();
			for (int j = available - 1; j > 0; j--)
			{
				int k = Math.RandomInt(0, j + 1);
				vector swap = pool[j];
				pool[j] = pool[k];
				pool[k] = swap;
			}
			int kept = Math.Min(available, MAX_PER_TYPE);
			for (int n = 0; n < kept; n++)
				m_aGenerated.Insert(pool[n]);
			Print(string.Format("[INF_CacheExport]   %1: kept %2 of %3", m_mPool.GetKey(i), kept, available), LogLevel.NORMAL);
		}
	}

	protected bool NearFixed(vector position)
	{
		foreach (vector fixedPoint : m_aFixed)
		{
			if (vector.Distance(fixedPoint, position) < DUPLICATE_DISTANCE)
				return true;
		}
		return false;
	}

	// The anchor stands on a floor, so a short downward trace must meet one at about the same height.
	protected bool OnFloor(vector position)
	{
		TraceParam trace = new TraceParam();
		trace.Start = position + Vector(0, 1.5, 0);
		trace.End = position - Vector(0, 0.6, 0);
		trace.Flags = TraceFlags.ENTS | TraceFlags.WORLD;
		float fraction = GetGame().GetWorld().TraceMove(trace, null);
		if (fraction >= 1)
			return false;
		float hitHeight = trace.Start[1] - fraction * (trace.Start[1] - trace.End[1]);
		return Math.AbsFloat(hitHeight - position[1]) <= FLOOR_TOLERANCE;
	}

	protected void Finish()
	{
		Stop();
		CapPerType();
		array<string> lines = {};
		if (!m_aGenerated.IsEmpty())
			lines.Insert("$grp GenericEntity : \"{D1F622E16EA54223}Prefabs/INS_CacheSpawnPoint.et\" {");
		foreach (int index, vector position : m_aGenerated)
		{
			lines.Insert(string.Format(" Generated_%1 {", index + 1));
			lines.Insert("  components {");
			lines.Insert("   SCR_EditableEntityComponent \"{C37A976D24C696DC}\" {");
			lines.Insert("    Enabled 0");
			lines.Insert("   }");
			lines.Insert("  }");
			lines.Insert(string.Format("  coords %1 %2 %3", position[0], position[1], position[2]));
			lines.Insert(string.Format("  angles 0 %1 0", Math.RandomInt(0, 360)));
			lines.Insert(" }");
		}
		if (!m_aGenerated.IsEmpty())
			lines.Insert("}");
		FileIO.MakeDirectory("$profile:INF");
		SCR_FileIOHelper.WriteFileContent(OUTPUT, lines);
		Print(string.Format("[INF_CacheExport] Wrote %1 candidates to %2 (%3 rejected by the floor check, %4 skipped as copies of hand-placed points).", m_aGenerated.Count(), OUTPUT, m_iRejected, m_iDuplicates), LogLevel.NORMAL);
	}
}
