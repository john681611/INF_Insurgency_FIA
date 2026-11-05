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
	IEntity m_Entity;
	bool m_bDespawned;
	float m_fIdleSince = -1;
	bool m_bSeenNear;
	bool m_bQueued;
	int m_iSpawnFailures;
}

// A slot to place a defence in: a live slot entity, or a baked slot (no entity) when the world has none at runtime.
class INF_SlotCandidate
{
	string m_sName;
	vector m_vPosition;
	float m_fYaw;
	SCR_SiteSlotEntity m_Slot;
}

class INF_DefenceConfig
{
	int m_iPercent = 25;
	float m_fSpacing = 800;
	float m_fMaxOwnerDistance = 1000;
	int m_iPerZone = 2;
	ResourceName m_sSlotCache;
	ref array<ResourceName> m_aComposition = {};
}

class INF_Defences
{
	static const string SAVE_FILE = "$profile:INS_Saves/defence_sites.dat";
	static const int SAVE_VERSION = 1;
	static const int MAX_SITES = 256;
	static const int TILE_SIZE = 400;
	static const int SCAN_BUDGET_MS = 10;
	// Streaming: despawn beyond DESPAWN_DISTANCE (for the zone deactivation delay), respawn inside SPAWN_DISTANCE; the gap stops flapping.
	static const float DESPAWN_DISTANCE = 4000;
	static const float SPAWN_DISTANCE = 3000;
	static const float OWN_SPAWN_RADIUS = 30;
	static const int MAX_SPAWN_FAILURES = 3;
	static const float MIN_FROM_STATIC_SITE = 250;
	static const float GUARD_RADIUS = 15;
	static const float DEFEND_RADIUS = 25;
	static const int FLAT_MIN_GUARDS = 2;
	static const int FLAT_MAX_GUARDS = 4;
	// Roaming AI a zone keeps before its guards stop being drawn from it.
	static const int MIN_ROAMING = 6;
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
	protected float m_fResumedAt;
	protected bool m_bPaused;
	protected int m_iOwnerRejects;
	protected int m_iSpacingRejects;
	protected int m_iFlatSeen;
	protected int m_iFlatOccupied;
	protected bool m_bCollecting;
	protected bool m_bScanned;
	protected bool m_bResolved;

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
		GetGame().GetCallqueue().Remove(RespawnQueued);
		GetGame().GetCallqueue().Remove(DespawnQueued);
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

	// Baked list of the world's slots, for a server whose world does not create the slot entities.
	protected void UseBakedSlots()
	{
		if (m_Config.m_sSlotCache.IsEmpty())
		{
			Fail("No slot cache is configured (INS_AmbientPatrolManager m_sINFDefenceSlotCache), so there is no fallback; no defences placed.");
			return;
		}
		INF_DefenceSlotCache cache = INF_DefenceSlotCache.Load(m_Config.m_sSlotCache);
		if (!cache || !cache.m_aSlots || cache.m_aSlots.IsEmpty())
		{
			Fail("The slot cache cannot be loaded or is empty, no defences placed: " + m_Config.m_sSlotCache);
			return;
		}
		array<ref INF_SlotCandidate> candidates = {};
		foreach (INF_DefenceSlotEntry entry : cache.m_aSlots)
		{
			string type = entry.m_sType;
			if (m_SlotTypes.Find(type) < 0)
				continue;
			INF_SlotCandidate candidate = new INF_SlotCandidate();
			candidate.m_sName = type;
			candidate.m_vPosition = entry.m_vPosition;
			candidate.m_vPosition[1] = GetGame().GetWorld().GetSurfaceY(entry.m_vPosition[0], entry.m_vPosition[2]);
			candidate.m_fYaw = entry.m_fYaw;
			candidates.Insert(candidate);
		}
		Log(string.Format("No slot entities in the world; using the slot cache: %1 slots, %2 of the wanted types.", cache.m_aSlots.Count(), candidates.Count()));
		PlaceDefences(candidates);
	}

	protected void Select()
	{
		string seen;
		foreach (int seenIndex, string seenName : m_SeenSlotNames)
			seen += string.Format(" %1 x%2", seenName, m_SeenSlotCounts[seenIndex]);
		Log(string.Format("Slots in the world by prefab:%1. Small flat slots: %2, already occupied: %3. Slot types wanted: %4.", seen, m_iFlatSeen, m_iFlatOccupied, m_SlotTypes.Count()));
		if (m_Slots.IsEmpty())
		{
			if (m_SeenSlotNames.IsEmpty())
				UseBakedSlots();
			else
				Fail("No free matching slots (road slots or small flat slots); no defences placed.");
			return;
		}
		array<ref INF_SlotCandidate> candidates = {};
		foreach (SCR_SiteSlotEntity slot : m_Slots)
		{
			INF_SlotCandidate candidate = new INF_SlotCandidate();
			candidate.m_sName = SlotName(slot);
			candidate.m_vPosition = slot.GetOrigin();
			candidate.m_Slot = slot;
			candidates.Insert(candidate);
		}
		PlaceDefences(candidates);
	}

	protected void PlaceDefences(array<ref INF_SlotCandidate> slots)
	{
		array<ref INF_SlotCandidate> candidates = {};
		foreach (INF_SlotCandidate slot : slots)
		{
			if (Viable(slot.m_vPosition))
				candidates.Insert(slot);
		}
		for (int index = candidates.Count() - 1; index > 0; index--)
		{
			int swap = Math.RandomInt(0, index + 1);
			INF_SlotCandidate held = candidates[index];
			candidates.Set(index, candidates[swap]);
			candidates.Set(swap, held);
		}
		// One percentage of every viable road and small flat slot together.
		int quota = Math.Ceil(candidates.Count() * m_Config.m_iPercent / 100.0);
		foreach (INF_SlotCandidate candidate : candidates)
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
			m_Sites.Count(), m_Sites.Count() - flat, flat, candidates.Count(), slots.Count(), m_Config.m_iPercent, m_iSpacingRejects, m_iOwnerRejects));
		SweepToBases("start");
	}

	protected bool TryPlace(INF_SlotCandidate slot)
	{
		string slotName = slot.m_sName;
		vector position = slot.m_vPosition;
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

	protected INF_Defence PlaceInSlot(INF_SlotCandidate candidate, ResourceName composition)
	{
		Resource resource = Resource.Load(composition);
		if (!resource || !resource.IsValid())
			return null;
		SCR_SiteSlotEntity slot = candidate.m_Slot;
		IEntity entity;
		if (slot)
			entity = slot.SpawnEntityInSlot(resource);
		else
			entity = SpawnAt(resource, candidate.m_vPosition, candidate.m_fYaw);
		if (!entity)
		{
			Fail("Slot refused the composition: " + composition);
			return null;
		}
		if (!INF_ForestCamps.PrepareComposition(entity))
		{
			if (slot)
				slot.SetOccupant(null);
			SCR_EntityHelper.DeleteEntityAndChildren(entity);
			return null;
		}
		INF_Defence defence = new INF_Defence();
		defence.m_Entity = entity;
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
		SweepToBases("start");
	}

	protected static IEntity SpawnAt(Resource resource, vector position, float yaw)
	{
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		Math3D.AnglesToMatrix(Vector(yaw, 0, 0), params.Transform);
		params.Transform[3] = position;
		return GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params);
	}

	protected IEntity SpawnComposition(INF_Defence defence)
	{
		Resource resource = Resource.Load(defence.m_Composition);
		if (!resource || !resource.IsValid())
		{
			Fail("Saved defence composition cannot load, skipping: " + defence.m_Composition);
			return null;
		}
		IEntity entity = SpawnAt(resource, defence.m_vPosition, defence.m_fYaw);
		if (!entity)
		{
			Fail("Saved defence could not be respawned: " + defence.m_sKey);
			return null;
		}
		if (!INF_ForestCamps.PrepareComposition(entity))
		{
			SCR_EntityHelper.DeleteEntityAndChildren(entity);
			return null;
		}
		// The slot is not re-resolved on restore, so tell the slot manager it is taken (GM placement would otherwise stack).
		SetSlotOccupant(defence.m_vPosition, entity);
		defence.m_Entity = entity;
		return entity;
	}

	protected static void SetSlotOccupant(vector position, IEntity entity)
	{
		SCR_CompositionSlotManagerComponent slotManager = SCR_CompositionSlotManagerComponent.GetInstance();
		if (slotManager)
			slotManager.SetOccupant(position, entity);
	}

	// Streams defences out and back in by player distance. Runs from the proximity loop; the whole roster is checked each
	// time (a few squared distances per defence), and the actual spawns and despawns are queued through staggered gates
	// like the AI ones, so the loop never waits for them.
	void Maintain(array<vector> players, float now, bool serverEmpty)
	{
		if (!Replication.IsServer() || !m_Zones || m_Sites.IsEmpty())
			return;
		if (serverEmpty)
		{
			SweepToBases("server empty");
			m_bPaused = true;
			return;
		}
		// Nobody has a body (everyone on the respawn screen): not evidence the map is empty, so pause and restart the idle timers.
		if (players.IsEmpty())
		{
			m_bPaused = true;
			return;
		}
		if (m_bPaused)
		{
			m_bPaused = false;
			m_fResumedAt = now;
		}
		array<INF_Defence> due = {};
		array<float> distances = {};
		for (int index = m_Sites.Count() - 1; index >= 0; index--)
			MaintainOne(m_Sites[index], players, now, due, distances);
		// Nearest first, so what a player is about to meet appears first.
		while (!due.IsEmpty())
		{
			int nearest;
			for (int next = 1; next < due.Count(); next++)
			{
				if (distances[next] < distances[nearest])
					nearest = next;
			}
			INF_Defence defence = due[nearest];
			due.Remove(nearest);
			distances.Remove(nearest);
			nearest = 0;
			defence.m_bQueued = true;
			GetGame().GetCallqueue().CallLater(RespawnQueued, INF_StaggerGate.DefenceSpawns().Reserve(), false, defence);
		}
	}

	protected void RespawnQueued(INF_Defence defence)
	{
		defence.m_bQueued = false;
		if (m_Sites.Find(defence) < 0 || !defence.m_bDespawned)
			return;
		// The player may have left again while this waited in the queue.
		INF_BlockPresence presence = m_Zones.GetPresence();
		if (presence && NearestSq(presence.m_AllPlayers, defence.m_vPosition) > SPAWN_DISTANCE * SPAWN_DISTANCE)
			return;
		if (Respawn(defence))
			return;
		// A transient failure (resource not ready) is retried on later passes; only a persistent one drops the defence.
		defence.m_iSpawnFailures++;
		if (defence.m_iSpawnFailures >= MAX_SPAWN_FAILURES)
			Retire(defence);
	}

	protected void DespawnQueued(INF_Defence defence)
	{
		defence.m_bQueued = false;
		if (m_Sites.Find(defence) < 0 || defence.m_bDespawned || !defence.m_Entity || IsOwnerActive(defence))
			return;
		// A player may have arrived while this waited in the queue.
		INF_BlockPresence presence = m_Zones.GetPresence();
		if (presence && !presence.m_AllPlayers.IsEmpty() && NearestSq(presence.m_AllPlayers, defence.m_vPosition) <= DESPAWN_DISTANCE * DESPAWN_DISTANCE)
		{
			defence.m_fIdleSince = -1;
			return;
		}
		Despawn(defence);
	}

	// Unbudgeted: at start and when the server empties nobody is watching, so everything beyond SPAWN_DISTANCE of a base
	// spawn point goes at once and the slower player-distance streaming only handles the rest of the session.
	void SweepToBases(string reason)
	{
		if (!Replication.IsServer() || !m_Zones || m_Sites.IsEmpty())
			return;
		array<vector> bases = {};
		INF_SpawnProtection.BasePositions(bases);
		// Spawn points built into a defence would anchor that defence to itself.
		for (int index = bases.Count() - 1; index >= 0; index--)
		{
			foreach (INF_Defence own : m_Sites)
			{
				if (vector.DistanceSqXZ(own.m_vPosition, bases[index]) < OWN_SPAWN_RADIUS * OWN_SPAWN_RADIUS)
				{
					bases.Remove(index);
					break;
				}
			}
		}
		if (bases.IsEmpty())
		{
			Fail("No base spawn points found; skipping the " + reason + " defence sweep.");
			return;
		}
		int despawned;
		int retired;
		for (int site = m_Sites.Count() - 1; site >= 0; site--)
		{
			INF_Defence defence = m_Sites[site];
			if (defence.m_bDespawned || defence.m_bQueued || IsOwnerActive(defence) || NearestSq(bases, defence.m_vPosition) <= SPAWN_DISTANCE * SPAWN_DISTANCE)
				continue;
			if (!defence.m_Entity || FullyDestroyed(defence.m_Entity))
			{
				Retire(defence);
				retired++;
			}
			else
			{
				Despawn(defence);
				despawned++;
			}
		}
		if (despawned > 0 || retired > 0)
			Log(string.Format("Sweep (%1): despawned %2, retired %3 of %4 defences beyond %5 m of a base.", reason, despawned, retired, m_Sites.Count() + retired, SPAWN_DISTANCE));
	}

	protected void Despawn(INF_Defence defence)
	{
		SetSlotOccupant(defence.m_vPosition, null);
		SCR_EntityHelper.DeleteEntityAndChildren(defence.m_Entity);
		defence.m_Entity = null;
		defence.m_bDespawned = true;
		defence.m_fIdleSince = -1;
	}

	// Decides what this defence needs; spawns of despawned ones are collected for nearest-first queueing, despawns are queued here.
	protected void MaintainOne(INF_Defence defence, array<vector> players, float now, array<INF_Defence> due, array<float> distances)
	{
		if (defence.m_bQueued)
			return;
		float nearestSq = NearestSq(players, defence.m_vPosition);
		if (defence.m_bDespawned)
		{
			if (nearestSq <= SPAWN_DISTANCE * SPAWN_DISTANCE)
			{
				due.Insert(defence);
				distances.Insert(nearestSq);
			}
			return;
		}
		// A live owner zone means its guards are out; never pull the composition from under them.
		if (nearestSq <= DESPAWN_DISTANCE * DESPAWN_DISTANCE || IsOwnerActive(defence))
		{
			defence.m_fIdleSince = -1;
			defence.m_bSeenNear = true;
			return;
		}
		if (!defence.m_Entity)
		{
			Retire(defence);
			return;
		}
		// The delay only covers a player coming back (respawn); a defence nobody has been near since start can go at once.
		if (defence.m_bSeenNear)
		{
			if (defence.m_fIdleSince < m_fResumedAt)
			{
				defence.m_fIdleSince = now;
				return;
			}
			if (now - defence.m_fIdleSince < INF_BlockRules.DESPAWN_DELAY_MS)
				return;
		}
		if (FullyDestroyed(defence.m_Entity))
		{
			Retire(defence);
			return;
		}
		defence.m_bQueued = true;
		GetGame().GetCallqueue().CallLater(DespawnQueued, INF_StaggerGate.DefenceDespawns().Reserve(), false, defence);
	}

	protected bool Respawn(INF_Defence defence)
	{
		if (!SpawnComposition(defence))
			return false;
		defence.m_bDespawned = false;
		defence.m_iSpawnFailures = 0;
		defence.m_bSeenNear = true;
		defence.m_fIdleSince = -1;
		return true;
	}

	// Destroyed defences are not brought back: dropped from the roster, the owner zone and the saved file.
	protected void Retire(INF_Defence defence)
	{
		if (defence.m_Entity)
		{
			SetSlotOccupant(defence.m_vPosition, null);
			SCR_EntityHelper.DeleteEntityAndChildren(defence.m_Entity);
			defence.m_Entity = null;
		}
		INS_AmbientPatrolZoneState owner = m_Zones.GetZone(defence.m_sOwnerKey);
		if (owner)
			owner.m_aINFDefences.RemoveItem(defence);
		m_Sites.RemoveItem(defence);
		if (!Save())
			Print("[INF_Defences] Cannot save the defence roster after retiring " + defence.m_sKey + ".", LogLevel.ERROR);
	}

	protected bool IsOwnerActive(INF_Defence defence)
	{
		INS_AmbientPatrolZoneState owner = m_Zones.GetZone(defence.m_sOwnerKey);
		return owner && owner.m_bActive;
	}

	protected static float NearestSq(array<vector> players, vector position)
	{
		float best = 1e18;
		foreach (vector player : players)
			best = Math.Min(best, vector.DistanceSqXZ(player, position));
		return best;
	}

	protected static bool FullyDestroyed(IEntity entity)
	{
		int total;
		int intact;
		CountDamageable(entity, total, intact);
		return total > 0 && intact == 0;
	}

	protected static void CountDamageable(IEntity entity, out int total, out int intact)
	{
		DamageManagerComponent damage = DamageManagerComponent.Cast(entity.FindComponent(DamageManagerComponent));
		if (damage)
		{
			total++;
			if (damage.GetState() != EDamageState.DESTROYED)
				intact++;
		}
		IEntity child = entity.GetChildren();
		while (child)
		{
			CountDamageable(child, total, intact);
			child = child.GetSibling();
		}
	}

	protected bool Restore(INF_Defence defence)
	{
		if (!SpawnComposition(defence))
			return false;
		defence.m_bFlat = SlotType(defence.m_Composition) == FLAT_SLOT;
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
