#pragma once
#include "ModDiagnosticsPolicy.h"
#include "../shared/Common.h"
namespace ModDiagnosticsCheck {
class MemoryLogger : public CLogger {
public:
    std::vector<std::string> lines;
    bool Begin(ELogLevel level) {
        WithLevel(); WithSection(); WithNewLine(); return CLogger::Start(level);
    }
private:
    bool StartLocked() override { return true; }
    void StopLocked() override {}
    void FlushLocked() override {}
    void WriteLineLocked(const char* line,size_t size) override { lines.emplace_back(line,size); }
};
inline nlohmann::json Run() {
    using ModSettingsCheck::Require;
    using namespace ModDiagnosticsPolicy;
    Require(Dword(ERROR_FILE_NOT_FOUND,REG_DWORD,4,1,0)==0,"Missing FullDump default");
    Require(Dword(ERROR_FILE_NOT_FOUND,REG_DWORD,4,0,1)==1,"Missing AutoRecover default");
    Require(Dword(ERROR_SUCCESS,REG_SZ,4,1,0)==0 && Dword(ERROR_SUCCESS,REG_DWORD,8,1,0)==0,
            "Wrong type/size accepted as DWORD");
    Require(Dword(ERROR_SUCCESS,REG_DWORD,4,0,1)==0 && Dword(ERROR_SUCCESS,REG_DWORD,4,27,0)==27,
            "Valid DWORD/zero/nonzero changed");
    Require(CLogger::LevelFromInt(-1)==CLogger::ELogLevel::LL_DEBUG &&
            CLogger::LevelFromInt(99)==CLogger::ELogLevel::LL_DISABLED,"Native signed clamp");
    MemoryLogger file; Require(file.Begin(CLogger::ELogLevel::LL_INFO),"RAM logger start");
    file.Debug("Patch","filtered");file.Info("Main","information");file.Error("Patch","error %d",7);
    Require(file.lines.size()==2 && file.lines[0]=="[INFO][Main]\tinformation\n" &&
            file.lines[1]=="[ERROR][Patch]\terror 7\n","Severity/section/append formatting");
    file.SetLevel(CLogger::ELogLevel::LL_DISABLED);file.Error("Patch","filtered");
    Require(file.lines.size()==2,"Disabled logger wrote");
    file.SetLevel(CLogger::ELogLevel::LL_INFO);
    for(int i=0;i<30;++i)file.Info("Main","line %d",i);
    Require(file.RecentLineCount()==24 && std::string(file.RecentLine(0))=="[INFO][Main]\tline 6" &&
            std::string(file.RecentLine(23))=="[INFO][Main]\tline 29" && file.RecentLine(24)==nullptr,
            "Recent ring order/capacity/newline stripping");
    const std::string longLine(500,'x');file.Info("Main","%s",longLine.c_str());
    Require(strlen(file.RecentLine(23))==199,"Recent ring line bound");
    file.EnterCrashMode();file.Error("Patch","crash mode");
    Require(file.lines.back()=="[ERROR][Patch]\tcrash mode\n","Crash mode RAM sink");file.Stop();
    MemoryLogger console;Require(console.Begin(CLogger::ELogLevel::LL_ERROR),"Console level start");
    console.Info("","filtered");console.Error("","visible");
    Require(console.lines.size()==1,"Console ERROR default");console.Stop();
    std::vector<std::string> sequence;
    Recover("123.mp",[&]{sequence.push_back("remove");},[&](const std::string& name){sequence.push_back(name);},
            [&]{sequence.push_back("deploy");},[&]{sequence.push_back("loot");});
    Require(sequence==std::vector<std::string>({"remove","123.mp","deploy","loot","remove","lootall-123.mp","deploy"}),
            "Production recovery two-save/mutation sequence");
    sequence.clear();bool threw=false;
    try {Recover("123.mp",[&]{sequence.push_back("remove");},[](const std::string&){throw std::runtime_error("RAM save failure");},
                 [&]{sequence.push_back("deploy");},[&]{sequence.push_back("loot");});}
    catch(const std::runtime_error&){threw=true;}
    Require(threw && sequence==std::vector<std::string>({"remove"}),"Recovery silently swallowed failure or invented rollback");
    CFileRegistry absent;Require(!ModConfiguration::Bool(absent,"Addon","AntiCrash",false),"AntiCrash default");
    auto bytes=ModSettingsCheck::Registry("Addon","AntiCrash",ERT_DWORD,27);CFileRegistry enabled;
    Require(enabled.Open(static_cast<LPVOID>(bytes.data()))!=FALSE &&
            ModConfiguration::Bool(enabled,"Addon","AntiCrash",false),"Existing AntiCrash bool");
    return {{"name","native_dword_levels_recent_ring_and_mock_recovery_sequence"},
            {"actualProductionPolicy",true},{"fileSinkWritten",false},{"serializerExecuted",false},
            {"worldMutated",false},{"hiveRead",false},{"crashInvoked",false}};
}
}
inline bool InspectDiagnosticsCommandLine(int& result) {
    int count=0;auto* args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)return false;
    struct Free{LPWSTR* p;~Free(){LocalFree(p);}} free{args};
    if(count<2 || wcscmp(args[1],L"--inspect-p10-diagnostics"))return false;
    result=1;nlohmann::json report={{"success",false},{"actualProductionPolicy",true}};
    for(const char* key:{"hostCreated","worldCreated","unitCreated","luaExecuted","deviceCreated","audioStarted",
        "saveRead","serializerExecuted","recoveryExecuted","uiExecuted","rngExecuted","commandsExecuted",
        "hiveRead","registryWritten","dllLoaded","chatFileWritten","fileSinkWritten","crashInvoked"})report[key]=false;
    try {
        if(count!=3)throw std::runtime_error("Expected one report path");
        report["cases"]=ModSettingsCheck::Run();report["cases"].push_back(ModDiagnosticsCheck::Run());
        report["success"]=true;result=0;
    } catch(const std::exception& e){report["error"]=e.what();}
    if(count>=3){std::ofstream file{std::filesystem::path(args[2])};file<<report.dump(2);if(!file)result=1;}
    return true;
}
