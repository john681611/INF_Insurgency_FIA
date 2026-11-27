modded class SCR_MapMarkerWidgetComponent
{
	protected bool m_bINFCacheHint;

	override void SetMarkerObject(notnull SCR_MapMarkerBase marker)
	{
		m_bINFCacheHint = false;
		super.SetMarkerObject(marker);

		INS_IntelManager intel = INS_IntelManager.GetInstance();
		int distance;
		if (!intel || !intel.INF_TryGetCacheMarkerDistance(marker, distance))
			return;

		m_bINFCacheHint = true;
		SetTextVisible(true);
	}

	override void SetTextVisible(bool state)
	{
		if (m_bINFCacheHint)
			state = true;
		super.SetTextVisible(state);
	}

	override void HandlerDeattached(Widget w)
	{
		m_bINFCacheHint = false;
		super.HandlerDeattached(w);
	}

}
