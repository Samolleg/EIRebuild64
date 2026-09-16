from pathlib import Path
import hashlib,json,shutil
r=Path('work/eirebuild-local');p=Path('work/handoffs/eirebuild-x86-shadow-optimization-20260916-r6');p.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
exe=r/'out/Win32/Release/EI_Rebuild_dx11_Release_Win32.exe';shutil.copy2(exe,p/'EIRebuild.exe');symbols=r/'symbols/20260916-r6';symbols.mkdir();shutil.copy2(exe.with_suffix('.pdb'),symbols/'EIRebuild.pdb')
changed=[]
for item in json.loads((r/'evidence/pre-shadow-optimization/manifest.json').read_text()):
 changed.append(dict(file=item['file'],before=item['sha256'],after=sha(r/item['file'])))
changed.append(dict(file='Game/ModShadowSchedule.h',after=sha(r/'Game/ModShadowSchedule.h')))
m={'revision':'R6 shadow optimization diagnostic','exe_sha256':sha(p/'EIRebuild.exe'),'pdb_sha256':sha(symbols/'EIRebuild.pdb'),'source':str(r.resolve()),'git':'N/A copied snapshot','reference':'quiet e2c4867 shadow_fixes.cpp; forced refresh tiers132/400/1000ms budget32, density prebake/opaque bypass','changed':changed,'build':'build-12-shadow-optimization PASS','check':'R6 reference prebake equivalence; forced refresh tiers/budget/timer wrap; DX11 WARP alpha/max-union PASS','runtime':'FPS and culling/movement/map changes unverified'}
(p/'manifest.json').write_text(json.dumps(m,indent=2));print(m['exe_sha256'])
(r/'evidence/shadow-offscreen-r6-result.txt').write_text('PASS: reference-equivalent prebaked partial foliage; scheduler tiers/budget/wrap; actual DX11 WARP shaders, alpha readback, union1=union20=ffbfbfbf, foliage density pixels1285, transparent0.\n')
