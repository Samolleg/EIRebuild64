from pathlib import Path
import hashlib,json,shutil,datetime,struct
r=Path(__file__).resolve().parent;e=r/'evidence/addon-integration';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest().upper()
data=json.loads((e/'r23-runtime.json').read_text());assert data['engineDispatch']['mobOptionalVariadicStringRoundTrip']
exe=r/'out/Win32/Release/EI_Rebuild_dx11_Release_Win32.exe';pdb=exe.with_suffix('.pdb');b=exe.read_bytes();pe=struct.unpack_from('<I',b,60)[0];assert struct.unpack_from('<H',b,pe+4)[0]==0x14c
pkg=r.parent/'handoffs/eirebuild-x86-mob-variadic-20260916-r23';pkg.mkdir(exist_ok=False);symbols=r/'symbols/20260916-r23';symbols.mkdir(exist_ok=False)
shutil.copy2(exe,pkg/'EIRebuild.exe');shutil.copy2(pdb,symbols/'EIRebuild.pdb')
sources={str(p.relative_to(e/'pre-r23')):{'before':sha(p),'after':sha(r/p.relative_to(e/'pre-r23'))} for p in (e/'pre-r23').rglob('*') if p.is_file()}
manifest={'sourceRoot':str(r),'branchCommit':'N/A - copied engine snapshot','referenceCommit':'57b8e0c42061a0acd51eaf64ea008aa1447d94ee','referenceBranch':'feature/world-background-data-20260916','build':'Build60 Release Win32','created':datetime.datetime.now().isoformat(),'exe':sha(exe),'pdb':sha(pdb),'sources':sources,'symbols':str(symbols),'checks':['r23-runtime.json','PE32 x86'],'installed':False,'interactiveTest':False,'mergePushAuthorized':False}
for p in [pkg/'manifest.json',e/'r23-manifest.json']:p.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
report='''# R23 — optional/variadic MOB arguments and string results

Parent GAME-EXE-EIREBUILD-ADDON-INTEGRATION-001 ACTIVE. User requires full source migration before gameplay testing. Engine copied snapshot work/eirebuild-local, branch/commit N/A. Source reference57b8e0c feature/world-background-data-20260916; exact R22 original225-token/Release93-handler evidence reused unchanged. No installed file, game launch, merge or push.

## Source changes
Native LoadFunction now recognizes the reference `?` optional-tail and `+` repeat-previous-type grammar. On closing a call it trims omitted optional slots using actual consumed counts; repeated arguments grow the proper typed native array. Required argument minima stay enforced. Native function/variable evaluation and stored argument metadata remain in use. No opcode or binary command layout changed.

The shared argument validator expands a format against actual float/string/object/group counts, rejects missing/excess/type-mismatched arguments, and supplies a concrete format to text export. The registry now advertises string-returning callbacks as functions, and CString CalcFunction dispatches to the typed native handler.

10 more source-backed commands implemented: Add, Mul, And, Or, Max, Min, FloatToStr, StrCat, StrHas, GetUnitExp. Total29 implemented of93 registered handler identities;64 implementations remain. GetUnitExp supports the omitted/default max argument and explicit max/spent/penalty/real using actual engine stat fields. Reduction order, integer-based truth conversion, FloatToStr default/clamped precision and its original trailing-zero trimming are retained. No invented formula or changed default. Optional/variadic tokens occupy the exact same IDs.

Concrete defect found by re-execution of exported scripts: StoreTextFunction removes two assignment metadata floats but left local/global/function float-reference indices at their original positions. A numeric function assignment therefore exported its placeholder zero. Fixed the indices in the export-only command copy after removing metadata; runtime command execution and serialized state are not changed. This is a proved engine exporter correction required for the migration test, not a reconstructed addon algorithm.

## Validation
Final Build60 Release Win32 PASS, PE32 x86. check-variadic-r23.py passes through actual native CScript LoadText, Segment, StoreText and reparse/Segment. Cases include nested Add/Round, variadic numeric reductions, omitted/explicit GetUnitExp with empty unit, nested variadic StrCat/optional FloatToStr, explicit precision0, and equality of original/reparsed numeric and string results. Invalid minimum/excess arguments rejected for Add, StrCat and FloatToStr. Prior fixed-format/parser/native-stat/regeneration/queue regression PASS.

Build58 compiled but execution comparison caught exporter loss. Build59 added exact failing variable/value/export text and localized the existing index defect. Build60 corrects the exporter and passes. No game/UI or full historical re-audit. The root-cause failure and correction are described here to avoid another investigation from scratch.

## Handoff and remaining evidence
Clean package work/handoffs/eirebuild-x86-mob-variadic-20260916-r23 contains EXE, manifest and README. Matching PDB separate symbols/20260916-r23. Existing test R8, original EXE/Starter, Lua/maps/config preserved. Requires game dependencies; not a complete game distributable. Source preimages and hashes in pre-r23/manifest. Merge/push not authorized.

Only headless script execution is confirmed. Populated-unit GetUnitExp, generic variadic object/group argument paths, very large calls, actual saved binary scripts, full non-mod exporter regression and all gameplay/save/network behavior remain untested. StrHas is source-integrated but not separately exercised by the new script fixture. No full-addon or save compatibility claim. Original FloatToStr precision0 trailing-zero behavior is retained rather than silently corrected. The unchanged DistancePointPoint argument mismatch remains pending.

Next coherent block: remaining object/string/unit commands and branch lowering/branch dispatch using exact registered IDs, then binary compiled-script round-trip; continue NPC equipment and combat/AI migration. Source transfer remains incomplete and R17 startup/rendering issues are deferred, not resolved. After transfer, run actual MOB/save/load/zone tests. Do not launch those now.
'''
(e/'R23-REPORT.md').write_text(report,encoding='utf-8');(pkg/'README.md').write_text('R23: optional/variadic MOB args, string results,10 additional handlers (29/93 total), numeric assignment text-export fix. Build60 and native execution/reparse checks PASS. Not installed; gameplay deferred. Source branch/commit N/A copied engine. See manifest and work/eirebuild-local/evidence/addon-integration/R23-REPORT.md. Requires game dependencies;64 handlers and binary save parity remain pending. Symbols separate20260916-r23. No merge/push authorized.\n',encoding='utf-8')
p=e/'FEATURE-MATRIX.md';b=p.read_bytes();p.write_bytes(b+b'\r\nR23: optional/variadic parser and string return dispatch;10 more handlers (29/93). Numeric assignment export-index defect fixed. Build60/native parse-execute-export-reparse execution PASS; binary saved scripts and generic object/group paths untested. See R23-REPORT.md.\r\n')
print(manifest['exe']);print(pkg)
