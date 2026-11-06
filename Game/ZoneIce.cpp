#include "StdAfx.h"
#include "ZoneIce.h"
#include "WorldServer.h"
#include "Player.h"
#include "PlayerServer.h"
#include "Party.h"
#include "ModPackage.h"
#include "ModRuntime.h"
#include "NavigationWorld.h"
#include "GameJournal.h"
#include "TypeIDs.h"
#include <map>
#include <set>
#include <vector>
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace {
constexpr int FirstId = 600001;
constexpr int Count = 19;
constexpr const char* StateKey = "ice.gz6g.state";
constexpr const char* EndKey = "ice.gz6g.endDay";

[[noreturn]] void Fail(const char* reason) {
    throw std::runtime_error(std::string("ZoneIceSync: ") + reason);
}
int State(CGSVariableSet& vars) {
    const float value = vars.GetVar(StateKey);
    if(value != 0 && value != 1 && value != 2) Fail("invalid party ice state");
    return int(value);
}
bool Scene(CWorldServer& world) {
    return Navigation::ResourceName(world.szZoneFileName) == "zone6.mpr" &&
        Navigation::ResourceName(world.szMobFileName) == "zone6.mob" &&
        world.GetCurrentZoneName().CompareNoCase("gz6g") == 0;
}
int Inspect(CWorldServer& world) {
    int count = 0;
    for(int i = 0; i < Count; ++i) {
        auto* object = world.GetMapObject(FirstId + i);
        if(!object) continue;
        char model[16]; sprintf_s(model, "z6i4%02d", i + 1);
        if(object->IsInvalid() || object->GetTypeID() != MOTID_BUILDING ||
            object->WhoAmI() != HPC_BUILDING ||
            _stricmp(object->GetMaskName(), model) != 0)
            Fail("reserved ice ID belongs to an invalid or foreign object");
        ++count;
    }
    return count;
}

// Pin the exact validated reinforcement against writes/deletion until import
// finishes. The ordinary MOB reader reopens this same explicit mod-owned path.
class Reinforcement {
    CFile file;
    std::vector<unsigned char> bytes;
    struct Chunk { unsigned tag; size_t begin, end; };
    unsigned Word(size_t at) const {
        unsigned value; memcpy(&value, bytes.data() + at, 4); return value;
    }
    bool Next(size_t& at, size_t end, Chunk& chunk) const {
        if(at == end) return false;
        if(at > end || end - at < 8) Fail("truncated ice MOB header");
        const auto size = Word(at + 4);
        if(size < 8 || size > end - at) Fail("invalid ice MOB chunk size");
        chunk = {Word(at), at + 8, at + size}; at += size; return true;
    }
public:
    std::string path;
    Reinforcement() {
        auto* mod = ModRuntime::Resources();
        if(!mod) Fail("missing mod resources");
        path = (std::filesystem::path(mod->Directory()) / "Maps" / "zone6_ice.mob").string();
        if(!file.Open(path.c_str(), CFile::modeRead | CFile::shareDenyWrite | CFile::typeBinary))
            Fail("cannot open zone6_ice.mob");
        const auto size = file.GetLength();
        if(size < 8 || size > 16 * 1024 * 1024) Fail("invalid ice MOB size");
        bytes.resize(size_t(size));
        if(file.Read(bytes.data(), UINT(size)) != size) Fail("incomplete ice MOB read");
        size_t at = 0; Chunk root;
        if(!Next(at, bytes.size(), root) || root.tag != 0xA000 || at != bytes.size())
            Fail("ice MOB must contain one object database");
        bool haveObjects = false, haveSecondary = false; std::set<unsigned> seen;
        at = root.begin; Chunk section;
        while(Next(at, root.end, section)) {
            // Only one empty secondary marker; no primary MOB, script,
            // diplomacy, world settings or graph may enter this path.
            if(section.tag == 0xD000 && section.begin == section.end && !haveSecondary) {
                haveSecondary = true; continue;
            }
            if(section.tag != 0xB000 || haveObjects) Fail("unexpected ice MOB section");
            haveObjects = true;
            size_t objectAt = section.begin; Chunk object;
            while(Next(objectAt, section.end, object)) {
                if(object.tag != 0xB001) Fail("non-static ice MOB record");
                size_t fieldAt = object.begin; Chunk field;
                std::map<unsigned, Chunk> fields;
                while(Next(fieldAt, object.end, field))
                    if(!fields.emplace(field.tag, field).second) Fail("duplicate ice MOB field");
                if(!fields.count(0xB002) || !fields.count(0xB003) || !fields.count(0xB006))
                    Fail("missing ice MOB identity field");
                const auto idField = fields.at(0xB002), typeField = fields.at(0xB003);
                if(idField.end - idField.begin != 4 || typeField.end - typeField.begin != 4)
                    Fail("invalid ice MOB ID/type size");
                const unsigned id = Word(idField.begin);
                if(id < FirstId || id >= FirstId + Count || Word(typeField.begin) != MOTID_BUILDING ||
                    !seen.insert(id).second) Fail("invalid ice MOB ID/type");
                char model[16]; sprintf_s(model, "z6i4%02u", id - FirstId + 1);
                const auto modelField = fields.at(0xB006);
                const std::string actual(reinterpret_cast<const char*>(bytes.data() + modelField.begin),
                    modelField.end - modelField.begin);
                if(actual != model) Fail("ice MOB model does not match ID");
            }
        }
        if(!haveSecondary) Fail("ice MOB must be secondary");
        if(seen.size() != Count) Fail("ice MOB must contain all 19 objects");
    }
};

void Apply(CWorldServer& world, CGSVariableSet* vars, bool realEntry, bool loading) {
    const int count = Inspect(world);
    // Inactive visited-zone documents have no party. Restore their physical
    // snapshot only. The travelling party decides when that zone is entered.
    const int state = vars ? State(*vars) : (count ? 1 : 0);
    if(state == 1 && count != 0 && count != Count) Fail("partial ice object set");
    bool expired = false;
    if(vars && state == 1) {
        const float end = vars->GetVar(EndKey), hours = vars->GetVar("gtime");
        if(!std::isfinite(end) || end < 30 || !std::isfinite(hours) || hours < 0)
            Fail("invalid ice deadline or gtime");
        expired = realEntry && hours / 24.0f >= end;
    }
    const int desired = expired ? 2 : state;
    if(state == 0 && count) Fail("ice exists before the first freeze");
    if(desired == 1 && !count && !realEntry)
        Fail("saved/synchronized ice is missing; automatic reimport is forbidden");
    auto& navigation = world.aiMap.Navigation();
    if(!loading && navigation.IsIceVariant() == (desired == 1) &&
        (desired == 1 ? count == Count : count == 0)) return;

    // Decode the complete navigation candidate before mutating world objects.
    auto candidate = navigation.PrepareVariant(desired == 1);
    std::unique_ptr<Reinforcement> reinforcement;
    if(desired == 1 && count == 0) reinforcement = std::make_unique<Reinforcement>();
    try {
        if(reinforcement) {
            if(!world.AddSecondaryMob(reinforcement->path.c_str(), true) || Inspect(world) != Count)
                Fail("ice reinforcement import did not produce all 19 objects");
        } else if(desired != 1) {
            for(int i = 0; i < Count; ++i)
                if(world.GetMapObject(FirstId + i)) world.RemoveMapObject(FirstId + i);
            if(Inspect(world)) Fail("ice removal was incomplete");
        }
        navigation.CommitVariant(std::move(candidate), world);
        if(expired) {
            vars->SetVar(StateKey, 2);
            vars->SetVar("z.gz6g.frozen", 0);
        }
        if(vars) if(auto* player = world.GetPlayer(0))
            if(vars != &player->party.vars) ZoneIce::CopyState(player->party.vars, *vars);
        GameJournal::Event("ZONE", "ice_sync", "entry=%d state=%d variant=%s objects=%d",
            int(realEntry), desired, desired == 1 ? "ice" : "normal", Inspect(world));
    } catch(...) {
        // Staged saves are discarded. A failed live entry cannot resume a
        // partially imported world or expose a mismatched geometry/mesh pair.
        world.bCanRunScript = FALSE; world.bMapLoaded = FALSE; throw;
    }
}
}

namespace ZoneIce {
void CopyState(CGSVariableSet& destination, CGSVariableSet& source) {
    for(const char* key : {StateKey, EndKey, "z.gz6g.frozen"})
        destination.SetVar(key, source.GetVar(key));
}
bool Load(CWorldServer& world, CGSVariableSet* arriving) {
    if(!Scene(world)) return false;
    auto* player = world.GetPlayer(0);
    auto* vars = arriving ? arriving : (player ? &player->party.vars : nullptr);
    Apply(world, vars, arriving != nullptr, true);
    return true;
}
void Sync() {
    auto& world = theWorldServer;
    if(!Scene(world)) return;
    try {
        auto* player = world.GetPlayer(0);
        if(!player) Fail("party 0 is unavailable");
        Apply(world, &player->party.vars, false, false);
    } catch(...) {
        world.bCanRunScript = FALSE; world.bMapLoaded = FALSE; throw;
    }
}
void Freeze() {
    auto* player = theWorldServer.GetPlayer(0);
    if(!player) Fail("party 0 is unavailable for freeze");
    auto& vars = player->party.vars;
    if(State(vars) != 0) return;
    const float hours = vars.GetVar("gtime");
    if(!std::isfinite(hours) || hours < 0) Fail("invalid freeze gtime");
    const float end = hours / 24.0f + 30.0f;
    if(!std::isfinite(end) || end <= hours / 24.0f) Fail("ice deadline overflow");
    // SetVar publishes normal party GS updates through the existing owner.
    auto* server = static_cast<CPlayerServer*>(player->GetPlayerSpecific());
    server->SetVar(EndKey, end);
    server->SetVar(StateKey, 1);
    server->SetVar("z.gz6g.frozen", 1);
}
}
