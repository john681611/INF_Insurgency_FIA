// Extra objectives that can each be required for victory; caches are always required.
enum INF_EMissionObjective
{
	CAMPS,
	ZONES,
	OFFICERS
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
class INF_MissionObjectiveEditorAttribute : SCR_BaseEditorAttribute
{
	[Attribute("0")]
	protected int m_iObjective;

	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item))
			return null;
		if (!INF_ZoneEditorContext.Grid(m_GameMode))
			return null;
		INS_IntelManager intel = INF_ZoneEditorContext.Intel(m_GameMode);
		if (!intel)
			return null;
		return SCR_BaseEditorAttributeVar.CreateBool(intel.INF_IsObjectiveRequired(m_iObjective));
	}

	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var,
		SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (!var || !IsGameMode(item))
			return;
		if (!INF_ZoneEditorContext.Grid(m_GameMode))
			return;
		INS_IntelManager intel = INF_ZoneEditorContext.Intel(m_GameMode);
		if (intel)
			intel.INF_SetObjectiveRequired(m_iObjective, var.GetBool());
	}
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class INF_RequireCampsEditorAttribute : INF_MissionObjectiveEditorAttribute
{
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class INF_RequireZonesEditorAttribute : INF_MissionObjectiveEditorAttribute
{
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class INF_RequireOfficersEditorAttribute : INF_MissionObjectiveEditorAttribute
{
}
