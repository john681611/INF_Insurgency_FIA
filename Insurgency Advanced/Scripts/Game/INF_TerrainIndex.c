// Derived terrain data survives clean mission ends; selections and progress never belong here.
class INF_TerrainIndex
{
	static const int VERSION = 5;
	static const string DIRECTORY = "$profile:INF_TerrainIndexes";
	protected string m_World;
	protected string m_MapPath;
	protected string m_CampPath;
	protected string m_CampContext;
	protected int m_Revision;
	protected vector m_Min;
	protected vector m_Max;

	void INF_TerrainIndex(INS_AmbientPatrolManager manager)
	{
		m_World = GetGame().GetWorldFile();
		m_Revision = manager.INF_GetTerrainIndexRevision();
		GetGame().GetWorld().GetBoundBox(m_Min, m_Max);
		if (m_World.IsEmpty() || m_Revision < 1)
		{
			Print("[INF_TerrainIndex] Missing world identity or invalid index revision; disk reuse disabled.", LogLevel.ERROR);
			return;
		}
		string key = m_World;
		array<string> unsafe = {"\\", "/", ":", "{", "}", "<", ">", "\"", "|", "?", "*"};
		foreach (string character : unsafe)
			key.Replace(character, "_");
		m_MapPath = DIRECTORY + "/" + key + "_map.dat";
		m_CampPath = DIRECTORY + "/" + key + "_camps.dat";
		array<string> exclusions = {};
		array<INS_SafeZone> safeZones = {};
		INS_SafeZone.CollectZones(safeZones);
		foreach (INS_SafeZone zone : safeZones)
		{
			if (zone.GetOwner())
				exclusions.Insert(zone.GetOwner().GetOrigin().ToString() + ":" + zone.GetRadius().ToString());
		}
		exclusions.Sort();
		m_CampContext = manager.INF_GetCampComposition() + ":" + manager.INF_GetCampFootprintRadius().ToString();
		m_CampContext += ":vegetation-tolerant-v2:forest-interior-v1:reward-vegetation-v1:camp-spacing-v2";
		foreach (string exclusion : exclusions)
			m_CampContext += "|" + exclusion;
	}

	protected bool WriteHeader(FileSerializer file, int kind)
	{
		return file.Write("INF_TERRAIN_INDEX") && file.Write(VERSION) && file.Write(kind)
			&& file.Write(m_World) && file.Write(m_Revision) && file.Write(m_Min) && file.Write(m_Max)
			&& file.Write(m_CampContext);
	}

	protected bool ReadHeader(FileSerializer file, int kind)
	{
		string magic, world, context;
		int version, storedKind, revision;
		vector minimum, maximum;
		if (!file.Read(magic) || !file.Read(version) || !file.Read(storedKind) || !file.Read(world)
			|| !file.Read(revision) || !file.Read(minimum) || !file.Read(maximum) || !file.Read(context))
			return false;
		return magic == "INF_TERRAIN_INDEX" && version == VERSION && storedKind == kind
			&& world == m_World && revision == m_Revision && minimum == m_Min && maximum == m_Max
			&& (kind == 0 || context == m_CampContext);
	}

	protected bool Invalid(FileSerializer file, string path)
	{
		file.Close();
		Print("[INF_TerrainIndex] Stale or incomplete derived index; rebuilding: " + path, LogLevel.WARNING);
		return false;
	}

	protected bool ValidPosition(vector pos)
	{
		for (int axis = 0; axis < 3; axis++)
		{
			if (!(pos[axis] >= -1000000 && pos[axis] <= 1000000))
				return false;
		}
		return true;
	}

	protected bool ReadPositions(FileSerializer file, array<vector> positions)
	{
		int count;
		if (!file.Read(count) || count < 0 || count > 1000000)
			return false;
		for (int i = 0; i < count; i++)
		{
			vector pos;
			if (!file.Read(pos) || !ValidPosition(pos))
				return false;
			positions.Insert(pos);
		}
		return true;
	}

	protected bool WritePositions(FileSerializer file, array<vector> positions)
	{
		bool success = file.Write(positions.Count());
		foreach (vector pos : positions)
		{
			if (!file.Write(pos))
				success = false;
		}
		return success;
	}

	bool LoadMap(out array<vector> buildings, out array<vector> settlements, out array<ref INF_ForestCell> cells)
	{
		if (!Replication.IsServer() || m_MapPath.IsEmpty() || !FileIO.FileExists(m_MapPath))
			return false;
		FileSerializer file = new FileSerializer();
		if (!file.Open(m_MapPath, FileMode.READ))
		{
			Print("[INF_TerrainIndex] Cannot open map index; rebuilding: " + m_MapPath, LogLevel.WARNING);
			return false;
		}
		array<vector> readBuildings = {};
		array<vector> readSettlements = {};
		array<ref INF_ForestCell> readCells = {};
		if (!ReadHeader(file, 0) || !ReadPositions(file, readBuildings) || !ReadPositions(file, readSettlements))
			return Invalid(file, m_MapPath);
		int count;
		if (!file.Read(count) || count < 0 || count > 1000000)
			return Invalid(file, m_MapPath);
		map<string, bool> keys = new map<string, bool>();
		for (int i = 0; i < count; i++)
		{
			INF_ForestCell cell = new INF_ForestCell();
			if (!file.Read(cell.m_Center) || !file.Read(cell.m_Trees) || !ValidPosition(cell.m_Center)
				|| cell.m_Trees < 1 || cell.m_Trees > 1000000)
				return Invalid(file, m_MapPath);
			string key = INF_BuildingGrid.Key(cell.m_Center);
			if (keys.Contains(key))
				return Invalid(file, m_MapPath);
			keys.Set(key, true);
			readCells.Insert(cell);
		}
		string footer;
		if (!file.Read(footer) || footer != "COMPLETE")
			return Invalid(file, m_MapPath);
		file.Close();
		buildings = readBuildings;
		settlements = readSettlements;
		cells = readCells;
		Print(string.Format("[INF_TerrainIndex] Reused map index: buildings=%1, settlements=%2, woodland cells=%3.",
			buildings.Count(), settlements.Count(), cells.Count()));
		return true;
	}

	void SaveMap(array<vector> buildings, array<vector> settlements, array<ref INF_ForestCell> cells)
	{
		if (!Replication.IsServer() || m_MapPath.IsEmpty())
			return;
		FileIO.MakeDirectory(DIRECTORY);
		FileSerializer file = new FileSerializer();
		if (!file.Open(m_MapPath, FileMode.WRITE))
		{
			Print("[INF_TerrainIndex] Cannot write map index: " + m_MapPath, LogLevel.ERROR);
			return;
		}
		bool success = WriteHeader(file, 0);
		if (!WritePositions(file, buildings) || !WritePositions(file, settlements) || !file.Write(cells.Count()))
			success = false;
		foreach (INF_ForestCell cell : cells)
		{
			if (!file.Write(cell.m_Center) || !file.Write(cell.m_Trees))
				success = false;
		}
		if (!file.Write("COMPLETE"))
			success = false;
		file.Close();
		if (!success)
			Print("[INF_TerrainIndex] Map index write incomplete; it will be rebuilt next startup.", LogLevel.ERROR);
		else
			Print("[INF_TerrainIndex] Saved reusable map index: " + m_MapPath);
	}

	bool LoadCandidates(out array<ref INF_ForestCampSite> candidates)
	{
		if (!Replication.IsServer() || m_CampPath.IsEmpty() || !FileIO.FileExists(m_CampPath))
			return false;
		FileSerializer file = new FileSerializer();
		if (!file.Open(m_CampPath, FileMode.READ))
		{
			Print("[INF_TerrainIndex] Cannot open camp candidate pool; rebuilding: " + m_CampPath, LogLevel.WARNING);
			return false;
		}
		int count;
		if (!ReadHeader(file, 1) || !file.Read(count) || count < 1 || count > INF_ForestCamps.MAX_CANDIDATES)
			return Invalid(file, m_CampPath);
		array<ref INF_ForestCampSite> readCandidates = {};
		array<string> keys = {};
		for (int i = 0; i < count; i++)
		{
			INF_ForestCampSite site = new INF_ForestCampSite();
			if (!file.Read(site.m_Key) || !file.Read(site.m_Center) || !file.Read(site.m_Position) || !file.Read(site.m_Yaw)
				|| site.m_Key != INF_BuildingGrid.Key(site.m_Center) || keys.Contains(site.m_Key)
				|| !ValidPosition(site.m_Center) || !ValidPosition(site.m_Position)
				|| vector.DistanceSqXZ(site.m_Center, site.m_Position) > 22500
				|| !(site.m_Yaw >= -360 && site.m_Yaw <= 360))
				return Invalid(file, m_CampPath);
			keys.Insert(site.m_Key);
			readCandidates.Insert(site);
		}
		string footer;
		if (!file.Read(footer) || footer != "COMPLETE")
			return Invalid(file, m_CampPath);
		file.Close();
		candidates = readCandidates;
		Print(string.Format("[INF_TerrainIndex] Reused %1 validated potential camp locations.", candidates.Count()));
		return true;
	}

	void SaveCandidates(array<ref INF_ForestCampSite> candidates)
	{
		if (!Replication.IsServer() || m_CampPath.IsEmpty() || candidates.IsEmpty())
			return;
		FileIO.MakeDirectory(DIRECTORY);
		FileSerializer file = new FileSerializer();
		if (!file.Open(m_CampPath, FileMode.WRITE))
		{
			Print("[INF_TerrainIndex] Cannot write camp candidate pool: " + m_CampPath, LogLevel.ERROR);
			return;
		}
		bool success = WriteHeader(file, 1) && file.Write(candidates.Count());
		foreach (INF_ForestCampSite site : candidates)
		{
			if (!file.Write(site.m_Key) || !file.Write(site.m_Center) || !file.Write(site.m_Position) || !file.Write(site.m_Yaw))
				success = false;
		}
		if (!file.Write("COMPLETE"))
			success = false;
		file.Close();
		if (!success)
			Print("[INF_TerrainIndex] Camp pool write incomplete; it will be rebuilt next startup.", LogLevel.ERROR);
		else
			Print(string.Format("[INF_TerrainIndex] Saved %1 potential camp locations, independently of this mission's selection.", candidates.Count()));
	}
}
