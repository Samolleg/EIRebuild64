#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class CWorldServer;
class CScript;
namespace SaveSchema { class Writer; class Reader; }

namespace Settlement {

// Public identities never depend on the order in which MOBs were loaded.
struct Name
{
    std::uint32_t settlement = 0;
    std::string local;
};
bool ParseName(const std::string &text, Name &name);
std::string Prefix(std::uint32_t id);
std::string MapName(const std::string &text);

struct Briefing
{
    std::string text;
    std::string reward;
    int shop = 0;
};

struct Definition
{
    std::uint32_t id = 0;
    std::string map;
    std::vector<std::string> mobs;
    // Object imports are either decimal world IDs or explicit actor names.
    std::map<std::string, std::string> objectImports;
    std::map<std::string, std::string> groupImports;
    // Exact keys, or prefix mappings ending in '.' on both sides.
    std::map<std::string, std::string> stateImports;
    std::map<std::string, int> aliases;
    std::map<std::string, Briefing> briefings;
    std::string signature;
};

class Catalog
{
    std::map<std::uint32_t, Definition> definitions;
public:
    void Load(const std::string &modDirectory);
    const Definition *Find(std::uint32_t id) const;
    const std::map<std::uint32_t, Definition> &All() const { return definitions; }
};

class World
{
    struct Instance
    {
        std::string signature;
        std::vector<std::string> mobs;
        std::map<int, int> objects;
        std::map<int, std::string> sources;
        std::map<std::string, int> names;
    };
    std::map<std::uint32_t, Instance> instances;
    bool admitting = false;
    const Definition &GetDefinition(std::uint32_t id) const;
    void ValidateActor(std::uint32_t id, const std::string &local) const;
    int ImportedObject(CWorldServer &world, const std::string &target) const;
public:
    void Clear();
    bool Empty() const { return instances.empty(); }
    bool Contains(std::uint32_t id) const { return instances.count(id) != 0; }
    bool Reserved(int runtimeId) const;
    bool Admitting() const { return admitting; }
    bool Load(CWorldServer &world);
    bool HasMob(std::uint32_t id, const std::string &mob) const;
    bool UsesMob(const std::string &mob) const;
    std::string ObjectName(CWorldServer &world, std::uint32_t context, int runtimeId) const;
    int Object(CWorldServer &world, std::uint32_t context, const std::string &reference,
               bool actorName = false) const;
    std::string Symbol(std::uint32_t context, const std::string &local, bool group = false) const;
    bool IsGroupImport(std::uint32_t context, const std::string &local) const;
    std::string StateKey(std::uint32_t context, const std::string &key) const;
    const Briefing &ResolveBriefing(const std::string &qualified) const;
    std::uint32_t BriefingOwner(const std::string &key) const;
    void BindObjects(CWorldServer &world, CScript &script, std::uint32_t id) const;
    void RestoreRenderSources(CWorldServer &world) const noexcept;
    void Store(SaveSchema::Writer &writer, CWorldServer &world) const;
    void Restore(SaveSchema::Reader &reader, CWorldServer &world);
};

}
