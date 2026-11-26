#pragma once

#include <cstddef>
#include <cstdint>

namespace owned {

// Ownership of one game function entry that the bridge wants to detour.
//
// The supported addon installs its runtime function hooks through one
// builder: the entry becomes `jmp dispatcher` (plus NOP fill up to the
// replayed instruction boundary). The dispatcher is a generated wrapper
//   55 89 E5 51 52 {FF 75 i} [52] [51] E8 callback 8D 65 F8 5A 59 C9 C2 n 00
// emitted directly after its forwarder, which is a calling-convention
// converter followed by the replayed stock prologue and `jmp entry+len`,
// padded with NOPs to a 4-byte boundary. Calling the dispatcher is exactly
// what a game caller of the hooked entry does, so a detour that finds this
// chain forwards to the dispatcher and keeps the addon's behaviour.
// Any other foreign bytes (a raw replacement jump, a code-hook wrapper, an
// unknown patch) are rejected with a named reason.
enum class EntryOwner : std::uint8_t { stock, framework_chain, rejected };

struct EntryResolution {
    EntryOwner owner = EntryOwner::rejected;
    const std::uint8_t* dispatcher = nullptr;  // framework_chain only
    const std::uint8_t* callback = nullptr;    // framework_chain only
    std::size_t addon_patch_bytes = 0;         // framework_chain only
    const char* reason = "not_resolved";
};

// The renderer's own entry wrappers (the owned widescreen's anchored widget
// methods) are callable exactly like the function they wrap; a detour that
// finds `jmp wrapper` at an entry forwards to the wrapper as it does to an
// addon dispatcher. The resolver accepts only the wrapper installed at that entry.
using OwnedEntryResolver = const void* (*)(const void* entry, const void* jump_target) noexcept;
void set_owned_entry_resolver(OwnedEntryResolver resolver) noexcept;

// True when `address` lies in a loaded module named addon.dll.
bool addon_module_contains(const void* address) noexcept;

// A raw replacement: `jmp target` at `entry` into a loaded module that is
// neither the game image nor this renderer. Only an observe-only detour (it
// reads the call and continues it unchanged) may forward there, exactly as
// a game caller of the entry does; the module keeps its behaviour. Returns
// the target and copies the module's file name, or nullptr.
const std::uint8_t* raw_replacement_target(const std::uint8_t* entry, wchar_t* module_name,
                                           std::size_t name_chars) noexcept;

// Observe-only entries: the bridge's audited list of detours that only read
// the call and continue it unchanged. resolve_entry_owner resolves a raw
// replacement at such an entry as a chain (dispatcher = the module's jump
// target, 5 patched bytes, no callback), so every install path forwards to
// it; the check is told the target and module so it can log once.
using ObserverEntryCheck = bool (*)(const void* entry, const void* target, const wchar_t* module) noexcept;
void set_observer_entry_check(ObserverEntryCheck check) noexcept;

// `stock` holds the complete expected stock bytes at `entry` (at least
// `hook_bytes`, and at least as many as the addon replays). `callback_owned`
// decides whether a dispatcher's callback belongs to the addon module.
EntryResolution resolve_entry_owner(const std::uint8_t* entry,
                                    const std::uint8_t* stock,
                                    std::size_t stock_bytes,
                                    std::size_t hook_bytes,
                                    bool (*callback_owned)(const void*)) noexcept;

// A raw detour from another module can sit on top of an addon chain: it kept
// the addon's `jmp dispatcher` as its continuation and calls it through a
// pointer. Returns the dispatcher stored at `slot` when it is the addon's
// generated dispatcher for `entry` (its forwarder replays `stock` and returns
// to the entry), and its callback; nullptr otherwise. The entry bytes are the
// other module's and are not checked.
const std::uint8_t* layered_framework_dispatcher(const std::uint8_t* entry,
                                                 const std::uint8_t* const* slot,
                                                 const std::uint8_t* stock,
                                                 std::size_t stock_bytes,
                                                 bool (*callback_owned)(const void*),
                                                 const std::uint8_t** callback) noexcept;

} // namespace owned
