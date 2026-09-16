from pathlib import Path
import hashlib,json,shutil,datetime
r=Path('work/eirebuild-local').resolve();e=r/'evidence/addon-integration';pkg=r.parent/'handoffs/eirebuild-x86-stat-effects-20260916-r14';pkg.mkdir(parents=True,exist_ok=False)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest().upper()
exe=r/'out/Win32/Release/EI_Rebuild_dx11_Release_Win32.exe';pdb=exe.with_suffix('.pdb')
check=json.loads((e/'r14-check.json').read_text());assert sha(exe)==check['exe'] and check['success']
sym=r/'symbols/20260916-r14';sym.mkdir(parents=True,exist_ok=True);shutil.copy2(exe,pkg/'EIRebuild.exe');shutil.copy2(pdb,sym/'EIRebuild.pdb')
files=['Game/ModPackage.h','Game/ModPackage.cpp','Game/ModLuaBridge.h','Game/ModLuaBridge.cpp','Game/ModRuntime.cpp','Game/Item.h','check-stat-effects.py']
manifest={'revision':'R14 native initial/spell/custom stage diagnostic','created':datetime.datetime.now(datetime.timezone.utc).isoformat(),'sourceRoot':str(r),'branch':'N/A - copied engine snapshot','commit':'N/A - copied engine snapshot','addonReference':{'commit':'6538a636402d8bea92f74799bed237d9ca9d475e','sha256':'D834EB76351BCDDBCB046236DEA597329B2E17B30E1C7290286A7A74B0E07DDD','evidence':'work/handoffs/world-background-real-mpr-r5-20260916/manifest.json','scope':'LuaState/EIClasses/LuaScripts diff from10947135 empty'},'source':{f:sha(r/f) for f in files},'files':{'EIRebuild.exe':sha(pkg/'EIRebuild.exe')},'symbols':{'file':str(sym/'EIRebuild.pdb'),'sha256':sha(sym/'EIRebuild.pdb')},'checks':'build-37-stat-effects + check-stat-effects.py, PE32 x86; no played game','installed':False,'newToggle':'N/A - not requested or added','mergePush':'not authorized; none performed'}
for p in [e/'r14-manifest.json',pkg/'manifest.json']:p.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
report='''# R14 — initial stats, spell effects and custom effects

Parent GAME-EXE-EIREBUILD-ADDON-INTEGRATION-001 remains ACTIVE. This is a verified component of full recalculation, not full RecalcStats or normal mod startup.

## Source and scope
Engine source work/eirebuild-local; branch/commit N/A: copied engine snapshot. Pre-edit hashes and copies: pre-r14/source.json. Installed addon at preflight D834EB76 maps6538a636 via world-background-real-mpr-r5; relevant LuaState/EIClasses/LuaScripts diff from10947135 empty. Lua/config/database inputs are the installed bytes recorded in r14-check.json. No Lua changes or new settings.

CModPackage::PrepareUnitStats executes unchanged RecalcInitialJmvStats, ApplySpellEffects and ApplyCustomEffects with actual typed native objects. The argument order and nil armorRecalc/forceRecalc globals follow the current full Lua callback, not the different Ext wrapper. NPC AddPerkRecord invokes native AddPerk against staged stats (including its HP-preservation behavior), never against the live unit behind the staged copy. NPC Trickster input comes from the actual UnitPerk table.

The 53-element native effect array aggregates maximum power per spell, rejects nonfinite inputs and exposes read-only bounded Float access. Array factories reject incompatible count/type, writes and expired access. The bridge adds weapon slots, prototype name/type, armor database names and item mana-regeneration access. Item.h adds accessors for the existing field only; object/save layout unchanged.

Stats/common data and item regeneration publish only after all three Lua callbacks succeed. Item regeneration uses actual instance pointers in the callback scope; multiple references see pending values. The new stage does not provide a generic transaction for arbitrary Lua globals or every existing Item setter: its current reference chain writes only regeneration to items. No full callback is enabled on this basis.

## Verification
Build37 Release Win32/x86 PASS; check-stat-effects.py on exact resulting EXE PASS, PE32 x86 PASS. Four hero perk levels: strongest duplicate fire protection, antimagic maximum, Flow/Armory scaling, invisibility/silence, actual armor/material sums and armor/weapon/staff/quick-item regeneration. Armor is attached/detached via native Wear/Remove with a real CUnitServer component. Eight NPC names selected from installed UnitPerk/UnitStat tables: actual perk records reach staged stats; second preparation does not accumulate modifiers. Injected failure after the unchanged custom-effects callback preserves stats, common data and all three item regeneration fields. This is explicitly a negative contract test, not a game formula.

Prior HP/MP, skill/perk price, hero injury, equipment and movement-helper cases remain successful in the combined check. Files in installed mod remain byte-identical during validation. No game window launched. Fixtures are not a loaded map or gameplay/save/network evidence.

Resolved development failures: incorrect native enum spelling; fixture weapon lacked manager-assigned TypeID; a proposed fixture direct armor access was private and replaced by native Wear/Remove. Build36 failure was accidentally followed by a check of the previous EXE; that result is discarded. Only Build37 and its matching check are final evidence.

## Remaining work and exact next step
The full native NewRecalcStats wrapper additionally calls JMV_RecalculateArmor before Lua, then ApplyNpcTrickster/ApplyNpcArmorMaterialEffects and StatInitialization identity/epoch publication afterward. RecalcUnitStatsJmv also requires GetLeftHand; current reference uses client userUnitStats StrongMan perk, shield precedence and two-staff rules. Do not substitute server staged perks, return -1 unconditionally, skip armor, or treat old EIRebuild wrappers as equivalent.

Next: port these native dependencies and complete full RecalcUnitStatsJmv under one correctly owned operation, accounting for Lua stunSpell state and item mutations on failure. Then connect package resource/config/map consumers and Session to normal game lifecycle. Complete client party fallback/set bonuses, NPC armor/race database fixture, full HP/MP final redistribution, game camp/save/load/zone/network checks and equipment-cache/performance parity remain. These are necessary dependencies of the original full integration task, not a scope reduction.

## Delivery
Clean package: work/handoffs/eirebuild-x86-stat-effects-20260916-r14 (EXE, manifest, README); matching PDB symbols/20260916-r14. Requires test-game dependencies; not a standalone game distribution. Automatic reproduction: run check-stat-effects.py --mod <installed-mod> --game <test-game>. Ordinary launch still has no active package, so no manual gameplay acceptance is requested for this component.

Nothing installed; test-root R8 remains C73F35FC3AE3A833FC9D9C34B493944998F07488C9FA0CBFACC08A49541BA452. No main-mod edits, merge, push or publication. Confidence boundary: source-linked and headless-verified stage only, no production/3-of-3 or full-addon readiness claim.
'''
(e/'R14-REPORT.md').write_text(report,encoding='utf-8')
(pkg/'README.md').write_text('R14 diagnostic component: initial NPC/hero stats, spell/custom effects and transactional item regeneration. Ordinary mod startup is not enabled. Not installed.\n\nSource/branch/commit N/A copied snapshot work/eirebuild-local. Build37 and check-stat-effects.py pass; no played-game/save/load/performance acceptance. See work/eirebuild-local/evidence/addon-integration/R14-REPORT.md and manifest. PDB stored separately. No merge/push authorization.\n',encoding='utf-8')
with (e/'FEATURE-MATRIX.md').open('a',encoding='utf-8') as f:f.write('\nR14 partial advancement of ADDON-092: native initial stats/NPC perks + full spell/custom stage, typed53-effect arrays and staged item regeneration verified. Full RecalcStats/native armor/postprocessing/lifecycle and ordinary startup remain pending. See R14-REPORT.md.\n')
print(json.dumps({'package':str(pkg),'exe':sha(pkg/'EIRebuild.exe'),'pdb':str(sym/'EIRebuild.pdb')},ensure_ascii=False))
