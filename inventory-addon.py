from pathlib import Path
import re,json,csv,hashlib
r=Path('work/src_addon_world_background_base_0458524_20260916/src/addon'); e=Path('work/eirebuild-local'); out=e/'evidence/addon-integration'
def read(p):return p.read_bytes().decode('utf-8-sig',errors='replace')
patch=read(r/'Patches/Patch.cpp');rows=[];group='';seen=set()
for lineNo,line in enumerate(patch.splitlines(),1):
 m=re.search(r'SAddonPatch (\w+)\[\]',line)
 if m:group=m[1]
 m=re.search(r'^\s*\{\s*"([^"]+)",\s*&CPEImagePatch::(\w+)',line)
 if m:
  name,method=m.groups();files=[]
  for f in (r/'Patches').glob('*.cpp'):
   txt=read(f);mm=re.search(r'EPatchStatus\s+CPEImagePatch::'+method+r'\s*\(',txt)
   if mm:files.append(f'{f.relative_to(r)}:{txt[:mm.start()].count(chr(10))+1}')
  rows.append(dict(id=f'ADDON-{len(rows)+1:03}',group=group,name=name,method=method,registration=f'Patches/Patch.cpp:{lineNo}',definition='; '.join(files),status='partial-source-port-R4' if name=='WideScreen' else 'pending-semantic-comparison'))
(out/'patch-registry.json').write_text(json.dumps(rows,indent=2,ensure_ascii=False),encoding='utf-8')
with (out/'patch-registry.csv').open('w',encoding='utf-8-sig',newline='') as f:
 w=csv.DictWriter(f,fieldnames=rows[0].keys());w.writeheader();w.writerows(rows)
# Keep the monolith's internal entry points visible; one registration is not one feature.
functions=[]
for p in r.rglob('*.cpp'):
 if 'EIClasses' in p.parts:continue
 for n,l in enumerate(read(p).splitlines(),1):
  if re.match(r'^(?:static\s+)?(?:[\w:*<>]+\s+)+(?:\w+::)*\w+\s*\(',l) and not l.lstrip().startswith(('return ','if ','for ','while ')):
   functions.append({'source':str(p.relative_to(r)),'line':n,'declaration':l.strip()})
(out/'source-functions.json').write_text(json.dumps(functions,ensure_ascii=False,indent=2),encoding='utf-8')
def pushes(path):return sorted(set(re.findall(r'^\s*PushFunc\((\w+)\);',read(path),re.M)))
current=pushes(r/'LuaState.cpp');existing=pushes(e/'Game/LuaState.cpp');mods=Path('C:/GOG Games/Evil Islands/Starter/MODS/JamaisVu135');snap=out/'installed';snap.mkdir(exist_ok=True);ident=[]
for p in [mods/'config.ini',mods/'config.reg',mods/'config/ai.reg',mods/'main.lua',*sorted((mods/'lua').rglob('*.lua'))]:
 dest=snap/p.relative_to(mods);dest.parent.mkdir(parents=True,exist_ok=True);data=p.read_bytes();dest.write_bytes(data);ident.append({'path':str(p.relative_to(mods)),'size':len(data),'sha256':hashlib.sha256(data).hexdigest()})
(out/'installed-inputs.json').write_text(json.dumps(ident,indent=2))
lf=[]
for p in [mods/'main.lua',*sorted((mods/'lua').rglob('*.lua'))]:
 txt=read(p)
 lf.append({'path':str(p.relative_to(mods)),'functions':re.findall(r'^\s*(?:local\s+)?function\s+([\w.:]+)',txt,re.M),'requires':re.findall(r'require\s*\(?["\']([^"\']+)',txt),'rawMemoryReferences':len(re.findall(r'\b(?:Memory|FromMem|ToMem|ffi)\b',txt))})
(out/'lua-contracts.json').write_text(json.dumps({'nativeCurrent':current,'engineExisting':existing,'missingNativeNames':sorted(set(current)-set(existing)),'sameNameDoesNotProveABI':True,'modules':lf},ensure_ascii=False,indent=2),encoding='utf-8')
config=read(r/'Config.cpp');keys=re.findall(r'(\w+)\.(Get\w+)\("([^"]+)",\s*"([^"]+)"',config)
(out/'config-keys.json').write_text(json.dumps(keys,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'patchRegistrations':len(rows),'sourceFunctionCandidates':len(functions),'nativeBindings':len(current),'missingBindingNames':len(set(current)-set(existing)),'LuaModules':len(lf),'configReads':len(keys)}))
md='# Addon integration registry\n\nBaseline: installed DLL FEBFF3C773CC808DBFC74E5E4A149FC85702A38D3B2363CF5D3DE969A0004CE6, source0458524693d3e92e02d5a05da58de10885235ee1. Engine copied snapshot work/eirebuild-local, Git N/A.\n\nComplete registration list below is source inventory, NOT runtime-enabled features or completion percentage. Monolithic registrations contain many mechanics: source-functions.json preserves internal entry candidates (lexical inventory, not a complete C++ parser). Lua/config manifests retained separately. DDraw graphics are separate from addon.\n\n| ID | Registered patch | Group | Source | State |\n|---|---|---|---|---|\n'
for x in rows:md+=f"| {x['id']} | {x['name']} | {x['group']} | {x['definition']} | {x['status']} |\n"
md+='\nOrder: package/config/Lua data -> typed Unit/Stats/Items bridge and full main.lua -> MOB/save contracts -> combat/AI -> UI/audio/network. No item is complete merely because a same-named function exists in EIRebuild. Existing R4 widescreen partial; R8 terrain reference belongs to DDraw.\n'
(out/'FEATURE-MATRIX.md').write_text(md,encoding='utf-8')
