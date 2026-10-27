#include "../stdafx.h"
#include "WorldServer.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "ModCliTool.h"
#include "GeneralSettings.h"
#include "PerkDatabase.h"
#include "QuestDatabase.h"
#include "UnitDatabase.h"
#include "SpellDatabase.h"
#include "ItemDatabase.h"
#include "LeverDatabase.h"
#include "AckDatabase.h"
#include "TerrainPrintsDatabase.h"
#include "GlobalMap.h"
#include "TextManager.h"
#include "2DScreenWait.h"
#include <filesystem>
#include <map>
#include <set>
#include <chrono>
#include <cstdio>

namespace {
using Clock=std::chrono::steady_clock;
long long Milliseconds(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count();
}
std::string Extension(std::string name,const char* extension) {
    return std::filesystem::path(name).replace_extension(extension).string();
}
}
int wmain(int argc,wchar_t** argv) {
    // This executable has no renderer/audio/window/session initialization.
    // Flush stdout after every map/profile so the mapper can show progress.
    std::map<std::wstring,std::filesystem::path> options;
    bool all=false;
    for(int i=1;i<argc;++i) {
        const std::wstring name=argv[i];
        if(name==L"--all"){if(all)return 2;all=true;continue;}
        if(name!=L"--game-root"&&name!=L"--mod"&&name!=L"--map"&&name!=L"--mob"&&name!=L"--output-root")return 2;
        if(i+1>=argc||options.count(name))return 2;
        options[name]=argv[++i];
    }
    if(!options.count(L"--game-root")||!options.count(L"--mod")||!options.count(L"--output-root")||
        all==bool(options.count(L"--map"))||(all&&options.count(L"--mob"))) {
        fprintf(stderr,"Usage: EIRebuild64NavBake --game-root DIR --mod DIR (--all | --map NAME [--mob NAME]) --output-root DIR\n");
        return 2;
    }
    ModCliTool::Scope tool(ModCliTool::Kind::NavBake);
    int exitCode=3;
    try {
        const auto root=std::filesystem::absolute(options.at(L"--game-root")).lexically_normal();
        auto mod=options.at(L"--mod");if(mod.is_relative())mod=root/mod;
        const auto output=std::filesystem::absolute(options.at(L"--output-root")).lexically_normal()/"Maps"/"Nav64";
        const auto base=std::filesystem::is_directory(root/"Base"/"Res")?root/"Base":root;
        CModPackage package;package.Load(mod.string());
        ModRuntime::ResourceScope resources(package);
        CWorld::InitCurrentDir((base.string()+"\\").c_str());
        const auto settings=std::filesystem::is_regular_file(root/"Config64"/"settings.ini")?root/"Config64"/"settings.ini":base/"config"/"settings.ini";
        theSettings.ReadSettingsFromFile(settings.string().c_str());
        theSettings.szWorkPath=(base.string()+"\\").c_str();
        theSettings.szResourcePath=((base/"Res").string()+"\\").c_str();
        theSettings.szCDPath=theSettings.szWorkPath;
        C2DScreenWait::Ignore(true);
        const auto* res=static_cast<LPCSTR>(theSettings.szResourcePath);
        if(!thePerkDatabase.LoadDatabase(res,"database.res")||!theQuestDatabase.Load(res)||
            !theUnitDatabase.Load(res,"database.res")||!theSpellDatabase.Load(res,"database.res")||
            !theItemDatabase.Load(res,"database.res")||!theLeverDatabase.Load(res)||
            !theAckDatabase.Load(res)||!theTPDatabase.Load(res))
            throw std::runtime_error("Cannot load effective game databases");
        std::set<std::pair<std::string,std::string>> maps;
        if(all) {
            CTextManager texts;texts.SelectResource((std::string(res)+"texts.res").c_str());
            CGlobalMap registry;
            if(!registry.LoadZones("map.txt",texts))throw std::runtime_error("Cannot load mod map.txt registrations");
            for(int i=0;i<registry.GetZoneNum();++i) {
                const auto* zone=registry.GetZone(i);
                if(zone->szTerrainName.IsEmpty()||zone->szMobName.IsEmpty())continue;
                maps.emplace(Extension((LPCSTR)zone->szTerrainName,".mpr"),Extension((LPCSTR)zone->szMobName,".mob"));
                // Both authored terrain states selected by WorldInterface.
                if(zone->szName.CompareNoCase("gz6g")==0) {
                    maps.emplace("zone6.mpr",Extension((LPCSTR)zone->szMobName,".mob"));
                    maps.emplace("zone6_2.mpr",Extension((LPCSTR)zone->szMobName,".mob"));
                }
            }
            maps.emplace("ZoneMainMenuNew.mpr","ZoneMainMenuNew.mob");
        } else {
            const auto map=Extension(options.at(L"--map").string(),".mpr");
            maps.emplace(map,options.count(L"--mob")?Extension(options.at(L"--mob").string(),".mob"):Extension(map,".mob"));
        }
        const auto total=Clock::now();unsigned completed=0,errors=0,missing=0;std::uint64_t totalBytes=0;
        exitCode=4;
        printf("BAKE_BEGIN maps=%zu output=%s algorithm=%u\n",maps.size(),output.string().c_str(),Navigation::AlgorithmVersion);fflush(stdout);
        for(const auto& map:maps) {
            const auto start=Clock::now();
            printf("MAP_BEGIN mpr=%s mob=%s\n",map.first.c_str(),map.second.c_str());fflush(stdout);
            try {
                CWorldServer world(true);SavedWorldScope worldScope(&world);
                struct DestroyWorld {CWorldServer& world;~DestroyWorld(){world.Destroy();}} cleanup{world};
                const bool haveMpr=std::filesystem::is_regular_file(ModRuntime::MapPath(world.GetFullMobName(map.first.c_str())));
                const bool haveMob=std::filesystem::is_regular_file(ModRuntime::MapPath(world.GetFullMobName(map.second.c_str())));
                if(!haveMpr||!haveMob) {
                    ++missing;
                    fprintf(stderr,"MAP_MISSING mpr=%s mob=%s code=5 mpr_present=%d mob_present=%d reason=no_sources\n",
                        map.first.c_str(),map.second.c_str(),int(haveMpr),int(haveMob));
                    fflush(stderr);continue;
                }
                if(!world.Create()||!world.LoadNavigationScene(map.first.c_str(),map.second.c_str()))
                    throw std::runtime_error("Shared MPR/MOB import failed");
                const auto importMs=Milliseconds(start);
                const auto bytes=world.aiMap.Navigation().Bake(world,output);totalBytes+=bytes;
                ++completed;
                printf("MAP_DONE mpr=%s mob=%s import_ms=%lld total_ms=%lld profiles=8 bytes=%llu\n",
                    map.first.c_str(),map.second.c_str(),importMs,Milliseconds(start),static_cast<unsigned long long>(bytes));
            }catch(const std::exception& e) {
                ++errors;
                fprintf(stderr,"MAP_ERROR mpr=%s mob=%s code=4 ms=%lld reason=%s\n",map.first.c_str(),map.second.c_str(),Milliseconds(start),e.what());
            }
            fflush(stdout);fflush(stderr);
        }
        if(totalBytes>400ull*1024*1024){++errors;fprintf(stderr,"BAKE_ERROR code=4 reason=campaign_exceeds_400_MiB\n");}
        printf("BAKE_DONE maps=%zu completed=%u missing=%u errors=%u total_ms=%lld bytes=%llu\n",maps.size(),completed,missing,errors,Milliseconds(total),static_cast<unsigned long long>(totalBytes));fflush(stdout);
        return errors?4:missing?5:0;
    }catch(const std::exception& e) {
        fprintf(stderr,"BAKE_ERROR code=%d reason=%s\n",exitCode,e.what());return exitCode;
    }
}
