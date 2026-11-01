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
        // The explosion is spawned by the objective manager's OnCacheDestroyed.
        mgr.OnCacheDestroyed(owner);
    }
}