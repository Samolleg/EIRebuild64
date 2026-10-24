#include "StdAfx.h"
#include "SaveSchema.h"
#include "SaveLoader.h"
#include <cstring>
#include <limits>

namespace SaveSchema {
namespace {
constexpr std::uint8_t Magic[8] = {'E', 'I', 'R', '6', '4', 'S', 'A', 'V'};
constexpr ObjectId LastId = (std::numeric_limits<ObjectId>::max)();
struct Record
{
    std::uint32_t type = 0;
    ObjectId id = 0;
    std::uint32_t length = 0;
    Bytes body;
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
        types.count(id) || ids.count(std::type_index(nativeType)))
        throw Error(ErrorCode::UnknownType, "invalid or duplicate save type registration");
    types.emplace(id, std::move(type));
    ids.emplace(std::type_index(nativeType), id);
}
const Type &Registry::Find(std::uint32_t id) const
{
    const auto found = types.find(id);
    if (found == types.end()) throw Error(ErrorCode::UnknownType, "unknown save typeId");
    return found->second;
}
std::uint32_t Registry::Id(const CFundament &object) const
{
    const auto found = ids.find(std::type_index(typeid(object)));
    if (found == ids.end()) throw Error(ErrorCode::UnknownType, "unregistered save object");
    return found->second;
}

struct WriteState
{
    const Registry &registry;
    Limits limits;
    std::map<ObjectId, CPtr<CFundament>> objects;
    std::vector<ObjectId> pending;
    std::uint64_t bodyBytes = 0;

    ObjectId Reference(CFundament *object)
    {
        if (!object) return 0;
        const ObjectId id = object->GetRTID();
        if (!id || id >= LastId - 1) throw Error(ErrorCode::Identity, "invalid save ObjectId");
        const auto found = objects.find(id);
        if (found != objects.end())
        {
            if (found->second.GetPtr() != object)
                throw Error(ErrorCode::Identity, "duplicate save ObjectId");
            return id;
        }
        registry.Find(registry.Id(*object));
        if (objects.size() >= limits.objects)
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

void Store(CSave &file, const Registry &registry, const std::function<void(Writer &)> &root, Limits limits)
{
    if (file.GetPosition() != file.GetSize())
        throw Error(ErrorCode::Stream, "save output must be a new/truncated document");
    WriteState state{registry, limits};
    Bytes rootBody;
    Writer rootWriter(rootBody, state);
    root(rootWriter);
    std::map<ObjectId, Record> records;
    for (std::uint32_t i = 0; i < state.pending.size(); ++i)
    {
        const auto id = state.pending[i];
        CFundament *object = state.objects.at(id).GetPtr();
        Record record;
        record.id = id;
        record.type = registry.Id(*object);
        Writer writer(record.body, state);
        registry.Find(record.type).store(writer, *object);
        record.length = static_cast<std::uint32_t>(record.body.size());
        records.emplace(id, std::move(record));
    }
    for (const auto &entry : state.objects)
        if (entry.second->GetRTID() != entry.first)
            throw Error(ErrorCode::Identity, "save writer changed object identity");
    const std::uint64_t payload = records.size() * std::uint64_t{16} + state.bodyBytes;
    if (payload > limits.payloadBytes || payload + HeaderBytes > UINT32_MAX - file.GetPosition())
        throw Error(ErrorCode::Limit, "save document limit");
    Bytes header(Magic, Magic + 8);
    Put(header, Version, 4);
    Put(header, HeaderBytes, 4);
    Put(header, payload, 8);
    Put(header, records.size(), 4);
    Put(header, rootBody.size(), 4);
    file.StartSaving();
    WriteBytes(file, header);
    for (const auto &entry : records)
    {
        const auto &record = entry.second;
        Bytes descriptor;
        Put(descriptor, record.type, 4);
        Put(descriptor, record.id, 8);
        Put(descriptor, record.length, 4);
        WriteBytes(file, descriptor);
    }
    WriteBytes(file, rootBody);
    for (const auto &entry : records) WriteBytes(file, entry.second.body);
}

Graph Load(CSave &file, const Registry &registry, const std::function<void(Reader &)> &root, Limits limits)
{
    file.StartLoading();
    std::uint8_t magic[8] = {};
    if (file.Remaining() < HeaderBytes || file.Read(magic, 8) != 8 ||
        std::memcmp(magic, Magic, 8) != 0)
        throw Error(ErrorCode::Incompatible, "Несовместимое сохранение: требуется EIR64SAV");
    const auto version = Get(file, 4), headerSize = Get(file, 4), payload = Get(file, 8);
    const auto count = Get(file, 4), rootSize = Get(file, 4);
    if (version != Version || headerSize != HeaderBytes)
        throw Error(ErrorCode::Incompatible, "Несовместимое сохранение: версия схемы не поддерживается");
    if (payload > limits.payloadBytes || count > limits.objects || rootSize > limits.fieldBytes)
        throw Error(ErrorCode::Limit, "save header limit");
    if (payload != file.Remaining() || count * 16 + rootSize > payload)
        throw Error(ErrorCode::Invalid, "invalid save payload length");
    std::vector<Record> records;
    records.reserve(static_cast<std::uint32_t>(count));
    std::map<ObjectId, bool> seen;
    std::uint64_t total = count * 16 + rootSize;
    ObjectId maxId = 0;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        Record record;
        record.type = static_cast<std::uint32_t>(Get(file, 4));
        record.id = Get(file, 8);
        record.length = static_cast<std::uint32_t>(Get(file, 4));
        registry.Find(record.type); // reject all unknown types before constructing anything
        if (!record.id || record.id >= LastId - 1 || !seen.emplace(record.id, true).second)
            throw Error(ErrorCode::Identity, "invalid or duplicate save ObjectId");
        if (record.length > limits.fieldBytes || record.length > payload - total)
            throw Error(ErrorCode::Invalid, "invalid save object length");
        total += record.length;
        maxId = (std::max)(maxId, record.id);
        records.push_back(std::move(record));
    }
    if (total != payload) throw Error(ErrorCode::Invalid, "inconsistent save body lengths");
    Bytes rootBody;
    ReadBytes(file, rootBody, static_cast<std::uint32_t>(rootSize));
    for (auto &record : records) ReadBytes(file, record.body, record.length);

    Graph graph;
    ReadState state{graph, limits};
    try
    {
        // Pass 1: own the complete registry, including unreferenced entries.
        for (const auto &record : records)
        {
            auto object = registry.Find(record.type).create(record.id);
            if (!object.GetPtr() || object->GetRTID() != record.id || registry.Id(*object) != record.type)
                throw Error(ErrorCode::Identity, "save factory did not restore identity/type");
            graph.objects.emplace(record.id, object);
        }
        // Pass 2: every reference can resolve now, including forward links/cycles.
        for (const auto &record : records)
        {
            Reader reader(record.body, state);
            registry.Find(record.type).load(reader, *graph.Find(record.id));
            reader.Finish();
        }
        // Root callback only stages references; publication belongs to the caller.
        Reader reader(rootBody, state);
        root(reader);
        reader.Finish();
        for (const auto &record : records)
            if (graph.Find(record.id)->GetRTID() != record.id)
                throw Error(ErrorCode::Identity, "save loader changed object identity");
        ObjectIdentity::SetNext(maxId + 1);
    }
    catch (...)
    {
        // Break any partially restored cycles while ALL registry entries are pinned.
        for (const auto &record : records)
            if (CFundament *object = graph.Find(record.id))
                registry.Find(record.type).discard(*object);
        throw;
    }
    return graph;
}
} // namespace SaveSchema
