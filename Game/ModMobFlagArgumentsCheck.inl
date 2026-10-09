#pragma once
#include "ModMobFlagArguments.h"
#include "../SDKs/JSON/json.hpp"
#include <filesystem>
#include <fstream>
#include <limits>
#include <shellapi.h>
#include <stdexcept>
namespace ModMobFlagArgumentsCheck {
inline void Require(bool good,const char* reason){if(!good)throw std::runtime_error(reason);}
inline nlohmann::json Run(){
 nlohmann::json cases=nlohmann::json::array();
 struct Sample{float input;std::uint32_t expected;};
 const Sample masks[]={{0,0},{1,1},{2147483648.f,0x80000000u},{4294967040.f,0xffffff00u},{-1,0xffffffffu},{-2147483648.f,0x80000000u}};
 for(const auto& sample:masks){std::uint32_t actual=123;Require(ModMobFlagArguments::Mask(sample.input,actual)&&actual==sample.expected,"Signed/unsigned DWORD mask mismatch");}
 cases.push_back({{"name","signed_unsigned_masks"},{"samples",6}});
 const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
 const float invalidMasks[]={1.5f,-.5f,4294967296.f,-2147483904.f,nan,inf,-inf};
 for(float input:invalidMasks){std::uint32_t actual=123;Require(!ModMobFlagArguments::Mask(input,actual)&&actual==0,"Invalid mask accepted or output retained");}
 cases.push_back({{"name","invalid_masks"},{"samples",7}});
 const Sample bits[]={{0,1},{31,0x80000000u}};
 for(const auto& sample:bits){std::uint32_t actual=123;Require(ModMobFlagArguments::Bit(sample.input,actual)&&actual==sample.expected,"Bit endpoint mismatch");}
 const float invalidBits[]={1.5f,-1,32,nan,inf};
 for(float input:invalidBits){std::uint32_t actual=123;Require(!ModMobFlagArguments::Bit(input,actual)&&actual==0,"Invalid bit accepted or output retained");}
 cases.push_back({{"name","bit_endpoints_and_rejection"},{"accepted",2},{"rejected",5}});
 return cases;
}
}
inline bool InspectMobFlagArgumentsCommandLine(int& result){
 int count=0;auto* args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)return false;
 struct Free{LPWSTR* p;~Free(){LocalFree(p);}} free{args};
 if(count<2||wcscmp(args[1],L"--inspect-mob-flag-arguments"))return false;
 result=1;nlohmann::json report={{"success",false},{"actualProductionDecoders",true},{"rngExecuted",false}};
 for(const char* name:{"hostCreated","worldCreated","unitCreated","luaExecuted","deviceCreated","audioStarted","saveRead","uiExecuted"})report[name]=false;
 try{if(count!=3)throw std::runtime_error("Expected one report path");report["cases"]=ModMobFlagArgumentsCheck::Run();report["success"]=true;result=0;}
 catch(const std::exception& error){report["error"]=error.what();}
 if(count>=3){std::ofstream file{std::filesystem::path(args[2])};file<<report.dump(2);if(!file)result=1;}
 return true;
}
