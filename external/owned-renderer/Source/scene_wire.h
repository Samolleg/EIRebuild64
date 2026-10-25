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
