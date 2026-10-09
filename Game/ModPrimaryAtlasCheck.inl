#include "ModPrimaryAtlas.h"

Json CheckPrimaryAtlasG3(const std::string& directory) {
    std::ifstream file(fs::path(directory)/"oracle.json");Json input;file>>input;
    if(input.at("cases").size()!=4)throw std::runtime_error("Expected four primary atlas representatives");
    const auto& row=input.at("recipe");std::string data;
    const auto hexData=row["data_hex"].get<std::string>();
    for(size_t i=0;i<hexData.size();i+=2)data.push_back(char(std::stoul(hexData.substr(i,2),nullptr,16)));
    const auto recipe=ModSecondaryAtlas::Recipe(row["size"].get<unsigned>(),row["mips"].get<unsigned>(),
        row["background"].get<unsigned>(),row["format"].get<unsigned>(),data,
        row["layers"].get<std::vector<std::string>>(),row["slots"].get<std::array<int,4>>());
    const auto canonical=ModSecondaryAtlas::Name(recipe,true);
    const auto privateName=ModPrimaryAtlas::PrivateName(row["unit_id"].get<unsigned>(),canonical);
    std::string hex;const char* digits="0123456789abcdef";
    for(unsigned char value:recipe){hex+=digits[value>>4];hex+=digits[value&15];}
    Json cases=Json::array();
    auto acquire=[](CTexture* p){p->AddRef();};auto release=[](CTexture* p){p->Release();};
    auto init=[](CTexture& p,SecondaryAtlasCheckManager& manager,const std::string& name){p.SetManager(&manager);p.SetTypeID(TTID_UNIT_MAIN);p.SetName(name.c_str());};
    auto swap=[](CTexture*& owner,CTexture* candidate){auto* old=owner;owner=candidate;candidate->AddRef();old->Release();};
    {
        SecondaryAtlasCheckManager manager;CTexture s,t;init(s,manager,canonical);init(t,manager,privateName);
        CTexture* a=&s;CTexture* b=&s;a->AddRef();b->AddRef();ModPrimaryAtlas::Aliases aliases;
        std::unordered_map<CTexture*,int> paint{{&s,0},{&t,0}};
        const bool detached=ModPrimaryAtlas::EnsurePrivate(aliases,42,a->GetName(),[](){return true;},[&](const char*){
            return ModSecondaryAtlas::Rebind(a,[&](){return &t;},[](CTexture*){return true;},[](CTexture*){return true;},
                [&](CTexture* p){swap(a,p);},[&](){return a;},acquire,release);
        });
        if(!detached)throw std::runtime_error("Private detach failed");
        paint[a]=7;std::string source;if(!aliases.Source(a->GetName(),source))throw std::runtime_error("Private source missing");
        cases.push_back({{"id","shared_primary_then_mouth_copy_on_write"},{"nameA",std::string(a->GetName())},{"nameB",std::string(b->GetName())},
            {"sName",std::string(s.GetName())},{"sRefs",s.GetRefCount()},{"privateRefs",t.GetRefCount()},{"aliasSource",source},{"paintA",paint[a]},{"paintB",paint[b]}});
        a->Release();b->Release();
    }
    {
        SecondaryAtlasCheckManager manager;CTexture s,failed;init(s,manager,canonical);init(failed,manager,privateName);
        CTexture* a=&s;a->AddRef();ModPrimaryAtlas::Aliases aliases;
        const bool detached=ModPrimaryAtlas::EnsurePrivate(aliases,42,a->GetName(),[](){return true;},[&](const char*){
            return ModSecondaryAtlas::Rebind(a,[&](){return &failed;},[](CTexture*){return false;},[](CTexture*){return true;},
                [&](CTexture* p){swap(a,p);},[&](){return a;},acquire,release);
        });
        cases.push_back({{"id","private_provider_load_failure"},{"committed",detached},{"nameA",std::string(a->GetName())},
            {"sRefs",s.GetRefCount()},{"failedRefs",failed.GetRefCount()},{"aliases",aliases.entries.size()},{"paintB",0}});
        a->Release();
    }
    {
        SecondaryAtlasCheckManager manager;CTexture old,s,wound,secondary;init(old,manager,privateName);init(s,manager,canonical);
        init(wound,manager,"mo42w");init(secondary,manager,"secondary");secondary.SetTypeID(TTID_UNIT_WEAPON);
        CTexture* a=&old;CTexture* w=&wound;CTexture* secondaryOwner=&secondary;a->AddRef();w->AddRef();
        ModPrimaryAtlas::Aliases aliases;aliases.Add(privateName,canonical);
        const bool bound=ModSecondaryAtlas::Rebind(a,[&](){return &s;},[](CTexture*){return true;},[](CTexture*){return true;},
            [&](CTexture* p){swap(a,p);},[&](){return a;},acquire,release);
        if(bound)aliases.Remove(privateName.c_str());
        cases.push_back({{"id","primary_refresh_retains_wound_overlay"},{"committed",bound},{"nameA",std::string(a->GetName())},
            {"sRefs",s.GetRefCount()},{"oldRefs",old.GetRefCount()},{"aliases",aliases.entries.size()},
            {"woundRefs",w->GetRefCount()},{"woundName",std::string(w->GetName())},{"secondaryUnchanged",secondaryOwner==&secondary}});
        a->Release();w->Release();
    }
    {
        ModPrimaryAtlas::Aliases aliases;for(size_t i=0;i<ModPrimaryAtlas::Aliases::Limit;++i)aliases.entries.emplace("fixture"+std::to_string(i),"occupied");
        const bool capacityRejected=!aliases.Add(privateName,canonical);aliases.Clear();aliases.Add(privateName,canonical);
        const bool mismatchRejected=!aliases.Add(privateName,"other canonical");std::string source;
        const bool missingAliasRejected=!aliases.Source("jmvatmMissing",source);
        ModSecondaryAtlas::Registry registry;std::string primary,secondary;registry.Resolve(recipe,primary,true);registry.Resolve(recipe,secondary);
        const auto namespaceCount=registry.recipes.size();aliases.Clear();registry.Clear();
        cases.push_back({{"id","bounded_alias_and_shared_recipe_reset"},{"capacityRejected",capacityRejected},{"mismatchRejected",mismatchRejected},
            {"missingAliasRejected",missingAliasRejected},{"primaryDistinctFromSecondary",primary!=secondary},{"recipeNamespaceCount",namespaceCount},
            {"afterAliasClear",aliases.entries.size()},{"afterRecipeClear",registry.recipes.size()}});
    }
    return {{"success",true},{"cases",cases},{"recipe_hex",hex},{"canonical",canonical},{"private",privateName},{"actualTextureRefs",true},
        {"hostCreated",false},{"worldCreated",false},{"unitCreated",false},{"screenCreated",false},{"deviceCreated",false},
        {"gpuLoads",0},{"audioStarted",false},{"saveRead",false}};
}
