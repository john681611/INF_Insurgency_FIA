enum INF_EGarrisonMapState
{
	DORMANT,
	ACTIVE,
	CLEARED,
	COUNTERATTACK,
	CAPTURING,
	INBOUND,
	SAFE
}

modded class INS_GarrisonEnabledEditorAttribute
{
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (INF_ZoneEditorContext.Grid(GetGame().GetGameMode()))
			return null;
		return super.ReadVariable(item, manager);
	}
}

modded class INS_GarrisonCacheGroupsEditorAttribute
{
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (INF_ZoneEditorContext.Grid(GetGame().GetGameMode()))
			return null;
		return super.ReadVariable(item, manager);
	}
}

class INF_GarrisonMapAreaClass : SCR_MapMarkerDotCircleClass
{
}

modded class SCR_MapDotCircleHandler
{
	protected CanvasWidget m_INFZoneCanvas;
	protected ref array<ref CanvasWidgetCommand> m_INFZoneCommands = {};

	override void CreateDots()
	{
		if (!INF_GarrisonMapArea.Cast(m_DotMarkerEnt))
		{
			super.CreateDots();
			return;
		}

		if (!m_wFrame || m_fActualRadius <= 0)
			return;

		if (!m_INFZoneCanvas)
		{
			m_INFZoneCanvas = CanvasWidget.Cast(GetGame().GetWorkspace().CreateWidget(
				WidgetType.CanvasWidgetTypeID, WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR,
				Color.FromRGBA(255, 255, 255, 255), -1, m_wFrame));
			if (!m_INFZoneCanvas)
			{
				Print("[INF_GarrisonMapAreas] Cannot create the filled map-area canvas.", LogLevel.ERROR);
				return;
			}
		}

		// Map zoom and canvas vertices use pixels; FrameSlot uses DPI-scaled layout units.
		float layoutRadius = GetGame().GetWorkspace().DPIUnscale(m_fActualRadius);
		FrameSlot.SetAnchor(m_INFZoneCanvas, 0, 0);
		FrameSlot.SetAlignment(m_INFZoneCanvas, 0, 0);
		FrameSlot.SetPos(m_INFZoneCanvas, -layoutRadius, -layoutRadius);
		FrameSlot.SetSize(m_INFZoneCanvas, layoutRadius * 2, layoutRadius * 2);

		PolygonDrawCommand fill = new PolygonDrawCommand();
		fill.m_iColor = m_DotMarkerEnt.m_DotColor.PackToInt();
		fill.m_Vertices = new array<float>();
		INF_GarrisonMapArea area = INF_GarrisonMapArea.Cast(m_DotMarkerEnt);
		if (area.INF_IsSquare())
		{
			float diameter = m_fActualRadius * 2;
			fill.m_Vertices = {0, 0, diameter, 0, diameter, diameter, 0, diameter};
		}
		else
			m_INFZoneCanvas.TessellateCircle(Vector(m_fActualRadius, m_fActualRadius, 0), m_fActualRadius, 96, fill.m_Vertices);
		m_INFZoneCommands.Clear();
		m_INFZoneCommands.Insert(fill);
		m_INFZoneCanvas.SetDrawCommands(m_INFZoneCommands);
	}

	override void HandlerDeattached(Widget w)
	{
		if (INF_GarrisonMapArea.Cast(m_DotMarkerEnt) && m_MapEnt)
			m_MapEnt.GetOnMapZoom().Remove(OnMapZoom);

		m_INFZoneCanvas = null;
		m_INFZoneCommands.Clear();
		super.HandlerDeattached(w);
	}
}

class INF_GarrisonMapArea : SCR_MapMarkerDotCircle
{
	// One opacity for every live state (active, capturing, inbound, counterattack).
	protected static const int ACTIVE_ALPHA = 160;

	[RplProp(onRplName: "INF_ApplyAppearance")]
	protected float m_fINFZoneRadius;

	[RplProp(onRplName: "INF_ApplyAppearance")]
	protected INF_EGarrisonMapState m_eINFState;

	[RplProp(onRplName: "INF_ApplyAppearance")]
	protected bool m_bINFSquare;

	[RplProp(onRplName: "INF_ApplyAppearance")]
	protected int m_iINFBudget;

	bool INF_IsSquare()
	{
		return m_bINFSquare;
	}

	override protected void EOnInit(IEntity owner)
	{
		INF_ApplyAppearance();
		super.EOnInit(owner);

		BaseRplComponent rpl = BaseRplComponent.Cast(FindComponent(BaseRplComponent));
		if (rpl && rpl.IsMaster())
		{
			m_vPos = GetOrigin();
			ClearEventMask(EntityEvent.FRAME);
			rpl.EnableSpatialRelevancy(false);
			Replication.BumpMe();
		}
	}

	void INF_SetZoneState(vector center, float radius, INF_EGarrisonMapState state, bool square = false, int budget = 0)
	{
		if (m_vPos == center && m_fINFZoneRadius == radius && m_eINFState == state && m_bINFSquare == square && m_iINFBudget == budget)
			return;

		SetOrigin(center);
		m_vPos = center;
		m_fINFZoneRadius = radius;
		m_eINFState = state;
		m_bINFSquare = square;
		m_iINFBudget = budget;
		INF_ApplyAppearance();
		Replication.BumpMe();
	}

	protected void INF_ApplyAppearance()
	{
		m_fRadius = m_fINFZoneRadius;
		switch (m_eINFState)
		{
			case INF_EGarrisonMapState.DORMANT:
				// Stronger garrisons are more opaque: 2 AI is faint but visible, 16 AI stays well below ACTIVE.
				// Linear from 35 at 2 AI (and camp/cache cells, floored to 2) to 100 at 16 AI.
				m_DotColor = Color.FromRGBA(220, 30, 30, 35 + (Math.Clamp(m_iINFBudget, 2, 16) - 2) * 65 / 14);
				break;
			case INF_EGarrisonMapState.ACTIVE:
				m_DotColor = Color.FromRGBA(220, 30, 30, ACTIVE_ALPHA);
				break;
			case INF_EGarrisonMapState.CAPTURING:
				m_DotColor = Color.FromRGBA(255, 220, 40, ACTIVE_ALPHA);
				break;
			case INF_EGarrisonMapState.INBOUND:
				m_DotColor = Color.FromRGBA(155, 90, 180, ACTIVE_ALPHA);
				break;
			case INF_EGarrisonMapState.CLEARED:
				m_DotColor = Color.FromRGBA(30, 190, 60, 64);
				break;
			case INF_EGarrisonMapState.COUNTERATTACK:
				m_DotColor = Color.FromRGBA(255, 145, 20, ACTIVE_ALPHA);
				break;
			case INF_EGarrisonMapState.SAFE:
				m_DotColor = Color.FromRGBA(40, 140, 255, 70);
				break;
		}

		if (!m_wRoot)
			return;

		SCR_MapDotCircleHandler handler = SCR_MapDotCircleHandler.Cast(m_wRoot.FindHandler(SCR_MapDotCircleHandler));
		SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
		if (handler && mapEntity)
			handler.OnMapZoom(mapEntity.GetCurrentZoom());
	}
}







modded class INS_GarrisonManager
{
	[Attribute("0", UIWidgets.CheckBox, "Show building-grid and camp squares, coloured by zone and counterattack state.")]
	protected bool m_bINFShowZoneAreas;

	protected ref map<string, INF_GarrisonMapArea> m_INFZoneAreas = new map<string, INF_GarrisonMapArea>();
	protected ref array<INF_GarrisonMapArea> m_INFStaleAreas = {};
	protected ref array<string> m_aINFSavedCellKeys = {};
	protected ref Resource m_INFZoneAreaResource;
	protected float m_fINFNextAreaSpawnAttempt;
	protected float m_fINFNextAreaSpawnWarning;
	protected INF_ZoneStateSystem m_INFObservedZones;
	protected ref map<string, INS_AmbientPatrolZoneState> m_INFDirtyAreas = new map<string, INS_AmbientPatrolZoneState>();
	protected bool m_bINFAreaUpdateQueued;

	void INF_BindZoneStates(INF_ZoneStateSystem zones)
	{
		if (!Replication.IsServer() || !m_bINFShowZoneAreas || !zones || zones == m_INFObservedZones)
			return;
		if (m_INFObservedZones)
			m_INFObservedZones.GetOnChanged().Remove(INF_QueueZoneArea);
		m_INFObservedZones = zones;
		zones.GetOnChanged().Insert(INF_QueueZoneArea);
		foreach (INS_AmbientPatrolZoneState state : zones.GetZones())
			INF_QueueZoneArea(state);
	}

	protected void INF_QueueZoneArea(INS_AmbientPatrolZoneState state)
	{
		m_INFDirtyAreas.Set(INF_BuildingGrid.Key(state.m_vCenter), state);
		if (!m_bINFAreaUpdateQueued && m_INFZoneAreaResource)
		{
			m_bINFAreaUpdateQueued = true;
			GetGame().GetCallqueue().CallLater(INF_UpdateZoneAreas, 0, false);
		}
	}

	override void OnGameModeStart()
	{
		if (!INF_UsesBlockControl())
		{
			super.OnGameModeStart();
			return;
		}
		// The inherited component is retained only as the existing map-area host.
		// Do not start upstream garrison discovery, persistence or its spawn queue.
		m_bEnabled = false;
		if (Replication.IsServer())
			Print("[INF_BuildingGrid] Authored garrisons retired; building-grid and camps own zone infantry.");

		if (!Replication.IsServer() || !m_bINFShowZoneAreas)
			return;

		m_INFZoneAreaResource = Resource.Load("{6AA19CB200000002}Prefabs/Markers/INF_GarrisonMapArea.et");
		if (!m_INFZoneAreaResource || !m_INFZoneAreaResource.IsValid())
		{
			Print("[INF_GarrisonMapAreas] Cannot load the garrison map-area prefab.", LogLevel.ERROR);
			return;
		}
		INF_RemoveStaleAreas();

		INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(GetOwner().FindComponent(INS_AmbientPatrolManager));
		if (patrols)
			INF_BindZoneStates(patrols.INF_GetZoneStates());
		if (!m_INFDirtyAreas.IsEmpty() && !m_bINFAreaUpdateQueued)
		{
			m_bINFAreaUpdateQueued = true;
			GetGame().GetCallqueue().CallLater(INF_UpdateZoneAreas, 0, false);
		}
	}

	// Areas left over from an earlier session or build would otherwise stay on the map untracked.
	protected void INF_RemoveStaleAreas()
	{
		vector minimum, maximum;
		GetGame().GetWorld().GetBoundBox(minimum, maximum);
		m_INFStaleAreas.Clear();
		GetGame().GetWorld().QueryEntitiesByAABB(minimum, maximum, INF_CollectStaleArea);
		foreach (INF_GarrisonMapArea stale : m_INFStaleAreas)
			SCR_EntityHelper.DeleteEntityAndChildren(stale);
		if (!m_INFStaleAreas.IsEmpty())
			Print(string.Format("[INF_GarrisonMapAreas] Removed %1 stale map area(s).", m_INFStaleAreas.Count()));
		m_INFStaleAreas.Clear();
	}

	protected bool INF_CollectStaleArea(IEntity entity)
	{
		INF_GarrisonMapArea area = INF_GarrisonMapArea.Cast(entity);
		if (area)
			m_INFStaleAreas.Insert(area);
		return true;
	}

	protected INF_GarrisonMapArea INF_CreateZoneArea(vector center)
	{
		float now = GetGame().GetWorld().GetWorldTime();
		if (now < m_fINFNextAreaSpawnAttempt)
			return null;
		EntitySpawnParams spawnParams = new EntitySpawnParams();
		spawnParams.TransformMode = ETransformMode.WORLD;
		spawnParams.Transform[3] = center;
		IEntity entity = GetGame().SpawnEntityPrefab(m_INFZoneAreaResource, GetGame().GetWorld(), spawnParams);
		INF_GarrisonMapArea area = INF_GarrisonMapArea.Cast(entity);
		if (!area)
		{
			if (entity)
				SCR_EntityHelper.DeleteEntityAndChildren(entity);
			m_fINFNextAreaSpawnAttempt = now + 5000;
			if (now >= m_fINFNextAreaSpawnWarning)
			{
				Print("[INF_GarrisonMapAreas] Failed to spawn a map area; retrying while existing areas continue updating.", LogLevel.ERROR);
				m_fINFNextAreaSpawnWarning = now + 30000;
			}
		}

		return area;
	}

	// Blue squares for every grid cell a safe zone excludes; static map decoration, not tracked as zones.
	void INF_ShowSafeZones()
	{
		if (!Replication.IsServer() || !m_bINFShowZoneAreas || !m_INFZoneAreaResource)
			return;
		array<INS_SafeZone> safeZones = {};
		INS_SafeZone.CollectZones(safeZones);
		foreach (INS_SafeZone safeZone : safeZones)
		{
			if (!safeZone.GetOwner())
				continue;
			vector origin = safeZone.GetOwner().GetOrigin();
			float radius = safeZone.GetRadius();
			int minX = INF_BuildingGrid.CellIndex(origin[0] - radius);
			int maxX = INF_BuildingGrid.CellIndex(origin[0] + radius);
			int minZ = INF_BuildingGrid.CellIndex(origin[2] - radius);
			int maxZ = INF_BuildingGrid.CellIndex(origin[2] + radius);
			for (int x = minX; x <= maxX; x++)
			{
				for (int z = minZ; z <= maxZ; z++)
				{
					vector center = Vector(x * INF_BuildingGrid.SIZE + INF_BuildingGrid.HALF, 0,
						z * INF_BuildingGrid.SIZE + INF_BuildingGrid.HALF);
					string key = "safe:" + x.ToString() + ":" + z.ToString();
					if (m_INFZoneAreas.Contains(key) || !INF_BuildingGrid.Overlaps(center, origin, radius))
						continue;
					center[1] = GetGame().GetWorld().GetSurfaceY(center[0], center[2]);
					INF_GarrisonMapArea area = INF_CreateZoneArea(center);
					if (!area)
					{
						INF_QueueStaticAreaRetry();
						return;
					}
					m_INFZoneAreas.Set(key, area);
					area.INF_SetZoneState(center, INF_BuildingGrid.HALF, INF_EGarrisonMapState.SAFE, true);
				}
			}
		}
	}

	protected void INF_QueueStaticAreaRetry()
	{
		GetGame().GetCallqueue().Remove(INF_RetryStaticAreas);
		GetGame().GetCallqueue().CallLater(INF_RetryStaticAreas, 5000, false);
	}

	protected void INF_RetryStaticAreas()
	{
		INF_ShowSafeZones();
		INF_ShowSavedCells(m_aINFSavedCellKeys);
	}

	// Green markers for saved secured cells that have no zone; they are not tracked as zones.
	void INF_ShowSavedCells(array<string> keys)
	{
		if (!Replication.IsServer() || !m_bINFShowZoneAreas || !m_INFZoneAreaResource)
			return;
		if (keys != m_aINFSavedCellKeys)
			m_aINFSavedCellKeys.Copy(keys);
		foreach (string key : keys)
		{
			if (m_INFZoneAreas.Contains(key))
				continue;
			array<string> parts = {};
			key.Split(":", parts, false);
			vector center = Vector(parts[0].ToInt() * INF_BuildingGrid.SIZE + INF_BuildingGrid.HALF, 0,
				parts[1].ToInt() * INF_BuildingGrid.SIZE + INF_BuildingGrid.HALF);
			center[1] = GetGame().GetWorld().GetSurfaceY(center[0], center[2]);
			INF_GarrisonMapArea area = INF_CreateZoneArea(center);
			if (!area)
			{
				INF_QueueStaticAreaRetry();
				return;
			}
			m_INFZoneAreas.Set(key, area);
			area.INF_SetZoneState(center, INF_BuildingGrid.HALF, INF_EGarrisonMapState.CLEARED, true);
		}
	}

	protected void INF_UpdateZoneAreas()
	{
		m_bINFAreaUpdateQueued = false;
		if (!Replication.IsServer() || !m_INFZoneAreaResource)
			return;
		int processed;
		for (int i = m_INFDirtyAreas.Count() - 1; i >= 0 && processed < 64; i--)
		{
			string key = m_INFDirtyAreas.GetKey(i);
			INS_AmbientPatrolZoneState state = m_INFDirtyAreas.GetElement(i);
			INF_GarrisonMapArea area = m_INFZoneAreas.Get(key);
			if (!area)
			{
				area = INF_CreateZoneArea(state.m_vCenter);
				if (!area)
					break;
				m_INFZoneAreas.Set(key, area);
			}
			INF_EGarrisonMapState appearance = INF_EGarrisonMapState.DORMANT;
			if (state.m_bINFAttackPending || state.m_bINFAttackActive)
				appearance = INF_EGarrisonMapState.COUNTERATTACK;
			else if (state.m_bINFPermanent)
				appearance = INF_EGarrisonMapState.CLEARED;
			else if (state.m_fINFCaptureSince >= 0)
				appearance = INF_EGarrisonMapState.CAPTURING;
			else if (state.m_bINFInboundWaiting)
				appearance = INF_EGarrisonMapState.INBOUND;
			else if (state.m_bActive)
				appearance = INF_EGarrisonMapState.ACTIVE;
			area.INF_SetZoneState(state.m_vCenter, INF_BuildingGrid.HALF, appearance, true, state.m_iINFTargetAI);
			m_INFDirtyAreas.Remove(key);
			processed++;
		}
		if (!m_INFDirtyAreas.IsEmpty())
		{
			int delay = 50;
			if (processed < 64)
				delay = 5000;
			m_bINFAreaUpdateQueued = true;
			GetGame().GetCallqueue().CallLater(INF_UpdateZoneAreas, delay, false);
		}
	}

	protected void INF_ClearZoneAreas()
	{
		GetGame().GetCallqueue().Remove(INF_UpdateZoneAreas);
		GetGame().GetCallqueue().Remove(INF_RetryStaticAreas);
		if (m_INFObservedZones)
			m_INFObservedZones.GetOnChanged().Remove(INF_QueueZoneArea);
		m_INFObservedZones = null;
		m_INFDirtyAreas.Clear();
		m_bINFAreaUpdateQueued = false;
		foreach (string key, INF_GarrisonMapArea area : m_INFZoneAreas)
		{
			if (area)
				SCR_EntityHelper.DeleteEntityAndChildren(area);
		}

		m_INFZoneAreas.Clear();
	}

	override void OnGameModeEnd(SCR_GameModeEndData data)
	{
		if (!INF_UsesBlockControl())
		{
			super.OnGameModeEnd(data);
			return;
		}
		INF_ActivationPolicy.Reset();
		INF_ClearZoneAreas();
	}

	void ~INS_GarrisonManager()
	{
		if (GetGame())
			INF_ClearZoneAreas();
	}

	protected bool INF_UsesBlockControl()
	{
		INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(GetOwner().FindComponent(INS_AmbientPatrolManager));
		return patrols && patrols.INF_UsesBuildingGrid();
	}

	override void SetEnabled(bool bEnabled)
	{
		if (!INF_UsesBlockControl())
		{
			super.SetEnabled(bEnabled);
			return;
		}
		m_bEnabled = false;
		if (bEnabled && Replication.IsServer())
			Print("[INF_BuildingGrid] Garrison activation is retired; use building-grid settings.", LogLevel.WARNING);
	}
}
