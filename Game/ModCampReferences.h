#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
namespace ModCampReferences {
struct Access;
using Token=uintptr_t;
inline bool Bounded(int count){return count>=0&&count<=10000;}
inline bool MetadataAvailable(int index,int values,int names,bool data){return index>=0&&index<values&&index<names&&data;}
inline std::string OwnerName(const char* name){
 if(!name||!*name)return "<unnamed>";
 size_t length=0;while(length<255&&name[length])++length;return std::string(name,length);
}
template<class Position,class Next,class Visit> bool Walk(int count,Position position,Next next,Visit visit){
 if(!Bounded(count))return false;
 for(int i=0;i<count;++i){if(!position)return false;visit(next(position));}
 return !position;
}
struct View {
 Token script,variables;int scope;Token owner,signature;std::string name;
 bool operator==(const View& v)const{return script==v.script&&variables==v.variables&&scope==v.scope&&owner==v.owner&&signature==v.signature&&name==v.name;}
};
struct Name {bool available;int count;std::string text;};
inline bool Matches(const Name& before,const Name& after){return before.available==after.available&&(before.available?before.text==after.text:before.count==after.count);}
struct ObjectBinding {int index,id;Name name;};
struct GroupBinding {int index;std::vector<int> ids;Name name;};
struct Snapshot {View view;std::vector<ObjectBinding> objects;std::vector<GroupBinding> groups;};
using Pending=std::vector<Snapshot>;
struct Party {int id;Token object;};
inline int PartyId(Token object,const std::vector<Party>& party){if(object)for(const auto& p:party)if(p.object==object)return p.id;return -1;}
inline Token PartyObject(int id,const std::vector<Party>& party){for(const auto& p:party)if(p.id==id)return p.object;return 0;}
inline void Reset(Pending& pending){pending.clear();}
template<class Context> bool Capture(Pending& pending,Context& context,const std::vector<int>& ids){
 Reset(pending);if(ids.empty()||ids.size()>8)return false;
 std::vector<Party> party;for(int id:ids){Token object=context.Resolve(id);if(!object)return false;party.push_back({id,object});}
 std::vector<View> views;if(!context.Collect(views))return false;
 for(const auto& v:views){
  Snapshot snapshot;snapshot.view=v;int oc=context.Count(v,false),gc=context.Count(v,true);
  if(!Bounded(oc)||!Bounded(gc)){Reset(pending);return false;}
  for(int i=0;i<oc;++i){int id=PartyId(context.Object(v,i),party);if(id>=0)snapshot.objects.push_back({i,id,context.VariableName(v,false,i)});}
  for(int i=0;i<gc;++i){
   int mc=context.Members(v,i);if(!Bounded(mc)){Reset(pending);return false;}
   GroupBinding binding;binding.index=i;binding.name=context.VariableName(v,true,i);
   for(int m=0;m<mc;++m){int id=PartyId(context.Member(v,i,m),party);if(id>=0&&std::find(binding.ids.begin(),binding.ids.end(),id)==binding.ids.end())binding.ids.push_back(id);}
   if(!binding.ids.empty())snapshot.groups.push_back(binding);
  }
  if(!snapshot.objects.empty()||!snapshot.groups.empty())pending.push_back(snapshot);
 }
 return true;
}
template<class Context> void Restore(Pending& pending,Context& context){
 if(pending.empty())return;
 std::vector<View> live;if(!context.Collect(live)){Reset(pending);return;}
 std::vector<Party> party;
 auto resolve=[&](int id){if(PartyObject(id,party))return;Token object=context.Resolve(id);if(object)party.push_back({id,object});};
 for(const auto& s:pending){for(const auto& o:s.objects)resolve(o.id);for(const auto& g:s.groups)for(int id:g.ids)resolve(id);}
 for(const auto& s:pending){
  if(std::find(live.begin(),live.end(),s.view)==live.end())continue;
  const View& v=s.view;int oc=context.Count(v,false),gc=context.Count(v,true);if(!Bounded(oc)||!Bounded(gc))continue;
  for(const auto& o:s.objects){
   if(o.index<0||o.index>=oc||!Matches(o.name,context.VariableName(v,false,o.index)))continue;
   Token replacement=PartyObject(o.id,party);if(replacement&&!context.Object(v,o.index))context.Set(v,o.index,replacement);
  }
  for(const auto& g:s.groups){
   if(g.index<0||g.index>=gc||!Matches(g.name,context.VariableName(v,true,g.index))||!Bounded(context.Members(v,g.index)))continue;
   for(int id:g.ids){
    Token replacement=PartyObject(id,party);if(!replacement)continue;bool present=false;
    for(int m=0;m<context.Members(v,g.index);++m)if(context.Member(v,g.index,m)==replacement){present=true;break;}
    if(!present)context.Add(v,g.index,replacement);
   }
  }
 }
 Reset(pending);
}
}
