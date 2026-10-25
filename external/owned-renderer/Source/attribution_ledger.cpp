#include "attribution_ledger.h"

#include <bcrypt.h>
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <string>

#include "build_version.h"

namespace owned {
namespace {

std::uint64_t filetime_value(const FILETIME& value) noexcept {
    ULARGE_INTEGER result{};
    result.LowPart = value.dwLowDateTime;
    result.HighPart = value.dwHighDateTime;
    return result.QuadPart;
}

bool marker_file_matches_capture_id(const char* path, const char* capture_id) noexcept {
    FILE* file = nullptr;
    if (fopen_s(&file, path, "rb") != 0 || !file) return false;
    char prefix[512]{};
    const std::size_t size = std::fread(prefix, 1, sizeof(prefix), file);
    const bool read_failed = std::ferror(file) != 0;
    std::fclose(file);
    return !read_failed && attribution_marker_evidence_matches_capture_id(
        prefix, size, capture_id);
}

} // namespace

bool attribution_capture_identity_qpc_valid(bool query_succeeded,
                                            std::int64_t qpc) noexcept {
    return query_succeeded && qpc > 0;
}

bool attribution_marker_evidence_matches_capture_id(const char* evidence,
                                                     std::size_t evidence_size,
                                                     const char* capture_id) noexcept {
    if (!evidence || !capture_id || !*capture_id) return false;
    const std::string needle = std::string("\"capture_id\":\"") + capture_id + "\"";
    if (needle.size() > evidence_size) return false;
    return std::search(evidence, evidence + evidence_size, needle.begin(), needle.end()) !=
        evidence + evidence_size;
}

const char* attribution_marker_label_name(AttributionMarkerLabel label) noexcept {
    constexpr const char* names[] = {
        "main_menu_high_fps_ui", "store_inventory", "city_heavy_forced_visibility",
        "outdoor_daytime", "outdoor_nighttime", "cave_interior",
        "local_light_effect_torch_spell_quest_emitter_mutation", "camera_pan_teleport",
        "map_load_reload_transition_return_to_menu", "camera_motion_rotation_zoom",
        "camera_turns", "terrain_foliage_water_vfx", "animated_figures_equipment",
        "interaction_text_ui", "shadow_heavy_future" };
    const auto index = static_cast<std::size_t>(label);
    return index < std::size(names) ? names[index] : "invalid";
}

const char* attribution_marker_repetition_name(AttributionMarkerRepetition value) noexcept {
    switch (value) {
    case AttributionMarkerRepetition::first_observed: return "first_observed";
    case AttributionMarkerRepetition::repeated_opening: return "repeated_opening";
    default: return "";
    }
}

const char* attribution_issue13_menu_label_name(AttributionIssue13MenuLabel label) noexcept {
    constexpr const char* names[] = {
        "main_load", "main_options", "ingame_root", "inventory", "store",
        "ingame_save", "ingame_load", "ingame_options" };
    const auto index = static_cast<std::size_t>(label);
    return index < std::size(names) ? names[index] : "invalid";
}

bool attribution_marker_selection_valid(
    const AttributionMarkerSelection& selection) noexcept {
    if (selection.label >= AttributionMarkerLabel::count) return false;
    const bool menu_label_terminated =
        std::memchr(selection.menu_label.data(), '\0', selection.menu_label.size()) != nullptr;
    const bool menu_metadata = selection.menu_label[0] != '\0' || selection.opening_ordinal != 0 ||
        selection.repetition != AttributionMarkerRepetition::none;
    if (!menu_label_terminated) return false;
    if (selection.issue13_menu_transition != menu_metadata) return false;
    if (!selection.issue13_menu_transition) return true;
    if (!selection.menu_label[0] || selection.opening_ordinal == 0 ||
        selection.repetition == AttributionMarkerRepetition::none) return false;
    bool exact_label = false;
    for (std::uint32_t index = 0;
         index < static_cast<std::uint32_t>(AttributionIssue13MenuLabel::count); ++index) {
        if (std::strcmp(selection.menu_label.data(), attribution_issue13_menu_label_name(
                static_cast<AttributionIssue13MenuLabel>(index))) == 0) {
            exact_label = true;
            break;
        }
    }
    return exact_label;
}

bool AttributionMarkerRecorder::arm_begin(
    const AttributionMarkerSelection& selection) noexcept {
    if (!attribution_marker_selection_valid(selection) ||
        state_ == AttributionMarkerState::begin_armed ||
        state_ == AttributionMarkerState::active || state_ == AttributionMarkerState::end_armed ||
        state_ == AttributionMarkerState::overflow || state_ == AttributionMarkerState::invalid) {
        return false;
    }
    if (size_ == records_.size()) {
        state_ = AttributionMarkerState::overflow;
        return false;
    }
    armed_ = selection;
    state_ = AttributionMarkerState::begin_armed;
    return true;
}

bool AttributionMarkerRecorder::arm_end() noexcept {
    if (state_ != AttributionMarkerState::active) return false;
    state_ = AttributionMarkerState::end_armed;
    return true;
}

void AttributionMarkerRecorder::successful_present_boundary(
    std::uint64_t next_frame_serial, std::int64_t qpc) noexcept {
    if (state_ == AttributionMarkerState::begin_armed) {
        active_ = {};
        active_.selection = armed_;
        active_.begin_frame_serial = next_frame_serial;
        active_.begin_qpc = qpc;
        state_ = AttributionMarkerState::active;
    } else if (state_ == AttributionMarkerState::end_armed) {
        if (qpc <= active_.begin_qpc || next_frame_serial <= active_.begin_frame_serial) {
            state_ = AttributionMarkerState::invalid;
            return;
        }
        if (size_ == records_.size()) {
            state_ = AttributionMarkerState::overflow;
            return;
        }
        active_.end_frame_serial_exclusive = next_frame_serial;
        active_.end_qpc_exclusive = qpc;
        records_[size_++] = active_;
        active_ = {};
        state_ = AttributionMarkerState::complete;
    }
}

void AttributionMarkerRecorder::fail_if_open() noexcept {
    if (state_ == AttributionMarkerState::begin_armed || state_ == AttributionMarkerState::active ||
        state_ == AttributionMarkerState::end_armed) state_ = AttributionMarkerState::invalid;
}

void AttributionMarkerRecorder::fail_capacity() noexcept {
    state_ = AttributionMarkerState::overflow;
}

AttributionMarkerStatus AttributionMarkerRecorder::status() const noexcept {
    AttributionMarkerStatus result{};
    result.state = state_;
    result.completed = static_cast<std::uint32_t>(size_);
    result.current = state_ == AttributionMarkerState::begin_armed ? armed_ : active_.selection;
    return result;
}

namespace {

bool environment_flag(const char* name) noexcept {
    char value[8]{};
    return GetEnvironmentVariableA(name, value, sizeof(value)) != 0 && value[0] == '1';
}

bool module_sha256(char (&hex)[65]) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&module_sha256), &module)) return false;
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (!length || length == MAX_PATH) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_size = 0, bytes = 0;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &bytes, 0) == 0;
    std::string object(object_size, '\0');
    if (ok) ok = BCryptCreateHash(algorithm, &hash,
        reinterpret_cast<PUCHAR>(object.data()), object_size, nullptr, 0, 0) == 0;
    std::array<unsigned char, 64 * 1024> buffer{};
    while (ok) {
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) { ok = false; break; }
        if (!read) break;
        ok = BCryptHashData(hash, buffer.data(), read, 0) == 0;
    }
    std::array<unsigned char, 32> digest{};
    if (ok) ok = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    if (!ok) return false;
    static constexpr char digits[] = "0123456789ABCDEF";
    for (std::size_t i = 0; i < digest.size(); ++i) {
        hex[i * 2] = digits[digest[i] >> 4];
        hex[i * 2 + 1] = digits[digest[i] & 15];
    }
    hex[64] = 0;
    return true;
}

void add_delta(AttributionCpuDelta& target, const AttributionCpuDelta& value) noexcept {
    target.qpc += value.qpc;
    target.thread_kernel_100ns += value.thread_kernel_100ns;
    target.thread_user_100ns += value.thread_user_100ns;
    target.thread_cycles += value.thread_cycles;
    target.process_kernel_100ns += value.process_kernel_100ns;
    target.process_user_100ns += value.process_user_100ns;
    target.process_cycles += value.process_cycles;
}

} // namespace

class AttributionLedger::Ring {
public:
    Ring() noexcept
        : values_(new (std::nothrow) AttributionFrameRecord[kAttributionRingCapacity]) {}
    bool valid() const noexcept { return values_ != nullptr; }
    bool push(const AttributionFrameRecord& value) noexcept {
        if (size_ == kAttributionRingCapacity) { overflowed_ = true; return false; }
        values_[size_++] = value;
        return true;
    }
    const AttributionFrameRecord& at(std::size_t index) const noexcept { return values_[index]; }
    std::size_t size() const noexcept { return size_; }
    bool overflowed() const noexcept { return overflowed_; }
    void clear() noexcept { size_ = 0; overflowed_ = false; }
private:
    std::unique_ptr<AttributionFrameRecord[]> values_{};
    std::size_t size_ = 0;
    bool overflowed_ = false;
};

AttributionLedger::AttributionLedger() = default;
AttributionLedger::~AttributionLedger() = default;

void AttributionLedger::configure_diagnostic_log(const char* path) noexcept {
    diagnostic_log_path_ = {};
    if (path && *path) {
        strncpy_s(diagnostic_log_path_.data(), diagnostic_log_path_.size(), path, _TRUNCATE);
        return;
    }
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(&environment_flag), &module)) return;
    const DWORD length = GetModuleFileNameA(module, diagnostic_log_path_.data(),
                                            static_cast<DWORD>(diagnostic_log_path_.size()));
    if (!length || length == static_cast<DWORD>(diagnostic_log_path_.size())) {
        diagnostic_log_path_ = {};
        return;
    }
    char* slash = std::strrchr(diagnostic_log_path_.data(), '\\');
    if (!slash) {
        diagnostic_log_path_ = {};
        return;
    }
    strcpy_s(slash + 1, diagnostic_log_path_.size() -
             static_cast<std::size_t>(slash + 1 - diagnostic_log_path_.data()),
             "owned_renderer.log");
}

void AttributionLedger::diagnostic(const char* event, const char* format, ...) noexcept {
    char detail[1024]{};
    if (format) {
        va_list arguments;
        va_start(arguments, format);
        _vsnprintf_s(detail, sizeof(detail), _TRUNCATE, format, arguments);
        va_end(arguments);
    }
    char line[1280]{};
    _snprintf_s(line, sizeof(line), _TRUNCATE, "Attribution.%s%s%s\r\n",
                event ? event : "unknown", format ? " " : "", detail);
    OutputDebugStringA(line);
    if (!diagnostic_log_path_[0]) return;
    FILE* file = nullptr;
    fopen_s(&file, diagnostic_log_path_.data(), "ab");
    if (!file) return;
    std::fwrite(line, 1, std::strlen(line), file);
    std::fclose(file);
}

AttributionCpuDelta attribution_delta(const AttributionCpuPoint& begin,
                                      const AttributionCpuPoint& end) noexcept {
    AttributionCpuDelta result{};
    result.qpc = end.qpc - begin.qpc;
    result.thread_kernel_100ns = static_cast<std::int64_t>(end.thread_kernel_100ns) - static_cast<std::int64_t>(begin.thread_kernel_100ns);
    result.thread_user_100ns = static_cast<std::int64_t>(end.thread_user_100ns) - static_cast<std::int64_t>(begin.thread_user_100ns);
    result.thread_cycles = static_cast<std::int64_t>(end.thread_cycles) - static_cast<std::int64_t>(begin.thread_cycles);
    result.process_kernel_100ns = static_cast<std::int64_t>(end.process_kernel_100ns) - static_cast<std::int64_t>(begin.process_kernel_100ns);
    result.process_user_100ns = static_cast<std::int64_t>(end.process_user_100ns) - static_cast<std::int64_t>(begin.process_user_100ns);
    result.process_cycles = static_cast<std::int64_t>(end.process_cycles) - static_cast<std::int64_t>(begin.process_cycles);
    return result;
}

bool attribution_modes_compatible(bool wpr_active, bool suspend_sampler_requested) noexcept {
    return !(wpr_active && suspend_sampler_requested);
}

std::uint32_t validate_cpu_attribution(AttributionFrameRecord& record) noexcept {
    AttributionCpuDelta recomputed_named{};
    std::int64_t prior_scope_end = record.qpc_begin;
    for (std::uint32_t index = 0; index < record.scope_count; ++index) {
        const auto& scope = record.scopes[index];
        if (scope.serial != record.serial) record.invalid |= attribution_mixed_serial;
        if (scope.qpc_end < scope.qpc_begin || scope.qpc_begin < prior_scope_end)
            record.invalid |= attribution_scope_reconciliation;
        prior_scope_end = scope.qpc_end;
        add_delta(recomputed_named, scope.cpu);
    }
    const auto equal = [](const AttributionCpuDelta& left, const AttributionCpuDelta& right) {
        return left.qpc == right.qpc && left.thread_kernel_100ns == right.thread_kernel_100ns &&
               left.thread_user_100ns == right.thread_user_100ns && left.thread_cycles == right.thread_cycles &&
               left.process_kernel_100ns == right.process_kernel_100ns &&
               left.process_user_100ns == right.process_user_100ns && left.process_cycles == right.process_cycles;
    };
    if (!equal(recomputed_named, record.named)) record.invalid |= attribution_scope_reconciliation;
    auto subtract = [](const AttributionCpuDelta& left, const AttributionCpuDelta& right) {
        AttributionCpuDelta value{};
        value.qpc = left.qpc - right.qpc;
        value.thread_kernel_100ns = left.thread_kernel_100ns - right.thread_kernel_100ns;
        value.thread_user_100ns = left.thread_user_100ns - right.thread_user_100ns;
        value.thread_cycles = left.thread_cycles - right.thread_cycles;
        value.process_kernel_100ns = left.process_kernel_100ns - right.process_kernel_100ns;
        value.process_user_100ns = left.process_user_100ns - right.process_user_100ns;
        value.process_cycles = left.process_cycles - right.process_cycles;
        return value;
    };
    auto negative = [](const AttributionCpuDelta& value) {
        return value.qpc < 0 || value.thread_kernel_100ns < 0 || value.thread_user_100ns < 0 ||
               value.thread_cycles < 0 || value.process_kernel_100ns < 0 ||
               value.process_user_100ns < 0 || value.process_cycles < 0;
    };
    record.device_gap = subtract(record.device, record.named);
    record.outside_device = subtract(record.totals, record.device);
    record.present_minus_process_kernel_100ns = record.totals.thread_kernel_100ns - record.totals.process_kernel_100ns;
    record.present_minus_process_user_100ns = record.totals.thread_user_100ns - record.totals.process_user_100ns;
    record.present_minus_process_cycles = record.totals.thread_cycles - record.totals.process_cycles;
    if (record.present_minus_process_kernel_100ns > 0 || record.present_minus_process_user_100ns > 0 ||
        record.present_minus_process_cycles > 0) record.invalid |= attribution_present_exceeds_process;
    if (negative(record.device_gap) || negative(record.outside_device)) {
        record.invalid |= attribution_negative_residual;
        record.invalid |= attribution_excess_residual;
    }
    if (record.qpc_present_return < record.qpc_present_enter ||
        record.qpc_present_enter < record.qpc_begin || record.totals.qpc < 0 ||
        record.totals.qpc != record.qpc_present_return - record.qpc_begin)
        record.invalid |= attribution_excess_residual;
    return record.invalid;
}

std::uint32_t reconcile_gpu_attribution(AttributionFrameRecord& record,
                                        bool resolved, bool disjoint,
                                        std::uint64_t span_begin,
                                        std::uint64_t span_end,
                                        std::uint64_t named_ticks,
                                        std::uint64_t frequency) noexcept {
    record.gpu_frequency = frequency;
    record.gpu_named_ticks = named_ticks;
    if (!resolved || span_end < span_begin || !frequency) {
        record.invalid |= attribution_gpu_unresolved;
        return record.invalid;
    }
    if (disjoint) record.invalid |= attribution_gpu_disjoint;
    record.gpu_span_ticks = span_end - span_begin;
    record.gpu_gap_ticks = static_cast<std::int64_t>(record.gpu_span_ticks) -
                           static_cast<std::int64_t>(named_ticks);
    if (record.gpu_gap_ticks < 0) record.invalid |= attribution_gpu_named_exceeds_span;
    return record.invalid;
}

bool AttributionLedger::initialize(const char* diagnostic_log_path) {
    char attribution_value[8]{};
    const DWORD attribution_length = GetEnvironmentVariableA(
        "EI_RENDERER_ATTRIBUTION", attribution_value, static_cast<DWORD>(sizeof(attribution_value)));
    enabled_ = attribution_length != 0 && attribution_value[0] == '1';
    if (!enabled_) {
        if (attribution_length != 0) {
            configure_diagnostic_log(diagnostic_log_path);
            diagnostic("DisabledByEnvironment", "value=%s", attribution_value);
        }
        return true;
    }
    configure_diagnostic_log(diagnostic_log_path);
    const bool old_sampler = environment_flag("EI_RENDERER_SAMPLE_PROFILE");
    const bool wpr_active = environment_flag("EI_RENDERER_WPR_ACTIVE");
    if (old_sampler || !attribution_modes_compatible(wpr_active, old_sampler)) {
        diagnostic("RetiredSamplerConflict", "wpr_active=%u", wpr_active ? 1u : 0u);
        enabled_ = false;
        return false;
    }
    ring_.reset(new (std::nothrow) Ring{});
    if (!ring_ || !ring_->valid()) {
        diagnostic("RingAllocationFailure", "capacity=%u", kAttributionRingCapacity);
        ring_.reset(); enabled_ = false; return false;
    }
    FILETIME created{}, exited{}, kernel{}, user{};
    LARGE_INTEGER qpc_frequency{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) ||
        !QueryPerformanceFrequency(&qpc_frequency) || qpc_frequency.QuadPart <= 0) {
        diagnostic("ProcessOrQpcSetupFailure");
        ring_.reset();
        enabled_ = false;
        return false;
    }
    process_creation_filetime_100ns_ = filetime_value(created);
    qpc_frequency_ = static_cast<std::uint64_t>(qpc_frequency.QuadPart);
    LARGE_INTEGER capture_qpc{};
    const BOOL capture_qpc_ok = QueryPerformanceCounter(&capture_qpc);
    if (!attribution_capture_identity_qpc_valid(capture_qpc_ok != FALSE,
                                                capture_qpc.QuadPart)) {
        diagnostic("CaptureIdentitySetupFailure", "qpc_ok=%u qpc=%lld",
                   capture_qpc_ok ? 1u : 0u,
                   static_cast<long long>(capture_qpc.QuadPart));
        ring_.reset();
        enabled_ = false;
        return false;
    }
    std::snprintf(capture_id_.data(), capture_id_.size(), "%016llX-%08lX-%016llX",
        static_cast<unsigned long long>(process_creation_filetime_100ns_),
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long long>(capture_qpc.QuadPart));
    strncpy_s(commit_.data(), commit_.size(), kBuildCommit, _TRUNCATE);
    char hash[65]{};
    if (module_sha256(hash)) strncpy_s(module_hash_.data(), module_hash_.size(), hash, _TRUNCATE);
    else strncpy_s(module_hash_.data(), module_hash_.size(), "unavailable", _TRUNCATE);
    diagnostic("ActiveNoFrames", "capacity=%u", kAttributionRingCapacity);
    return true;
}

void AttributionLedger::record_timestamp_query_failure(const char* stage, HRESULT result) noexcept {
    if (enabled_) diagnostic("TimestampQueryCreationFailure", "stage=%s hr=0x%08lX",
                             stage ? stage : "unknown", static_cast<unsigned long>(result));
}

AttributionCpuPoint AttributionLedger::sample_point() const noexcept {
    AttributionCpuPoint result{};
    LARGE_INTEGER qpc{};
    FILETIME created{}, exited{}, thread_kernel{}, thread_user{}, process_kernel{}, process_user{};
    ULONG64 thread_cycles = 0, process_cycles = 0;
    const BOOL qpc_ok = QueryPerformanceCounter(&qpc);
    const BOOL thread_ok = present_thread_ && GetThreadTimes(present_thread_, &created, &exited, &thread_kernel, &thread_user);
    const BOOL process_ok = GetProcessTimes(GetCurrentProcess(), &created, &exited, &process_kernel, &process_user);
    const BOOL thread_cycle_ok = present_thread_ && QueryThreadCycleTime(present_thread_, &thread_cycles);
    const BOOL process_cycle_ok = QueryProcessCycleTime(GetCurrentProcess(), &process_cycles);
    result.qpc = qpc.QuadPart;
    result.thread_kernel_100ns = filetime_value(thread_kernel);
    result.thread_user_100ns = filetime_value(thread_user);
    result.thread_cycles = thread_cycles;
    result.process_kernel_100ns = filetime_value(process_kernel);
    result.process_user_100ns = filetime_value(process_user);
    result.process_cycles = process_cycles;
    result.valid = qpc_ok && thread_ok && process_ok && thread_cycle_ok && process_cycle_ok;
    return result;
}

bool AttributionLedger::begin_interval(std::uint64_t serial) {
    if (!enabled_ || interval_open_ || !next_begin_valid_) return false;
    current_ = {};
    current_.serial = serial;
    current_.process_creation_filetime_100ns = process_creation_filetime_100ns_;
    current_.pid = GetCurrentProcessId();
    current_.present_thread_id = present_thread_id_;
    current_.qpc_frequency = qpc_frequency_;
    current_.renderer_commit = commit_;
    current_.renderer_sha256 = module_hash_;
    current_.capture_id = capture_id_;
    current_.invalid = attribution_valid;
    const auto marker_state = markers_.status().state;
    current_.capture_selected = marker_state == AttributionMarkerState::active ||
        marker_state == AttributionMarkerState::end_armed;
    if (pending_marker_cpu_qpc_valid_) {
        current_.renderer_workload.set(RendererWorkloadField::capture_marker_cpu_qpc,
                                       pending_marker_cpu_qpc_);
        pending_marker_cpu_qpc_valid_ = false;
        pending_marker_cpu_qpc_ = 0;
    }
    begin_ = next_begin_;
    next_begin_valid_ = false;
    current_.qpc_begin = begin_.qpc;
    if (!begin_.valid) current_.invalid |= attribution_query_failed;
    if (GetCurrentThreadId() != present_thread_id_) current_.invalid |= attribution_mixed_thread;
    interval_open_ = true;
    return true;
}

bool AttributionLedger::capture_present_thread() noexcept {
    const DWORD current_id = GetCurrentThreadId();
    if (present_thread_) return current_id == present_thread_id_;
    HANDLE duplicate = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &duplicate,
                         THREAD_QUERY_INFORMATION, FALSE, 0)) return false;
    present_thread_ = duplicate;
    present_thread_id_ = current_id;
    return true;
}

void AttributionLedger::record_device_begin() {
    if (!interval_open_) return;
    device_begin_ = sample_point();
    device_open_ = true;
    if (!device_begin_.valid) current_.invalid |= attribution_query_failed;
    if (GetCurrentThreadId() != present_thread_id_) current_.invalid |= attribution_mixed_thread;
}

void AttributionLedger::record_device_end() {
    if (!interval_open_ || !device_open_) return;
    const auto end = sample_point();
    current_.device = attribution_delta(device_begin_, end);
    device_open_ = false;
    if (!end.valid) current_.invalid |= attribution_query_failed;
    if (GetCurrentThreadId() != present_thread_id_) current_.invalid |= attribution_mixed_thread;
}

void AttributionLedger::record_present_enter(std::uint32_t interval, std::uint32_t flags) {
    if (!interval_open_) return;
    LARGE_INTEGER qpc{};
    if (!QueryPerformanceCounter(&qpc)) current_.invalid |= attribution_query_failed;
    current_.qpc_present_enter = qpc.QuadPart;
    current_.present_interval = interval;
    current_.present_flags = flags;
    if (GetCurrentThreadId() != present_thread_id_) current_.invalid |= attribution_mixed_thread;
}

void AttributionLedger::record_present_return(HRESULT result) {
    if (!interval_open_) return;
    const AttributionCpuPoint end = sample_point();
    current_.qpc_present_return = end.qpc;
    current_.present_hresult = result;
    current_.totals = attribution_delta(begin_, end);
    if (!end.valid) current_.invalid |= attribution_query_failed;
    if (FAILED(result)) current_.invalid |= attribution_present_failed;
    if (GetCurrentThreadId() != present_thread_id_) current_.invalid |= attribution_mixed_thread;
    if (device_open_) current_.invalid |= attribution_open_scope;
    validate_cpu_attribution(current_);
    interval_open_ = false;
    if (current_.capture_selected && window_frame_count_ < UINT32_MAX) ++window_frame_count_;
    if (SUCCEEDED(result)) {
        LARGE_INTEGER marker_begin{}, marker_end{};
        if (!QueryPerformanceCounter(&marker_begin)) current_.invalid |= attribution_query_failed;
        const auto before = markers_.status().state;
        markers_.successful_present_boundary(current_.serial + 1, end.qpc);
        if (before == AttributionMarkerState::end_armed &&
            markers_.status().state == AttributionMarkerState::complete)
            window_flush_pending_ = true;
        if (QueryPerformanceCounter(&marker_end) && marker_begin.QuadPart != 0 &&
            marker_end.QuadPart >= marker_begin.QuadPart) {
            pending_marker_cpu_qpc_ = static_cast<std::uint64_t>(
                marker_end.QuadPart - marker_begin.QuadPart);
            pending_marker_cpu_qpc_valid_ = true;
        } else current_.invalid |= attribution_query_failed;
        next_begin_ = end;
        next_begin_valid_ = end.valid;
    }
}

void AttributionLedger::submit_first_partial(std::uint64_t serial, std::uint32_t interval,
                                             std::uint32_t flags, HRESULT result) noexcept {
    if (!enabled_) return;
    AttributionFrameRecord record{};
    record.serial = serial;
    record.process_creation_filetime_100ns = process_creation_filetime_100ns_;
    record.pid = GetCurrentProcessId();
    const bool same_thread = capture_present_thread();
    record.present_thread_id = present_thread_id_;
    record.qpc_frequency = qpc_frequency_;
    record.renderer_commit = commit_;
    record.renderer_sha256 = module_hash_;
    record.capture_id = capture_id_;
    record.present_interval = interval;
    record.present_flags = flags;
    record.present_hresult = result;
    const auto point = sample_point();
    record.qpc_begin = record.qpc_present_enter = record.qpc_present_return = point.qpc;
    record.invalid = attribution_first_partial | attribution_gpu_unresolved;
    if (!point.valid) record.invalid |= attribution_query_failed;
    if (!same_thread) record.invalid |= attribution_mixed_thread;
    if (FAILED(result)) record.invalid |= attribution_present_failed;
    if (SUCCEEDED(result)) {
        LARGE_INTEGER marker_begin{}, marker_end{};
        const BOOL marker_begin_ok = QueryPerformanceCounter(&marker_begin);
        markers_.successful_present_boundary(serial + 1, point.qpc);
        if (marker_begin_ok && QueryPerformanceCounter(&marker_end) &&
            marker_end.QuadPart >= marker_begin.QuadPart) {
            pending_marker_cpu_qpc_ = static_cast<std::uint64_t>(
                marker_end.QuadPart - marker_begin.QuadPart);
            pending_marker_cpu_qpc_valid_ = true;
        } else record.invalid |= attribution_query_failed;
    }
    if (SUCCEEDED(result) && point.valid && same_thread) {
        next_begin_ = point;
        next_begin_valid_ = true;
    }
}

void AttributionLedger::add_scope(const char* name, const AttributionCpuPoint& begin,
                                  const AttributionCpuPoint& end, std::uint64_t serial,
                                  std::uint64_t upload_events, std::uint64_t upload_bytes,
                                  std::uint64_t draws, std::uint64_t triangles,
                                  std::uint64_t batches, std::uint64_t capture_cpu_ticks,
                                  std::uint64_t capture_calls, std::uint64_t capture_bytes,
                                  std::uint64_t packets_created,
                                  std::uint64_t packets_appended) {
    if (!interval_open_) return;
    if (serial != current_.serial) { current_.invalid |= attribution_mixed_serial | attribution_cross_frame_scope; return; }
    if (current_.scope_count >= current_.scopes.size()) { current_.invalid |= attribution_scope_overflow; return; }
    auto& scope = current_.scopes[current_.scope_count++];
    strncpy_s(scope.name.data(), scope.name.size(), name ? name : "unnamed", _TRUNCATE);
    scope.serial = serial;
    scope.qpc_begin = begin.qpc;
    scope.qpc_end = end.qpc;
    scope.cpu = attribution_delta(begin, end);
    scope.upload_events = upload_events;
    scope.upload_bytes = upload_bytes;
    scope.draws = draws;
    scope.triangles = triangles;
    scope.batches = batches;
    scope.capture_cpu_ticks = capture_cpu_ticks;
    scope.capture_calls = capture_calls;
    scope.capture_bytes = capture_bytes;
    scope.packets_created = packets_created;
    scope.packets_appended = packets_appended;
    add_delta(current_.named, scope.cpu);
    if (!begin.valid || !end.valid) current_.invalid |= attribution_query_failed;
    if (GetCurrentThreadId() != present_thread_id_) current_.invalid |= attribution_mixed_thread;
}

void write_renderer_workload(FILE* file, const RendererWorkloadSnapshot& workload) {
    if (!workload.active()) return;
    const auto present = [&](RendererWorkloadField field) { return workload.has(field); };
    const auto value = [&](RendererWorkloadField field) {
        return static_cast<unsigned long long>(workload.get(field));
    };
    std::fputs(",\"renderer_workload\":{\"current_legacy_partial\":{", file);
    bool outer = false;
    const auto group = [&](const char* name, std::initializer_list<std::pair<RendererWorkloadField, const char*>> fields) {
        bool any = false;
        for (const auto& field : fields) any |= present(field.first);
        if (!any) return;
        std::fprintf(file, "%s\"%s\":{", outer ? "," : "", name);
        bool first = true;
        for (const auto& field : fields) if (present(field.first)) {
            std::fprintf(file, "%s\"%s\":%llu", first ? "" : ",", field.second,
                         value(field.first));
            first = false;
        }
        std::fputc('}', file);
        outer = true;
    };
    group("native_owned_queue_partial", {
        {RendererWorkloadField::native_opaque_draws_partial, "opaque_draws"},
        {RendererWorkloadField::native_cutout_draws_partial, "cutout_draws"}});
    group("world_object_simple_partial", {
        {RendererWorkloadField::world_draw_calls_partial, "world_draw_calls"},
        {RendererWorkloadField::object_draw_calls_partial, "object_draw_calls"},
        {RendererWorkloadField::simple_calls_partial, "simple_calls"}});
    group("figure_partial", {
        {RendererWorkloadField::figure_placed_calls_partial, "placed_calls"},
        {RendererWorkloadField::figure_lit_calls_partial, "lit_calls"},
        {RendererWorkloadField::figure_gpu_owned_calls_partial, "gpu_owned_calls"},
        {RendererWorkloadField::figure_cpu_fallback_calls_partial, "cpu_fallback_calls"}});
    group("terrain_partial", {
        {RendererWorkloadField::terrain_calls_partial, "ingress_calls"},
        {RendererWorkloadField::terrain_outer_calls_partial, "outer_calls"},
        {RendererWorkloadField::terrain_gpu_owned_calls_partial, "gpu_owned_calls"},
        {RendererWorkloadField::terrain_cpu_fallback_calls_partial, "cpu_fallback_calls"},
        {RendererWorkloadField::terrain_candidate_sectors_partial, "candidate_sectors"},
        {RendererWorkloadField::terrain_visible_sectors_partial, "visible_sectors"},
        {RendererWorkloadField::terrain_frustum_rejected_sectors_partial, "frustum_rejected_sectors"}});
    group("ui_blit_queue_partial", {
        {RendererWorkloadField::ui_blit_queue_cpu_qpc_partial, "queue_cpu_qpc"},
        {RendererWorkloadField::ui_blit_queue_calls_partial, "queue_calls"},
        {RendererWorkloadField::ui_blit_conversion_cpu_qpc_partial, "conversion_cpu_qpc"},
        {RendererWorkloadField::ui_blit_conversion_calls_partial, "conversion_calls"},
        {RendererWorkloadField::ui_blit_conversion_pixels_partial, "conversion_pixels"}});
    group("camp_scroll_temporary", {
        {RendererWorkloadField::camp_scroll_valid, "valid"},
        {RendererWorkloadField::camp_scroll_mode, "mode"},
        {RendererWorkloadField::camp_scroll_generation, "texture_manager_generation"},
        {RendererWorkloadField::camp_scroll_first_qpc, "first_qpc"},
        {RendererWorkloadField::camp_scroll_last_qpc, "last_qpc"},
        {RendererWorkloadField::camp_scroll_item_calls, "native_item_draw_calls"},
        {RendererWorkloadField::camp_scroll_item_wall_qpc, "native_item_draw_wall_qpc"},
        {RendererWorkloadField::camp_scroll_item_max_wall_qpc, "native_item_draw_max_wall_qpc"},
        {RendererWorkloadField::camp_scroll_miss_calls, "texture_miss_completion_calls"},
        {RendererWorkloadField::camp_scroll_miss_wall_qpc, "texture_miss_completion_wall_qpc"},
        {RendererWorkloadField::camp_scroll_miss_max_wall_qpc, "texture_miss_completion_max_wall_qpc"},
        {RendererWorkloadField::camp_scroll_figure_misses, "figure_miss_calls"},
        {RendererWorkloadField::camp_scroll_stash_first, "stash_first_item"},
        {RendererWorkloadField::camp_scroll_stash_count, "stash_item_count"},
        {RendererWorkloadField::camp_scroll_stash_category, "stash_category"},
        {RendererWorkloadField::camp_scroll_stash_columns, "stash_columns"},
        {RendererWorkloadField::camp_scroll_stash_rows, "stash_rows"},
        {RendererWorkloadField::camp_scroll_vendor_first, "vendor_first_item"},
        {RendererWorkloadField::camp_scroll_vendor_count, "vendor_item_count"},
        {RendererWorkloadField::camp_scroll_vendor_category, "vendor_category"},
        {RendererWorkloadField::camp_scroll_vendor_columns, "vendor_columns"},
        {RendererWorkloadField::camp_scroll_vendor_rows, "vendor_rows"}});
    std::fputc('}', file);
    const bool observer = present(RendererWorkloadField::profiler_observer_cpu_qpc) ||
        present(RendererWorkloadField::capture_marker_cpu_qpc);
    if (observer) {
        std::fputs(",\"observer\":{", file);
        bool first = true;
        if (present(RendererWorkloadField::profiler_observer_cpu_qpc)) {
            std::fprintf(file, "\"profiler_cpu_qpc\":%llu",
                         value(RendererWorkloadField::profiler_observer_cpu_qpc));
            first = false;
        }
        if (present(RendererWorkloadField::capture_marker_cpu_qpc))
            std::fprintf(file, "%s\"capture_marker_cpu_qpc\":%llu", first ? "" : ",",
                         value(RendererWorkloadField::capture_marker_cpu_qpc));
        std::fputc('}', file);
    }
    std::fprintf(file, ",\"presence_bitmap\":%llu}",
                 static_cast<unsigned long long>(workload.presence));
}

void AttributionLedger::set_renderer_workload(
    const RendererWorkloadSnapshot& workload) noexcept {
    if (!interval_open_) return;
    for (std::size_t index = 0;
         index < static_cast<std::size_t>(RendererWorkloadField::count); ++index) {
        const auto field = static_cast<RendererWorkloadField>(index);
        if (workload.has(field)) current_.renderer_workload.set(field, workload.get(field));
    }
}

bool AttributionLedger::arm_marker_begin(
    const AttributionMarkerSelection& selection) noexcept {
    return enabled_ && !window_flush_pending_ && markers_.arm_begin(selection);
}

bool AttributionLedger::arm_marker_end() noexcept {
    return enabled_ && markers_.arm_end();
}

AttributionMarkerStatus AttributionLedger::marker_status() const noexcept {
    auto result = markers_.status();
    result.window_flush_pending = window_flush_pending_;
    result.window_frame_count = window_frame_count_;
    return result;
}

AttributionFrameRecord AttributionLedger::take_current() noexcept {
    auto& workload=current_.renderer_workload;
    if(workload.has(RendererWorkloadField::camp_scroll_valid)&&
       workload.get(RendererWorkloadField::camp_scroll_valid)&&
       workload.get(RendererWorkloadField::camp_scroll_item_calls)){
        const auto first=workload.get(RendererWorkloadField::camp_scroll_first_qpc);
        const auto last=workload.get(RendererWorkloadField::camp_scroll_last_qpc);
        if(current_.qpc_begin<=0||current_.qpc_present_enter<current_.qpc_begin||
           first<static_cast<std::uint64_t>(current_.qpc_begin)||last<first||
           last>static_cast<std::uint64_t>(current_.qpc_present_enter)){
            // Preserve an explicit unavailable result, never a guessed serial join.
            for(auto index=static_cast<std::size_t>(RendererWorkloadField::camp_scroll_mode);
                index<=static_cast<std::size_t>(RendererWorkloadField::camp_scroll_vendor_rows);++index)
                workload.presence&=~(std::uint64_t{1}<<index);
            workload.set(RendererWorkloadField::camp_scroll_valid,0);
        }
    }
    return current_;
}

bool AttributionLedger::submit(const AttributionFrameRecord& record) noexcept {
    AttributionFrameRecord copy = record;
    if (!ring_) return false;
    if (dropped_ || ring_->overflowed()) copy.invalid |= attribution_dropped_record;
    if (!ring_->push(copy)) {
        dropped_ = true;
        return false;
    }
    else if (!frames_diagnostic_logged_) {
        frames_diagnostic_logged_ = true;
        diagnostic("ActiveFrames", "records=%zu", ring_->size());
    }
    return ring_->size() == kAttributionRingCapacity;
}

bool AttributionLedger::finalize_if_full(const char* output_root) noexcept {
    if (!enabled_ || !ring_ || ring_->size() != kAttributionRingCapacity) return false;
    diagnostic("CapacityFinalizationReached", "records=%zu", ring_->size());
    dropped_ = true;
    markers_.fail_capacity();
    bool flushed = false;
    try {
        flushed = flush(output_root);
    } catch (...) {
        diagnostic("CapacityFinalizationException", "records=%u", kAttributionRingCapacity);
    }
    diagnostic(flushed ? "CapacityFinalizationComplete" : "CapacityFinalizationFailed",
               "records=%u", kAttributionRingCapacity);
    disable_capture();
    return true;
}

bool AttributionLedger::flush_completed_window(const char* output_root) noexcept {
    if (!enabled_ || !window_flush_pending_ || !ring_ || !ring_->size()) return false;
    bool flushed = false;
    try { flushed = flush(output_root); }
    catch (...) { diagnostic("CompletedWindowFlushException", "records=%zu", ring_->size()); }
    if (!flushed) {
        diagnostic("CompletedWindowFlushFailure", "records=%zu", ring_->size());
        disable_capture();
        return false;
    }
    diagnostic("CompletedWindowFlushSuccess", "records=%zu", ring_->size());
    ring_->clear();
    window_flush_pending_ = false;
    window_frame_count_ = 0;
    frames_diagnostic_logged_ = false;
    dropped_ = false;
    return true;
}

void AttributionLedger::shutdown(const char* output_root) {
    if (enabled_) {
        markers_.fail_if_open();
        if (interval_open_) {
            const bool capture_selected = current_.capture_selected;
            current_.invalid |= attribution_open_scope | attribution_first_partial;
            record_present_return(E_ABORT);
            if (capture_selected) submit(current_);
        }
        diagnostic("ShutdownFlushPathReached", "records=%zu", ring_ ? ring_->size() : 0u);
        (void)flush(output_root);
    }
    disable_capture();
}

void AttributionLedger::disable_capture() noexcept {
    if (present_thread_) CloseHandle(present_thread_);
    present_thread_ = nullptr;
    present_thread_id_ = 0;
    enabled_ = interval_open_ = false;
    next_begin_valid_ = device_open_ = false;
    frames_diagnostic_logged_ = false;
    dropped_ = false;
    window_flush_pending_ = false;
    pending_marker_cpu_qpc_valid_ = false;
    ring_.reset();
}

bool AttributionLedger::flush(const char* output_root) {
    if (!ring_) {
        diagnostic("FlushSkippedNoFrames");
        return false;
    }
    if (!output_root || !*output_root) {
        diagnostic("DirectoryCreationFailure", "path=<empty> error=%lu", ERROR_INVALID_NAME);
        return false;
    }
    const auto ensure_directory = [this](const char* path) {
        if (CreateDirectoryA(path, nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) return true;
        diagnostic("DirectoryCreationFailure", "path=%s error=%lu", path, GetLastError());
        return false;
    };
    if (!ensure_directory(output_root)) return false;
    const std::string outputs = std::string(output_root) + "\\outputs";
    const std::string directory = outputs + "\\attribution";
    if (!ensure_directory(outputs.c_str()) || !ensure_directory(directory.c_str())) return false;
    if (!ring_->size()) {
        const bool markers_flushed = flush_markers(directory.c_str());
        diagnostic("FlushSkippedNoFrames", "markers=%s",
                   markers_flushed ? "written_or_empty" : "invalid");
        return markers_flushed;
    }
    const std::string path = directory + "\\renderer-ledger.jsonl";
    FILE* file = nullptr;
    const errno_t open_error = fopen_s(&file, path.c_str(), ledger_written_ ? "ab" : "wb");
    if (!file) {
        diagnostic("FileOpenFailure", "path=%s error=%d", path.c_str(), open_error);
        return false;
    }
    for (std::size_t index = 0; index < ring_->size(); ++index) {
        const auto& r = ring_->at(index);
        std::fprintf(file,
            "{\"serial\":%llu,\"pid\":%u,\"process_creation_filetime_100ns\":%llu,\"present_thread_id\":%u,"
            "\"capture_capacity_frames\":%u,\"frame_record_bytes\":%zu,"
            "\"qpc_begin\":%lld,\"qpc_present_enter\":%lld,\"qpc_present_return\":%lld,\"qpc_frequency\":%llu,"
            "\"present_interval\":%u,\"present_flags\":%u,\"present_hresult\":%d,"
            "\"renderer_commit\":\"%s\",\"renderer_sha256\":\"%s\","
            "\"capture_id\":\"%s\",\"capture_mode\":\"marked_windows_v1\",\"invalid\":%u,"
            "\"cpu\":{\"present_to_present\":{\"wall_qpc\":%lld,\"present_thread_kernel_100ns\":%lld,\"present_thread_user_100ns\":%lld,"
            "\"present_thread_cycles\":%lld,\"process_kernel_100ns\":%lld,\"process_user_100ns\":%lld,\"process_cycles\":%lld},"
            "\"renderer_device_span\":{\"wall_qpc\":%lld,\"present_thread_kernel_100ns\":%lld,\"present_thread_user_100ns\":%lld,\"present_thread_cycles\":%lld,\"process_kernel_100ns\":%lld,\"process_user_100ns\":%lld,\"process_cycles\":%lld},"
            "\"named_pass_union\":{\"wall_qpc\":%lld,\"present_thread_kernel_100ns\":%lld,\"present_thread_user_100ns\":%lld,\"present_thread_cycles\":%lld,\"process_kernel_100ns\":%lld,\"process_user_100ns\":%lld,\"process_cycles\":%lld},"
            "\"renderer_gap\":{\"wall_qpc\":%lld,\"present_thread_kernel_100ns\":%lld,\"present_thread_user_100ns\":%lld,\"present_thread_cycles\":%lld,\"process_kernel_100ns\":%lld,\"process_user_100ns\":%lld,\"process_cycles\":%lld},"
            "\"outside_renderer\":{\"wall_qpc\":%lld,\"present_thread_kernel_100ns\":%lld,\"present_thread_user_100ns\":%lld,\"present_thread_cycles\":%lld,\"process_kernel_100ns\":%lld,\"process_user_100ns\":%lld,\"process_cycles\":%lld},"
            "\"present_thread_minus_process_kernel_100ns\":%lld,\"present_thread_minus_process_user_100ns\":%lld,\"present_thread_minus_process_cycles\":%lld},"
            "\"gpu\":{\"device_command_span_ticks\":%llu,\"named_pass_union_ticks\":%llu,"
            "\"gap_ticks\":%lld,\"frequency\":%llu},\"scopes\":[",
            static_cast<unsigned long long>(r.serial), r.pid,
            static_cast<unsigned long long>(r.process_creation_filetime_100ns), r.present_thread_id,
            kAttributionRingCapacity, sizeof(AttributionFrameRecord),
            static_cast<long long>(r.qpc_begin), static_cast<long long>(r.qpc_present_enter),
            static_cast<long long>(r.qpc_present_return), static_cast<unsigned long long>(r.qpc_frequency),
            r.present_interval, r.present_flags,
            r.present_hresult, r.renderer_commit.data(), r.renderer_sha256.data(), r.capture_id.data(),
            r.invalid | (dropped_ ? attribution_dropped_record : 0u),
            static_cast<long long>(r.totals.qpc), static_cast<long long>(r.totals.thread_kernel_100ns),
            static_cast<long long>(r.totals.thread_user_100ns), static_cast<long long>(r.totals.thread_cycles),
            static_cast<long long>(r.totals.process_kernel_100ns), static_cast<long long>(r.totals.process_user_100ns),
            static_cast<long long>(r.totals.process_cycles),
            static_cast<long long>(r.device.qpc), static_cast<long long>(r.device.thread_kernel_100ns), static_cast<long long>(r.device.thread_user_100ns), static_cast<long long>(r.device.thread_cycles), static_cast<long long>(r.device.process_kernel_100ns), static_cast<long long>(r.device.process_user_100ns), static_cast<long long>(r.device.process_cycles),
            static_cast<long long>(r.named.qpc), static_cast<long long>(r.named.thread_kernel_100ns), static_cast<long long>(r.named.thread_user_100ns), static_cast<long long>(r.named.thread_cycles), static_cast<long long>(r.named.process_kernel_100ns), static_cast<long long>(r.named.process_user_100ns), static_cast<long long>(r.named.process_cycles),
            static_cast<long long>(r.device_gap.qpc), static_cast<long long>(r.device_gap.thread_kernel_100ns), static_cast<long long>(r.device_gap.thread_user_100ns), static_cast<long long>(r.device_gap.thread_cycles), static_cast<long long>(r.device_gap.process_kernel_100ns), static_cast<long long>(r.device_gap.process_user_100ns), static_cast<long long>(r.device_gap.process_cycles),
            static_cast<long long>(r.outside_device.qpc), static_cast<long long>(r.outside_device.thread_kernel_100ns), static_cast<long long>(r.outside_device.thread_user_100ns), static_cast<long long>(r.outside_device.thread_cycles), static_cast<long long>(r.outside_device.process_kernel_100ns), static_cast<long long>(r.outside_device.process_user_100ns), static_cast<long long>(r.outside_device.process_cycles),
            static_cast<long long>(r.present_minus_process_kernel_100ns),
            static_cast<long long>(r.present_minus_process_user_100ns), static_cast<long long>(r.present_minus_process_cycles),
            static_cast<unsigned long long>(r.gpu_span_ticks), static_cast<unsigned long long>(r.gpu_named_ticks),
            static_cast<long long>(r.gpu_gap_ticks), static_cast<unsigned long long>(r.gpu_frequency));
        for (std::uint32_t scope = 0; scope < r.scope_count; ++scope) {
            const auto& s = r.scopes[scope];
            std::fprintf(file, "%s{\"name\":\"%s\",\"serial\":%llu,\"qpc_begin\":%lld,\"qpc_end\":%lld,\"wall_qpc\":%lld,\"present_thread_kernel_100ns\":%lld,"
                "\"present_thread_user_100ns\":%lld,\"present_thread_cycles\":%lld,\"process_kernel_100ns\":%lld,\"process_user_100ns\":%lld,\"process_cycles\":%lld,"
                "\"gpu_resolved\":%s,\"gpu_ticks\":%llu,\"upload_events\":%llu,\"upload_bytes\":%llu,"
                "\"draws\":%llu,\"triangles\":%llu,\"batches\":%llu,\"capture_cpu_ticks\":%llu,"
                "\"capture_calls\":%llu,\"capture_bytes\":%llu,\"packets_created\":%llu,\"packets_appended\":%llu}", scope ? "," : "", s.name.data(),
                static_cast<unsigned long long>(s.serial),
                static_cast<long long>(s.qpc_begin), static_cast<long long>(s.qpc_end),
                static_cast<long long>(s.cpu.qpc), static_cast<long long>(s.cpu.thread_kernel_100ns),
                static_cast<long long>(s.cpu.thread_user_100ns), static_cast<long long>(s.cpu.thread_cycles),
                static_cast<long long>(s.cpu.process_kernel_100ns), static_cast<long long>(s.cpu.process_user_100ns),
                static_cast<long long>(s.cpu.process_cycles), s.gpu_resolved ? "true" : "false",
                static_cast<unsigned long long>(s.gpu_ticks), static_cast<unsigned long long>(s.upload_events),
                static_cast<unsigned long long>(s.upload_bytes), static_cast<unsigned long long>(s.draws),
                static_cast<unsigned long long>(s.triangles), static_cast<unsigned long long>(s.batches),
                static_cast<unsigned long long>(s.capture_cpu_ticks),
                static_cast<unsigned long long>(s.capture_calls),
                static_cast<unsigned long long>(s.capture_bytes),
                static_cast<unsigned long long>(s.packets_created),
                static_cast<unsigned long long>(s.packets_appended));
        }
        std::fputc(']', file);
        write_renderer_workload(file, r.renderer_workload);
        std::fputs("}\n", file);
    }
    const bool write_failed = std::ferror(file) != 0 || std::fflush(file) != 0;
    const int close_result = std::fclose(file);
    if (write_failed || close_result != 0) {
        if (!ledger_written_) DeleteFileA(path.c_str());
        diagnostic("FileWriteFailure", "path=%s", path.c_str());
        return false;
    }
    ledger_written_ = true;
    const bool markers_flushed = flush_markers(directory.c_str());
    diagnostic("FlushSuccess", "records=%zu path=%s markers=%s", ring_->size(), path.c_str(),
               markers_flushed ? "written_or_empty" : "invalid");
    return markers_flushed;
}

bool AttributionLedger::flush_markers(const char* directory) {
    const auto status = markers_.status();
    if (status.state == AttributionMarkerState::inactive && markers_.size() == 0) return true;
    const bool valid = status.state == AttributionMarkerState::complete ||
        (status.state == AttributionMarkerState::inactive && markers_.size() != 0);
    const std::string path = std::string(directory) +
        (valid ? "\\renderer-markers.json" : "\\renderer-markers.invalid.json");
    const std::string counterpart = std::string(directory) +
        (valid ? "\\renderer-markers.invalid.json" : "\\renderer-markers.json");
    FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "wb") != 0 || !file) {
        diagnostic("MarkerFileOpenFailure", "path=%s", path.c_str());
        return false;
    }
    if (!valid) {
        std::fprintf(file, "{\"capture_id\":\"%s\",\"valid\":false,\"state\":%u,"
                     "\"completed\":%u,\"capacity\":%u}\n", capture_id_.data(),
                     static_cast<unsigned>(status.state), status.completed, status.capacity);
    } else {
        std::fprintf(file, "{\"capture_id\":\"%s\",\"valid\":true,\"segments\":[",
                     capture_id_.data());
        for (std::size_t index = 0; index < markers_.size(); ++index) {
            const auto& marker = markers_.at(index);
            std::fprintf(file,
                "%s{\"label\":\"%s\",\"begin_frame_serial\":%llu,"
                "\"end_frame_serial_exclusive\":%llu,\"begin_qpc\":%lld,"
                "\"end_qpc_exclusive\":%lld",
                index ? "," : "", attribution_marker_label_name(marker.selection.label),
                static_cast<unsigned long long>(marker.begin_frame_serial),
                static_cast<unsigned long long>(marker.end_frame_serial_exclusive),
                static_cast<long long>(marker.begin_qpc),
                static_cast<long long>(marker.end_qpc_exclusive));
            if (marker.selection.issue13_menu_transition)
                std::fprintf(file,
                    ",\"menu_label\":\"%s\",\"opening_ordinal\":%u,\"repetition_state\":\"%s\"",
                    marker.selection.menu_label.data(), marker.selection.opening_ordinal,
                    attribution_marker_repetition_name(marker.selection.repetition));
            std::fputc('}', file);
        }
        std::fputs("]}\n", file);
    }
    const bool write_failed = std::ferror(file) != 0 || std::fflush(file) != 0;
    const bool failed = std::fclose(file) != 0 || write_failed;
    if (failed) {
        DeleteFileA(path.c_str());
        diagnostic("MarkerFileWriteFailure", "path=%s", path.c_str());
        return false;
    }
    if (GetFileAttributesA(counterpart.c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (marker_file_matches_capture_id(counterpart.c_str(), capture_id_.data())) {
            if (!DeleteFileA(counterpart.c_str()))
                diagnostic("MarkerCounterpartDeleteFailure", "path=%s error=%lu",
                           counterpart.c_str(), GetLastError());
        } else {
            diagnostic("MarkerCounterpartPreservedDifferentCapture", "path=%s",
                       counterpart.c_str());
        }
    }
    diagnostic(valid ? "MarkerFlushSuccess" : "MarkerFlushInvalid", "path=%s completed=%u",
               path.c_str(), status.completed);
    return valid;
}

} // namespace owned
