#include "addon_hook_chain.h"

#include <windows.h>

#include <cstring>

namespace owned {
namespace {

bool readable_copy(void* destination, const void* source, std::size_t bytes) noexcept {
    if (!source || !bytes) return false;
    MEMORY_BASIC_INFORMATION memory{};
    const auto* first = static_cast<const std::uint8_t*>(source);
    for (const std::uint8_t* at = first; at < first + bytes;) {
        if (!VirtualQuery(at, &memory, sizeof(memory)) || memory.State != MEM_COMMIT ||
            (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !memory.Protect)
            return false;
        at = static_cast<const std::uint8_t*>(memory.BaseAddress) + memory.RegionSize;
    }
    __try {
        std::memcpy(destination, source, bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

const std::uint8_t* relative_target(const std::uint8_t* at, const std::uint8_t* bytes) noexcept {
    std::int32_t relative = 0;
    std::memcpy(&relative, bytes + 1, sizeof(relative));
    return at + 5 + relative;
}

enum class Convention : std::uint8_t { stack, thiscall, fastcall };

// Parses one generated dispatcher. Returns its callback or nullptr.
const std::uint8_t* parse_dispatcher(const std::uint8_t* dispatcher,
                                     Convention& convention) noexcept {
    // Header, at most 16 copied stack words, two register pushes, call,
    // epilogue. Each read stays inside the bytes the builder emitted.
    std::uint8_t bytes[5 + 16 * 3 + 2 + 5 + 6 + 3]{};
    if (!readable_copy(bytes, dispatcher, 8) ||
        std::memcmp(bytes, "\x55\x89\xe5\x51\x52", 5) != 0)
        return nullptr;
    unsigned words = 0;
    if (bytes[5] == 0xff && bytes[6] == 0x75) {
        // Words are pushed from [ebp+4+4n] down to [ebp+8].
        if (bytes[7] % 4u || bytes[7] < 8u || bytes[7] > 4u + 16u * 4u) return nullptr;
        words = (bytes[7] - 4u) / 4u;
    }
    std::size_t at = 5 + 3u * words;
    if (!readable_copy(bytes, dispatcher, at + 2)) return nullptr;
    for (unsigned index = 0; index < words; ++index)
        if (bytes[5 + 3 * index] != 0xff || bytes[6 + 3 * index] != 0x75 ||
            bytes[7 + 3 * index] != 4u * (words + 1u - index))
            return nullptr;
    if (bytes[at] == 0x52 && bytes[at + 1] == 0x51) {
        convention = Convention::fastcall;
        at += 2;
    } else if (bytes[at] == 0x51) {
        convention = Convention::thiscall;
        at += 1;
    } else {
        convention = Convention::stack;
    }
    if (!readable_copy(bytes, dispatcher, at + 5 + 6 + 1) || bytes[at] != 0xe8)
        return nullptr;
    const std::uint8_t* callback = relative_target(dispatcher + at, bytes + at);
    at += 5;
    if (std::memcmp(bytes + at, "\x8d\x65\xf8\x5a\x59\xc9", 6) != 0) return nullptr;
    at += 6;
    if (bytes[at] == 0xc3) {
        // Caller-cleanup builder output exists only for stack conventions.
        if (convention != Convention::stack) return nullptr;
        return callback;
    }
    if (bytes[at] != 0xc2 || !readable_copy(bytes, dispatcher, at + 3) ||
        bytes[at + 1] != 4u * words || bytes[at + 2] != 0)
        return nullptr;
    return callback;
}

} // namespace

static OwnedEntryResolver g_owned_entry_resolver = nullptr;

void set_owned_entry_resolver(OwnedEntryResolver resolver) noexcept { g_owned_entry_resolver = resolver; }

bool addon_module_contains(const void* address) noexcept {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(address), &module))
        return false;
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return false;
    const wchar_t* name = wcsrchr(path, L'\\');
    return _wcsicmp(name ? name + 1 : path, L"addon.dll") == 0;
}

EntryResolution resolve_entry_owner(const std::uint8_t* entry,
                                    const std::uint8_t* stock,
                                    std::size_t stock_bytes,
                                    std::size_t hook_bytes,
                                    bool (*callback_owned)(const void*)) noexcept {
    EntryResolution result{};
    std::uint8_t current[32]{};
    if (!entry || !stock || hook_bytes < 5 || stock_bytes < hook_bytes ||
        stock_bytes > sizeof(current)) {
        result.reason = "invalid_entry_contract";
        return result;
    }
    if (!readable_copy(current, entry, stock_bytes)) {
        result.reason = "entry_unreadable";
        return result;
    }
    if (std::memcmp(current, stock, stock_bytes) == 0) {
        result.owner = EntryOwner::stock;
        result.reason = nullptr;
        return result;
    }
    if (current[0] != 0xe9) {
        result.reason = "unknown_entry_patch";
        return result;
    }
    const std::uint8_t* dispatcher = relative_target(entry, current);
    if (g_owned_entry_resolver && g_owned_entry_resolver(entry, dispatcher)) {
        result.owner = EntryOwner::framework_chain;
        result.dispatcher = dispatcher;
        result.addon_patch_bytes = 5;
        result.reason = nullptr;
        return result;
    }
    Convention convention = Convention::stack;
    const std::uint8_t* callback = parse_dispatcher(dispatcher, convention);
    if (!callback) {
        result.reason = "raw_replacement";
        return result;
    }
    if (!callback_owned || !callback_owned(callback)) {
        result.reason = "dispatcher_callback_outside_addon";
        return result;
    }
    if (reinterpret_cast<std::uintptr_t>(dispatcher) % 4u) {
        result.reason = "dispatcher_unaligned";
        return result;
    }
    static const std::uint8_t kThiscall[] = {0x59, 0x87, 0x0c, 0x24};
    static const std::uint8_t kFastcall[] = {0x5a, 0x87, 0x54, 0x24, 0x04, 0x59};
    const std::uint8_t* converter = convention == Convention::thiscall ? kThiscall :
        convention == Convention::fastcall ? kFastcall : nullptr;
    const std::size_t converter_bytes = convention == Convention::thiscall ? sizeof(kThiscall) :
        convention == Convention::fastcall ? sizeof(kFastcall) : 0;
    // The forwarder replays whole stock instructions covering >= 5 bytes and
    // jumps back to entry+len. Only one replay length can satisfy that jump.
    for (std::size_t length = 5; length <= stock_bytes; ++length) {
        const std::size_t body = converter_bytes + length + 5;
        const std::size_t padding = 4 - body % 4;
        const std::uint8_t* forwarder = dispatcher - padding - body;
        std::uint8_t bytes[48]{};
        if (body + padding > sizeof(bytes) ||
            !readable_copy(bytes, forwarder, body + padding))
            continue;
        if (bytes[converter_bytes + length] != 0xe9 ||
            relative_target(forwarder + converter_bytes + length,
                            bytes + converter_bytes + length) != entry + length)
            continue;
        if ((converter_bytes && std::memcmp(bytes, converter, converter_bytes) != 0) ||
            std::memcmp(bytes + converter_bytes, stock, length) != 0) {
            result.reason = "forwarder_replay_mismatch";
            return result;
        }
        for (std::size_t index = body; index < body + padding; ++index)
            if (bytes[index] != 0x90) {
                result.reason = "forwarder_padding_mismatch";
                return result;
            }
        for (std::size_t index = 5; index < length; ++index)
            if (current[index] != 0x90) {
                result.reason = "addon_patch_fill_mismatch";
                return result;
            }
        if (hook_bytes > length) {
            result.reason = "hook_longer_than_addon_patch";
            return result;
        }
        result.owner = EntryOwner::framework_chain;
        result.dispatcher = dispatcher;
        result.callback = callback;
        result.addon_patch_bytes = length;
        result.reason = nullptr;
        return result;
    }
    result.reason = "forwarder_not_found";
    return result;
}

} // namespace owned
