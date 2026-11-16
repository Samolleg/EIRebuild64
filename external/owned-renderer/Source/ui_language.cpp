#include "ui_language.h"

#include "debug_overlay_config.h"
#include "graphics_settings.h"
#include "renderer_ui_ru.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstring>
#include <cwchar>
#include <string_view>
#include <unordered_map>

namespace owned {
namespace {

std::atomic<bool> g_russian{false};
std::atomic<unsigned> g_game_text_code_page{CP_ACP};

bool system_prefers_russian() noexcept {
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN || GetACP() == 1251;
}

// A '\0'-separated list ends at the first empty entry ("a\0b\0" + terminator).
std::string_view choice_list(const char* list) noexcept {
    const char* end = list;
    while (*end) end += std::strlen(end) + 1;
    return std::string_view(list, static_cast<std::size_t>(end - list));
}

using TextMap = std::unordered_map<std::string_view, const char*>;
using KeyMap = std::unordered_map<std::string_view, const ui_ru::KeyEntry*>;

const TextMap& text_map() {
    static const TextMap map = [] {
        TextMap result;
        for (const auto& entry : ui_ru::kText) result.emplace(entry.english, entry.russian);
        return result;
    }();
    return map;
}
const TextMap& choice_map() {
    static const TextMap map = [] {
        TextMap result;
        for (const auto& entry : ui_ru::kChoices) result.emplace(choice_list(entry.english), entry.russian);
        return result;
    }();
    return map;
}
template <std::size_t N>
KeyMap key_map(const ui_ru::KeyEntry (&entries)[N]) {
    KeyMap result;
    for (const auto& entry : entries) result.emplace(entry.key, &entry);
    return result;
}
const ui_ru::KeyEntry* option_entry(std::string_view key) {
    static const KeyMap map = key_map(ui_ru::kOptions);
    const auto found = map.find(key);
    return found == map.end() ? nullptr : found->second;
}
const ui_ru::KeyEntry* setting_entry(std::string_view key) {
    static const KeyMap map = key_map(ui_ru::kSettings);
    const auto found = map.find(key);
    return found == map.end() ? nullptr : found->second;
}

const char* lookup(const TextMap& map, std::string_view english, const char* fallback) {
    const auto found = map.find(english);
    return found == map.end() ? fallback : found->second;
}

} // namespace

void set_ui_language(int setting) noexcept {
    g_russian.store(setting == 2 || (setting != 1 && system_prefers_russian()), std::memory_order_relaxed);
}

bool ui_russian() noexcept { return g_russian.load(std::memory_order_relaxed); }

void read_game_text_code_page(const wchar_t* registry_root) noexcept {
    // The game applies the setting once per process; the first read with a
    // known registry root stays for the run, so font and layout caches keyed
    // without the code page cannot see it change.
    static bool latched = false;
    if (latched) return;
    unsigned code_page = CP_ACP;
    if (registry_root) {
        latched = true;
        wchar_t key[192]{};
        wchar_t name[64]{};
        DWORD size = sizeof(name);
        if (std::swprintf(key, sizeof(key) / sizeof(key[0]), L"%ls\\Game Specific Settings", registry_root) > 0 &&
            RegGetValueW(HKEY_CURRENT_USER, key, L"Font CharSet", RRF_RT_REG_SZ, nullptr, name, &size) == ERROR_SUCCESS &&
            _wcsicmp(name, L"russian") == 0)
            code_page = 1251;
    }
    g_game_text_code_page.store(code_page, std::memory_order_relaxed);
}

unsigned game_text_code_page() noexcept { return g_game_text_code_page.load(std::memory_order_relaxed); }

const char* ui_text(const char* english) noexcept {
    if (!english || !ui_russian()) return english;
    try { return lookup(text_map(), english, english); } catch (...) { return english; }
}

const char* ui_choices(const char* english) noexcept {
    if (!english || !ui_russian()) return english;
    try { return lookup(choice_map(), choice_list(english), english); } catch (...) { return english; }
}

// Developer window rows: the option's own translation, else the player
// table's wording for the same key, else English.
const char* ui_option_label(const RendererOption& option) noexcept {
    if (!ui_russian()) return option.label.c_str();
    try {
        if (const auto* entry = option_entry(option.key); entry && entry->label) return entry->label;
        if (const auto* entry = setting_entry(option.key); entry && entry->label) return entry->label;
        return lookup(text_map(), option.label, option.label.c_str());
    } catch (...) { return option.label.c_str(); }
}

const char* ui_option_description(const RendererOption& option) noexcept {
    if (!ui_russian()) return option.description.c_str();
    try {
        if (const auto* entry = option_entry(option.key); entry && entry->description) return entry->description;
        if (const auto* entry = setting_entry(option.key); entry && entry->description) return entry->description;
        return lookup(text_map(), option.description, option.description.c_str());
    } catch (...) { return option.description.c_str(); }
}

const char* ui_setting_label(const PlayerGraphicsSetting& setting) noexcept {
    if (!ui_russian()) return setting.label;
    try {
        if (const auto* entry = setting_entry(setting.key); entry && entry->label) return entry->label;
    } catch (...) {}
    return setting.label;
}

const char* ui_setting_tooltip(const PlayerGraphicsSetting& setting) noexcept {
    if (!ui_russian()) return setting.tooltip;
    try {
        if (const auto* entry = setting_entry(setting.key); entry && entry->description) return entry->description;
    } catch (...) {}
    return setting.tooltip;
}

} // namespace owned
