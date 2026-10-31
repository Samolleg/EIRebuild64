#include "StdAfx.h"
#include "SettlementMob.h"
#include "ModPackage.h"
#include "ModRuntime.h"
#include "WorldServer.h"
#include "ObjectDBReader.h"
#include "GSVars.h"
#include "SaveSchema.h"
#include "GameJournal.h"
#include "UnitServer.h"
#include "OwnedRender/E1MobSource.h"
#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace Settlement {
namespace {
using Json = nlohmann::json;
[[noreturn]] void Fail(const std::string &message)
{
    throw std::runtime_error("Settlement MOB: " + message);
}
std::string Lower(std::string value)
{
    for (auto &c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
int Number(const std::string &text)
{
    if (text.empty()) Fail("empty object ID");
    std::uint64_t value = 0;
    for (unsigned char c : text) {
        if (c < '0' || c > '9') Fail("invalid object ID: " + text);
        value = value * 10 + c - '0';
        if (value > INT_MAX) Fail("object ID out of range: " + text);
    }
    return static_cast<int>(value);
}
bool LocalObjectID(const std::string &text)
{
    return text.size() > 2 && text.compare(0, 2, "id") == 0 &&
        std::all_of(text.begin() + 2, text.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
}
int Integer(const Json &value, int minimum = 0)
{
    if (!value.is_number_integer() || value.get<std::int64_t>() < minimum ||
        value.get<std::uint64_t>() > INT_MAX) Fail("integer out of range");
    return value.get<int>();
}
void Keys(const Json &object, std::initializer_list<const char *> allowed)
{
    if (!object.is_object()) Fail("expected an object");
    for (auto it = object.begin(); it != object.end(); ++it)
        if (std::none_of(allowed.begin(), allowed.end(), [&](const char *key) { return it.key() == key; }))
            Fail("unknown field: " + it.key());
}
std::string Resource(const std::string &value, const char *extension)
{
    if (value.empty() || value.find(':') != std::string::npos || value.front() == '/' || value.front() == '\\')
        Fail("MOB/map path must be relative");
    std::string path = value;
    std::replace(path.begin(), path.end(), '\\', '/');
    std::filesystem::path p(path);
    for (const auto &part : p) if (part == ".." || part == ".") Fail("invalid relative path: " + value);
    if (Lower(p.extension().string()) != extension) Fail("invalid resource extension: " + value);
    return Lower(path);
}
void SymbolName(const std::string &name)
{
    if (name.empty()) Fail("empty local name");
    Name qualified;
    if (ParseName(name, qualified)) Fail("local declarations must not contain another settlement prefix");
    for (unsigned char c : name)
        if (!std::isalnum(c) && c != '_' && c != '#' && c != '-') Fail("invalid local name: " + name);
}
const Catalog &CurrentCatalog()
{
    const auto *mod = ModRuntime::Current();
    if (!mod) Fail("no loaded mod");
    return mod->Settlements();
}
}

std::string Prefix(std::uint32_t id)
{
    if (!id || id > INT_MAX) Fail("invalid settlement number");
    return std::to_string(id) + "m_";
}
bool ParseName(const std::string &text, Name &name)
{
    name = {};
    std::size_t digits = 0;
    while (digits < text.size() && text[digits] >= '0' && text[digits] <= '9') ++digits;
    if (!digits || text.compare(digits, 2, "m_") != 0) return false;
    if (text.front() == '0') Fail("settlement number must be positive and canonical: " + text);
    name.settlement = static_cast<std::uint32_t>(Number(text.substr(0, digits)));
    name.local = text.substr(digits + 2);
    if (name.local.empty()) Fail("empty qualified name: " + text);
    return true;
}
std::string MapName(const std::string &text)
{
    std::string result = Lower(text);
    std::replace(result.begin(), result.end(), '\\', '/');
    if (result.compare(0, 5, "maps/") == 0) result.erase(0, 5);
    if (result.size() > 4 && (result.compare(result.size() - 4, 4, ".mpr") == 0 ||
                              result.compare(result.size() - 4, 4, ".mob") == 0)) result.resize(result.size() - 4);
    return result;
}

void Catalog::Load(const std::string &directory)
{
    definitions.clear();
    const auto path = std::filesystem::path(directory) / "settlements.json";
    if (!std::filesystem::exists(path)) return;
    std::ifstream input(path, std::ios::binary);
    if (!input) Fail("cannot open settlements.json");
    std::vector<std::set<std::string>> keys;
    auto callback = [&](int, Json::parse_event_t event, Json &value) {
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        else if (event == Json::parse_event_t::object_end) keys.pop_back();
        else if (event == Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second)
            Fail("duplicate JSON field: " + value.get<std::string>());
        return true;
    };
    const auto document = Json::parse(input, callback);
    Keys(document, {"version", "settlements"});
    if (Integer(document.at("version"), 1) != 1 || !document.at("settlements").is_array())
        Fail("unsupported settlements.json schema");
    if (document.at("settlements").size() > 4096) Fail("too many settlements");
    for (const auto &row : document.at("settlements")) {
        Keys(row, {"id", "revision", "map", "mob", "dependencies", "object_imports", "group_imports",
                   "state_imports", "aliases", "briefings", "peace"});
        Definition def;
        def.id = Integer(row.at("id"), 1);
        Integer(row.at("revision"), 1); // Data authors bump this when module content changes.
        const auto map = row.at("map").get<std::string>();
        def.map = MapName(Resource(map + (std::filesystem::path(map).extension().empty() ? ".mpr" : ""), ".mpr"));
        std::set<std::string> mobs;
        auto addMob = [&](const Json &value) {
            auto mob = Resource(value.get<std::string>(), ".mob");
            if (!mobs.insert(mob).second) Fail("duplicate MOB in " + Prefix(def.id));
            def.mobs.push_back(std::move(mob));
        };
        if (row.contains("dependencies")) {
            if (!row.at("dependencies").is_array()) Fail("dependencies must be an array");
            for (const auto &dependency : row.at("dependencies")) addMob(dependency);
        }
        if (row.contains("mob")) addMob(row.at("mob"));
        else if (!def.mobs.empty()) Fail("dependencies require a main settlement MOB");
        if (row.contains("peace")) {
            const auto &peace = row.at("peace");
            Keys(peace, {"polygon", "min_z", "max_z", "hysteresis", "quiet_seconds", "faction", "alarm_key", "gate"});
            auto real = [&](const Json &value) {
                if (!value.is_number() || !std::isfinite(value.get<float>())) Fail("invalid peace area coordinate");
                return value.get<float>();
            };
            auto point = [&](const Json &value) {
                if (!value.is_array() || value.size() != 2) Fail("peace area points require two coordinates");
                return Point{real(value[0]), real(value[1])};
            };
            if (!peace.at("polygon").is_array()) Fail("peace polygon must be an array");
            for (const auto &p : peace.at("polygon")) def.peace.polygon.push_back(point(p));
            def.peace.minZ = real(peace.at("min_z")); def.peace.maxZ = real(peace.at("max_z"));
            if (peace.contains("hysteresis")) def.peace.hysteresis = real(peace.at("hysteresis"));
            if (peace.contains("quiet_seconds")) def.peace.quietSeconds = real(peace.at("quiet_seconds"));
            def.peace.faction = Integer(peace.at("faction"));
            def.peace.alarmKey = Prefix(def.id) + peace.value("alarm_key", std::string("Alarm"));
            SymbolName(def.peace.alarmKey.substr(Prefix(def.id).size()));
            for (const auto *output : {"AlarmActive", "AlarmReasons", "Inside", "Mode"})
                if (Lower(def.peace.alarmKey) == Lower(Prefix(def.id) + output)) Fail("alarm_key shadows a peace output");
            const auto &gate = peace.at("gate");
            Keys(gate, {"object", "exit", "a", "b", "inside", "legacy_zone", "legacy_entrances"});
            def.peace.gateObject = Integer(gate.at("object")); def.peace.exit = Integer(gate.at("exit"));
            def.peace.gateA = point(gate.at("a")); def.peace.gateB = point(gate.at("b"));
            def.peace.inside = point(gate.at("inside"));
            def.peace.legacyZone = Lower(gate.at("legacy_zone").get<std::string>());
            if (!gate.at("legacy_entrances").is_array()) Fail("legacy entrances must be an array");
            std::set<int> entrances;
            for (const auto &value : gate.at("legacy_entrances")) {
                const int entrance = Integer(value);
                if (!entrances.insert(entrance).second) Fail("duplicate legacy entrance");
                def.peace.legacyEntrances.push_back(entrance);
            }
            ValidatePeaceArea(def.peace);
        }
        if (def.mobs.empty() && !def.peace.Enabled()) Fail("settlement needs a MOB or peace area");
        if (def.mobs.size() > 256) Fail("too many MOB dependencies");
        auto imports = [&](const char *field, std::map<std::string, std::string> &target, bool objects) {
            if (!row.contains(field)) return;
            const auto &values = row.at(field);
            if (!values.is_object()) Fail(std::string(field) + " must be an object");
            for (auto it = values.begin(); it != values.end(); ++it) {
                SymbolName(it.key());
                if (objects && LocalObjectID(Lower(it.key()))) Fail("object import uses a reserved ID name");
                std::string value;
                if (objects && it.value().is_number_integer()) value = std::to_string(Integer(it.value()));
                else value = it.value().get<std::string>();
                if (value.empty() || !target.emplace(Lower(it.key()), value).second) Fail("duplicate/empty import");
            }
        };
        imports("object_imports", def.objectImports, true);
        imports("group_imports", def.groupImports, false);
        if (row.contains("state_imports")) {
            const auto &values = row.at("state_imports");
            if (!values.is_object()) Fail("state_imports must be an object");
            for (auto it = values.begin(); it != values.end(); ++it) {
                const auto value = it.value().get<std::string>();
                if (it.key().empty() || value.empty() || (it.key().back() == '.') != (value.back() == '.'))
                    Fail("invalid state import");
                def.stateImports.emplace(it.key(), value);
            }
        }
        if (row.contains("aliases")) {
            const auto &aliases = row.at("aliases");
            if (!aliases.is_object()) Fail("aliases must be an object");
            for (auto it = aliases.begin(); it != aliases.end(); ++it) {
                SymbolName(it.key());
                if (LocalObjectID(Lower(it.key()))) Fail("actor alias uses a reserved ID name");
                if (!def.aliases.emplace(Lower(it.key()), Integer(it.value())).second) Fail("duplicate actor alias");
            }
        }
        if (row.contains("briefings")) {
            const auto &briefs = row.at("briefings");
            if (!briefs.is_object()) Fail("briefings must be an object");
            for (auto it = briefs.begin(); it != briefs.end(); ++it) {
                SymbolName(it.key()); Keys(it.value(), {"text", "reward", "shop"});
                Briefing brief;
                brief.text = it.value().value("text", std::string{});
                brief.reward = it.value().value("reward", std::string{});
                if (it.value().contains("shop")) brief.shop = Integer(it.value().at("shop"), 1);
                if (brief.text.empty() && !brief.shop) Fail("briefing needs text or shop binding");
                if (!def.briefings.emplace(it.key(), std::move(brief)).second) Fail("duplicate briefing");
            }
        }
        def.signature = row.dump();
        if (!definitions.emplace(def.id, std::move(def)).second) Fail("duplicate settlement number");
    }
}
const Definition *Catalog::Find(std::uint32_t id) const
{
    const auto found = definitions.find(id);
    return found == definitions.end() ? nullptr : &found->second;
}
const Definition &World::GetDefinition(std::uint32_t id) const
{
    const auto *definition = CurrentCatalog().Find(id);
    if (!definition || !Contains(id)) Fail("unregistered settlement: " + std::to_string(id));
    return *definition;
}
void World::Clear() { trade.reset(); dialogue = {}; instances.clear(); visitors.clear(); admitting = false; }
void World::ValidateActor(std::uint32_t id, const std::string &local) const
{
    const auto &def = GetDefinition(id);
    const auto &instance = instances.at(id);
    if (instance.names.count(Lower(local)) || def.objectImports.count(Lower(local))) return;
    if (LocalObjectID(local) && instance.objects.count(Number(local.substr(2)))) return;
    Fail("unknown briefing actor: " + Prefix(id) + local);
}
bool World::Reserved(int runtimeId) const
{
    for (const auto &instance : instances)
        for (const auto &object : instance.second.objects)
            if (object.second == runtimeId) return true;
    return false;
}
int World::ImportedObject(CWorldServer &world, const std::string &target) const
{
    const bool numeric = std::all_of(target.begin(), target.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
    return Object(world, 0, target, !numeric);
}
int World::Object(CWorldServer &world, std::uint32_t context, const std::string &reference, bool actorName) const
{
    std::string local = reference;
    std::set<std::pair<std::uint32_t, std::string>> visited;
    for (;;) {
        Name name;
        if (ParseName(local, name)) { context = name.settlement; local = name.local; actorName = true; }
        if (!context) return actorName ? GetPersonageIDByName(local.c_str()) : Number(local);
        if (!visited.emplace(context, Lower(local)).second) Fail("cyclic object import: " + reference);
        const auto &def = GetDefinition(context);
        const auto &instance = instances.at(context);
        if (actorName) {
            if (LocalObjectID(local))
                local.erase(0, 2);
            else {
                const auto key = Lower(local);
                const auto import = def.objectImports.find(key);
                if (import != def.objectImports.end()) {
                    local = import->second; context = 0;
                    actorName = !std::all_of(local.begin(), local.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
                    continue;
                }
                const auto alias = instance.names.find(key);
                if (alias == instance.names.end()) Fail("unknown actor: " + Prefix(context) + local);
                return instance.objects.at(alias->second);
            }
        }
        const int id = Number(local);
        const auto found = instance.objects.find(id);
        if (found == instance.objects.end()) Fail("unknown object: " + Prefix(context) + "id" + local);
        return found->second;
    }
}
std::string World::ObjectName(CWorldServer &world, std::uint32_t context, int runtimeId) const
{
    for (const auto &instance : instances)
        for (const auto &object : instance.second.objects)
            if (object.second == runtimeId) return Prefix(instance.first) + "id" + std::to_string(object.first);
    if (context) {
        for (const auto &import : GetDefinition(context).objectImports)
            if (ImportedObject(world, import.second) == runtimeId) return Prefix(context) + import.first;
        Fail("object has no declared settlement binding");
    }
    return std::to_string(runtimeId);
}
std::string World::Symbol(std::uint32_t context, const std::string &local, bool group) const
{
    Name name;
    if (ParseName(local, name)) { GetDefinition(name.settlement); return local; }
    const auto &def = GetDefinition(context);
    const auto imported = def.groupImports.find(Lower(local));
    return group && imported != def.groupImports.end() ? imported->second : Prefix(context) + local;
}
bool World::IsGroupImport(std::uint32_t context, const std::string &local) const
{
    return context && GetDefinition(context).groupImports.count(Lower(local)) != 0;
}
std::string World::StateKey(std::uint32_t context, const std::string &key) const
{
    if (!context) return key;
    const auto &def = GetDefinition(context);
    std::string imported;
    std::size_t longest = 0;
    for (const auto &item : def.stateImports) {
        if (item.first == key) return item.second;
        if (item.first.back() == '.' && key.compare(0, item.first.size(), item.first) == 0 && item.first.size() > longest) {
            imported = item.second + key.substr(item.first.size()); longest = item.first.size();
        }
    }
    if (longest) return imported;
    Name absolute;
    if (ParseName(key, absolute)) { GetDefinition(absolute.settlement); return key; }
    if (key.compare(0, 2, "b.") == 0) {
        const auto separator = key.find('.', 2);
        if (separator == std::string::npos || key.find('.', separator + 1) != std::string::npos) Fail("invalid briefing key: " + key);
        const auto actor = key.substr(2, separator - 2), topic = key.substr(separator + 1);
        Name a, b;
        const bool qa = ParseName(actor, a), qb = ParseName(topic, b);
        if (qa || qb) {
            if (!qa || !qb || a.settlement != b.settlement) Fail("mixed briefing namespace: " + key);
            ValidateActor(a.settlement, a.local); return key;
        }
        ValidateActor(context, actor);
        return "b." + Prefix(context) + actor + "." + Prefix(context) + topic;
    }
    if (key.compare(0, 2, "q.") == 0 || key.compare(0, 2, "z.") == 0)
        Fail("global quest/zone key needs an explicit state_import: " + key);
    return Prefix(context) + key;
}
const Briefing &World::ResolveBriefing(const std::string &qualified) const
{
    Name name;
    if (!ParseName(qualified, name)) Fail("briefing name must be qualified");
    const auto &briefings = GetDefinition(name.settlement).briefings;
    const auto found = briefings.find(name.local);
    if (found == briefings.end()) Fail("missing briefing resource binding: " + qualified);
    return found->second;
}
std::uint32_t World::BriefingOwner(const std::string &key) const
{
    if (key.compare(0, 2, "b.") != 0) return 0;
    const auto separator = key.find('.', 2);
    if (separator == std::string::npos) return 0;
    Name actor, topic;
    const bool a = ParseName(key.substr(2, separator - 2), actor);
    const bool b = ParseName(key.substr(separator + 1), topic);
    if (!a && !b) return 0;
    if (!a || !b || actor.settlement != topic.settlement) Fail("mixed briefing namespace: " + key);
    ValidateActor(actor.settlement, actor.local);
    return actor.settlement;
}
bool World::HasMob(std::uint32_t id, const std::string &mob) const
{
    GetDefinition(id);
    const auto value = MapName(mob);
    const auto &sources = instances.at(id).mobs;
    return std::any_of(sources.begin(), sources.end(), [&](const std::string &s) { return MapName(s) == value; });
}
bool World::UsesMob(const std::string &mob) const
{
    for (const auto &instance : instances) if (HasMob(instance.first, mob)) return true;
    return false;
}
void World::BindObjects(CWorldServer &world, CScript &script, std::uint32_t id) const
{
    const auto &def = GetDefinition(id);
    for (const auto &import : def.objectImports) {
        const auto name = Prefix(id) + import.first;
        auto &variables = script.GetGlobalVars().oVars;
        for (int i = 0; i < variables.GetSize(); ++i)
            if (Lower(variables.GetName(i)) == Lower(name))
                variables.GetVar(i)->SetObject(world.GetMapObject(ImportedObject(world, import.second)));
    }
}
void World::RestoreRenderSources(CWorldServer &world) const noexcept try
{
    for (const auto &instance : instances) {
        for (const auto &mob : instance.second.mobs) {
            std::vector<owned::RetainedAdmission> members;
            for (const auto &binding : instance.second.objects) {
                if (instance.second.sources.at(binding.first) != mob) continue;
                const auto *object = world.GetMapObject(binding.second);
                if (object) members.push_back({static_cast<std::uint32_t>(binding.first),
                    static_cast<std::uint32_t>(object->GetTypeID()), binding.second});
            }
            OwnedRender::E1RestoreSavedWorldSource(nullptr, mob.c_str(), members, true);
        }
    }
} catch (...) { GameJournal::AllocationFailureInCatch("Settlement renderer restoration"); }

bool World::Load(CWorldServer &world)
{
    if (!Empty()) return ReportError("Settlement MOBs are already loaded in this world.\n");
    struct Source
    {
        Source() = default;
        Source(Source &&) = default;
        Source(const Source &) = delete;
        std::string mob;
        CString script;
        std::vector<std::unique_ptr<CMEBasicObject>> objects;
    };
    struct Pending
    {
        Pending() = default;
        Pending(Pending &&) = default;
        Pending(const Pending &) = delete;
        std::uint32_t id;
        std::vector<Source> sources;
    };
    std::vector<Pending> pending;
    std::vector<int> admitted;
    Clear();
    try {
        std::set<int> reserved;
        // Explicit imports can name a hero/mercenary that has not deployed yet.
        // Its world ID must never be allocated to a local module object.
        for (const auto &entry : CurrentCatalog().All()) {
            if (entry.second.map != MapName((LPCSTR)world.szZoneFileName)) continue;
            for (const auto &import : entry.second.objectImports) {
                Name qualified;
                if (!ParseName(import.second, qualified)) reserved.insert(ImportedObject(world, import.second));
            }
        }
        int ordinary = 999999999, personage = 1999999999;
        for (const auto &entry : CurrentCatalog().All()) {
            const auto &def = entry.second;
            if (def.map != MapName(static_cast<const char *>(world.szZoneFileName))) continue;
            Instance instance; instance.signature = def.signature; instance.mobs = def.mobs;
            Pending module; module.id = def.id;
            for (const auto &mob : def.mobs) {
                if (MapName(mob) == MapName((LPCSTR)world.szMobFileName)) Fail("main MOB cannot be a settlement module");
                Source source; source.mob = mob;
                CGameMOBFile reader;
                struct Objects : CBasicObjectMap {
                    ~Objects() { for (const auto &entry : *this) delete entry.second; }
                } objects;
                if (!reader.Open(world.GetFullMobName(mob.c_str()),
                                 CFile::modeRead | CFile::shareDenyWrite | CFile::typeBinary))
                    Fail("cannot open " + mob);
                if (!reader.ReadModule(objects, source.script)) Fail("cannot parse " + mob);
                reader.Close();
                source.objects.reserve(objects.GetCount());
                for (auto &entry : objects) {
                    source.objects.emplace_back(entry.second); entry.second = nullptr;
                }
                for (const auto &object : source.objects) {
                    const int local = object->p_nID;
                    if (local < 0 || instance.objects.count(local)) Fail("duplicate/invalid local ID in " + Prefix(def.id));
                    int &candidate = IsIDofPersonage(local) ? personage : ordinary;
                    const int minimum = IsIDofPersonage(local) ? 1000000000 : 1;
                    while (candidate >= minimum && (reserved.count(candidate) || world.GetMapObject(candidate))) --candidate;
                    if (candidate < minimum) Fail("runtime map ID space exhausted");
                    instance.objects.emplace(local, candidate); reserved.insert(candidate--);
                    instance.sources.emplace(local, mob);
                    const auto name = Lower(static_cast<const char *>(object->p_szName));
                    if (!name.empty() && name != "none") {
                        if (LocalObjectID(name) && Number(name.substr(2)) != local)
                            Fail("object name conflicts with a reserved local ID: " + name);
                        const auto alias = instance.names.emplace(name, local);
                        if (!alias.second && alias.first->second != local) Fail("duplicate object name: " + name);
                    }
                }
                module.sources.push_back(std::move(source));
            }
            for (const auto &alias : def.aliases) {
                if (!instance.objects.count(alias.second)) Fail("alias references an absent local object");
                const auto found = instance.names.emplace(alias.first, alias.second);
                if (!found.second && found.first->second != alias.second) Fail("conflicting actor alias");
            }
            for (const auto &import : def.objectImports)
                if (instance.names.count(import.first)) Fail("object import shadows a local object: " + import.first);
            instances.emplace(def.id, std::move(instance));
            pending.push_back(std::move(module));
        }
        for (const auto &instance : instances)
            for (const auto &import : GetDefinition(instance.first).objectImports)
                ImportedObject(world, import.second); // Validate import chains before admission.
        std::size_t objectCount = 0;
        for (const auto &instance : instances) objectCount += instance.second.objects.size();
        admitted.reserve(objectCount);
        // Admit all objects before compiling any module. Forward object imports
        // can therefore bind another declared settlement on the same map.
        admitting = true;
        for (auto &module : pending) {
            auto &instance = instances.at(module.id);
            for (auto &source : module.sources) {
                OwnedRender::E1MobSource renderSource(source.mob.c_str());
                OwnedRender::E1MobSource::Parsed(source.mob.c_str());
                for (auto &object : source.objects) {
                    const int local = object->p_nID;
                    object->p_nID = instance.objects.at(local);
                    if (object->m_eType == MEOT_MAGIC_TRAP) {
                        auto *trap = static_cast<CMEMagicTrap *>(object.get());
                        if (trap->p_nPlayer < 0 || trap->p_nPlayer >= N_MAX_PLAYERS) Fail("invalid trap faction");
                        trap->p_Specific.ptPosition = trap->p_ptPosition;
                        trap->p_Specific.dwDiplomacy = 0;
                        for (int j = 0; j < N_MAX_PLAYERS; ++j)
                            trap->p_Specific.dwDiplomacy |= (world.fof.GetDiplomacy(trap->p_nPlayer, j) == War) << j;
                    }
                    const int runtime = world.AddMapObject(object.get());
                    if (runtime != object->p_nID || !world.GetMapObject(runtime)) Fail("cannot create module object");
                    admitted.push_back(runtime);
                    if (object->m_eType == MEOT_UNIT && object->p_lpMapObject) {
                        auto *unit = static_cast<CMEUnit *>(object.get());
                        static_cast<CUnitServer *>(static_cast<CUnit *>(unit->p_lpMapObject)->GetUnitSpecific())
                            ->logic.SetLogics(unit->p_sLogic, N_NUMBER_OF_ALARM);
                    }
                    const auto *mapObject = dynamic_cast<const CMEObject *>(object.get());
                    OwnedRender::E1MobSource::Admitted(static_cast<std::uint32_t>(local),
                        mapObject ? static_cast<std::uint32_t>(mapObject->p_tidTypeID) : 0u, runtime);
                }
                renderSource.Succeeded(true);
            }
        }
        admitting = false;
        for (auto &module : pending) {
            CScript::SettlementScope scope(world.script, module.id);
            for (auto &source : module.sources) {
                if (!world.script.LoadText(source.script)) {
                    const auto error = world.script.GetLastError();
                    Fail(source.mob + ": " + std::string(static_cast<const char *>(error.text)));
                }
            }
            BindObjects(world, world.script, module.id);
            GameJournal::Event("ZONE", "settlement_mob", "namespace=%s sources=%zu objects=%zu",
                Prefix(module.id).c_str(), module.sources.size(), instances.at(module.id).objects.size());
        }
        InitializePeace(world);
        return true;
    } catch (CException *error) {
        error->Delete();
        world.script.Clear(); world.scriptTemp.Clear(); world.bMapLoaded = FALSE;
        for (auto it = admitted.rbegin(); it != admitted.rend(); ++it) world.RemoveMapObject(*it);
        Clear();
        return ReportError("Cannot load settlement MOB: file/resource error.\n");
    } catch (const std::exception &error) {
        world.script.Clear(); world.scriptTemp.Clear(); world.bMapLoaded = FALSE;
        for (auto it = admitted.rbegin(); it != admitted.rend(); ++it) world.RemoveMapObject(*it);
        Clear();
        return ReportError("Cannot load settlement MOB: %s\n", error.what());
    }
}

void World::Store(SaveSchema::Writer &writer, CWorldServer &world) const
{
    writer.Count(static_cast<std::uint32_t>(instances.size()));
    for (const auto &entry : instances) {
        writer.UInt32(entry.first); writer.String(entry.second.signature);
        writer.Bool(entry.second.alarm); writer.UInt32(entry.second.reasons); writer.Int32(entry.second.lastIncident);
        writer.Count(static_cast<std::uint32_t>(entry.second.objects.size()));
        for (const auto &object : entry.second.objects) {
            writer.Int32(object.first); writer.Int32(object.second);
            const auto *live = world.GetMapObject(object.second);
            writer.UInt64(live ? live->GetRTID() : 0);
            writer.String(entry.second.sources.at(object.first));
        }
        writer.Count(static_cast<std::uint32_t>(entry.second.names.size()));
        for (const auto &name : entry.second.names) { writer.String(name.first); writer.Int32(name.second); }
    }
    const auto liveVisit = [&](const auto &entry) {
        const auto *unit = world.GetUnit(entry.first);
        return unit && !unit->IsDead() && unit->GetRTID() == entry.second.identity;
    };
    writer.Count(static_cast<std::uint32_t>(std::count_if(visitors.begin(), visitors.end(), liveVisit)));
    for (const auto &entry : visitors) {
        if (!liveVisit(entry)) continue;
        writer.Int32(entry.first); writer.UInt64(entry.second.identity); writer.UInt32(entry.second.settlement);
        for (float coordinate : entry.second.previous) writer.Float32(coordinate);
    }
}
void World::Restore(SaveSchema::Reader &reader, CWorldServer &world)
{
    Clear();
    const auto count = reader.Count();
    if (count > 4096) Fail("invalid saved settlement count");
    std::set<int> runtimeIds;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto id = reader.UInt32();
        const auto *def = CurrentCatalog().Find(id);
        if (!def || def->map != MapName(static_cast<const char *>(world.szZoneFileName))) Fail("saved settlement does not belong to this map");
        Instance instance; instance.signature = reader.String(); instance.mobs = def->mobs;
        if (instance.signature != def->signature) Fail("settlement definition changed since save");
        instance.alarm = reader.Bool(); instance.reasons = reader.UInt32(); instance.lastIncident = reader.Int32();
        if ((instance.reasons & ~63u) || instance.lastIncident < -1 || instance.lastIncident > world.GetSegmentTimeCounter() ||
            (instance.alarm ? (!def->peace.Enabled() || !instance.reasons || instance.lastIncident < 0) :
                              (instance.reasons != 0 || instance.lastIncident != -1)))
            Fail("invalid saved settlement alarm");
        const auto objects = reader.Count();
        if (objects > 1000000) Fail("invalid saved settlement objects");
        for (std::uint32_t j = 0; j < objects; ++j) {
            const int local = reader.Int32(), runtime = reader.Int32();
            const auto identity = reader.UInt64();
            const auto source = reader.String();
            const auto *live = world.GetMapObject(runtime);
            if (local < 0 || runtime <= 0 || IsIDofPersonage(local) != IsIDofPersonage(runtime) ||
                (live ? live->GetRTID() : 0) != identity ||
                std::find(def->mobs.begin(), def->mobs.end(), source) == def->mobs.end() ||
                !instance.objects.emplace(local, runtime).second || !runtimeIds.insert(runtime).second)
                Fail("invalid saved settlement object identity");
            instance.sources.emplace(local, source);
        }
        const auto names = reader.Count();
        if (names > 1000000) Fail("invalid saved settlement aliases");
        for (std::uint32_t j = 0; j < names; ++j) {
            const auto name = reader.String(); const int local = reader.Int32();
            if (name.empty() || Lower(name) != name || !instance.objects.count(local) || !instance.names.emplace(name, local).second)
                Fail("invalid saved settlement alias");
        }
        if (!instances.emplace(id, std::move(instance)).second) Fail("duplicate saved settlement");
    }
    const auto members = reader.Count();
    if (members > 1000000) Fail("invalid saved settlement visitors");
    for (std::uint32_t i = 0; i < members; ++i) {
        const int id = reader.Int32(); Presence presence;
        presence.identity = reader.UInt64(); presence.settlement = reader.UInt32();
        for (auto &coordinate : presence.previous) {
            coordinate = reader.Float32();
            if (!std::isfinite(coordinate)) Fail("invalid saved settlement position");
        }
        const auto *unit = world.GetUnit(id);
        if (!unit || unit->GetRTID() != presence.identity ||
            (presence.settlement && (!Contains(presence.settlement) || !GetDefinition(presence.settlement).peace.Enabled())) ||
            !visitors.emplace(id, presence).second) Fail("invalid saved settlement membership");
    }
    for (const auto &entry : CurrentCatalog().All())
        if (entry.second.map == MapName(static_cast<const char *>(world.szZoneFileName)) && !Contains(entry.first))
            Fail("settlement added since save; a new map state is required");
    InitializePeace(world);
}

}

CScript::SettlementScope::SettlementScope(CScript &owner, std::uint32_t id)
    : script(owner), previous(owner.settlementContext)
{
    if (id && !script.NamespaceWorld().Settlements().Contains(id))
        throw std::runtime_error("Script references an unloaded settlement");
    // Hero and other explicit imports may appear only after party deployment.
    if (id) script.NamespaceWorld().Settlements().BindObjects(script.NamespaceWorld(), script, id);
    script.settlementContext = id;
}
CScript::SettlementScope::~SettlementScope() { script.settlementContext = previous; }
CWorldServer &CScript::NamespaceWorld() const
{
    return settlementWorld ? *settlementWorld : theWorldServer;
}
int CScript::SettlementObject(const char *reference, bool actorName) const
{
    Settlement::Name qualified;
    if (CurrentSettlement() || Settlement::ParseName(reference, qualified))
        return NamespaceWorld().Settlements().Object(NamespaceWorld(), CurrentSettlement(), reference, actorName);
    return actorName ? GetPersonageIDByName(reference) : atoi(reference);
}
std::string CScript::SettlementKey(const char *key) const
{
    return NamespaceWorld().Settlements().StateKey(CurrentSettlement(), key);
}
bool CScript::IsSettlementGroupImport(const char *local) const
{
    return NamespaceWorld().Settlements().IsGroupImport(CurrentSettlement(), local);
}
void CScript::ValidateSettlementContexts() const
{
    auto check = [&](const auto &list) {
        for (POSITION pos = list.GetHeadPosition(); pos;) {
            const auto &thread = list.GetNext(pos);
            if (thread.settlement && !NamespaceWorld().Settlements().Contains(thread.settlement))
                throw SaveSchema::Error(SaveSchema::ErrorCode::Identity, "Unknown saved script settlement");
            for (int i = 0; i < thread.commands.GetSize(); ++i) {
                const auto *loop = thread.commands[i].pLoopBody;
                if (loop && loop->settlement != thread.settlement)
                    throw SaveSchema::Error(SaveSchema::ErrorCode::Identity, "Saved loop crosses settlement context");
            }
        }
    };
    check(threads); check(worldActions);
    auto definition = [&](const SOneScript &script) {
        Settlement::Name name;
        Settlement::ParseName((LPCSTR)script.szScriptName, name);
        if (name.settlement && !NamespaceWorld().Settlements().Contains(name.settlement))
            throw SaveSchema::Error(SaveSchema::ErrorCode::Identity, "Unknown saved script namespace");
        for (int i = 0; i < script.conditions.GetSize(); ++i) {
            const auto &condition = script.conditions[i];
            if (!condition.pActions || !condition.pConditions ||
                condition.pActions->settlement != name.settlement || condition.pConditions->settlement != name.settlement)
                throw SaveSchema::Error(SaveSchema::ErrorCode::Identity, "Saved script crosses settlement context");
        }
    };
    for (int i = 0; i < scripts.GetSize(); ++i) definition(scripts[i]);
    for (POSITION pos = runningScripts.GetHeadPosition(); pos;) definition(runningScripts.GetNext(pos));
}
