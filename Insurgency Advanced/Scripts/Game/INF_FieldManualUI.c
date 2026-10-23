modded class SCR_FieldManualUI
{
	override protected void CreateCategoryMenuWidgets()
	{
		if (m_ConfigRoot && m_ConfigRoot.m_aCategories)
		{
			for (int i = 0; i < m_ConfigRoot.m_aCategories.Count(); i++)
			{
				SCR_FieldManualConfigCategory category = m_ConfigRoot.m_aCategories[i];
				if (!category || category.m_sTitle != "Insurgency Advanced")
					continue;
				// Inherited config arrays do not reliably follow source declaration order.
				if (i > 0)
				{
					m_ConfigRoot.m_aCategories.RemoveOrdered(i);
					m_ConfigRoot.m_aCategories.InsertAt(category, 0);
				}
				break;
			}
		}
		super.CreateCategoryMenuWidgets();
	}
}
