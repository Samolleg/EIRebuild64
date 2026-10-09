#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include <sstream>

#include "Console.h"
#include "../shared/Common.h"
#include "EngineDiagnostics.h"
#include "StringProcessing.h"
#include "WorldInterface.h"

#include "I3DMain.h"
#include "I3DScreen.h"
#include "I3DEffects.h"
#include "Time32.h"
#include "GeneralSettings.h"
#include "ModPackage.h"
#include "ModRuntime.h"
#include "ModConfiguration.h"
#include <cstdlib>
#include <stdexcept>
#include <vector>
#include <fstream>
#include <iomanip>
#include "I3DCursor.h"
#include "ModSettingsPolicy.h"

// CRAP - this include is related to interface and different options on writing statistics
#include "WorldClient.h"
#include "WorldServer.h"
#include "PlayerServer.h"
#include "PlayerClient.h"

#include "AckDatabase.h"

#include "ChatServer.h"
#include "GameMain.h"
#include "CSbase.h"
// end of CRAP
/////////////////////////////////////////////////////////////////////////////////////
#include <map>
/////////////////////////////////////////////////////////////////////////////////////
using std::map;
/////////////////////////////////////////////////////////////////////////////////////
namespace NConsole {
#ifdef FINALRELEASE
bool bConsoleActive = false;
#else
bool bConsoleActive = true;
#endif // FINALRELEASE
typedef void (*ConsoleCommand)(const CStringArray &szParams);
map<CString, ConsoleCommand> commands;
map<CString, ConsoleCommand> commandsCommon;

void ProcessHelp(const CStringArray &szParams);
void ProcessDrawAll(const CStringArray &szParams);
void ProcessConsole(const CStringArray &szParams);
void ProcessHistory(const CStringArray &szParams);
void ProcessFilter(const CStringArray &szParams);
void ProcessShow(const CStringArray &szParams);
void ProcessListVar(const CStringArray &szParams);
void ProcessLoadVar(const CStringArray &szParams);
void ProcessLastFPS(const CStringArray &szParams);
void ProcessExec(const CStringArray &szParams);
void ProcessDebugInfo(const CStringArray &szParams);
void ProcessGive(const CStringArray &szParams);
void ProcessFadeout(const CStringArray &szParams);
void ProcessJoin(const CStringArray &szParams);
void ProcessServer(const CStringArray &szParams);
void ProcessMemUsage(const CStringArray &szParams);
void ProcessLootAll(const CStringArray &szParams);
void ProcessNet(const CStringArray &szParams);
void ProcessDisconnect(const CStringArray &szParams);
void ProcessGenerateAcks(const CStringArray &szParams);
void ProcessDays(const CStringArray &szParams);
void ProcessPartyResend(const CStringArray &szParams);
void ProcessKick(const CStringArray &szParams);
void ProcessBan(const CStringArray &szParams);
void ProcessFPS(const CStringArray &szParams);
void ProcessRate(const CStringArray &szParams);
void ProcessLocalRate(const CStringArray &szParams);

}; // namespace NConsole
/////////////////////////////////////////////////////////////////////////////////////
using namespace NConsole;
/////////////////////////////////////////////////////////////////////////////////////
EFilter NConsole::eCurrentFilter = NONE;
/////////////////////////////////////////////////////////////////////////////////////
class CConsoleBuf : public std::stringbuf
{
    EFilter filter;
    CString szBuf;
    char szLowBuf[1024];

public:
    CConsoleBuf(EFilter _filter)
        : std::stringbuf(szLowBuf, 1024)
        , filter(_filter)
    {
    }
    virtual ~CConsoleBuf() {}
    virtual int sync();
    virtual int overflow(int nCh = EOF);
    virtual int underflow() { return EOF; }
};
/////////////////////////////////////////////////////////////////////////////////////
int CConsoleBuf::overflow(int nCh)
{
    if (filter != NONE && (filter & eCurrentFilter) == 0)
        return 0;
    char *pb = pbase(), *pend = pptr();
    while (pb < pend)
        szBuf += *pb++;
    if (nCh != EOF)
        szBuf += (char)nCh;
    setp(this->pbase(), this->eback());
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
int CConsoleBuf::sync()
{
    overflow();
    theInterface.ConsoleOutput(szBuf);
    szBuf = "";
    return 0;
}

/////////////////////////////////////////////////////////////////////////////////////
// global parameters
static CConsoleBuf aiBuf(AI);
static CConsoleBuf eventBuf(EVENT);
static CConsoleBuf graphicsBuf(GRAPHICS);
static CConsoleBuf rpgBuf(RPG);
static CConsoleBuf unitAcksBuf(UNITACKS);
static CConsoleBuf netBuf(NET);
static CConsoleBuf allBuf(ALL);
static CConsoleBuf gameBuf(NONE);
ostream NConsole::ai(&aiBuf);
ostream NConsole::event(&eventBuf);
ostream NConsole::graphics(&graphicsBuf);
ostream NConsole::rpg(&rpgBuf);
ostream NConsole::net(&netBuf);
ostream NConsole::unitAcks(&unitAcksBuf);
ostream NConsole::all(&allBuf);
ostream NConsole::game(&gameBuf);

/////////////////////////////////////////////////////////////////////////////////////
// command processing
/////////////////////////////////////////////////////////////////////////////////////
static void ProcessScript(const CString &szStart, const CStringArray &szParams, const char *prefix,
                          const char *postfix)
{
    // process script command
    SScriptError error;
    CString szScript(prefix);
    szScript += szStart;
    for (int i = 0; i < szParams.GetSize(); i++)
    {
        szScript += " ";
        szScript += szParams[i];
    }
    szScript += postfix;
    if (!IWorldServer.LoadScriptText(szScript, true, &error))
        game << "Script error: " << error.text << endl;
}
/////////////////////////////////////////////////////////////////////////////////////
const char pszConsoleActivateWord[] = { 0x12, 0x0e, 0x0f, 0x08, 0x01, 0x07,
                                        0x0b, 0x07, 0x04, 0x09, 0x04, 0 };
namespace {
ModSettingsPolicy::Gui gui;
struct StarterNetwork {
    bool rejectServerMQ, mpSpeedControl, mpConsole;
    StarterNetwork()
    {
        const wchar_t *key = L"Software\\Gipat.Ru\\EI_Starter\\Settings";
        rejectServerMQ = RegGetBool(HKEY_CURRENT_USER, key, L"MQ client");
        mpSpeedControl = RegGetBool(HKEY_CURRENT_USER, key, L"MP Speed-control");
        mpConsole = RegGetBool(HKEY_CURRENT_USER, key, L"MP console");
    }
};
const StarterNetwork& NetworkSettings()
{
    static const StarterNetwork settings;
    return settings;
}
struct Starter {
 std::vector<std::string> registry,file;DWORD objects=100,landscape=100;
 Starter(){
  HKEY key=nullptr;
  if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Gipat.Ru\\EI_Starter\\Settings",0,KEY_READ,&key)==ERROR_SUCCESS){
   struct Close{HKEY key;~Close(){RegCloseKey(key);}}close{key};
   auto dword=[&](const wchar_t* name){DWORD type=0,bytes=4,value=100;
    return RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<LPBYTE>(&value),&bytes)==ERROR_SUCCESS&&type==REG_DWORD&&bytes==4?value:100u;};
   objects=dword(L"ObjectsDrawRadius");landscape=dword(L"LandscapeDrawRadius");
   DWORD type=0,bytes=0;
   if(RegQueryValueExW(key,L"Console AutoExec",nullptr,&type,nullptr,&bytes)==ERROR_SUCCESS&&type==REG_MULTI_SZ&&bytes%sizeof(wchar_t)==0){
    std::vector<wchar_t> data(bytes/sizeof(wchar_t));
    if(RegQueryValueExW(key,L"Console AutoExec",nullptr,&type,reinterpret_cast<LPBYTE>(data.data()),&bytes)==ERROR_SUCCESS&&type==REG_MULTI_SZ){
     for(const auto& line:ModSettingsPolicy::MultiString(data.data(),bytes/sizeof(wchar_t))){
      int n=WideCharToMultiByte(CP_ACP,0,line.c_str(),int(line.size()),nullptr,0,nullptr,nullptr);std::string text(n,'\0');
      if(n)WideCharToMultiByte(CP_ACP,0,line.c_str(),int(line.size()),&text[0],n,nullptr,nullptr);registry.push_back(text);}}}
  }
  std::ifstream input("2utoexec.cfg");std::string line;while(std::getline(input,line))file.push_back(line);
 }
};
const Starter& StarterSettings(){static const Starter settings;return settings;}
std::string ObjectText(CMapObject* object){return object?ModSettingsPolicy::Object(object->GetGameClassName(),object->GetID()):"null";}
void SetAddonDebug(const CStringArray& args){
 CString type; if(args.GetSize()>=2)type=args[1];
 const int level=args.GetSize()>=3?atoi((LPCSTR)args[2]):-1;
 if(type==""||type=="help"){
  game<<"Usage:\n\taddon debug {type} [level]\n\n"
        "Set debug level for type of addon's debugging settings\n"
        "or show current value if level is not specified\n\n"
        "Types:\n"
        "* log      - addon log file (current: "<<EngineDiagnostics::GetLogLevel(false)<<")\n"
        "* console  - addon's messages to console (current: "<<EngineDiagnostics::GetLogLevel(true)<<")\n"
        "\nLevels:\n"
        "* 4        - disable\n"
        "* 3        - error\n"
        "* 2        - warning\n"
        "* 1        - information\n"
        "* 0        - debug"<<endl;
 }else if(type=="console"){
  if(level>=0)EngineDiagnostics::SetLogLevel(true,level);
  else game<<"Current console log level: "<<EngineDiagnostics::GetLogLevel(true)<<endl;
 }else if(type=="log"){
  if(level>=0)EngineDiagnostics::SetLogLevel(false,level);
  else game<<"Current addon log level: "<<EngineDiagnostics::GetLogLevel(false)<<endl;
 }else game<<"Unknown debug type: "<<(LPCSTR)type<<endl;
}
void ProcessAddon(const CStringArray& args){
 auto* mod=ModRuntime::Resources();
 if(args.GetSize()&&args[0]=="info"){game<<"Integrated EIRebuild addon source 39454a3; mod: "<<(mod?mod->Directory():"none")<<endl;return;}
 if(args.GetSize()&&args[0]=="debug"){SetAddonDebug(args);return;}
 if(args.GetSize()&&args[0]!="help"){game<<"Unknown command: "<<(LPCSTR)args[0]<<endl;return;}
 game<<"addon info - integrated addon source and active mod\naddon debug [args]  - set addon's debug settings"<<endl;
}
void ProcessExecs(const CStringArray& args){
 if(args.GetSize()!=1){game<<"execs [script file]"<<endl;return;}
 if(!theWorldServer.script.Load(args[0])){auto error=theWorldServer.script.GetLastError();game<<"Script error: line "<<error.nLine<<", "<<error.text<<endl;}
}
void ProcessListGVar(const CStringArray&){
 auto& vars=theWorldServer.script.GetGlobalVars();
 auto list=[&](auto& set,const char* type,auto render){
  for(int i=0;i<set.GetSize();++i){const char* name=nullptr;if(!set.TryGetVarName(i,name))continue;
   game<<ModSettingsPolicy::Variable(name,type,render(set[i]))<<endl;}};
 list(vars.fVars,"float",[](float value){std::ostringstream out;out<<std::fixed<<std::setprecision(6)<<value;return out.str();});
 list(vars.szVars,"string",[](const CString& value){return "\""+std::string((LPCSTR)value)+"\"";});
 list(vars.oVars,"object",[](CScriptObject& value){return ObjectText(value.GetObject());});
 list(vars.gVars,"group",[](CScriptObjectGroup& value){std::string out;for(int i=0;i<value.GetSize();++i){if(i)out+=", ";out+=ObjectText(value.GetAt(i).GetObject());}return out.empty()?std::string("empty"):out;});
}
void ProcessNoGui(const CStringArray& args){
 std::vector<std::string> input;for(int i=0;i<args.GetSize();++i)input.emplace_back((LPCSTR)args[i]);
 if(!gui.Set(input)){game<<"nogui 0 - show all\nnogui 1 - hide GUI, show cursor\nnogui 2 - hide GUI and cursor"<<endl;return;}
 theCursor.Show(gui.cursor);game<<"GUI: "<<(gui.controls?"show":"hide")<<", cursor: "<<(gui.cursor?"show":"hide")<<endl;
}
}
void NConsole::InitializeNetworkSettings(){(void)NetworkSettings();}
bool NConsole::RejectServerMQ(){return NetworkSettings().rejectServerMQ;}
bool NConsole::MpSpeedControlEnabled(){return NetworkSettings().mpSpeedControl;}
bool NConsole::MpConsoleEnabled(){return NetworkSettings().mpConsole;}
bool NConsole::ApplyNoGui(bool& show){return gui.Apply(show);}
float NConsole::ObjectsDrawRadius(){return ModRuntime::Resources()?float(StarterSettings().objects):100.f;}
float NConsole::LandscapeDrawRadius(){return ModRuntime::Resources()?float(StarterSettings().landscape):100.f;}
void NConsole::RunAutoExec(const char* original){
 if(!ModRuntime::Resources()){Process(original);return;}
 const auto& settings=StarterSettings();ModSettingsPolicy::AutoExec(settings.registry,settings.file,original,[](const char* command){Process(command);});
}

void NConsole::Process(const char *pszCommand)
{
    // initialize commands map
    commands["help"] = ProcessHelp;
    size_t addedCount=0;const auto* added=ModSettingsPolicy::Commands(addedCount);
    for(size_t i=0;i<addedCount;++i){ConsoleCommand handler=nullptr;
        switch(added[i].command){
        case ModSettingsPolicy::Command::Addon:handler=ProcessAddon;break;
        case ModSettingsPolicy::Command::Execs:handler=ProcessExecs;break;
        case ModSettingsPolicy::Command::ListGvar:handler=ProcessListGVar;break;
        case ModSettingsPolicy::Command::NoGui:handler=ProcessNoGui;break;}
        (added[i].admin?commands:commandsCommon)[added[i].name]=handler;
    }
    commands["drawall"] = ProcessDrawAll;
    commands["debuginfo"] = ProcessDebugInfo;
    commandsCommon["console"] = ProcessConsole;
    commands["history"] = ProcessHistory;
    commands["filter"] = ProcessFilter;
    commands["show"] = ProcessShow;
    commands["listvar"] = ProcessListVar;
    commands["loadvar"] = ProcessLoadVar;
    commandsCommon["lastfps"] = ProcessLastFPS;
    commandsCommon["exec"] = ProcessExec;
    commandsCommon["execute"] = ProcessExec;
    commands["give"] = ProcessGive;
    commands["fadeout"] = ProcessFadeout;
    commands["join"] = ProcessJoin;
    commands["server"] = ProcessServer;
    commands["memusage"] = ProcessMemUsage;
    commands["lootall"] = ProcessLootAll;
    commands["net"] = ProcessNet;
    commands["disconnect"] = ProcessDisconnect;
    commands["generateacks"] = ProcessGenerateAcks;
    commands["days"] = ProcessDays;
    commands["partyresend"] = ProcessPartyResend;
    commandsCommon["kick"] = ProcessKick;
    commandsCommon["ban"] = ProcessBan;
    commandsCommon["fps"] = ProcessFPS;
    commandsCommon["rate"] = ProcessRate;
    commandsCommon["localrate"] = ProcessLocalRate;

    // parse input string
    CString szCmd;
    CStringArray szParams;

    NStrProc::SplitStringQuotes(pszCommand, szParams);

    if (!szParams.GetSize()) // empty
        return;

    szCmd = szParams[0];
    szParams.RemoveAt(0);
    const bool bAllowCheats = !IWorldConnector.IsLMP() || MpConsoleEnabled();

    // processing
    if (szCmd[0] == '@') // script commands
    {
        if (bConsoleActive && bAllowCheats)
            ProcessScript(szCmd.Mid(1), szParams, "WorldScript(", ")");
        return;
    }
    else if (szCmd[0] == '#') // script function returning float
    {
        if (bConsoleActive && bAllowCheats)
            ProcessScript(szCmd.Mid(1), szParams, "WorldScript( ConsoleFloat( ", " ))");
        return;
    }
    else if (szCmd[0] == '$') // script function returning string
    {
        if (bConsoleActive && bAllowCheats)
            ProcessScript(szCmd.Mid(1), szParams, "WorldScript( ConsoleString( ", " ))");
        return;
    }
    else
    {
        szCmd.MakeLower();
        for (int i = 0; i < szParams.GetSize(); i++)
            szParams[i].MakeLower();
        if (szCmd == "quit" || szCmd == "exit") // exit/quit console command
        {
            NGame::QuitProgram(0);
        }
        else if (commandsCommon[szCmd]) // another console commands
            commandsCommon[szCmd](szParams);
        else if (!bConsoleActive)
        {
            char *pszCmd = szCmd.GetBuffer(12);
            while (*pszCmd)
                (*pszCmd++) ^= 0x66;
            szCmd.ReleaseBuffer();
            if (strcmp(szCmd, pszConsoleActivateWord) == 0)
            {
                bConsoleActive = true;
                game << "Activated!" << endl;
            }
            else
                game << "Unknown command" << endl;
        }
        else if (bConsoleActive && commands[szCmd]) // another console commands
        {
            if (bAllowCheats)
                commands[szCmd](szParams);
        }
        else // unknown command
            game << "Unknown command" << endl;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
template<class Output>
static void ApplyDrawAll(const CStringArray &args, bool restricted, int &visibleOnly, Output output)
{
    if (args.GetSize() == 0)
        return;
    if (restricted)
        output("Stop right now you criminal scum!");
    else
        visibleOnly = atoi(args[0].GetString()) ? 0 : 1;
}

void NConsole::ProcessDrawAll(const CStringArray &szParams)
{
    if (szParams.GetSize() == 0)
        return;
    CModPackage *package = ModRuntime::Current();
    const bool restricted = package && package->CombatSetting("JMVNoDrawall") != 0;
    ApplyDrawAll(szParams, restricted, theSettings.nDrawVisibleOnly,
                 [](const char *line) { game << line << endl; });
}

std::string NConsole::CheckDrawAllCommand()
{
    using Json = nlohmann::json;
    auto require = [](bool value, const char *message) {
        if (!value) throw std::runtime_error(message);
    };
    size_t count = 0;
    const auto *rules = ModConfiguration::CombatRules(count);
    bool found = false;
    for (size_t i = 0; i < count; ++i)
        if (strcmp(rules[i].key, "JMVNoDrawall") == 0) {
            require(!rules[i].fallback, "NoDrawall must default false");
            found = true;
        }
    require(found, "NoDrawall configuration rule missing");
    Json cases = Json::array();
    auto run = [&](const char *command, bool restricted, int &visibility, std::vector<std::string> &lines) {
        CStringArray args;
        NStrProc::SplitStringQuotes(command, args);
        require(args.GetSize() && args[0] == "drawall", "Local parser command mismatch");
        args.RemoveAt(0);
        for (int i = 0; i < args.GetSize(); ++i) args[i].MakeLower();
        ApplyDrawAll(args, restricted, visibility, [&](const char *line) { lines.emplace_back(line); });
    };
    {
        int visibility = 7; std::vector<std::string> lines;
        run("drawall -2tail 0", false, visibility, lines);
        require(visibility == 0 && lines.empty(), "First nonzero atoi argument differs");
        cases.push_back({{"id", "allowed-first-nonzero"}, {"visibility", visibility}, {"lines", lines}});
    }
    {
        int visibility = 0; std::vector<std::string> lines;
        run("drawall 0", false, visibility, lines);
        require(visibility == 1, "Zero argument differs");
        run("drawall help", false, visibility, lines);
        run("drawall", false, visibility, lines);
        require(visibility == 1 && lines.empty(), "Text atoi or empty argument differs");
        cases.push_back({{"id", "zero-text-and-no-argument"}, {"visibility", visibility}, {"lines", lines}});
    }
    {
        int visibility = 7; std::vector<std::string> lines;
        run("drawall 0", true, visibility, lines);
        run("drawall", true, visibility, lines);
        require(visibility == 7 && lines.size() == 1 && lines[0] == "Stop right now you criminal scum!",
                "Restriction output or empty-argument order differs");
        cases.push_back({{"id", "restricted-and-empty"}, {"visibility", visibility}, {"lines", lines}});
    }
    return Json({{"success", true}, {"cases", cases}, {"productionHelper", true}, {"realCStringParser", true},
                 {"defaultFalseRule", true}, {"globalSettingsWritten", false}, {"consoleProcessRun", false},
                 {"hostCreated", false}, {"worldCreated", false}, {"screenCreated", false}, {"deviceCreated", false},
                 {"audioStarted", false}, {"saveRead", false}}).dump();
}

void NConsole::ProcessDebugInfo(const CStringArray &szParams)
{
    if (!szParams.GetSize())
    {
        game << "Invalid debug info position" << endl;
        return;
    }
    CRect rect(10, 10, 290, 290);
    if (szParams[0] == "left" || szParams[0] == "l")
        rect = CRect(10, 10, 290, 590);
    else if (szParams[0] == "right" || szParams[0] == "r")
        rect = CRect(510, 10, 790, 590);
    else if (szParams[0] == "lefttop" || szParams[0] == "lt")
        rect = CRect(10, 10, 290, 290);
    else if (szParams[0] == "righttop" || szParams[0] == "rt")
        rect = CRect(510, 10, 790, 290);
    else
    {
        game << "Invalid debug info position" << endl;
        return;
    }
    theInterface.DebugInfoResize(rect);
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessConsole(const CStringArray &szParams)
{
    if (!szParams.GetSize())
    {
        game << "Invalid console position" << endl;
        return;
    }
    CRect rect(0, 0, 800, 150);
    if (szParams[0] == "left" || szParams[0] == "l")
        rect = CRect(0, 0, 200, 600);
    else if (szParams[0] == "right" || szParams[0] == "r")
        rect = CRect(600, 0, 800, 600);
    else if (szParams[0] == "top" || szParams[0] == "t")
        rect = CRect(0, 0, 800, 150);
    else if (szParams[0] == "bottom" || szParams[0] == "b")
        rect = CRect(0, 450, 800, 600);
    else if (szParams[0] == "center" || szParams[0] == "c")
        rect = CRect(200, 150, 600, 450);
    else if (szParams[0] == "fullscreen" || szParams[0] == "f")
        rect = CRect(0, 0, 800, 600);
    else
    {
        game << "Invalid console position" << endl;
        return;
    }
    theInterface.ConsoleResize(rect);
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessHistory(const CStringArray &szParams)
{
    if (!szParams.GetSize())
    {
        game << "Invalid parameters" << endl;
        return;
    }
    int num = atoi(szParams[0]);
    theInterface.ConsoleHistory(num);
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessFilter(const CStringArray &szParams)
{
    // set filter mode
    EFilter eSetFilter = NONE;
    if (!szParams.GetSize())
    {
        game << "AI: " << (eCurrentFilter & AI ? "on" : "off");
        game << "\nEVENT: " << (eCurrentFilter & EVENT ? "on" : "off");
        game << "\nGRAPHICS: " << (eCurrentFilter & GRAPHICS ? "on" : "off");
        game << "\nRPG: " << (eCurrentFilter & RPG ? "on" : "off");
        game << "\nUNITACKS: " << (eCurrentFilter & UNITACKS ? "on" : "off");
        game << "\nNET: " << (eCurrentFilter & NET ? "on" : "off");
        game << endl;
        return;
    }

    if (szParams[0] == "none")
        eSetFilter = NONE;
    else if (szParams[0] == "ai")
        eSetFilter = AI;
    else if (szParams[0] == "event")
        eSetFilter = EVENT;
    else if (szParams[0] == "graphics")
        eSetFilter = GRAPHICS;
    else if (szParams[0] == "rpg")
        eSetFilter = RPG;
    else if (szParams[0] == "unitacks")
        eSetFilter = UNITACKS;
    else if (szParams[0] == "net")
        eSetFilter = NET;
    else if (szParams[0] == "all")
        eSetFilter = ALL;
    else
    {
        game << "Invalid parameters" << endl;
        return;
    }

    if (szParams.GetSize() > 1)
    {
        if (szParams[1] == "1" || szParams[1] == "on")
            eCurrentFilter = static_cast<EFilter>(eCurrentFilter | eSetFilter);
        else if (szParams[1] == "0" || szParams[1] == "off")
            eCurrentFilter = static_cast<EFilter>(eCurrentFilter & ~eSetFilter);
        else
            game << "Invalid parameters" << endl;
    }
    else
        eCurrentFilter = eSetFilter;
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessShow(const CStringArray &szParams)
{
    if (szParams.GetSize() < 2)
    {
        game << "Invalid number of parameters for command \"show\"" << endl;
        return;
    }
    // fps
    if (szParams[0] == "fps")
    {
        if (szParams[1] == "1" || szParams[1] == "on")
            theWorld.ShowFPSOn();
        else if (szParams[1] == "0" || szParams[1] == "off")
            theWorld.ShowFPSOff();
    }
    else if (szParams[0] == "managers")
    {
        if (szParams[1] == "1" || szParams[1] == "on")
            theWorld.ShowManagersOn();
        else if (szParams[1] == "0" || szParams[1] == "off")
            theWorld.ShowManagersOff();
    }
    else if (szParams[0] == "render_info")
    {
        if (szParams[1] == "1" || szParams[1] == "on")
            theWorld.ShowRenderInfoOn();
        else if (szParams[1] == "0" || szParams[1] == "off")
            theWorld.ShowRenderInfoOff();
    }
    else if (szParams[0] == "world_data")
    {
        if (szParams[1] == "1" || szParams[1] == "on")
            theWorld.ShowWorldDataOn();
        else if (szParams[1] == "0" || szParams[1] == "off")
            theWorld.ShowWorldDataOff();
    }
    else
        game << "Invalid parameters in command 'show'" << endl;
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessListVar(const CStringArray &szParams)
{
    if (szParams.GetSize() == 0)
    {
        game << "usage: listvar [Nplayer] <[output file name]>" << endl;
        return;
    }
    CPlayer *pPlayer;
    if (IWorldConnector.IsLMP())
        pPlayer = theWorld.GetPlayer()->GetPlayer();
    else
        pPlayer = theWorldServer.GetPlayer(atoi(szParams[0]));
    if (!pPlayer)
    {
        game << "Wrong player number" << endl;
        return;
    }

    BOOL bOutputFile = FALSE;
    CString szStr;
    CStdioFile file;
    if (szParams.GetSize() > 1)
    {
        bOutputFile = file.Open(szParams[1], CFile::modeWrite | CFile::modeCreate);
        if (!bOutputFile)
            game << "Cannot open file " << szParams[1] << " for writing" << endl;
    }

    for (auto it = pPlayer->party.vars.begin(); it != pPlayer->party.vars.end(); ++it)
    {
        game << it->first << " = " << it->second << endl;
        if (bOutputFile)
        {
            szStr.Format("%s %g\n", it->first, it->second);
            file.WriteString(szStr);
        }
    }

    if (bOutputFile)
        file.Close();
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessLoadVar(const CStringArray &szParams)
{
    if (szParams.GetSize() < 2)
    {
        game << "usage: loadvar [N player] [input file name]" << endl;
        return;
    }
    CPlayer *pPlayer = theWorldServer.GetPlayer(atoi(szParams[0]));
    if (!pPlayer)
    {
        game << "Wrong player number" << endl;
        return;
    }
    CPlayerServer *pPlayerServer = static_cast<CPlayerServer *>(pPlayer->GetPlayerSpecific());
    if (!pPlayerServer)
    {
        game << "Wrong player number" << endl;
        return;
    }

    CString szStr;
    CStringArray szStrArray;
    CStdioFile file;
    if (!file.Open(szParams[1], CFile::modeRead))
    {
        game << "Cannot open file " << szParams[1] << " for reading" << endl;
        return;
    }

    while (file.ReadString(szStr))
    {
        NStrProc::SplitString(szStr, szStrArray, ' ');
        if (szStrArray.GetSize() > 1)
            pPlayerServer->SetVar(szStrArray[0], static_cast<float>(atof(szStrArray[1])));
    }

    file.Close();
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessLastFPS(const CStringArray &szParams)
{
    game << "Current FPS: " << 1000.0f / static_cast<float>(GetRenderer()->GetLastFrameTime())
         << endl;
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessExec(const CStringArray &szParams)
{
    CString szCommand, szFileName;
    if (szParams.GetSize() == 0)
    {
        game << "usage: exec<ute> [input file name]" << endl;
        return;
    }
    szFileName = szParams[0];
    szFileName.TrimLeft('\"');
    szFileName.TrimRight('\"');
    CStdioFile file;
    if (file.Open(szFileName, CFile::modeRead | CFile::typeText) == FALSE)
    {
        game << "Cannot open file " << szParams[0] << " for reading" << endl;
        return;
    }
    while (file.ReadString(szCommand))
        Process(szCommand);
    file.Close();
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessGive(const CStringArray &szParams)
{
    if (szParams.GetSize() < 2)
    {
        game << "IMPORTANT: use only from global map" << endl;
        game << "give [NPlayer] money [NMoney]" << endl;
        game << "give [NPlayer] exp [NExp]" << endl;
        return;
    }
    CPlayer *pPlayer = theWorldServer.GetPlayer(atoi(szParams[0]));
    if (!pPlayer)
    {
        game << "Wrong player number" << endl;
        return;
    }
    CPlayerServer *pPlayerServer = static_cast<CPlayerServer *>(pPlayer->GetPlayerSpecific());
    if (!pPlayerServer)
    {
        game << "Wrong player number" << endl;
        return;
    }

    if (szParams[1] == "money" && szParams.GetSize() > 2)
    {
        pPlayer->party.nMoney += atoi(szParams[2]);
        pPlayerServer->SendParty();
    }
    else if (szParams[1] == "exp" && szParams.GetSize() > 2)
    {
        int nUnits = pPlayer->party.people.GetSize();
        float fExp = float(atof(szParams[2]) / nUnits);
        for (int i = 0; i < nUnits; i++)
            pPlayer->party.people[i].stats.GainExperience(fExp);
        pPlayerServer->SendParty();
    }
    else if (szParams[1] == "assortment")
    {
        theInterface.GetCurrentScreen()->GenerateCheatAssortment();
    }
    else
    {
        game << "Give what?" << endl;
        return;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessJoin(const CStringArray &szParams)
{
    if (szParams.GetSize() != 1)
    {
        game << "Join has 1 parameter, server name" << endl;
        return;
    }
    CParty crapSquad;
    crapSquad.CreatePersonage("F", "Human Hero", "Hercules");
    IWorldConnector.ReqConnectTcpAddress(szParams[0], crapSquad);
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessServer(const CStringArray &szParams)
{
    if (szParams.GetSize() != 1)
    {
        game << "Join has 1 parameter, map name" << endl;
        return;
    }
    game << "Starting up a server" << endl;
    CParty crapSquad;
    IWorldConnector.ReqStartLMPServer(szParams[0], crapSquad);
    crapSquad.CreatePersonage("F", "Human Hero", "Serv");
}
/////////////////////////////////////////////////////////////////////////////////////
extern void TypeMemoryUsage();
void NConsole::ProcessMemUsage(const CStringArray &szParams)
{
    TypeMemoryUsage();
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessLootAll(const CStringArray &szParams)
{
    game << "Looting..." << endl;
    theWorldServer.LootAllCorpses();
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessHelp(const CStringArray &szParams)
{
    game << "Commands are:" << endl;
    game << "ADDON [info|debug], EXECS [script file], LISTGVAR, NOGUI [0|1|2]" << endl;

    game << "HELP" << endl;
    game << "  show this screen" << endl;
    game << "DEBUGINFO [pos]" << endl;
    game << "  change debug info position; <pos> = l<eft>, r<ight>, rt, righttop, lt, lefttop"
         << endl;
    game << "CONSOLE [pos]" << endl;
    game << "  change console position; <pos> = l<eft>, r<ight>, t<op>, b<ottom>, c<enter>, "
            "f<ullscreen>"
         << endl;
    game << "HISTORY [N lines]" << endl;
    game << "  change console history depth" << endl;
    game << "FILTER <[type] [1|on|0|off]]>" << endl;
    game << "  filter system messages; [type] = none, ai, event, graphics, rpg, net, unitacks, all"
         << endl;
    //	game << "SHOW" << endl;
    //	"fps managers render_info world_data"
    game << "LISTVAR [N player] <[output file name]>" << endl;
    game << "  dump list of global script variables to console and to file if needed" << endl;
    game << "LOADVAR [N player] [input file name]" << endl;
    game << "  load previously dumped list of global script variables from file" << endl;
    game << "LASTFPS" << endl;
    game << "  show last fps" << endl;
    game << "EXEC<UTE> [input file name]" << endl;
    game << "  execute text file with console commands" << endl;
    game << "QUIT, EXIT" << endl;
    game << "  exit game" << endl;
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessFadeout(const CStringArray &szParams)
{
    if (szParams.GetSize() > 0)
    {
        theInterface.GetIEffect()->Fadeout(atoi(szParams[0]));
        game << "Ok" << endl;
    }
    else
        game << "FADEOUT nMillSec" << endl;
}
/////////////////////////////////////////////////////////////////////////////////////
extern int nShowClientTimeBalance;
void NConsole::ProcessNet(const CStringArray &szParams)
{
    if (szParams.GetSize() != 2)
    {
        game << "net client 0/1 - show client traffic" << endl;
        game << "net server 0/1 - show server traffic" << endl;
        game << "net time 0/1   - show client time counting info" << endl;
        return;
    }
    if (szParams[0].CompareNoCase("client") == 0)
        theWorld.GetPlayer()->GetPlayer()->bShowTraffic = atoi(szParams[1]);
    if (szParams[0].CompareNoCase("server") == 0)
    {
        CPlayer *pPlayer = theWorldServer.GetPlayer(0);
        if (pPlayer)
            pPlayer->bShowTraffic = atoi(szParams[1]);
    }
    if (szParams[0].CompareNoCase("time") == 0)
        nShowClientTimeBalance = atoi(szParams[1]);
    if (szParams[0].CompareNoCase("zone") == 0)
    {
        if (pChatClient->IsValid())
        {
            pChatClient->Command(CC_SELECT_ZONE, szParams[1]);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessDisconnect(const CStringArray &szParams)
{
    IWorldConnector.ClientSideDisconnect();
}
/////////////////////////////////////////////////////////////////////////////////////
void ProcOut(CStdioFile &file, const SAckEntryVector *pVector)
{
    if (!pVector)
        return;
    CString szTmp;
    for (auto it = pVector->begin(); it != pVector->end(); ++it)
        if (!it->szDescription.empty())
        {
            szTmp.Format("#say %s\n", it->szKey.c_str());
            file.WriteString(szTmp);
            szTmp.Format("%s\n", it->szDescription.c_str());
            file.WriteString(szTmp);
        }
}
void ProcOut2(CStdioFile &file, const SAckEntryBoredVector *pVector)
{
    if (!pVector)
        return;
    CString szTmp;
    for (auto it = pVector->begin(); it != pVector->end(); ++it)
        if (!it->szDescription.empty())
        {
            szTmp.Format("#say %s\n", it->szKey.c_str());
            file.WriteString(szTmp);
            szTmp.Format("%s\n", it->szDescription.c_str());
            file.WriteString(szTmp);
        }
}
void NConsole::ProcessGenerateAcks(const CStringArray &szParams)
{
    CStdioFile file;
    if (file.Open(IWorld.GetWorkPath() + "scenack.txt", CFile::modeCreate | CFile::modeWrite) ==
        FALSE)
        return;
    int i;
    std::unordered_map<DWORD, SAckEntryVector *>::const_iterator it;
    for (i = 0; i < theAckDatabase.ackHeroAcks.GetNumEntries(); i++)
    {
        const SAckHeroAck *pAck = theAckDatabase.ackHeroAcks[i];
        for (it = pAck->acks.begin(); it != pAck->acks.end(); ++it)
            ProcOut(file, it->second);
        ProcOut2(file, &pAck->ackBored);
    }
    for (i = 0; i < theAckDatabase.ackUnitAcks.GetNumEntries(); i++)
    {
        const SAckUnitGeneral *pAck = theAckDatabase.ackUnitAcks[i];
        for (it = pAck->acks.begin(); it != pAck->acks.end(); ++it)
            ProcOut(file, it->second);
    }
    for (i = 0; i < theAckDatabase.ackNPCAcks.GetNumEntries(); i++)
    {
        const SAckNPC *pAck = theAckDatabase.ackNPCAcks[i];
        for (it = pAck->acks.begin(); it != pAck->acks.end(); ++it)
            ProcOut(file, it->second);
    }
    file.Close();
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessDays(const CStringArray &szParams)
{
    int i, nYear, nMonth, nDay;
    __time32_t time1, time2, t;

    if (szParams.GetSize() < 2)
        goto wrongpar;
    if (szParams[0].GetLength() < 8 || szParams[1].GetLength() < 8)
        goto wrongpar;
    for (i = 0; i < 8; i++)
    {
        char c = szParams[0][i];
        if (c < '0' || c > '9')
            goto wrongpar;
    }
    for (i = 0; i < 8; i++)
    {
        char c = szParams[1][i];
        if (c < '0' || c > '9')
            goto wrongpar;
    }
    nYear = atoi(szParams[0].Mid(4, 4));
    if (nYear < 1900)
        goto wrongpar;
    nMonth = atoi(szParams[0].Mid(2, 2));
    if (nMonth < 1 || nMonth > 12)
        goto wrongpar;
    nDay = atoi(szParams[0].Mid(0, 2));
    if (nDay < 1 || nDay > 31)
        goto wrongpar;
    time1 = CTime32::Make(nYear, nMonth, nDay, 6, 0, 0);

    nYear = atoi(szParams[1].Mid(4, 4));
    if (nYear < 1900)
        goto wrongpar;
    nMonth = atoi(szParams[1].Mid(2, 2));
    if (nMonth < 1 || nMonth > 12)
        goto wrongpar;
    nDay = atoi(szParams[1].Mid(0, 2));
    if (nDay < 1 || nDay > 31)
        goto wrongpar;
    time2 = CTime32::Make(nYear, nMonth, nDay, 7, 1, 1);

    game << "Your age: " << (time2 - time1) / (24 * 3600) << " days" << endl;
    return;

wrongpar:
    game << "usage: days [ddmmyyyy] [DDMMYYYY]" << endl;
    game << "ddmmyyyy - date of birth" << endl;
    game << "DDMMYYYY - current date" << endl;
    game << "example:" << endl;
    game << "> days 25011978 01072000" << endl;
    game << "Your age: 8192 days" << endl;
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessPartyResend(const CStringArray &szParams)
{
    CPlayer *pPlayer = theWorldServer.GetPlayer(0);
    if (pPlayer)
        ((CPlayerServer *)pPlayer->GetPlayerSpecific())->ResendParty();
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessKick(const CStringArray &szParams)
{
    if (szParams.GetSize() == 0 && pChatClient->IsValid())
    {
        if (pChatClient->pState->pPerson->bLeader)
            theInterface.GetCurrentScreen()->SetKickMode(KM_KICK);
        return;
    }
    if (szParams.GetSize() != 1 || szParams[0] == "")
        return;
    CString szName = szParams[0];
    if (szName[0] == '"' && szName[szName.GetLength() - 1] == '"')
    {
        szName = szName.Mid(1, szName.GetLength() - 2);
    }
    if (pChatClient->IsValid())
        pChatClient->Command(CC_KICK, szName);
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessBan(const CStringArray &szParams)
{
    if (szParams.GetSize() == 0 && pChatClient->IsValid())
    {
        if (pChatClient->pState->pPerson->bLeader)
            theInterface.GetCurrentScreen()->SetKickMode(KM_BAN);
        return;
    }
    if (szParams.GetSize() != 1 || szParams[0] == "")
        return;
    CString szName = szParams[0];
    if (szName[0] == '"' && szName[szName.GetLength() - 1] == '"')
    {
        szName = szName.Mid(1, szName.GetLength() - 2);
    }
    if (pChatClient->IsValid())
        pChatClient->Command(CC_BAN, szName);
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessFPS(const CStringArray &szParams)
{
    if (szParams.GetSize() < 1)
    {
        game << "Invalid number of parameters for command \"FPS\"" << endl;
        return;
    }
    if (szParams[0] == "1" || szParams[0] == "on")
        theInterface.ShowFPS(true);
    else if (szParams[0] == "0" || szParams[0] == "off")
        theInterface.ShowFPS(false);
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessRate(const CStringArray &szParams)
{
    if (szParams.GetSize() != 1)
    {
        game << "Rate has one parameter - connection rate in bytes per second" << endl;
        return;
    }
    int nRate = atoi(szParams[0]);
    nRate = Max(500, nRate);
    nRate = Min(100000, nRate);
    NCSParams::nCSRate = nRate;
}
/////////////////////////////////////////////////////////////////////////////////////
void NConsole::ProcessLocalRate(const CStringArray &szParams)
{
    if (szParams.GetSize() != 1)
    {
        game << "LocalRate has one parameter - connection rate in bytes per second" << endl;
        return;
    }
    int nRate = atoi(szParams[0]);
    nRate = Max(500, nRate);
    nRate = Min(100000, nRate);
    NCSParams::nCSLocalRate = nRate;
}
/////////////////////////////////////////////////////////////////////////////////////
