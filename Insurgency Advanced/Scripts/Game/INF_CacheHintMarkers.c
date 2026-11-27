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
		SetColor(INF_CacheHintColor(distance));
		SetText(marker.GetCustomText(), true);
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

	protected static Color INF_CacheHintColor(int distance)
	{
		if (distance < 250)
			return Color.FromRGBA(225, 55, 55, 255);
		if (distance < 500)
			return Color.FromRGBA(235, 135, 45, 255);
		if (distance < 1000)
			return Color.FromRGBA(225, 205, 70, 255);
		return Color.FromRGBA(65, 145, 225, 255);
	}
}
