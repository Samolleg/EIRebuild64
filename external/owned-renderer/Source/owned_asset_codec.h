#pragma once
// Wire codec for the retained-scene publication stream (retained_publication.h):
// the byte primitives both sides share, and the decoded immutable assets the
// retained scene references (assembly, figure, model, texture, material,
// completed surface, animation database and clip, map snapshot with its
// terrain, and shared byte blobs).
//
// Identity: every x86 asset object gets one wire id, assigned when it is
// first referenced and validated by a weak_ptr (an expired object never
// aliases a new one at the same address). Its definition is encoded once,
// dependencies first, before any record names the id; a release record
// follows when the x86 object has died. Raw addresses never cross the wire,
// only the assigned ids. The scene compares some assets by pointer
// (VisualPartAsset::texture against a replacement), so the host decodes one
// object per id, never one per content hash.
//
// Not carried: terrain texture pages (store-backed, lazily filled and
// released by the render thread) cross as their count; a core-mode host
// resolves them from its own store (set_terrain_page_resolver). Lazily
// derived terrain caches (procedural falls, liquid outflow
// field), neighbour-seam outside liquid, budget reservations
// (byte_reservation) and assembly source_backings (x86 store keep-alives).
#include "owned_asset_loader.h"
#include "owned_visual_evaluation.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace owned {

struct VisualAssemblyAsset;

namespace codec {

// ---- byte primitives ------------------------------------------------------

// Output: a local buffer, or a stream that hands each full fragment to a sink
// (large assets never build one whole transient copy in the game process).
class Out {
public:
    using Sink = void (*)(void* context, const std::uint8_t* data, std::size_t bytes, bool last) noexcept;
    Out() = default;
    Out(Sink sink, void* context, std::size_t fragment) : sink_(sink), context_(context), fragment_(fragment) {
        buffer.reserve(fragment < 4096 ? fragment : 4096);
    }
    void bytes(const void* data, std::size_t count) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        if (sink_ && buffer.size() + count > fragment_) {
            while (count) {
                const std::size_t room = fragment_ - buffer.size();
                const std::size_t part = count < room ? count : room;
                buffer.insert(buffer.end(), p, p + part);
                p += part; count -= part;
                if (buffer.size() == fragment_) { sink_(context_, buffer.data(), buffer.size(), false); buffer.clear(); }
            }
            return;
        }
        buffer.insert(buffer.end(), p, p + count);
    }
    // Stream mode: sends the final fragment.
    void finish() noexcept { if (sink_) { sink_(context_, buffer.data(), buffer.size(), true); buffer.clear(); } }
    std::vector<std::uint8_t> buffer;
private:
    Sink sink_ = nullptr;
    void* context_ = nullptr;
    std::size_t fragment_ = 0;
};

class In {
public:
    In(const std::uint8_t* data, std::size_t size) : at_(data), end_(data + size) {}
    bool bytes(void* data, std::size_t count) {
        if (!ok_ || std::size_t(end_ - at_) < count) return ok_ = false;
        std::memcpy(data, at_, count);
        at_ += count;
        return true;
    }
    // A count of `element`-byte items that must still fit in the input.
    std::size_t count(std::size_t element) {
        std::uint32_t value = 0;
        if (!bytes(&value, 4)) return 0;
        if (element && value > std::size_t(end_ - at_) / element) { ok_ = false; return 0; }
        return value;
    }
    bool ok() const noexcept { return ok_; }
    bool done() const noexcept { return ok_ && at_ == end_; }
    void fail() noexcept { ok_ = false; }
private:
    const std::uint8_t* at_;
    const std::uint8_t* end_;
    bool ok_ = true;
};

// Plain values copied as bytes: scalars, and PODs made only of fixed-width
// fields (identical layout in the x86 DLL and the x64 host; no pointer,
// size_t or bool-before-double padding difference).
template<class T> struct raw : std::bool_constant<std::is_arithmetic_v<T> || std::is_enum_v<T>> {};
template<class T, std::size_t N> struct raw<std::array<T, N>> : raw<T> {};
#define OWNED_CODEC_RAW(Type) template<> struct raw<Type> : std::true_type {}; \
    static_assert(std::is_trivially_copyable_v<Type>, #Type)
OWNED_CODEC_RAW(NativePackedGroup);
OWNED_CODEC_RAW(NativeTextureCoordinate);
OWNED_CODEC_RAW(NativeVertexComponent);
OWNED_CODEC_RAW(NativeTerrainState);
OWNED_CODEC_RAW(FigureMorphComponent);
OWNED_CODEC_RAW(FigureBounds);
OWNED_CODEC_RAW(MmpMipLevel);
OWNED_CODEC_RAW(TerrainVertex);
OWNED_CODEC_RAW(TerrainBounds);
OWNED_CODEC_RAW(TerrainObjectDescriptor);
OWNED_CODEC_RAW(TerrainTileDescriptor);
OWNED_CODEC_RAW(TerrainTileRangeDescriptor::Envelope);
OWNED_CODEC_RAW(DDPIXELFORMAT);
OWNED_CODEC_RAW(DDCOLORKEY);
OWNED_CODEC_RAW(VisualWaypoint);
OWNED_CODEC_RAW(VisualBridgePlane);
#undef OWNED_CODEC_RAW
// Members of at most four bytes only: one layout in both builds.
static_assert(sizeof(NativeVertexComponent) == 6 && sizeof(TerrainVertex) == 32 &&
              sizeof(VisualBridgePlane) == 76 && sizeof(VisualWaypoint) == 20, "wire layout");

template<class T> std::enable_if_t<raw<T>::value> io(Out& out, const T& value) { out.bytes(&value, sizeof(T)); }
template<class T> std::enable_if_t<raw<T>::value> io(In& in, T& value) { in.bytes(&value, sizeof(T)); }

inline void io_size(Out& out, std::size_t value) { const std::uint64_t v = value; io(out, v); }
inline void io_size(In& in, std::size_t& value) {
    std::uint64_t v = 0; io(in, v);
    if (v > static_cast<std::uint64_t>(static_cast<std::size_t>(-1))) in.fail();
    value = static_cast<std::size_t>(v);
}
inline void io_count(Out& out, std::size_t value) { const auto v = static_cast<std::uint32_t>(value); io(out, v); }

inline void io(Out& out, const std::string& value) { io_count(out, value.size()); out.bytes(value.data(), value.size()); }
inline void io(In& in, std::string& value) {
    const auto n = in.count(1);
    value.resize(n);
    if (n) in.bytes(value.data(), n);
}

template<class T> void io(Out& out, const std::vector<T>& value) {
    io_count(out, value.size());
    if constexpr (raw<T>::value) { if (!value.empty()) out.bytes(value.data(), value.size() * sizeof(T)); }
    else for (const auto& item : value) io(out, item);
}
template<class T> void io(In& in, std::vector<T>& value) {
    const auto n = in.count(raw<T>::value ? sizeof(T) : 1);
    value.clear();
    value.resize(n);
    if constexpr (raw<T>::value) { if (n) in.bytes(value.data(), n * sizeof(T)); }
    else for (auto& item : value) { io(in, item); if (!in.ok()) return; }
}
inline void io(Out& out, const std::vector<bool>& value) {
    io_count(out, value.size());
    for (const bool item : value) io(out, item);
}
template<class T, std::size_t N> std::enable_if_t<!raw<T>::value> io(Out& out, const std::array<T, N>& value) {
    for (const auto& item : value) io(out, item);
}
template<class T, std::size_t N> std::enable_if_t<!raw<T>::value> io(In& in, std::array<T, N>& value) {
    for (auto& item : value) io(in, item);
}

// One field list per struct, used for both directions.
#define OWNED_CODEC_FIELDS(Type, ...) \
    inline void io(::owned::codec::Out& s, const Type& v) { __VA_ARGS__ } \
    inline void io(::owned::codec::In& s, Type& v) { __VA_ARGS__ }

OWNED_CODEC_FIELDS(ResourceIdentity,
    io(s, v.canonical_name); io(s, v.container_incarnation); io(s, v.container_file_key);
    io(s, v.absolute_offset); io(s, v.bytes); io(s, v.mount_ordinal); io(s, v.entry_index); io(s, v.loose_file);)
OWNED_CODEC_FIELDS(LifecycleIdentity, io(s, v.id); io(s, v.generation); io(s, v.positive_world_epoch);)

// ---- assets ---------------------------------------------------------------

enum class AssetKind : std::uint8_t {
    blob = 1, figure, model, texture, material, surface, database, clip, assembly, terrain, map, count
};
const char* asset_kind_name(AssetKind kind) noexcept;

template<class T> struct asset_kind;
template<> struct asset_kind<std::vector<std::uint8_t>> { static constexpr AssetKind value = AssetKind::blob; };
template<> struct asset_kind<FigureCpuAsset> { static constexpr AssetKind value = AssetKind::figure; };
template<> struct asset_kind<ModelCpuAsset> { static constexpr AssetKind value = AssetKind::model; };
template<> struct asset_kind<TextureCpuAsset> { static constexpr AssetKind value = AssetKind::texture; };
template<> struct asset_kind<MaterialCpuAsset> { static constexpr AssetKind value = AssetKind::material; };
template<> struct asset_kind<OwnedVisualSurface> { static constexpr AssetKind value = AssetKind::surface; };
template<> struct asset_kind<AnimationDatabaseAsset> { static constexpr AssetKind value = AssetKind::database; };
template<> struct asset_kind<AnimationClipAsset> { static constexpr AssetKind value = AssetKind::clip; };
template<> struct asset_kind<VisualAssemblyAsset> { static constexpr AssetKind value = AssetKind::assembly; };
template<> struct asset_kind<TerrainCpuAsset> { static constexpr AssetKind value = AssetKind::terrain; };
template<> struct asset_kind<OwnedMapSnapshot> { static constexpr AssetKind value = AssetKind::map; };

// Message tags shared with retained_publication.h (first byte of a message).
constexpr std::uint8_t kAssetDefine = 1;
constexpr std::uint8_t kAssetRelease = 2;

// Game-process side: ids, definitions and releases. Not thread safe: the
// publication recorder serializes every call under its own lock.
class AssetEncoder {
public:
    // Emits one complete message: the stream-mode Out it is given carries
    // the definition, finished by the encoder.
    using MessageSink = Out::Sink;
    AssetEncoder(MessageSink sink, void* context, std::size_t fragment) noexcept
        : sink_(sink), context_(context), fragment_(fragment) {}

    // The object's id, defining it (and every asset it references) first.
    // 0 for null.
    template<class T> std::uint64_t id(const std::shared_ptr<const T>& value) {
        if (!value) return 0;
        constexpr AssetKind kind = asset_kind<T>::value;
        if (const auto known = lookup(value.get(), kind)) return known;
        dependencies(*value);
        const std::uint64_t assigned = ++next_id_;
        Out out(sink_, context_, fragment_);
        io(out, kAssetDefine);
        io(out, kind);
        io(out, assigned);
        body(out, *value);
        out.finish();
        objects_[{value.get(), kind}] = {assigned, kind, std::weak_ptr<const void>(value)};
        return assigned;
    }
    template<class T> std::uint64_t id(const std::shared_ptr<T>& value) {
        return id(std::shared_ptr<const T>(value));
    }
    // An id already defined (inside a definition, after dependencies()).
    std::uint64_t known(const void* address, AssetKind kind) const noexcept;
    // Emits one release message for every object that died since the last
    // sweep. Returns the number released.
    std::size_t sweep();
    std::size_t live() const noexcept { return objects_.size(); }
    // The id of a live object, without defining it (0: never defined).
    std::uint64_t key_of(const void* address) const noexcept;

private:
    struct Entry {
        std::uint64_t id = 0;
        AssetKind kind{};
        std::weak_ptr<const void> alive;
    };
    std::uint64_t lookup(const void* address, AssetKind kind);
    void release(std::uint64_t id);

    void dependencies(const std::vector<std::uint8_t>&) {}
    void dependencies(const FigureCpuAsset&) {}
    void dependencies(const ModelCpuAsset&) {}
    void dependencies(const TextureCpuAsset& value);
    void dependencies(const MaterialCpuAsset&) {}
    void dependencies(const OwnedVisualSurface& value);
    void dependencies(const AnimationDatabaseAsset&) {}
    void dependencies(const AnimationClipAsset&) {}
    void dependencies(const VisualAssemblyAsset& value);
    void dependencies(const TerrainCpuAsset&) {}
    void dependencies(const OwnedMapSnapshot& value);

    void body(Out& out, const std::vector<std::uint8_t>& value);
    void body(Out& out, const FigureCpuAsset& value);
    void body(Out& out, const ModelCpuAsset& value);
    void body(Out& out, const TextureCpuAsset& value);
    void body(Out& out, const MaterialCpuAsset& value);
    void body(Out& out, const OwnedVisualSurface& value);
    void body(Out& out, const AnimationDatabaseAsset& value);
    void body(Out& out, const AnimationClipAsset& value);
    void body(Out& out, const VisualAssemblyAsset& value);
    void body(Out& out, const TerrainCpuAsset& value);
    void body(Out& out, const OwnedMapSnapshot& value);
    template<class T> void ref(Out& out, const std::shared_ptr<const T>& value) const {
        const std::uint64_t key = value ? known(value.get(), asset_kind<T>::value) : 0;
        io(out, key);
    }
    template<class T> void ref(Out& out, const std::shared_ptr<T>& value) const {
        ref(out, std::shared_ptr<const T>(value));
    }

    MessageSink sink_;
    void* context_;
    std::size_t fragment_;
    std::uint64_t next_id_ = 0;
    // Keyed by address and kind: an aliasing pointer to an object's first
    // member never takes over the object's id.
    struct ObjectKey {
        const void* address;
        AssetKind kind;
        bool operator==(const ObjectKey& other) const noexcept { return address == other.address && kind == other.kind; }
    };
    struct ObjectKeyHash {
        std::size_t operator()(const ObjectKey& key) const noexcept {
            return std::hash<const void*>()(key.address) ^ (static_cast<std::size_t>(key.kind) * 0x9e3779b9u);
        }
    };
    std::unordered_map<ObjectKey, Entry, ObjectKeyHash> objects_;
};

// Host side: one decoded object per id. A definition naming an unknown or
// wrong-kind id fails (the stream is then out of sync).
class AssetDecoder {
public:
    // Reads the body of a kAssetDefine message (after the tag).
    bool define(In& in, std::string& error);
    // Reads the body of a kAssetRelease message (after the tag).
    bool release(In& in, std::string& error);
    template<class T> bool get(std::uint64_t id, std::shared_ptr<const T>& value) const {
        value.reset();
        if (!id) return true;
        const auto found = objects_.find(id);
        if (found == objects_.end() || found->second.kind != asset_kind<T>::value) return false;
        value = std::static_pointer_cast<const T>(found->second.object);
        return true;
    }
    std::size_t live() const noexcept { return objects_.size(); }
    std::uint64_t key_of(const void* address) const noexcept;

private:
    struct Entry {
        AssetKind kind{};
        std::shared_ptr<const void> object;
    };
    template<class T> bool ref(In& in, std::shared_ptr<const T>& value) {
        std::uint64_t key = 0;
        io(in, key);
        if (!in.ok() || !get(key, value)) { in.fail(); return false; }
        return true;
    }
    template<class T> bool decode(In& in, std::shared_ptr<const void>& output);
    bool body(In& in, std::vector<std::uint8_t>& value);
    bool body(In& in, FigureCpuAsset& value);
    bool body(In& in, ModelCpuAsset& value);
    bool body(In& in, TextureCpuAsset& value);
    bool body(In& in, MaterialCpuAsset& value);
    bool body(In& in, OwnedVisualSurface& value);
    bool body(In& in, AnimationDatabaseAsset& value);
    bool body(In& in, AnimationClipAsset& value);
    bool body(In& in, VisualAssemblyAsset& value);
    bool body(In& in, TerrainCpuAsset& value);
    bool body(In& in, OwnedMapSnapshot& value);

    std::unordered_map<std::uint64_t, Entry> objects_;
    std::unordered_map<const void*, std::uint64_t> keys_;
};

} // namespace codec
// Core-mode host: called for every decoded terrain asset to fill its texture
// pages (shipped mip chains) from the host's store; null elsewhere.
using TerrainPageResolver = void (*)(TerrainCpuAsset& terrain) noexcept;
void set_terrain_page_resolver(TerrainPageResolver resolver) noexcept;

} // namespace owned
