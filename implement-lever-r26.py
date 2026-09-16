from pathlib import Path
r=Path(__file__).resolve().parent
def edit(name,fn):
 p=r/name;b=p.read_bytes();dest=r/'evidence/addon-integration/pre-r26'/name;dest.parent.mkdir(parents=True,exist_ok=True)
 if not dest.exists():dest.write_bytes(b)
 p.write_bytes(fn(b.decode('cp1251')).encode('cp1251'))
edit('Game/ModPackage.h',lambda s:s.replace('bool registeredLoot=true;','bool registeredLoot=false;'))
edit('Game/ModPackage.cpp',lambda s:s.replace('"JMVRegisterLoot",1','"JMVRegisterLoot",0'))
edit('Game/UnitServer.h',lambda s:s.replace('    EUnitPose GetScriptPose()', '    const SUnitCommand& GetScriptCommand() const { return curCommand; }\r\n    EUnitPose GetScriptPose()'))
edit('Game/StuffMapObjects.h',lambda s:s.replace('    CLeverObject();','    bool UsesAuthoredInteraction() const;\r\n    bool ScriptActivationAllowed(CUnit* who);\r\n    CLeverObject();'))
def stuff(s):
 s=s.replace('#include "ModRegisteredLoot.h"','#include "ModRegisteredLoot.h"\r\n#include "ModRuntime.h"\r\n#include "UnitServer.h"\r\n#include <cmath>')
 a='bool CLeverObject::ApplyScience(const SRPGScienceStats &tool, CUnit *pWho)\r\n{';assert a in s
 s=s.replace(a,a+'\r\n    if(ModRuntime::Current() && !ScriptActivationAllowed(pWho))return false;')
 a='void CLeverObject::GetUsePoints(CArray<CPlot3D, CPlot3D &> &points, float fpUnitRadius)'
 helper='''bool CLeverObject::UsesAuthoredInteraction() const { return theLeverDatabase.levers.GetEntry(GetMaskName())!=nullptr; }
static void LeverFallback(CLeverObject&lever,CArray<CPlot3D,CPlot3D&>&points,float radius){
 const auto&p=lever.GetCP();if(!std::isfinite(radius)||radius<=0||!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return;
 points.SetSize(16);for(int i=0;i<16;i++){double angle=i*(6.2831853071795864769/16);points[i]=CPlot3D(p.x+radius*float(cos(angle)),p.y+radius*float(sin(angle)),p.z);}
}
bool CLeverObject::ScriptActivationAllowed(CUnit*who){
 if(!who||UsesAuthoredInteraction())return true;auto*server=dynamic_cast<CUnitServer*>(who->GetUnitSpecific());if(!server)return false;
 float radius=who->GetAttackDistance();CArray<CPlot3D,CPlot3D&> points;LeverFallback(*this,points,radius);const auto&cmd=server->GetScriptCommand();bool current=false;
 for(int i=0;i<points.GetSize();i++){auto d=cmd.ptTarget-points[i];if(d.x==0&&d.y==0&&d.z==0)current=true;}
 float dx=who->GetCP().x-cmd.ptTarget.x,dy=who->GetCP().y-cmd.ptTarget.y,ox=who->GetCP().x-GetCP().x,oy=who->GetCP().y-GetCP().y;
 float distance=dx*dx+dy*dy,objectDistance=ox*ox+oy*oy,reach=radius+.2f;
 return cmd.IsScience()&&cmd.pTarget==this&&cmd.HasPointOfUse()&&current&&std::isfinite(distance)&&distance<=.2f*.2f&&std::isfinite(objectDistance)&&std::isfinite(reach)&&radius>0&&objectDistance<=reach*reach;
}
'''
 assert a in s;s=s.replace(a,helper.replace('\n','\r\n')+a)
 a='\r\n    const SLDBPrototype *pLever = theLeverDatabase.levers.GetEntry(GetMaskName());';assert s.count(a)==1
 s=s.replace(a,a+'\r\n    if(ModRuntime::Current() && !pLever){LeverFallback(*this,points,fpUnitRadius);return;}');return s
edit('Game/StuffMapObjects.cpp',stuff)
def unit(s):
 s=s.replace('#include "StdAfx.h"','#include "StdAfx.h"\r\n#include "ModRuntime.h"\r\n#include "StuffMapObjects.h"')
 a='        case RPGMSKL_SCIENCE: {\r\n            CArray<CPlot3D, CPlot3D &> points;';assert a in s
 b='''        case RPGMSKL_SCIENCE: {
            const bool fallback=ModRuntime::Current() && pTarget->WhoAmI()==HPC_LEVER && !static_cast<CLeverObject*>(pTarget)->UsesAuthoredInteraction();
            if(fallback)cmd.bUsePoint=false;
            CArray<CPlot3D, CPlot3D &> points;'''
 s=s.replace(a,b.replace('\n','\r\n'));a='            cmd.pTarget->GetUsePoints(points, lpUnit->GetAttackDistance());';assert s.count(a)==1;s=s.replace(a,a+'\r\n            if(fallback && points.GetSize()==0)return false;');return s
edit('Game/UnitServer.cpp',unit)
# Headless marker/codec checks run inside the same source unit, with state restored.
p=r/'Game/ModRegisteredLoot.inl';s=p.read_text();s+='''
bool JMV_CheckRegisteredLootCodec(){
 auto saved=G_registeredLoot;struct Restore{SRegisteredLootState&s;SRegisteredLootState old;~Restore(){s=std::move(old);}} restore{G_registeredLoot,std::move(saved)};
 G_registeredLoot=SRegisteredLootState();G_registeredLoot.markerPrefix="jmv.registerloot.v1.zone.";
 const std::string spec="sword{iron;1}";std::string decoded,tail;int id=-1;
 if(!HexDecode(HexEncode(spec),decoded)||decoded!=spec||HexDecode("0X",decoded))return false;
 if(!ParseObjectMarker(BuildItemMarker(123,spec),id,tail)||id!=123||tail!="item."+HexEncode(spec))return false;
 if(ParseObjectMarker("jmv.registerloot.v1.other.123.pending",id,tail))return false;
 return MarkerCount(-3)==3&&NormalizeZoneName("Maps/ZONE_1.mob")=="zone_1_mob";
}
''';p.write_text(s,encoding='utf-8')
p=r/'Game/ModRegisteredLoot.h';p.write_text(p.read_text()+'\nbool JMV_CheckRegisteredLootCodec();\n',encoding='utf-8')
edit('Game/ModRuntime.cpp',lambda s:s.replace('            SSCCmd pending;','            require(JMV_CheckRegisteredLootCodec(),"Registered loot marker codec mismatch");\r\n            SSCCmd pending;').replace('return {{"mobWorldCommands"','return {{"registeredLootMarkerCodec",true},{"mobWorldCommands"'))
