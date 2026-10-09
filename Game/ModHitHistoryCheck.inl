#pragma once
#include "ModHitHistory.h"
#include "../SDKs/JSON/json.hpp"
#include <filesystem>
#include <fstream>
#include <shellapi.h>
#include <stdexcept>
namespace ModHitHistoryCheck {
inline void Require(bool value,const char*reason){if(!value)throw std::runtime_error(reason);}
inline nlohmann::json Run()
{
    using namespace ModHitHistory;nlohmann::json cases=nlohmann::json::array();int calls=0;
    auto plain=[&]{++calls;return 0.f;};Ledger hits;
    Result last{0,{}, {},Reason::First};
    for(int n=1;n<=9;++n){last=hits.Observe(true,true,10,20,false,100,50,plain);
        Require(last.score==(n==9?0.f:100.f)&&last.after.hits==(n==9?0:n),"Native ninth hit score/count mismatch");}
    Require(calls==8&&last.zero&&!last.maximum,"WeaponsMaster called on initialization or forced score mismatch");
    last=hits.Observe(true,true,10,20,false,100,50,plain);Require(last.after.hits==1&&last.score==100,"Hit threshold did not reset own count");
    Ledger misses;for(int n=1;n<=9;++n){last=misses.Observe(true,true,10,20,false,-100,50,plain);Require(last.score==(n==9?255.f:-100.f)&&last.after.misses==(n==9?0:n),"Native ninth miss mismatch");}
    Ledger mixed;for(float value:{100.f,-100.f,100.f,-100.f,100.f})last=mixed.Observe(true,true,10,20,false,value,50,plain);
    Require(last.after.hits==3&&last.after.misses==2,"Opposite outcome reset cumulative history");
    auto both=Advance(Count{20,8,9},100,50,plain);Require(both.zero&&both.maximum&&both.score==255&&both.after.hits==0&&both.after.misses==0,"Miss threshold did not override preceding hit threshold");
    auto zeroDefense=Advance(Count{20,8,0},100,0,plain);auto highDefense=Advance(Count{20,0,8},-100,300,plain);
    Require(zeroDefense.score>=0&&!(highDefense.score>=300),"Score replacement was turned into absolute forced result");
    cases.push_back({{"name","native_accumulation_thresholds_and_score_semantics"}});

    Ledger targets;calls=0;last=targets.Observe(true,true,10,20,false,100,50,plain);
    last=targets.Observe(true,true,10,30,false,-100,50,plain);Require(last.reason==Reason::ChangedVictim&&last.after.victim==30&&last.after.hits==0&&last.after.misses==1&&calls==0,"Victim switch retained old pair counts/called mastery");
    last=targets.Observe(true,true,10,20,false,100,50,plain);Require(last.after.hits==1&&last.after.misses==0,"Returning to old target restored an independent pair record");
    last=targets.Observe(true,true,10,30,true,-100,50,plain);Require(last.reason==Reason::AllLocations&&last.after.victim==20&&last.after.hits==1&&calls==0,"Existing ALL changed history/victim or called mastery");
    last=targets.Observe(true,true,11,30,true,-100,50,plain);Require(last.reason==Reason::First&&last.after.victim==30&&last.after.misses==1&&targets.Size()==2&&calls==0,"First ALL must initialize separate attacker");
    last=targets.Observe(false,true,12,20,false,100,50,plain);last=targets.Observe(true,false,0,20,false,100,50,plain);
    Require(targets.Size()==2&&calls==0&&last.reason==Reason::NoAttacker,"Disabled/null attacker created history");
    Require(!Enabled(false,false)&&Enabled(true,false)&&Enabled(false,true)&&Enabled(true,true),"Registered recipient/direct custom gate mismatch");
    cases.push_back({{"name","last_victim_all_attacker_and_owner_gates"}});

    Ledger master;int masterCalls=0;auto perk=[&]{++masterCalls;return 2.f;};
    for(int n=1;n<=7;++n){last=master.Observe(true,true,10,20,false,-100,50,perk);Require(last.score==(n==7?255.f:-100.f),"Defender WeaponsMaster seventh miss threshold mismatch");}
    Require(masterCalls==6&&last.missCap==7&&last.after.misses==0,"Mastery callback or own miss reset mismatch");
    auto fractional=Advance(Count{20,0,6},-100,50,[]{return .5f;});Require(fractional.missCap==9&&fractional.score==-100,"Native int mastery truncation lost");
    auto skipped=Advance(Count{20,0,8},-100,50,[]{return 1.f;});Require(skipped.after.misses==9&&!skipped.maximum,"Equality threshold replaced with >= after perk change");
    auto clamp=Advance(Count{20,3,11},100,50,plain);Require(clamp.after.hits==4&&clamp.after.misses==0,"Inactive miss counter >10 was not sanitized");
    clamp=Advance(Count{20,-1,3},-100,50,plain);Require(clamp.after.hits==0&&clamp.after.misses==4,"Inactive negative hit counter was not sanitized");
    master.Clear();Require(master.Size()==0&&!master.Find(10),"Explicit texture lifecycle clear failed");
    last=master.Observe(true,true,10,20,false,-100,50,perk);Require(last.reason==Reason::First&&last.after.misses==1&&masterCalls==6,"History survived clear or init called perk");
    cases.push_back({{"name","lazy_defender_mastery_exact_cap_and_transient_clear"},{"rngExecuted",false},{"runtimeLedgerUsed",false}});
    return cases;
}
}
inline bool InspectHitHistoryCommandLine(int& result)
{
    int count=0;auto*args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)return false;
    struct Free {LPWSTR*p;~Free(){LocalFree(p);}} free{args};
    if(count<2||wcscmp(args[1],L"--inspect-hit-history"))return false;
    result=1;nlohmann::json report={{"success",false},{"actualProductionPolicy",true},{"rngExecuted",false}};
    for(const char*name:{"hostCreated","worldCreated","unitCreated","luaExecuted","deviceCreated","audioStarted","saveRead","uiExecuted"})report[name]=false;
    try{if(count!=3)throw std::runtime_error("Expected one report path");report["cases"]=ModHitHistoryCheck::Run();report["success"]=true;result=0;}
    catch(const std::exception&error){report["error"]=error.what();}
    if(count>=3){std::ofstream file{std::filesystem::path(args[2])};file<<report.dump(2);if(!file)result=1;}
    return true;
}
