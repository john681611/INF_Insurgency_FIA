// Spawned entities are rebuilt by the zone systems each session, so the save system must not track them.
class INF_Persistence
{
	static void Untrack(IEntity entity)
	{
		PersistenceSystem persistence = PersistenceSystem.GetInstance();
		if (persistence)
			persistence.StopTracking(entity, true);
	}
}

class INF_Spawn
{
	static IEntity Prefab(Resource resource, vector position, float yaw = 0)
	{
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		Math3D.AnglesToMatrix(Vector(yaw, 0, 0), params.Transform);
		params.Transform[3] = position;
		return GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params);
	}
}

class INF_ArrayHelper<Class T>
{
	static void Shuffle(notnull array<T> items)
	{
		for (int i = items.Count() - 1; i > 0; i--)
		{
			int j = Math.RandomInt(0, i + 1);
			T swap = items[i];
			items[i] = items[j];
			items[j] = swap;
		}
	}
}
