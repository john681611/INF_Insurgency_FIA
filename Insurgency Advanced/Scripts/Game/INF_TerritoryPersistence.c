class INF_TerritorySnapshot
{
	int m_Generation;
	ref map<string, string> m_Cells = new map<string, string>();
}

// Alternate complete snapshots so a failed write cannot destroy the last good save.
class INF_TerritoryPersistence
{
	protected static const string PREFIX = "$profile:INS_Saves/territory_";
	protected ref INF_TerritorySnapshot m_State = new INF_TerritorySnapshot();
	protected bool m_Ready;
	protected bool m_Dirty;
	protected int m_Slot = -1;
	protected float m_NextWriteWarning;

	bool IsDirty()
	{
		return m_Dirty;
	}

	protected void WriteFailure(string message)
	{
		float now = GetGame().GetWorld().GetWorldTime();
		if (now >= m_NextWriteWarning)
		{
			Print("[INF_Territory] " + message, LogLevel.ERROR);
			m_NextWriteWarning = now + 30000;
		}
	}

	bool IsReady()
	{
		return m_Ready;
	}

	protected string PathFor(int slot)
	{
		return PREFIX + slot.ToString() + ".dat";
	}

	protected INF_TerritorySnapshot ReadSlot(int slot)
	{
		FileSerializer file = new FileSerializer();
		if (!file.Open(PathFor(slot), FileMode.READ))
			return null;
		string magic, world, complete;
		int version, count;
		INF_TerritorySnapshot result = new INF_TerritorySnapshot();
		bool valid = file.Read(magic) && file.Read(version) && file.Read(world)
			&& file.Read(result.m_Generation) && file.Read(count);
		valid = valid && magic == "INF_TERRITORY" && version == 1
			&& world == GetGame().GetWorldFile() && result.m_Generation > 0
			&& count >= 0 && count <= 100000;
		for (int i = 0; valid && i < count; i++)
		{
			string key, camp;
			valid = file.Read(key) && file.Read(camp);
			valid = valid && ValidCellKey(key)
				&& camp.Length() <= 160 && !result.m_Cells.Contains(key);
			if (valid)
				result.m_Cells.Set(key, camp);
		}
		valid = valid && file.Read(complete) && complete == "COMPLETE";
		file.Close();
		if (!valid)
			return null;
		return result;
	}

	protected bool ValidCellKey(string key)
	{
		array<string> parts = {};
		key.Split(":", parts, false);
		if (parts.Count() != 2)
			return false;
		int x = parts[0].ToInt();
		int z = parts[1].ToInt();
		return x >= -10000 && x <= 10000 && z >= -10000 && z <= 10000
			&& key == x.ToString() + ":" + z.ToString();
	}

	void Load()
	{
		if (!Replication.IsServer())
			return;
		bool exists = false;
		for (int slot = 0; slot < 2; slot++)
		{
			if (!FileIO.FileExists(PathFor(slot)))
				continue;
			exists = true;
			INF_TerritorySnapshot candidate = ReadSlot(slot);
			if (!candidate)
			{
				Print("[INF_Territory] Invalid or incompatible snapshot: " + PathFor(slot), LogLevel.ERROR);
				continue;
			}
			if (candidate.m_Generation > m_State.m_Generation)
			{
				m_State = candidate;
				m_Slot = slot;
			}
		}
		m_Ready = !exists || m_Slot >= 0;
		if (!m_Ready)
			Print("[INF_Territory] No valid territory snapshot; startup blocked and existing files preserved.", LogLevel.ERROR);
	}

	bool HasCell(string key)
	{
		return m_Ready && m_State.m_Cells.Contains(key);
	}

	// Saved grid cells that no longer have a qualifying building keep their green marker only.
	// A saved camp cell missing from the registry still blocks startup.
	bool CollectSavedOnlyCells(array<ref INS_AmbientPatrolZoneState> zones, array<string> keys)
	{
		map<string, bool> known = new map<string, bool>();
		foreach (INS_AmbientPatrolZoneState state : zones)
			known.Set(INF_BuildingGrid.Key(state.m_vCenter), true);
		foreach (string key, string camp : m_State.m_Cells)
		{
			if (known.Contains(key))
				continue;
			if (!camp.IsEmpty())
			{
				Print("[INF_Territory] Saved secured camp cell is missing from the completed mission registry: " + key, LogLevel.ERROR);
				return false;
			}
			keys.Insert(key);
		}
		return true;
	}

	bool MatchesCamp(string key, INF_ForestCampSite camp)
	{
		string identity;
		if (camp)
			identity = camp.m_Key + "|" + INF_ForestCampRewards.SiteID(camp.m_Position);
		return HasCell(key) && m_State.m_Cells.Get(key) == identity;
	}

	bool Record(INS_AmbientPatrolZoneState state)
	{
		if (!m_Ready || !state.m_bINFPermanent || !state.m_bINFObjectiveCaptured)
			return false;
		string camp;
		if (state.m_INFCamp)
		{
			if (!state.m_INFCamp.m_Friendly)
				return false;
			camp = state.m_INFCamp.m_Key + "|" + INF_ForestCampRewards.SiteID(state.m_INFCamp.m_Position);
		}
		string key = INF_BuildingGrid.Key(state.m_vCenter);
		if (m_State.m_Cells.Contains(key) && m_State.m_Cells.Get(key) == camp)
			return false;
		m_State.m_Cells.Set(key, camp);
		m_Dirty = true;
		return true;
	}

	void Flush()
	{
		if (!Replication.IsServer() || !m_Ready || !m_Dirty)
			return;
		FileIO.MakeDirectory(INS_CACHE_SAVE_DIR);
		int slot = 0;
		if (m_Slot == 0)
			slot = 1;
		FileSerializer file = new FileSerializer();
		if (!file.Open(PathFor(slot), FileMode.WRITE))
		{
			WriteFailure("Cannot open territory save; progress remains dirty for retry.");
			return;
		}
		int generation = m_State.m_Generation + 1;
		bool success = file.Write("INF_TERRITORY") && file.Write(1)
			&& file.Write(GetGame().GetWorldFile()) && file.Write(generation)
			&& file.Write(m_State.m_Cells.Count());
		foreach (string key, string camp : m_State.m_Cells)
		{
			if (!file.Write(key) || !file.Write(camp))
				success = false;
		}
		if (success)
			success = file.Write("COMPLETE");
		file.Close();
		INF_TerritorySnapshot verified;
		if (success)
			verified = ReadSlot(slot);
		if (!verified || verified.m_Generation != generation || verified.m_Cells.Count() != m_State.m_Cells.Count())
		{
			WriteFailure("Territory snapshot write/verification failed; retaining previous snapshot and retrying.");
			return;
		}
		foreach (string savedKey, string savedCamp : m_State.m_Cells)
		{
			if (!verified.m_Cells.Contains(savedKey) || verified.m_Cells.Get(savedKey) != savedCamp)
			{
				WriteFailure("Territory snapshot contents failed verification; retaining previous snapshot and retrying.");
				return;
			}
		}
		m_State.m_Generation = generation;
		m_Slot = slot;
		m_Dirty = false;
		if (m_NextWriteWarning > 0)
		{
			Print("[INF_Territory] Territory save recovered after a write failure.");
			m_NextWriteWarning = 0;
		}
	}
}
