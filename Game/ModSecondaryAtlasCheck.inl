#include "ModSecondaryAtlas.h"
#include "BaseManager.h"
#include "Texture.h"

// Actual CTexture/CGameBase refs; the local manager observes release without
// allocating a world, resource surface or renderer. GPU readiness is an adapter.
struct SecondaryAtlasCheckManager : CBaseManager {
    std::vector<LPGAMEBASE> released;
    LPCSTR GetGameClassName() const override {return "SecondaryAtlasCheckManager";}
    EClassIDs WhoAmI() const override {return HPC_TEXTUREMANAGER;}
    LPGAMEBASE CreateInstant(ETypeIDs) override {return nullptr;}
    void Release(LPGAMEBASE value) override {released.push_back(value);}
};
Json CheckSecondaryAtlasG3(const std::string& directory) {
    std::ifstream file(fs::path(directory)/"oracle.json");Json input;file>>input;
    if(input.at("cases").size()!=5)throw std::runtime_error("Expected five secondary atlas representatives");
    auto decode=[](const std::string& hex){std::string out;for(size_t i=0;i<hex.size();i+=2)out.push_back(char(std::stoul(hex.substr(i,2),nullptr,16)));return out;};
    std::vector<std::string> recipes,names;
    Json encoded=Json::array();
    for(const auto& row:input.at("recipes")) {
        auto recipe=ModSecondaryAtlas::Recipe(row["size"].get<unsigned>(),row["mips"].get<unsigned>(),
            row["background"].get<unsigned>(),row["format"].get<unsigned>(),decode(row["data_hex"]),
            row["layers"].get<std::vector<std::string>>(),row["slots"].get<std::array<int,4>>());
        recipes.push_back(recipe);names.push_back(ModSecondaryAtlas::Name(recipe));
        const char* digits="0123456789abcdef";std::string hex;
        for(unsigned char value:recipe){hex+=digits[value>>4];hex+=digits[value&15];}
        encoded.push_back({{"recipe_hex",hex},{"name",names.back()}});
    }
    Json cases=Json::array();
    auto acquire=[](CTexture* p){p->AddRef();};auto release=[](CTexture* p){p->Release();};
    auto init=[](CTexture& p,SecondaryAtlasCheckManager& manager,const std::string& name){p.SetManager(&manager);p.SetTypeID(TTID_UNIT_WEAPON);p.SetName(name.c_str());};
    auto swap=[](CTexture*& owner,CTexture* candidate){CTexture* old=owner;owner=candidate;candidate->AddRef();old->Release();};
    {
        SecondaryAtlasCheckManager manager;CTexture s;init(s,manager,input["recipes"][0]["expected_name"]);
        CTexture* a=&s;CTexture* b=&s;a->AddRef();b->AddRef();
        ModSecondaryAtlas::Registry registry;std::string first,second;registry.Resolve(recipes[0],first);registry.Resolve(recipes[0],second);
        int compositions=0;
        if(!ModSecondaryAtlas::ReuseLoaded<CTexture>([&](){return &s;},[](CTexture*){return true;},acquire,release))++compositions;
        auto bind=[&](CTexture*& owner){return ModSecondaryAtlas::Rebind(owner,[&](){return &s;},[](CTexture*){return true;},[](CTexture*){return true;},[&](CTexture* p){swap(owner,p);},[&](){return owner;},acquire,release);};
        if(!bind(a)||!bind(b))throw std::runtime_error("Same provider rebind failed");
        cases.push_back({{"id","same_recipe_different_units"},{"nameA",first},{"nameB",second},{"sRefs",s.GetRefCount()},{"compositions",compositions}});
        a->Release();b->Release();
    }
    {
        SecondaryAtlasCheckManager manager;CTexture s,t;init(s,manager,names[0]);init(t,manager,names[1]);
        CTexture* a=&s;CTexture* b=&s;a->AddRef();b->AddRef();
        const bool bound=ModSecondaryAtlas::Rebind(a,[&](){return &t;},[](CTexture*){return true;},[](CTexture*){return true;},[&](CTexture* p){swap(a,p);},[&](){return a;},acquire,release);
        if(!bound)throw std::runtime_error("Changed provider rebind failed");
        cases.push_back({{"id","one_unit_equipment_change"},{"nameA",std::string(a->GetName())},{"nameB",std::string(b->GetName())},{"sName",std::string(s.GetName())},{"sRefs",s.GetRefCount()},{"tRefs",t.GetRefCount()}});
        a->Release();b->Release();
    }
    {
        ModSecondaryAtlas::Registry registry;std::string a,c;registry.Resolve(recipes[0],a);registry.Resolve(recipes[2],c);
        cases.push_back({{"id","slot_mapping_is_identity"},{"nameA",a},{"nameC",c},{"distinct",a!=c}});
    }
    {
        SecondaryAtlasCheckManager manager;CTexture s,failed;init(s,manager,names[0]);init(failed,manager,names[1]);
        CTexture* a=&s;a->AddRef();std::string ownerName=names[0];
        const bool bound=ModSecondaryAtlas::Rebind(a,[&](){return &failed;},[](CTexture*){return false;},[](CTexture*){return true;},[&](CTexture* p){swap(a,p);},[&](){return a;},acquire,release);
        if(bound)ownerName=names[1];
        cases.push_back({{"id","provider_load_failure"},{"committed",bound},{"nameA",ownerName},{"sName",std::string(s.GetName())},{"sRefs",s.GetRefCount()},{"failedRefs",failed.GetRefCount()},{"releasedFailed",std::find(manager.released.begin(),manager.released.end(),&failed)!=manager.released.end()}});
        a->Release();
    }
    {
        ModSecondaryAtlas::Registry registry;
        // Bulk fixture state for ONE capacity observation, not 1024 test cases.
        for(size_t i=0;i<ModSecondaryAtlas::Registry::Limit;++i)registry.recipes.emplace("fixture"+std::to_string(i),"occupied");
        std::string out;const bool capacityRejected=!registry.Resolve(recipes[0],out);
        registry.Clear();registry.recipes.emplace(names[0],"unequal recipe bytes");
        const bool collisionRejected=!registry.Resolve(recipes[0],out);
        registry.Clear();const auto afterClear=registry.recipes.size();registry.Resolve(recipes[0],out);
        cases.push_back({{"id","recipe_registry_limit_or_collision"},{"capacityRejected",capacityRejected},{"collisionRejected",collisionRejected},{"fallback","baseline"},{"afterClear",afterClear},{"afterClearName",out}});
    }
    return {{"success",true},{"cases",cases},{"recipes",encoded},{"actualTextureRefs",true},
        {"hostCreated",false},{"worldCreated",false},{"unitCreated",false},{"screenCreated",false},
        {"deviceCreated",false},{"gpuLoads",0},{"audioStarted",false},{"saveRead",false}};
}
