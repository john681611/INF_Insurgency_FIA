class INF_ForestCampRewardSite
{
	string m_Key;
	vector m_Center;
	vector m_Position;
	float m_Yaw;
}

// Camp discovery owns site positions; cache objectives remain owned by the upstream managers.
class INF_ForestCampRewards
{
	protected static ref array<vector> s_aSites = {};
	protected static ref array<ref INF_ForestCampRewardSite> s_aLayouts = {};
	protected static ref array<vector> s_aCachePositions = {};
	protected static ref map<string, IEntity> s_mCacheEntities = new map<string, IEntity>();
	protected static ref array<string> s_aSearched = {};
	protected static bool s_bDiscoveryReady = true;
	protected static bool s_bBegun;
	protected static bool s_bSkipped;
	protected static bool s_bCachesReady;
	protected static bool s_bCampObjectsReady;
	protected static bool s_bSearchStateValid = true;
	protected static bool s_bManifestValid = true;
	protected static const string SEARCH_SAVE = "$profile:INS_Saves/forest_camp_searches.dat";
	protected static const string SITE_SAVE = "$profile:INS_Saves/forest_camp_sites.dat";

	static string SiteID(vector pos)
	{
		int x = Math.Round(pos[0] * 10);
		int z = Math.Round(pos[2] * 10);
		return string.Format("%1:%2", x, z);
	}

	static void BeginDiscovery()
	{
		if (!Replication.IsServer() || s_bBegun)
			return;

		s_bBegun = true;
		s_bSkipped = false;
		s_aSites.Clear();
		s_aLayouts.Clear();
		s_aCachePositions.Clear();
		s_mCacheEntities.Clear();
		s_aSearched.Clear();
		s_bDiscoveryReady = false;
		s_bCachesReady = false;
		s_bCampObjectsReady = false;
		s_bManifestValid = true;
		s_bSearchStateValid = LoadSearches();
	}

	// Legacy missions must not wait for a camp service or overwrite its saved manifest.
	static void SkipDiscovery()
	{
		if (!Replication.IsServer())
			return;
		s_bBegun = true;
		s_bSkipped = true;
		s_aSites.Clear();
		s_aLayouts.Clear();
		s_aCachePositions.Clear();
		s_mCacheEntities.Clear();
		s_aSearched.Clear();
		s_bDiscoveryReady = true;
		s_bCachesReady = false;
		s_bCampObjectsReady = true;
		s_bSearchStateValid = true;
		s_bManifestValid = true;
	}

	static vector GetCachePosition(vector layoutPosition, float yaw)
	{
		float angle = yaw * Math.DEG2RAD;
		vector pos = layoutPosition + Vector(8 * Math.Sin(angle), 0, 8 * Math.Cos(angle));
		pos[1] = GetGame().GetWorld().GetSurfaceY(pos[0], pos[2]);
		return pos;
	}

	// Register the layout centre. Cache/reward objects share the separate local (0,0,8) slot.
	static string RegisterSite(vector layoutPosition, vector center = "0 0 0", float yaw = 0, string key = "")
	{
		if (!Replication.IsServer() || s_bDiscoveryReady || !s_bManifestValid)
			return string.Empty;

		string id = SiteID(layoutPosition);
		foreach (vector pos : s_aSites)
		{
			if (SiteID(pos) == id)
				return id;
		}
		s_aSites.Insert(layoutPosition);
		INF_ForestCampRewardSite layout = new INF_ForestCampRewardSite();
		layout.m_Key = key;
		layout.m_Center = center;
		layout.m_Position = layoutPosition;
		layout.m_Yaw = yaw;
		s_aLayouts.Insert(layout);
		return id;
	}

	static void CompleteDiscovery()
	{
		if (Replication.IsServer() && !s_bSkipped)
		{
			if (!s_bManifestValid)
			{
				Print("[INF_ForestCampRewards] Saved camp layouts unavailable; preserving the manifest and restoring original saved cache positions without new camp candidates.", LogLevel.ERROR);
				s_bDiscoveryReady = true;
				return;
			}
			if (!SaveSites())
			{
				Print("[INF_ForestCampRewards] Cannot save camp manifest; excluding new camp cache candidates.", LogLevel.ERROR);
				s_aSites.Clear();
			}
			s_bDiscoveryReady = true;
		}
	}

	// Call immediately after BeginDiscovery, before fresh discovery. Reuse recovered layouts
	// and RegisterSite each one; do not regenerate sites while recovering saved objectives.
	static bool GetRecoveredSites(out array<ref INF_ForestCampRewardSite> sites)
	{
		sites = {};
		if (!Replication.IsServer() || !FileIO.FileExists(INS_CACHE_SAVE_FILE))
			return false;
		if (!FileIO.FileExists(SITE_SAVE))
		{
			Print("[INF_ForestCampRewards] Saved caches exist without a camp manifest; recovering existing caches with fresh camp discovery.", LogLevel.WARNING);
			return false;
		}

		FileSerializer file = new FileSerializer();
		if (!file.Open(SITE_SAVE, FileMode.READ))
		{
			return UnavailableManifest("Cannot open saved camp manifest: " + SITE_SAVE);
		}
		int count;
		if (!file.Read(count) || count < 0 || count > 24)
		{
			file.Close();
			return UnavailableManifest("Invalid camp manifest count (expected 0..24).");
		}
		if (count == 0)
		{
			file.Close();
			Print("[INF_ForestCampRewards] Saved camp manifest is empty; retrying discovery without changing saved cache positions.", LogLevel.WARNING);
			return false;
		}
		array<string> keys = {};
		array<string> coordinates = {};
		for (int i = 0; i < count; i++)
		{
			INF_ForestCampRewardSite site = new INF_ForestCampRewardSite();
			if (!file.Read(site.m_Key) || !file.Read(site.m_Center) || !file.Read(site.m_Position) || !file.Read(site.m_Yaw))
			{
				file.Close();
				sites.Clear();
				return UnavailableManifest(string.Format("Cannot read camp manifest entry %1.", i));
			}
			if (site.m_Key.IsEmpty() || site.m_Key.Length() > 64 || !ValidSavedVector(site.m_Center) || !ValidSavedVector(site.m_Position)
				|| !(site.m_Yaw >= -360 && site.m_Yaw <= 360))
			{
				file.Close();
				sites.Clear();
				return UnavailableManifest(string.Format("Invalid camp manifest coordinates, yaw or key at entry %1.", i));
			}
			string coordinate = SiteID(site.m_Position);
			if (keys.Contains(site.m_Key) || coordinates.Contains(coordinate) || vector.DistanceSqXZ(site.m_Center, site.m_Position) > 22500)
			{
				file.Close();
				sites.Clear();
				return UnavailableManifest(string.Format("Duplicate or inconsistent camp manifest entry %1.", i));
			}
			keys.Insert(site.m_Key);
			coordinates.Insert(coordinate);
			sites.Insert(site);
		}
		file.Close();
		return true;
	}

	protected static bool ValidSavedVector(vector value)
	{
		// Positive range comparisons also reject NaN and infinities without unverified math APIs.
		for (int i = 0; i < 3; i++)
		{
			if (!(value[i] >= -1000000 && value[i] <= 1000000))
				return false;
		}
		return true;
	}

	protected static bool UnavailableManifest(string reason)
	{
		s_bManifestValid = false;
		s_aSites.Clear();
		s_aLayouts.Clear();
		Print("[INF_ForestCampRewards] " + reason + " Camp recovery is fail-closed; preserving the manifest, with no fresh camps. Saved caches will still restore.", LogLevel.ERROR);
		return true;
	}

	protected static bool SaveSites()
	{
		FileIO.MakeDirectory(INS_CACHE_SAVE_DIR);
		FileSerializer file = new FileSerializer();
		if (!file.Open(SITE_SAVE, FileMode.WRITE))
			return false;
		bool success = file.Write(s_aLayouts.Count());
		foreach (INF_ForestCampRewardSite site : s_aLayouts)
		{
			if (!file.Write(site.m_Key) || !file.Write(site.m_Center) || !file.Write(site.m_Position) || !file.Write(site.m_Yaw))
				success = false;
		}
		file.Close();
		return success;
	}

	static bool IsDiscoveryReady()
	{
		return s_bDiscoveryReady;
	}

	static bool AreCachesReady()
	{
		return s_bCachesReady;
	}

	static int CacheCount()
	{
		return s_aCachePositions.Count();
	}

	static void GetCachePositions(out array<vector> positions)
	{
		positions = {};
		foreach (vector pos : s_aCachePositions)
			positions.Insert(pos);
	}

	static void FinishCampObjects()
	{
		s_bCampObjectsReady = true;
	}

	static bool AreCampObjectsReady()
	{
		return s_bCampObjectsReady;
	}

	static void AppendCandidates(notnull array<vector> positions)
	{
		foreach (INF_ForestCampRewardSite site : s_aLayouts)
		{
			if (!s_aSites.Contains(site.m_Position))
				continue;
			vector pos = GetCachePosition(site.m_Position, site.m_Yaw);
			if (!positions.Contains(pos))
				positions.Insert(pos);
		}
	}

	static void SetCachePositions(notnull array<vector> positions)
	{
		s_aCachePositions.Clear();
		foreach (vector pos : positions)
			s_aCachePositions.Insert(pos);
	}

	static void FinishCaches()
	{
		s_bCachesReady = true;
	}

	static bool IsCacheSite(string id)
	{
		foreach (INF_ForestCampRewardSite site : s_aLayouts)
		{
			if (SiteID(site.m_Position) != id)
				continue;
			string slotID = SiteID(GetCachePosition(site.m_Position, site.m_Yaw));
			foreach (vector pos : s_aCachePositions)
			{
				if (SiteID(pos) == slotID)
					return true;
			}
		}
		return false;
	}

	static void SetCacheEntity(vector position, IEntity entity)
	{
		s_mCacheEntities.Set(SiteID(position), entity);
	}

	// For clearance trace exclusions ONLY. Never hand this entity to ordinary camp cleanup.
	static IEntity GetCacheEntity(string id)
	{
		foreach (INF_ForestCampRewardSite site : s_aLayouts)
		{
			if (SiteID(site.m_Position) == id)
				return s_mCacheEntities.Get(SiteID(GetCachePosition(site.m_Position, site.m_Yaw)));
		}
		return null;
	}

	static bool CanSearch(string id)
	{
		if (!s_bCachesReady || !s_bSearchStateValid || id.IsEmpty() || IsCacheSite(id) || s_aSearched.Contains(id))
			return false;

		foreach (vector pos : s_aSites)
		{
			if (SiteID(pos) == id)
				return true;
		}
		return false;
	}

	// Persist consumption BEFORE rolling or awarding: failed writes must never allow rewards.
	static bool Search(string id, IEntity user)
	{
		if (!Replication.IsServer() || !user || !CanSearch(id))
			return false;

		INS_IntelManager intel = INS_IntelManager.GetInstance();
		if (!intel || !intel.INF_IsIntelReady())
		{
			Print("[INF_ForestCampRewards] Cannot search arsenal before intel recovery is ready.", LogLevel.WARNING);
			return false;
		}

		int playerID = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(user);
		if (playerID <= 0)
			return false;

		s_aSearched.Insert(id);
		if (!SaveSearches())
		{
			s_bSearchStateValid = false;
			Print("[INF_ForestCampRewards] Search persistence failed; disabling rewards to prevent repeat awards.", LogLevel.ERROR);
			return false;
		}

		if (Math.RandomFloat01() < 0.8)
			intel.AddIntelAndBroadcast(1, playerID);
		else
			intel.BroadcastSearchFailed(playerID);
		return true;
	}

	protected static bool LoadSearches()
	{
		if (!FileIO.FileExists(SEARCH_SAVE))
			return true;

		FileSerializer file = new FileSerializer();
		if (!file.Open(SEARCH_SAVE, FileMode.READ))
		{
			Print("[INF_ForestCampRewards] Cannot open saved searches; disabling camp intel rewards: " + SEARCH_SAVE, LogLevel.ERROR);
			return false;
		}

		int count;
		if (!file.Read(count) || count < 0 || count > 100000)
		{
			file.Close();
			Print("[INF_ForestCampRewards] Invalid saved search count; disabling camp intel rewards.", LogLevel.ERROR);
			return false;
		}
		for (int i = 0; i < count; i++)
		{
			string id;
			if (!file.Read(id) || id.IsEmpty())
			{
				file.Close();
				Print(string.Format("[INF_ForestCampRewards] Cannot read saved search entry %1; disabling camp intel rewards.", i), LogLevel.ERROR);
				return false;
			}
			s_aSearched.Insert(id);
		}
		file.Close();
		return true;
	}

	protected static bool SaveSearches()
	{
		FileIO.MakeDirectory(INS_CACHE_SAVE_DIR);
		FileSerializer file = new FileSerializer();
		if (!file.Open(SEARCH_SAVE, FileMode.WRITE))
			return false;

		bool success = file.Write(s_aSearched.Count());
		foreach (string id : s_aSearched)
		{
			if (!file.Write(id))
				success = false;
		}
		file.Close();
		return success;
	}

	static void EndSession()
	{
		if (!Replication.IsServer())
			return;
		if (FileIO.FileExists(SEARCH_SAVE) && !FileIO.DeleteFile(SEARCH_SAVE))
			Print("[INF_ForestCampRewards] Cannot delete saved searches on clean mission end: " + SEARCH_SAVE, LogLevel.ERROR);
		if (!s_bManifestValid && FileIO.FileExists(SITE_SAVE))
			Print("[INF_ForestCampRewards] Preserving unreadable camp manifest on mission end for diagnosis: " + SITE_SAVE, LogLevel.WARNING);
		else if (FileIO.FileExists(SITE_SAVE) && !FileIO.DeleteFile(SITE_SAVE))
			Print("[INF_ForestCampRewards] Cannot delete camp manifest on clean mission end: " + SITE_SAVE, LogLevel.ERROR);
		s_aSites.Clear();
		s_aLayouts.Clear();
		s_aCachePositions.Clear();
		s_mCacheEntities.Clear();
		s_aSearched.Clear();
		s_bDiscoveryReady = true;
		s_bBegun = false;
		s_bSkipped = false;
		s_bCachesReady = false;
		s_bManifestValid = true;
	}
}

modded class INS_CacheSpawnManager
{
	protected bool m_bINFCacheSelectionStarted;
	protected ref array<vector> m_aINFChosenCaches = {};
	protected ref array<vector> m_aINFVerifiedCaches = {};
	protected ref array<IEntity> m_aINFVerifiedCacheEntities = {};

	override void OnGameModeStart()
	{
		if (Replication.IsServer())
		{
			INS_AmbientPatrolManager patrols = INS_AmbientPatrolManager.Cast(GetOwner().FindComponent(INS_AmbientPatrolManager));
			if (patrols && patrols.INF_UsesBuildingGrid())
				INF_ForestCampRewards.BeginDiscovery();
			else
				INF_ForestCampRewards.SkipDiscovery();
			m_bINFCacheSelectionStarted = false;
			m_aINFVerifiedCaches.Clear();
			m_aINFVerifiedCacheEntities.Clear();
		}
		super.OnGameModeStart();
	}

	override protected void SpawnCaches()
	{
		if (!INF_ZoneEditorContext.Grid(GetOwner()))
		{
			super.SpawnCaches();
			return;
		}
		if (!Replication.IsServer() || m_bINFCacheSelectionStarted)
			return;

		if (!INF_ForestCampRewards.IsDiscoveryReady() || !INF_ForestCampRewards.AreCampObjectsReady())
		{
			GetGame().GetCallqueue().CallLater(SpawnCaches, 250, false);
			return;
		}
		m_bINFCacheSelectionStarted = true;

		// Keep upstream recovery, selection count and spawn/adoption lifecycle intact.
		if (m_sCachePrefab.IsEmpty())
		{
			Print("[INF_ForestCampRewards] No cache prefab assigned; static-site startup failed.", LogLevel.ERROR);
			return;
		}

		array<vector> chosen = {};
		bool recovered = LoadCachePositions(chosen);
		if (!recovered && (FileIO.FileExists(INS_CACHE_SAVE_FILE) || FileIO.FileExists(INS_INTEL_SAVE_FILE)))
		{
			Print("[INF_ForestCampRewards] Cache position recovery failed or paired positions are missing; preserving saves and blocking fresh selection.", LogLevel.ERROR);
			return;
		}
		if (!recovered)
		{
			array<vector> all = {};
			INS_CacheSpawnPoint.CollectPositions(all);
			INF_ForestCampRewards.AppendCandidates(all);
			array<vector> unique = {};
			foreach (vector candidate : all)
			{
				if (!unique.Contains(candidate))
					unique.Insert(candidate);
			}
			all = unique;
			for (int i = all.Count() - 1; i > 0; i--)
			{
				int j = Math.RandomInt(0, i + 1);
				vector temp = all[i];
				all[i] = all[j];
				all[j] = temp;
			}
			int count = Math.Min(m_iCacheCount, all.Count());
			if (count < m_iCacheCount)
				Print(string.Format("[INF_ForestCampRewards] Only %1 unique cache candidates available for %2 requested caches.", count, m_iCacheCount), LogLevel.WARNING);
			for (int i = 0; i < count; i++)
				chosen.Insert(all[i]);
			if (!chosen.IsEmpty())
				SaveCachePositions(chosen);
		}
		else
		{
			Print(string.Format("[INF_ForestCampRewards] Recovering %1 saved cache position(s).", chosen.Count()));
		}
		Print(string.Format("[INF_ForestCampRewards] Authoritative cache roster: configured=%1, selected=%2, recovered=%3.",
			m_iCacheCount, chosen.Count(), recovered));

		// Retain assignments even when intel recovery deletes a destroyed cache entity.
		INF_ForestCampRewards.SetCachePositions(chosen);
		m_aINFChosenCaches = chosen;
		if (chosen.IsEmpty())
		{
			Print("[INF_ForestCampRewards] No cache positions selected; static-site startup failed.", LogLevel.ERROR);
			return;
		}
		INF_VerifyStaticCaches();
	}

	override protected bool LoadCachePositions(out array<vector> outPos)
	{
		if (!INF_ZoneEditorContext.Grid(GetOwner()))
			return super.LoadCachePositions(outPos);
		outPos = {};
		if (!Replication.IsServer() || !FileIO.FileExists(INS_CACHE_SAVE_FILE))
			return false;
		FileSerializer file = new FileSerializer();
		if (!file.Open(INS_CACHE_SAVE_FILE, FileMode.READ))
			return false;
		int count;
		if (!file.Read(count) || count <= 0 || count > 100000)
		{
			file.Close();
			return false;
		}
		for (int i = 0; i < count; i++)
		{
			float x, y, z;
			if (!file.Read(x) || !file.Read(y) || !file.Read(z)
				|| !(x >= -1000000 && x <= 1000000 && y >= -1000000 && y <= 1000000 && z >= -1000000 && z <= 1000000))
			{
				file.Close();
				return false;
			}
			vector pos = Vector(x, y, z);
			if (outPos.Contains(pos))
			{
				file.Close();
				return false;
			}
			outPos.Insert(pos);
		}
		file.Close();
		return true;
	}

	protected void INF_VerifyStaticCaches()
	{
		if (!Replication.IsServer())
			return;
		bool complete = true;
		foreach (vector pos : m_aINFChosenCaches)
		{
			if (m_aINFVerifiedCaches.Contains(pos))
				continue;
			SpawnCacheAt(pos);
			IEntity cache = FindExistingCacheNearPoint(pos, 2.0);
			INF_ForestCampRewards.SetCacheEntity(pos, cache);
			if (!cache)
				complete = false;
			else if (m_aINFVerifiedCacheEntities.Contains(cache))
			{
				Print("[INF_ForestCampRewards] Selected positions resolve to the same cache entity; holding startup rather than reducing the objective total.", LogLevel.ERROR);
				complete = false;
			}
			else
			{
				m_aINFVerifiedCaches.Insert(pos);
				m_aINFVerifiedCacheEntities.Insert(cache);
			}
		}
		if (!complete)
		{
			Print("[INF_ForestCampRewards] Selected cache entities missing; holding startup and retrying in five seconds.", LogLevel.ERROR);
			GetGame().GetCallqueue().CallLater(INF_VerifyStaticCaches, 5000, false);
			return;
		}
		INF_ForestCampRewards.FinishCaches();
	}

	override void OnGameModeEnd(SCR_GameModeEndData data)
	{
		GetGame().GetCallqueue().Remove(SpawnCaches);
		GetGame().GetCallqueue().Remove(INF_VerifyStaticCaches);
		m_bINFCacheSelectionStarted = true;
		super.OnGameModeEnd(data);
		INF_ForestCampRewards.EndSession();
	}
}

[ComponentEditorProps(category: "Insurgency", description: "Binds a camp arsenal to persistent per-site intel and replicated friendly ownership.")]
class INF_ForestCampSearchComponentClass : ScriptComponentClass
{
}

class INF_ForestCampSearchComponent : ScriptComponent
{
	[RplProp()]
	protected string m_sSiteID;

	[RplProp()]
	protected bool m_bUnavailable = true;

	[RplProp(onRplName: "ApplyFriendlyFaction")]
	protected string m_sFriendlyFaction;
	protected bool m_bFactionWarning;

	bool BindFriendlyFaction(string factionKey)
	{
		if (!Replication.IsServer())
			return false;
		if (factionKey.IsEmpty() || !GetGame().GetFactionManager().GetFactionByKey(factionKey)
			|| !GetOwner().FindComponent(FactionAffiliationComponent))
		{
			Print("[INF_ForestCamps] Cannot transfer arsenal: player faction or arsenal affiliation is missing.", LogLevel.ERROR);
			return false;
		}
		m_sFriendlyFaction = factionKey;
		ApplyFriendlyFaction();
		Replication.BumpMe();
		return true;
	}

	protected void ApplyFriendlyFaction()
	{
		GetGame().GetCallqueue().Remove(ApplyFriendlyFaction);
		if (m_sFriendlyFaction.IsEmpty())
			return;
		FactionAffiliationComponent affiliation = FactionAffiliationComponent.Cast(GetOwner().FindComponent(FactionAffiliationComponent));
		if (!affiliation)
		{
			Print("[INF_ForestCamps] Captured arsenal is missing faction affiliation.", LogLevel.ERROR);
			return;
		}
		Faction faction = GetGame().GetFactionManager().GetFactionByKey(m_sFriendlyFaction);
		if (!faction)
		{
			if (!m_bFactionWarning)
				Print("[INF_ForestCamps] Replicated arsenal player faction is not registered; waiting for faction initialization.", LogLevel.WARNING);
			m_bFactionWarning = true;
			GetGame().GetCallqueue().CallLater(ApplyFriendlyFaction, 5000, false);
			return;
		}
		m_bFactionWarning = false;
		if (affiliation.GetAffiliatedFaction() != faction)
			affiliation.SetAffiliatedFaction(faction);
	}

	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(ApplyFriendlyFaction);
		super.OnDelete(owner);
	}

	override void OnPostInit(IEntity owner)
	{
		SetEventMask(owner, EntityEvent.INIT);
	}

	override void EOnInit(IEntity owner)
	{
		ApplyFriendlyFaction();
	}

	void BindSite(string id)
	{
		if (!Replication.IsServer())
			return;
		m_sSiteID = id;
		m_bUnavailable = !INF_ForestCampRewards.CanSearch(id);
		Replication.BumpMe();
	}

	bool IsAvailable()
	{
		return !m_bUnavailable && !m_sSiteID.IsEmpty();
	}

	void Search(IEntity user)
	{
		if (!Replication.IsServer() || !IsAvailable() || !user || !GetOwner())
			return;
		if (vector.Distance(user.GetOrigin(), GetOwner().GetOrigin()) > 3)
			return;

		INF_ForestCampRewards.Search(m_sSiteID, user);
		m_bUnavailable = !INF_ForestCampRewards.CanSearch(m_sSiteID);
		Replication.BumpMe();
	}
}

class INF_ForestCampSearchAction : ScriptedUserAction
{
	protected INF_ForestCampSearchComponent GetSearchComponent()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return null;
		return INF_ForestCampSearchComponent.Cast(owner.FindComponent(INF_ForestCampSearchComponent));
	}

	override bool GetActionNameScript(out string outName)
	{
		outName = "Search camp for intel";
		return true;
	}

	override bool CanBeShownScript(IEntity user)
	{
		INF_ForestCampSearchComponent component = GetSearchComponent();
		return component && component.IsAvailable();
	}

	override bool CanBePerformedScript(IEntity user)
	{
		return CanBeShownScript(user);
	}

	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		INF_ForestCampSearchComponent component = GetSearchComponent();
		if (component)
			component.Search(pUserEntity);
	}
}
