#include "ModHudAssignment.h"
Json CheckHudAssignmentG3(const std::string& directory){
    ModRuntime::Host host(directory);auto* package=ModRuntime::Current();std::string owner;
    const auto base=(fs::path(directory).parent_path().parent_path()/"Base/res/database.res").string();
    package->ReadResource("database.res","items.idb",base,owner);
    if(!theItemDatabase.Load("",owner.c_str()))throw std::runtime_error("HUD item DB unavailable");
    Json report={{"success",true},{"luaMainExecuted",true},{"worldCreated",false},
        {"unitCreated",false},{"screenCreated",false},{"deviceCreated",false},{"audioStarted",false},
        {"gameplayActivated",false},{"weaponEnabled",package->CombatSetting("useLS_SetUnitW")},
        {"extended",package->CombatSetting("JMVExtendedSpellSlots")},
        {"hd",package->CombatSetting("JMVGraphics2")},{"weapons",Json::array()},
        {"models",Json::array()},{"hotkeys",Json::array()},{"layouts",Json::array()}};
    int checks=0;auto require=[&](bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);};
    for(int i=0;i<theItemDatabase.weapons.GetNumEntries();++i){const auto* shape=theItemDatabase.weapons[i];
        report["weapons"].push_back({{"id",i},{"type",shape->dwTypeID},{"texture",shape->dwTextureTypeIndex},
            {"model",std::string((LPCSTR)ModHudAssignment::WeaponModel(*shape))}});
    }
    for(int type=0;type<=int(RPGWST_MAGIC);++type)for(int texture=0;texture<16;++texture){
        SIDBWeaponShape shape={};shape.dwTypeID=DWORD(type);shape.dwTextureTypeIndex=DWORD(texture);
        report["models"].push_back({{"type",type},{"texture",texture},
            {"model",std::string((LPCSTR)ModHudAssignment::WeaponModel(shape))}});
    }
    for(int slot=-1;slot<=16;++slot){CString name="spell";ModHudAssignment::AppendSpellHotkey(name,slot);
        const char*key=ModHudAssignment::SpellHotkey(slot);report["hotkeys"].push_back({{"slot",slot},
            {"key",key?Json(key):Json(nullptr)},{"name",std::string((LPCSTR)name)}});
    }
    for(bool extended:{false,true})for(bool hd:{false,true}){
        require(ModHudAssignment::SpellSlots(extended)==(extended?16:8),"HUD presentation slot count");
        for(int count=1;count<=ModHudAssignment::SpellSlots(extended);++count)for(int slot=0;slot<count;++slot){
            auto p=ModHudAssignment::SpellPlacement(slot,count,extended,hd);
            require(p.x>=0&&p.x+p.size<=p.canvas&&p.y>=0&&p.y+p.size<=p.canvas,"HUD atlas outside canvas");
            report["layouts"].push_back({{"extended",extended},{"hd",hd},{"count",count},{"slot",slot},
                {"left",p.left},{"top",p.top},{"x",p.x},{"y",p.y},{"size",p.size},{"canvas",p.canvas}});
        }
    }
    require(!theWorldServer.IsMapLoaded(),"HUD inspection loaded world");report["assertions"]=checks;return report;
}
