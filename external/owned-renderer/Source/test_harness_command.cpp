#include "test_harness_command.h"

#include <charconv>
#include <limits>
#include <cmath>
#include <cstdio>
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

    if (verb == "load_save" || verb == "console" || verb == "note") {
        // These arguments are the remainder of a line, not individual words.
        // Console quotes and internal whitespace belong to the game parser.
        const auto start = line.find_first_not_of(" \t", line.find(verb) + verb.size());
        if (start == std::string_view::npos)
            return rejected(verb == "load_save" ? "usage: load_save <save name or slot file>"
                            : verb == "note" ? "usage: note <text>" : "usage: console <text>");
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
        command.verb = verb == "load_save" ? HarnessVerb::load_save :
                       verb == "note" ? HarnessVerb::note : HarnessVerb::console;
    } else if (verb == "walk") {
        // The game's own move order for the party leader (script MoveToPoint:
        // the unit logic's StartMove, real steps), through the console parser
        // like @SetCP; needs the console's script access (thingamabob) first.
        // Completes on dispatch; the route waits for the walk with wait.
        float x = 0, y = 0;
        if (w.size() != 3 || !number(w[1], x) || !number(w[2], y) || x < 0 || y < 0)
            return rejected("usage: walk <x> <y> (world metres)");
        char text[96];
        std::snprintf(text, sizeof(text), "@MoveToPoint(GetLeader(),%.3f,%.3f)", x, y);
        command.name = text;
        command.verb = HarnessVerb::console;
    } else if (verb == "camera") {
        if (w.size() >= 2 && w[1] == "hero") {
            if (w.size() != 2 && w.size() != 5)
                return rejected("usage: camera hero [yaw pitch zoom]");
            for (int i = 0; i < 5; ++i) command.number[i] = std::numeric_limits<float>::quiet_NaN();
            if (w.size() == 5) {
                for (int i = 2; i < 5; ++i)
                    if (i == 2 && w[i] == "-") continue;
                    else if (!number(w[i], command.number[i])) return rejected("camera hero: bad number");
                if (command.number[3] < -90.0f || command.number[3] > 90.0f)
                    return rejected("camera hero: pitch outside -90..90 degrees");
                if (command.number[4] <= 0.0f) return rejected("camera hero: zoom must be positive");
            }
            command.verb = HarnessVerb::camera_hero;
            return command;
        }
        if (w.size() != 6) return rejected("usage: camera x y yaw pitch zoom (x y yaw may be - : keep the current one)");
        for (int i = 0; i < 5; ++i)
            if (i < 3 && w[i + 1] == "-") command.number[i] = std::numeric_limits<float>::quiet_NaN();
            else if (!number(w[i + 1], command.number[i])) return rejected("camera: bad number");
        if (command.number[3] < -90.0f || command.number[3] > 90.0f)
            return rejected("camera: pitch outside -90..90 degrees");
        if (command.number[4] <= 0.0f) return rejected("camera: zoom must be positive");
        command.verb = HarnessVerb::camera;
    } else if (verb == "camera_unit") {
        // The unit by id, or by place: @x,y is the unit nearest that world
        // position (name "@", position in number[3..4]).
        std::int32_t id = 0;
        bool target = false;
        if (w.size() == 5 && w[1].size() > 1 && w[1].front() == '@') {
            const std::string_view place = w[1].substr(1);
            const auto comma = place.find(',');
            target = comma != std::string_view::npos && number(place.substr(0, comma), command.number[3]) &&
                     number(place.substr(comma + 1), command.number[4]);
            command.name = "@";
        } else if (w.size() == 5) {
            const auto [end, error] = std::from_chars(w[1].data(), w[1].data() + w[1].size(), id);
            target = error == std::errc() && end == w[1].data() + w[1].size();
        }
        if (!target || !number(w[2], command.number[0]) || !number(w[3], command.number[1]) || !number(w[4], command.number[2]))
            return rejected("usage: camera_unit <unit id (dump_units) | @x,y (nearest unit)> <yaw> <pitch> <distance>");
        if (command.number[1] < -90.0f || command.number[1] > 90.0f)
            return rejected("camera_unit: pitch outside -90..90 degrees");
        if (command.number[2] <= 0.0f) return rejected("camera_unit: distance must be positive");
        command.frames = static_cast<std::uint32_t>(id);
        command.verb = HarnessVerb::camera_unit;
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
    } else if (verb == "equip_cycle") {
        std::uint32_t steps = 0, between = 0;
        if (w.size() != 3 || !whole(w[1], steps) || !whole(w[2], between) || steps == 0 || steps > 10000 ||
            between == 0 || between > 600)
            return rejected("usage: equip_cycle <steps 1..10000> <presents_between 1..600>");
        command.frames = steps * between;
        command.number[0] = static_cast<float>(between);
        command.verb = HarnessVerb::equip_cycle;
    } else if (verb == "outfit_equip" || verb == "scroll_items") {
        std::uint32_t steps = 0, between = 0;
        bool negative = false;
        std::string_view count = w.size() == 3 ? w[1] : std::string_view{};
        if (verb == "scroll_items" && !count.empty() && count.front() == '-') { negative = true; count.remove_prefix(1); }
        if (w.size() != 3 || !whole(count, steps) || !whole(w[2], between) || steps == 0 || steps > 10000 ||
            between == 0 || between > 600)
            return rejected(verb == "outfit_equip" ? "usage: outfit_equip <steps 1..10000> <presents_between 1..600>"
                                                   : "usage: scroll_items <[-]steps 1..10000> <presents_between 1..600>");
        command.frames = steps * between;
        command.number[0] = static_cast<float>(between);
        command.number[1] = negative ? -1.0f : 1.0f;
        command.verb = verb == "outfit_equip" ? HarnessVerb::outfit_equip : HarnessVerb::scroll_items;
    } else if (verb == "open_outfitting") {
        // Optional shop number (base maps): 0 or none opens the camp without a shop.
        if (w.size() > 2 || (w.size() == 2 && (!whole(w[1], command.frames) || command.frames > 100000)))
            return rejected("usage: open_outfitting [shop 0..100000]");
        command.verb = HarnessVerb::open_outfitting;
    } else if (verb == "state" || verb == "capture" || verb == "dump_units" ||
               verb == "list_saves" || verb == "quit" || verb == "main_menu") {
        if (w.size() != 1) return rejected("this command takes no arguments");
        command.verb = verb == "state" ? HarnessVerb::state :
                       verb == "capture" ? HarnessVerb::capture :
                       verb == "dump_units" ? HarnessVerb::dump_units :
                       verb == "list_saves" ? HarnessVerb::list_saves :
                       verb == "main_menu" ? HarnessVerb::main_menu : HarnessVerb::quit;
    } else if (verb == "diagnostics_package") {
        // Shift+F11 without a key: the F11 capture, packed into the testers' zip.
        if (w.size() != 1) return rejected("this command takes no arguments");
        command.verb = HarnessVerb::capture;
        command.name = "package";
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
    } else if (verb == "spawn") {
        // The prototype may hold spaces: "..." quotes it.
        std::string_view rest = line.substr(line.find(verb) + verb.size());
        const auto skip = [&rest] { while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) rest.remove_prefix(1); };
        const auto token = [&rest, &skip]() -> std::string_view {
            skip();
            if (!rest.empty() && rest.front() == '"') {
                const auto close = rest.find('"', 1);
                if (close == std::string_view::npos) return {};
                const auto out = rest.substr(1, close - 1);
                rest.remove_prefix(close + 1);
                return out;
            }
            std::size_t n = 0;
            while (n < rest.size() && rest[n] != ' ' && rest[n] != '\t') ++n;
            const auto out = rest.substr(0, n);
            rest.remove_prefix(n);
            return out;
        };
        static const char kUsage[] =
            "usage: spawn <prototype | \"prototype\"> <x> <y> [yaw=deg] [player=0..31] [model=mask] [prim=texture] [sec=texture]";
        const auto prototype = token();
        if (prototype.empty() || prototype.size() > 63 || prototype.find_first_of("\r\n") != std::string_view::npos)
            return rejected(kUsage);
        if (!number(token(), command.number[0]) || !number(token(), command.number[1])) return rejected(kUsage);
        command.name = std::string(prototype);
        std::string model, prim, sec;
        for (auto option = token(); !option.empty(); option = token()) {
            const auto eq = option.find('=');
            if (eq == std::string_view::npos || eq + 1 == option.size()) return rejected(kUsage);
            const auto key = option.substr(0, eq), value = option.substr(eq + 1);
            if (key == "yaw") { if (!number(value, command.number[2])) return rejected(kUsage); }
            else if (key == "player") {
                if (!whole(value, command.frames) || command.frames > 31) return rejected(kUsage);
            } else if ((key == "model" || key == "prim" || key == "sec") && plain_name(value)) {
                (key == "model" ? model : key == "prim" ? prim : sec) = std::string(value);
            } else return rejected(kUsage);
        }
        command.value = model + "\n" + prim + "\n" + sec;
        command.verb = HarnessVerb::spawn;
    } else if (verb == "drawall") {
        if (w.size() != 2 || (w[1] != "on" && w[1] != "off")) return rejected("usage: drawall on|off");
        command.frames = w[1] == "on" ? 1u : 0u;
        command.verb = HarnessVerb::drawall;
    } else if (verb == "pause" || verb == "godmode") {
        if (w.size() != 2 || (w[1] != "on" && w[1] != "off"))
            return rejected(verb == "pause" ? "usage: pause on|off" : "usage: godmode on|off");
        command.frames = w[1] == "on" ? 1u : 0u;
        command.verb = verb == "pause" ? HarnessVerb::pause : HarnessVerb::godmode;
    } else if (verb == "expect") {
        if (w.size() == 2 && (w[1] == "godmode" || w[1] == "hero_in_view")) {
            command.verb = w[1] == "godmode" ? HarnessVerb::expect_godmode : HarnessVerb::expect_hero_in_view;
        } else if (w.size() == 3 && (w[1] == "event_absent" || w[1] == "event_present") && plain_name(w[2])) {
            command.name = w[2];
            command.verb = w[1] == "event_absent" ? HarnessVerb::expect_event_absent : HarnessVerb::expect_event_present;
        } else if (w.size() == 5 && w[1] == "value" && plain_name(w[2])) {
            if (w[3] != "==" && w[3] != "!=" && w[3] != "<" && w[3] != "<=" && w[3] != ">" && w[3] != ">=")
                return rejected("expect value: operator must be == != < <= > >=");
            const auto [end, error] = std::from_chars(w[4].data(), w[4].data() + w[4].size(), command.expected);
            if (error != std::errc{} || end != w[4].data() + w[4].size() || !std::isfinite(command.expected))
                return rejected("expect value: number must be finite");
            command.name = w[2]; command.value = std::string(w[3]);
            command.verb = HarnessVerb::expect_value;
        } else return rejected("usage: expect godmode|hero_in_view | expect event_absent|event_present <name> | expect value <name> <op> <number>");
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
