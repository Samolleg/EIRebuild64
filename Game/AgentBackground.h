#pragma once
#include <windows.h>
#include <shellapi.h>
#include <cwchar>

// Immutable per-process command line policy. No setting or registry key.
namespace AgentBackground {
inline bool Enabled() noexcept {
    static const bool enabled=[]() noexcept {
        int count=0;auto** arguments=CommandLineToArgvW(GetCommandLineW(),&count);
        if(!arguments)ExitProcess(ERROR_NOT_ENOUGH_MEMORY);
        bool found=false;
        for(int i=1;i<count;++i)if(wcscmp(arguments[i],L"--background")==0){found=true;break;}
        LocalFree(arguments);return found;
    }();
    return enabled;
}
void DialogError(const char* text,const char* title) noexcept;
void DialogError(const wchar_t* text,const wchar_t* title) noexcept;
}
