#pragma once
// Core-mode scene record codec (phase 2 step S3; PLAN.md 1.2).
//
// EI_RENDERER_X64_CORE=1: the x86 game process encodes each typed scene
// command (scene_commands_world.h, scene_commands_ui.h) into Op::scene ring
// records and the x64 host decodes it and runs it on its own Renderer. One
// codec serves both directions: a type's wire form is written once, as a
// field visit `template <class A> void fields(A&, T&)` found by ADL, and the
// same visit encodes (Out) and decodes (In).
//
// What may cross raw (memcpy) is restricted, because the two ends are
// different compilers' layouts (x86 and x64):
// - arithmetic types and enums (fixed width: never size_t; no pointers);
// - structs registered with SCENE_WIRE_POD. Each registration names the type
//   and its size in the connect-time layout check: x86 sends its table, the
//   host compares it with its own and loses the device naming the first type
//   whose size differs (a pointer or size_t member changes the size). A
//   struct holding pointers is never registered; it gets a fields() visit.
// Anything else fails to compile with "scene_wire: no wire form".
#include <emmintrin.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace scene_wire {

// ---- layout table -------------------------------------------------------
struct PodLayout {
    const char* name;
    std::uint32_t bytes;
};
// Every registered POD (static registration order; compared by name).
inline std::vector<PodLayout>& pod_layouts() {
    static std::vector<PodLayout> table;
    return table;
}
inline bool register_pod(const char* name, std::uint32_t bytes) {
    pod_layouts().push_back({name, bytes});
    return true;
}

template <class T> struct wire_pod : std::false_type {};

}  // namespace scene_wire

// At namespace scope, with the fully qualified type. Registers T as raw on
// the wire and in the layout check.
#define SCENE_WIRE_POD_NAMED(T, tag)                                                            \
    template <> struct scene_wire::wire_pod<T> : std::true_type {                               \
        static_assert(std::is_trivially_copyable_v<T>, "scene_wire: POD must be trivially copyable"); \
    };                                                                                          \
    namespace scene_wire { namespace registered { inline const bool tag = register_pod(#T, sizeof(T)); } }

namespace scene_wire {

// Encoder: appends to bytes.
struct Out {
    std::vector<std::uint8_t>& bytes;
    static constexpr bool decoding = false;
    void raw(const void* data, std::size_t size) {
        if (!size) return;
        const std::size_t at = bytes.size();
        bytes.resize(at + size);
        std::memcpy(bytes.data() + at, data, size);
    }
};

// Sizing pass: the encoded size of a value (Out's bytes without writing them).
struct Count {
    std::size_t bytes = 0;
    static constexpr bool decoding = false;
    void raw(const void*, std::size_t size) noexcept { bytes += size; }
};
// Encoder into memory the caller sized with Count (a ring record itself).
struct OutAt {
    std::uint8_t* at;
    static constexpr bool decoding = false;
    void raw(const void* data, std::size_t size) noexcept {
        if (!size) return;
        std::memcpy(at, data, size);
        at += size;
    }
};

// 64-bit content hash of a byte stream (core-mode stream deltas: "has the
// host these exact bytes"). SSE2, 64-byte stripes in the manner of XXH3's
// accumulate: per 64-bit lane acc += lo32(d^k) * hi32(d^k) + swapped(d), with
// keys that change per stripe (so stripe order matters); the lanes are mixed
// once at the end with the length. Not keyed against adversaries: it tells
// identical game data from changed game data.
class Hasher {
public:
    Hasher() noexcept {
        for (auto& lane : acc_) lane = _mm_setzero_si128();
    }
    void update(const void* data, std::size_t size) noexcept {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        length_ += size;
        if (buffered_) {
            const std::size_t take = (std::min)(size, std::size_t(64) - buffered_);
            std::memcpy(buffer_ + buffered_, bytes, take);
            buffered_ += take; bytes += take; size -= take;
            if (buffered_ < 64) return;
            stripe(buffer_);
            buffered_ = 0;
        }
        for (; size >= 64; bytes += 64, size -= 64) stripe(bytes);
        if (size) { std::memcpy(buffer_, bytes, size); buffered_ = size; }
    }
    std::uint64_t finish() noexcept {
        if (buffered_) {
            std::memset(buffer_ + buffered_, 0, 64 - buffered_);
            stripe(buffer_);
            buffered_ = 0;
        }
        alignas(16) std::uint64_t lanes[8];
        for (int i = 0; i < 4; ++i) _mm_store_si128(reinterpret_cast<__m128i*>(lanes + 2 * i), acc_[i]);
        std::uint64_t hash = 0x9E3779B97F4A7C15ull ^ (std::uint64_t(length_) * 0xC2B2AE3D27D4EB4Full);
        for (const std::uint64_t lane : lanes) {
            hash ^= lane + 0x165667B19E3779F9ull + (hash << 6) + (hash >> 2);
            hash ^= hash >> 33; hash *= 0xFF51AFD7ED558CCDull; hash ^= hash >> 33;
        }
        hash ^= hash >> 29; hash *= 0xBF58476D1CE4E5B9ull; hash ^= hash >> 32;
        return hash ? hash : 1;
    }

private:
    void stripe(const std::uint8_t* p) noexcept {
        const __m128i step = _mm_set1_epi32(static_cast<int>(stripes_++ * 0x9E3779B1u));
        static const std::uint32_t keys[16] = {0xb8fe6c39u, 0x23a44bbeu, 0x7c01812cu, 0xf721ad1cu, 0xded46de9u, 0x839097dbu,
            0x7240a4a4u, 0xb7b3671fu, 0xcb79e64eu, 0xccc0e578u, 0x825ad07du, 0xccff7221u, 0xb8084674u, 0xf743248eu,
            0xe03590e6u, 0x81a20102u};
        for (int i = 0; i < 4; ++i) {
            const __m128i d = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + 16 * i));
            const __m128i k = _mm_add_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i*>(keys + 4 * i)), step);
            const __m128i dk = _mm_xor_si128(d, k);
            const __m128i product = _mm_mul_epu32(dk, _mm_shuffle_epi32(dk, _MM_SHUFFLE(0, 3, 0, 1)));
            acc_[i] = _mm_add_epi64(acc_[i], _mm_add_epi64(product, _mm_shuffle_epi32(d, _MM_SHUFFLE(1, 0, 3, 2))));
        }
    }
    __m128i acc_[4];
    std::uint8_t buffer_[64];
    std::size_t buffered_ = 0;
    std::uint64_t length_ = 0;
    std::uint32_t stripes_ = 0;
};
// The hash of a value's wire bytes (no bytes are produced).
struct HashOut {
    Hasher hasher;
    static constexpr bool decoding = false;
    void raw(const void* data, std::size_t size) noexcept { hasher.update(data, size); }
};

// Decoder over one whole message. A short or oversized read sets failed and
// leaves the destination value-initialised; the caller checks ok() once.
struct In {
    const std::uint8_t* at;
    const std::uint8_t* end;
    bool failed = false;
    static constexpr bool decoding = true;
    void raw(void* data, std::size_t size) {
        if (!size) return;
        if (failed || static_cast<std::size_t>(end - at) < size) {
            failed = true;
            std::memset(data, 0, size);
            return;
        }
        std::memcpy(data, at, size);
        at += size;
    }
    // A declared element count that cannot fit the rest of the message.
    bool plausible(std::uint32_t count, std::size_t element_bytes) {
        if (failed || std::uint64_t(count) * (element_bytes ? element_bytes : 1) > std::uint64_t(end - at)) {
            failed = true;
            return false;
        }
        return true;
    }
    bool ok() const noexcept { return !failed && at == end; }
};

template <class T> struct is_vector : std::false_type {};
template <class T, class Al> struct is_vector<std::vector<T, Al>> : std::true_type {};
template <class T> struct is_std_array : std::false_type {};
template <class T, std::size_t N> struct is_std_array<std::array<T, N>> : std::true_type {};

namespace detail {
template <class A, class T, class = void> struct has_fields : std::false_type {};
template <class A, class T>
struct has_fields<A, T, std::void_t<decltype(fields(std::declval<A&>(), std::declval<T&>()))>> : std::true_type {};
template <class T> struct dependent_false : std::false_type {};
}  // namespace detail

// Arithmetic and enum scalars. A size_t or ptrdiff_t field cannot be told
// apart from unsigned/int on x86 (same type there): it differs in size
// between the builds, so the host decode fails its exact-size check and
// names the command. Wire structs use fixed-width members instead.
template <class T> constexpr bool raw_scalar_v =
    (std::is_arithmetic_v<T> || std::is_enum_v<T>) && !std::is_same_v<T, long double>;

template <class T> constexpr bool raw_v = raw_scalar_v<T> || wire_pod<T>::value;

template <class A, class T> void field(A& a, T& value);

template <class A, class T> void raw_field(A& a, T& value) { a.raw(&value, sizeof(T)); }

template <class A> void field(A& a, std::string& value) {
    std::uint32_t count = static_cast<std::uint32_t>(value.size());
    raw_field(a, count);
    if constexpr (A::decoding) {
        if (!a.plausible(count, 1)) { value.clear(); return; }
        value.resize(count);
    }
    if (count) a.raw(value.data(), count);
}

// Wide paths (wchar_t is 2 bytes in both Windows builds).
template <class A> void field(A& a, std::wstring& value) {
    std::uint32_t count = static_cast<std::uint32_t>(value.size());
    raw_field(a, count);
    if constexpr (A::decoding) {
        if (!a.plausible(count, sizeof(wchar_t))) { value.clear(); return; }
        value.resize(count);
    }
    if (count) a.raw(value.data(), count * sizeof(wchar_t));
}

template <class A, class T> void field(A& a, T& value) {
    if constexpr (detail::has_fields<A, T>::value) {
        fields(a, value);
    } else if constexpr (std::is_same_v<T, bool>) {
        std::uint8_t byte = value ? 1 : 0;
        raw_field(a, byte);
        if constexpr (A::decoding) value = byte != 0;
    } else if constexpr (raw_v<T>) {
        raw_field(a, value);
    } else if constexpr (is_vector<T>::value) {
        using E = typename T::value_type;
        std::uint32_t count = static_cast<std::uint32_t>(value.size());
        raw_field(a, count);
        if constexpr (A::decoding) {
            if (!a.plausible(count, raw_v<E> && !std::is_same_v<E, bool> ? sizeof(E) : 1)) { value.clear(); return; }
            value.resize(count);
        }
        if constexpr (raw_v<E> && !std::is_same_v<E, bool>) {
            if (count) a.raw(value.data(), sizeof(E) * count);
        } else {
            static_assert(!std::is_same_v<E, bool>, "scene_wire: std::vector<bool> has no wire form");
            for (auto& element : value) field(a, element);
        }
    } else if constexpr (is_std_array<T>::value) {
        using E = typename T::value_type;
        if constexpr (raw_v<E> && !std::is_same_v<E, bool>) a.raw(value.data(), sizeof(value));
        else for (auto& element : value) field(a, element);
    } else if constexpr (std::is_array_v<T>) {
        for (auto& element : value) field(a, element);
    } else {
        static_assert(detail::dependent_false<T>::value, "scene_wire: no wire form (add fields() or SCENE_WIRE_POD)");
    }
}

// A static diagnostic string (a const char* field naming a reason): the text
// crosses; the decoder points the field at its own interned copy, which
// lives as long as the process (the set of such strings is small and fixed).
inline const char* intern(const std::string& text) {
    static std::vector<std::string*> table;  // never freed: fields keep pointing into it
    for (const std::string* entry : table) if (*entry == text) return entry->c_str();
    table.push_back(new std::string(text));
    return table.back()->c_str();
}
template <class A> void static_text(A& a, const char*& value) {
    std::uint8_t present = value ? 1 : 0;
    raw_field(a, present);
    if (!present) { value = nullptr; return; }
    std::string text = A::decoding ? std::string() : std::string(value);
    field(a, text);
    if constexpr (A::decoding) value = intern(text);
}

// Several fields in order: a.f(x, y, z).
template <class A, class... T> void fields_of(A& a, T&... values) { (field(a, values), ...); }

template <class T> std::vector<std::uint8_t> encode(const T& value) {
    std::vector<std::uint8_t> bytes;
    Out out{bytes};
    field(out, const_cast<T&>(value));  // Out never writes the value
    return bytes;
}
template <class T> void encode_into(std::vector<std::uint8_t>& bytes, const T& value) {
    Out out{bytes};
    field(out, const_cast<T&>(value));
}
template <class T> bool decode(const std::uint8_t* data, std::size_t size, T& value) {
    In in{data, data + size};
    field(in, value);
    return in.ok();
}

// ---- layout check ------------------------------------------------------
// x86 -> host at connect: "name\0" u32 bytes, for every registered POD.
inline std::vector<std::uint8_t> encode_layouts() {
    std::vector<std::uint8_t> bytes;
    for (const auto& entry : pod_layouts()) {
        const std::size_t length = std::strlen(entry.name) + 1;
        const std::size_t at = bytes.size();
        bytes.resize(at + length + sizeof(std::uint32_t));
        std::memcpy(bytes.data() + at, entry.name, length);
        std::memcpy(bytes.data() + at + length, &entry.bytes, sizeof(std::uint32_t));
    }
    return bytes;
}
// Host: compares the game process's table with its own. Returns false and
// names the first mismatch in `problem`.
inline bool check_layouts(const std::uint8_t* data, std::size_t size, std::string& problem) {
    std::size_t at = 0, matched = 0;
    while (at < size) {
        const void* nul = std::memchr(data + at, 0, size - at);
        if (!nul) { problem = "layout table truncated"; return false; }
        const std::string name(reinterpret_cast<const char*>(data + at));
        at += name.size() + 1;
        if (size - at < sizeof(std::uint32_t)) { problem = "layout table truncated"; return false; }
        std::uint32_t bytes = 0;
        std::memcpy(&bytes, data + at, sizeof(bytes));
        at += sizeof(bytes);
        const PodLayout* own = nullptr;
        for (const auto& entry : pod_layouts()) if (name == entry.name) { own = &entry; break; }
        if (!own) { problem = "host does not know wire type " + name; return false; }
        if (own->bytes != bytes) {
            problem = "wire type " + name + " is " + std::to_string(bytes) + " bytes in the game, " +
                      std::to_string(own->bytes) + " in the host";
            return false;
        }
        ++matched;
    }
    if (matched != pod_layouts().size()) { problem = "game sent fewer wire types than the host has"; return false; }
    return true;
}

}  // namespace scene_wire
