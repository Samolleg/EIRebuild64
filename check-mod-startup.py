from pathlib import Path
import hashlib,json,subprocess,struct
r=Path(__file__).resolve().parent;mod=Path(r'C:\GOG Games\Evil Islands\Starter\MODS\JamaisVu135');game=Path(r'C:\GOG Games\Evil');e=r/'evidence/addon-integration';exe=r/'out/Win32/Release/EI_Rebuild_dx11_Release_Win32.exe'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest().upper()
inputs=[mod/'config.reg',mod/'config/ai.reg',mod/'main.lua',mod/'res/database.res',mod/'res/databaseJmv.res',game/'res/database.res']+list((mod/'lua').glob('*.lua'))
before={str(p):sha(p) for p in inputs};identity=sha(exe)
result=subprocess.run([str(exe),'--inspect-mod-startup',str(mod),str(e/'r16-runtime.json')],cwd=game,timeout=45,creationflags=subprocess.CREATE_NO_WINDOW)
data=json.loads((e/'r16-runtime.json').read_text());assert result.returncode==0 and data['success'],data
assert data['nativeReaderEntries']==3 and data['mappedReaderEntries']==3
for key in ['hostLifecycle','resourceOwnership','enumeration','configConsumer','mapReadConsumer']:assert data[key],key
assert before=={str(p):sha(p) for p in inputs};assert identity==sha(exe)
b=exe.read_bytes();offset=struct.unpack_from('<I',b,60)[0];assert struct.unpack_from('<H',b,offset+4)[0]==0x14c
(e/'r16-check.json').write_text(json.dumps({'success':True,'exe':identity,'inputs':before,'checks':data,'artifact':'PE32 x86','limits':'Native headless consumers, no played game or normal WinMain/window acceptance'},ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(data,indent=2))
