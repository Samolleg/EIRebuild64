#pragma once
#include "ModNativeCrtRandom.h"
#include "ModRetailShuffle.h"
#include "ModCustomLower.h"
#include "../SDKs/JSON/json.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <shellapi.h>
#include <stdexcept>

namespace ModRandomOwnershipCheck {
inline void Require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
struct Trace { std::array<int,2> draws={{-1,-1}}; void* parent=nullptr; };
inline DWORD WINAPI Thread(void* input){auto& t=*static_cast<Trace*>(input);t.draws[0]=ModNativeCrtRandom::Random(100);t.draws[1]=ModNativeCrtRandom::Random(100);return 0;}
inline void WINAPI Fiber(void* input){auto& t=*static_cast<Trace*>(input);for(int i=0;i<2;++i){t.draws[i]=ModNativeCrtRandom::Random(100);SwitchToFiber(t.parent);}for(;;)SwitchToFiber(t.parent);}
inline nlohmann::json Run()
{
    using nlohmann::json;
    json cases=json::array();
    std::srand(1); // Controlled inspection fixture only; never a game seed.
    Require(std::rand()==41,"Engine fixture first draw");
    SetLastError(0x1234);
    std::array<int,5> native={{ModNativeCrtRandom::Random(2),ModNativeCrtRandom::Dice(100),ModCustomLower::CrtRoll(100),ModNativeCrtRandom::Random(100),ModNativeCrtRandom::Random(100)}};
    Require(native==std::array<int,5>{{0,57,19,80,58}},"Shared native continuation across helpers");
    Require(GetLastError()==0x1234,"FLS owner preserves LastError");
    std::array<int,5> values={{0,1,2,3,4}};
    ModRetailShuffle::Shuffle(values.begin(),values.end());
    Require(values==std::array<int,5>{{3,2,1,0,4}},"Retail forward engine-CRT permutation");
    Require(std::rand()==15724,"Native calls do not advance engine; shuffle consumes four draws");
    std::array<int,0> empty;std::array<int,1> one={{7}};
    ModRetailShuffle::Shuffle(empty.begin(),empty.end());ModRetailShuffle::Shuffle(one.begin(),one.end());
    Require(one[0]==7&&std::rand()==11478,"Empty and singleton preserve engine state");
    cases.push_back({{"name","native_engine_separation_and_retail_shuffle"},{"native",native},{"permutation",values}});
    std::array<Trace,2> threads;
    for(auto& t:threads){HANDLE h=CreateThread(nullptr,0,Thread,&t,0,nullptr);Require(h!=nullptr,"Create isolated thread");DWORD wait=WaitForSingleObject(h,10000);CloseHandle(h);if(wait!=WAIT_OBJECT_0)std::abort();Require(t.draws==std::array<int,2>{{0,56}},"Fresh thread native CRT state");}
    Require(ModNativeCrtRandom::Random(100)==47,"Thread teardown preserves parent continuation");
    cases.push_back({{"name","thread_isolation_and_lifetime"},{"first",threads[0].draws},{"second",threads[1].draws},{"parent_next",47}});
    const bool converted=!IsThreadAFiber();void* parent=converted?ConvertThreadToFiber(nullptr):GetCurrentFiber();
    Require(parent!=nullptr,"Convert inspection thread to fiber");
    struct Context {void* fibers[2]={};bool converted;~Context(){for(void* f:fibers)if(f)DeleteFiber(f);if(converted)ConvertFiberToThread();}} context{{},converted};
    std::array<Trace,2> fibers;for(int i=0;i<2;++i){fibers[i].parent=parent;context.fibers[i]=CreateFiber(0,Fiber,&fibers[i]);Require(context.fibers[i]!=nullptr,"Create isolated fiber");}
    for(int round=0;round<2;++round)for(void* f:context.fibers)SwitchToFiber(f);
    for(auto& f:fibers)Require(f.draws==std::array<int,2>{{0,56}},"Resumed fiber retains its private state");
    DeleteFiber(context.fibers[0]);context.fibers[0]=nullptr;Trace fresh;fresh.parent=parent;context.fibers[0]=CreateFiber(0,Fiber,&fresh);Require(context.fibers[0]!=nullptr,"Replace deleted fiber");SwitchToFiber(context.fibers[0]);Require(fresh.draws[0]==0,"New fiber gets bound default after old teardown");
    Require(ModNativeCrtRandom::Random(100)==35,"Fiber switches preserve parent continuation");
    cases.push_back({{"name","fiber_isolation_and_lifetime"},{"first",fibers[0].draws},{"second",fibers[1].draws},{"fresh_first",fresh.draws[0]},{"parent_next",35}});
    return cases;
}
}
inline bool InspectNativeRandomCommandLine(int& result)
{
    int count=0;auto* args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)return false;
    struct Free {LPWSTR* p;~Free(){LocalFree(p);}} free{args};
    if(count<2||wcscmp(args[1],L"--inspect-native-crt-ownership"))return false;
    result=1;nlohmann::json report={{"success",false},{"actualProductionFlsProvider",true}};
    for(const char* name:{"hostCreated","worldCreated","unitCreated","luaExecuted","deviceCreated","audioStarted","saveRead","uiExecuted"})report[name]=false;
    try{if(count!=3)throw std::runtime_error("Expected one report path");report["cases"]=ModRandomOwnershipCheck::Run();report["success"]=true;result=0;}
    catch(const std::exception& e){report["error"]=e.what();}
    if(count>=3){std::ofstream file{std::filesystem::path(args[2])};file<<report.dump(2);if(!file)result=1;}
    return true;
}
