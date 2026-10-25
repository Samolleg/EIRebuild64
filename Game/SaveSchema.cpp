#include "StdAfx.h"
#include "SaveSchema.h"
#include "SaveLoader.h"
#include "GameBase.h"
#include "BaseManager.h"
#include "MapObject.h"
#include <cstring>
#include <limits>

namespace SaveSchema {
[[noreturn]] void RequireDynamicSchema()
{
    throw Error(ErrorCode::Incompatible, "dynamic state requires SaveSchema (P17)");
}
namespace {
constexpr std::uint8_t Magic[8] = {'E', 'I', 'R', '6', '4', 'S', 'A', 'V'};
constexpr ObjectId LastId = (std::numeric_limits<ObjectId>::max)();
struct Record
{
    std::uint32_t type = 0;
    ObjectId id = 0;
    std::uint32_t length = 0;
    Bytes body;
    ObjectId owner = 0;
    int mapId = -1;
    std::uint32_t runtimeType = 0, subtype = 0, flags = 0;
};
void Put(Bytes &bytes, std::uint64_t value, unsigned width)
{
    for (unsigned i = 0; i < width; ++i)
        bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}
std::uint64_t Get(CSave &file, unsigned width)
{
    std::uint8_t bytes[8] = {};
    if (file.Read(bytes, width) != width || !file.IsOk())
        throw Error(ErrorCode::Stream, "truncated save");
    std::uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i)
        value |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
    return value;
}
void ReadBytes(CSave &file, Bytes &bytes, std::uint32_t count)
{
    if (count > file.Remaining()) throw Error(ErrorCode::Invalid, "truncated save body");
    bytes.resize(count);
    if (count && (file.Read(bytes.data(), count) != count || !file.IsOk()))
        throw Error(ErrorCode::Stream, "save read failed");
}
void WriteBytes(CSave &file, const Bytes &bytes)
{
    if (bytes.empty()) return;
    const auto before = file.GetPosition();
    const auto count = static_cast<std::uint32_t>(bytes.size());
    file.Write(bytes.data(), count);
    if (!file.IsOk() || file.GetPosition() != before + count)
        throw Error(ErrorCode::Stream, "save write failed");
}
}

void Registry::Register(std::uint32_t id, const std::type_info &nativeType, Type type)
{
    if (!id || !type.create || !type.store || !type.load || !type.discard ||
        types.count(id) || gameTypes.count(id) || ids.count(std::type_index(nativeType)))
        throw Error(ErrorCode::UnknownType, std::string("invalid or duplicate save type: ") + nativeType.name() + " typeId=" + std::to_string(id));
    types.emplace(id, std::move(type));
    ids.emplace(std::type_index(nativeType), id);
}
void Registry::RegisterGame(std::uint32_t id, const std::type_info &native, GameType type)
{
    if (!id || !type.create || !type.store || !type.load || !type.discard ||
        types.count(id) || gameTypes.count(id) || gameIds.count(std::type_index(native)))
        throw Error(ErrorCode::UnknownType, std::string("invalid or duplicate game type: ") + native.name() + " typeId=" + std::to_string(id));
    gameTypes.emplace(id, std::move(type));
    gameIds.emplace(std::type_index(native), id);
}
const GameType &Registry::FindGame(std::uint32_t id) const
{
    const auto it = gameTypes.find(id);
    if (it == gameTypes.end()) throw Error(ErrorCode::UnknownType, "unknown game typeId: " + std::to_string(id));
    return it->second;
}
std::uint32_t Registry::GameTypeId(const CGameBase &object) const
{
    const auto it = gameIds.find(std::type_index(typeid(object)));
    if (it == gameIds.end()) throw Error(ErrorCode::UnknownType, std::string("unregistered game object: ") + typeid(object).name());
    return it->second;
}
CGameBase *Registry::MapObject(int id) const
{
    if (id == -1) return nullptr;
    if (id < 0 || !gameObjects.map)
        throw Error(ErrorCode::Identity, "map object writer not bound or invalid map ID");
    CGameBase *object = gameObjects.map(id);
    if (!dynamic_cast<CMapObject *>(object) || object->GetID() != id)
        throw Error(ErrorCode::Identity, "dangling runtime map ID");
    return object;
}
const Type &Registry::Find(std::uint32_t id) const
{
    const auto found = types.find(id);
    if (found == types.end()) throw Error(ErrorCode::UnknownType, "unknown save typeId: " + std::to_string(id));
    return found->second;
}
std::uint32_t Registry::Id(const CFundament &object) const
{
    const auto found = ids.find(std::type_index(typeid(object)));
    if (found == ids.end()) throw Error(ErrorCode::UnknownType, std::string("unregistered save object: ") + typeid(object).name());
    return found->second;
}

struct WriteState
{
    const Registry &registry;
    Limits limits;
    std::map<ObjectId, CPtr<CFundament>> objects;
    std::vector<ObjectId> pending;
    std::map<ObjectId, CSmartPtr<CGameBase>> gameObjects;
    std::vector<ObjectId> pendingGames;
    ObjectId GameReference(CGameBase *object)
    {
        if (!object) return 0;
        const auto id = object->GetRTID();
        if (!id || id >= LastId - 1 || objects.count(id))
            throw Error(ErrorCode::Identity, "invalid or overlapping game ObjectId");
        const auto it = gameObjects.find(id);
        if (it != gameObjects.end())
        {
            if (static_cast<CGameBase *>(it->second) != object)
                throw Error(ErrorCode::Identity, "duplicate game ObjectId");
            return id;
        }
        registry.FindGame(registry.GameTypeId(*object));
        if (gameObjects.size() + objects.size() >= limits.objects)
            throw Error(ErrorCode::Limit, "save object count limit");
        gameObjects.emplace(id, object);
        pendingGames.push_back(id);
        return id;
    }
    std::uint64_t bodyBytes = 0;

    ObjectId Reference(CFundament *object)
    {
        if (!object) return 0;
        const ObjectId id = object->GetRTID();
        if (!id || id >= LastId - 1 || gameObjects.count(id)) throw Error(ErrorCode::Identity, "invalid save ObjectId");
        const auto found = objects.find(id);
        if (found != objects.end())
        {
            if (found->second.GetPtr() != object)
                throw Error(ErrorCode::Identity, "duplicate save ObjectId");
            return id;
        }
        registry.Find(registry.Id(*object));
        if (objects.size() + gameObjects.size() >= limits.objects)
            throw Error(ErrorCode::Limit, "save object count limit");
        objects.emplace(id, object); // pin before visiting ANY fields: cycles are legal
        pending.push_back(id);
        return id;
    }
};
struct ReadState
{
    Graph &graph;
    Limits limits;
    const Registry &registry;
    CFundament *Reference(ObjectId id)
    {
        if (!id) return nullptr;
        CFundament *object = graph.Find(id);
        if (!object) throw Error(ErrorCode::Invalid, "dangling save reference");
        return object;
    }
};

void Writer::Append(const std::uint8_t *source, std::uint32_t count)
{
    if (bytes.size() > state.limits.fieldBytes || count > state.limits.fieldBytes - bytes.size() ||
        state.bodyBytes > state.limits.payloadBytes || count > state.limits.payloadBytes - state.bodyBytes)
        throw Error(ErrorCode::Limit, "save field/payload limit");
    if (count) bytes.insert(bytes.end(), source, source + count);
    state.bodyBytes += count;
}
void Writer::UInt(std::uint64_t value, unsigned width)
{
    std::uint8_t encoded[8];
    for (unsigned i = 0; i < width; ++i) encoded[i] = static_cast<std::uint8_t>(value >> (8 * i));
    Append(encoded, width);
}
void Writer::Float32(float value)
{
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "IEEE binary32 required");
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    UInt32(bits);
}
void Writer::Float64(double value)
{
    static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559, "IEEE binary64 required");
    std::uint64_t bits;
    std::memcpy(&bits, &value, 8);
    UInt64(bits);
}
void Writer::Count(std::uint32_t count)
{
    if (count > state.limits.elements) throw Error(ErrorCode::Limit, "save element count limit");
    UInt32(count);
}
void Writer::String(const std::string &value)
{
    if (value.size() > state.limits.fieldBytes) throw Error(ErrorCode::Limit, "save string limit");
    UInt32(static_cast<std::uint32_t>(value.size()));
    Append(reinterpret_cast<const std::uint8_t *>(value.data()), static_cast<std::uint32_t>(value.size()));
}
void Writer::Blob(const Bytes &value)
{
    if (value.size() > state.limits.fieldBytes) throw Error(ErrorCode::Limit, "save blob limit");
    UInt32(static_cast<std::uint32_t>(value.size()));
    Append(value.data(), static_cast<std::uint32_t>(value.size()));
}
void Writer::GameReference(CGameBase *object) { UInt64(state.GameReference(object)); }
CGameBase *Reader::GameReference()
{
    const auto id = UInt64();
    if (!id) return nullptr;
    auto *object = state.graph.FindGame(id);
    if (!object) throw Error(ErrorCode::Identity, "dangling game ObjectId");
    return object;
}
void Writer::MapReference(int id) { GameReference(state.registry.MapObject(id)); }
int Reader::MapReference()
{
    auto *object = GameReferenceAs<CMapObject>();
    if (!object) return -1;
    if (object->GetID() < 0) throw Error(ErrorCode::Identity, "invalid restored map ID");
    return object->GetID();
}
std::int16_t Reader::Int16()
{
    const auto bits = UInt16();
    return bits <= INT16_MAX ? static_cast<std::int16_t>(bits) :
        static_cast<std::int16_t>(-1 - static_cast<std::int16_t>(static_cast<std::uint16_t>(~bits)));
}
void Writer::Reference(CFundament *object) { UInt64(state.Reference(object)); }

void Reader::Require(std::uint32_t count) const
{
    if (count > bytes.size() - position) throw Error(ErrorCode::Invalid, "truncated save field");
}
std::uint64_t Reader::UInt(unsigned width)
{
    Require(width);
    std::uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i) value |= static_cast<std::uint64_t>(bytes[position++]) << (8 * i);
    return value;
}
std::int32_t Reader::Int32()
{
    const auto bits = UInt32();
    return bits <= INT32_MAX ? static_cast<std::int32_t>(bits) : -1 - static_cast<std::int32_t>(~bits);
}
std::int64_t Reader::Int64()
{
    const auto bits = UInt64();
    return bits <= INT64_MAX ? static_cast<std::int64_t>(bits) : -1 - static_cast<std::int64_t>(~bits);
}
bool Reader::Bool()
{
    const auto value = UInt8();
    if (value > 1) throw Error(ErrorCode::Invalid, "invalid save bool");
    return value != 0;
}
float Reader::Float32()
{
    const auto bits = UInt32();
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}
double Reader::Float64()
{
    const auto bits = UInt64();
    double value;
    std::memcpy(&value, &bits, 8);
    return value;
}
std::uint32_t Reader::Count()
{
    const auto count = UInt32();
    if (count > state.limits.elements) throw Error(ErrorCode::Limit, "save element count limit");
    return count;
}
std::string Reader::String()
{
    const auto count = UInt32();
    if (count > state.limits.fieldBytes) throw Error(ErrorCode::Limit, "save string limit");
    Require(count);
    if (!count) return {};
    std::string value(reinterpret_cast<const char *>(bytes.data() + position), count);
    position += count;
    return value;
}
Bytes Reader::Blob()
{
    const auto count = UInt32();
    if (count > state.limits.fieldBytes) throw Error(ErrorCode::Limit, "save blob limit");
    Require(count);
    Bytes value(bytes.begin() + position, bytes.begin() + position + count);
    position += count;
    return value;
}
CFundament *Reader::Reference() { return state.Reference(UInt64()); }
void Reader::Finish() const
{
    if (position != bytes.size()) throw Error(ErrorCode::Invalid, "unconsumed save fields");
}
CFundament *Graph::Find(ObjectId id) const
{
    const auto found = objects.find(id);
    return found == objects.end() ? nullptr : found->second.GetPtr();
}

CGameBase *Graph::FindGame(ObjectId id) const
{
    const auto it = gameObjects.find(id);
    return it == gameObjects.end() ? nullptr : static_cast<CGameBase *>(it->second);
}
void Graph::Swap(Graph &other) noexcept
{
    registryOwner.swap(other.registryOwner);
    objects.swap(other.objects); gameObjects.swap(other.gameObjects);
    discard.swap(other.discard); std::swap(savedNext, other.savedNext);
}
Graph &Graph::operator=(Graph &&other) noexcept
{
    if (this != &other) { Graph old(std::move(other)); Swap(old); }
    return *this;
}
Graph::~Graph()
{
    if (!discard.empty())
    {
        ObjectIdentity::RestoreNamespace cleanup(savedNext);
        for (auto &release : discard) release();
        discard.clear(); objects.clear(); gameObjects.clear();
    }
}

std::vector<ObjectId> Graph::Identities() const
{
    std::vector<ObjectId> result;
    for (const auto &x:objects) result.push_back(x.first);
    for (const auto &x:gameObjects) result.push_back(x.first);
    return result;
}

void Graph::Adopt()
{
    ObjectIdentity::RestoreNamespace::Publish(savedNext);
    discard.clear();
}

void Store(CSave &file, const Registry &registry, const std::function<void(Writer &)> &root, Limits limits)
{
    if (file.GetPosition() != file.GetSize())
        throw Error(ErrorCode::Stream, "save output must be a new/truncated document");
    WriteState state{registry, limits};
    Bytes rootBody;
    Writer rootWriter(rootBody, state);
    root(rootWriter);
    std::map<ObjectId, Record> records, games;
    std::size_t fi = 0, gi = 0;
    while (fi < state.pending.size() || gi < state.pendingGames.size())
    {
        if (fi < state.pending.size())
        {
            Record record;
            record.id = state.pending[fi++];
            auto *object = state.objects.at(record.id).GetPtr();
            record.type = registry.Id(*object);
            Writer writer(record.body, state);
            registry.Find(record.type).store(writer, *object);
            record.length = static_cast<std::uint32_t>(record.body.size());
            records.emplace(record.id, std::move(record));
        }
        else
        {
            Record record;
            record.id = state.pendingGames[gi++];
            CGameBase *object = state.gameObjects.at(record.id);
            record.type = registry.GameTypeId(*object);
            record.owner = ObjectIdentity::IdOf(object->GetManager());
            record.runtimeType = static_cast<std::uint32_t>(object->GetTypeID());
            record.mapId = object->GetID(); record.subtype = object->GetSubtypeID(); record.flags = object->GetFlags();
            Writer writer(record.body, state);
            registry.FindGame(record.type).store(writer, *object);
            record.length = static_cast<std::uint32_t>(record.body.size());
            games.emplace(record.id, std::move(record));
        }
    }
    for (const auto &entry : state.objects)
        if (entry.second->GetRTID() != entry.first)
            throw Error(ErrorCode::Identity, "save writer changed object identity");
    for (const auto &entry : state.gameObjects)
        if (entry.second->GetRTID() != entry.first)
            throw Error(ErrorCode::Identity, "save writer changed game identity");
    const std::uint64_t payload = records.size() * std::uint64_t{16} + games.size() * std::uint64_t{40} + state.bodyBytes;
    if (payload > limits.payloadBytes || payload + HeaderBytes > UINT32_MAX - file.GetPosition())
        throw Error(ErrorCode::Limit, "save document limit");
    Bytes header(Magic, Magic + 8);
    Put(header, Version, 4); Put(header, HeaderBytes, 4); Put(header, payload, 8);
    Put(header, records.size(), 4); Put(header, rootBody.size(), 4);
    Put(header, games.size(), 4); Put(header, 0, 4); Put(header, ObjectIdentity::GetNext(), 8);
    file.StartSaving(); WriteBytes(file, header);
    auto descriptor = [&](const Record &record, bool game) {
        Bytes bytes;
        Put(bytes, record.type, 4); Put(bytes, record.id, 8); Put(bytes, record.length, 4);
        if (game) { Put(bytes, static_cast<std::uint32_t>(record.mapId), 4);
            Put(bytes, record.runtimeType, 4); Put(bytes, record.subtype, 4); Put(bytes, record.flags, 4); Put(bytes, record.owner, 8); }
        WriteBytes(file, bytes);
    };
    for (const auto &entry : records) descriptor(entry.second, false);
    for (const auto &entry : games) descriptor(entry.second, true);
    WriteBytes(file, rootBody);
    for (const auto &entry : records) WriteBytes(file, entry.second.body);
    for (const auto &entry : games) WriteBytes(file, entry.second.body);
}

Graph Load(CSave &file, const Registry &registry, const std::function<void(Reader &)> &root, Limits limits)
{
    file.StartLoading();
    std::uint8_t magic[8] = {};
    if (file.Remaining() < HeaderBytes || file.Read(magic, 8) != 8 || std::memcmp(magic, Magic, 8) != 0)
        throw Error(ErrorCode::Incompatible, "Несовместимое сохранение: требуется EIR64SAV");
    const auto version = Get(file, 4), headerSize = Get(file, 4), payload = Get(file, 8);
    const auto count = Get(file, 4), rootSize = Get(file, 4), gameCount = Get(file, 4);
    const auto reserved = Get(file, 4), next = Get(file, 8);
    if (version != Version || headerSize != HeaderBytes || reserved)
        throw Error(ErrorCode::Incompatible, "Несовместимое сохранение: версия схемы не поддерживается");
    if (payload > limits.payloadBytes || count + gameCount > limits.objects || rootSize > limits.fieldBytes)
        throw Error(ErrorCode::Limit, "save header limit");
    const std::uint64_t tableBytes = count * 16 + gameCount * 40;
    if (payload != file.Remaining() || tableBytes + rootSize > payload)
        throw Error(ErrorCode::Invalid, "invalid save payload length");
    std::vector<Record> records, games;
    records.reserve(static_cast<std::uint32_t>(count)); games.reserve(static_cast<std::uint32_t>(gameCount));
    std::map<ObjectId, bool> seen;
    std::uint64_t total = tableBytes + rootSize;
    ObjectId maxId = 0;
    auto descriptor = [&](bool game) {
        Record record;
        record.type = static_cast<std::uint32_t>(Get(file, 4)); record.id = Get(file, 8);
        record.length = static_cast<std::uint32_t>(Get(file, 4));
        if (game)
        {
            registry.FindGame(record.type);
            const auto bits = static_cast<std::uint32_t>(Get(file, 4));
            record.mapId = bits <= INT32_MAX ? static_cast<int>(bits) : -1 - static_cast<int>(~bits);
            record.runtimeType = static_cast<std::uint32_t>(Get(file, 4));
            record.subtype = static_cast<std::uint32_t>(Get(file, 4)); record.flags = static_cast<std::uint32_t>(Get(file, 4));
            record.owner = Get(file, 8);
            if (record.owner >= LastId - 1) throw Error(ErrorCode::Identity, "invalid save owner identity");
        }
        else registry.Find(record.type);
        if (!record.id || record.id >= LastId - 1 || !seen.emplace(record.id, true).second)
            throw Error(ErrorCode::Identity, "invalid or duplicate save ObjectId");
        if (record.length > limits.fieldBytes || record.length > payload - total)
            throw Error(ErrorCode::Invalid, "invalid save object length");
        total += record.length; maxId = (std::max)(maxId, (std::max)(record.id, record.owner));
        return record;
    };
    for (std::uint32_t i = 0; i < count; ++i) records.push_back(descriptor(false));
    for (std::uint32_t i = 0; i < gameCount; ++i) games.push_back(descriptor(true));
    if (total != payload || next <= maxId || next >= LastId)
        throw Error(ErrorCode::Identity, "inconsistent save sizes/next ObjectId");
    Bytes rootBody;
    ReadBytes(file, rootBody, static_cast<std::uint32_t>(rootSize));
    for (auto &record : records) ReadBytes(file, record.body, record.length);
    for (auto &record : games) ReadBytes(file, record.body, record.length);
    Graph graph;
    graph.registryOwner = std::make_shared<Registry>(registry);
    graph.savedNext = next;
    ReadState state{graph, limits, registry};
    ObjectIdentity::RestoreNamespace allocation(next); // both tables, before any constructor
    // Persist the local watermark even if a later factory/field throws.
    struct Watermark
    {
        ObjectId &saved;
        ~Watermark() { saved = ObjectIdentity::GetNext(); }
    } watermark{graph.savedNext};
    graph.discard.reserve(records.size() + games.size());
    for (const auto &record : records)
    {
        const auto &type = registry.Find(record.type);
        auto object = type.create(record.id);
        if (!object.GetPtr() || object->GetRTID() != record.id || registry.Id(*object) != record.type)
            throw Error(ErrorCode::Identity, "save factory did not restore identity/type");
        graph.objects.emplace(record.id, object);
        graph.discard.emplace_back([object, release = type.discard]() { release(*object); });
    }
    for (const auto &record : games)
    {
        const auto &type = registry.FindGame(record.type);
        auto object = type.create(record.id, record.mapId, record.runtimeType, record.subtype, record.owner);
        if (object == nullptr || object->GetRTID() != record.id || registry.GameTypeId(*object) != record.type ||
            static_cast<std::uint32_t>(object->GetTypeID()) != record.runtimeType ||
            ObjectIdentity::IdOf(object->GetManager()) != record.owner)
            throw Error(ErrorCode::Identity, "game factory did not restore identity/type");
        object->SetID(record.mapId); object->SetSubtypeID(record.subtype);
        object->ResetAllFlags(); object->SetFlag(record.flags);
        graph.gameObjects.emplace(record.id, object);
        graph.discard.emplace_back([object, release = type.discard]() { release(*object); });
    }
    for (const auto &record : games)
    {
        Reader reader(record.body, state);
        registry.FindGame(record.type).load(reader, *graph.FindGame(record.id)); reader.Finish();
    }
    for (const auto &record : records)
    {
        Reader reader(record.body, state);
        registry.Find(record.type).load(reader, *graph.Find(record.id)); reader.Finish();
    }
    Reader reader(rootBody, state); root(reader); reader.Finish();
    for (const auto &record : records)
        if (graph.Find(record.id)->GetRTID() != record.id)
            throw Error(ErrorCode::Identity, "save loader changed object identity");
    for (const auto &record : games)
        if (graph.FindGame(record.id)->GetRTID() != record.id)
            throw Error(ErrorCode::Identity, "save loader changed game identity");
    graph.savedNext = ObjectIdentity::GetNext();
    allocation.KeepInParent();
    return graph;
}
} // namespace SaveSchema
