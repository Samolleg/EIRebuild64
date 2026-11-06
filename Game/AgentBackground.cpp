#include "StdAfx.h"
#include "AgentBackground.h"
#include "LiveObserver.h"
#include "GameJournal.h"
#include <string>

namespace AgentBackground {
void DialogError(const char* text,const char* title) noexcept {
    LiveObserver::RecordError(text,"");
    GameJournal::Event("ERROR","background_dialog","title=%s text=%s",
        GameJournal::Quote(title).c_str(),GameJournal::Quote(text).c_str());
    GameJournal::FlushCritical();
}
void DialogError(const wchar_t* text,const wchar_t* title) noexcept {
    try {
        const auto ansi=[](const wchar_t* value){
            if(!value)return std::string();
            const int bytes=WideCharToMultiByte(1251,0,value,-1,nullptr,0,nullptr,nullptr);
            if(bytes<=0)return std::string();
            std::string result(size_t(bytes),'\0');
            WideCharToMultiByte(1251,0,value,-1,result.data(),bytes,nullptr,nullptr);
            result.pop_back();return result;
        };
        const auto message=ansi(text),caption=ansi(title);DialogError(message.c_str(),caption.c_str());
    }catch(...){GameJournal::Event("ERROR","background_dialog","text_conversion_failed=1");}
}
}
namespace owned {
// Typed embedded-host policy, not a renderer configuration override.
bool embedded_agent_background() noexcept {return AgentBackground::Enabled();}
}
