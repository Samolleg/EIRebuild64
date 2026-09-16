from pathlib import Path
import json,hashlib,shutil,datetime,struct
r=Path(__file__).resolve().parent;e=r/'evidence/addon-integration'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest().upper()
exe=r/'out/Win32/Release/EI_Rebuild_dx11_Release_Win32.exe';pdb=exe.with_suffix('.pdb')
data=json.loads((e/'r21-runtime.json').read_text());assert data['engineDispatch']['refreshQueueBatchDedupReset']
b=exe.read_bytes();pe=struct.unpack_from('<I',b,60)[0];assert struct.unpack_from('<H',b,pe+4)[0]==0x14c
pkg=r.parent/'handoffs/eirebuild-x86-stat-refresh-20260916-r21';pkg.mkdir(exist_ok=False)
symbols=r/'symbols/20260916-r21';symbols.mkdir(exist_ok=False)
shutil.copy2(exe,pkg/'EIRebuild.exe');shutil.copy2(pdb,symbols/'EIRebuild.pdb')
sources={str(p.relative_to(e/'pre-r21')):{'before':sha(p),'after':sha(r/p.relative_to(e/'pre-r21'))} for p in (e/'pre-r21').rglob('*') if p.is_file()}
sources['Game/ModNpcRefresh.h']={'before':None,'after':sha(r/'Game/ModNpcRefresh.h')}
manifest={'sourceRoot':str(r),'branchCommit':'N/A - copied EIRebuild snapshot','referenceCommit':'57b8e0c42061a0acd51eaf64ea008aa1447d94ee','referenceBranch':'feature/world-background-data-20260916','build':'Build55 Release Win32','created':datetime.datetime.now().isoformat(),'exe':sha(exe),'pdb':sha(pdb),'sources':sources,'symbols':str(symbols),'checks':['r21-runtime.json','PE32 x86'],'installed':False,'interactiveTest':False,'mergePushAuthorized':False}
for p in [pkg/'manifest.json',e/'r21-manifest.json']:p.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
report='''# R21 — world stat initialization and NPC refresh queue

Parent GAME-EXE-EIREBUILD-ADDON-INTEGRATION-001 ACTIVE. Full addon-to-EXE transfer remains the objective; no game testing until source transfer finishes, per owner. No installation or publication authorized/performed. Copied engine branch/commit N/A; source reference feature/world-background-data-20260916 at57b8e0c, installed522EEF9E provenance reused from R20. Reference files are read-only; source preimages and exact hashes accompany the manifest.

## Source integration
- Package now owns the NPC refresh queue: 12 IDs processed per active-object tick, insertion order, duplicate suppression retained after a batch completes and cleared for a new world. Invalid or removed objects are re-resolved at processing time through the authoritative native world manager. Like the reference, owner/dead eligibility is checked at enqueue; processing rechecks authoritative validity, not owner/dead state.
- Initial hero scan runs once after world readiness. It considers referenced, valid, authoritative, owned living units with finite HP/MP and invokes real CUnitServer::RecalcRPGStats(false) when completed-stat proof is absent/changed. This retains native attack-timer adjustment and update/export notifications.
- TickStatRefresh executes at the beginning of ActiveObjectsSegment after scratch-array initialization, matching the reference hook's enclosing function and before ordinary unit segments. Exact instruction-level equivalence of the historical hook offset was not established; the original address is not reused by this engine.
- Successful LoadSave resets package transient stats/cache/queue state and immediately initializes eligible heroes. Successful map-load completion resets state; the next active-object tick scans the new world. Failed LoadSave return does not invoke the success callback. Cache identity now uses actual szMobFileName instead of player0 party-zone inference. Backward segment, manager/MOB change and Session teardown also clear proof/queue state.
- Existing JMVNpcStatRefresh registry setting is read with original default true. When enabled, native CreateSpell suppresses only case-insensitive exact invisibility{d05;d05}, as in the reference, preventing the former temporary-spell refresh workaround. No new toggle, Lua/data edit or serialized stat field.

## Validation and delivery
Final Build55 Release Win32 PASS; PE32 x86. check-refresh-r21.py executes the actual pre-window inspector: 29 IDs drained12/12/5, FIFO, duplicate rejection before/after completion, pending-queue reset and rescanning flags, exact/case-insensitive legacy formula filter with nonmatching/null preservation. Native package successful-load/tick calls tolerate an unloaded world without touching nonexistent managers. Existing real-Lua/native-stat/regeneration/item-charge/proof/rollback checks PASS. Build54 passed before moving the tick from WorldSegment to the correct ActiveObjectsSegment phase; final Build55 includes that source change. No further repeated historical checks.

Clean handoff work/handoffs/eirebuild-x86-stat-refresh-20260916-r21 contains EXE, manifest and README. Matching PDB separate symbols/20260916-r21. Existing test-game R8, original game.exe, Starter and mod unchanged. Requires game dependencies; not a full game distributable. Merge/push not authorized/performed.

## Remaining scope and confidence audit
Hero scanning, actual populated-world NPC refresh and LoadSave callsites are source-integrated but not executed on a loaded game world. The headless queue test exercises the real queue algorithm, not a simulation of the entire world load. Real owner/manager identity, save/load/earlier-save/zone traversal, client export and FPS remain deferred. Exact enclosing phase is source-supported; historical hook instruction-position parity remains unverified.

RefreshUnitStats MOB command parser/serialized dispatch remains unported; no arbitrary opcode was introduced. The automatic queue is complete as a source block, not the entire NPC refresh feature. NPC equipment database migration, registered-loot and tactical/transient AI success-load observers are separate pending parts of the original callback. They were not replaced with empty implementations. Existing Lua-disabled/legacy configuration parity remains part of the full migration backlog. R17 startup/rendering defects and all unported combat/MOB/client/network work remain open.

Next: migrate the custom MOB command registry using the addon's exact token numbering/argument contract, beginning with RefreshUnitStats and its disabled-mode legacy cast; then integrate remaining NPC migration and per-segment AI observers. After source transfer, manual checks: load current/older saves and switch zones; confirm owned heroes initialize once, NPC updates drain without spell flicker, deleted queued objects are skipped, and client stats/equipment remain correct. Do not run now.
'''
(e/'R21-REPORT.md').write_text(report,encoding='utf-8')
(pkg/'README.md').write_text('R21: initial hero stats and automatic NPC refresh queue. Build55 and headless checks PASS. Not installed; gameplay deferred. Copied engine branch/commit N/A. Requires game dependencies. Full MOB RefreshUnitStats and equipment/AI migration remain pending. See manifest and work/eirebuild-local/evidence/addon-integration/R21-REPORT.md. Matching PDB separate symbols/20260916-r21. No merge/push authorized.\n',encoding='utf-8')
p=e/'FEATURE-MATRIX.md';b=p.read_bytes();p.write_bytes(b+b'\r\nR21: source-integrated initial hero world-ready/save-load scan, automatic NPC refresh queue and exact legacy-spell suppression. Build55/headless queue+shared regression PASS; populated-world tests deferred. MOB RefreshUnitStats opcode/handler and equipment/AI migration remain pending. See R21-REPORT.md.\r\n')
print(manifest['exe']);print(pkg)
