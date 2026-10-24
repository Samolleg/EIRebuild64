#include "ModCampReferences.h"
namespace ModCampReferences {
// Typed traversal replaces native layout offsets. No engine object ownership is
// retained by Pending; engine SetObject/Add own the actual reference writes.
struct Access {
 static bool Append(CScript& script,std::vector<View>& views){
  Token token=reinterpret_cast<Token>(&script);
  views.push_back({token,reinterpret_cast<Token>(&script.globalVars),0,token,token,"<globals>"});
  if(!Walk(script.runningScripts.GetCount(),script.runningScripts.GetHeadPosition(),
    [&](POSITION& p)->SOneScript&{return script.runningScripts.GetNext(p);},[&](SOneScript& one){
     const char* name=one.szScriptName;
     views.push_back({token,reinterpret_cast<Token>(&one.vars),1,reinterpret_cast<Token>(&one),reinterpret_cast<Token>(name),OwnerName(name)});
    }))return false;
  return Walk(script.runningThreads.GetCount(),script.runningThreads.GetHeadPosition(),
    [&](POSITION& p)->CScriptThreadState&{return script.runningThreads.GetNext(p);},[&](CScriptThreadState& thread){
     views.push_back({token,reinterpret_cast<Token>(&thread.variables),2,reinterpret_cast<Token>(&thread),reinterpret_cast<Token>(thread.pThread),thread.pLinkedOneScript?OwnerName(thread.pLinkedOneScript->szScriptName):"<unlinked-thread>"});
    });
 }
};
struct WorldAccess {
 CWorldServer& world;
 bool Collect(std::vector<View>& views){views.clear();return Access::Append(world.script,views)&&Access::Append(world.scriptTemp,views);}
 Token Resolve(int id){CUnit* unit=world.GetUnit(id);return unit&&!unit->IsInvalid()?reinterpret_cast<Token>(static_cast<CMapObject*>(unit)):0;}
 static SScriptVariablesSet& Vars(const View& view){return *reinterpret_cast<SScriptVariablesSet*>(view.variables);}
 int Count(const View& v,bool group){return group?Vars(v).gVars.GetSize():Vars(v).oVars.GetSize();}
 template<class T> Name ReadName(CScriptVars<T>& vars,int index){const char* text=nullptr;bool available=vars.TryGetVarName(index,text);return {available,vars.GetSize(),available&&text?text:""};}
 Name VariableName(const View& v,bool group,int index){return group?ReadName(Vars(v).gVars,index):ReadName(Vars(v).oVars,index);}
 Token Object(const View& v,int index){return reinterpret_cast<Token>(Vars(v).oVars[index].GetObject());}
 int Members(const View& v,int index){return Vars(v).gVars[index].GetSize();}
 Token Member(const View& v,int index,int member){return reinterpret_cast<Token>(Vars(v).gVars[index].GetAt(member).GetObject());}
 void Set(const View& v,int index,Token object){Vars(v).oVars[index].SetObject(reinterpret_cast<CMapObject*>(object));}
 void Add(const View& v,int index,Token object){Vars(v).gVars[index].Add(reinterpret_cast<CMapObject*>(object));}
};
inline bool CaptureWorld(Pending& pending,CWorldServer& world,const std::vector<int>& ids){WorldAccess context{world};return Capture(pending,context,ids);}
inline void RestoreWorld(Pending& pending,CWorldServer& world){WorldAccess context{world};Restore(pending,context);}
}
