[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class INF_ZoneVehicleSettingEditorAttribute : SCR_BaseValueListEditorAttribute
{
	[Attribute("0")]
	protected int m_iSetting;

	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item) || !INF_ZoneEditorContext.Grid(m_GameMode))
			return null;
		INS_AmbientVehicleManager vehicles = INS_AmbientVehicleManager.Cast(m_GameMode.FindComponent(INS_AmbientVehicleManager));
		if (!vehicles)
			return null;
		return SCR_BaseEditorAttributeVar.CreateFloat(vehicles.INF_GetZoneVehicleSetting(m_iSetting));
	}

	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var,
		SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (!var || !IsGameMode(item) || !INF_ZoneEditorContext.Grid(m_GameMode))
			return;
		INS_AmbientVehicleManager vehicles = INS_AmbientVehicleManager.Cast(m_GameMode.FindComponent(INS_AmbientVehicleManager));
		if (vehicles)
			vehicles.INF_SetZoneVehicleSetting(m_iSetting, (int)var.GetFloat());
	}

	override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
	{
		int maximum = 100;
		if (m_iSetting == 1)
			maximum = 32;
		outEntries.Insert(new SCR_BaseEditorAttributeEntrySlider(new INS_SliderValues(0, maximum, 1)));
		return outEntries.Count();
	}
}

class INF_ZoneEditorContext
{
	static INS_AmbientPatrolManager Grid(IEntity mode)
	{
		if (!mode)
			return null;
		INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(mode.FindComponent(INS_AmbientPatrolManager));
		if (patrols && patrols.INF_UsesBuildingGrid())
			return patrols;
		return null;
	}
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class INF_QuestioningChanceEditorAttribute : SCR_BaseValueListEditorAttribute
{
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item) || !INF_ZoneEditorContext.Grid(m_GameMode))
			return null;
		INS_IntelManager intel = INS_IntelManager.Cast(m_GameMode.FindComponent(INS_IntelManager));
		if (!intel)
			return null;
		return SCR_BaseEditorAttributeVar.CreateFloat(intel.INF_GetQuestioningChance() * 100);
	}

	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var,
		SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (!var || !IsGameMode(item) || !INF_ZoneEditorContext.Grid(m_GameMode))
			return;
		INS_IntelManager intel = INS_IntelManager.Cast(m_GameMode.FindComponent(INS_IntelManager));
		if (intel)
			intel.INF_SetQuestioningChance(var.GetFloat() / 100);
	}

	override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
	{
		outEntries.Insert(new SCR_BaseEditorAttributeEntrySlider(new INS_SliderValues(0, 100, 1)));
		return outEntries.Count();
	}
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class INF_TravelSettingEditorAttribute : SCR_BaseValueListEditorAttribute
{
	[Attribute("0")]
	protected int m_iSetting;

	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item))
			return null;
		INS_AmbientPatrolManager patrols = INF_ZoneEditorContext.Grid(m_GameMode);
		if (!patrols)
			return null;
		return SCR_BaseEditorAttributeVar.CreateFloat(patrols.INF_GetTravelSetting(m_iSetting));
	}

	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var,
		SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (!var || !IsGameMode(item))
			return;
		INS_AmbientPatrolManager patrols = INF_ZoneEditorContext.Grid(m_GameMode);
		if (patrols)
			patrols.INF_SetTravelSetting(m_iSetting, var.GetFloat());
	}

	override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
	{
		float minimum = 1;
		float maximum = 180;
		if (m_iSetting == 0)
			maximum = 200;
		if (m_iSetting == 1)
		{
			minimum = 0;
			maximum = 199;
		}
		outEntries.Insert(new SCR_BaseEditorAttributeEntrySlider(new INS_SliderValues(minimum, maximum, 1)));
		return outEntries.Count();
	}
}

modded class INS_MaxConcurrentCounterAttacksEditorAttribute
{
	override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
	{
		if (!INF_ZoneEditorContext.Grid(GetGame().GetGameMode()))
			return super.GetEntries(outEntries);
		outEntries.Insert(new SCR_BaseEditorAttributeEntrySlider(new INS_SliderValues(0, 2, 1)));
		return outEntries.Count();
	}
}

modded class INS_CounterAttackIntervalEditorAttribute
{
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item) || INF_ZoneEditorContext.Grid(m_GameMode))
			return null;
		return super.ReadVariable(item, manager);
	}
}

modded class INS_TownPatrolCountEditorAttribute
{
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item) || INF_ZoneEditorContext.Grid(m_GameMode))
			return null;
		return super.ReadVariable(item, manager);
	}
}

modded class INS_VillagePatrolCountEditorAttribute
{
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item) || INF_ZoneEditorContext.Grid(m_GameMode))
			return null;
		return super.ReadVariable(item, manager);
	}
}

modded class INS_SettlementPatrolCountEditorAttribute
{
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item) || INF_ZoneEditorContext.Grid(m_GameMode))
			return null;
		return super.ReadVariable(item, manager);
	}
}

modded class INS_PatrolRespawnDelayEditorAttribute
{
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item) || INF_ZoneEditorContext.Grid(m_GameMode))
			return null;
		return super.ReadVariable(item, manager);
	}
}

modded class INS_MinSquadsPerAttackEditorAttribute
{
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item) || INF_ZoneEditorContext.Grid(m_GameMode))
			return null;
		return super.ReadVariable(item, manager);
	}
}

modded class INS_MaxSquadsPerAttackEditorAttribute
{
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item) || INF_ZoneEditorContext.Grid(m_GameMode))
			return null;
		return super.ReadVariable(item, manager);
	}
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class INF_SiteStrengthEditorAttribute : SCR_BaseEditorAttribute
{
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item))
			return null;
		INS_AmbientPatrolManager patrols = INF_ZoneEditorContext.Grid(m_GameMode);
		if (!patrols)
			return null;
		return SCR_BaseEditorAttributeVar.CreateInt(patrols.INF_GetSiteStrengthMultiplier() - 1);
	}

	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var,
		SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (!var || !IsGameMode(item))
			return;
		INS_AmbientPatrolManager patrols = INF_ZoneEditorContext.Grid(m_GameMode);
		if (patrols)
			patrols.INF_SetSiteStrengthMultiplier(var.GetInt() + 1);
	}

	override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
	{
		outEntries.Insert(new SCR_BaseEditorAttributeEntryText("1x"));
		outEntries.Insert(new SCR_BaseEditorAttributeEntryText("2x"));
		outEntries.Insert(new SCR_BaseEditorAttributeEntryText("3x"));
		outEntries.Insert(new SCR_BaseEditorAttributeEntryText("4x"));
		return outEntries.Count();
	}
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class INF_CounterAttackChanceEditorAttribute : SCR_BaseValueListEditorAttribute
{
	override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
	{
		if (!IsGameMode(item) || !INF_ZoneEditorContext.Grid(m_GameMode))
			return null;
		INS_CounterAttackManager attacks = INS_CounterAttackManager.Cast(m_GameMode.FindComponent(INS_CounterAttackManager));
		if (!attacks)
			return null;
		return SCR_BaseEditorAttributeVar.CreateFloat(attacks.INF_GetChance());
	}

	override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var,
		SCR_AttributesManagerEditorComponent manager, int playerID)
	{
		if (!var || !IsGameMode(item))
			return;
		INS_CounterAttackManager attacks = INS_CounterAttackManager.Cast(m_GameMode.FindComponent(INS_CounterAttackManager));
		if (attacks && INF_ZoneEditorContext.Grid(m_GameMode))
			attacks.INF_SetChance((int)var.GetFloat());
	}

	override int GetEntries(notnull array<ref SCR_BaseEditorAttributeEntry> outEntries)
	{
		outEntries.Insert(new SCR_BaseEditorAttributeEntrySlider(new INS_SliderValues(0, 100, 1)));
		return outEntries.Count();
	}
}
