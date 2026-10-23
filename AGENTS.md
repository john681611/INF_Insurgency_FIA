# Agent notes

This folder holds several Arma Reforger addons. Some are separate Git repos nested inside it (each has its own `AGENTS.md`).

The `Insurgency*` addons (`Insurgency Advanced`, `Insurgency Advanced FIA`, `Insurgency FIA`, `Insurgency FIA Ext Veh`, `Insurgency FIA Guerrilla Militia`) are tracked together by the Git repo rooted at this folder, hosted at https://github.com/john681611/INF_Insurgency_FIA (branch `main`). The root `.gitignore` ignores everything else.

- When publishing one of those addons or finishing a set of changes, suggest committing and pushing the repo changes to GitHub.
- Check `git status` first so uncommitted or unpushed work isn't missed.

## Reforger development knowledge

Before writing or changing Arma Reforger code (Enfusion scripts, prefabs, configs), read the wiki skill at `C:\Users\john6\Documents\My Games\ArmaReforgerWorkbench\addons\ReforgerDevSkill` (start with `reforgerwiki\SKILL.md` and `README.md`). It holds Enfusion API overviews, scripting patterns and gotchas. Add new lessons learned to it.