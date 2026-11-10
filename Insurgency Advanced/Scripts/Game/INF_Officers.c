// Enemy officers: a few zones and camps (selected once, saved with the territory snapshot) hold an officer whose
// squad is an ordinary defence-guard group, so spawning, budget, memory, despawn and capture blocking reuse the
// defence guard path. Nothing here runs on its own timer.
class INF_Officers
{
	static const int ZONES_PER_OFFICER = 50;
	static const int SQUAD_SIZE = 4;
	static const int CHANCE_MULTIPLIER = 3;
	static const int QUESTION_LIMIT = 3;
	static const float QUESTION_CHANCE = 0.75;
	static const float SEARCH_CHANCE = 0.5;

	static bool Active(INS_AmbientPatrolZoneState state)
	{
		return state.m_bINFOfficer && state.m_INFOfficerPost;
	}

	// Mortar and vehicle rolls are tripled (capped at 100) in a zone that holds an officer.
	static int Boost(int chance, INS_AmbientPatrolZoneState zone)
	{
		if (chance <= 0 || !zone.m_bINFOfficer)
			return chance;
		return Math.Min(100, chance * CHANCE_MULTIPLIER);
	}

	// Selection pool: hostile zones and camps that are not already secured.
	static void Select(array<ref INS_AmbientPatrolZoneState> zones, array<INS_AmbientPatrolZoneState> chosen)
	{
		array<INS_AmbientPatrolZoneState> pool = {};
		foreach (INS_AmbientPatrolZoneState state : zones)
		{
			if (state.m_iGroupCount > 0 && !state.m_bINFPermanent)
				pool.Insert(state);
		}
		INF_ArrayHelper<INS_AmbientPatrolZoneState>.Shuffle(pool);
		int count = Math.Min(pool.Count(), Math.Max(1, pool.Count() / ZONES_PER_OFFICER));
		for (int k = 0; k < count; k++)
			chosen.Insert(pool[k]);
	}

	// The post is where the officer's squad stands: the camp, else the building nearest the zone centre.
	static INF_Defence CreatePost(INS_AmbientPatrolZoneState state)
	{
		INF_Defence post = new INF_Defence();
		post.m_sKey = "officer:" + INF_BuildingGrid.Key(state.m_vCenter);
		post.m_sOwnerKey = INF_BuildingGrid.Key(state.m_vCenter);
		post.m_bOfficerPost = true;
		vector position = state.m_vCenter;
		if (state.m_INFCamp)
			position = state.m_INFCamp.m_Position;
		else
		{
			float nearest = -1;
			foreach (vector building : state.m_aINFBuildingPositions)
			{
				float distance = vector.DistanceSqXZ(building, state.m_vCenter);
				if (nearest < 0 || distance < nearest)
				{
					nearest = distance;
					position = building;
				}
			}
		}
		position[1] = GetGame().GetWorld().GetSurfaceY(position[0], position[2]);
		post.m_vPosition = position;
		return post;
	}

	// Called while the zone plan is built, before the other guards so the officer's squad is always plan entry zero of the guards.
	static void AppendPost(INS_AmbientPatrolZoneState state, float now)
	{
		if (!Active(state))
			return;
		INF_Defence post = state.m_INFOfficerPost;
		int size = SQUAD_SIZE;
		if (post.m_iRememberedAI >= 0 && now - post.m_fRememberedAt < INF_BuildingGrid.DEFENDER_MEMORY_MS)
			size = Math.Min(size, post.m_iRememberedAI);
		if (size <= 0)
			return;
		post.m_iActiveSize = size;
		state.m_aINFPlan.Insert(size);
		state.m_aINFActiveDefences.Insert(post);
	}

	static bool HasRememberedSquad(INS_AmbientPatrolZoneState state, float now)
	{
		return Active(state) && state.m_INFOfficerPost.m_iRememberedAI > 0
			&& now - state.m_INFOfficerPost.m_fRememberedAt < INF_BuildingGrid.DEFENDER_MEMORY_MS;
	}

	// The officer joins the squad that was just spawned at the post.
	static void SpawnInto(IEntity groupEntity, vector position, ResourceName prefab, string key)
	{
		SCR_AIGroup group = SCR_AIGroup.Cast(groupEntity);
		Resource resource = Resource.Load(prefab);
		if (!group || !resource || !resource.IsValid())
		{
			Print("[INF_Officers] Cannot spawn the officer: squad or officer prefab unavailable: " + prefab, LogLevel.ERROR);
			return;
		}
		IEntity entity = INF_Spawn.Prefab(resource, position);
		ChimeraCharacter character = ChimeraCharacter.Cast(entity);
		AIControlComponent control;
		AIAgent agent;
		if (character)
			control = character.GetAIControlComponent();
		if (control)
			agent = control.GetControlAIAgent();
		if (!agent)
		{
			Print("[INF_Officers] Officer prefab has no AI agent: " + prefab, LogLevel.ERROR);
			if (entity)
				SCR_EntityHelper.DeleteEntityAndChildren(entity);
			return;
		}
		INF_Persistence.Untrack(entity);
		group.AddAgent(agent);
		INF_PrisonerComponent prisoner = INF_PrisonerComponent.Find(entity);
		if (!prisoner)
		{
			Print("[INF_Officers] Officer prefab has no INF_PrisonerComponent; it cannot be captured or questioned: " + prefab, LogLevel.ERROR);
			return;
		}
		prisoner.INF_MarkOfficer(key);
	}

	// Killed or captured: the officer is gone for good and his zone no longer gets the boosts.
	static void Down(string key)
	{
		INS_AmbientPatrolManager patrols = INF_ZoneEditorContext.Grid(GetGame().GetGameMode());
		if (patrols && patrols.INF_GetZoneStates())
			patrols.INF_GetZoneStates().OfficerDown(key);
	}
}
