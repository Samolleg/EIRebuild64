#pragma once
#include <cstdint>

// Lexical tool ownership; not a setting or a gameplay feature switch.
namespace ModCliTool {
enum class Kind { None, Compiler, Graph };
inline Kind& CurrentKind() {
    static thread_local Kind kind = Kind::None;
    return kind;
}
inline bool Active() { return CurrentKind() != Kind::None; }
inline bool GeneratingGraph() { return CurrentKind() == Kind::Graph; }
// Original 0x7A9B9C is zero-filled process storage, shared by all error overloads.
// Scope changes do not reset it. Unsigned increment preserves x86 wraparound.
inline std::uint32_t& ErrorCount() {
    static std::uint32_t count = 0;
    return count;
}
inline bool StopAfterError(bool plainFormat, bool antiCrash) {
    const std::uint32_t count = ++ErrorCount();
    if (!GeneratingGraph()) return false; // Ordinary/other tool fatal policy untouched.
    // Native AntiCrash removes 4800C9..4800D1 only, not the HRESULT overload exits.
    if (plainFormat && antiCrash) return false;
    const std::uint32_t limit = plainFormat ? 127u : 1u;
    // Native CMP/JL is signed after INC and stores the incremented value first.
    return count < 0x80000000u && count >= limit;
}
class Scope {
    Kind previous;
public:
    explicit Scope(Kind kind) : previous(CurrentKind()) { CurrentKind() = kind; }
    ~Scope() { CurrentKind() = previous; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
}
