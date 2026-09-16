from pathlib import Path
import subprocess,json
r=Path(__file__).resolve().parent
exe=r/'out/Win32/Release/EI_Rebuild_dx11_Release_Win32.exe';ev=r/'evidence/addon-integration'
args=[str(exe),'--inspect-addon','C:/GOG Games/Evil Islands/Starter/MODS/JamaisVu135',str(ev/'package-runtime.json')]
p=subprocess.run(args,cwd='C:/GOG Games/Evil',timeout=60,creationflags=subprocess.CREATE_NO_WINDOW)
assert p.returncode==0,p.returncode
result=json.loads((ev/'package-runtime.json').read_text(encoding='utf-8'))
assert result['success'] and result['luaErrorRecovery'] and result['overlapOwnerProbes']>0
assert result['resourceReadProbes']==61 and result['mapCount']==199
assert result['fullMainExecuted'] is False and result['gameplayActivated'] is False
empty=ev/'missing-mod';empty.mkdir(exist_ok=True)
args[2]=str(empty);args[3]=str(ev/'missing-mod-result.json')
p=subprocess.run(args,cwd='C:/GOG Games/Evil',timeout=15,creationflags=subprocess.CREATE_NO_WINDOW)
negative=json.loads((ev/'missing-mod-result.json').read_text(encoding='utf-8'))
assert p.returncode==2 and negative['success'] is False
summary={k:v for k,v in result.items() if k not in ['resourceGroups','luaTables']};summary['negativeMissingConfig']='PASS';summary['uiLaunched']=False
(ev/'foundation-check.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(summary))
