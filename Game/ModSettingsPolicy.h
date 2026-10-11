#pragma once
#include "ModConfiguration.h"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
// Installed-bound native settings contracts; no environment access here.
namespace ModSettingsPolicy {
struct Profile {
    uint32_t charge=0;float factor=1;
    uint32_t skills[6]={100,100,100,100,100,100};
    void Read(CFileRegistry& reg) {
        charge=reg.GetDWord("AddonExp","ChargeMethod",0);
        factor=ModConfiguration::Float(reg,"AddonExp","MulFactor",1);
        const char*keys[]={"Melee","Archery",nullptr,"Elemental","Sense","Astral"};
        for(int i=0;i<6;++i)skills[i]=keys[i]?reg.GetDWord("AddonPerk",keys[i],100):100;
    }
    int SkillLimit(int index,bool nativeRedraw=false) const {
        return nativeRedraw?120:index>=0&&index<6?int(skills[index]):100;
    }
    float Experience(float exp,int count) const {
        if(!charge&&!(std::fabs(factor-1)>0.00001f))return exp/count;
        // Native alternate path rounds only after the x87 divide/add/multiply chain.
        const uint32_t mode=charge;const float multiplier=factor;float result;
        __asm {
            fild count
            cmp mode, 1
            je multiplyPlayers
            cmp mode, 2
            je addPlayers
            fdivr exp
            jmp scaleExperience
        multiplyPlayers:
            fmul exp
            jmp scaleExperience
        addPlayers:
            fadd exp
        scaleExperience:
            fmul multiplier
            fstp result
        }
        return result;
    }
};
inline bool Prefix(const std::string& name,const char* prefix) {
    for(size_t i=0;prefix[i];++i){if(i>=name.size())return false;
        unsigned char c=static_cast<unsigned char>(name[i]);if(c>='A'&&c<='Z')c+=32;
        if(c!=static_cast<unsigned char>(prefix[i]))return false;}
    return true;
}
template<class Size,class Name,class Remove>
bool EquipQuick(const std::string& incoming,bool oneAmulet,bool oneRing,Size size,Name name,Remove remove) {
    // Caller checks capacity first. Preserve native forward removal, including shift skip.
    if(oneAmulet&&Prefix(incoming,"amulet"))
        for(int i=0;i<size();++i)if(Prefix(name(i),"amulet"))remove(i);
    if(oneRing&&Prefix(incoming,"ring"))
        for(int i=0;i<size();++i)if(Prefix(name(i),"ring"))return false;
    return true;
}
template<class Resolve>
void Tooltip(bool server,bool mana,bool effect,bool duration,float intelligence,
             float& manaValue,float& effectValue,float& durationTicks,Resolve resolve) {
    if(!server)return;
    if(mana)manaValue=resolve(0)*25.f/intelligence;
    if(effect)effectValue=resolve(1);
    if(duration)durationTicks=resolve(2);
}
struct Gui {
    bool controls=true,cursor=true,pending=false;
    bool Set(const std::vector<std::string>& args) {
        const int mode=args.size()==1?std::atoi(args[0].c_str()):-1;
        if(mode<0||mode>2)return false;
        controls=mode==0;cursor=mode<2;pending=true;return true;
    }
    bool Apply(bool& show){if(!pending)return false;show=controls;pending=!controls;return true;}
};
enum class Command {Addon,Execs,ListGvar,NoGui};
struct CommandEntry {const char* name;Command command;bool admin;};
inline const CommandEntry* Commands(size_t& count) {
    static const CommandEntry entries[]={{"addon",Command::Addon,true},{"execs",Command::Execs,true},
        {"listgvar",Command::ListGvar,true},{"nogui",Command::NoGui,false}};
    count=sizeof(entries)/sizeof(*entries);return entries;
}
template<class Execute>
void AutoExec(const std::vector<std::string>& registry,const std::vector<std::string>& file,
              const char* original,Execute execute) {
    for(const auto& command:registry)execute(command.c_str());
    for(const auto& command:file)execute(command.c_str());
    execute(original);
}
inline std::vector<std::wstring> MultiString(const wchar_t* data,size_t count) {
    std::vector<std::wstring> out;size_t i=0;
    while(i<count&&data[i]){size_t end=i;while(end<count&&data[end])++end;
        if(end==count)return {};out.emplace_back(data+i,end-i);i=end+1;}
    return out;
}
template<class Exists,class Load>
bool ModBink(const std::string& path,Exists exists,Load load,bool& selected) {
    selected=!path.empty()&&exists(path);return !selected||load(path);
}
inline std::string Chat(uint32_t color,const char* nick,const char* text) {
    return "<font class="+std::to_string(color)+">"+(nick?nick:"")+"</font>: "+(text?text:"")+"<BR>";
}
inline std::string Object(const char* classname,int id){return "<"+std::string(classname)+":"+std::to_string(id)+">";}
inline std::string Variable(const char* name,const char* type,const std::string& value){return std::string(name)+" : "+type+" = "+value;}
}
