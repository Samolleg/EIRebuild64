from pathlib import Path
import hashlib,json,shutil,datetime
r=Path('work/eirebuild-local').resolve();e=r/'evidence/addon-integration';pkg=r.parent/'handoffs/eirebuild-x86-mod-startup-20260916-r16';pkg.mkdir(parents=True,exist_ok=False)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest().upper()
exe=r/'out/Win32/Release/EI_Rebuild_dx11_Release_Win32.exe';check=json.loads((e/'r16-check.json').read_text());assert check['success'] and sha(exe)==check['exe']
sym=r/'symbols/20260916-r16';sym.mkdir(parents=True,exist_ok=True);shutil.copy2(exe,pkg/'EIRebuild.exe');shutil.copy2(exe.with_suffix('.pdb'),sym/'EIRebuild.pdb')
files=['ModPackage.h','ModPackage.cpp','ModRuntime.h','ModRuntime.cpp','CMappedResource.h','CMappedResource.cpp','CResourceBase.h','FileRegistry.cpp','SaveLoader.h','GameMain.cpp','Win32Classes.cpp','ChunkFile.cpp','ResourceManager.cpp']
m={'revision':'R16 normal startup plumbing diagnostic','sourceRoot':str(r),'branch':'N/A copied engine snapshot','commit':'N/A copied engine snapshot','created':datetime.datetime.now(datetime.timezone.utc).isoformat(),'source':{'Game/'+f:sha(r/'Game'/f) for f in files},'files':{'EIRebuild.exe':sha(pkg/'EIRebuild.exe')},'symbols':{'path':str(sym/'EIRebuild.pdb'),'sha256':sha(sym/'EIRebuild.pdb')},'build':'build-43-mod-startup','validation':'check-mod-startup.py, native headless consumers and PE32 x86','installed':False,'interactiveTested':False,'mergePush':'none; not authorized'}
for p in [pkg/'manifest.json',e/'r16-manifest.json']:p.write_text(json.dumps(m,ensure_ascii=False,indent=2),encoding='utf-8')
report='''# R16 — package connected to normal startup

Parent GAME-EXE-EIREBUILD-ADDON-INTEGRATION-001 ACTIVE. Copied engine source work/eirebuild-local; Git branch/commit N/A. Pre-edit snapshots pre-r16. No addon/Lua/data/installed changes. Existing native calculations from R15 reused; this block changes the engine loading path.

## Implementation
WinMain now constructs ModRuntime::Host when supplied `--mod <directory>`, before WinMainInit; the Host survives the message loop and world destruction. No directory means the existing base-game path. This selects a package, not a new feature toggle. Host loads/validates the package and initializes the typed Lua session. CommandLineToArgvW handles the selected path; duplicate/missing selectors fail explicitly. It does not read or change the Starter profile and does not replace EIStarter/game.exe.

Both CMappedResource and CWin32MappedResource apply declared ordered resource overlays, including ignore-base. Native file-list and existence calls are virtual and reflect the selected resources. Reads and closes go to the actual child archive, whose ownership remains within the parent handle after Host destruction. Package/child archives open raw, avoiding recursive overrides. Native resource managers admit ignore-base packages even when the base file is absent. Existing graphics-resources.ini overlays remain for the no-mod path; a selected mod owns its own graphics archive order.

CFileRegistry read-open redirects declared config filenames. Map-only read resolution is wired to CFileSave::OpenRead, CMappedFile read mapping and read-only CChunkFile opens. Relative map subdirectories are retained. Write/read-write paths are not redirected, and save paths are not moved into the installed mod. The base fallback remains if no override exists.

## Validation
Build43 Release Win32/x86 PASS. check-mod-startup.py PASS on exact EXE, PE x86, installed Lua/config/database and test base database unchanged. The headless inspector creates the same Host class and exercises three real entries (units.udb/items.idb/perks.pdb) through both native resource implementations against raw ordered-package bytes. Existence, masked enumeration replacing old output, close ownership, config path/open and a real MPR file read pass. Binding clears after Host teardown; an already-open resource handle remains usable and closes correctly.

No window or populated world was launched. This proves the component loading path, not a successful ordinary WinMain/menu/map or gameplay run. R15 stat evidence reused for unchanged formulas; no repeated full historical suite.

## Handoff and next step
Clean package work/handoffs/eirebuild-x86-mod-startup-20260916-r16: EXE, manifest, README. Matching PDB symbols/20260916-r16. It requires the test-game DLLs/resources. For a future user-run check, working directory is the test game C:/GOG Games/Evil and arguments are `--mod "C:/GOG Games/Evil Islands/Starter/MODS/JamaisVu135"`; run this staged EXE, not EIStarter. No installation authorized or performed, no launch/merge/push/publication.

Next validate normal WinMain/menu and a real mod scene, address actual missing consumers/contracts rather than inventing behavior. Ordinary mod mode now exists in source, but the following remain open: additional MQ discovery still enumerates the base directory; arbitrary read-write map paths are deliberately not redirected; loader thread affinity and resources first opened on another thread have not been exercised (Host binding is thread-local); resource-manager/graphics consumers beyond the two readers were not tested with the rendered world. Resource error/partial WinMain teardown still needs actual startup coverage. Existing addon-specific MOB commands, full combat/UI/audio/network and StatInitialization/migration/save lifecycle are not all ported. An existing mod save is not declared compatible. Runtime cache/load reset, client party fallback and complex sets remain from R15.

No performance or full-mod/production claim. Active R8 is not replaced. This is an executable diagnostic handoff for the next startup test, not the completed engine migration.
'''
(e/'R16-REPORT.md').write_text(report,encoding='utf-8')
(pkg/'README.md').write_text('R16 diagnostic with normal --mod <directory> selection. Source/branch/commit N/A copied engine snapshot; Build43 + native resource/config/map headless check PASS.\n\nNot installed or launched interactively. Requires test-game dependencies. Future manual check: run this EIRebuild.exe with working directory C:/GOG Games/Evil and --mod "C:/GOG Games/Evil Islands/Starter/MODS/JamaisVu135". Check menu then new scene; record any actual error. Existing mod save compatibility and full addon/MOB/gameplay are not established.\n\nReport work/eirebuild-local/evidence/addon-integration/R16-REPORT.md, exact manifest attached; PDB separate. No merge/push.\n',encoding='utf-8')
with (e/'FEATURE-MATRIX.md').open('a',encoding='utf-8') as f:f.write('\nR16: normal WinMain --mod Host/session + both resource readers, config registry and read-only map consumers connected. Native headless loading/lifetime PASS; actual menu/map/gameplay, extra MQ enumeration, cross-thread loading and full addon contracts remain open. See R16-REPORT.md.\n')
print(m['files'])
