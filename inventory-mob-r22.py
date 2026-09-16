from pathlib import Path
import re,struct,json,hashlib
r=Path(__file__).resolve().parent;ref=r.parent/'src_addon_world_background_data_20260916/src/addon'
p=Path(r'C:\GOG Games\Evil Islands\Starter\Engine\game.exe');b=p.read_bytes();pe=struct.unpack_from('<I',b,60)[0];n,opt=struct.unpack_from('<H',b,pe+6)[0],struct.unpack_from('<H',b,pe+20)[0];base=struct.unpack_from('<I',b,pe+24+28)[0]
sections=[struct.unpack_from('<IIII',b,pe+24+opt+i*40+8) for i in range(n)]
def offset(va):
 for size,rva,rawsize,raw in sections:
  if rva<=va-base<rva+rawsize:return raw+va-base-rva
 raise ValueError(hex(va))
def string(va):
 if not va:return None
 start=offset(va);return b[start:b.index(b'\0',start)].decode('ascii')
tokens=[];at=offset(0x78bca0)
while True:
 name,id,fun,proc=struct.unpack_from('<4I',b,at);at+=16
 if not name:break
 tokens.append({'name':string(name),'id':id,'function':string(fun),'procedure':string(proc)})
enum=(r/'Game/ScriptParser.h').read_text();names=re.findall(r'\b(SC_\w+)\s*(?:=\s*\d+)?\s*,',enum);ids={name:i for i,name in enumerate(names)}
native={name:int(ids[id]) for name,id in re.findall(r'\{\s*"([^"\n]+)"\s*,\s*(SC_\w+)',re.sub(r'/\*.*?\*/|//[^\n]*','',(r/'Game/ScriptParser.cpp').read_text(encoding='cp1251'),flags=re.S))}
diff=[(t['name'],t['id'],native.get(t['name'])) for t in tokens if native.get(t['name'])!=t['id']];assert not diff,diff
body=(ref/'NewScriptCommands.cpp').read_text(encoding='utf-8-sig').split('const SCustomHandler NewScriptCommands::NewScriptCmds[] = {',1)[1];body=re.sub(r'#ifdef _DEBUG.*?#endif','',body,flags=re.S)
commands=[]
for name,fmt,handler in re.findall(r'\{\s*"([^"\n]+)"\s*,\s*"([^"\n]*)"\s*,\s*(\w+)_handler',body):
 id=native[name] if name in native and native[name]<300 else 300+len(commands)
 commands.append({'name':name,'format':fmt,'id':id,'handler':handler})
out={'gameExe':str(p),'sha256':hashlib.sha256(b).hexdigest().upper(),'sourceReference':'57b8e0c42061a0acd51eaf64ea008aa1447d94ee','originalTokens':tokens,'customCommands':commands,'nativeTokenParity':True}
(r/'evidence/addon-integration/r22-command-registry.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
print(len(tokens),len(commands));print([x for x in commands if x['name']=='RefreshUnitStats'])
