#pragma once
#include "ModSettingsPolicy.h"
#include "../SDKs/JSON/json.hpp"
#include <filesystem>
#include <fstream>
#include <shellapi.h>
#include <stdexcept>
#include <limits>
namespace ModSettingsCheck {
inline void Require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
inline std::vector<BYTE> Registry(const std::string& section,const std::string& key,BYTE type,DWORD value)
{
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

inline nlohmann::json Run() {
 using namespace ModSettingsPolicy;nlohmann::json cases=nlohmann::json::array();
 Profile profile;CFileRegistry absent;profile.Read(absent);
 Require(profile.charge==0&&profile.factor==1&&profile.Experience(30,3)==10,"Missing experience defaults");
 const char* keys[]={"Melee","Archery","Elemental","Sense","Astral"};const int indexes[]={0,1,3,4,5};
 for(int i=0;i<5;++i){auto bytes=Registry("AddonPerk",keys[i],ERT_DWORD,120);CFileRegistry reg;
  Require(reg.Open(static_cast<LPVOID>(bytes.data()))!=FALSE,"RAM perk REG open");profile.Read(reg);
  Require(profile.SkillLimit(indexes[i])==120&&profile.SkillLimit(2)==100&&profile.SkillLimit(indexes[i],true)==120,"Configured maximum/Backstab/native owner mismatch");}
 for(BYTE type:{BYTE(ERT_DWORD),BYTE(ERT_FLOAT),BYTE(ERT_DWORD|ERT_ARRAY)}){
  DWORD value=2;if(type==ERT_FLOAT){float f=2;std::memcpy(&value,&f,4);}auto bytes=Registry("AddonExp","MulFactor",type,value);CFileRegistry reg;
  Require(reg.Open(static_cast<LPVOID>(bytes.data()))!=FALSE,"RAM exp REG open");profile.Read(reg);
  Require(profile.factor==((type&ERT_ARRAY)?1.f:2.f),"FLOAT/DWORD/ARRAY factor semantics");}
 auto charge=Registry("AddonExp","ChargeMethod",ERT_DWORD,2);CFileRegistry reg;
 Require(reg.Open(static_cast<LPVOID>(charge.data()))!=FALSE,"RAM charge REG open");profile.Read(reg);
 Require(profile.charge==2&&profile.Experience(30,3)==33,"Charge method2 owner");
 profile.charge=1;profile.factor=2;Require(profile.Experience(30,3)==180,"Charge method1");
 profile.charge=2;profile.factor=.5f;Require(profile.Experience(30,3)==16.5f,"Charge method2 factor");
 profile.charge=19;profile.factor=2;Require(profile.Experience(30,3)==20,"Other method division fallback");
 profile.charge=0;profile.factor=1.000001f;Require(profile.Experience(30,3)==10,"Native inactive epsilon path changed");
 profile.factor=std::numeric_limits<float>::quiet_NaN();Require(profile.Experience(30,3)==10,"Native NaN activation comparison");
 profile.skills[0]=87;Require(profile.SkillLimit(0)==87&&profile.SkillLimit(0,true)==120,"Redraw override versus repeat configured owner");
 size_t rulesCount=0;auto* rules=ModConfiguration::CombatRules(rulesCount);int defaultsFound=0;
 for(size_t i=0;i<rulesCount;++i)if(!std::strncmp(rules[i].key,"P100MCalcSpell",13)||!std::strcmp(rules[i].key,"JMVOneAmuletLimit")||!std::strcmp(rules[i].key,"JMVRingsLimit")){Require(!rules[i].fallback,"Disabled existing setting default changed");++defaultsFound;}
 Require(defaultsFound==5,"Missing native false defaults");
 auto ring=Registry("Addon","JMVRingsLimit",ERT_DWORD,27);CFileRegistry ringReg;Require(ringReg.Open(static_cast<LPVOID>(ring.data()))!=FALSE,"RAM ring REG open");
 Require(ModConfiguration::Bool(ringReg,"Addon","JMVRingsLimit",false),"Ring setting is native bool");
 std::vector<std::string> equipped={"AMULETa","amuletb","ringA"};std::vector<std::string> removed;
 auto size=[&](){return int(equipped.size());};auto name=[&](int i){return equipped[i];};auto remove=[&](int i){removed.push_back(equipped[i]);equipped.erase(equipped.begin()+i);};
 Require(EquipQuick("amuletnew",true,false,size,name,remove)&&removed.size()==1&&equipped[0]=="amuletb","Native forward shifted-index removal order");
 Require(!EquipQuick("RINGnew",false,true,size,name,remove)&&EquipQuick("ringnew",false,false,size,name,remove),"Ring rejection/disabled branch");
 float mana=17,effect=18,ticks=19;std::vector<int> callbacks;
 auto resolve=[&](int i){callbacks.push_back(i);return i==0?300.f:i==1?9.f:90.f;};
 Tooltip(false,true,true,true,50,mana,effect,ticks,resolve);Require(callbacks.empty()&&mana==17&&ticks==19,"Missing server called tooltip Lua");
 Tooltip(true,false,false,false,50,mana,effect,ticks,resolve);Require(callbacks.empty(),"Disabled tooltip called resolver");
 Tooltip(true,true,true,true,50,mana,effect,ticks,resolve);Require(callbacks==std::vector<int>({0,1,2})&&mana==150&&effect==9&&ticks==90,"Tooltip override order/units/intelligence");
 cases.push_back({{"name","settings_types_defaults_formula_limits_equipment_tooltip"},{"luaExecuted",false}});

 size_t commandCount=0;auto* commands=Commands(commandCount);Require(commandCount==4,"Command inventory");int admin=0;
 for(size_t i=0;i<commandCount;++i){if(commands[i].admin)++admin;else Require(std::string(commands[i].name)=="nogui","Common command owner");}
 Require(admin==3,"Admin command gate changed");
 Gui gui;bool show=true;Require(!gui.Set({})&&!gui.Set({"3"})&&!gui.pending,"Invalid nogui changed state");
 Require(gui.Set({"2"})&&!gui.controls&&!gui.cursor&&gui.Apply(show)&&!show&&gui.Apply(show),"Hidden GUI repeated application/cursor");
 Require(gui.Set({"1"})&&!gui.controls&&gui.cursor,"Cursor-visible nogui mode");
 Require(gui.Set({"0"})&&gui.Apply(show)&&show&&!gui.Apply(show),"Restoration must apply once");
 Require(gui.Set({"nonnumeric"})&&gui.controls,"Native atoi nonnumeric mode0");
 std::vector<std::string> replay;AutoExec({"first","same"},{"","same","last"},"exec autoexec.cfg",[&](const char* text){replay.emplace_back(text);});
 Require(replay==std::vector<std::string>({"first","same","","same","last","exec autoexec.cfg"}),"AutoExec order/blank/dedup/original");
 const wchar_t multi[]={L'a',0,L'b',0,0};Require(MultiString(multi,5)==std::vector<std::wstring>({L"a",L"b"}),"REG_MULTI_SZ order");
 const wchar_t broken[]={L'a',L'b'};Require(MultiString(broken,2).empty(),"Unterminated multi-string bound");
 Require(Object("CUnit",42)=="<CUnit:42>"&&Variable("obj","object","null")=="obj : object = null","gvar formatting");
 cases.push_back({{"name","console_metadata_gui_and_mock_autoexec_order"},{"commandsExecuted",false},{"hiveRead",false}});

 Require(Chat(7,"A&B","<text>")=="<font class=7>A&B</font>: <text><BR>","Native raw HTML append format");
 cases.push_back({{"name","chat_format"},{"chatFileWritten",false}});
 return cases;
}
}
inline bool InspectSettingsCommandLine(int& result) {
 int count=0;auto* args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)return false;
 struct Free{LPWSTR*p;~Free(){LocalFree(p);}}free{args};
 if(count<2||wcscmp(args[1],L"--inspect-settings"))return false;
 result=1;nlohmann::json report={{"success",false},{"actualProductionPolicy",true}};
 for(const char* key:{"hostCreated","worldCreated","unitCreated","luaExecuted","deviceCreated","audioStarted","saveRead","uiExecuted","rngExecuted","commandsExecuted","hiveRead","dllLoaded","chatFileWritten"})report[key]=false;
 try{if(count!=3)throw std::runtime_error("Expected one report path");report["cases"]=ModSettingsCheck::Run();report["success"]=true;result=0;}
 catch(const std::exception& error){report["error"]=error.what();}
 if(count>=3){std::ofstream file{std::filesystem::path(args[2])};file<<report.dump(2);if(!file)result=1;}
 return true;
}
