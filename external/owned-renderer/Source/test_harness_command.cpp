#include "test_harness_command.h"

#include <charconv>
#include <cmath>
#include <vector>

namespace owned {
namespace {

std::vector<std::string_view> words(std::string_view line) {
    std::vector<std::string_view> result;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' ||
                                   line[i] == '\r' || line[i] == '\n')) ++i;
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t' &&
               line[i] != '\r' && line[i] != '\n') ++i;
        if (i > start) result.push_back(line.substr(start, i - start));
    }
    return result;
}

bool number(std::string_view text, float& value) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() && std::isfinite(value);
}

bool whole(std::string_view text, std::uint32_t& value) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

// File stems and option keys: no separators, so nothing escapes the folder.
bool plain_name(std::string_view text) {
    if (text.empty() || text.size() > 64 || text == "." || text == "..") return false;
    for (const char c : text)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.')) return false;
    return true;
}

HarnessCommand rejected(const char* why) {
    HarnessCommand command;
    command.error = why;
    return command;
}

} // namespace

bool harness_line_skipped(std::string_view line) noexcept {
    for (const char c : line) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        return c == '#';
    }
    return true;
}

HarnessCommand parse_harness_command(std::string_view line) {
    const auto w = words(line);
    if (w.empty()) return rejected("empty");
    const std::string_view verb = w[0];
    HarnessCommand command;

    if (verb == "load_save" || verb == "console") {
        // These arguments are the remainder of a line, not individual words.
        // Console quotes and internal whitespace belong to the game parser.
        const auto start = line.find_first_not_of(" \t", line.find(verb) + verb.size());
        if (start == std::string_view::npos)
            return rejected(verb == "load_save" ? "usage: load_save <save name or slot file>"
                                                : "usage: console <text>");
        auto argument = line.substr(start);
        while (!argument.empty() && (argument.back() == '\r' || argument.back() == '\n'))
            argument.remove_suffix(1);
        if (argument.empty() || argument.find_first_of("\r\n") != std::string_view::npos ||
            argument.find('\0') != std::string_view::npos)
            return rejected("command text must be one nonempty line without NUL bytes");
        if (verb == "load_save") {
            while (!argument.empty() && (argument.back() == ' ' || argument.back() == '\t'))
                argument.remove_suffix(1);
            if (argument.size() >= 2 && argument.front() == '"' && argument.back() == '"') {
                argument.remove_prefix(1);
                argument.remove_suffix(1);
            }
            if (argument.empty()) return rejected("load_save: empty save selector");
        }
        command.name = argument;
        command.verb = verb == "load_save" ? HarnessVerb::load_save : HarnessVerb::console;
    } else if (verb == "camera") {
        if (w.size() != 6) return rejected("usage: camera x y yaw pitch zoom");
        for (int i = 0; i < 5; ++i)
            if (!number(w[i + 1], command.number[i])) return rejected("camera: bad number");
        if (command.number[3] < -90.0f || command.number[3] > 90.0f)
            return rejected("camera: pitch outside -90..90 degrees");
        if (command.number[4] <= 0.0f) return rejected("camera: zoom must be positive");
        command.verb = HarnessVerb::camera;
    } else if (verb == "camera_path") {
        // Temporary (camera-movement frame cost): frames, then the target as
        // x y yaw dist pitch, stored in the camera order (x y yaw pitch zoom).
        float values[5]{};
        if (w.size() != 7 || !whole(w[1], command.frames) || command.frames == 0 || command.frames > 100000)
            return rejected("usage: camera_path <frames 1..100000> x y yaw dist pitch");
        for (int i = 0; i < 5; ++i)
            if (!number(w[i + 2], values[i])) return rejected("camera_path: bad number");
        if (values[4] < -90.0f || values[4] > 90.0f) return rejected("camera_path: pitch outside -90..90 degrees");
        if (values[3] <= 0.0f) return rejected("camera_path: dist must be positive");
        command.number[0] = values[0]; command.number[1] = values[1]; command.number[2] = values[2];
        command.number[3] = values[4]; command.number[4] = values[3];
        command.verb = HarnessVerb::camera_path;
    } else if (verb == "state" || verb == "capture" || verb == "dump_units" ||
               verb == "list_saves" || verb == "quit" || verb == "main_menu") {
        if (w.size() != 1) return rejected("this command takes no arguments");
        command.verb = verb == "state" ? HarnessVerb::state :
                       verb == "capture" ? HarnessVerb::capture :
                       verb == "dump_units" ? HarnessVerb::dump_units :
                       verb == "list_saves" ? HarnessVerb::list_saves :
                       verb == "main_menu" ? HarnessVerb::main_menu : HarnessVerb::quit;
    } else if (verb == "clock") {
        if (w.size() != 2) return rejected("usage: clock <hour> | clock release");
        if (w[1] == "release") {
            command.verb = HarnessVerb::clock_release;
        } else {
            if (!number(w[1], command.number[0]) || command.number[0] < 0.0f ||
                command.number[0] >= 24.0f) return rejected("clock: hour must be in [0, 24)");
            command.verb = HarnessVerb::clock_pin;
        }
    } else if (verb == "wait") {
        if (w.size() != 2 || !whole(w[1], command.frames) || command.frames == 0 ||
            command.frames > 100000) return rejected("usage: wait <presents 1..100000>");
        command.verb = HarnessVerb::wait;
    } else if (verb == "pause" || verb == "godmode") {
        if (w.size() != 2 || (w[1] != "on" && w[1] != "off"))
            return rejected(verb == "pause" ? "usage: pause on|off" : "usage: godmode on|off");
        command.frames = w[1] == "on" ? 1u : 0u;
        command.verb = verb == "pause" ? HarnessVerb::pause : HarnessVerb::godmode;
    } else if (verb == "wait_ready") {
        std::uint32_t seconds = kHarnessReadyDefaultSeconds;
        if (w.size() > 2 || (w.size() == 2 && !whole(w[1], seconds)) ||
            seconds < 1 || seconds > 3600) return rejected("usage: wait_ready [seconds 1..3600]");
        command.frames = seconds;
        command.verb = HarnessVerb::wait_ready;
    } else if (verb == "profile") {
        std::uint32_t seconds = 5;
        if (w.size() > 2 || (w.size() == 2 && !whole(w[1], seconds)) ||
            seconds < 1 || seconds > 30) return rejected("usage: profile [seconds 1..30]");
        command.number[0] = static_cast<float>(seconds);
        command.verb = HarnessVerb::profile;
    } else if (verb == "screenshot") {
        if (w.size() != 2 || !plain_name(w[1]))
            return rejected("usage: screenshot <name of [A-Za-z0-9_.-]>");
        command.name = w[1];
        command.verb = HarnessVerb::screenshot;
    } else if (verb == "config") {
        if (w.size() != 3 || !plain_name(w[1]) || !plain_name(w[2]))
            return rejected("usage: config <key> <value>");
        command.name = w[1];
        command.value = w[2];
        command.verb = HarnessVerb::config;
    } else {
        return rejected("unknown command");
    }
    return command;
}

bool harness_load_completed(HarnessLoadWait& wait, std::uint64_t epoch,
                            std::uint64_t completed_draws, bool transition_pending) noexcept {
    if (transition_pending || epoch == wait.request_epoch) return false;
    if (!wait.new_epoch_seen) {
        wait.new_epoch_seen = true;
        wait.draws_at_new_epoch = completed_draws;
        return false;
    }
    return completed_draws > wait.draws_at_new_epoch;
}

bool harness_load_retry(bool zone_matches, bool loaded_zone_empty, bool& retried) noexcept {
    if (zone_matches || !loaded_zone_empty || retried) return false;
    retried = true;
    return true;
}

bool harness_timed_out(std::uint64_t start_ms, std::uint64_t now_ms,
                       std::uint32_t seconds) noexcept {
    return now_ms >= start_ms && now_ms - start_ms >= std::uint64_t{seconds} * 1000u;
}

} // namespace owned
