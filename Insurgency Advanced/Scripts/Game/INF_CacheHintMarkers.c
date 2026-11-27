modded class SCR_MapMarkerWidgetComponent
{
	protected bool m_bINFCacheHint;

	override void SetMarkerObject(notnull SCR_MapMarkerBase marker)
	{
		m_bINFCacheHint = false;
		super.SetMarkerObject(marker);
		string text = marker.GetCustomText();
		m_bINFCacheHint = text.IndexOf("~") == 0 && text.IndexOf("m") > 1;
		if (m_bINFCacheHint)
			SetTextVisible(true);
	}

	override void SetTextVisible(bool state)
	{
		if (m_bINFCacheHint)
			state = true;
		super.SetTextVisible(state);
	}

	override bool OnMouseLeave(Widget w, Widget enterW, int x, int y)
	{
		bool result = super.OnMouseLeave(w, enterW, x, y);
		if (m_bINFCacheHint && m_wMarkerText)
			m_wMarkerText.SetVisible(true);
		return result;
	}

	override bool OnFocusLost(Widget w, int x, int y)
	{
		bool result = super.OnFocusLost(w, x, y);
		if (m_bINFCacheHint && m_wMarkerText)
			m_wMarkerText.SetVisible(true);
		return result;
	}
}
