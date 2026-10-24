#pragma once
#include "SocialStealthRules.h"
#include <lua.hpp>
#include <cmath>

namespace SocialStealth {
// Private registry storage; configuration is loaded once, never on a MOB query.
inline void LoadConfig(lua_State* state, const std::string& path) {
    const int top = lua_gettop(state);
    if (luaL_loadfile(state, path.c_str()) || lua_pcall(state, 0, 1, 0)) {
        const char* error = lua_tostring(state, -1);
        lua_pushstring(state, error ? error : "config-load-error");
    } else if (lua_type(state, -1) == LUA_TTABLE) {
        lua_pushvalue(state, -1);
    } else {
        lua_pushliteral(state, "social_stealth.lua must return a table");
    }
    lua_setfield(state, LUA_REGISTRYINDEX, "JMV.SocialStealth");
    lua_settop(state, top);
}
inline bool ReadConfig(lua_State* state, const char* key, Config& config, std::string& error) {
    config = Config();
    error.clear();
    if (!state || !key || !*key) { error = "missing-profile"; return false; }
    const int top = lua_gettop(state);
    struct Restore { lua_State* state; int top; ~Restore() { lua_settop(state, top); } } restore{state, top};
    lua_getfield(state, LUA_REGISTRYINDEX, "JMV.SocialStealth");
    if (lua_type(state, -1) != LUA_TTABLE) {
        const char* message = lua_type(state, -1) == LUA_TSTRING ? lua_tostring(state, -1) : nullptr;
        error = message ? message : "config-unavailable";
        return false;
    }
    lua_pushstring(state, key);
    lua_rawget(state, -2);
    if (lua_type(state, -1) != LUA_TTABLE) { error = "unknown-profile"; return false; }
    lua_pushliteral(state, "minimum"); lua_rawget(state, -2);
    const double minimum = lua_tonumber(state, -1);
    if (lua_type(state, -1) != LUA_TNUMBER || !std::isfinite(minimum) ||
        minimum < 0 || minimum > ArmorSlots || std::floor(minimum) != minimum) {
        error = "minimum-must-be-integer-0-to-7"; return false;
    }
    config.minimum = static_cast<int>(minimum);
    lua_pop(state, 1);
    lua_pushliteral(state, "armor"); lua_rawget(state, -2);
    if (lua_type(state, -1) != LUA_TTABLE) { error = "armor-must-be-array"; return false; }
    const size_t size = lua_objlen(state, -1);
    if (size > 4096) { error = "too-many-armor-entries"; return false; }
    size_t count = 0;
    lua_pushnil(state);
    while (lua_next(state, -2)) {
        const double index = lua_tonumber(state, -2);
        if (lua_type(state, -2) != LUA_TNUMBER || !std::isfinite(index) ||
            index < 1 || index > size || std::floor(index) != index ||
            lua_type(state, -1) != LUA_TSTRING) {
            error = "armor-must-be-dense-string-array"; return false;
        }
        size_t length = 0;
        const char* name = lua_tolstring(state, -1, &length);
        if (!length || length > 255 || std::string(name, length).find('\0') != std::string::npos) {
            error = "invalid-armor-name"; return false;
        }
        config.armorNames.emplace_back(name, length);
        ++count;
        lua_pop(state, 1);
    }
    if (count != size) { error = "armor-array-has-holes"; return false; }
    return true;
}
}
