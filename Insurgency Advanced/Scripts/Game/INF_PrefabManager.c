[ComponentEditorProps(category: "Insurgency Advanced", description: "The one place that defines the mission's vehicles, crews, infantry groups, officer and site compositions.")]
class INF_PrefabManagerComponentClass : ScriptComponentClass
{
}

// Set on the game mode prefab. A list left empty here keeps the value the base mod's own managers already hold;
// the composition and officer values have no other source. Map data such as the defence slot cache is not
// defined here: it belongs to the mission, not the faction.
class INF_PrefabManagerComponent : ScriptComponent
{
	[Attribute("", UIWidgets.ResourceNamePicker, "Light vehicles for ambient routes and zones (written to INS_AmbientVehicleManager m_aVehiclePrefabs).", "et", category: "Vehicles")]
	protected ref array<ResourceName> m_aLightVehicles;
	[Attribute("", UIWidgets.ResourceNamePicker, "Heavy vehicles (APCs, IFVs, armour). Merged with the light vehicles, and crewed from the heavy crew list.", "et", category: "Vehicles")]
	protected ref array<ResourceName> m_aHeavyVehicles;
	[Attribute("", UIWidgets.ResourceNamePicker, "Character prefabs for the crew seats of light vehicles. Empty keeps the vehicle's native crew.", "et", category: "Crews")]
	protected ref array<ResourceName> m_aLightCrew;
	[Attribute("", UIWidgets.ResourceNamePicker, "Character prefabs for the crew seats of heavy vehicles (armoured crewmen). Empty keeps the vehicle's native crew.", "et", category: "Crews")]
	protected ref array<ResourceName> m_aHeavyCrew;
	[Attribute("", UIWidgets.ResourceNamePicker, "Infantry character prefabs for passengers on every vehicle. Empty keeps the vehicle's default occupants.", "et", category: "Crews")]
	protected ref array<ResourceName> m_aPassengers;

	[Attribute("", UIWidgets.ResourceNamePicker, "Zone defender groups (INS_AmbientPatrolManager m_aGroupPrefabs).", "et", category: "Infantry Groups")]
	protected ref array<ResourceName> m_aDefenderGroups;
	[Attribute("", UIWidgets.ResourceNamePicker, "Counterattack squads (INS_CounterAttackManager m_aSquadPrefabs).", "et", category: "Infantry Groups")]
	protected ref array<ResourceName> m_aCounterAttackSquads;
	[Attribute("", UIWidgets.ResourceNamePicker, "Field threat groups (INS_FieldThreatManager m_aGroupPrefabs).", "et", category: "Infantry Groups")]
	protected ref array<ResourceName> m_aFieldThreatGroups;

	[Attribute("", UIWidgets.ResourceNamePicker, "Officer character. Empty disables officers.", "et", category: "Specials")]
	protected ResourceName m_sOfficer;
	[Attribute("", UIWidgets.ResourceNamePicker, "Camp composition.", "et", category: "Compositions")]
	protected ResourceName m_sCamp;
	[Attribute("", UIWidgets.ResourceNamePicker, "Mortar battery composition.", "et", category: "Compositions")]
	protected ResourceName m_sMortar;
	[Attribute("", UIWidgets.ResourceNamePicker, "Defence compositions (road and flat slot compositions, preferably the editable PrefabsEditable E_ variants).", "et", category: "Compositions")]
	protected ref array<ResourceName> m_aDefences;

	array<ResourceName> GetLightVehicles() { return m_aLightVehicles; }
	array<ResourceName> GetHeavyVehicles() { return m_aHeavyVehicles; }
	array<ResourceName> GetLightCrew() { return m_aLightCrew; }
	array<ResourceName> GetHeavyCrew() { return m_aHeavyCrew; }
	array<ResourceName> GetPassengers() { return m_aPassengers; }
	array<ResourceName> GetDefenderGroups() { return m_aDefenderGroups; }
	array<ResourceName> GetCounterAttackSquads() { return m_aCounterAttackSquads; }
	array<ResourceName> GetFieldThreatGroups() { return m_aFieldThreatGroups; }
	ResourceName GetOfficer() { return m_sOfficer; }
	ResourceName GetCamp() { return m_sCamp; }
	ResourceName GetMortar() { return m_sMortar; }
	array<ResourceName> GetDefences() { return m_aDefences; }

	// A copy of source when it has entries, otherwise fallback.
	static array<ResourceName> PreferList(array<ResourceName> source, array<ResourceName> fallback)
	{
		if (!source || source.IsEmpty())
			return fallback;
		array<ResourceName> copy = {};
		copy.Copy(source);
		return copy;
	}

	static ResourceName RandomPrefab(array<ResourceName> pool)
	{
		if (!pool || pool.IsEmpty())
			return "";
		return pool[Math.RandomInt(0, pool.Count())];
	}

	override void OnPostInit(IEntity owner)
	{
		SetEventMask(owner, EntityEvent.INIT);
	}

	// Pushes the definitions into the managers before the mission starts, so the base mod and our
	// existing readers see them without knowing about this component.
	override void EOnInit(IEntity owner)
	{
		INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(owner.FindComponent(INS_AmbientPatrolManager));
		if (patrols)
			patrols.INF_ApplyPrefabs(this);
		INS_CounterAttackManager attacks = INS_CounterAttackManager.Cast(owner.FindComponent(INS_CounterAttackManager));
		if (attacks)
			attacks.INF_ApplyPrefabs(this);
		INS_FieldThreatManager threats = INS_FieldThreatManager.Cast(owner.FindComponent(INS_FieldThreatManager));
		if (threats)
			threats.INF_ApplyPrefabs(this);
		INS_AmbientVehicleManager vehicles = INS_AmbientVehicleManager.Cast(owner.FindComponent(INS_AmbientVehicleManager));
		if (vehicles)
			vehicles.INF_ApplyPrefabs(this);
	}
}
