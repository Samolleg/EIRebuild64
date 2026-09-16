from pathlib import Path
import json,hashlib,shutil,datetime,struct
r=Path(__file__).resolve().parent;e=r/'evidence/addon-integration'
exe=r/'out/Win32/Release/EI_Rebuild_dx11_Release_Win32.exe';pdb=exe.with_suffix('.pdb')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest().upper()
data=json.loads((e/'r20-runtime.json').read_text());assert data['engineDispatch']['regenerationCadenceHpMpRollback']
b=exe.read_bytes();pe=struct.unpack_from('<I',b,60)[0];assert struct.unpack_from('<H',b,pe+4)[0]==0x14c
pkg=r.parent/'handoffs/eirebuild-x86-regeneration-20260916-r20';pkg.mkdir(exist_ok=False)
symbols=r/'symbols/20260916-r20';symbols.mkdir(exist_ok=False)
shutil.copy2(exe,pkg/'EIRebuild.exe');shutil.copy2(pdb,symbols/'EIRebuild.pdb')
sources={str(p.relative_to(e/'pre-r20')):{'before':sha(p),'after':sha(r/p.relative_to(e/'pre-r20'))} for p in (e/'pre-r20').rglob('*') if p.is_file()}
sources['Game/ModStatInitialization.h']={'before':None,'after':sha(r/'Game/ModStatInitialization.h')}
mod=Path(r'C:\GOG Games\Evil Islands\Starter\MODS\JamaisVu135')
inputs={str(mod/p):sha(mod/p) for p in ['addon.dll','main.lua','lua/default.lua','lua/tables.lua','config/ai.reg']}
assert inputs[str(mod/'addon.dll')]=='522EEF9E5D61AC253B1F8DE040C6AC9E0AEB48470DE9ECF3D1FF63CD6F8CE58A'
manifest={'sourceRoot':str(r),'sourceBranch':'N/A - copied engine snapshot','sourceCommit':'N/A - copied engine snapshot','referenceCommit':'57b8e0c42061a0acd51eaf64ea008aa1447d94ee','referenceBranch':'feature/world-background-data-20260916','build':'Build53 Release Win32','created':datetime.datetime.now().isoformat(),'exe':sha(exe),'pdb':sha(pdb),'symbols':str(symbols),'sources':sources,'inputs':inputs,'checks':['r20-runtime.json','PE32 x86'],'installed':False,'interactiveTest':False,'mergePushAuthorized':False}
for p in [pkg/'manifest.json',e/'r20-manifest.json']:p.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
report='''# R20 — regeneration and completed-stat proof

Parent GAME-EXE-EIREBUILD-ADDON-INTEGRATION-001 ACTIVE. Full addon migration remains the objective. Per owner, finish source migration before gameplay tests. No interactive launch, installation, merge or push.

## Sources and implementation
Engine work/eirebuild-local is a copied snapshot, branch/commit N/A. Verified installed addon 522EEF9E corresponds to source57b8e0c, feature/world-background-data-20260916. Reference sources: RegenerationSchedule.h, StatInitialization.h, RegenerationInvalidation.h, LuaState.cpp1299+, Patches/LuaScripts.cpp7170+. Installed main.lua1701–1930 and data unchanged. Source preimages preserved in pre-r20; exact before/after hashes in manifest. No new runtime/config toggle.

- Native SRPGCommonStats::Regenerate now dispatches into the package. Dead units keep their counter; living units use the serialized 15-segment schedule, including signed-counter boundary behavior through unsigned increment. Due calls run unchanged RegenerateHpAndMpJmvDue. Native player0 danger state and server aggression supply the source-defined safe-state argument.
- Typed heal and regenMp bindings use the engine's real hit-location/MP methods. HP, common stats and item mana publish only after callback success and finite-value checks; failure retains the original counter/resources. This transactional failure behavior is an explicit engine adaptation, not claimed byte-for-byte exception parity with the addon.
- Item spell exposes its actual manaCost as a read-only value snapshot. Item MP writes/read-after-write are staged in callback scope. Existing MagicBlood Lua therefore transfers available unit MP to item spells, including equipped armor. This is not a complete spell-object bridge.
- Package-owned completed-stat ledger records successful full RecalcUnitStats; attempted recalculation invalidates the old record. Identity includes unit/specific/common/stats pointers and subtype, with bounded 8192 entries. Ready comparison ignores only current MP and regeneration counter, as in the addon. Session reset, manager/zone changes and backward segment reset clear the ledger alongside the existing armor cache.
- Regeneration captures complete common/unit stats, effect fields, current weapon and weapon/armor/quick-item identity/durability/subtype. MP-only dispatch requires unchanged transient dependencies and a ready completed-stat proof in the same epoch. It publishes both native stats masks and skips full recalculation. Missing/changed proof retains the full native caller. No save-format field added.

## Checks
Final Build53 Release Win32 PASS; PE32 x86. One final headless --inspect-addon pass executes unchanged installed Lua, full native-stat/equipment fixtures and the new regression cases: 14 waiting ticks and due tick; dead counter preservation; -1/14/15/INT_MAX boundaries; HP restoration; MP restoration; MP-only skip through the real native callsite; completed proof identity/change/reset cases; transient stat-change rejection; actual item-spell MagicBlood transfer (12 available MP into a 20-cost spell); late callback error after resource/equipment writes with rollback. Shared typed-item tests now explicitly check no MP publication before CommitItems.

Build50/core check passed before proof integration. Build51 compiled; its MP-only test failed because the fixture had HP100/max105, so HP regeneration correctly required full recalc. The fixture was made fully healed and given a successful recalc; Build52 passed. Build53 adds the previously uncovered nonempty spell-charge case and passes. The implementation was not weakened to pass a wounded-unit MP-only test. No repeated historical manifest or broad runtime scans.

## Delivery and evidence gaps
Clean package work/handoffs/eirebuild-x86-regeneration-20260916-r20: EXE, manifest and README. Matching PDB is separate at symbols/20260916-r20. Existing installed test R8 and original mod/Starter preserved. Requires test-game runtime dependencies; not a complete distributable game.

ADDON-093 remains partial: NPC mana-caster/active-ability classification and per-segment death-fear, crippled-speed, cast-trace, murder-awareness and combat-consumable observers are still unported. Do not interpret this as the complete original hook. The source-ready ledger is integrated, but original ProcessInitialHeroStats proactive post-load scan and NPC stat-refresh/migration queues are still pending. Missing proof safely keeps full recalc; that does not establish complete post-load parity. Scalar MP/HP/source tests do not establish real owned-player safe-state, save/load, zone rewind, connected-network mask delivery, actual spell-equipped weapon gameplay or measured FPS. Config legacy Lua-disabled modes and broad spell/client interfaces remain outside this block. R17 startup/resource/UI defects remain deferred. No production/full-game claim.

Next coherent block: integrate ProcessInitialHeroStats with authoritative world-ready/segment lifecycle and NPC stat-refresh queue, then active ability/caster dependencies and remaining per-segment observers; continue combat/MOB/native callback migration. After all transfer is finished, manually test wounded/full/dead units, movement/combat/caster recovery, equipment charging, load/earlier-save/zone changes and client publication. Do not run these now.
'''
(e/'R20-REPORT.md').write_text(report,encoding='utf-8')
(pkg/'README.md').write_text('R20: native regeneration, typed heal/item charge and completed-stat proof. Build53 + headless checks PASS. Not installed; gameplay deferred until source migration complete. Full ADDON-093 and world-ready refresh remain incomplete. Requires test-game runtime dependencies. Branch/commit N/A copied engine; see manifest and work/eirebuild-local/evidence/addon-integration/R20-REPORT.md. PDB separate symbols/20260916-r20. No merge/push authorized.\n',encoding='utf-8')
p=e/'FEATURE-MATRIX.md';b=p.read_bytes();lines=b.splitlines(keepends=True)
for i,line in enumerate(lines):
 if line.startswith(b'| ADDON-093 |'):lines[i]=line.replace(b'pending-semantic-comparison',b'partial-R20: regeneration/proof integrated; AI observers and caster classification pending')
p.write_bytes(b''.join(lines)+b'\r\nR20: native HP/MP regeneration, item charge and completed-stat proof integrated; source/headless verified only. Post-load hero scan/NPC refresh and per-segment AI observers remain. See R20-REPORT.md.\r\n')
print(manifest['exe']);print(pkg)
