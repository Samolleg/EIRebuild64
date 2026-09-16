from pathlib import Path
import subprocess,hashlib,json,re,struct
r=Path(__file__).resolve().parent;e=r/'evidence/addon-integration';mod=Path(r'C:\GOG Games\Evil Islands\Starter\MODS\JamaisVu135');game=Path(r'C:\GOG Games\Evil');exe=r/'out/Win32/Release/EI_Rebuild_dx11_Release_Win32.exe'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest().upper()
inputs=[mod/'main.lua',mod/'lua/default.lua',mod/'lua/tables.lua',mod/'config/ai.reg'];before={str(p):sha(p) for p in inputs}
result=subprocess.run([str(exe),'--inspect-mod-economy',str(mod),str(e/'r19-runtime.json')],cwd=game,timeout=40,creationflags=subprocess.CREATE_NO_WINDOW)
data=json.loads((e/'r19-runtime.json').read_text());assert result.returncode==0 and data.get('success'),data
text=(mod/'lua/tables.lua').read_text(encoding='utf-8-sig');body=text.split('function RecalcShopsPriceMultipliers',1)[1].split('\nend',1)[0]
assignments=re.findall(r'mults:set\((\d+) \* mults_count \+ (\d+), ([0-9.]+)\)',body);assert len(assignments)==36
for n,mode in zip([5,4],data['shopCoefficients']):
 expected=[[float(100+i*10+j) for j in range(10)] for i in range(n)]
 for i,j,value in assignments:expected[int(i)][int(j)]=float(value)
 assert len(mode)==n
 for a,b in zip(expected,mode):assert len(b)==10 and all(abs(x-y)<1e-5 for x,y in zip(a,b)),(a,b)
assert before=={str(p):sha(p) for p in inputs}
b=exe.read_bytes();pe=struct.unpack_from('<I',b,60)[0];assert struct.unpack_from('<H',b,pe+4)[0]==0x14c
(e/'r19-check.json').write_text(json.dumps({'success':True,'exe':sha(exe),'inputs':before,'oracleAssignments':len(assignments),'checks':data,'limit':'Headless native methods and Lua callbacks; no game, UI or connected network test'},indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in data.items() if k!='shopCoefficients'},indent=2))
