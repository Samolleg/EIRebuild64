#pragma once
#include "ModNpcConsumables.h"
#include <map>
#include <string>
#include <cstring>
struct lua_State;
namespace ModStaticLookup {
// Same admission and clear-before-put limit as the addon production cache.
template<class T> class Cache {
    std::map<std::string,T> entries;
public:
    template<class Calculate> T Get(const std::string& key,bool eligible,Calculate calculate) {
        if(!eligible){bool valid=false;return calculate(valid);}
        auto found=entries.find(key);
        if(found!=entries.end())return found->second;
        bool valid=false;T fresh=calculate(valid);
        if(valid){if(entries.size()>=4096)entries.clear();entries.emplace(key,fresh);}
        return fresh;
    }
};
inline bool NameKey(const char* name){return name && strlen(name)<=256;}
template<class T> void KeyValue(std::string& key,const T& value){key.append(reinterpret_cast<const char*>(&value),sizeof(value));}
inline void KeyString(std::string& key,const char* value){const size_t n=strlen(value);KeyValue(key,n);key.append(value,n);}
struct ConsumableValue {bool found=false;ModConsumableConfig config;};
struct State {
    bool contract=false;
    const void* functions[3]{};
    // Keep the typed timing validation and float scaling of DualStrikes.
    Cache<double> dual;
    Cache<float> vamp;
    Cache<ConsumableValue> consumables;
    void Initialize(lua_State* state);
    bool Eligible(lua_State* state,int index) const;
};
}
