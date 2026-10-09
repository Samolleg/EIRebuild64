#include "ModSpellTargeting.h"
Json CheckSpellTargetingG3(const std::string& directory) {
    std::ifstream file(fs::path(directory)/"oracle.json");Json input;file>>input;
    Json out={{"success",true},{"classification",Json::array()},{"preview",Json::array()},
        {"mesh",Json::array()},{"dispatch",Json::array()},{"hostCreated",false},{"worldCreated",false},
        {"unitCreated",false},{"screenCreated",false},{"deviceCreated",false},{"audioStarted",false}};
    using namespace ModSpellTargeting;
    for(const auto& row:input["classification"])out["classification"].push_back(Offensive(row["id"].get<int>(),row["self"].get<bool>(),row["particles"].get<bool>()));
    auto point=[](const Json& p){return Point{p[0].get<float>(),p[1].get<float>(),p[2].get<float>()};};
    for(const auto& row:input["preview"]) {
        Point center=PreviewCenter(row["hover"][0],row["hover"][1],row["hover"][2],point(row["unit"]),point(row["ground"]));
        std::array<bool,8> flags;for(int i=0;i<8;++i)flags[i]=row["flags"][i];
        out["preview"].push_back({{"visible",PreviewSelected(flags,row["slot"].get<int>(),row["radius"].get<float>(),center)},{"center",{center.x,center.y,center.z}}});
    }
    for(const auto& row:input["mesh"]) {
        Point center={row["center"][0],row["center"][1],0};
        const float a=row["slope"][0],b=row["slope"][1],c=row["slope"][2];int heights=0;
        auto ring=AreaRing(center,row["radius"].get<float>(),[&](float x,float y){++heights;return a*x+b*y+c;});
        Json vertices=Json::array(),indices=Json::array();for(auto v:ring.vertices)vertices.push_back({v.x,v.y,v.z,v.w});
        for(auto index:ring.indices)indices.push_back(index);
        if(heights!=64)throw std::runtime_error("Preview must sample every bridge vertex");
        out["mesh"].push_back({{"vertices",vertices},{"indices",indices}});
    }
    for(const auto& row:input["dispatch"]) {
        Json commands=Json::array();
        auto send=[&](const char* route){commands.push_back({{"caster",row["caster"]},{"target",row["target"]},{"slot",row["slot"]},{"fast",false},{"route",route}});};
        ActiveSelfCast(row["point"].get<bool>(),[&](){send("unit");},[&](){send("point");});
        out["dispatch"].push_back(commands);
    }
    return out;
}
