#include "debug_overlay_config.h"
#include "embedded_process_policy.h"
#ifdef EID3D11_EMBEDDED
#include "embedded_host.h"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string_view>

namespace owned {
namespace {

constexpr DWORD kMaximumConfigBytes = 1024u * 1024u;

bool valid_key(const char* key) {
    if (!key || !*key) return false;
    for (const unsigned char c : std::string_view(key)) {
        const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!valid) return false;
    }
    return true;
}

std::string trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
        value.remove_suffix(1);
    return std::string(value);
}

bool parse_boolean(const char* value, bool& result) {
    if (!value) return false;
    if (_stricmp(value, "true") == 0 || std::strcmp(value, "1") == 0 ||
        _stricmp(value, "on") == 0) {
        result = true;
        return true;
    }
    if (_stricmp(value, "false") == 0 || std::strcmp(value, "0") == 0 ||
        _stricmp(value, "off") == 0) {
        result = false;
        return true;
    }
    return false;
}

bool parse_integer(const char* value, int& result) {
    if (!value || !*value) return false;
    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (errno == ERANGE || end == value || *end != '\0' ||
        parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) return false;
    result = static_cast<int>(parsed);
    return true;
}

bool parse_float(const char* value, float& result) {
    if (!value || !*value) return false;
    errno = 0;
    char* end = nullptr;
    const float parsed = std::strtof(value, &end);
    if (errno == ERANGE || end == value || *end != '\0' || !std::isfinite(parsed)) return false;
    result = parsed;
    return true;
}

bool read_text_file(const wchar_t* path, std::string& text, bool& missing) {
    missing = false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { missing = GetLastError() == ERROR_FILE_NOT_FOUND; return false; }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > kMaximumConfigBytes) {
        CloseHandle(file);
        return false;
    }
    text.assign(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const bool read_ok = text.empty() ||
        (ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr) && read == text.size());
    CloseHandle(file);
    return read_ok;
}

bool write_all(HANDLE file, const char* data, std::size_t size) {
    while (size) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(size, 1024u * 1024u));
        DWORD written = 0;
        if (!WriteFile(file, data, chunk, &written, nullptr) || written == 0) return false;
        data += written;
        size -= written;
    }
    return true;
}

// Write-flush-atomic-replace.
bool write_text_file(const wchar_t* path, const std::string& text) {
#ifdef EID3D11_EMBEDDED
    if (embedded_agent_background()) return true;
#endif
    const std::wstring temporary = std::wstring(path) + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const bool wrote = write_all(file, text.data(), text.size());
    const bool flushed = wrote && FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    if (!flushed || !MoveFileExW(temporary.c_str(), path,
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

} // namespace

bool interface_profile_option(const RendererOption& option) noexcept {
    return option.group == "HUD layout" || option.group == "Interface";
}

std::wstring interface_profiles_directory() {
    std::wstring path = renderer_config_path();
    const auto slash = path.find_last_of(L'\\');
    path.erase(slash == std::wstring::npos ? 0 : slash + 1);
#ifdef EID3D11_EMBEDDED
    return path + L"rddraw_profiles";
#else
    return path + L"eid3d11_profiles";
#endif
}

const char* interface_profiles_help() noexcept {
#ifdef EID3D11_EMBEDDED
    return "Profiles are files in rddraw_profiles next to EIRebuild.exe. Share one by sending its .ini; copy a received one there and pick it here.";
#else
    return "Profiles are files in eid3d11_profiles next to ddraw.dll. Share one by sending its .ini; copy a received one there and pick it here.";
#endif
}

std::wstring renderer_config_path() {
#ifdef EID3D11_EMBEDDED
    return embedded_host_configuration().snapshot().renderer_config;
#else
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&renderer_config_path), &module)) return L"eid3d11.ini";
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return L"eid3d11.ini";
    wchar_t* slash = std::wcsrchr(path.data(), L'\\');
    if (!slash) return L"eid3d11.ini";
    wcscpy_s(slash + 1, path.size() - static_cast<std::size_t>(slash + 1 - path.data()),
             L"eid3d11.ini");
    return path.data();
#endif
}

const char* debug_overlay_mode_name(DebugOverlayMode mode) noexcept {
    switch (mode) {
    case DebugOverlayMode::compact: return "compact";
    case DebugOverlayMode::full: return "full";
    default: return "hidden";
    }
}

bool parse_debug_overlay_mode(const char* value, DebugOverlayMode& mode) noexcept {
    if (!value) return false;
    if (_stricmp(value, "hidden") == 0) mode = DebugOverlayMode::hidden;
    else if (_stricmp(value, "compact") == 0) mode = DebugOverlayMode::compact;
    else if (_stricmp(value, "full") == 0) mode = DebugOverlayMode::full;
    else return false;
    return true;
}

DebugOverlayMode next_debug_overlay_mode(DebugOverlayMode mode) noexcept {
    switch (mode) {
    case DebugOverlayMode::hidden: return DebugOverlayMode::compact;
    case DebugOverlayMode::compact: return DebugOverlayMode::full;
    default: return DebugOverlayMode::hidden;
    }
}

const char* debug_resource_view_name(DebugResourceView view) noexcept {
    switch (view) {
    case DebugResourceView::raw_depth: return "raw_depth";
    case DebugResourceView::linearized_depth: return "linearized_depth";
    case DebugResourceView::stencil: return "stencil";
    case DebugResourceView::ambient_occlusion: return "ambient_occlusion";
    case DebugResourceView::normals: return "normals";
    case DebugResourceView::materials: return "materials";
    case DebugResourceView::object_ids: return "object_ids";
    case DebugResourceView::motion: return "motion";
    case DebugResourceView::shadows: return "shadows";
    case DebugResourceView::shadow_cascade_coverage: return "shadow_cascade_coverage";
    case DebugResourceView::hierarchical_depth: return "hierarchical_depth";
    default: return "disabled";
    }
}

const char* debug_resource_view_label(DebugResourceView view) noexcept {
    switch (view) {
    case DebugResourceView::raw_depth: return "Raw depth";
    case DebugResourceView::linearized_depth: return "Linearized depth";
    case DebugResourceView::stencil: return "Stencil";
    case DebugResourceView::ambient_occlusion: return "Ambient occlusion";
    case DebugResourceView::normals: return "Normals (unavailable)";
    case DebugResourceView::materials: return "Material IDs (unavailable)";
    case DebugResourceView::object_ids: return "Object IDs (unavailable)";
    case DebugResourceView::motion: return "Motion vectors (unavailable)";
    case DebugResourceView::shadows: return "Shadow maps";
    case DebugResourceView::shadow_cascade_coverage: return "Shadow cascade coverage";
    case DebugResourceView::hierarchical_depth: return "Hi-Z (unavailable)";
    default: return "Disabled";
    }
}

bool parse_debug_resource_view(const char* value, DebugResourceView& view) noexcept {
    if (!value) return false;
    for (std::uint8_t raw = 0; raw < static_cast<std::uint8_t>(DebugResourceView::count); ++raw) {
        const auto candidate = static_cast<DebugResourceView>(raw);
        if (_stricmp(value, debug_resource_view_name(candidate)) == 0) {
            view = candidate;
            return true;
        }
    }
    return false;
}

bool DebugResourceViewAvailability::available(DebugResourceView view) const noexcept {
    switch (view) {
    case DebugResourceView::disabled: return true;
    case DebugResourceView::raw_depth: return raw_depth;
    case DebugResourceView::linearized_depth: return linearized_depth;
    case DebugResourceView::stencil: return stencil;
    case DebugResourceView::ambient_occlusion: return ambient_occlusion;
    case DebugResourceView::shadows: return shadows;
    case DebugResourceView::shadow_cascade_coverage: return shadow_cascade_coverage;
    default: return false;
    }
}

namespace {
std::uint64_t option_key_hash(const char* key) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    for (; *key; ++key) { hash ^= static_cast<unsigned char>(*key); hash *= 1099511628211ull; }
    return hash;
}
} // namespace

bool RendererConfig::register_option(RendererOption option) {
    if (!valid_key(option.key.c_str()) || option.label.empty() || find(option.key.c_str())) return false;
    const std::uint64_t hash = option_key_hash(option.key.c_str());
    options_.push_back(std::move(option));
    ++revision_;
    if (!index_.emplace(hash, options_.size() - 1).second) index_collision_ = true;
    return true;
}

bool RendererConfig::register_boolean(const char* key, const char* label, const char* group,
                                      bool default_value, bool restart_required,
                                      const char* description) {
    RendererOption option{};
    option.key = key ? key : "";
    option.label = label ? label : "";
    option.group = group ? group : "Graphics";
    option.description = description ? description : "";
    option.type = RendererOptionType::boolean;
    option.restart_required = restart_required;
    option.boolean_value = option.boolean_default = default_value;
    return register_option(std::move(option));
}

bool RendererConfig::register_integer(const char* key, const char* label, const char* group,
                                      int default_value, int minimum, int maximum, int step,
                                      bool restart_required, const char* description) {
    if (minimum > maximum || step <= 0 || default_value < minimum || default_value > maximum) return false;
    RendererOption option{};
    option.key = key ? key : "";
    option.label = label ? label : "";
    option.group = group ? group : "Graphics";
    option.description = description ? description : "";
    option.type = RendererOptionType::integer;
    option.restart_required = restart_required;
    option.integer_value = option.integer_default = default_value;
    option.integer_minimum = minimum;
    option.integer_maximum = maximum;
    option.integer_step = step;
    return register_option(std::move(option));
}

bool RendererConfig::register_floating_point(const char* key, const char* label, const char* group,
                                             float default_value, float minimum, float maximum,
                                             float step, bool restart_required,
                                             const char* description) {
    if (!std::isfinite(default_value) || !std::isfinite(minimum) || !std::isfinite(maximum) ||
        !std::isfinite(step) || minimum > maximum || step <= 0.0f ||
        default_value < minimum || default_value > maximum) return false;
    RendererOption option{};
    option.key = key ? key : "";
    option.label = label ? label : "";
    option.group = group ? group : "Graphics";
    option.description = description ? description : "";
    option.type = RendererOptionType::floating_point;
    option.restart_required = restart_required;
    option.floating_value = option.floating_default = default_value;
    option.floating_minimum = minimum;
    option.floating_maximum = maximum;
    option.floating_step = step;
    return register_option(std::move(option));
}

const RendererOption* RendererConfig::find(const char* key) const noexcept {
    if (!key) return nullptr;
    const auto indexed = index_.find(option_key_hash(key));
    if (indexed != index_.end() && options_[indexed->second].key == key) return &options_[indexed->second];
    if (!index_collision_) return nullptr;
    const auto found = std::find_if(options_.begin(), options_.end(),
                                    [key](const RendererOption& option) { return option.key == key; });
    return found == options_.end() ? nullptr : &*found;
}

RendererOption* RendererConfig::find_mutable(const char* key) noexcept {
    return const_cast<RendererOption*>(static_cast<const RendererConfig*>(this)->find(key));
}

bool RendererConfig::set_boolean(const char* key, bool value) noexcept {
    auto* option = find_mutable(key);
    if (!option || option->type != RendererOptionType::boolean) return false;
    if (option->boolean_value != value) {
        option->boolean_value = value;
        mark_changed();
    }
    return true;
}

bool RendererConfig::set_integer(const char* key, int value) noexcept {
    auto* option = find_mutable(key);
    if (!option || option->type != RendererOptionType::integer) return false;
    value = std::clamp(value, option->integer_minimum, option->integer_maximum);
    if (option->integer_value != value) {
        option->integer_value = value;
        mark_changed();
    }
    return true;
}

bool RendererConfig::set_floating_point(const char* key, float value) noexcept {
    auto* option = find_mutable(key);
    if (!option || option->type != RendererOptionType::floating_point || !std::isfinite(value)) return false;
    value = std::clamp(value, option->floating_minimum, option->floating_maximum);
    if (option->floating_value != value) {
        option->floating_value = value;
        mark_changed();
    }
    return true;
}

void RendererConfig::set_overlay_mode(DebugOverlayMode mode) noexcept {
    if (overlay_mode_ != mode) {
        overlay_mode_ = mode;
        mark_changed();
    }
}

void RendererConfig::set_first_run_done(bool done) noexcept {
    if (first_run_done_ != done) {
        first_run_done_ = done;
        mark_changed();
    }
}

void RendererConfig::set_nvidia_cache_id(const std::string& id) {
    if (nvidia_cache_id_ != id) {
        nvidia_cache_id_ = id;
        mark_changed();
    }
}

void RendererConfig::set_debug_resource_view(DebugResourceView view) noexcept {
    if (view >= DebugResourceView::count) view = DebugResourceView::disabled;
    if (debug_resource_view_ != view) {
        debug_resource_view_ = view;
        mark_changed();
    }
}

bool RendererConfig::register_text(const char* key, const char* label,
                                   const char* group, const char* description,
                                   const char* default_value) {
    RendererOption option;
    option.key = key; option.label = label; option.group = group;
    option.description = description;
    option.type = RendererOptionType::text;
    option.text_value = default_value ? default_value : "";
    return register_option(std::move(option));
}

bool RendererConfig::set_text(const char* key, const char* value) {
    auto* option = find_mutable(key);
    if (!option || option->type != RendererOptionType::text || !value ||
        std::strlen(value) > 1023 || std::strpbrk(value, "\r\n")) return false;
    if (option->text_value == value) return true;
    option->text_value = value;
    mark_changed();
    return true;
}

bool RendererConfig::reset_to_default(const char* key) noexcept {
    const auto* option = find(key);
    if (!option) return false;
    switch (option->type) {
    case RendererOptionType::boolean: return set_boolean(key, option->boolean_default);
    case RendererOptionType::integer: return set_integer(key, option->integer_default);
    case RendererOptionType::floating_point: return set_floating_point(key, option->floating_default);
    case RendererOptionType::text: return false;
    }
    return false;
}

bool RendererConfig::load(const wchar_t* path) {
    if (!path || !*path) return false;
    ++revision_;
    std::string text;bool missing = false;
    if (!read_text_file(path, text, missing)) return missing;

    std::string section;
    std::size_t offset = 0;
    while (offset <= text.size()) {
        const std::size_t end = text.find('\n', offset);
        const std::string line = trim(std::string_view(text).substr(
            offset, end == std::string::npos ? text.size() - offset : end - offset));
        offset = end == std::string::npos ? text.size() + 1 : end + 1;
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
            section = trim(std::string_view(line).substr(1, line.size() - 2));
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = trim(std::string_view(line).substr(0, equals));
        const std::string value = trim(std::string_view(line).substr(equals + 1));
        if (_stricmp(section.c_str(), "overlay") == 0 && _stricmp(key.c_str(), "mode") == 0) {
            DebugOverlayMode mode{};
            if (parse_debug_overlay_mode(value.c_str(), mode)) {
                overlay_mode_ = mode;
                overlay_mode_loaded_ = true;
            }
            continue;
        }
        if (_stricmp(section.c_str(), "setup") == 0 && _stricmp(key.c_str(), "first_run_done") == 0) {
            bool done = false;
            if (parse_boolean(value.c_str(), done)) first_run_done_ = done;
            continue;
        }
        if (_stricmp(section.c_str(), "setup") == 0 && _stricmp(key.c_str(), "over_budget_pending") == 0) {
            bool pending = false;
            if (parse_boolean(value.c_str(), pending)) over_budget_pending_ = pending;
            continue;
        }
        if (_stricmp(section.c_str(), "setup") == 0 && _stricmp(key.c_str(), "serial_shader_preparation") == 0) {
            bool serial = false;
            if (parse_boolean(value.c_str(), serial)) serial_shader_preparation_ = serial;
            continue;
        }
        if (_stricmp(section.c_str(), "setup") == 0 && _stricmp(key.c_str(), "settings_version") == 0) {
            int version = 0;
            if (parse_integer(value.c_str(), version) && version > 0) settings_version_ = version;
            continue;
        }
        if (_stricmp(section.c_str(), "setup") == 0 && _stricmp(key.c_str(), "nvidia_cache_id") == 0) {
            if (value.size() <= 16) nvidia_cache_id_ = value;
            continue;
        }
        if (_stricmp(section.c_str(), "overlay") == 0 &&
            _stricmp(key.c_str(), "debug_resource_view") == 0) {
            DebugResourceView view{};
            if (parse_debug_resource_view(value.c_str(), view)) debug_resource_view_ = view;
            continue;
        }
        if (_stricmp(section.c_str(), "graphics") != 0) continue;
        auto* option = find_mutable(key.c_str());
        if (option) apply_value(*option, value);
    }
    dirty_ = false;
    return true;
}

void RendererConfig::apply_value(RendererOption& option, const std::string& value) {
    switch (option.type) {
    case RendererOptionType::text:
        if (value.size() <= 1023) option.text_value = value;
        break;
    case RendererOptionType::boolean: {
        bool parsed = false;
        if (parse_boolean(value.c_str(), parsed)) option.boolean_value = parsed;
        break;
    }
    case RendererOptionType::integer: {
        int parsed = 0;
        // renderer_backend was a text option ("d3d11"/"d3d12") in older ini files.
        if (option.key == "renderer_backend" && (value == "d3d11" || value == "d3d12"))
            option.integer_value = value == "d3d12" ? 1 : 0;
        else if (parse_integer(value.c_str(), parsed))
            option.integer_value = std::clamp(parsed, option.integer_minimum, option.integer_maximum);
        break;
    }
    case RendererOptionType::floating_point: {
        float parsed = 0.0f;
        if (parse_float(value.c_str(), parsed))
            option.floating_value = std::clamp(parsed, option.floating_minimum, option.floating_maximum);
        break;
    }
    }
}

bool RendererConfig::load_interface_profile(const wchar_t* path) {
    if (!path || !*path) return false;
    ++revision_;
    std::string text;bool missing = false;
    if (!read_text_file(path, text, missing)) return false;
    std::size_t offset = 0;
    while (offset <= text.size()) {
        const std::size_t end = text.find('\n', offset);
        const std::string line = trim(std::string_view(text).substr(
            offset, end == std::string::npos ? text.size() - offset : end - offset));
        offset = end == std::string::npos ? text.size() + 1 : end + 1;
        if (line.empty() || line.front() == ';' || line.front() == '#' || line.front() == '[') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        auto* option = find_mutable(trim(std::string_view(line).substr(0, equals)).c_str());
        if (option && interface_profile_option(*option)) {
            apply_value(*option, trim(std::string_view(line).substr(equals + 1)));
            mark_changed();
        }
    }
    return true;
}

bool RendererConfig::save(const wchar_t* path) const {
    if (!path || !*path) return false;
    if (!save_text(path)) return false;
    ++saves_;
    return true;
}

bool RendererConfig::save_text(const wchar_t* path) const {
    std::string text = "; EID3D11 renderer-owned configuration\r\n[overlay]\r\nmode=";
    text += debug_overlay_mode_name(overlay_mode_);
    text += "\r\ndebug_resource_view=";
    text += debug_resource_view_name(debug_resource_view_);
    text += "\r\n\r\n[setup]\r\nsettings_version=" + std::to_string(settings_version_);
    if (first_run_done_) text += "\r\nfirst_run_done=true";
    if (over_budget_pending_) text += "\r\nover_budget_pending=1";
    if (serial_shader_preparation_) text += "\r\nserial_shader_preparation=1";
    if (!nvidia_cache_id_.empty()) text += "\r\nnvidia_cache_id=" + nvidia_cache_id_;
    text += "\r\n\r\n[graphics]\r\n";
    for (const auto& option : options_) {
        text += option.key;
        text.push_back('=');
        char value[64]{};
        switch (option.type) {
        case RendererOptionType::text:
            text += option.text_value;
            break;
        case RendererOptionType::boolean:
            text += option.boolean_value ? "true" : "false";
            break;
        case RendererOptionType::integer:
            std::snprintf(value, sizeof(value), "%d", option.integer_value);
            text += value;
            break;
        case RendererOptionType::floating_point:
            std::snprintf(value, sizeof(value), "%.9g", static_cast<double>(option.floating_value));
            text += value;
            break;
        }
        text += "\r\n";
    }

    return write_text_file(path, text);
}

bool RendererConfig::save_interface_profile(const wchar_t* path) const {
    if (!path || !*path) return false;
    std::string text = "; EID3D11 interface profile (HUD layout).\r\n; ";
    text += interface_profiles_help();
    text += "\r\n[interface]\r\n";
    for (const auto& option : options_) {
        if (!interface_profile_option(option)) continue;
        text += option.key;
        text.push_back('=');
        char value[64]{};
        switch (option.type) {
        case RendererOptionType::text: text += option.text_value; break;
        case RendererOptionType::boolean: text += option.boolean_value ? "true" : "false"; break;
        case RendererOptionType::integer:
            std::snprintf(value, sizeof(value), "%d", option.integer_value);text += value;break;
        case RendererOptionType::floating_point:
            std::snprintf(value, sizeof(value), "%.9g", static_cast<double>(option.floating_value));text += value;break;
        }
        text += "\r\n";
    }
    return write_text_file(path, text);
}

} // namespace owned
