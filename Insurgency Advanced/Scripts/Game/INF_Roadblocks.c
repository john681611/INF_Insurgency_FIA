// Persistent static sites placed into the world's own composition slots (SCR_SiteSlotEntity): roadblocks in road slots and
// defences (sandbags, machine-gun nests, bunkers) in small flat slots the world labels as fortifications. Placement is
// recorded in a saved roster and respawned from it. Each site's guards belong to the nearest zone, so they activate,
// count towards the shared AI budget, respawn and block capture like that zone's other defenders.
class INF_Roadblock
{
	string m_sKey;
	ResourceName m_Composition;
	vector m_vPosition;
	float m_fYaw;
	string m_sOwnerKey;
	bool m_bDefence;
	int m_iActiveSize;
	int m_iRememberedAI = -1;
	float m_fRememberedAt = -1;
}

class INF_RoadblockConfig
{
	int m_iPercent = 25;
	float m_fSpacing = 800;
	float m_fMaxOwnerDistance = 1000;
	int m_iPerZone = 2;
	ref array<ResourceName> m_aComposition = {};
}

class INF_Roadblocks
{
	static const string SAVE_FILE = "$profile:INS_Saves/roadblock_sites.dat";
	static const int SAVE_VERSION = 1;
	static const int MAX_SITES = 256;
	static const int TILE_SIZE = 400;
	static const int TILES_PER_TICK = 8;
	static const float MIN_FROM_STATIC_SITE = 250;
	static const float GUARD_RADIUS = 15;
	static const float DEFEND_RADIUS = 25;
	static const int DEFENCE_MIN_GUARDS = 2;
	static const int DEFENCE_MAX_GUARDS = 4;
	// Defences go in small flat slots (the larger flat slots hold base content such as helipads and hospitals).
	static const string DEFENCE_SLOT = "slotflatsmall";
	static const ResourceName WAYPOINT_DEFEND = "{93291E72AC23930F}Prefabs/AI/Waypoints/AIWaypoint_Defend.et";

	protected INF_ZoneStateSystem m_Zones;
	protected ref INF_RoadblockConfig m_Config;
	protected ref array<ref INF_Roadblock> m_Sites = {};
	protected ref array<ResourceName> m_Compositions = {};
	protected ref array<string> m_CompositionTypes = {};
	protected ref array<string> m_SlotTypes = {};
	protected ref array<SCR_SiteSlotEntity> m_Slots = {};
	protected ref array<string> m_SeenSlotNames = {};
	protected ref array<vector> m_Tiles = {};
	protected ref array<vector> m_Avoid = {};
	protected vector m_vMin;
	protected vector m_vMax;
	protected int m_iOwnerRejects;
	protected int m_iSpacingRejects;
	protected bool m_bReady;

	static void Log(string message)
	{
		Print("[INF_Roadblocks] " + message);
	}

	static void Fail(string message)
	{
		Print("[INF_Roadblocks] " + message, LogLevel.WARNING);
	}

	bool IsReady()
	{
		return m_bReady;
	}

	void Stop()
	{
		GetGame().GetCallqueue().Remove(ScanTick);
	}

	// The slot type is the folder under Slotted/, e.g. slotroadmedium; world slots are matched on it.
	static string SlotType(ResourceName composition)
	{
		string path = composition;
		path.ToLower();
		int start = path.IndexOf("/slotted/");
		if (start < 0)
			return string.Empty;
		start += 9;
		int end = path.IndexOfFrom(start, "/");
		if (end < 0)
			return string.Empty;
		return path.Substring(start, end - start);
	}

	protected static bool ValidVector(vector value)
	{
		for (int axis = 0; axis < 3; axis++)
		{
			if (!(value[axis] >= -1000000 && value[axis] <= 1000000))
				return false;
		}
		return true;
	}

	void Begin(INF_ZoneStateSystem zones, INF_RoadblockConfig config, array<vector> avoid)
	{
		m_Zones = zones;
		m_Config = config;
		if (!Replication.IsServer() || !zones)
		{
			m_bReady = true;
			return;
		}
		m_Avoid.Copy(avoid);
		if (FileIO.FileExists(INS_CACHE_SAVE_FILE) && FileIO.FileExists(SAVE_FILE))
		{
			Recover();
			m_bReady = true;
			return;
		}
		if (config.m_iPercent <= 0)
		{
			Log("Disabled: roadblock percentage is 0.");
			m_bReady = true;
			return;
		}
		ResolveCompositions();
		if (m_SlotTypes.IsEmpty())
		{
			m_bReady = true;
			return;
		}
		GetGame().GetWorld().GetBoundBox(m_vMin, m_vMax);
		int firstX = Math.Floor(m_vMin[0] / TILE_SIZE);
		int firstZ = Math.Floor(m_vMin[2] / TILE_SIZE);
		int endX = Math.Ceil(m_vMax[0] / TILE_SIZE);
		int endZ = Math.Ceil(m_vMax[2] / TILE_SIZE);
		for (int tileX = firstX; tileX < endX; tileX++)
		{
			for (int tileZ = firstZ; tileZ < endZ; tileZ++)
				m_Tiles.Insert(Vector(tileX * TILE_SIZE, 0, tileZ * TILE_SIZE));
		}
		Log(string.Format("Searching %1 map tiles for %2 slot types.", m_Tiles.Count(), m_SlotTypes.Count()));
		GetGame().GetCallqueue().CallLater(ScanTick, 100, true);
	}

	protected void ResolveCompositions()
	{
		foreach (ResourceName candidate : m_Config.m_aComposition)
		{
			Resource resource = Resource.Load(candidate);
			string slotType = SlotType(candidate);
			if (!resource || !resource.IsValid() || slotType.IsEmpty())
			{
				Fail("Ignoring roadblock composition that cannot load or is not under a Slotted/<slot type> folder: " + candidate);
				continue;
			}
			m_Compositions.Insert(candidate);
			m_CompositionTypes.Insert(slotType);
			if (m_SlotTypes.Find(slotType) < 0)
				m_SlotTypes.Insert(slotType);
		}
		if (m_Compositions.IsEmpty())
			Fail("No usable roadblock compositions; set Roadblocks: Compositions on the game mode. Roadblocks and defences are disabled.");
	}

	protected void ScanTick()
	{
		for (int step = 0; step < TILES_PER_TICK && !m_Tiles.IsEmpty(); step++)
		{
			int last = m_Tiles.Count() - 1;
			vector tile = m_Tiles[last];
			m_Tiles.Remove(last);
			vector minimum = Vector(tile[0], m_vMin[1], tile[2]);
			vector maximum = Vector(tile[0] + TILE_SIZE, m_vMax[1], tile[2] + TILE_SIZE);
			GetGame().GetWorld().QueryEntitiesByAABB(minimum, maximum, CollectSlot);
		}
		if (!m_Tiles.IsEmpty())
			return;
		GetGame().GetCallqueue().Remove(ScanTick);
		Select();
		m_Slots.Clear();
		m_bReady = true;
	}

	protected bool CollectSlot(IEntity entity)
	{
		SCR_SiteSlotEntity slot = SCR_SiteSlotEntity.Cast(entity);
		if (!slot)
			return true;
		string name = INF_BuildingGrid.PrefabNameLower(slot);
		if (m_SeenSlotNames.Count() < 12 && m_SeenSlotNames.Find(name) < 0)
			m_SeenSlotNames.Insert(name);
		foreach (string wanted : m_SlotTypes)
		{
			if (name.IndexOf(wanted) == -1)
				continue;
			if (!slot.IsOccupied() && m_Slots.Find(slot) < 0)
				m_Slots.Insert(slot);
			break;
		}
		return true;
	}

	protected void Select()
	{
		if (m_Slots.IsEmpty())
		{
			string seen;
			foreach (string seenName : m_SeenSlotNames)
				seen += " " + seenName;
			if (seen.IsEmpty())
				Fail("The world contains no composition slots (SCR_SiteSlotEntity); its layers must include the slot layer. No sites placed.");
			else
				Fail("No free matching slots (road slots or small flat slots); no sites placed. Slot prefab types seen:" + seen);
			return;
		}
		array<SCR_SiteSlotEntity> candidates = {};
		foreach (SCR_SiteSlotEntity slot : m_Slots)
		{
			if (Viable(slot.GetOrigin()))
				candidates.Insert(slot);
		}
		for (int index = candidates.Count() - 1; index > 0; index--)
		{
			int swap = Math.RandomInt(0, index + 1);
			SCR_SiteSlotEntity held = candidates[index];
			candidates.Set(index, candidates[swap]);
			candidates.Set(swap, held);
		}
		// One percentage of every viable road and defence slot together.
		int quota = Math.Ceil(candidates.Count() * m_Config.m_iPercent / 100.0);
		foreach (SCR_SiteSlotEntity candidate : candidates)
		{
			if (m_Sites.Count() >= quota || m_Sites.Count() >= MAX_SITES)
				break;
			TryPlace(candidate);
		}
		// An empty roster is not saved, so a later start with a fixed config or map can still place sites.
		if (!m_Sites.IsEmpty() && !Save())
			Print("[INF_Roadblocks] Cannot save the roadblock roster; the next start will pick new slots.", LogLevel.ERROR);
		int defences;
		foreach (INF_Roadblock site : m_Sites)
		{
			if (site.m_bDefence)
				defences++;
		}
		Log(string.Format("Placed %1 roadblocks and %2 defences from %3 viable of %4 free slots, target %5 percent (%6 rejected for spacing, %7 with no zone in range).",
			m_Sites.Count() - defences, defences, candidates.Count(), m_Slots.Count(), m_Config.m_iPercent, m_iSpacingRejects, m_iOwnerRejects));
	}

	protected bool TryPlace(SCR_SiteSlotEntity slot)
	{
		string slotName = INF_BuildingGrid.PrefabNameLower(slot);
		vector position = slot.GetOrigin();
		bool defence = slotName.IndexOf(DEFENCE_SLOT) != -1;
		if (!defence && !SpacingClear(position))
			return false;
		ResourceName composition = PickComposition(slotName);
		if (composition.IsEmpty())
			return false;
		INS_AmbientPatrolZoneState owner = FindOwner(position);
		if (!owner)
		{
			m_iOwnerRejects++;
			return false;
		}
		INF_Roadblock block = PlaceInSlot(slot, composition);
		if (!block)
			return false;
		block.m_bDefence = defence;
		Attach(block, owner);
		m_Sites.Insert(block);
		return true;
	}

	protected bool Viable(vector position)
	{
		if (INF_BuildingGrid.IsSpawnBlocked(position))
			return false;
		foreach (vector avoid : m_Avoid)
		{
			if (vector.DistanceSqXZ(avoid, position) < MIN_FROM_STATIC_SITE * MIN_FROM_STATIC_SITE)
				return false;
		}
		return true;
	}

	// Spacing applies between roadblocks only; defences sit one to a location.
	protected bool SpacingClear(vector position)
	{
		float spacingSq = m_Config.m_fSpacing * m_Config.m_fSpacing;
		foreach (INF_Roadblock existing : m_Sites)
		{
			if (!existing.m_bDefence && vector.DistanceSqXZ(existing.m_vPosition, position) < spacingSq)
			{
				m_iSpacingRejects++;
				return false;
			}
		}
		return true;
	}

	protected ResourceName PickComposition(string slotName)
	{
		array<ResourceName> fitting = {};
		foreach (int index, ResourceName composition : m_Compositions)
		{
			if (slotName.IndexOf(m_CompositionTypes[index]) != -1)
				fitting.Insert(composition);
		}
		if (fitting.IsEmpty())
			return "";
		return fitting[Math.RandomInt(0, fitting.Count())];
	}

	// The nearest zone that can still activate within the configured range; unless ignoring the cap, it must have room for another roadblock.
	protected INS_AmbientPatrolZoneState FindOwner(vector position, bool ignoreCap = false)
	{
		INS_AmbientPatrolZoneState best;
		float bestSq = m_Config.m_fMaxOwnerDistance * m_Config.m_fMaxOwnerDistance;
		foreach (INS_AmbientPatrolZoneState zone : m_Zones.GetZones())
		{
			if (!zone.m_bINFBuildingCell || zone.m_iGroupCount <= 0 || zone.m_bINFPermanent || zone.m_bINFMarkedCleared)
				continue;
			if (!ignoreCap && zone.m_aINFRoadblocks.Count() >= m_Config.m_iPerZone)
				continue;
			float distanceSq = vector.DistanceSqXZ(zone.m_vCenter, position);
			if (distanceSq > bestSq)
				continue;
			bestSq = distanceSq;
			best = zone;
		}
		return best;
	}

	protected INF_Roadblock PlaceInSlot(SCR_SiteSlotEntity slot, ResourceName composition)
	{
		Resource resource = Resource.Load(composition);
		if (!resource || !resource.IsValid())
			return null;
		IEntity entity = slot.SpawnEntityInSlot(resource);
		if (!entity)
		{
			Fail("Road slot refused the composition: " + composition);
			return null;
		}
		if (!INF_ForestCamps.PrepareComposition(entity))
		{
			slot.SetOccupant(null);
			SCR_EntityHelper.DeleteEntityAndChildren(entity);
			return null;
		}
		INF_Roadblock block = new INF_Roadblock();
		block.m_Composition = composition;
		block.m_vPosition = entity.GetOrigin();
		vector angles = entity.GetYawPitchRoll();
		block.m_fYaw = angles[0];
		block.m_sKey = INF_ForestCampRewards.SiteID(block.m_vPosition);
		return block;
	}

	protected void Attach(INF_Roadblock block, INS_AmbientPatrolZoneState owner)
	{
		block.m_sOwnerKey = INF_BuildingGrid.Key(owner.m_vCenter);
		owner.m_aINFRoadblocks.Insert(block);
	}

	protected bool Save()
	{
		FileIO.MakeDirectory(INS_CACHE_SAVE_DIR);
		FileSerializer file = new FileSerializer();
		if (!file.Open(SAVE_FILE, FileMode.WRITE))
			return false;
		bool success = file.Write(SAVE_VERSION) && file.Write(m_Sites.Count());
		foreach (INF_Roadblock block : m_Sites)
		{
			string composition = block.m_Composition;
			if (!file.Write(block.m_sKey) || !file.Write(composition) || !file.Write(block.m_vPosition)
				|| !file.Write(block.m_fYaw) || !file.Write(block.m_sOwnerKey))
				success = false;
		}
		file.Close();
		return success;
	}

	// Fails closed: an unreadable roster leaves the file untouched and places no roadblocks this session.
	protected void Recover()
	{
		FileSerializer file = new FileSerializer();
		if (!file.Open(SAVE_FILE, FileMode.READ))
		{
			Print("[INF_Roadblocks] Cannot open the saved roadblock roster; no roadblocks this session.", LogLevel.ERROR);
			return;
		}
		int version;
		int count;
		if (!file.Read(version) || version != SAVE_VERSION || !file.Read(count) || count < 0 || count > MAX_SITES)
		{
			file.Close();
			Print("[INF_Roadblocks] Saved roadblock roster has an unknown version or count; preserving it, no roadblocks this session.", LogLevel.ERROR);
			return;
		}
		array<ref INF_Roadblock> loaded = {};
		for (int entry = 0; entry < count; entry++)
		{
			INF_Roadblock block = new INF_Roadblock();
			string composition;
			if (!file.Read(block.m_sKey) || !file.Read(composition) || !file.Read(block.m_vPosition)
				|| !file.Read(block.m_fYaw) || !file.Read(block.m_sOwnerKey))
			{
				file.Close();
				Print(string.Format("[INF_Roadblocks] Cannot read roadblock roster entry %1; preserving it, no roadblocks this session.", entry), LogLevel.ERROR);
				return;
			}
			block.m_Composition = composition;
			if (block.m_sKey.IsEmpty() || !ValidVector(block.m_vPosition) || !(block.m_fYaw >= -360 && block.m_fYaw <= 360))
			{
				file.Close();
				Print(string.Format("[INF_Roadblocks] Invalid roadblock roster entry %1; preserving it, no roadblocks this session.", entry), LogLevel.ERROR);
				return;
			}
			loaded.Insert(block);
		}
		file.Close();
		foreach (INF_Roadblock saved : loaded)
		{
			if (Restore(saved))
				m_Sites.Insert(saved);
		}
		Log(string.Format("Restored %1 of %2 saved roadblocks.", m_Sites.Count(), loaded.Count()));
	}

	protected bool Restore(INF_Roadblock block)
	{
		Resource resource = Resource.Load(block.m_Composition);
		if (!resource || !resource.IsValid())
		{
			Fail("Saved roadblock composition cannot load, skipping: " + block.m_Composition);
			return false;
		}
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		Math3D.AnglesToMatrix(Vector(block.m_fYaw, 0, 0), params.Transform);
		params.Transform[3] = block.m_vPosition;
		IEntity entity = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params);
		if (!entity)
		{
			Fail("Saved roadblock could not be respawned: " + block.m_sKey);
			return false;
		}
		if (!INF_ForestCamps.PrepareComposition(entity))
		{
			SCR_EntityHelper.DeleteEntityAndChildren(entity);
			return false;
		}
		block.m_bDefence = SlotType(block.m_Composition) == DEFENCE_SLOT;
		// The slot is not re-resolved on restore, so tell the slot manager it is taken (GM placement would otherwise stack).
		SCR_CompositionSlotManagerComponent slotManager = SCR_CompositionSlotManagerComponent.GetInstance();
		if (slotManager)
			slotManager.SetOccupant(block.m_vPosition, entity);
		INS_AmbientPatrolZoneState owner = m_Zones.GetZone(block.m_sOwnerKey);
		if (!owner)
		{
			owner = FindOwner(block.m_vPosition, true);
			if (owner)
				Fail("Saved owner zone of roadblock " + block.m_sKey + " no longer exists; reassigned to the nearest zone.");
		}
		if (owner)
			Attach(block, owner);
		else
			Fail("Roadblock " + block.m_sKey + " has no zone in range and will stay unguarded.");
		return true;
	}

	// A defend waypoint over the composition: the group searches for turrets within its radius and takes defensive positions.
	static AIWaypoint SpawnDefendWaypoint(vector position)
	{
		Resource resource = Resource.Load(WAYPOINT_DEFEND);
		if (!resource || !resource.IsValid())
		{
			Fail("Defend waypoint prefab cannot load; roadblock guards will idle.");
			return null;
		}
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		Math3D.MatrixIdentity4(params.Transform);
		params.Transform[3] = position;
		AIWaypoint waypoint = AIWaypoint.Cast(GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params));
		if (!waypoint)
		{
			Fail("Defend waypoint failed to spawn; roadblock guards will idle.");
			return null;
		}
		waypoint.SetCompletionRadius(DEFEND_RADIUS);
		SCR_DefendWaypoint defend = SCR_DefendWaypoint.Cast(waypoint);
		SCR_DefendWaypointPreset preset;
		if (defend)
			preset = defend.GetCurrentDefendPreset();
		if (preset)
			preset.SetUseTurrets(true);
		Log(string.Format("Defend waypoint at %1: defend waypoint class %2, preset %3, use turrets %4.", position.ToString(), defend != null, preset != null, preset && preset.GetUseTurrets()));
		return waypoint;
	}

	// Guard groups are appended to the zone's plan after its ordinary groups, so budget, reservation and
	// pending-group accounting all include them. A remembered survivor count carries over like a zone's own.
	// Roadblock guards use the configured size; defences get 2-4, rolled per activation.
	static void AppendGuards(INS_AmbientPatrolZoneState state, int guards, float now)
	{
		state.m_aINFActiveRoadblocks.Clear();
		foreach (INF_Roadblock block : state.m_aINFRoadblocks)
		{
			int full = guards;
			if (block.m_bDefence)
				full = Math.RandomInt(DEFENCE_MIN_GUARDS, DEFENCE_MAX_GUARDS + 1);
			int size = full;
			if (block.m_iRememberedAI >= 0 && now - block.m_fRememberedAt < INF_BuildingGrid.DEFENDER_MEMORY_MS)
				size = Math.Min(full, block.m_iRememberedAI);
			if (size <= 0)
				continue;
			block.m_iActiveSize = size;
			state.m_aINFPlan.Insert(size);
			state.m_aINFActiveRoadblocks.Insert(block);
		}
	}

	static bool HasRememberedGuards(INS_AmbientPatrolZoneState state, float now)
	{
		foreach (INF_Roadblock block : state.m_aINFRoadblocks)
		{
			if (block.m_iRememberedAI > 0 && now - block.m_fRememberedAt < INF_BuildingGrid.DEFENDER_MEMORY_MS)
				return true;
		}
		return false;
	}

	// Called once the activation is committed, so a refused activation keeps the memory.
	static void ConsumeMemory(INS_AmbientPatrolZoneState state)
	{
		foreach (INF_Roadblock block : state.m_aINFActiveRoadblocks)
			block.m_iRememberedAI = -1;
	}

	static int OrdinaryGroups(INS_AmbientPatrolZoneState state)
	{
		return state.m_aINFPlan.Count() - state.m_aINFActiveRoadblocks.Count();
	}

	static int OrdinaryPlannedAI(INS_AmbientPatrolZoneState state)
	{
		int total = 0;
		for (int index = 0; index < OrdinaryGroups(state); index++)
			total += state.m_aINFPlan[index];
		return total;
	}

	static INF_Roadblock PlannedGuard(INS_AmbientPatrolZoneState state, int planIndex)
	{
		int offset = planIndex - OrdinaryGroups(state);
		if (offset < 0 || offset >= state.m_aINFActiveRoadblocks.Count())
			return null;
		return state.m_aINFActiveRoadblocks[offset];
	}

	// A zone cannot be captured while any of its roadblock guards still live, wherever they stand.
	static bool GuardsAlive(INS_AmbientPatrolZoneState state, INF_BlockPresence presence)
	{
		foreach (INS_PatrolAgent agent : state.m_aAgents)
		{
			if (!agent.m_INFRoadblock || !agent.m_Group)
				continue;
			array<vector> living = presence.GroupPositions(agent.m_Group);
			if (!living.IsEmpty())
				return true;
		}
		return false;
	}

	// Moves the guard agents out of the zone's agent list so the zone's own despawn and memory logic only sees ordinary groups.
	static void ExtractGuards(INS_AmbientPatrolZoneState state, array<ref INS_PatrolAgent> guards)
	{
		for (int index = state.m_aAgents.Count() - 1; index >= 0; index--)
		{
			if (!state.m_aAgents[index].m_INFRoadblock)
				continue;
			guards.InsertAt(state.m_aAgents[index], 0);
			state.m_aAgents.Remove(index);
		}
	}

	// A roadblock whose group never spawned (e.g. a player stood on it) keeps its previous memory.
	static void RememberGuards(INS_AmbientPatrolZoneState state, array<ref INS_PatrolAgent> guards, bool membersMayBePending, float now)
	{
		foreach (INF_Roadblock block : state.m_aINFActiveRoadblocks)
		{
			bool spawned = false;
			int living = 0;
			foreach (INS_PatrolAgent guard : guards)
			{
				if (guard.m_INFRoadblock != block)
					continue;
				spawned = true;
				if (!guard.m_Group)
					continue;
				array<vector> positions = {};
				INF_BlockRules.CollectLiving(guard.m_Group, positions);
				living += positions.Count();
			}
			if (!spawned)
				continue;
			if (membersMayBePending)
				living = Math.Max(living, block.m_iActiveSize);
			block.m_iRememberedAI = living;
			block.m_fRememberedAt = now;
		}
	}
}
