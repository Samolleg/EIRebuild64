from pathlib import Path
import hashlib,json,shutil
r=Path('work/eirebuild-local');package=Path('work/handoffs/eirebuild-x86-mod-shadows-20260916-r5');package.mkdir(exist_ok=False)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
exe=r/'out/Win32/Release/EI_Rebuild_dx11_Release_Win32.exe';shutil.copy2(exe,package/'EIRebuild.exe')
symbols=r/'symbols/20260916-r5';symbols.mkdir();shutil.copy2(exe.with_suffix('.pdb'),symbols/'EIRebuild.pdb')
settings=Path('C:/GOG Games/Evil/Config/settings.ini');before=settings.read_bytes();assert b'ShadowQuality=0' in before
(package/'settings.ini').write_bytes(before.replace(b'ShadowQuality=0',b'ShadowQuality=2',1))
changed=[]
for old in json.loads((r/'evidence/pre-shadows/manifest.json').read_text()):
 p=r/old['file'];changed.append(dict(file=old['file'],before=old['sha256'],after=sha(p)))
for n in ['Game/ModShadowRaster.h','Game/dx11/ModShadowDX11.h']:changed.append(dict(file=n,after=sha(r/n)))
m={'revision':'R5 diagnostic object-shadow port','source':str(r.resolve()),'git':'N/A copied snapshot','reference_commit':'e2c4867c7be7136248a86840c82eb89deef08c10','reference_dll_sha256':'7922D2D8EB55964DD4760475BEFF310A72A74CCB309E96AEE80566260EF8BFA0','files':{p.name:sha(p) for p in package.iterdir()},'settings_before':sha(settings),'pdb_sha256':sha(symbols/'EIRebuild.pdb'),'changed':changed,'build':'build-10-mod-shadows-final PASS','offscreen':'WARP actual shaders PASS; union1=union20=ffbfbfbf, alpha foliage1286px, transparent0','game_runtime':'NOT TESTED','remaining':'terrain sun field/temporal blending/shared terrain-object union and mod scheduling/atlas/adaptive-band parity are NOT ported'}
(package/'manifest.json').write_text(json.dumps(m,indent=2))
(r/'evidence/shadow-offscreen-result.txt').write_text(m['offscreen']+'\nSource evidence/shadow-offscreen.cpp; run-shadow-check.cmd; Windows D3D11 WARP.\n')
print(json.dumps({'package':str(package),'files':m['files']}))
