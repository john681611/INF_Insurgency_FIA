// A world's composition slots baked into a config, for servers whose world does not create the slot entities. The
// mission names it in its header (INS_MissionParams.m_sINFDefenceSlotCache) and defences fall back to it when the
// scan finds no SCR_SiteSlotEntity. Generated from the world's slot layer by the mission addon's
// Tools/generate_defence_slot_cache.py.
[BaseContainerProps()]
class INF_DefenceSlotEntry
{
	[Attribute("", UIWidgets.EditBox, "Slot type, the lowercase slot prefab name without the E_ prefix, e.g. slotroadmedium.")]
	string m_sType;
	[Attribute("0 0 0", UIWidgets.EditBox, "World position of the slot.")]
	vector m_vPosition;
	[Attribute("0", UIWidgets.EditBox, "World yaw of the slot in degrees.")]
	float m_fYaw;
}

[BaseContainerProps(configRoot: true)]
class INF_DefenceSlotCache
{
	[Attribute("", UIWidgets.Object, "Every slot in the world.")]
	ref array<ref INF_DefenceSlotEntry> m_aSlots;

	static INF_DefenceSlotCache Load(ResourceName path)
	{
		if (path.IsEmpty())
			return null;
		Resource resource = BaseContainerTools.LoadContainer(path);
		if (!resource || !resource.IsValid())
			return null;
		BaseContainer container = resource.GetResource().ToBaseContainer();
		if (!container)
			return null;
		return INF_DefenceSlotCache.Cast(BaseContainerTools.CreateInstanceFromContainer(container));
	}
}
