#include "ModCampEquipment.h"

Json CheckCampEquipmentG3(const std::string& directory)
{
    std::ifstream file(fs::path(directory)/"oracle.json");Json input;file>>input;
    Json out={{"success",true},{"shield",Json::array()},{"shell",Json::array()},
        {"quick",Json::array()},{"primaries",Json::array()},{"pose",Json::array()},
        {"clear",Json::array()},{"hostCreated",false},{"worldCreated",false},
        {"unitCreated",false},{"screenCreated",false},{"deviceCreated",false},{"audioStarted",false}};
    for(const auto& row:input["shield"]){std::vector<std::pair<int,int>> items;int i=0;
        for(const auto& type:row["shapes"])items.emplace_back(i++,type.get<int>());
        Json removed=Json::array();ModCampEquipment::ReplaceShields(row["incoming"].get<int>()==9,
            [&](){return int(items.size());},[&](int n){return items[n].second==9;},
            [&](int n){removed.push_back(items[n].first);items.erase(items.begin()+n);});
        Json kept=Json::array();for(auto v:items)kept.push_back(v.first);
        out["shield"].push_back({{"kept",kept},{"removed",removed},{"allowed",items.size()<4}});}
    for(const auto& row:input["shell"]){Json names=Json::array();
        ModCampEquipment::ArmorShells(row["type"].get<int>(),row["tti"].get<int>(),[&](const std::string& name){names.push_back(name);});
        out["shell"].push_back(names);}
    ModCampEquipment::ClearShells([&](const std::string& name){out["clear"].push_back(name);});
    struct Ref {int id=0,refs=0,adds=0,releases=0;void AddRef(){++refs;++adds;}void Release(){--refs;++releases;}};
    for(const auto& row:input["quick"]){Ref items[5];for(int i=0;i<5;++i)items[i].id=i;
        Ref* slots[4]={};for(int i=0;i<4;++i){int id=row["initial"][i].get<int>();if(id>=0){slots[i]=&items[id];++items[id].refs;}}
        Json actual;if(row["kind"]=="add")actual["index"]=ModCampEquipment::AddQuick(slots,4,&items[row["item"].get<int>()]);
        else actual["changed"]=ModCampEquipment::RemoveQuick(slots,4,row["index"].get<int>());
        actual["slots"]=Json::array();for(auto slot:slots)actual["slots"].push_back(slot?slot->id:-1);
        int adds=0,releases=0;for(auto& item:items){adds+=item.adds;releases+=item.releases;}
        actual["adds"]=adds;actual["releases"]=releases;out["quick"].push_back(actual);
        for(auto& item:items){int owners=0;for(auto slot:slots)if(slot==&item)++owners;if(item.refs!=owners)throw std::runtime_error("Quick reference ownership imbalance");}}
    for(const auto& row:input["primaries"]){ModCampEquipment::PrimarySnapshot snapshot(row["values"][0].get<float>(),row["values"][1].get<float>(),row["values"][2].get<float>());
        float s=999,d=-999,i=0.5f;snapshot.Restore(s,d,i);out["primaries"].push_back({s,d,i});}
    for(const auto& row:input["pose"])out["pose"].push_back(ModCampEquipment::IdlePose(row["type"].get<int>(),row["twohanded"].get<bool>()));
    return out;
}
