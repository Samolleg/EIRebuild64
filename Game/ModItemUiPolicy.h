#pragma once
#include <string>
#include <cstring>
namespace ModItemUiPolicy {
// Names imposed by LS_GetItemNameAndDescMeleeActive after the ordinary lookup.
inline const char* WeaponSkillName(const char* code)
{
    if(!code)return nullptr;
    if(!_stricmp(code,"doubleshot"))return "\xC4\xE2\xEE\xE9\xED\xEE\xE9 \xE2\xFB\xF1\xF2\xF0\xE5\xEB";
    if(!_stricmp(code,"preciseshot"))return "\xD2\xEE\xF7\xED\xFB\xE9 \xE2\xFB\xF1\xF2\xF0\xE5\xEB";
    if(!_stricmp(code,"precisehit"))return "\xD2\xEE\xF7\xED\xFB\xE9 \xF3\xE4\xE0\xF0";
    if(!_stricmp(code,"cripplinghit"))return "\xCA\xE0\xEB\xE5\xF7\xE0\xF9\xE8\xE9 \xF3\xE4\xE0\xF0";
    if(!_stricmp(code,"explosiveshot"))return "\xC2\xE7\xF0\xFB\xE2\xED\xEE\xE9 \xE2\xFB\xF1\xF2\xF0\xE5\xEB";
    if(!_stricmp(code,"powerhit")||!_stricmp(code,"test"))return "\xCC\xEE\xF9\xED\xFB\xE9 \xF3\xE4\xE0\xF0";
    return nullptr;
}
template<class Get> inline bool Advanced(const char* key,Get get)
{
    if(!get("useAdvancedDescriptions"))return false;
    // Native LS_UseWeaponActiveSpells is permanently true, separately from
    // companion controls and the independent extended-slot registration.
    if(!strcmp(key,"useLS_SetUnitS"))return get(key)||get("JMVExtendedSpellSlots")||true;
    return get(key)!=0;
}
template<class Resolve> inline std::string Instruction(bool enabled,bool prototype,bool figure,
    const char* code,const char* fallback,Resolve resolve)
{
    if(!enabled||!code)return fallback;
    const std::string name=resolve(code,1);
    const std::string value=resolve(code,figure?2:3);
    if(_stricmp(code,name.c_str()))return fallback;
    return prototype&&figure?std::string("initqi3item"):value;
}
inline bool FirstDeploy(bool campReturn,bool prevent,const char* zone,bool nearStart)
{return !campReturn&&!prevent&&_strnicmp(zone,"zone1",5)==0&&nearStart;}
struct Icon {float z,size;};
inline Icon ItemIcon(bool advanced){return advanced?Icon{4,150}:Icon{3,120};}
inline int NameInset(bool advancedConsider){return advancedConsider?0:5;}
}
