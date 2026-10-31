#pragma once
#include "Basic.h"
#include "GameBase.h"
#include "SmartPtr.h"
#include "SaveValueCodecs.h"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <vector>

class CSave;
class CGameBase;

// Dynamic saves only. Asset files continue to use CStructureSaver.
// No auto detection/fallback and no calls to Serialize(CStructureSaver&).
namespace SaveSchema {
class Slot;
using Bytes = std::vector<std::uint8_t>;
constexpr std::uint32_t Version = 5;
constexpr std::uint32_t HeaderBytes = 48;
// Wire v5 (LE): magic[8], u32 version/headerBytes, u64 payloadBytes,
// u32 fundamentCount/rootBytes/gameCount/reserved, u64 nextId.
// CFundament descriptor: u32 type, u64 id, u32 bodyBytes.
// CGameBase descriptor: same + i32 mapId, u32 runtimeType/subtype/flags, u64 ownerId.
// Root, fundament bodies, game bodies follow in descriptor order.
// Both registries share one namespace reserved before any factory.
// v3 defines CStringArray fields (u32 count followed by length-prefixed strings).
// v4 stores settlement bindings and the namespace of every script thread.
// v5 adds settlement alarms and party membership, including boundary history.
// References = u64; text/blob = u32 length + bytes; Count = u32.

struct Limits
{
    std::uint32_t payloadBytes = 256u * 1024u * 1024u;
    std::uint32_t fieldBytes = 16u * 1024u * 1024u;
    std::uint32_t objects = 1000000;
    std::uint32_t elements = 1000000;
};

enum class ErrorCode { Incompatible, Invalid, Limit, UnknownType, Identity, Stream };
class Error : public std::runtime_error
{
public:
    const ErrorCode code;
    Error(ErrorCode code, const std::string &message) : std::runtime_error(message), code(code) {}
};

class Writer;
class Reader;
struct WriteState;
struct ReadState;

// All callbacks are mandatory; no legacy Serialize/raw-memory default exists.
// create(id) restores the *runtime* identity before returning a fresh, pinned object.
// load must only link registry objects, never publish them or construct graph nodes.
// discard must remove restored links without throwing (failed-load cleanup).
struct Type
{
    std::function<CPtr<CFundament>(ObjectId)> create;
    std::function<void(Writer &, CFundament &)> store;
    std::function<void(Reader &, CFundament &)> load;
    std::function<void(CFundament &)> discard;
};

// Selected by ValueCodec<T> at compile time; callbacks never capture runtime state.
struct ValueType
{
    void (*store)(Writer &, const void *);
    void (*load)(Reader &, void *);
};
// Factories bind staged owners, never global live managers. They capture owners
// strongly: Graph retains a copy of Registry through cleanup/adoption. The factory
// restores ObjectId via Create/CreateWith, runtimeType and registration with its
// staged manager; it must not reuse manager trash. Arguments: id/mapId/type/subtype/ownerId.
// The owner manifest is staged before loading this graph; ownerId is its saved
// manager identity, so identical map IDs in different parties stay unambiguous.
struct GameType
{
    std::function<CSmartPtr<CGameBase>(ObjectId, int, std::uint32_t, std::uint32_t, ObjectId)> create;
    std::function<void(Writer &, CGameBase &)> store;
    std::function<void(Reader &, CGameBase &)> load;
    std::function<void(CGameBase &)> discard;
};
struct GameObjects
{
    // P17 supplies a lookup in the captured map-object table, never the live world.
    std::function<CGameBase *(int)> map;
};
class Registry
{
    std::map<std::uint32_t, Type> types;
    std::map<std::type_index, std::uint32_t> ids;
    GameObjects gameObjects;
    std::map<std::uint32_t, GameType> gameTypes;
    std::map<std::type_index, std::uint32_t> gameIds;
public:
    void Register(std::uint32_t id, const std::type_info &nativeType, Type type);
    void RegisterGame(std::uint32_t id, const std::type_info &, GameType);
    const GameType &FindGame(std::uint32_t id) const;
    std::uint32_t GameTypeId(const CGameBase &) const;
    void SetGameObjects(GameObjects objects) { gameObjects = std::move(objects); }
    CGameBase *MapObject(int id) const;
    const Type &Find(std::uint32_t id) const;
    std::uint32_t Id(const CFundament &object) const;
};

// Sequential, explicitly typed fields; field order belongs to Version/type schema.
// Native size_t, enum, structs, pointers and AddRaw are deliberately unsupported.
class Writer
{
    Bytes &bytes;
    WriteState &state;
    void UInt(std::uint64_t value, unsigned width);
    void Append(const std::uint8_t *data, std::uint32_t count);
public:
    Writer(Bytes &bytes, WriteState &state) : bytes(bytes), state(state) {}
    void UInt8(std::uint8_t value) { UInt(value, 1); }
    void UInt16(std::uint16_t value) { UInt(value, 2); }
    void UInt32(std::uint32_t value) { UInt(value, 4); }
    void UInt64(std::uint64_t value) { UInt(value, 8); }
    void Int16(std::int16_t value) { UInt16(static_cast<std::uint16_t>(value)); }
    void Int32(std::int32_t value) { UInt32(static_cast<std::uint32_t>(value)); }
    void Int64(std::int64_t value) { UInt64(static_cast<std::uint64_t>(value)); }
    void Bool(bool value) { UInt8(value ? 1 : 0); }
    void Float32(float value);
    void Float64(double value);
    void Count(std::uint32_t count);
    void String(const std::string &value); // bytes in the consumer's declared encoding
    void Blob(const Bytes &value); // encoded bytes, never a native object image
    template <class T, class Codec = ValueCodec<T>> void Value(const T &value)
    {
        UInt32(Codec::Id);
        try { Codec::Get().store(*this, &value); }
        catch (const Error &e) { throw Error(e.code, std::string(typeid(T).name()) + ": " + e.what()); }
    }
    void GameReference(CGameBase *object);
    void MapReference(int id); // runtime map ID -> UInt64 ObjectId; -1 -> null
    void Reference(CFundament *object); // uint64 ObjectId; zero = null
};

class Reader
{
    const Bytes &bytes;
    ReadState &state;
    std::uint32_t position = 0;
    std::uint64_t UInt(unsigned width);
    void Require(std::uint32_t count) const;
public:
    Reader(const Bytes &bytes, ReadState &state) : bytes(bytes), state(state) {}
    std::uint8_t UInt8() { return static_cast<std::uint8_t>(UInt(1)); }
    std::uint16_t UInt16() { return static_cast<std::uint16_t>(UInt(2)); }
    std::uint32_t UInt32() { return static_cast<std::uint32_t>(UInt(4)); }
    std::uint64_t UInt64() { return UInt(8); }
    std::int16_t Int16();
    std::int32_t Int32();
    std::int64_t Int64();
    bool Bool();
    float Float32();
    double Float64();
    std::uint32_t Count();
    std::string String();
    Bytes Blob();
    template <class T, class Codec = ValueCodec<T>> void Value(T &value)
    {
        const auto id = UInt32();
        if (id != Codec::Id)
            throw Error(ErrorCode::Invalid, std::string("save value type mismatch: ") + typeid(T).name() +
                " stored typeId=" + std::to_string(id) + " expected=" + std::to_string(Codec::Id));
        try { Codec::Get().load(*this, &value); }
        catch (const Error &e) { throw Error(e.code, std::string(typeid(T).name()) + ": " + e.what()); }
    }
    CGameBase *GameReference();
    int MapReference(); // UInt64 ObjectId -> restored map ID; null -> -1
    template <class T> T *GameReferenceAs()
    {
        CGameBase *object = GameReference();
        T *typed = dynamic_cast<T *>(object);
        if (object && !typed) throw Error(ErrorCode::Invalid, "game reference type mismatch");
        return typed;
    }
    CFundament *Reference();
    template <class T> T *ReferenceAs()
    {
        CFundament *object = Reference();
        T *typed = dynamic_cast<T *>(object);
        if (object && !typed) throw Error(ErrorCode::Invalid, "save reference type mismatch");
        return typed;
    }
    void Finish() const; // exact field consumption, including empty bodies
};

// Keeps EVERY created object alive through both passes and root adoption.
// Caller retains this owner until the restored world has adopted its references.
class Graph
{
    std::shared_ptr<const Registry> registryOwner;
    std::map<ObjectId, CPtr<CFundament>> objects;
    std::map<ObjectId, CSmartPtr<CGameBase>> gameObjects;
    std::vector<std::function<void()>> discard;
    ObjectId savedNext = 1;
    void Swap(Graph &) noexcept;
    friend struct ReadState;
    friend Graph Load(CSave &, const Registry &, const std::function<void(Reader &)> &, Limits);
public:
    Graph() = default;
    Graph(const Graph &) = delete;
    Graph &operator=(const Graph &) = delete;
    Graph(Graph &&other) noexcept { Swap(other); }
    Graph &operator=(Graph &&other) noexcept;
    ~Graph();
    CFundament *Find(ObjectId id) const;
    CGameBase *FindGame(ObjectId id) const;
    ObjectId NextId() const { return savedNext; }
    std::vector<ObjectId> Identities() const;
    // After complete slot adoption only. Before that, destruction breaks cycles.
    void Adopt();
};

// CSave's current stream abstraction is uint32-sized. The format's payload length
// is uint64, but this implementation rejects documents exceeding Limits/CSave.
// Exclusive world restore: factories/callbacks only touch staged owners.
// To stage several documents, enclose all loads in one RestoreNamespace whose
// minimum covers every saved document; publish/adopt after the full slot reads.
void Store(CSave &file, const Registry &registry,
           const std::function<void(Writer &)> &root, Limits limits = {});
Graph Load(CSave &file, const Registry &registry,
           const std::function<void(Reader &)> &root, Limits limits = {});
} // namespace SaveSchema
