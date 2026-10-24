#pragma once
#include "Basic.h"
#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <vector>

class CSave;

// Dynamic saves only. Asset files continue to use CStructureSaver.
// No auto detection/fallback and no calls to Serialize(CStructureSaver&).
namespace SaveSchema {
using Bytes = std::vector<std::uint8_t>;
constexpr std::uint32_t Version = 1;
constexpr std::uint32_t HeaderBytes = 32;
// Wire (all integers LE): magic[8], u32 version, u32 headerBytes,
// u64 payloadBytes, u32 objectCount, u32 rootBytes. Then the COMPLETE
// registry [u32 typeId, u64 ObjectId, u32 bodyBytes] in ObjectId order,
// rootBytes of root fields, then each body in registry order.
// Reference = u64; string/blob = u32 byte length + bytes; Count = u32.
// Bool = 0/1 u8; signed = two's complement; floats = IEEE-754 bits LE.

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
    Error(ErrorCode code, const char *message) : std::runtime_error(message), code(code) {}
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

class Registry
{
    std::map<std::uint32_t, Type> types;
    std::map<std::type_index, std::uint32_t> ids;
public:
    void Register(std::uint32_t id, const std::type_info &nativeType, Type type);
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
    void Int32(std::int32_t value) { UInt32(static_cast<std::uint32_t>(value)); }
    void Int64(std::int64_t value) { UInt64(static_cast<std::uint64_t>(value)); }
    void Bool(bool value) { UInt8(value ? 1 : 0); }
    void Float32(float value);
    void Float64(double value);
    void Count(std::uint32_t count);
    void String(const std::string &value); // bytes in the consumer's declared encoding
    void Blob(const Bytes &value); // encoded bytes, never a native object image
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
    std::int32_t Int32();
    std::int64_t Int64();
    bool Bool();
    float Float32();
    double Float64();
    std::uint32_t Count();
    std::string String();
    Bytes Blob();
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
    std::map<ObjectId, CPtr<CFundament>> objects;
    friend struct ReadState;
    friend Graph Load(CSave &, const Registry &, const std::function<void(Reader &)> &, Limits);
public:
    Graph() = default;
    Graph(const Graph &) = delete;
    Graph &operator=(const Graph &) = delete;
    Graph(Graph &&) = default;
    Graph &operator=(Graph &&) = default;
    CFundament *Find(ObjectId id) const;
};

// CSave's current stream abstraction is uint32-sized. The format's payload length
// is uint64, but this implementation rejects documents exceeding Limits/CSave.
// Exclusive world restore: no concurrent creation or publication in callbacks.
void Store(CSave &file, const Registry &registry,
           const std::function<void(Writer &)> &root, Limits limits = {});
Graph Load(CSave &file, const Registry &registry,
           const std::function<void(Reader &)> &root, Limits limits = {});
} // namespace SaveSchema
