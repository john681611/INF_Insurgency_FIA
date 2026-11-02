// Persistent defences placed into the world's own composition slots (SCR_SiteSlotEntity): barricades and checkpoints in
// road slots, and sandbag positions, machine-gun nests, bunkers and towers in small flat slots. Placement is recorded in a
// saved roster and respawned from it. Each defence's guards belong to the nearest zone, so they activate, count towards
// the shared AI budget, respawn and block capture like that zone's other defenders.
class INF_Defence
{
	string m_sKey;
	ResourceName m_Composition;
	vector m_vPosition;
	float m_fYaw;
	string m_sOwnerKey;
	bool m_bFlat;
	int m_iActiveSize;
	int m_iRememberedAI = -1;
	float m_fRememberedAt = -1;
}

class INF_DefenceConfig
{
	int m_iPercent = 25;
	float m_fSpacing = 800;
	float m_fMaxOwnerDistance = 1000;
	int m_iPerZone = 2;
	ref array<ResourceName> m_aComposition = {};
}

class INF_Defences
{
	static const string SAVE_FILE = "$profile:INS_Saves/defence_sites.dat";
	static const int SAVE_VERSION = 1;
	static const int MAX_SITES = 256;
	static const int TILE_SIZE = 400;
	static const int SCAN_BUDGET_MS = 10;
	static const float MIN_FROM_STATIC_SITE = 250;
	static const float GUARD_RADIUS = 15;
	static const float DEFEND_RADIUS = 25;
	static const int FLAT_MIN_GUARDS = 2;
	static const int FLAT_MAX_GUARDS = 4;
	// Minimum distance between small flat defences, so they don't cluster.
	static const float FLAT_SPACING = 100;
	// Small flat slots hold the sandbag, nest and bunker defences (the larger flat slots hold base content such as helipads).
	static const string FLAT_SLOT = "slotflatsmall";
	static const ResourceName WAYPOINT_DEFEND = "{93291E72AC23930F}Prefabs/AI/Waypoints/AIWaypoint_Defend.et";

	protected INF_ZoneStateSystem m_Zones;
	protected ref INF_DefenceConfig m_Config;
	protected ref array<ref INF_Defence> m_Sites = {};
	protected ref array<ResourceName> m_Compositions = {};
	protected ref array<string> m_CompositionTypes = {};
	protected ref array<string> m_SlotTypes = {};
	protected ref array<SCR_SiteSlotEntity> m_Slots = {};
	protected ref array<string> m_SeenSlotNames = {};
	protected ref array<int> m_SeenSlotCounts = {};
	protected ref array<vector> m_Tiles = {};
	protected ref array<vector> m_Avoid = {};
	protected vector m_vMin;
	protected vector m_vMax;
	protected int m_iOwnerRejects;
	protected int m_iSpacingRejects;
	protected int m_iFlatSeen;
	protected int m_iFlatOccupied;
	protected bool m_bCollecting;
	protected bool m_bScanned;
	protected bool m_bResolved;
	protected bool m_bSelected;
	protected bool m_bSlotsFound;
	protected bool m_bSphereRunning;
	protected string m_sSphereReason;
	protected int m_iSphereCells;
	protected int m_iSphereNextReport;
	protected int m_iSphereStarted;
	protected ref array<vector> m_SphereQueue = {};
	protected ref set<IEntity> m_SphereSeen = new set<IEntity>();
	static const int SPHERE_STEP = 400;
	static const float SPHERE_RADIUS = 300;
	static const int PLAYER_WATCH_MS = 5000;
	static void Log(string message)
	{
		Print("[INF_Defences] " + message);
	}

	static void Fail(string message)
	{
		Print("[INF_Defences] " + message, LogLevel.WARNING);
	}

	// Called before the terrain scan so the scan can hand over slots it meets anyway, avoiding a second full-map pass.
	void Prepare(INF_DefenceConfig config)
	{
		m_Config = config;
		if (!Replication.IsServer() || config.m_iPercent <= 0)
			return;
		if (FileIO.FileExists(INS_CACHE_SAVE_FILE) && FileIO.FileExists(SAVE_FILE))
			return;
		ResolveCompositions();
		m_bCollecting = !m_SlotTypes.IsEmpty();
	}

	void OfferSlot(IEntity entity)
	{
		if (m_bCollecting)
			CollectSlot(entity);
	}

	void SlotsScanned()
	{
		if (m_bCollecting)
			m_bScanned = true;
	}

	void Stop()
	{
		GetGame().GetCallqueue().Remove(ScanTick);
		GetGame().GetCallqueue().Remove(SphereTick);
		GetGame().GetCallqueue().Remove(WatchForPlayer);
		GetGame().GetCallqueue().Remove(ProbeKnownSlot);
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

	void Begin(INF_ZoneStateSystem zones, INF_DefenceConfig config, array<vector> avoid)
	{
		m_Zones = zones;
		m_Config = config;
		if (!Replication.IsServer() || !zones)
			return;
		m_Avoid.Copy(avoid);
		if (FileIO.FileExists(INS_CACHE_SAVE_FILE) && FileIO.FileExists(SAVE_FILE))
		{
			Recover();
			return;
		}
		if (config.m_iPercent <= 0)
		{
			Log("Disabled: defence percentage is 0.");
			return;
		}
		ResolveCompositions();
		if (m_SlotTypes.IsEmpty())
			return;
		// Independent of the terrain and tile scans: if they find no slots, sphere scans retry after init and again
		// once the first player has spawned.
		GetGame().GetCallqueue().CallLater(WatchForPlayer, PLAYER_WATCH_MS, true);
		Log("Watching for the first spawned player to retry the slot search if the startup searches find none.");
		// The terrain scan already collected the slots; only a reused map index needs the tile scan below.
		if (m_bScanned)
		{
			Select();
			m_Slots.Clear();
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
		GetGame().GetCallqueue().CallLater(ScanTick, 0, true);
	}

	protected void ResolveCompositions()
	{
		if (m_bResolved)
			return;
		m_bResolved = true;
		foreach (ResourceName candidate : m_Config.m_aComposition)
		{
			Resource resource = Resource.Load(candidate);
			string slotType = SlotType(candidate);
			if (!resource || !resource.IsValid() || slotType.IsEmpty())
			{
				Fail("Ignoring defence composition that cannot load or is not under a Slotted/<slot type> folder: " + candidate);
				continue;
			}
			m_Compositions.Insert(candidate);
			m_CompositionTypes.Insert(slotType);
			if (m_SlotTypes.Find(slotType) < 0)
				m_SlotTypes.Insert(slotType);
		}
		if (m_Compositions.IsEmpty())
			Fail("No usable defence compositions; set Defences: Compositions on the game mode. Defences are disabled.");
	}

	protected void ScanTick()
	{
		int started = System.GetTickCount();
		while (!m_Tiles.IsEmpty() && System.GetTickCount() - started < SCAN_BUDGET_MS)
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
	}

	// Slots written as $grp instances in a layer carry no prefab data, so their prefab name is empty; the slot's own
	// editor mesh (e.g. SlotFlatSmall_01.xob) names the type instead.
	protected string SlotName(SCR_SiteSlotEntity slot)
	{
		string name = INF_BuildingGrid.PrefabNameLower(slot);
		if (!name.IsEmpty())
			return name;
		IEntity child = slot.GetChildren();
		if (!child || !child.GetVObject())
			return name;
		name = child.GetVObject().GetResourceName();
		name.ToLower();
		return name;
	}

	protected bool CollectSlot(IEntity entity)
	{
		SCR_SiteSlotEntity slot = SCR_SiteSlotEntity.Cast(entity);
		if (!slot)
			return true;
		string name = SlotName(slot);
		int seenIndex = m_SeenSlotNames.Find(name);
		if (seenIndex < 0 && m_SeenSlotNames.Count() < 16)
		{
			m_SeenSlotNames.Insert(name);
			m_SeenSlotCounts.Insert(1);
		}
		else if (seenIndex >= 0)
			m_SeenSlotCounts.Set(seenIndex, m_SeenSlotCounts[seenIndex] + 1);
		if (name.IndexOf(FLAT_SLOT) != -1)
		{
			m_iFlatSeen++;
			if (slot.IsOccupied())
				m_iFlatOccupied++;
		}
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

	// Fallback for servers where the spatial query cannot see slots (no bounds without their editor mesh); the Game
	// Master registry holds every editable slot regardless.
	protected void CollectEditableSlots()
	{
		SCR_EditableEntityCore core = SCR_EditableEntityCore.Cast(SCR_EditableEntityCore.GetInstance(SCR_EditableEntityCore));
		if (!core)
		{
			Log("Editable entity registry unavailable; cannot look for slots there.");
			return;
		}
		set<SCR_EditableEntityComponent> editables = new set<SCR_EditableEntityComponent>();
		core.GetAllEntities(editables, false, true);
		int total = editables.Count();
		array<string> classNames = {};
		array<int> classCounts = {};
		foreach (SCR_EditableEntityComponent editable : editables)
		{
			if (!editable)
				continue;
			IEntity owner = editable.GetOwner();
			CollectSlot(owner);
			string className = owner.ClassName();
			int classIndex = classNames.Find(className);
			if (classIndex >= 0)
				classCounts.Set(classIndex, classCounts[classIndex] + 1);
			else if (classNames.Count() < 12)
			{
				classNames.Insert(className);
				classCounts.Insert(1);
			}
		}
		string classes;
		foreach (int classIndex, string className : classNames)
			classes += string.Format(" %1 x%2", className, classCounts[classIndex]);
		Log(string.Format("Editable entity registry holds %1 entities; %2 slots found there. Classes:%3", total, m_Slots.Count(), classes));
	}

	// Diagnostic for a dedicated server that sees no slots: the first slot in Slots.layer is at this fixed spot, so the
	// entities found around it show whether the slot layer exists there at all, and whether it appears later.
	protected static const vector PROBE_POSITION = Vector(7563.3, 17.5, 8102.6);
	protected ref array<string> m_ProbeNames = {};
	protected ref array<int> m_ProbeCounts = {};

	protected bool ProbeEntity(IEntity entity)
	{
		string className = entity.ClassName();
		int index = m_ProbeNames.Find(className);
		if (index >= 0)
			m_ProbeCounts.Set(index, m_ProbeCounts[index] + 1);
		else if (m_ProbeNames.Count() < 12)
		{
			m_ProbeNames.Insert(className);
			m_ProbeCounts.Insert(1);
		}
		return true;
	}

	protected void ProbeKnownSlot(int attempt)
	{
		m_ProbeNames.Clear();
		m_ProbeCounts.Clear();
		GetGame().GetWorld().QueryEntitiesBySphere(PROBE_POSITION, 300, ProbeEntity);
		string found;
		foreach (int index, string name : m_ProbeNames)
			found += string.Format(" %1 x%2", name, m_ProbeCounts[index]);
		Log(string.Format("Probe %1 around the first slot position (300m), players online %2:%3", attempt, GetGame().GetPlayerManager().GetPlayerCount(), found));
		if (attempt < 3)
			GetGame().GetCallqueue().CallLater(ProbeKnownSlot, 60000, false, attempt + 1);
	}

	protected void Select()
	{
		m_bSelected = true;
		if (m_SeenSlotNames.IsEmpty())
			CollectEditableSlots();
		if (m_SeenSlotNames.IsEmpty())
			ProbeKnownSlot(1);
		string seen;
		foreach (int seenIndex, string seenName : m_SeenSlotNames)
			seen += string.Format(" %1 x%2", seenName, m_SeenSlotCounts[seenIndex]);
		Log(string.Format("Slots in the world by prefab:%1. Small flat slots: %2, already occupied: %3. Slot types wanted: %4.", seen, m_iFlatSeen, m_iFlatOccupied, m_SlotTypes.Count()));
		if (m_Slots.IsEmpty())
		{
			if (m_SeenSlotNames.IsEmpty())
				Fail("The world contains no composition slots (SCR_SiteSlotEntity); its layers must include the slot layer. No defences placed yet; retrying with a sphere scan.");
			else
				Fail("No free matching slots (road slots or small flat slots); no defences placed.");
			if (m_SeenSlotNames.IsEmpty())
				StartSphereScan("startup, after all initialisation");
			return;
		}
		m_bSlotsFound = true;
		PlaceDefences();
	}

	// Polls until the first player controls an entity, then runs one sphere scan if no slots were found by then.
	protected void WatchForPlayer()
	{
		if (!m_bSelected)
			return;
		array<int> playerIds = {};
		GetGame().GetPlayerManager().GetPlayers(playerIds);
		bool spawned;
		foreach (int playerId : playerIds)
		{
			if (GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId))
				spawned = true;
		}
		if (!spawned)
			return;
		GetGame().GetCallqueue().Remove(WatchForPlayer);
		if (m_bSlotsFound)
		{
			Log("First player spawned; slots were already found, no retry needed.");
			return;
		}
		Log(string.Format("First player spawned (%1 online); no slots found yet, retrying with a sphere scan.", playerIds.Count()));
		StartSphereScan("first player spawned");
	}

	// Independent of the terrain and tile scans: sphere queries on an overlapping grid cover the whole map.
	protected void StartSphereScan(string reason)
	{
		if (m_bSlotsFound || m_bSphereRunning)
		{
			Log(string.Format("Sphere scan (%1) skipped: slots found=%2, scan already running=%3.", reason, m_bSlotsFound, m_bSphereRunning));
			return;
		}
		m_bSphereRunning = true;
		m_sSphereReason = reason;
		m_SphereQueue.Clear();
		m_SphereSeen.Clear();
		m_Slots.Clear();
		m_SeenSlotNames.Clear();
		m_SeenSlotCounts.Clear();
		m_iFlatSeen = 0;
		m_iFlatOccupied = 0;
		vector minimum, maximum;
		GetGame().GetWorld().GetBoundBox(minimum, maximum);
		int firstX = Math.Floor(minimum[0] / SPHERE_STEP);
		int firstZ = Math.Floor(minimum[2] / SPHERE_STEP);
		int endX = Math.Ceil(maximum[0] / SPHERE_STEP);
		int endZ = Math.Ceil(maximum[2] / SPHERE_STEP);
		for (int cellX = firstX; cellX < endX; cellX++)
		{
			for (int cellZ = firstZ; cellZ < endZ; cellZ++)
				m_SphereQueue.Insert(Vector((cellX + 0.5) * SPHERE_STEP, 0, (cellZ + 0.5) * SPHERE_STEP));
		}
		m_iSphereCells = m_SphereQueue.Count();
		m_iSphereNextReport = 25;
		m_iSphereStarted = System.GetTickCount();
		Log(string.Format("Sphere scan (%1) started: %2 cells, radius %3m, world bounds %4 to %5, players online %6.",
			reason, m_iSphereCells, SPHERE_RADIUS, minimum.ToString(), maximum.ToString(), GetGame().GetPlayerManager().GetPlayerCount()));
		GetGame().GetCallqueue().CallLater(SphereTick, 0, true);
	}

	protected bool SphereEntity(IEntity entity)
	{
		if (!SCR_SiteSlotEntity.Cast(entity) || m_SphereSeen.Contains(entity))
			return true;
		m_SphereSeen.Insert(entity);
		return CollectSlot(entity);
	}

	protected void SphereTick()
	{
		int started = System.GetTickCount();
		while (!m_SphereQueue.IsEmpty() && System.GetTickCount() - started < SCAN_BUDGET_MS)
		{
			int last = m_SphereQueue.Count() - 1;
			vector centre = m_SphereQueue[last];
			m_SphereQueue.Remove(last);
			centre[1] = GetGame().GetWorld().GetSurfaceY(centre[0], centre[2]);
			GetGame().GetWorld().QueryEntitiesBySphere(centre, SPHERE_RADIUS, SphereEntity);
		}
		int done = m_iSphereCells - m_SphereQueue.Count();
		if (m_iSphereCells > 0 && done * 100 / m_iSphereCells >= m_iSphereNextReport && !m_SphereQueue.IsEmpty())
		{
			Log(string.Format("Sphere scan (%1): %2 of %3 cells searched, %4 slots found so far.", m_sSphereReason, done, m_iSphereCells, m_SphereSeen.Count()));
			m_iSphereNextReport += 25;
		}
		if (!m_SphereQueue.IsEmpty())
			return;
		GetGame().GetCallqueue().Remove(SphereTick);
		m_bSphereRunning = false;
		Log(string.Format("Sphere scan (%1) finished in %2 ms: %3 cells, %4 distinct slots, %5 free matching slots.",
			m_sSphereReason, System.GetTickCount() - m_iSphereStarted, m_iSphereCells, m_SphereSeen.Count(), m_Slots.Count()));
		if (m_Slots.IsEmpty())
		{
			if (m_SphereSeen.IsEmpty())
				Fail("Sphere scan (" + m_sSphereReason + ") found no composition slots; no defences placed from this attempt.");
			else
				Fail("Sphere scan (" + m_sSphereReason + ") found slots but none free and matching; no defences placed.");
			return;
		}
		m_bSlotsFound = true;
		GetGame().GetCallqueue().Remove(WatchForPlayer);
		Log("Sphere scan found slots; later retries are cancelled.");
		PlaceDefences();
		m_Slots.Clear();
	}

	protected void PlaceDefences()
	{
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
		// One percentage of every viable road and small flat slot together.
		int quota = Math.Ceil(candidates.Count() * m_Config.m_iPercent / 100.0);
		foreach (SCR_SiteSlotEntity candidate : candidates)
		{
			if (m_Sites.Count() >= quota || m_Sites.Count() >= MAX_SITES)
				break;
			TryPlace(candidate);
		}
		// An empty roster is not saved, so a later start with a fixed config or map can still place defences.
		if (!m_Sites.IsEmpty() && !Save())
			Print("[INF_Defences] Cannot save the defence roster; the next start will pick new slots.", LogLevel.ERROR);
		int flat;
		foreach (INF_Defence site : m_Sites)
		{
			if (site.m_bFlat)
				flat++;
		}
		Log(string.Format("Placed %1 defences (%2 on road slots, %3 on small flat slots) from %4 viable of %5 free slots, target %6 percent (%7 rejected for spacing, %8 with no zone in range).",
			m_Sites.Count(), m_Sites.Count() - flat, flat, candidates.Count(), m_Slots.Count(), m_Config.m_iPercent, m_iSpacingRejects, m_iOwnerRejects));
	}

	protected bool TryPlace(SCR_SiteSlotEntity slot)
	{
		string slotName = SlotName(slot);
		vector position = slot.GetOrigin();
		bool flat = slotName.IndexOf(FLAT_SLOT) != -1;
		if (!SpacingClear(position, flat))
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
		INF_Defence defence = PlaceInSlot(slot, composition);
		if (!defence)
			return false;
		defence.m_bFlat = flat;
		Attach(defence, owner);
		m_Sites.Insert(defence);
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

	// Spacing applies between defences of the same kind: road ones by the configured distance, small flat ones by FLAT_SPACING.
	protected bool SpacingClear(vector position, bool flat)
	{
		float spacing = m_Config.m_fSpacing;
		if (flat)
			spacing = FLAT_SPACING;
		foreach (INF_Defence existing : m_Sites)
		{
			if (existing.m_bFlat == flat && vector.DistanceSqXZ(existing.m_vPosition, position) < spacing * spacing)
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

	// The nearest zone that can still activate within the configured range; unless ignoring the cap, it must have room for another defence.
	protected INS_AmbientPatrolZoneState FindOwner(vector position, bool ignoreCap = false)
	{
		INS_AmbientPatrolZoneState best;
		float bestSq = m_Config.m_fMaxOwnerDistance * m_Config.m_fMaxOwnerDistance;
		foreach (INS_AmbientPatrolZoneState zone : m_Zones.GetZones())
		{
			if (!zone.m_bINFBuildingCell || zone.m_iGroupCount <= 0 || zone.m_bINFPermanent || zone.m_bINFMarkedCleared)
				continue;
			if (!ignoreCap && zone.m_aINFDefences.Count() >= m_Config.m_iPerZone)
				continue;
			float distanceSq = vector.DistanceSqXZ(zone.m_vCenter, position);
			if (distanceSq > bestSq)
				continue;
			bestSq = distanceSq;
			best = zone;
		}
		return best;
	}

	protected INF_Defence PlaceInSlot(SCR_SiteSlotEntity slot, ResourceName composition)
	{
		Resource resource = Resource.Load(composition);
		if (!resource || !resource.IsValid())
			return null;
		IEntity entity = slot.SpawnEntityInSlot(resource);
		if (!entity)
		{
			Fail("Slot refused the composition: " + composition);
			return null;
		}
		if (!INF_ForestCamps.PrepareComposition(entity))
		{
			slot.SetOccupant(null);
			SCR_EntityHelper.DeleteEntityAndChildren(entity);
			return null;
		}
		INF_Defence defence = new INF_Defence();
		defence.m_Composition = composition;
		defence.m_vPosition = entity.GetOrigin();
		vector angles = entity.GetYawPitchRoll();
		defence.m_fYaw = angles[0];
		defence.m_sKey = INF_ForestCampRewards.SiteID(defence.m_vPosition);
		return defence;
	}

	protected void Attach(INF_Defence defence, INS_AmbientPatrolZoneState owner)
	{
		defence.m_sOwnerKey = INF_BuildingGrid.Key(owner.m_vCenter);
		owner.m_aINFDefences.Insert(defence);
	}

	protected bool Save()
	{
		FileIO.MakeDirectory(INS_CACHE_SAVE_DIR);
		FileSerializer file = new FileSerializer();
		if (!file.Open(SAVE_FILE, FileMode.WRITE))
			return false;
		bool success = file.Write(SAVE_VERSION) && file.Write(m_Sites.Count());
		foreach (INF_Defence defence : m_Sites)
		{
			string composition = defence.m_Composition;
			if (!file.Write(defence.m_sKey) || !file.Write(composition) || !file.Write(defence.m_vPosition)
				|| !file.Write(defence.m_fYaw) || !file.Write(defence.m_sOwnerKey))
				success = false;
		}
		file.Close();
		return success;
	}

	// Fails closed: an unreadable roster leaves the file untouched and places no defences this session.
	protected void Recover()
	{
		FileSerializer file = new FileSerializer();
		if (!file.Open(SAVE_FILE, FileMode.READ))
		{
			Print("[INF_Defences] Cannot open the saved defence roster; no defences this session.", LogLevel.ERROR);
			return;
		}
		int version;
		int count;
		if (!file.Read(version) || version != SAVE_VERSION || !file.Read(count) || count < 0 || count > MAX_SITES)
		{
			file.Close();
			Print("[INF_Defences] Saved defence roster has an unknown version or count; preserving it, no defences this session.", LogLevel.ERROR);
			return;
		}
		array<ref INF_Defence> loaded = {};
		for (int entry = 0; entry < count; entry++)
		{
			INF_Defence defence = new INF_Defence();
			string composition;
			if (!file.Read(defence.m_sKey) || !file.Read(composition) || !file.Read(defence.m_vPosition)
				|| !file.Read(defence.m_fYaw) || !file.Read(defence.m_sOwnerKey))
			{
				file.Close();
				Print(string.Format("[INF_Defences] Cannot read defence roster entry %1; preserving it, no defences this session.", entry), LogLevel.ERROR);
				return;
			}
			defence.m_Composition = composition;
			if (defence.m_sKey.IsEmpty() || !ValidVector(defence.m_vPosition) || !(defence.m_fYaw >= -360 && defence.m_fYaw <= 360))
			{
				file.Close();
				Print(string.Format("[INF_Defences] Invalid defence roster entry %1; preserving it, no defences this session.", entry), LogLevel.ERROR);
				return;
			}
			loaded.Insert(defence);
		}
		file.Close();
		foreach (INF_Defence saved : loaded)
		{
			if (Restore(saved))
				m_Sites.Insert(saved);
		}
		Log(string.Format("Restored %1 of %2 saved defences.", m_Sites.Count(), loaded.Count()));
	}

	protected bool Restore(INF_Defence defence)
	{
		Resource resource = Resource.Load(defence.m_Composition);
		if (!resource || !resource.IsValid())
		{
			Fail("Saved defence composition cannot load, skipping: " + defence.m_Composition);
			return false;
		}
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		Math3D.AnglesToMatrix(Vector(defence.m_fYaw, 0, 0), params.Transform);
		params.Transform[3] = defence.m_vPosition;
		IEntity entity = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params);
		if (!entity)
		{
			Fail("Saved defence could not be respawned: " + defence.m_sKey);
			return false;
		}
		if (!INF_ForestCamps.PrepareComposition(entity))
		{
			SCR_EntityHelper.DeleteEntityAndChildren(entity);
			return false;
		}
		defence.m_bFlat = SlotType(defence.m_Composition) == FLAT_SLOT;
		// The slot is not re-resolved on restore, so tell the slot manager it is taken (GM placement would otherwise stack).
		SCR_CompositionSlotManagerComponent slotManager = SCR_CompositionSlotManagerComponent.GetInstance();
		if (slotManager)
			slotManager.SetOccupant(defence.m_vPosition, entity);
		INS_AmbientPatrolZoneState owner = m_Zones.GetZone(defence.m_sOwnerKey);
		if (!owner)
		{
			owner = FindOwner(defence.m_vPosition, true);
			if (owner)
				Fail("Saved owner zone of defence " + defence.m_sKey + " no longer exists; reassigned to the nearest zone.");
		}
		if (owner)
			Attach(defence, owner);
		else
			Fail("Defence " + defence.m_sKey + " has no zone in range and will stay unguarded.");
		return true;
	}

	// A defend waypoint over the composition: the group searches for turrets within its radius and takes defensive positions.
	static AIWaypoint SpawnDefendWaypoint(vector position)
	{
		Resource resource = Resource.Load(WAYPOINT_DEFEND);
		if (!resource || !resource.IsValid())
		{
			Fail("Defend waypoint prefab cannot load; defence guards will idle.");
			return null;
		}
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		Math3D.MatrixIdentity4(params.Transform);
		params.Transform[3] = position;
		AIWaypoint waypoint = AIWaypoint.Cast(GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params));
		if (!waypoint)
		{
			Fail("Defend waypoint failed to spawn; defence guards will idle.");
			return null;
		}
		waypoint.SetCompletionRadius(DEFEND_RADIUS);
		SCR_DefendWaypoint defend = SCR_DefendWaypoint.Cast(waypoint);
		SCR_DefendWaypointPreset preset;
		if (defend)
			preset = defend.GetCurrentDefendPreset();
		if (preset)
			preset.SetUseTurrets(true);
		return waypoint;
	}

	// Guard groups are appended to the zone's plan after its ordinary groups, so budget, reservation and
	// pending-group accounting all include them. A remembered survivor count carries over like a zone's own.
	// Road defences use the configured guard size; small flat ones get 2-4, rolled per activation.
	static void AppendGuards(INS_AmbientPatrolZoneState state, int guards, float now)
	{
		state.m_aINFActiveDefences.Clear();
		foreach (INF_Defence defence : state.m_aINFDefences)
		{
			int full = guards;
			if (defence.m_bFlat)
				full = Math.RandomInt(FLAT_MIN_GUARDS, FLAT_MAX_GUARDS + 1);
			int size = full;
			if (defence.m_iRememberedAI >= 0 && now - defence.m_fRememberedAt < INF_BuildingGrid.DEFENDER_MEMORY_MS)
				size = Math.Min(full, defence.m_iRememberedAI);
			if (size <= 0)
				continue;
			defence.m_iActiveSize = size;
			state.m_aINFPlan.Insert(size);
			state.m_aINFActiveDefences.Insert(defence);
		}
	}

	static bool HasRememberedGuards(INS_AmbientPatrolZoneState state, float now)
	{
		foreach (INF_Defence defence : state.m_aINFDefences)
		{
			if (defence.m_iRememberedAI > 0 && now - defence.m_fRememberedAt < INF_BuildingGrid.DEFENDER_MEMORY_MS)
				return true;
		}
		return false;
	}

	// Called once the activation is committed, so a refused activation keeps the memory.
	static void ConsumeMemory(INS_AmbientPatrolZoneState state)
	{
		foreach (INF_Defence defence : state.m_aINFActiveDefences)
			defence.m_iRememberedAI = -1;
	}

	static int OrdinaryGroups(INS_AmbientPatrolZoneState state)
	{
		return state.m_aINFPlan.Count() - state.m_aINFActiveDefences.Count();
	}

	static int OrdinaryPlannedAI(INS_AmbientPatrolZoneState state)
	{
		int total = 0;
		for (int index = 0; index < OrdinaryGroups(state); index++)
			total += state.m_aINFPlan[index];
		return total;
	}

	static INF_Defence PlannedGuard(INS_AmbientPatrolZoneState state, int planIndex)
	{
		int offset = planIndex - OrdinaryGroups(state);
		if (offset < 0 || offset >= state.m_aINFActiveDefences.Count())
			return null;
		return state.m_aINFActiveDefences[offset];
	}

	// A zone cannot be captured while any of its defence guards still live, wherever they stand.
	static bool GuardsAlive(INS_AmbientPatrolZoneState state, INF_BlockPresence presence)
	{
		foreach (INS_PatrolAgent agent : state.m_aAgents)
		{
			if (!agent.m_INFDefence || !agent.m_Group)
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
			if (!state.m_aAgents[index].m_INFDefence)
				continue;
			guards.InsertAt(state.m_aAgents[index], 0);
			state.m_aAgents.Remove(index);
		}
	}

	// A defence whose group never spawned (e.g. a player stood on it) keeps its previous memory.
	static void RememberGuards(INS_AmbientPatrolZoneState state, array<ref INS_PatrolAgent> guards, bool membersMayBePending, float now)
	{
		foreach (INF_Defence defence : state.m_aINFActiveDefences)
		{
			bool spawned = false;
			int living = 0;
			foreach (INS_PatrolAgent guard : guards)
			{
				if (guard.m_INFDefence != defence)
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
				living = Math.Max(living, defence.m_iActiveSize);
			defence.m_iRememberedAI = living;
			defence.m_fRememberedAt = now;
		}
	}
}
