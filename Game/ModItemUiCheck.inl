#pragma once
#include "ModItemUiPolicy.h"
#include "ModConfiguration.h"
#include "../SDKs/JSON/json.hpp"
#include <filesystem>
#include <fstream>
#include <shellapi.h>
#include <stdexcept>
namespace ModItemUiCheck {
inline void Require(bool value,const char*why){if(!value)throw std::runtime_error(why);}
// One-section, one-value private REG in RAM. Runs the actual production reader,
// not a JSON-to-bool substitute; no installed config writes or package/world.
inline std::vector<BYTE> Registry(BYTE type,DWORD value)
{
    const std::string section="Addon",key="useLS_ObjItemInfoDraw";
    const size_t sectionAt=sizeof(SRegHeader)+sizeof(SRegHashEntry);
    const size_t valueAt=sizeof(SSectionHeader)+section.size()+sizeof(SRegHashEntry);
    std::vector<BYTE> data(sectionAt+valueAt+sizeof(SValueHeader)+key.size()+2+8,0);
    SRegHeader header={SREGHEADER_SIGNATURE,1};std::memcpy(data.data(),&header,sizeof(header));
    SRegHashEntry hash={WORD(-1),DWORD(sectionAt)};std::memcpy(data.data()+sizeof(header),&hash,sizeof(hash));
    SSectionHeader sect={1,WORD(section.size())};std::memcpy(data.data()+sectionAt,&sect,sizeof(sect));
    std::memcpy(data.data()+sectionAt+sizeof(sect),section.data(),section.size());
    hash.dwOffset=DWORD(valueAt);std::memcpy(data.data()+sectionAt+sizeof(sect)+section.size(),&hash,sizeof(hash));
    SValueHeader entry={type,WORD(key.size())};BYTE*at=data.data()+sectionAt+valueAt;
    std::memcpy(at,&entry,sizeof(entry));at+=sizeof(entry);std::memcpy(at,key.data(),key.size());at+=key.size();
    if(type&ERT_ARRAY){WORD n=2;std::memcpy(at,&n,2);at+=2;std::memcpy(at,&value,4);std::memcpy(at+4,&value,4);}
    else std::memcpy(at,&value,4);
    return data;
}
inline nlohmann::json Run()
{
    using namespace ModItemUiPolicy;nlohmann::json cases=nlohmann::json::array();
    int calls=0;std::vector<int> columns;
    auto resolve=[&](const char*code,int column){++calls;columns.push_back(column);return column==1?std::string("Rune"):column==2?std::string("model"):std::string("texture");};
    Require(Instruction(true,false,true,"rune","fallback",resolve)=="model"&&columns==std::vector<int>({1,2}),"Rune model columns/case mismatch");
    columns.clear();Require(Instruction(true,false,false,"RUNE","fallback",resolve)=="texture"&&columns==std::vector<int>({1,3}),"Texture column mismatch");
    columns.clear();Require(Instruction(true,true,true,"rune","initqi1item",resolve)=="initqi3item"&&columns==std::vector<int>({1,2}),"Prototype must fetch column2 but use fixed figure");
    Require(Instruction(true,false,true,"other","initqiRUNEitem",resolve)=="initqiRUNEitem","Rune mismatch fallback");
    const int before=calls;Require(Instruction(false,false,true,"rune","stock",resolve)=="stock"&&Instruction(true,true,true,nullptr,"initqi1item",resolve)=="initqi1item"&&calls==before,"Disabled/null branch executed callback");
    auto empty=[](const char*,int column){return column==1?std::string("rune"):std::string();};
    Require(Instruction(true,false,true,"rune","fallback",empty).empty(),"Matching empty result was replaced by fallback");
    cases.push_back({{"name","instruction_columns_matching_and_fallbacks"},{"luaExecuted",false}});

    Require(FirstDeploy(false,false,"zone1.mob",true)&&!FirstDeploy(false,true,"zone1.mob",true)&&!FirstDeploy(true,false,"zone1.mob",true)&&!FirstDeploy(false,false,"zone2",true)&&!FirstDeploy(false,false,"ZONE1",false),"Ordinary deploy/camp/zone/geometry gate mismatch");
    cases.push_back({{"name","ordinary_zone1_and_camp_return_gates"},{"worldCreated",false}});

    for(BYTE type:{BYTE(ERT_DWORD),BYTE(ERT_FLOAT),BYTE(ERT_DWORD|ERT_ARRAY)}){
        DWORD value=1;if(type==ERT_FLOAT){float f=1.f;std::memcpy(&value,&f,4);}auto data=Registry(type,value);CFileRegistry reg;
        Require(reg.Open(static_cast<LPVOID>(data.data()))!=FALSE,"Private REG open failed");
        Require(ModConfiguration::Bool(reg,"Addon","useLS_ObjItemInfoDraw",false)==!(type&ERT_ARRAY),"Scalar/array native Bool mismatch");
        Require(!ModConfiguration::Bool(reg,"Addon","missing",false),"Missing false default mismatch");
    }
    CFileRegistry absent;Require(!ModConfiguration::Bool(absent,"Addon","useAdvancedDescriptions",false),"Missing master default");
    size_t count=0;const auto*rules=ModConfiguration::CombatRules(count);int advancedRules=0;
    for(size_t i=0;i<count;++i)if(!strcmp(rules[i].key,"useAdvancedDescriptions")||!strncmp(rules[i].key,"useLS_",6)){Require(!rules[i].fallback,"Native UI false default changed");++advancedRules;}
    Require(advancedRules==18,"Missing native UI configuration mapping");
    for(int master:{0,1})for(int child:{0,1})for(int slots:{0,1}){
        auto get=[&](const char*key){return !strcmp(key,"useAdvancedDescriptions")?master:!strcmp(key,"JMVExtendedSpellSlots")?slots:child;};
        Require(Advanced("useLS_FormTextInfo",get)==bool(master&&child),"Master/child gate mismatch");
        Require(Advanced("useLS_SetUnitS",get)==bool(master),"Permanent active-spell owner lost native SetUnitS exception");
        auto icon=ItemIcon(Advanced("useLS_ObjItemInfoDraw",get));Require(icon.z==(master&&child?4.f:3.f)&&icon.size==(master&&child?150.f:120.f),"Conditional icon draw mismatch");
        Require(NameInset(Advanced("useLS_ShowEnemyConsider",get))==(master&&child?0:5),"Native name rectangle shared x/y immediate mismatch");
    }
    cases.push_back({{"name","native_scalar_types_defaults_master_and_children"},{"registryReader","CFileRegistry"},{"uiExecuted",false}});
    return cases;
}
}
inline bool InspectItemUiCommandLine(int& result)
{
    int count=0;auto*args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)return false;
    struct Free {LPWSTR*p;~Free(){LocalFree(p);}} free{args};
    if(count<2||wcscmp(args[1],L"--inspect-item-ui"))return false;
    result=1;nlohmann::json report={{"success",false},{"actualProductionPolicy",true},{"rngExecuted",false}};
    for(const char*name:{"hostCreated","worldCreated","unitCreated","luaExecuted","deviceCreated","audioStarted","saveRead","uiExecuted"})report[name]=false;
    try{if(count!=3)throw std::runtime_error("Expected one report path");report["cases"]=ModItemUiCheck::Run();report["success"]=true;result=0;}
    catch(const std::exception&error){report["error"]=error.what();}
    if(count>=3){std::ofstream file{std::filesystem::path(args[2])};file<<report.dump(2);if(!file)result=1;}
    return true;
}
