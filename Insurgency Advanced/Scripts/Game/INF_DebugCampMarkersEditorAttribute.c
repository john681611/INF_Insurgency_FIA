[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class INF_DebugMarkersEditorAttribute : SCR_BaseEditorAttribute
{
	protected bool UsesCampLayer()
	{
		return false;
	}

	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item))
			return null;
		if (!INF_ZoneEditorContext.Grid(m_GameMode))
			return null;
		SCR_EditorManagerEntity editor = manager.GetManager();
		if (!editor || !editor.HasMode(EEditorMode.EDIT))
			return null;
		INS_IntelManager intel = INF_ZoneEditorContext.Intel(m_GameMode);
		if (!intel)
			return null;
		return SCR_BaseEditorAttributeVar.CreateBool(intel.INF_GetDebugMarkersForPlayer(editor.GetPlayerID(), UsesCampLayer()));
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
			intel.INF_SetDebugMarkersForPlayer(playerID, UsesCampLayer(), var.GetBool());
	}
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class INF_DebugCampMarkersEditorAttribute : INF_DebugMarkersEditorAttribute
{
	override protected bool UsesCampLayer()
	{
		return true;
	}
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class INF_DebugCacheMarkersEditorAttribute : INF_DebugMarkersEditorAttribute
{
}

modded class INS_DebugCacheMarkersEditorAttribute
{
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item))
			return null;
		if (!INF_ZoneEditorContext.Grid(m_GameMode))
			return super.ReadVariable(item, manager);
		SCR_EditorManagerEntity editor = manager.GetManager();
		if (!editor || !editor.HasMode(EEditorMode.EDIT))
			return null;
		INS_IntelManager intel = INF_ZoneEditorContext.Intel(m_GameMode);
		if (!intel)
			return null;
		return SCR_BaseEditorAttributeVar.CreateBool(intel.INF_GetDebugMarkersForPlayer(editor.GetPlayerID(), false));
	}

	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var,
		SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (!var || !IsGameMode(item))
			return;
		if (!INF_ZoneEditorContext.Grid(m_GameMode))
		{
			super.WriteVariable(item, var, manager, playerID);
			return;
		}
		INS_IntelManager intel = INF_ZoneEditorContext.Intel(m_GameMode);
		if (intel)
			intel.INF_SetDebugMarkersForPlayer(playerID, false, var.GetBool());
	}
}
