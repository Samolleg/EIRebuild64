from pathlib import Path
import hashlib,json,shutil,datetime
r=Path('work/eirebuild-local').resolve();e=r/'evidence/addon-integration';pkg=r.parent/'handoffs/eirebuild-x86-full-stats-20260916-r15';pkg.mkdir(parents=True,exist_ok=False)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest().upper()
exe=r/'out/Win32/Release/EI_Rebuild_dx11_Release_Win32.exe';pdb=exe.with_suffix('.pdb');check=json.loads((e/'r15-check.json').read_text());assert check['success'] and sha(exe)==check['exe']
sym=r/'symbols/20260916-r15';sym.mkdir(parents=True,exist_ok=True);shutil.copy2(exe,pkg/'EIRebuild.exe');shutil.copy2(pdb,sym/'EIRebuild.pdb')
files=['Game/ModStatRules.h','Game/ModPackage.h','Game/ModPackage.cpp','Game/ModLuaBridge.cpp','Game/ModRuntime.h','Game/ModRuntime.cpp','Game/RPGSystem.cpp','check-full-stats.py']
m={'revision':'R15 full native stat computation diagnostic','created':datetime.datetime.now(datetime.timezone.utc).isoformat(),'sourceRoot':str(r),'branch':'N/A copied engine snapshot','commit':'N/A copied engine snapshot','source':{f:sha(r/f) for f in files},'addonReference':'6538a636402d8bea92f74799bed237d9ca9d475e; installed D834EB76351BCDDBCB046236DEA597329B2E17B30E1C7290286A7A74B0E07DDD; R5 manifest','files':{'EIRebuild.exe':sha(pkg/'EIRebuild.exe')},'symbols':{'path':str(sym/'EIRebuild.pdb'),'sha256':sha(sym/'EIRebuild.pdb')},'validation':'build-41-full-stats + check-full-stats.py; PE32 x86','installed':False,'production':False,'toggle':'No new toggle; existing NPC armor/material/Trickster keys read from installed ai.reg','mergePush':'not authorized or performed'}
for p in [e/'r15-manifest.json',pkg/'manifest.json']:p.write_text(json.dumps(m,ensure_ascii=False,indent=2),encoding='utf-8')
report='''# R15 — full native stat computation

Parent GAME-EXE-EIREBUILD-ADDON-INTEGRATION-001 ACTIVE. Full computation now reaches the actual SRPGUnitStats::RecalcStats under ModRuntime::Session; ordinary launch still does not create a mod session.

## Source and change
Source work/eirebuild-local, branch/commit N/A copied engine snapshot. Pre-edit snapshots pre-r15/source.json. Reference installed D834EB76 unchanged from R14, tied to6538a636 via work/handoffs/world-background-real-mpr-r5-20260916/manifest.json. Unchanged Lua and current config/DB hashes recorded by r15-check.json. No addon source/main.lua/config or main-mod edits.

ModStatRules.h ports IsTwohandedWeapon/GetLeftHand directly from reference branch structure, adapting engine field names only; StrongMan is read from client user stats perk29. No unconditional left-hand fallback. Armor aggregation follows JMV_RecalculateArmor and the existing JMVNpcArmorCalculation setting, preserving the reference legacy formula for hero IDs.

CModPackage::RecalcUnitStats now executes armor -> unchanged complete RecalcUnitStatsJmv -> NPC Trickster and armor/material modifiers -> publication. The existing JMVNpcTrickster and JMVNpcArmorMaterialEffects settings retain their defaults. Candidate armor ownership uses ID+subtype, current/server/client objects, then the last authoritative material entry. Cache is package-owned and reset by Session, server-specific-manager identity, zone and backward segment transitions. Cache allocation precedes publication. No arbitrary original addresses or DLL hooks.

The full Lua callback shares a local stunSpell table with attack calculations. An exact upvalue/entry guard retains table identity and restores the old unit entry on error. Stats/common/item-regeneration remain staged. This covers the current full callback mutation contract, not arbitrary Lua callbacks or generic global state transactions.

The real SRPGUnitStats::RecalcStats callsite dispatches the computation and then refreshes native AI parameters and update masks. It requires an owned unit-specific component. AI refresh follows commit so it sees the new stats; its current native reads do not depend on the later material-only move/percept writes from the reference order. No world/save schema fields added.

## Verified
Final Build41 Release Win32/x86 PASS; exact EXE check-full-stats.py and PE32x86 PASS. Real units.udb now joins the existing items/perks DBs in the headless fixture, with database swap/restore ownership.

- Eight actual prototype/race-backed NPC cases through the real RecalcStats callsite and native CUnitServer ownership; HP/MP/movement and repeat-recalculation consistency.
- Eight hero-ID cases through the same full path: HP including difficulty/spell multiplier, positive proportional HP, circular vision and movement speeds. These fixtures keep no player owner; they do not prove a live party lifecycle.
- Eight nonempty head/body armor cases, native Wear/Remove, independently calculated weighted contributions distinguish corrected NPC versus legacy computation. Real armor remains equipped during full-stat cases.
- Six source-derived weapon cases: two-handed sword, StrongMan enabling shield, dual staves from either slot and shield priority.
- Failure injected at GetLeftHand after the full callback has written stunSpell: prior stats/common/equipment regeneration and previous stun entry preserved. Test restores original callback afterwards.
- R14 initial NPC/effect/equipment and earlier HP/MP/perk/movement helper cases still pass in the combined run. No additional gameplay claims from those fixtures.

Build38 exposed missing helper include and different engine client-global name, corrected before Build39. Tests were then extended for concrete new risks; final evidence is Build41 only, not a failed/stale binary. No window/game launch. Test-root R8 remains C73F35FC3AE3A833FC9D9C34B493944998F07488C9FA0CBFACC08A49541BA452.

## Remaining main task / confidence boundaries
Normal package resource/config/map consumers and Session lifetime are still unwired. Do not install this diagnostic as a complete mod engine. Next integrate those consumers and lifecycle while preserving the accepted render path, then test actual map loading and play.

StatInitialization identity/epoch refresh and migration consumers from the addon are not yet ported; no imitation cache is declared equivalent. Material fallback resolution and reset code is present, but cross-world/client fallback, same-world save restoration, zone changes, backwards saves and cache performance have not been exercised with a real world. Explicit lifecycle reset coverage remains required during startup/save integration. Typed userdata still bypasses the numeric-address equipment optimization; game performance unmeasured. Client party-equipment fallback and enabled complex sets remain pending.

No live owned-hero, camp UI, combat targeting, full attack/spell callbacks, save/load, zone, networking or production acceptance. Current fixture AI refresh runs on native objects with a native manager attached to the default server world, not a populated map. All-mod feature parity remains open. Preserve these limits; do not infer completion from the successful full arithmetic path.

## Handoff
Clean package work/handoffs/eirebuild-x86-full-stats-20260916-r15 contains EXE, manifest, README; matching PDB symbols/20260916-r15. Branch/commit N/A copied engine snapshot; source hashes in manifest. Requires test-game dependencies. No installation/merge/push/publication authorized or performed. Reproduce automatically with check-full-stats.py --mod <installed-mod> --game <test-game>; manual game acceptance will follow normal startup integration, not this isolated component.
'''
(e/'R15-REPORT.md').write_text(report,encoding='utf-8')
(pkg/'README.md').write_text('R15 diagnostic full stat computation. Real RecalcStats -> native armor -> unchanged full Lua -> NPC modifiers. Ordinary mod startup remains unwired; this is not the complete game/mod.\n\nBuild41 + check-full-stats.py PASS; no played-game/save/load/performance acceptance. Source/branch/commit N/A copied engine snapshot; exact hashes in manifest. Report work/eirebuild-local/evidence/addon-integration/R15-REPORT.md. Symbols separate. Not installed; no merge/push.\n',encoding='utf-8')
with (e/'FEATURE-MATRIX.md').open('a',encoding='utf-8') as f:f.write('\nR15: ADDON-092 full RecalcStats arithmetic now routes through actual engine callsite in a diagnostic Session, including native armor, complete Lua and NPC post modifiers. Left-hand reference policy ported. Native fixtures pass; normal package startup, stat-refresh/migration lifecycle and game/save/zone/network acceptance remain pending. See R15-REPORT.md.\n')
print(json.dumps({'package':str(pkg),'exe':sha(pkg/'EIRebuild.exe')},ensure_ascii=False))
