#include "ModSafetyLookup.h"

Json CheckSafetyLookupG3(const std::string& directory)
{
    // Plain input views and scalar values only: no mod Host or live game objects.
    std::ifstream input(fs::path(directory)/"oracle.json"); Json oracle; input >> oracle;
    Json report={{"success",true},{"cases",Json::array()},
        {"hostCreated",false},{"worldCreated",false},{"unitCreated",false},
        {"screenCreated",false},{"deviceCreated",false},{"audioStarted",false}};
    struct PlayerView { std::string zone; };
    for (const auto& row : oracle["cases"]) {
        PlayerView view{row["zone"].get<std::string>()};
        int queries=0,zoneQueries=0;
        auto lookup=[&]()->PlayerView* {
            ++queries;
            // A second call would observe removal; one stable snapshot is required.
            return row["found"].get<bool>() && queries==1 ? &view : nullptr;
        };
        auto zone=[&](PlayerView* player) {
            ++zoneQueries;
            return _stricmp(player->zone.c_str(),"gz20g")==0;
        };
        bool final=ModSafetyLookup::FinalBattle(row["ready"].get<bool>(),lookup,zone);
        float night=ModSafetyLookup::NightPower(row["base"].get<float>(),row["cave"].get<bool>(),final);
        report["cases"].push_back({{"final",final},{"night",night},
            {"queries",queries},{"zoneQueries",zoneQueries}});
    }
    return report;
}
