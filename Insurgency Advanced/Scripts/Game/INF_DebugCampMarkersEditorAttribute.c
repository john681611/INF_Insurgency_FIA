[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class INF_DebugCampMarkersEditorAttribute : SCR_BaseEditorAttribute
{
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item))
			return null;
		if (!INF_ZoneEditorContext.Grid(m_GameMode))
			return null;
		SCR_EditorManagerEntity editor = manager.GetManager();
		if (!editor || !editor.HasMode(EEditorMode.EDIT))
			return null;
		INS_IntelManager intel = INS_IntelManager.Cast(m_GameMode.FindComponent(INS_IntelManager));
		if (!intel)
			return null;
		return SCR_BaseEditorAttributeVar.CreateBool(intel.INF_GetDebugMarkersForPlayer(editor.GetPlayerID(), true));
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
			intel.INF_SetDebugMarkersForPlayer(playerID, true, var.GetBool());
	}
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
		INS_IntelManager intel = INS_IntelManager.Cast(m_GameMode.FindComponent(INS_IntelManager));
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
		INS_IntelManager intel = INS_IntelManager.Cast(m_GameMode.FindComponent(INS_IntelManager));
		if (intel)
			intel.INF_SetDebugMarkersForPlayer(playerID, false, var.GetBool());
	}
}
