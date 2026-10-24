#pragma once
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
class CModPackage;
class CUnit;
struct SRPGUnitStats;
struct SRPGCommonStats;
struct lua_State;

namespace ModRuntime {
// A package belongs to one simulation-thread session. No process-global Lua fallback.
class Session {
public:
    explicit Session(CModPackage &package);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
};
CModPackage *Current();
CModPackage *Resources();
std::string ConfigPath(const char*);
std::string CameraPath(const char*);
std::string MoviePath(const char*);
std::string SaveRoot(const char*);
bool SinglePlayerAllowed();
std::string MapPath(const char*);
class Host {
 std::unique_ptr<CModPackage> package;
 std::unique_ptr<Session> session;
public:
 explicit Host(const std::string& directory);
 ~Host();
 Host(const Host&)=delete;Host&operator=(const Host&)=delete;
 static std::unique_ptr<Host> FromCommandLine();
 static std::unique_ptr<Host> FromStarterSelection();
};
// Shared with CUnitServer::L1ExecMove; caller retains pose/notification/clamping logic.
void SpendRunningMana(SRPGUnitStats&, CUnit&, SRPGCommonStats&);
nlohmann::json CheckEngineDispatch(CModPackage&, lua_State*, const std::string &perkArchive, const std::string &itemArchive,const std::string &unitArchive);
}
