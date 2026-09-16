from pathlib import Path
import hashlib,json,shutil,datetime,struct
r=Path(__file__).resolve().parent;e=r/'evidence/addon-integration';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest().upper()
data=json.loads((e/'r24-runtime.json').read_text());assert data['engineDispatch']['mobNestedBranchesAndSuspendedBinaryRoundTrip']
exe=r/'out/Win32/Release/EI_Rebuild_dx11_Release_Win32.exe';pdb=exe.with_suffix('.pdb');b=exe.read_bytes();pe=struct.unpack_from('<I',b,60)[0];assert struct.unpack_from('<H',b,pe+4)[0]==0x14c
ref=r.parent/'src_addon_world_background_data_20260916/src/addon/MobIfElse.h';assert sha(ref)==sha(r/'Game/ModMobIfElse.h')
pkg=r.parent/'handoffs/eirebuild-x86-mob-branches-20260916-r24';pkg.mkdir(exist_ok=False);symbols=r/'symbols/20260916-r24';symbols.mkdir(exist_ok=False)
shutil.copy2(exe,pkg/'EIRebuild.exe');shutil.copy2(pdb,symbols/'EIRebuild.pdb')
sources={str(p.relative_to(e/'pre-r24')):{'before':sha(p),'after':sha(r/p.relative_to(e/'pre-r24'))} for p in (e/'pre-r24').rglob('*') if p.is_file()};sources['Game/ModMobIfElse.h']={'before':None,'after':sha(ref),'exactReference':str(ref)}
manifest={'sourceRoot':str(r),'branchCommit':'N/A - copied engine snapshot','referenceCommit':'57b8e0c42061a0acd51eaf64ea008aa1447d94ee','referenceBranch':'feature/world-background-data-20260916','build':'Build61 Release Win32','created':datetime.datetime.now().isoformat(),'exe':sha(exe),'pdb':sha(pdb),'sources':sources,'symbols':str(symbols),'checks':['r24-runtime.json','PE32 x86','byte-identical MobIfElse source'],'installed':False,'interactiveTest':False,'mergePushAuthorized':False}
for p in [pkg/'manifest.json',e/'r24-manifest.json']:p.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
report='''# R24 — MOB if/then/else and suspended-script binary round-trip

Parent GAME-EXE-EIREBUILD-ADDON-INTEGRATION-001 ACTIVE. Finish full migration before gameplay per owner. Copied engine work/eirebuild-local, branch/commit N/A. Reference57b8e0c feature/world-background-data-20260916; unchanged R22 token/provenance evidence reused. No installation, interactive launch, merge or push.

## Implementation
Game/ModMobIfElse.h is byte-identical to the verified addon's MobIfElse.h. Native LoadText invokes this lowering before preprocessing in mod mode. Existing nested/else logic, 64-depth limit, quote/comment treatment, condition-list short-circuit construction and line preservation come from the source. Lowering errors become native parser errors. No replacement scripting VM or invented conditions.

Enabled the existing391 JmvBranchUnless and392 JmvBranchSkip registry entries. Native action dispatch receives the real current-command index and current-thread size; valid jumps follow the reference bounds/integral/finite checks and rely on the original caller's one subsequent increment. Thread lifetime, suspension, local variables and serialization remain owned by the engine. Invalid branch state becomes an explicit exception instead of the addon's logged FunctionError; this is a disclosed error-reporting adaptation, not a new toggle or normal-path semantic change.

Ported reference keyword-boundary protection and unsigned-byte ctype arguments in the native lexer. Longer keyword candidates are rejected before indexing the boundary byte. Mod-selected boundary checking prevents names with keyword prefixes from being split; unsigned ctype casts also protect CP1251 bytes. No wholesale source encoding/format normalization.

31 of93 registry handlers now implemented;62 remain. The substantial addition is the complete source lowering/native control-flow path, not just two command names. No command ID or save-layout field changed.

## Validation
Build61 Release Win32 PASS, one headless inspector pass PASS, PE32 x86. Actual native parser/Segment executes false/else, nested true branch and a later variable-dependent true branch, producing14. Direct native branch helper checks branch-to-end and rejects negative/fractional/out-of-range offsets. Unmatched else is rejected by LoadText. Existing optional/variadic, string/numeric export-reparse, stat/regeneration/queue checks remain clean.

Binary suspension fixture uses the real theWorldServer.script owner required by the native loader. It compiles a branch with WaitSegment(2), executes to suspension, calls StoreScript into CMemorySave, clears the script, calls LoadScript, consumes the complete byte buffer and resumes on the next segment. Final value6 confirms the restored program counter stayed inside the selected branch and skipped else. This is an in-memory engine script save round-trip using the default current schema, not a full game save nor an original-addon save import. Test data contain no loaded map, live NPC or external save. No UI/game launched.

## Delivery / confidence gaps
Clean folder work/handoffs/eirebuild-x86-mob-branches-20260916-r24 contains EXE, manifest, README; matching PDB separately symbols/20260916-r24. Preimages and exact hashes preserved. Existing installed test R8/game/Starter/mod unchanged. Requires game runtime dependencies, not a standalone game distribution. Merge/push not authorized.

Remaining evidence: Script rule lifetime/else and KillScript, branches inside native loop bodies, depth-limit boundary, CP1251 content and long keyword-prefix cases are source-backed but not separately executed by this fixture. Cross-version original-addon saves, populated-world objects/groups and other script owners remain untested. Native loaders still explicitly resolve thread links through theWorldServer.script; no claim of generalized alternate-owner save support.62 handlers, NPC equipment migration and remaining combat/AI/client/loot/audio/UI/network transfer remain; R17 startup/rendering defects are still deferred. DistancePointPoint contract mismatch remains open. No full-game/production claim.

Next coherent block: remaining object/unit/string handlers using real native ownership and exact registry IDs; then NPC equipment/combat/AI migration. After all source transfer, manually run actual MOB if/else, suspended save/load, nested loops and transitions on the test game. Do not launch now.
'''
(e/'R24-REPORT.md').write_text(report,encoding='utf-8');(pkg/'README.md').write_text('R24: exact addon if/else lowering, native391/392 branch commands, lexer fixes.31/93 handlers integrated. Build61 + native nested branch and suspended binary script round-trip PASS. No install/gameplay. Source branch/commit N/A copied engine. Full original-save/game parity unproved;62 handlers remain. See manifest and work/eirebuild-local/evidence/addon-integration/R24-REPORT.md. Requires game dependencies, PDB separate20260916-r24. No merge/push authorized.\n',encoding='utf-8')
p=e/'FEATURE-MATRIX.md';b=p.read_bytes();p.write_bytes(b+b'\r\nR24: byte-identical MobIfElse source lowering, native391/392 branch dispatch and lexer safety integrated (31/93 handlers). Build61/native nested branches and suspended engine-world-script binary round-trip PASS. Original-addon/fullgame saves untested. See R24-REPORT.md.\r\n')
print(manifest['exe']);print(pkg)
