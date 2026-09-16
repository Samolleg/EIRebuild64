#include "StdAfx.h"
#include "ModRegisteredLoot.h"
#include "ModPackage.h"
#include "ModRuntime.h"
#include "WorldServer.h"
#include "Player.h"
#include "StuffMapObjects.h"
#include "Item.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef JMV_REGISTERED_LOOT_DIAGNOSTICS
#define JMV_REGISTERED_LOOT_DIAGNOSTICS 0
#endif

#define WorldServer (&theWorldServer)

namespace
{
const char *LogSec = "RegisteredLoot";
const char *REGISTERED_LOOT_PREFIX = "jmv.registerloot.v1.";
const int REGISTERED_LOOT_DELAY_SEGMENTS = 5;
const int REGISTERED_LOOT_RETRY_SEGMENTS = 10;
const size_t REGISTERED_LOOT_MAX_ITEM_SPEC = 512;

struct SRegisteredItem
{
    std::string spec;
    std::string marker;
    int count;

    SRegisteredItem()
        : count(0)
    {
    }
};

struct SRegisteredObject
{
    int objectId;
    std::unordered_map<std::string, SRegisteredItem> items;
    bool pending;
    bool consumed;
    bool cleanupConsumed;
    int dueSegment;
    int lastRemaining;
    int playerNumber;

    SRegisteredObject()
        : objectId(-1)
        , pending(false)
        , consumed(false)
        , cleanupConsumed(false)
        , dueSegment(0)
        , lastRemaining(-1)
        , playerNumber(0)
    {
    }
};

struct SRegisteredLootState
{
    CWorldServer *world;
    std::string zone;
    std::string markerPrefix;
    std::unordered_map<int, SRegisteredObject> objects;
    std::unordered_map<std::string, int> registrationOccurrences;
    std::unordered_map<std::string, unsigned int> rejectLogCounts;
    std::unordered_map<std::string, bool> validatedItemSpecs;
    std::vector<int> pendingObjects;
    std::vector<int> cleanupObjects;
    unsigned int registrations;
    unsigned int activations;
    unsigned int grants;
    unsigned int removals;
    unsigned int rejects;

    SRegisteredLootState()
        : world(nullptr)
        , registrations(0)
        , activations(0)
        , grants(0)
        , removals(0)
        , rejects(0)
    {
    }
};

SRegisteredLootState G_registeredLoot;

std::string NormalizeZoneName(const char *rawName)
{
    std::string raw = rawName ? rawName : "";
    const size_t slash = raw.find_last_of("\\/");
    if (slash != std::string::npos)
        raw.erase(0, slash + 1);

    std::string result;
    result.reserve(raw.size());
    for (unsigned char ch : raw)
    {
        if (std::isalnum(ch) || ch == '_' || ch == '-')
            result.push_back(static_cast<char>(std::tolower(ch)));
        else
            result.push_back('_');
    }
    return result.empty() ? "unknown" : result;
}

std::string GetCurrentZoneName()
{
    if (!WorldServer)
        return std::string();
    CString *mobName = &theWorldServer.szMobFileName;
    return NormalizeZoneName(mobName ? (LPCSTR)*mobName : nullptr);
}

CGSVariableSet *GetPersistentVars()
{
    if (!WorldServer)
        return nullptr;
    CPlayer *player = WorldServer->GetPlayer(0);
    return player ? &player->party.vars : nullptr;
}

std::string HexEncode(const std::string &value)
{
    static const char HEX[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size() * 2);
    for (unsigned char ch : value)
    {
        result.push_back(HEX[ch >> 4]);
        result.push_back(HEX[ch & 0x0F]);
    }
    return result;
}

int HexValue(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    return -1;
}

bool HexDecode(const std::string &value, std::string &result)
{
    if (value.empty() || (value.size() & 1) != 0)
        return false;

    result.clear();
    result.reserve(value.size() / 2);
    for (size_t i = 0; i < value.size(); i += 2)
    {
        const int high = HexValue(value[i]);
        const int low = HexValue(value[i + 1]);
        if (high < 0 || low < 0)
            return false;
        result.push_back(static_cast<char>((high << 4) | low));
    }
    return true;
}

std::string BuildObjectMarkerPrefix(int objectId)
{
    char suffix[48] = {};
    _snprintf_s(suffix, sizeof(suffix), _TRUNCATE, "%d.", objectId);
    return G_registeredLoot.markerPrefix + suffix;
}

std::string BuildItemMarker(int objectId, const std::string &itemSpec)
{
    return BuildObjectMarkerPrefix(objectId) + "item." + HexEncode(itemSpec);
}

std::string BuildPendingMarker(int objectId)
{
    return BuildObjectMarkerPrefix(objectId) + "pending";
}

std::string BuildPlayerMarker(int objectId)
{
    return BuildObjectMarkerPrefix(objectId) + "player";
}

int MarkerCount(float value)
{
    const int rounded = static_cast<int>(std::floor(std::fabs(value) + 0.5f));
    return (std::max)(0, rounded);
}

void ClearTransientState(const char *reason)
{
#if JMV_REGISTERED_LOOT_DIAGNOSTICS
    if (G_registeredLoot.world || !G_registeredLoot.objects.empty())
    {
        LOGINFO("event=RESET reason=%s zone=%s objects=%u registrations=%u activations=%u grants=%u removals=%u rejects=%u",
                reason ? reason : "unknown",
                G_registeredLoot.zone.c_str(),
                static_cast<unsigned int>(G_registeredLoot.objects.size()),
                G_registeredLoot.registrations,
                G_registeredLoot.activations,
                G_registeredLoot.grants,
                G_registeredLoot.removals,
                G_registeredLoot.rejects);
    }
#else
    (void)reason;
#endif
    G_registeredLoot = SRegisteredLootState();
}

bool EnsureCurrentWorld()
{
    if (!(ModRuntime::Current() && ModRuntime::Current()->RegisteredLootEnabled()) || !WorldServer)
        return false;

    const std::string zone = GetCurrentZoneName();
    if (G_registeredLoot.world != WorldServer || G_registeredLoot.zone != zone)
    {
        ClearTransientState("world-change");
        G_registeredLoot.world = WorldServer;
        G_registeredLoot.zone = zone;
        G_registeredLoot.markerPrefix = REGISTERED_LOOT_PREFIX + zone + ".";
#if JMV_REGISTERED_LOOT_DIAGNOSTICS
        LOGINFO("event=WORLD zone=%s delay_segments=%d",
                zone.c_str(),
                REGISTERED_LOOT_DELAY_SEGMENTS);
#endif
    }
    return true;
}

void LogReject(const char *reason, CMapObject *object, const char *itemSpec)
{
    ++G_registeredLoot.rejects;
#if JMV_REGISTERED_LOOT_DIAGNOSTICS
    char objectId[32] = {};
    _snprintf_s(objectId,
                sizeof(objectId),
                _TRUNCATE,
                "%d",
                object ? object->GetID() : -1);
    const std::string key = std::string(reason ? reason : "unknown") + "|" +
                            objectId + "|" +
                            (itemSpec ? itemSpec : "<null>");
    const unsigned int occurrence = ++G_registeredLoot.rejectLogCounts[key];
    if (occurrence != 1 && (occurrence & (occurrence - 1)) != 0)
        return;

    LOGWARN("event=REGISTER_REJECT reason=%s object=%d class=%d item=%s",
            reason ? reason : "unknown",
            object ? object->GetID() : -1,
            object ? static_cast<int>(object->WhoAmI()) : -1,
            itemSpec ? itemSpec : "<null>");
#else
    (void)reason;
    (void)object;
    (void)itemSpec;
#endif
}

bool ValidateItemSpec(const std::string &itemSpec)
{
    const auto cached = G_registeredLoot.validatedItemSpecs.find(itemSpec);
    if (cached != G_registeredLoot.validatedItemSpecs.end())
        return cached->second;

    CItem *probe = WorldServer->CreateItem(itemSpec.c_str());
    const bool valid = probe != nullptr;
    if (probe)
        probe->Release();
    G_registeredLoot.validatedItemSpecs[itemSpec] = valid;
    return valid;
}

bool ParseObjectMarker(const std::string &name,
                       int &objectId,
                       std::string &tail)
{
    if (name.compare(0,
                     G_registeredLoot.markerPrefix.size(),
                     G_registeredLoot.markerPrefix) != 0)
    {
        return false;
    }

    const char *start = name.c_str() + G_registeredLoot.markerPrefix.size();
    char *end = nullptr;
    const long parsed = std::strtol(start, &end, 10);
    if (end == start || !end || *end != '.' || parsed < 0 || parsed > INT_MAX)
        return false;

    objectId = static_cast<int>(parsed);
    tail.assign(end + 1);
    return !tail.empty();
}

void SetPendingMarkers(SRegisteredObject &entry, int remaining)
{
    CGSVariableSet *vars = GetPersistentVars();
    if (!vars)
        return;
    vars->SetVar(BuildPendingMarker(entry.objectId).c_str(),
                 static_cast<float>(remaining));
    vars->SetVar(BuildPlayerMarker(entry.objectId).c_str(),
                 static_cast<float>(entry.playerNumber + 1));
    entry.lastRemaining = remaining;
}

void ClearPendingMarkers(const SRegisteredObject &entry)
{
    CGSVariableSet *vars = GetPersistentVars();
    if (!vars)
        return;
    vars->DelVar(BuildPendingMarker(entry.objectId).c_str());
    vars->DelVar(BuildPlayerMarker(entry.objectId).c_str());
}

void MarkConsumed(SRegisteredObject &entry)
{
    CGSVariableSet *vars = GetPersistentVars();
    if (!vars)
        return;

    for (auto &pair : entry.items)
    {
        SRegisteredItem &item = pair.second;
        vars->SetVar(item.marker.c_str(),
                     static_cast<float>(-(std::max)(1, item.count)));
    }
    ClearPendingMarkers(entry);
    entry.pending = false;
    entry.consumed = true;
    entry.cleanupConsumed = false;
}

bool IsRegisteredLever(CMapObject *object)
{
    return object && object->WhoAmI() == HPC_LEVER &&
        !object->IsInvalid();
}

bool GrantRegisteredItems(SRegisteredObject &entry)
{
    CPlayer *player = WorldServer->GetPlayer(entry.playerNumber);
    if (!player)
        return false;

    CList<CItem*,CItem*> items;
    std::vector<CItem *> created;
    for (const auto &pair : entry.items)
    {
        const SRegisteredItem &registeredItem = pair.second;
        for (int index = 0; index < registeredItem.count; ++index)
        {
            CItem *item = WorldServer->CreateItem(registeredItem.spec.c_str());
            if (!item)
            {
                for (CItem *createdItem : created)
                    createdItem->Release();
#if JMV_REGISTERED_LOOT_DIAGNOSTICS
                LOGERR("event=CREATE_FAIL object=%d player=%d item=%s created=%u action=retry",
                       entry.objectId,
                       entry.playerNumber,
                       registeredItem.spec.c_str(),
                       static_cast<unsigned int>(created.size()));
#endif
                return false;
            }
            created.push_back(item);
            items.AddTail(item);
        }
    }

    if (created.empty())
        return false;

    player->AddItemsToHoover(items);
    for (CItem *item : created)
        item->Release();

    ++G_registeredLoot.grants;
#if JMV_REGISTERED_LOOT_DIAGNOSTICS
    LOGINFO("event=GRANT object=%d player=%d item_types=%u item_count=%u",
            entry.objectId,
            entry.playerNumber,
            static_cast<unsigned int>(entry.items.size()),
            static_cast<unsigned int>(created.size()));
#endif
    return true;
}
} // namespace

bool JMV_RegisterLoot(CMapObject *object, const char *itemSpec)
{
    if (!(ModRuntime::Current() && ModRuntime::Current()->RegisteredLootEnabled()))
        return false;
    if (!EnsureCurrentWorld())
        return false;
    if (!IsRegisteredLever(object))
    {
        LogReject("not_lever", object, itemSpec);
        return false;
    }
    if (!itemSpec || !itemSpec[0] ||
        std::strlen(itemSpec) > REGISTERED_LOOT_MAX_ITEM_SPEC)
    {
        LogReject("invalid_item_spec", object, itemSpec);
        return false;
    }

    CGSVariableSet *vars = GetPersistentVars();
    if (!vars)
    {
        LogReject("no_persistent_party", object, itemSpec);
        return false;
    }

    const int objectId = object->GetID();
    const std::string spec(itemSpec);
    const std::string marker = BuildItemMarker(objectId, spec);
    const float storedValue = vars->GetVar(marker.c_str());
    const int storedCount = MarkerCount(storedValue);

    SRegisteredObject &entry = G_registeredLoot.objects[objectId];
    entry.objectId = objectId;
    SRegisteredItem &registeredItem = entry.items[spec];
    registeredItem.spec = spec;
    registeredItem.marker = marker;

    const int occurrence = ++G_registeredLoot.registrationOccurrences[marker];
    if (storedValue < 0.0f)
    {
        registeredItem.count = (std::max)(registeredItem.count,
                                          (std::max)(storedCount, occurrence));
        entry.consumed = true;
        if (!entry.cleanupConsumed)
        {
            entry.cleanupConsumed = true;
            G_registeredLoot.cleanupObjects.push_back(objectId);
        }
    }
    else
    {
        if (!ValidateItemSpec(spec))
        {
            entry.items.erase(spec);
            if (entry.items.empty())
                G_registeredLoot.objects.erase(objectId);
            LogReject("item_create_failed", object, itemSpec);
            return false;
        }
        registeredItem.count = (std::max)(registeredItem.count, occurrence);
        if (storedCount < occurrence)
            vars->SetVar(marker.c_str(), static_cast<float>(occurrence));
    }

    ++G_registeredLoot.registrations;
#if JMV_REGISTERED_LOOT_DIAGNOSTICS
    if (G_registeredLoot.registrations <= 12 ||
        (G_registeredLoot.registrations % 64) == 0)
    {
        LOGINFO("event=REGISTER seq=%u zone=%s object=%d item=%s occurrence=%d stored=%d consumed=%d objects=%u",
                G_registeredLoot.registrations,
                G_registeredLoot.zone.c_str(),
                objectId,
                spec.c_str(),
                occurrence,
                storedCount,
                entry.consumed ? 1 : 0,
                static_cast<unsigned int>(G_registeredLoot.objects.size()));
    }
#endif
    return true;
}

void JMV_RegisteredLootOnLeverActivated(CLeverObject *lever, CUnit *who)
{
    if (!EnsureCurrentWorld() || !lever)
        return;

    const auto found = G_registeredLoot.objects.find(lever->GetID());
    if (found == G_registeredLoot.objects.end())
        return;

    SRegisteredObject &entry = found->second;
    if (entry.consumed || entry.pending || entry.items.empty())
        return;

    CPlayer *player = who ? who->GetOwner() : nullptr;
    if (!player)
        player = WorldServer->GetPlayer(0);
    if (!player)
    {
#if JMV_REGISTERED_LOOT_DIAGNOSTICS
        LOGERR("event=ACTIVATE_REJECT object=%d reason=no_player",
               lever->GetID());
#endif
        return;
    }

    entry.playerNumber = player->GetPlayerNumber();
    entry.pending = true;
    entry.dueSegment = WorldServer->GetSegmentTimeCounter() +
        REGISTERED_LOOT_DELAY_SEGMENTS;
    SetPendingMarkers(entry, REGISTERED_LOOT_DELAY_SEGMENTS);
    G_registeredLoot.pendingObjects.push_back(entry.objectId);
    ++G_registeredLoot.activations;
#if JMV_REGISTERED_LOOT_DIAGNOSTICS
    LOGINFO("event=ACTIVATE seq=%u zone=%s object=%d player=%d current_segment=%d due_segment=%d delay_segments=%d item_types=%u",
            G_registeredLoot.activations,
            G_registeredLoot.zone.c_str(),
            entry.objectId,
            entry.playerNumber,
            WorldServer->GetSegmentTimeCounter(),
            entry.dueSegment,
            REGISTERED_LOOT_DELAY_SEGMENTS,
            static_cast<unsigned int>(entry.items.size()));
#endif
}

void JMV_RegisteredLootTick()
{
    if (!EnsureCurrentWorld())
        return;

    const int currentSegment = WorldServer->GetSegmentTimeCounter();
    std::vector<int> cleanupObjects;
    cleanupObjects.swap(G_registeredLoot.cleanupObjects);
    for (int objectId : cleanupObjects)
    {
        auto found = G_registeredLoot.objects.find(objectId);
        if (found == G_registeredLoot.objects.end())
            continue;
        SRegisteredObject &entry = found->second;
        if (!entry.consumed || !entry.cleanupConsumed)
            continue;

        MarkConsumed(entry);
        CMapObject *object = WorldServer->GetMapObject(objectId);
        if (IsRegisteredLever(object))
        {
            WorldServer->RemoveMapObject(objectId);
            ++G_registeredLoot.removals;
#if JMV_REGISTERED_LOOT_DIAGNOSTICS
            LOGINFO("event=REMOVE object=%d reason=already_consumed",
                    objectId);
#endif
        }
    }

    std::vector<int> pendingObjects;
    pendingObjects.swap(G_registeredLoot.pendingObjects);
    for (int objectId : pendingObjects)
    {
        auto found = G_registeredLoot.objects.find(objectId);
        if (found == G_registeredLoot.objects.end())
            continue;
        SRegisteredObject &entry = found->second;
        if (!entry.pending)
            continue;

        const int remaining = (std::max)(0, entry.dueSegment - currentSegment);
        if (remaining > 0)
        {
            if (remaining != entry.lastRemaining)
                SetPendingMarkers(entry, remaining);
            G_registeredLoot.pendingObjects.push_back(objectId);
            continue;
        }

        CMapObject *object = WorldServer->GetMapObject(objectId);
        if (!IsRegisteredLever(object))
        {
            entry.pending = false;
            ClearPendingMarkers(entry);
#if JMV_REGISTERED_LOOT_DIAGNOSTICS
            LOGWARN("event=DUE_CANCEL object=%d reason=object_missing",
                    objectId);
#endif
            continue;
        }

        if (!GrantRegisteredItems(entry))
        {
            entry.dueSegment = currentSegment + REGISTERED_LOOT_RETRY_SEGMENTS;
            SetPendingMarkers(entry, REGISTERED_LOOT_RETRY_SEGMENTS);
            G_registeredLoot.pendingObjects.push_back(objectId);
            continue;
        }

        MarkConsumed(entry);
        WorldServer->RemoveMapObject(objectId);
        ++G_registeredLoot.removals;
#if JMV_REGISTERED_LOOT_DIAGNOSTICS
        LOGINFO("event=REMOVE object=%d reason=pickup_complete segment=%d",
                objectId,
                currentSegment);
#endif
    }
}

void JMV_RegisteredLootOnSuccessfulSaveLoad()
{
    if (!(ModRuntime::Current() && ModRuntime::Current()->RegisteredLootEnabled()))
        return;

    ClearTransientState("save-load");
    if (!EnsureCurrentWorld())
        return;

    CGSVariableSet *vars = GetPersistentVars();
    if (!vars)
        return;

    for (const auto& variable : *vars)
    {
        const std::string name = variable.first;
        int objectId = -1;
        std::string tail;
        if (!ParseObjectMarker(name, objectId, tail))
            continue;

        SRegisteredObject &entry = G_registeredLoot.objects[objectId];
        entry.objectId = objectId;
        if (tail.compare(0, 5, "item.") == 0)
        {
            std::string spec;
            if (!HexDecode(tail.substr(5), spec))
                continue;
            const int count = MarkerCount(variable.second);
            if (count <= 0)
                continue;
            SRegisteredItem &item = entry.items[spec];
            item.spec = spec;
            item.marker = name;
            item.count = (std::max)(item.count, count);
            if (variable.second < 0.0f)
            {
                entry.consumed = true;
                entry.cleanupConsumed = true;
            }
        }
        else if (tail == "pending")
        {
            const int remaining = MarkerCount(variable.second);
            if (remaining > 0)
            {
                entry.pending = true;
                entry.lastRemaining = remaining;
                entry.dueSegment = WorldServer->GetSegmentTimeCounter() + remaining;
            }
        }
        else if (tail == "player")
        {
            entry.playerNumber = (std::max)(0, MarkerCount(variable.second) - 1);
        }
    }

    unsigned int itemTypes = 0;
    unsigned int pending = 0;
    unsigned int consumed = 0;
    for (auto &pair : G_registeredLoot.objects)
    {
        SRegisteredObject &entry = pair.second;
        itemTypes += static_cast<unsigned int>(entry.items.size());
        if (entry.items.empty())
        {
            entry.pending = false;
            entry.cleanupConsumed = false;
        }
        if (entry.consumed)
        {
            entry.pending = false;
            if (entry.cleanupConsumed)
                G_registeredLoot.cleanupObjects.push_back(entry.objectId);
            ++consumed;
        }
        else if (entry.pending)
        {
            G_registeredLoot.pendingObjects.push_back(entry.objectId);
            ++pending;
        }
    }
#if JMV_REGISTERED_LOOT_DIAGNOSTICS
    LOGINFO("event=LOAD_REBUILD zone=%s objects=%u item_types=%u pending=%u consumed=%u segment=%d",
            G_registeredLoot.zone.c_str(),
            static_cast<unsigned int>(G_registeredLoot.objects.size()),
            itemTypes,
            pending,
            consumed,
            WorldServer->GetSegmentTimeCounter());
#endif
}

void JMV_RegisteredLootReset(const char *reason)
{
    if (!(ModRuntime::Current() && ModRuntime::Current()->RegisteredLootEnabled()) && !G_registeredLoot.world)
        return;
    ClearTransientState(reason);
}



#undef WorldServer

bool JMV_CheckRegisteredLootCodec(){
 auto saved=G_registeredLoot;struct Restore{SRegisteredLootState&s;SRegisteredLootState old;~Restore(){s=std::move(old);}} restore{G_registeredLoot,std::move(saved)};
 G_registeredLoot=SRegisteredLootState();G_registeredLoot.markerPrefix="jmv.registerloot.v1.zone.";
 const std::string spec="sword{iron;1}";std::string decoded,tail;int id=-1;
 if(!HexDecode(HexEncode(spec),decoded)||decoded!=spec||HexDecode("0X",decoded))return false;
 if(!ParseObjectMarker(BuildItemMarker(123,spec),id,tail)||id!=123||tail!="item."+HexEncode(spec))return false;
 if(ParseObjectMarker("jmv.registerloot.v1.other.123.pending",id,tail))return false;
 return MarkerCount(-3)==3&&NormalizeZoneName("Maps/ZONE_1.mob")=="zone_1_mob";
}
