from pathlib import Path
import hashlib,json,shutil,datetime,struct,re
r=Path(__file__).resolve().parent;e=r/'evidence/addon-integration';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest().upper()
data=json.loads((e/'r25-runtime.json').read_text());assert data['engineDispatch']['mobUnitAndStringQueries']
assert len(re.findall(r',true}',(r/'Game/ModMobCommands.h').read_text()))==43
exe=r/'out/Win32/Release/EI_Rebuild_dx11_Release_Win32.exe';pdb=exe.with_suffix('.pdb');b=exe.read_bytes();pe=struct.unpack_from('<I',b,60)[0];assert struct.unpack_from('<H',b,pe+4)[0]==0x14c
pkg=r.parent/'handoffs/eirebuild-x86-mob-unit-commands-20260916-r25';pkg.mkdir(exist_ok=False);symbols=r/'symbols/20260916-r25';symbols.mkdir(exist_ok=False)
shutil.copy2(exe,pkg/'EIRebuild.exe');shutil.copy2(pdb,symbols/'EIRebuild.pdb')
sources={str(p.relative_to(e/'pre-r25')):{'before':sha(p),'after':sha(r/p.relative_to(e/'pre-r25'))} for p in (e/'pre-r25').rglob('*') if p.is_file()}
manifest={'sourceRoot':str(r),'branchCommit':'N/A - copied engine snapshot','referenceCommit':'57b8e0c42061a0acd51eaf64ea008aa1447d94ee','referenceBranch':'feature/world-background-data-20260916','build':'Build62 Release Win32','created':datetime.datetime.now().isoformat(),'exe':sha(exe),'pdb':sha(pdb),'sources':sources,'symbols':str(symbols),'checks':['r25-runtime.json','PE32 x86'],'installed':False,'interactiveTest':False,'mergePushAuthorized':False}
assert sha(pkg/'EIRebuild.exe')==manifest['exe']
for p in [pkg/'manifest.json',e/'r25-manifest.json']:p.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
report='''# R25 — native unit and string MOB commands

Parent GAME-EXE-EIREBUILD-ADDON-INTEGRATION-001 ACTIVE. Owner requires complete source migration before gameplay. Source work/eirebuild-local; branch/commit N/A, copied engine snapshot. Read-only addon reference57b8e0c42061a0acd51eaf64ea008aa1447d94ee, feature/world-background-data-20260916; unchanged R22 token and installed-source provenance reused. No installation, UI/game launch, merge or push. New toggle N/A: not requested or added.

## Changed source
Game/ModMobCommands.h enables12 exact registry entries: GetSkill, GetSkill2, GetSkill3, GetPerk, GetPerk2, GetPrototype, GetRace, GetUnitWeapon, GetUnitArmor, StrMid, StrLower, StrUpper.43 handlers have implementations,50 remain unimplemented. This is not a percentage of full addon migration or full behavioral parity.

Unit getters resolve native CUnitServer and read its real RPG stats. Named perks retain the reference26-name/index mapping; manual and automatic skill queries remain distinct. Prototype and race come from SUDBUnitPrototype.szName/szRace, directly corresponding to reference name/race strings. Equipment names come from the currently equipped item's shape entry. String slicing keeps the reference one-based/clamped positions, negative positions from the end, zero length as remaining text and negative length as no assignment. Case conversion uses the same CharLowerA/CharUpperA APIs as Shared/Common.cpp, not an invented Unicode or ASCII mapping.

Checked integer conversion rejects nonfinite/unrepresentable values; array indices reject out-of-range values after the reference integer truncation. Armor bounds are checked before the native unchecked GetQArmor. Invalid/missing weapon slot returns no assigned string as the reference's absent-item path. Empty string slicing safely returns empty. These are disclosed error/boundary adaptations; no stats layout or saved schema changed.

Confirmed reference defect: GetSkill("tame") uses RPGASKL_TAME=8 but RPG_NUM_AUTO_SKILLS=8 in both reference and engine. It is explicitly rejected, not aliased to manual tame or an adjacent byte. Thus GetSkill has an unresolved source-contract case despite registry availability. Unknown other skill/perk names retain zero result; reference logs are not reproduced.

## Validation
Build62 Release Win32 PASS (one build), check-unit-commands-r25.py headless inspector PASS (one invocation), PE32 x86 PASS. Game/ModRuntime.cpp fixtures execute SSCCmd::CalcFunction against8 real server/prototype fixtures: all3 manual,8 automatic and36 perk indices with distinct values; named/case-insensitive queries; unknown name; invalid indices; explicit tame rejection; prototype/race strings; two worn armor slots; absent weapon. Extra references protect stack fixture objects; query mutations are restored before existing stat checks.

Actual native parser, execution and text export/reparse execute StrMid negative position and zero length, lower/upper ASCII. Existing nested MOB branches and suspended binary world-script round-trip, optional/variadic commands, native stat/regen/queue checks remain clean in the same inspector. The inspector exits before window initialization, not an interactive game test.

## Handoff and limits
Clean folder work/handoffs/eirebuild-x86-mob-unit-commands-20260916-r25 has EIRebuild.exe, README, manifest; exact matching PDB separate symbols/20260916-r25. Requires test game's existing dependencies/resources. Source preimages and hashes saved in evidence/addon-integration/pre-r25 and r25-manifest.json. Installed R8, Starter and mod remain unchanged. No merge/push authorized.

Untested: nonempty weapon query, every named alias, CP1251 case conversion under alternate Windows locales, all slicing boundaries, parsed live object binding and original addon saves. Direct native objects and in-memory script fixtures do not prove populated game-world behavior. No new gameplay/balance change intended.

Additional source discrepancy identified: existing GetUnitPose bridge reads CUnit::currentPose, reference reads CUnitServer::currentPose.pose. Runtime equality is unproved; linked follow-up must use the server authority and a transition-state fixture. It is not fixed or covered by R25.

Remaining full scope:50 handlers plus unresolved tame/DistancePointPoint contracts, pose authority, NPC equipment migration, combat/AI/loot/audio/client/network behavior and original saves. R17 startup/rendering issues remain deferred. Next coherent block starts with authoritative object/state queries and pose correction, then mutation/action commands. After complete source transfer, test real MOB queries, equipment changes, save/load and zone transitions in the game; do not launch now.
'''
(e/'R25-REPORT.md').write_text(report,encoding='utf-8')
(pkg/'README.md').write_text('R25:12 additional native unit/string MOB commands,43 registry handlers implemented (GetSkill tame remains explicitly unsupported). Build62 + real-unit/headless script checks PASS. No installation/gameplay. Source branch/commit N/A copied engine; reference57b8e0c. Requires game dependencies. Matching PDB separate symbols/20260916-r25. See R25-REPORT.md in project evidence for limitations and manual acceptance after full migration. No merge/push authorized.\n',encoding='utf-8')
p=e/'FEATURE-MATRIX.md';p.write_bytes(p.read_bytes()+b'\r\nR25:12 native unit/string commands,43 implemented registry handlers; GetSkill tame explicitly unsupported due to reference out-of-bounds index. Build62/native object and parser-export checks PASS; no game/install. See R25-REPORT.md.\r\n')
p=r.parent/'investigations/project-roadmap.md'
with p.open('a',encoding='utf-8') as f:
 f.write('\nGAME-EXE-MOB-TAME-CONTRACT-001 OPEN linked addon-integration: reference GetSkill(tame) indexes autoSkills[8] with count8. R25 explicitly rejects this unsafe source case; no invented replacement or array/save-layout expansion. Next establish intended data contract from actual mod use.\n')
 f.write('GAME-EXE-MOB-POSE-AUTHORITY-001 OPEN linked addon-integration: reference GetUnitPose reads server.currentPose.pose; existing native bridge reads unit.GetCurrentPose(). Equality during transitions unproved. Next use server authority and targeted transition-state fixture. No fix/runtime claim yet.\n')
 f.write('GAME-EXE-EIREBUILD-ADDON-INTEGRATION-001 R25 checkpoint2026-09-16:12 additional skill/perk/prototype/race/equipment/string handlers implemented,43 registry entries available (tame unresolved),50 handlers unimplemented. Build62 ReleaseWin32 and one headless native8-unit/full array indices/worn armor/string parser-export/shared regression PASS. No game/install/merge/push. Copiedengine GitN/A, reference57b8e0c reused. Package work/handoffs/eirebuild-x86-mob-unit-commands-20260916-r25 EXE'+manifest['exe']+', matching PDBsymbols20260916-r25; evidence R25-REPORT.md,r25-runtime.json,r25-manifest.json under work/eirebuild-local/evidence/addon-integration. Parent ACTIVE fullscope. Next authoritative object/state queries and pose correction then action commands/NPC equipment/combat/AI. Nonempty weapon, locale/string boundaries, original saves and populated world untested; explicit tame and index safety adaptations disclosed. Source transfer first, gameplay afterward.\n')
print(manifest['exe']);print(pkg)
