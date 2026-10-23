class INF_GMMapMarkersUI : SCR_MapMarkersUI
{
	protected bool m_bINFMarkerLayerOpen;
	protected Widget m_INFMarkerFrame;

	protected bool INF_EnsureMarkerFrame(MapConfiguration config)
	{
		Widget root;
		if (config)
			root = config.RootWidgetRef;
		if (!root || !m_MarkerMgr)
		{
			Print("[INF_GMMarkers] Cannot open marker layer: GM map root or marker manager unavailable.", LogLevel.ERROR);
			return false;
		}
		if (root.FindAnyWidget(SCR_MapConstants.MAP_FRAME_NAME))
			return true;

		// GM uses MapFrame as its root; stock marker renderers search only its children.
		Widget frame = GetGame().GetWorkspace().CreateWidget(WidgetType.FrameWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR, Color.FromRGBA(255, 255, 255, 255), -1, root);
		if (!frame)
		{
			Print("[INF_GMMarkers] Cannot create the GM marker frame; marker updates remain disabled.", LogLevel.ERROR);
			return false;
		}
		frame.SetName(SCR_MapConstants.MAP_FRAME_NAME);
		FrameSlot.SetAnchorMin(frame, 0, 0);
		FrameSlot.SetAnchorMax(frame, 1, 1);
		FrameSlot.SetOffsets(frame, 0, 0, 0, 0);
		m_INFMarkerFrame = frame;
		return true;
	}

	override void OnMapOpen(MapConfiguration config)
	{
		m_bINFMarkerLayerOpen = false;
		if (!INF_EnsureMarkerFrame(config))
		{
			if (m_MarkerMgr)
				m_MarkerMgr.EnableUpdate(false);
			return;
		}
		super.OnMapOpen(config);
		m_bINFMarkerLayerOpen = true;

		// Render the shared marker layers without taking over GM selection/menu inputs.
		InputManager input = GetGame().GetInputManager();
		input.RemoveActionListener("MapQuickMarkerMenu", EActionTrigger.DOWN, OnInputQuickMarkerMenu);
		input.RemoveActionListener("MapMarkerDelete", EActionTrigger.DOWN, OnInputMarkerDelete);
		input.RemoveActionListener("MapSelect", EActionTrigger.DOWN, OnInputMapSelect);
		input.RemoveActionListener(UIConstants.MENU_ACTION_SELECT, EActionTrigger.DOWN, OnInputMenuConfirm);
		input.RemoveActionListener("MenuRefresh", EActionTrigger.DOWN, OnInputMenuConfirmAlter);
		input.RemoveActionListener(UIConstants.MENU_ACTION_BACK, EActionTrigger.DOWN, OnInputMenuBack);
		input.RemoveActionListener(UIConstants.MENU_ACTION_DOWN, EActionTrigger.DOWN, OnInputMenuDown);
		input.RemoveActionListener(UIConstants.MENU_ACTION_UP, EActionTrigger.DOWN, OnInputMenuUp);
		input.RemoveActionListener(UIConstants.MENU_ACTION_RIGHT, EActionTrigger.DOWN, OnInputMenuRight);
		input.RemoveActionListener(UIConstants.MENU_ACTION_LEFT, EActionTrigger.DOWN, OnInputMenuLeft);
	}

	override void OnMapClose(MapConfiguration config)
	{
		if (!m_bINFMarkerLayerOpen)
			return;
		m_bINFMarkerLayerOpen = false;
		super.OnMapClose(config);
		if (m_INFMarkerFrame)
		{
			Widget child = m_INFMarkerFrame.GetChildren();
			while (child)
			{
				child.RemoveFromHierarchy();
				child = m_INFMarkerFrame.GetChildren();
			}
		}
	}
}
