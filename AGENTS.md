# Agent notes

This folder holds several Arma Reforger addons. Some are separate Git repos nested inside it (each has its own `AGENTS.md`).

The `Insurgency*` addons (`Insurgency Advanced`, `Insurgency Advanced FIA`, `Insurgency FIA`, `Insurgency FIA Ext Veh`, `Insurgency FIA Guerrilla Militia`) are tracked together by the Git repo rooted at this folder, hosted at https://github.com/john681611/INF_Insurgency_FIA (branch `main`). The root `.gitignore` ignores everything else.

- When publishing one of those addons or finishing a set of changes, suggest committing and pushing the repo changes to GitHub.
- Check `git status` first so uncommitted or unpushed work isn't missed.

## Reforger development knowledge

Before writing or changing Arma Reforger code (Enfusion scripts, prefabs, configs), read the wiki skill at `C:\Users\john6\Documents\My Games\ArmaReforgerWorkbench\addons\ReforgerDevSkill` (start with `reforgerwiki\SKILL.md` and `README.md`). It holds Enfusion API overviews, scripting patterns and gotchas. Add new lessons learned to it.

## Pending work (deliberately uncommitted)

**Camp footprint blocked by its own saved cache** (`Insurgency Advanced`, uncommitted on purpose; the user asked to leave it uncommitted until it is tested on the server. Do not commit or push it without being asked.)

- Problem: a server restart with saves in place failed with `[INF_ForestCamps] Static composition not ready: <key>; retrying startup.` forever for one camp, blocking defences (they start after camps are ready). A fresh start has no issue.
- Cause (found from the server log, user confirmed it is a cache): the camp's reward cache is restored from the save before the cache registry knows it (`SetCacheEntity` runs after camp layouts), so `ExcludeSite` could not exclude it and it blocked the footprint check (code 2). The earlier theory that engine or ACE persistence respawns stale camp/defence compositions was wrong (ACE only stores deleted map entity IDs) and its code was removed.
- Fix: `UpdateLayouts` calls `FootprintFailure(camp, false, true)`; while that runs, `CampObstacleFilter` ignores entities whose root is within `OWN_CACHE_RADIUS` (3 m) of `INF_ForestCampRewards.GetCachePosition(...)`. Selection still checks the reward slot in full. The retry error names the failing stage and the blocker (class, prefab, root, children), and prints only when the reason changes.
- Tests: `validate_integrated_sites.py` (`test_camp_footprint_ignores_its_own_saved_cache`). Files touched: `INF_ForestCamps.c`, `Tests/validate_integrated_sites.py`.
- Status: confirmed working on the server (camp 7 places, startup completes). Still uncommitted; commit and push only when the user asks. If the camp still fails, read the `blocked by ... root ...` text in the log.

**Defence slot fallback** is already committed and pushed: when the live scan finds no `SCR_SiteSlotEntity` (dedicated server), defences use the baked `INF_DefenceSlotCache` config set on the game mode prefab (`INS_AmbientPatrolManager` `m_sINFDefenceSlotCache`); the Everon mission generates it with `Tools/generate_defence_slot_cache.py`. See the README and `ReforgerDevSkill\reforgerwiki\reforger\patterns\composition-slots.md`.