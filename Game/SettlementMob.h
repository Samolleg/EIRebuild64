#pragma once

#include <cstdint>
#include <array>
#include <map>
#include <string>
#include <vector>
#include <memory>

class CWorldServer;
class CScript;
class CUnit;
class CMapObject;
class CSpell;
class CPlot3D;
class CParty;
class CShop;
class CItem;
class CI3DScreenCamp;
struct SUnitBasicCmd;
namespace SaveSchema { class Writer; class Reader; }

namespace Settlement {
struct TradeState;
struct TradeAccess;
enum class TradeAction { Deal, Equip, Unequip };

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
struct DialogueTopic
{
    std::string key, actor, name;
    Briefing binding;
};
struct DialogueSession
{
    std::uint64_t token = 0, targetIdentity = 0, heroIdentity = 0;
    std::uint32_t settlement = 0;
    int player = -1, target = -1, hero = -1;
    std::string topic;
    bool completing = false, consumed = false;
};

struct Point { float x = 0, y = 0; };
struct PeaceArea
{
    std::vector<Point> polygon;
    float minZ = 0, maxZ = 0, hysteresis = .4f, quietSeconds = 5;
    int faction = 0;
    std::string alarmKey;
    int gateObject = -1, exit = -1;
    Point gateA, gateB, inside;
    std::string legacyZone;
    std::vector<int> legacyEntrances;
    Point center;
    float radius = 0;
    bool Enabled() const { return !polygon.empty(); }
};
void ValidatePeaceArea(PeaceArea &area);
enum class Mode : std::uint32_t { Outside, Peaceful, Alarm, Hostile };
enum Reason : std::uint32_t { Attack = 1, Spell = 2, Damage = 4, NativeAlarm = 8, Story = 16, Quiet = 32 };

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
    PeaceArea peace;
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
    friend struct TradeAccess;
    std::shared_ptr<TradeState> trade;
    struct Instance
    {
        std::string signature;
        std::vector<std::string> mobs;
        std::map<int, int> objects;
        std::map<int, std::string> sources;
        std::map<std::string, int> names;
        bool alarm = false;
        std::uint32_t reasons = 0;
        int lastIncident = -1;
    };
    struct Presence
    {
        std::uint64_t identity = 0;
        std::uint32_t settlement = 0;
        std::array<float, 3> previous{};
    };
    std::map<std::uint32_t, Instance> instances;
    std::map<int, Presence> visitors;
    bool admitting = false;
    DialogueSession dialogue;
    std::uint64_t nextDialogue = 0;
    const Definition &GetDefinition(std::uint32_t id) const;
    void ValidateActor(std::uint32_t id, const std::string &local) const;
    int ImportedObject(CWorldServer &world, const std::string &target) const;
    std::uint32_t AreaAt(const CPlot3D &point, std::uint32_t previous = 0) const;
    void Incident(CWorldServer &world, std::uint32_t id, std::uint32_t reason);
    void Publish(CWorldServer &world, std::uint32_t id);
    void Event(CWorldServer &world, std::uint32_t id, const char *name, int a, int b, int c);
    bool Unsafe(CWorldServer &world, CUnit *unit) const;
    bool Harmful(CUnit *unit, const SUnitBasicCmd &command) const;
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
    void InitializePeace(CWorldServer &world);
    void Segment(CWorldServer &world);
    bool LocalExit(int exit) const;
    bool LocalTravel(const std::string &zone, int entrance) const;
    Mode ModeFor(CWorldServer &world, CUnit *unit) const;
    bool CanInteract(CWorldServer &world, int player, std::uint32_t id, CUnit *npc = nullptr) const;
    bool AllowAction(CWorldServer &world, CUnit *actor, CMapObject *target, const CPlot3D &point,
                     CSpell *spell, bool trap, bool executing);
    void DamageAt(CWorldServer &world, CUnit *victim);
    void AlarmAt(CWorldServer &world, int alarm, const CPlot3D &point);
    std::vector<DialogueTopic> DialogueTopics(CWorldServer &world, int player, int target) const;
    bool DialogueAvailable(CWorldServer &world, int player, int target) const;
    bool DialogueReady(CWorldServer &world, int player, int target) const;
    std::uint64_t BeginDialogue(CWorldServer &world, int player, int target);
    bool DialogueValid(CWorldServer &world, std::uint64_t token) const;
    bool SelectDialogueTopic(CWorldServer &world, std::uint64_t token, const std::string &key);
    bool FinishDialogue(CWorldServer &world, std::uint64_t token);
    bool AcceptDialogueCompletion(int player, const char *key);
    void EndDialogue(std::uint64_t token);
    bool BeginTrade(CWorldServer &world, std::uint64_t token, const std::string &topic);
    bool TradeValid(CWorldServer &world, std::uint64_t token) const;
    CParty *TradeParty(std::uint64_t token) const;
    CShop *TradeShop(std::uint64_t token) const;
    std::shared_ptr<void> TradeLifetime(std::uint64_t token) const;
    bool CommitTrade(CWorldServer &world, std::uint64_t token, CI3DScreenCamp &screen,
                     TradeAction action = TradeAction::Deal, CItem *item = nullptr, CItem **result = nullptr);
    void EndTrade(std::uint64_t token);
    bool DialogueActive() const { return dialogue.token != 0; }
    const DialogueSession &Dialogue() const { return dialogue; }
};

}
