from pathlib import Path
import json,hashlib,shutil,datetime
r=Path(__file__).resolve().parent;e=r/'evidence/addon-integration';pkg=r.parent/'handoffs/eirebuild-x86-economy-20260916-r19';pkg.mkdir(exist_ok=False)
symbols=r/'symbols/20260916-r19';symbols.mkdir(exist_ok=False)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest().upper()
exe=r/'out/Win32/Release/EI_Rebuild_dx11_Release_Win32.exe';pdb=exe.with_suffix('.pdb')
check=json.loads((e/'r19-check.json').read_text());assert check['success'] and sha(exe)==check['exe']
shutil.copy2(exe,pkg/'EIRebuild.exe');shutil.copy2(pdb,symbols/'EIRebuild.pdb')
sources={str(p.relative_to(e/'pre-r19')):{'before':sha(p),'after':sha(r/p.relative_to(e/'pre-r19'))} for p in (e/'pre-r19').rglob('*') if p.is_file()}
manifest={'sourceRoot':str(r),'sourceCommit':'N/A - copied engine snapshot','referenceCommit':'57b8e0c42061a0acd51eaf64ea008aa1447d94ee','referenceAddon':'522EEF9E5D61AC253B1F8DE040C6AC9E0AEB48470DE9ECF3D1FF63CD6F8CE58A','build':'Build49 Release Win32','created':datetime.datetime.now().isoformat(),'exe':sha(exe),'pdb':sha(pdb),'symbols':str(symbols),'sources':sources,'inputs':check['inputs'],'checks':['r19-check.json','r19-shared-bridge.json'],'installed':False,'interactiveTest':False,'mergePushAuthorized':False}
(pkg/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8');(e/'r19-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
report='''# R19 — economy callbacks integrated into the engine

Parent GAME-EXE-EIREBUILD-ADDON-INTEGRATION-001 ACTIVE. Owner explicitly changed sequence: complete source migration first, interactive/game tests afterward. R17/R18 visual/startup issues are deferred, not closed. No game/UI launch or installation this block; only pre-window diagnostic commands.

## Sources and change
Copied engine work/eirebuild-local, Git branch/commit N/A. Addon reference read-only source feature/world-background-data-20260916 at clean 57b8e0c matches installed addon 522EEF9E via existing R6 manifest. Native reference LuaScripts.cpp 8230-8435 and LuaState.cpp; actual installed main/default/tables remain unchanged. Source byte snapshots/hashes in pre-r19 and r19-manifest.json. No new runtime toggle.

- ADDON-096: both server and client GetExpMultiplier and GetManaBonusMultiplier now invoke the corresponding unchanged Lua callbacks. Existing GainExperience/AddExperience penalty behavior and client experience messages / inventory intelligence text use those methods; therefore they consume the same migrated formulas. No stat-layout/save-format change. ClientStats exposes a read-only typed intelligence value rather than an original-EXE address. This is not a full client stats bridge.
- ADDON-097: SShopSet::NewGame calls the native package after the original shop/assortment creation. Nine coefficients per shop pass through a bounded mutable float array. Four LMP and five campaign shops are supported; slot ten and untouched campaign fifth-shop coefficients remain intact. All writes commit together after Lua success; failed callback retains original values. Existing save-loaded shop coefficients are not automatically recalculated or migrated.
- ADDON-098: server shared experience uses DistributeLmpExp(experience, actual player-manager count). The computed per-unit share flows through existing actual award, party-stat sync, and client notification paths. Original weighted distribution remains when no mod package is bound. Connected network behavior remains untested.
- Fixed package initialization order discovered by the shop test: Load previously required table_indexes -> tables before main required default -> cached tables. Default silently replaced table-defined functions. Load now requires default before table_indexes, preserving main.lua's default-then-tables override order. No installed Lua changed.

## Checks
Final Build49 Release Win32 PASS. Initial Build47 failed on the Windows max macro; corrected. Build48 compiled but actual shop coefficients failed because of require order; corrected at the loader, not by hardcoding prices. Final check-economy-r19.py PASS: 6 intelligence cases including native server/client XP and mana parity, native penalty repayment, 12 LMP shares, invalid zero player count rejection, both shop modes, all 36 assignments independently parsed from installed tables.lua, tenth-slot and untouched-shop preservation, failed-callback rollback and expired array rejection. PE32 x86 and exact input/artifact identity recorded.

check-regression-r19.py PASS because shared constructor/array/load-order code changed: unchanged installed full main.lua, typed bridge and full-stat native fixture dispatch. Uses r19-shared-bridge.json, preserving earlier report filenames. No historical manifests rechecked. No gameplay/performance/production claim.

## Delivery and remaining scope
Clean folder work/handoffs/eirebuild-x86-economy-20260916-r19 contains EXE, manifest, README; matching PDB stored separately symbols/20260916-r19. No installation, merge, push or publication. Existing installed test R8 and original game/Starter/mod preserved. This EXE depends on test-game runtime resources and is not a standalone full-game package.

After the remaining migration, planned manual checks: earn experience with penalty and compare server/client displayed awards; compare mana costs; create campaign and network shops and compare all prices; test network experience shares. Do not launch these now. Full StatInitialization/regeneration lifecycle, combat/active abilities/MOB commands, save/zone/migration, remaining UI/audio/network and R17 visual issues remain open. No complete-addon or compatible-save claim. Scalar mana memoization from addon is not yet migrated; formulas are invoked directly. Native shop construction and connected-world distribution callsites are source-integrated; headless checks exercise coefficient application / scalar methods, not a populated world or assortment generation. These are the material evidence gaps.
'''
(e/'R19-REPORT.md').write_text(report,encoding='utf-8')
(pkg/'README.md').write_text('R19: source migration of XP/mana multipliers, shop prices and LMP experience. Build49 + two headless checks PASS. Not installed. No interactive testing until full source migration, per owner. Requires test-game dependencies. Branch/commit N/A copied engine snapshot; exact sources/artifact in manifest. PDB separately work/eirebuild-local/symbols/20260916-r19. See work/eirebuild-local/evidence/addon-integration/R19-REPORT.md for limits. No merge/push authorized.\n',encoding='utf-8')
p=e/'FEATURE-MATRIX.md';b=p.read_bytes()
for number in [96,97,98]:
 lines=b.splitlines(keepends=True)
 for i,line in enumerate(lines):
  if line.startswith(f'| ADDON-{number:03} |'.encode()):lines[i]=line.replace(b'pending-semantic-comparison',b'source-integrated-headless-R19; gameplay-deferred')
 b=b''.join(lines)
p.write_bytes(b+b'\r\nR19: ADDON-096/097/098 source integrated, Build49 and economy/shared-bridge headless checks PASS. Corrected default/tables initialization order. Owner requires completing migration before interactive tests. Full gameplay/save/network acceptance and mana memoization remain open. See R19-REPORT.md.\r\n')
print(manifest['exe']);print(pkg)
