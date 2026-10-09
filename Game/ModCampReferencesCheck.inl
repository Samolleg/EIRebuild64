#include "ModCampReferences.h"
namespace {
using CampView=ModCampReferences::View;
using CampToken=ModCampReferences::Token;
CampView ReadCampView(const Json& id){return {id[0].get<CampToken>(),id[1].get<CampToken>(),id[2].get<int>(),id[3].get<CampToken>(),id[4].get<CampToken>(),id[5].get<std::string>()};}
struct CampFixture {
 Json event,views,writes=Json::array();
 bool Collect(std::vector<CampView>& result){result.clear();for(const auto& v:views)result.push_back(ReadCampView(v["identity"]));return event.value("collect",true);}
 CampToken Resolve(int id){for(const auto& p:event["party"])if(p[0]==id)return p[2].get<bool>()?p[1].get<CampToken>():0;return 0;}
 Json& Vars(const CampView& view){for(auto& v:views)if(v["identity"][1]==view.variables)return v;throw std::runtime_error("fixture variable set absent");}
 int Count(const CampView& v,bool group){auto& row=Vars(v);return row.value(group?"gc":"oc",static_cast<int>(row[group?"groups":"objects"].size()));}
 ModCampReferences::Name VariableName(const CampView& v,bool group,int index){
  auto& names=Vars(v)[group?"gnames":"onames"];int count=Count(v,group);
  bool available=ModCampReferences::MetadataAvailable(index,count,static_cast<int>(names.size()),!names.empty());
  return {available,count,available?names[index].get<std::string>():""};
 }
 CampToken Object(const CampView& v,int index){return Vars(v)["objects"][index].get<CampToken>();}
 int Members(const CampView& v,int index){return static_cast<int>(Vars(v)["groups"][index].size());}
 CampToken Member(const CampView& v,int index,int member){return Vars(v)["groups"][index][member].get<CampToken>();}
 void Set(const CampView& v,int index,CampToken object){auto& row=Vars(v);row["objects"][index]=object;writes.push_back({row["identity"],"o",index,object});}
 void Add(const CampView& v,int index,CampToken object){auto& row=Vars(v);auto& members=row["groups"][index];members.push_back(object);std::sort(members.begin(),members.end());writes.push_back({row["identity"],"g",index,object});}
};
}
Json CheckCampReferencesG3(const std::string& directory){
 std::ifstream file(fs::path(directory)/"oracle.json");Json input;file>>input;
 Json output={{"success",true},{"cases",Json::array()},{"lists",Json::array()},{"owner_names",Json::array()},
 {"hostCreated",false},{"worldCreated",false},{"unitCreated",false},{"screenCreated",false},{"deviceCreated",false},{"audioStarted",false},{"saveRead",false},{"scriptsRun",false}};
 for(const auto& c:input["cases"]){
  ModCampReferences::Pending pending;Json rows=Json::array();
  for(const auto& event:c["events"]){
   CampFixture context{event,event.value("views",Json::array())};std::string op=event["op"];Json captured=nullptr;
   if(op=="reset")ModCampReferences::Reset(pending);
   else if(op=="capture")captured=ModCampReferences::Capture(pending,context,event["ids"].get<std::vector<int>>());
   else if(op=="restore")ModCampReferences::Restore(pending,context);
   rows.push_back({{"captured",captured},{"pending",pending.size()},{"views",context.views},{"writes",context.writes}});
  }
  output["cases"].push_back(rows);
 }
 for(const auto& row:input["lists"]){
  int length=row["length"],position=length?1:0;
  output["lists"].push_back(ModCampReferences::Walk(row["count"].get<int>(),position,[&](int& p){int current=p;p=p<length?p+1:0;return current;},[](int){}));
 }
 for(const auto& row:input["owner_names"]){std::string value=row["value"];output["owner_names"].push_back(ModCampReferences::OwnerName(value.c_str()));}
 return output;
}
