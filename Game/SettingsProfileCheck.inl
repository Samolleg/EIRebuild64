#include <thread>
#include <atomic>
#include "WinRegistry.h"
namespace SettingsProfileCheck {
void Check(bool ok,int line) { if(!ok) throw std::runtime_error("ReStarter provider fixture failed at line "+std::to_string(line)); }
#define Require(expression) Check((expression),__LINE__)
void Put(const std::filesystem::path& path,const char* text) { std::ofstream f(path,std::ios::binary); f<<text; Require(f.good()); }
std::vector<char> Bytes(const std::wstring& s) { std::vector<char> b((s.size()+1)*2);memcpy(b.data(),s.c_str(),b.size());return b; }
void Run(const std::wstring& mode) {
    namespace fs=std::filesystem;
    auto path=fs::path(SettingsProfile::Path());
    if(mode==L"missing") {
        Require(SettingsProfile::Dword(L"Settings",L"absent",99)==99);
        Require(SettingsProfile::SetDword(L"Settings",L"new",7));Require(fs::exists(path));return;
    }
    if(mode==L"fallback") { Require(SettingsProfile::Dword(L"Settings",L"fallback")==81);return; }
    if(mode==L"types") {
        Require(SettingsProfile::Section(L"Software\\Gipat.Ru\\EI_Starter\\EvilIslands")==L"EvilIslands");
        Require(SettingsProfile::Section(L"Software\\Nival Interactive\\EvilIslands")==L"EvilIslands");
        Require(SettingsProfile::Dword(L"settings",L"SIGNED")==0xffffffffu);
        Require(SettingsProfile::String(L"Settings",L"unicode")==L"\x041d\x0430\x043b\x0430 & <x>");
        Require(SettingsProfile::String(L"Settings",L"expand")==L"%TEMP%");
        Require(SettingsProfile::MultiString(L"Settings",L"multi")==std::vector<std::wstring>({L"one",L"two"}));
        std::vector<char> data;uint32_t type=0;float f=0;
        Require(SettingsProfile::GetRaw(L"Settings",L"float",data,type)&&type==REG_BINARY&&data.size()==4);memcpy(&f,data.data(),4);Require(f==48.f);
        Require(SettingsProfile::GetRaw(L"Settings",L"qword",data,type)&&type==REG_QWORD&&data.size()==8);int64_t q;memcpy(&q,data.data(),8);Require(q==INT64_MIN);
        Require(SettingsProfile::GetRaw(L"Settings",L"none",data,type)&&type==REG_NONE&&data.size()==2);
        Require(SettingsProfile::Dword(L"Settings",L"float",91)==91);
        Require(!SettingsProfile::GetRaw(L"Settings",L"badbinary",data,type));
        Require(!SettingsProfile::GetRaw(L"Settings",L"badinteger",data,type));
        Require(!SettingsProfile::GetRaw(L"Settings",L"unknown",data,type));
        CWinRegistry typed;
        Require(typed.Open("Software\\Gipat.ru\\EI_Starter",HKEY_CURRENT_USER));
        Require(typed.GetFloat("Settings","float",0)==48.f);
        Require(typed.SetDouble("typed","double",3.5) && typed.GetDouble("typed","double",0)==3.5);
        Require(typed.SetChar("typed","char",-7) && typed.GetChar("typed","char",0)==-7);
        Require(typed.SetByte("typed","byte",250) && typed.GetByte("typed","byte",0)==250);
        Require(typed.SetShort("typed","short",-123) && typed.GetShort("typed","short",0)==-123);
        Require(typed.SetWord("typed","word",65000) && typed.GetWord("typed","word",0)==65000);
        Require(typed.SetLong("typed","long",-1000) && typed.GetLong("typed","long",0)==-1000);
        Require(typed.SetDWord("typed","dword",0xffffffff) && typed.GetDWord("typed","dword",0)==0xffffffff);
        Require(typed.SetInt("typed","int",-27) && typed.GetInt("typed","int",0)==-27);
        Require(typed.SetUInt("typed","uint",123) && typed.GetUInt("typed","uint",0)==123);
        Require(typed.SetString("typed","string","hello") && typed.GetString("typed","string","")=="hello");
        Require(typed.SetCPoint("typed","point",CPoint(1,2)) && typed.GetCPoint("typed","point",CPoint())==CPoint(1,2));
        Require(typed.SetCRect("typed","rect",CRect(1,2,3,4)) && typed.GetCRect("typed","rect",CRect())==CRect(1,2,3,4));
        Require(typed.SetCSize("typed","size",CSize(5,6)) && typed.GetCSize("typed","size",CSize())==CSize(5,6));
        for(uint32_t kind:{REG_NONE,REG_BINARY,REG_SZ,REG_EXPAND_SZ,REG_MULTI_SZ,REG_DWORD,REG_QWORD}) {
            auto bytes=kind==REG_SZ||kind==REG_EXPAND_SZ?Bytes(L"hello & <xml>"):std::vector<char>(kind==REG_QWORD?8:4,0);
            if(kind==REG_MULTI_SZ)bytes=Bytes(std::wstring(L"a\0b\0",4));
            Require(SettingsProfile::SetRaw(L"written",std::to_wstring(kind),bytes,kind));
            Require(SettingsProfile::GetRaw(L"written",std::to_wstring(kind),data,type)&&type==kind);
            if(kind!=REG_MULTI_SZ)Require(data==bytes);
        }
        // External edits between game writes must survive the next atomic save.
        std::ifstream input(path,std::ios::binary);std::string xml((std::istreambuf_iterator<char>(input)),{});input.close();
        auto at=xml.find("</profile>");Require(at!=std::string::npos);
        xml.insert(at,"<value section=\"foreign\" name=\"late\" kind=\"1\">keep &amp; preserve</value><extension private=\"yes\"/>");Put(path,xml.c_str());
        Require(SettingsProfile::SetDword(L"Settings",L"saved",123));
        Require(SettingsProfile::String(L"foreign",L"late")==L"keep & preserve");
        auto entries=SettingsProfile::Entries(L"written");Require(entries.size()==7);
        std::wstring entry=L"4";Require(SettingsProfile::Delete(L"written",&entry));Require(SettingsProfile::Dword(L"written",L"4",9)==9);
        Require(SettingsProfile::Delete(L"written",nullptr));
        Require(SettingsProfile::SetDword(L"nested\\child",L"n",1));Require(!SettingsProfile::Delete(L"nested",nullptr));
        // A sharing violation at replacement must leave both disk and cache unchanged.
        HANDLE locked=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);Require(locked!=INVALID_HANDLE_VALUE);
        Require(!SettingsProfile::SetDword(L"Settings",L"saved",124));CloseHandle(locked);
        Require(SettingsProfile::Dword(L"Settings",L"saved")==123);
        std::atomic<bool> stop{false}, complete{true};std::atomic<unsigned> observed{0};
        std::thread reader([&]{while(!stop){HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);if(file==INVALID_HANDLE_VALUE)continue;DWORD size=GetFileSize(file,nullptr),n=0;std::string text(size,'\0');ReadFile(file,text.data(),size,&n,nullptr);CloseHandle(file);if(n!=size||text.find("</profile>")==std::string::npos)complete=false;++observed;}});
        struct Join { std::atomic<bool>& stop; std::thread& thread; ~Join(){stop=true;if(thread.joinable())thread.join();} } join{stop,reader};
        for(unsigned i=0;i<30;++i)Require(SettingsProfile::SetDword(L"Settings",L"atomic",i));
        stop=true;reader.join();Require(observed>0 && complete);
        for(auto& file:fs::directory_iterator(path.parent_path()))Require(file.path().filename().wstring().find(L".tmp-")==std::wstring::npos);
        return;
    }

    throw std::runtime_error("Unknown ReStarter provider fixture");
}
}

// Headless inspection returns before journal, mod host, windows and renderer initialization.
static bool InspectSettingsProfileCommandLine(int& result) {
    int argc=0; LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(!argv) return false;
    struct Free { LPWSTR* p; ~Free(){LocalFree(p);} } free{argv};
    std::filesystem::path output;
    std::wstring checkMode;
    for(int i=1;i<argc;++i) if(wcscmp(argv[i],L"--inspect-restarter-profile")==0) {
        if(i+1>=argc) { result=2; return true; } output=argv[++i];
    }
    for(int i=1;i<argc;++i) if(wcscmp(argv[i],L"--check-restarter-provider")==0 && i+1<argc) checkMode=argv[++i];
    if(output.empty()) return false;
    try {
        SettingsProfile::Initialize();
        if(!checkMode.empty()) SettingsProfileCheck::Run(checkMode);
        SGeneralSettings settings;
        settings.ReadSettingsFromRegistry("Software\\Gipat.ru\\EI_Starter\\EvilIslands");
        CCamera camera;
        camera.Create(&settings,settings.fpNearClipDistance,settings.fpFarClipDistance,static_cast<float>(pi/3.5));
        camera.ActivateShortcut(0);
        nlohmann::json value={
            {"settings_source",WideToAnsi(AnsiToWide(SettingsProfile::Summary()),CP_UTF8)},
            {"near",settings.fpNearClipDistance},{"far",settings.fpFarClipDistance},
            {"distance_min",settings.fpCameraMinDistanceToCarrier},
            {"distance_max",settings.fpCameraMaxDistanceToCarrier},
            {"distance_limit_min",settings.fpCameraMinLimitDistanceToCarrier},
            {"distance_limit_max",settings.fpCameraMaxLimitDistanceToCarrier},
            {"shortcut_pitch",settings.camerashortcuts[0].fPitch},
            {"camera_pitch",camera.GetPitch()},{"camera_distance",camera.GetDistance()}
        };
        std::ofstream file(output,std::ios::binary); file<<value.dump(2)<<"\n";
        if(!file.good()) throw std::runtime_error("Cannot write profile inspection");
        result=0;
    } catch(const std::exception& error) {
        std::ofstream file(output,std::ios::binary);
        file<<nlohmann::json({{"error",error.what()}}).dump(2)<<"\n";
        result=2;
    }
    return true;
}
#undef Require
