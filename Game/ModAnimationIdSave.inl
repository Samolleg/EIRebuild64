// Included once by WorldServer.cpp. The keys and field order are the installed
// addon's __jmv_aidx1_ v2 format; v1 remains readable.
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ModAnimationIdSave {
namespace {
constexpr uint32_t kNoAnimation = 0xffffffffu;
constexpr unsigned kFields = 15;
const char kPrefix[] = "__jmv_aidx1_";
using SavedValues = std::map<std::string, float>;

struct Record {
    uint32_t unitId, kind, slot, start, cycle;
    BYTE rawStart, rawCycle;
    uint32_t startName, cycleName;
};

// Full IDs already use kNoAnimation; 255 is a real ADB entry.
bool HasExtended(uint32_t start, uint32_t cycle)
{
    return (start > 253 && start != kNoAnimation) ||
           (cycle > 253 && cycle != kNoAnimation);
}
uint32_t NameHash(const char *name, size_t maximum)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; name && i < maximum && name[i]; ++i) {
        hash ^= static_cast<BYTE>(name[i]);
        hash *= 16777619u;
    }
    return hash;
}
uint32_t EntryHash(CUnit *unit, uint32_t id)
{
    if (id == kNoAnimation) return 0;
    const SADEntry *entry = unit->GetAnimationByIndex(id);
    return entry ? NameHash(entry->pszAnimationName, sizeof(entry->pszAnimationName)) : 0;
}
SavedValues Snapshot(const CGSVariableSet &vars)
{
    SavedValues result;
    for (const auto &value : vars)
        if (value.first.compare(0, sizeof(kPrefix) - 1, kPrefix) == 0)
            result.emplace(value.first, value.second);
    return result;
}
bool ReadValue(const SavedValues &values, const char *key, uint32_t &result)
{
    const auto found = values.find(key);
    if (found == values.end()) return false;
    const float stored = found->second;
    if (!std::isfinite(stored) || stored < 0 || stored > 65535 ||
        stored != std::floor(stored)) return false;
    result = static_cast<uint32_t>(stored);
    return true;
}
void Key(char (&buffer)[48], unsigned record, unsigned field)
{
    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE,
                "__jmv_aidx1_%u_%u", record, field);
}
bool ReadWord(const SavedValues &values, unsigned record, unsigned field,
              uint32_t &result)
{
    char key[48];
    Key(key, record, field);
    return ReadValue(values, key, result);
}
// Only this private snapshot writer retains zero-valued words.
void WriteValue(CGSVariableSet &vars, const char *key, float value)
{
    vars.SetVar(key, value);
    if (value == 0) vars[key] = value;
}
void WriteWords(CGSVariableSet &vars, unsigned record, const Record &item)
{
    const uint32_t values[kFields] = {
        item.unitId & 0xffffu, item.unitId >> 16, item.kind,
        item.slot & 0xffffu, item.start & 0xffffu, item.start >> 16,
        item.cycle & 0xffffu, item.cycle >> 16, item.rawStart, item.rawCycle,
        item.startName & 0xffffu, item.startName >> 16,
        item.cycleName & 0xffffu, item.cycleName >> 16, item.slot >> 16
    };
    for (unsigned field = 0; field < kFields; ++field) {
        char key[48];
        Key(key, record, field);
        WriteValue(vars, key, static_cast<float>(values[field]));
    }
}
bool ReadRecord(const SavedValues &saved, unsigned version, unsigned index,
                Record &item)
{
    uint32_t values[kFields] = {};
    const unsigned fields = version == 1 ? 14 : kFields;
    for (unsigned field = 0; field < fields; ++field)
        if (!ReadWord(saved, index, field, values[field])) return false;
    if (values[8] > 255 || values[9] > 255) return false;
    item.unitId = values[0] | (values[1] << 16);
    item.kind = values[2];
    item.slot = values[3] | (values[14] << 16);
    item.start = values[4] | (values[5] << 16);
    item.cycle = values[6] | (values[7] << 16);
    item.rawStart = static_cast<BYTE>(values[8]);
    item.rawCycle = static_cast<BYTE>(values[9]);
    item.startName = values[10] | (values[11] << 16);
    item.cycleName = values[12] | (values[13] << 16);
    return true;
}
bool ReadCount(const SavedValues &values, unsigned &version, uint32_t &count)
{
    if (!ReadValue(values, "__jmv_aidx1_v", version)) return false;
    if (version == 1) {
        const auto found = values.find("__jmv_aidx1_c");
        if (found == values.end()) return false;
        const float stored = found->second;
        if (!std::isfinite(stored) || stored < 0 || stored != std::floor(stored) ||
            static_cast<double>(stored) > values.size() / 14) return false;
        count = static_cast<uint32_t>(stored);
        return true;
    }
    if (version != 2) return false;
    uint32_t low, high;
    if (!ReadValue(values, "__jmv_aidx1_c0", low) ||
        !ReadValue(values, "__jmv_aidx1_c1", high)) return false;
    count = low | (high << 16);
    return count <= values.size() / kFields;
}
uint32_t ZoneHash(const CWorldServer &world)
{
    return NameHash(world.szMobFileName.GetString(), 256);
}
void RecordSequence(std::vector<Record> &records, CUnit *unit,
                    SL0AnimationSeq &sequence, unsigned kind)
{
    for (int slot = 0; slot < sequence.animations.GetSize(); ++slot) {
        const uint32_t start = sequence.GetStartId(slot);
        const uint32_t cycle = sequence.GetCycleId(slot);
        if (!HasExtended(start, cycle)) continue;
        const SL0Animation &animation = sequence.animations[slot];
        records.push_back({ static_cast<uint32_t>(unit->GetID()), kind,
            static_cast<uint32_t>(slot), start, cycle,
            animation.bStart, animation.bCyclic,
            EntryHash(unit, start), EntryHash(unit, cycle) });
    }
}
void RecordUnit(std::vector<Record> &records, CUnit *unit)
{
    if (!unit->HasExtendedAnimationId()) return;
    const uint32_t start = unit->GetStartAnimationId();
    const uint32_t cycle = unit->GetCycleAnimationId();
    records.push_back({ static_cast<uint32_t>(unit->GetID()), 0, 0,
        start, cycle, unit->GetAnimationStartByte(), unit->GetAnimationCycleByte(),
        EntryHash(unit, start), EntryHash(unit, cycle) });
}
void WriteSnapshot(CGSVariableSet &vars, const std::vector<Record> &records,
                   uint32_t zone)
{
    const SavedValues previous = Snapshot(vars);
    for (const auto &value : previous) vars.DelVar(value.first.c_str());
    if (records.empty()) return;
    WriteValue(vars, "__jmv_aidx1_v", 0.0f);
    for (unsigned i = 0; i < records.size(); ++i) WriteWords(vars, i, records[i]);
    WriteValue(vars, "__jmv_aidx1_z0", static_cast<float>(zone & 0xffffu));
    WriteValue(vars, "__jmv_aidx1_z1", static_cast<float>(zone >> 16));
    const uint32_t count = static_cast<uint32_t>(records.size());
    WriteValue(vars, "__jmv_aidx1_c0", static_cast<float>(count & 0xffffu));
    WriteValue(vars, "__jmv_aidx1_c1", static_cast<float>(count >> 16));
    WriteValue(vars, "__jmv_aidx1_v", 2.0f);
}
bool RestoreRecord(CWorldServer &world, const Record &item)
{
    CUnit *unit = world.GetUnit(item.unitId);
    if (!unit || !unit->GetUnitSpecific() ||
        unit->GetUnitSpecific()->WhoAmI() != HPC_UNITSERVERSPECIFIC ||
        !HasExtended(item.start, item.cycle) ||
        (item.start != kNoAnimation && !unit->GetAnimationByIndex(item.start)) ||
        (item.cycle != kNoAnimation && !unit->GetAnimationByIndex(item.cycle)) ||
        EntryHash(unit, item.start) != item.startName ||
        EntryHash(unit, item.cycle) != item.cycleName) return false;
    if (item.kind == 0) {
        if (unit->GetAnimationStartByte() != item.rawStart ||
            unit->GetAnimationCycleByte() != item.rawCycle) return false;
        unit->RestoreExtendedAnimationIds(item.start, item.cycle);
        return true;
    }
    CUnitServer *server = static_cast<CUnitServer *>(unit->GetUnitSpecific());
    SL0AnimationSeq *sequence = server->GetExtendedAnimationSequence(item.kind);
    if (!sequence || item.slot >= static_cast<uint32_t>(sequence->animations.GetSize()))
        return false;
    const SL0Animation &animation = sequence->animations[item.slot];
    if (animation.bStart != item.rawStart ||
        animation.bCyclic != item.rawCycle) return false;
    sequence->EnsureIds();
    sequence->startIds[item.slot] = item.start;
    sequence->cycleIds[item.slot] = item.cycle;
    return true;
}
} // namespace

bool PrepareSave(CWorldServer &world, CGSVariableSet &partyVars)
{
    std::vector<Record> records;
    CMapObjPtrHashIterator it;
    for (CMapObject *object = it.GetFirst(&world.dynamicObjs); object;
         object = it.GetNext()) {
        if (object->IsInvalid() || object->IsUnreferenced() ||
            object->WhoAmI() != HPC_UNIT) continue;
        CUnit *unit = static_cast<CUnit *>(object);
        if (!unit->GetUnitSpecific() ||
            unit->GetUnitSpecific()->WhoAmI() != HPC_UNITSERVERSPECIFIC) continue;
        CUnitServer *server = static_cast<CUnitServer *>(unit->GetUnitSpecific());
        RecordUnit(records, unit);
        for (unsigned kind = 1; kind <= 5; ++kind)
            RecordSequence(records, unit, *server->GetExtendedAnimationSequence(kind), kind);
    }
    if (records.size() > 0xffffffffu) return false;
    const uint32_t zone = ZoneHash(world);
    WriteSnapshot(partyVars, records, zone);
    CPlayer *player = world.GetPlayer(0);
    if (player && &player->party.vars != &partyVars)
        WriteSnapshot(player->party.vars, records, zone);
    return true;
}

void AfterSuccessfulLoad(CWorldServer &world)
{
    CPlayer *player = world.GetPlayer(0);
    if (!player) return;
    const SavedValues values = Snapshot(player->party.vars);
    unsigned version = 0;
    uint32_t count = 0, low = 0, high = 0;
    if (!ReadCount(values, version, count) ||
        !ReadValue(values, "__jmv_aidx1_z0", low) ||
        !ReadValue(values, "__jmv_aidx1_z1", high) ||
        ZoneHash(world) != (low | (high << 16))) return;
    for (unsigned i = 0; i < count; ++i) {
        Record item = {};
        if (ReadRecord(values, version, i, item)) RestoreRecord(world, item);
    }
}
} // namespace ModAnimationIdSave
