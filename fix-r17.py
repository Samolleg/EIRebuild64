from pathlib import Path
import shutil,hashlib,json
r=Path(__file__).resolve().parent
e=r/'evidence/addon-integration/pre-r17';e.mkdir(exist_ok=True)
def replace(name,old,new):
 p=r/name;b=p.read_bytes();o=old.replace('\n','\r\n').encode();n=new.replace('\n','\r\n').encode();assert b.count(o)==1,name
 target=e/name;target.parent.mkdir(parents=True,exist_ok=True);assert not target.exists();shutil.copy2(p,target);p.write_bytes(b.replace(o,n))
replace('Game/ModLuaBridge.cpp',
' if(op!=2&&(!std::isfinite(p->hls[RPGHL_BODY].fpHPWeight)',
' // Native distribution writes only used hit locations; an empty skeleton is a no-op.\n if(op!=2){bool used=false;for(const auto&h:p->hls)used=used||h.state>RPGALS_NOTUSED;if(!used)return 0;}\n if(op!=2&&(!std::isfinite(p->hls[RPGHL_BODY].fpHPWeight)')
replace('Game/ModRuntime.cpp',
'            hero.hls[RPGHL_BODY].fpHPWeight=0;float prior=hero.psStrength.fpEffectiveValue;hero.psStrength.fpBaseValue=999;',
'            SRPGUnitStats empty=hero;for(auto&h:empty.hls)h.Clear();SRPGUnitStats beforeEmpty=empty;\n            empty.RecalcInternalHeroStats(42);\n            require(memcmp(empty.hls,beforeEmpty.hls,sizeof(empty.hls))==0,"Unused hit locations changed during HP distribution");\n            hero.hls[RPGHL_BODY].fpHPWeight=0;float prior=hero.psStrength.fpEffectiveValue;hero.psStrength.fpBaseValue=999;')
(e/'source.json').write_text(json.dumps({str(p.relative_to(e)):hashlib.sha256(p.read_bytes()).hexdigest() for p in e.rglob('*.cpp')},indent=2))
