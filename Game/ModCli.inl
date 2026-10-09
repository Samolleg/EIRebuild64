#pragma once
#include "ModCliTool.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "Script.h"
#include "ScriptErrors.h"
#include "SaveLoader.h"
#include "MapEditorChunkIDs.h"
#include "SimpleCryptographer.h"
#include "WorldServer.h"
#include "UnitDatabase.h"
#include "PerkDatabase.h"
#include "SpellDatabase.h"
#include "ItemDatabase.h"
#include "LeverDatabase.h"
#include "GeneralSettings.h"
#include "aiSettings.h"
#include "EngineDiagnostics.h"
#include "../shared/Common.h"
#include <shellapi.h>
#include <memory>
#include <stdexcept>
#include <utility>
#include <limits>

namespace ModCli {
enum Status { NotCli = -2, Continue = -1, Ok = 0, Error = 1,
              CannotRead = 2, NotFound = 3, ScriptError = 4 };

inline std::vector<std::string> Arguments() {
    int count = 0;
    LPWSTR* raw = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!raw) return {};
    struct Free { LPWSTR* p; ~Free() { LocalFree(p); } } free{raw};
    if (count < 2 || _wcsicmp(raw[1], L"addon")) return {};
    std::vector<std::string> args;
    for (int i = 0; i < count; ++i) args.push_back(WideToAnsi(raw[i]));
    return args;
}

struct Request {
    std::vector<std::string> args, params;
    std::string workDir, gameDir, runner, command;
    std::unique_ptr<ModRuntime::Host> host;
    bool selectionRead = false;

    explicit Request(std::vector<std::string> input) : args(std::move(input)) {
        const char* work = getenv("ADDON_WORKDIR");
        const char* label = getenv("ADDON_CLI_RUNNER");
        runner = label ? label : "EIRebuild.exe addon";
        if (!GetFullPath(work ? work : ".", workDir) || !DirExists(workDir) ||
            !GetFullPath(".", gameDir) || !DirExists(gameDir))
            throw std::runtime_error("Cannot initialize addon cmdline parser");
    }

    void Usage() const {
        printf("Usage:\n    %s [--moddir DIR] {command} [arguments...]\n\n"
               "Commands:\n    check        - Compile MOB or text scripts\n"
               "    check-deploy - Also compile the transient Heroes deployment contract\n"
               "    aigraph      - Generate and write an AI graph (.grp)\n"
               "    run          - Run the game\n"
               "    version      - Print EIRebuild and integrated source identity\n"
               "    help         - Show this help and exit\n", runner.c_str());
    }
    int UsageError(const char* format, const char* param = "") const {
        printf(format, param);
        printf("Try '%s help' for more information\n", runner.c_str());
        return Error;
    }
    int Parse() {
        size_t i = 2;
        while (i < args.size() && !strncmp(args[i].c_str(), "--", 2)) {
            if (!_stricmp(args[i].c_str()+2, "help")) { Usage(); return Ok; }
            if (_stricmp(args[i].c_str()+2, "moddir"))
                return UsageError("Invalid option: '%s'.\n", args[i].c_str());
            if (i+1 >= args.size())
                return UsageError("Not enough parameters for option: '%s'.\n", args[i].c_str());
            // Preserve option order/reloads; never overlap two active mod sessions.
            host.reset();
            host = std::make_unique<ModRuntime::Host>(args[i+1]);
            selectionRead = true;
            i += 2;
        }
        if (i == args.size()) return UsageError("Not enough arguments.\n");
        command = ToLower(args[i]);
        if (command != "help" && command != "version" && command != "run" &&
            command != "check" && command != "check-deploy" && command != "aigraph")
            return UsageError("Invalid command: '%s'.\n", args[i].c_str());
        params.assign(args.begin()+i+1, args.end());
        if (command == "help") { Usage(); return Ok; }
        if (command == "version") {
            printf("EIRebuild (local development build)\nCLI interface version: 1\n"
                   "Integrated native source: 39454a3a5bd25e27e8c10b0b104a591d2c106124\n"
                   "Engine source baseline: 0445cafa56dc69d101f2aabfc7936f20c79d0c6a\n"
                   "The baseline ID does not identify subsequent local modifications.\n"
                   "Compiled: %s %s\n", __DATE__, __TIME__);
            return Ok;
        }
        return Continue;
    }
    void SelectHost() {
        if (!selectionRead) {
            // Legacy command operands/ignored extras are not modern --mod options.
            host = ModRuntime::Host::FromStarterSelection();
            selectionRead = true;
        }
    }
    std::string FullPath(std::string path) const {
        // Use the same Windows resolver/CWD semantics as native GetFullPathAddon.
        if (!SetCurrentDirectoryA(workDir.c_str()))
            throw std::runtime_error("Cannot enter ADDON_WORKDIR");
        std::string result;
        const bool resolved = GetFullPath(path, result);
        const bool restored = SetCurrentDirectoryA(gameDir.c_str()) != FALSE;
        if (!resolved || !restored)
            throw std::runtime_error("Cannot resolve full path of " + path);
        return result;
    }
};

inline bool ReadMobScript(const std::string& path, CString& text) {
    // An explicit check operand is not a selected-mod map lookup. Read its exact
    // resolved file, then use the engine's chunk reader; ordinary map IO is unchanged.
    std::vector<char> bytes;
    if (!ReadFileFull(AnsiToWide(path), bytes) || bytes.empty() ||
        bytes.size() > (std::numeric_limits<UINT>::max)()) return false;
    CMemorySave file, objectDb, scriptChunk;
    file.StartSaving();
    file.Write(bytes.data(), static_cast<UINT>(bytes.size()));
    if (!file.GetChunk(ID_OBJECTDBFILE, objectDb)) return false;
    if (objectDb.GetChunk(ID_SS_TEXT, scriptChunk)) {
        if (!scriptChunk.GetSize()) return false;
        CSimpleCryptographer crypt;
        crypt.SetCodetData(scriptChunk.GetBuffer(), scriptChunk.GetSize());
        crypt.GetString(text);
    } else if (objectDb.GetChunk(ID_SS_TEXT_OLD, scriptChunk)) {
        scriptChunk.Seek(scriptChunk.GetSize());
        scriptChunk.Write(char(0));
        text = scriptChunk.GetBuffer();
    } else return false;
    return true;
}

inline int CheckSingle(const Request& request, std::string path, bool deploy) {
    path = request.FullPath(path);
    const char* ext = strrchr(path.c_str(), '.');
    const bool mob = ext && !strcmp(ext, ".mob");
    CString text;
    try {
        if (mob) {
            if (!ReadMobScript(path, text)) {
                printf("%s: Cannot read\n", path.c_str()); return CannotRead;
            }
        } else {
            std::string bytes;
            if (!ReadFileFull(AnsiToWide(path), bytes)) {
                printf("%s: Cannot read\n", path.c_str()); return CannotRead;
            }
            text = bytes.c_str();
        }
    } catch (CException* exception) {
        exception->Delete(); printf("%s: Cannot read\n", path.c_str()); return CannotRead;
    } catch (...) {
        printf("%s: Cannot read\n", path.c_str()); return CannotRead;
    }
    CScript script;
    script.Clear();
    script.LoadText(text, FALSE);
    SScriptError error = script.GetLastError();
    if (error.eError != SC_NO_ERROR) {
        printf("%s: Script error: line %d: %s\r\n", path.c_str(), error.nLine, (LPCSTR)error.text);
        return ScriptError;
    }
    if (deploy && mob) {
        // Same compiler/group context, no Clear and no execution of AddObject.
        CString deployment("WorldScript( AddObject( Heroes, GetObjectByID( \"0\" ) ) )");
        script.LoadText(deployment, FALSE);
        error = script.GetLastError();
        if (error.eError != SC_NO_ERROR) {
            printf("%s: Deploy contract error: line %d: %s\r\n", path.c_str(), error.nLine, (LPCSTR)error.text);
            return ScriptError;
        }
    }
    printf("%s: %s\r\n", path.c_str(), (LPCSTR)error.text);
    return Ok;
}

inline int Check(const Request& request, bool deploy) {
    if (request.params.empty()) {
        printf("Usage:\n    %s %s {PATH}...\n\nCheck script(s) for errors.\n"
               "PATH can be a directory, file, or file pattern. Directories check *.mob.\n%s",
               request.runner.c_str(), deploy ? "check-deploy" : "check",
               deploy ? "check-deploy also compiles the engine's transient Heroes deployment command; pass only complete deployable zone MOB files.\n" : "");
        return Ok;
    }
    bool success = true;
    int last = Ok;
    size_t checked = 0, missing = 0;
    for (const auto& operand : request.params) {
        std::string path = request.FullPath(operand);
        if (FileExists(path)) {
            ++checked; last = CheckSingle(request, path, deploy);
            if (last != Ok) success = false;
            continue;
        }
        WIN32_FIND_DATAA data;
        HANDLE handle;
        if (DirExists(path)) {
            AddTrailingSlash(path);
            handle = FindFirstFileA((path+"*.mob").c_str(), &data);
            if (handle == INVALID_HANDLE_VALUE) {
                printf("%s: There are no .mob files in directory\n", path.c_str());
                success = false; ++missing; continue;
            }
        } else {
            handle = FindFirstFileA(path.c_str(), &data);
            if (handle == INVALID_HANDLE_VALUE) {
                printf("%s: File(s) not found\n", path.c_str());
                success = false; ++missing; continue;
            }
            path = GetDirectoryName(path); AddTrailingSlash(path);
        }
        struct Find { HANDLE h; ~Find() { FindClose(h); } } find{handle};
        do {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            ++checked; last = CheckSingle(request, path+data.cFileName, deploy);
            if (last != Ok) success = false;
            fflush(stdout);
        } while (FindNextFileA(handle, &data));
    }
    if (checked == 1 && missing == 0) return last;
    if (checked == 0) return NotFound;
    return success ? Ok : ScriptError;
}

inline int GenerateGraph(const Request& request) {
    if (request.params.size() < 2) {
        printf("Usage:\n    %s aigraph {single|multi} mob_name [mpr_name]\n\n"
               "Generate AI Graph. Omitted mpr_name is derived from mob_name;\n"
               "zone10-lmp.mob -> zone10.mpr. Writes the resolved MOB stem + .grp.\n",
               request.runner.c_str());
        return Ok;
    }
    const std::string mode = ToLower(request.params[0]);
    const bool multi = mode == "multi";
    if (!multi && mode != "single") {
        printf("Invalid argument: expected 'multi' or 'single'\n"); return Error;
    }
    const std::string mob = request.params[1];
    std::string mpr = request.params.size() >= 3 ? request.params[2] : "";
    if (mpr.empty()) {
        if (EndsWith(ToLower(mob), ".mob")) mpr = mob.substr(0, mob.size()-4);
        const auto pos = ToLower(mpr).find("-lmp");
        if (pos != std::string::npos) mpr.replace(pos, 4, "");
        mpr += ".mpr";
    }
    std::string addonDir;
    if (auto* package = ModRuntime::Resources()) {
        addonDir = package->Directory(); AddTrailingSlash(addonDir);
    }
    for (const auto& name : {mob, mpr}) {
        if (!FileExists("maps\\"+name) && !FileExists(addonDir+"maps\\"+name)) {
            printf("%s: File not found\n", name.c_str()); return NotFound;
        }
    }
    ModCliTool::Scope graph(ModCliTool::Kind::Graph);
    CWorld::SetRegistryKey("Software\\Gipat.ru\\EI_Starter\\EvilIslands");
    CWorld::InitCurrentDir();
    theAIGeneralSettings.ReadFromFile(CWorld::GetCurrentPath()+"config\\ai.reg");
    theSettings.ReadSettingsFromRegistry(CWorld::GetRegistryKey());
    struct Server { ~Server() { theWorldServer.Destroy(); } } server;
    if (!theWorldServer.Create()) { printf("Failed to create server world\n"); return Error; }
    const char* db = multi ? "databaselmp.res" : "database.res";
    const CString resources = theWorldServer.GetResourcePath();
    if (!theUnitDatabase.Load(resources, db) || !thePerkDatabase.LoadDatabase(resources, db) ||
        !theSpellDatabase.Load(resources, db) || !theItemDatabase.Load(resources, db) ||
        !theLeverDatabase.Load(resources)) {
        printf("Failed to load databases\n"); return Error;
    }
    if (!theWorldServer.NewZone(mpr.c_str(), mob.c_str(), "")) {
        printf("Failed to load zone\n"); return Error;
    }
    theWorldServer.RemoveAllUnits();
    theWorldServer.aiMap.BuildGraph();
    CMemorySave data;
    data.StartSaving();
    theWorldServer.aiMap.StoreGraph(data);
    const CString fullMob = theWorldServer.GetFullMobName(mob.c_str());
    std::string output;
    if (!GetFullPath(ModRuntime::MapPath(fullMob), output)) {
        printf("Failed get full path of %s\n", (LPCSTR)fullMob); return Error;
    }
    if (EndsWith(ToLower(output), ".mob")) output.resize(output.size()-4);
    output += ".grp";
    CFileSave file;
    if (!file.OpenWrite(output.c_str())) { printf("Cannot write %s\n", output.c_str()); return Error; }
    file.CopyFrom(data);
    if (!file.IsOk()) { printf("Cannot write %s\n", output.c_str()); return Error; }
    file.CloseFile();
    printf("AI Graph successfully generated and saved to %s\n", output.c_str());
    return Ok;
}

// The caller resumes the ordinary game only for run; all other commands exit here.
inline int Dispatch(std::unique_ptr<ModRuntime::Host>& runHost, bool& run) {
    auto args = Arguments();
    if (args.empty()) return NotCli;
    ModCliTool::Scope tool(ModCliTool::Kind::Compiler);
    try {
        Request request(std::move(args));
        const int parsed = request.Parse();
        if (parsed != Continue) return parsed;
        request.SelectHost();
        if (request.command == "run") {
            runHost = std::move(request.host); run = true; return Continue;
        }
        EngineDiagnostics::Initialize(false);
        if (request.command == "aigraph") return GenerateGraph(request);
        return Check(request, request.command == "check-deploy");
    } catch (CException* exception) {
        char message[1024] = {};
        exception->GetErrorMessage(message, _countof(message));
        exception->Delete(); printf("%s\n", message); return Error;
    } catch (const std::exception& exception) {
        printf("%s\n", exception.what()); return Error;
    }
}
}
