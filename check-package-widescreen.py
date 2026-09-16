from pathlib import Path
import re, math, json, hashlib, shutil
r=Path('work/eirebuild-local'); src=(r/'Game/I3DMain.cpp').read_bytes().decode('cp1251')
def expr(signature, lhs):
    body=src.split(signature,1)[1].split('}',1)[0]
    return re.search(re.escape(lhs)+r' = (.*);',body)[1]
fwd=expr('void CInterface3D::VirtualToScreen(CPoint &point)','point.x')
inv=expr('void CInterface3D::ScreenToVirtual(CPoint &pt)','pt.x')
size=expr('void CInterface3D::VirtualToScreen(CSize &point)','point.cx')
def effective(w,h): return h*800//600 if w>=800 and h>=600 and w*3>h*4 else w
# Evaluate the actual assignment expressions with C++ integer division (truncate toward zero).
class I(int):
    def __add__(a,b): return I(int(a)+int(b))
    def __sub__(a,b): return I(int(a)-int(b))
    def __mul__(a,b): return I(int(a)*int(b))
    def __truediv__(a,b): return I(math.trunc(int(a)/int(b)))
def calc(e,x,w,h):
    e=e.replace('WideScreen::Effective(nScreenWidth, nScreenHeight)',str(effective(w,h)))
    for k,v in [('point.cx',x),('point.x',x),('pt.x',x),('nScreenWidth',w),('nScreenHeight',h)]: e=e.replace(k,str(v))
    e=re.sub(r'(?<![\w])\d+',lambda m:'I('+m[0]+')',e)
    return int(eval(e,{'I':I}))
rows=[]
for w,h in [(800,600),(1024,768),(1280,720),(1920,1080),(3840,2160),(3440,1440),(1919,1079)]:
    eff=effective(w,h); shift=(w-eff)//2; virtual=math.ceil(shift*800/eff)
    assert calc(fwd,0,w,h)==shift
    assert calc(size,0,w,h)==0
    assert abs(calc(fwd,-virtual,w,h))<=math.ceil(eff/800)
    assert abs(calc(fwd,800+virtual,w,h)-w)<=math.ceil(eff/800)+1
    err=max(abs(calc(inv,calc(fwd,x,w,h),w,h)-x) for x in range(-virtual,801+virtual))
    assert err<=1,(w,h,err)
    assert abs(eff/800-h/600)<1/800+1e-9
    rows.append(dict(resolution=f'{w}x{h}',effective=eff,offset=shift,virtualShift=virtual,maxRoundtripError=err))
assert rows[4]['effective']==2880 and rows[4]['offset']==480
out=r/'evidence/widescreen-coordinate-check.json'; out.write_text(json.dumps({'result':'PASS','method':'Actual CPoint/CSize source expressions, C++ integer semantics; no game runtime claim','cases':rows},indent=2))
handoff=Path('work/handoffs/eirebuild-x86-widescreen-20260916-r4'); handoff.mkdir(exist_ok=False)
exe=r/'out/Win32/Release/EI_Rebuild_dx11_Release_Win32.exe'; shutil.copy2(exe,handoff/'EIRebuild.exe')
symbols=r/'symbols/20260916-r4'; symbols.mkdir(exist_ok=False); shutil.copy2(exe.with_suffix('.pdb'),symbols/'EIRebuild.pdb')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
changed=[]
for p in (r/'evidence/pre-widescreen').iterdir():
    now=r/'Game'/p.name
    if now.exists() and sha(now)!=sha(p): changed.append({'file':p.name,'before':sha(p),'after':sha(now)})
changed.append({'file':'WideScreen.h','after':sha(r/'Game/WideScreen.h')})
manifest={'revision':'R4','exe_sha256':sha(handoff/'EIRebuild.exe'),'pdb_sha256':sha(symbols/'EIRebuild.pdb'),'source_root':str(r.resolve()),'git':'N/A copied EIRebuild source snapshot','changed':changed,'build':'build-07-widescreen PASS','coordinate_check':str(out),'runtime':'UNTESTED'}
(handoff/'manifest.json').write_text(json.dumps(manifest,indent=2))
print(json.dumps({'result':'PASS','cases':len(rows),'changed':len(changed),'package':str(handoff),'sha256':manifest['exe_sha256']}))
