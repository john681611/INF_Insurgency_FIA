modded class SCR_DestructionMultiPhaseComponent
{
    override void OnDamageStateChanged(EDamageState newState, EDamageState previousDamageState, bool isJIP)
    {
        super.OnDamageStateChanged(newState, previousDamageState, isJIP);

        if (!Replication.IsServer())
            return;

        if (newState != EDamageState.DESTROYED)
            return;

        IEntity owner = GetOwner();
        if (!owner)
            return;

        INS_IntelManager mgr = INS_IntelManager.GetInstance();
        if (!mgr || !mgr.IsRegisteredCache(owner))
            return;
        mgr.OnCacheDestroyed(owner);

        vector pos = owner.GetOrigin();
		 // Spawn the explosion prefab
        Resource res = Resource.Load("{72BEEF40AF179763}Prefabs/Weapons/Warheads/Explosions/Explosion_Tnt_Large.et");
        EntitySpawnParams params = new EntitySpawnParams();
        params.TransformMode = ETransformMode.WORLD;
        params.Transform[3] = pos;
        GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), params);
        

       
    }
}