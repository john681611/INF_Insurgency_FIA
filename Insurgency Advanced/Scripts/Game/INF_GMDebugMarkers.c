enum INF_EDebugMarkerLayers
{
	CACHES = 1,
	CAMPS = 2
}

modded class SCR_MapMarkerManagerComponent
{
	void INF_RemoveLocalDebugMarker(SCR_MapMarkerBase marker)
	{
		if (!marker || marker.GetMarkerID() != -1)
		{
			Print("[INF_DebugMarkers] Refusing local cleanup of a missing or synchronised marker.", LogLevel.ERROR);
			return;
		}
		// Native removal only removes active markers; off-screen markers live in a separate list.
		m_aDisabledMarkers.RemoveItem(marker);
		RemoveStaticMarker(marker);
	}
}

class INF_DebugMarkerAccess
{
	static bool IsGM(int playerId)
	{
		if (playerId <= 0)
			return false;
		SCR_EditorManagerEntity editor;
		if (Replication.IsServer())
		{
			SCR_EditorManagerCore core = SCR_EditorManagerCore.Cast(SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore));
			if (core)
				editor = core.GetEditorManager(playerId);
		}
		else if (playerId == SCR_PlayerController.GetLocalPlayerId())
			editor = SCR_EditorManagerEntity.GetInstance();
		return editor && editor.HasMode(EEditorMode.EDIT);
	}
}

modded class SCR_PlayerController
{
	void INF_SendPrisonerResponse(string message)
	{
		if (!Replication.IsServer())
			return;
		if (GetPlayerId() == SCR_PlayerController.GetLocalPlayerId())
			INF_RpcPrisonerResponse(message);
		else
			Rpc(INF_RpcPrisonerResponse, message);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void INF_RpcPrisonerResponse(string message)
	{
		if (GetPlayerId() != SCR_PlayerController.GetLocalPlayerId())
			return;
		SCR_HintManagerComponent hints = SCR_HintManagerComponent.GetInstance();
		if (hints)
			SCR_HintManagerComponent.ShowCustomHint(message, "Prisoner", 10);
		else
			Print("[INF_Prisoners] Cannot show prisoner response: hint manager missing. " + message, LogLevel.ERROR);
	}

	protected int m_iINFDebugMarkerLayers;
	protected int m_iINFDebugSnapshotRevision = -1;
	protected int m_iINFDebugSnapshotLayers = -1;

	bool INF_DebugSnapshotChanged(int layers, int revision)
	{
		if (m_iINFDebugSnapshotLayers == layers && m_iINFDebugSnapshotRevision == revision)
			return false;
		m_iINFDebugSnapshotLayers = layers;
		m_iINFDebugSnapshotRevision = revision;
		return true;
	}

	int INF_GetDebugMarkerLayers()
	{
		return m_iINFDebugMarkerLayers;
	}

	void INF_SendDebugMarkerSnapshot(int layers, array<vector> cachePositions, array<vector> campPositions)
	{
		if (!Replication.IsServer())
			return;
		m_iINFDebugMarkerLayers = layers;
		if (GetPlayerId() == SCR_PlayerController.GetLocalPlayerId())
			INF_RpcDebugMarkerSnapshot(layers, cachePositions, campPositions);
		else
			Rpc(INF_RpcDebugMarkerSnapshot, layers, cachePositions, campPositions);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void INF_RpcDebugMarkerSnapshot(int layers, array<vector> cachePositions, array<vector> campPositions)
	{
		if (GetPlayerId() != SCR_PlayerController.GetLocalPlayerId())
			return;
		if (!INF_DebugMarkerAccess.IsGM(GetPlayerId()))
			layers = 0;
		m_iINFDebugMarkerLayers = layers;
		INS_IntelManager intel = INS_IntelManager.GetInstance();
		if (!intel)
		{
			Print("[INF_DebugMarkers] Cannot apply GM debug snapshot: intel manager unavailable.", LogLevel.ERROR);
			return;
		}
		intel.INF_ApplyDebugMarkerSnapshot(layers, cachePositions, campPositions);
	}
}
