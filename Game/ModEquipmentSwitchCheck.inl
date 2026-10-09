#include "ModEquipmentSwitch.h"

Json CheckEquipmentSwitchG3(const std::string& directory)
{
    std::ifstream file(fs::path(directory)/"oracle.json");Json oracle;file>>oracle;
    Json report={{"success",true},{"cases",Json::array()},
        {"hostCreated",false},{"worldCreated",false},{"unitCreated",false},
        {"screenCreated",false},{"deviceCreated",false},{"audioStarted",false}};
    for (const auto& row:oracle["cases"]) {
        ModEquipmentSwitch::Cache cache;
        for (const auto& seed:row["initial"])
            cache.emplace(seed["id"].get<int>(),ModEquipmentSwitch::State{seed["changed"].get<bool>(),seed["time"].get<int>()});
        Json steps=Json::array();
        for (const auto& op:row["operations"]) {
            const int id=op["id"].get<int>();
            const bool active=op["mod"].get<bool>()&&op["attacks"].get<bool>();
            bool call=false;
            if (op["kind"]=="load") ModEquipmentSwitch::AfterLoad(cache,id,active);
            else if (op["kind"]=="stock") call=op["current"].get<unsigned>()!=op["wish"].get<unsigned>();
            else call=ModEquipmentSwitch::BeforeAttack(cache,id,op["current"].get<unsigned>(),op["wish"].get<unsigned>(),active,op["noDelay"].get<bool>());
            std::vector<int> ids;for(const auto& item:cache)ids.push_back(item.first);std::sort(ids.begin(),ids.end());
            Json state=Json::array();for(int key:ids){const auto& value=cache.at(key);state.push_back({{"id",key},{"changed",value.changed},{"time",value.time}});}
            steps.push_back({{"call",call},{"cache",state}});
        }
        report["cases"].push_back({{"name",row["name"]},{"steps",steps}});
    }
    return report;
}
