enum INF_EMissionEndMode
{
	CACHES,
	CACHES_CAMPS,
	CACHES_CAMPS_ZONES
}

class INF_CaptureObjectiveCells
{
	static void Add(map<string, bool> cells, string key, bool captured)
	{
		// Overlapping sources count once, after every source has earned capture credit.
		if (cells.Contains(key))
			captured = captured && cells.Get(key);
		cells.Set(key, captured);
	}

	static int Captured(map<string, bool> cells)
	{
		int count;
		foreach (string key, bool captured : cells)
		{
			if (captured)
				count++;
		}
		return count;
	}
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class INF_MissionEndModeEditorAttribute : SCR_BaseEditorAttribute
{
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item))
			return null;
		if (!INF_ZoneEditorContext.Grid(m_GameMode))
			return null;
		INS_IntelManager intel = INS_IntelManager.Cast(m_GameMode.FindComponent(INS_IntelManager));
		if (!intel)
			return null;
		return SCR_BaseEditorAttributeVar.CreateInt(intel.INF_GetMissionEndMode());
	}

	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var,
		SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (!var || !IsGameMode(item))
			return;
		if (!INF_ZoneEditorContext.Grid(m_GameMode))
			return;
		INS_IntelManager intel = INS_IntelManager.Cast(m_GameMode.FindComponent(INS_IntelManager));
		if (intel)
			intel.INF_SetMissionEndMode(var.GetInt());
	}

	override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
	{
		outEntries.Insert(new SCR_BaseEditorAttributeEntryText("Caches only"));
		outEntries.Insert(new SCR_BaseEditorAttributeEntryText("Caches and camps"));
		outEntries.Insert(new SCR_BaseEditorAttributeEntryText("Caches, camps and zones"));
		return outEntries.Count();
	}
}
