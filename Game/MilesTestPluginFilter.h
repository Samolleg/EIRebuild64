#pragma once
#include <windows.h>
#include <cstring>
#include <stdexcept>

// Temporary, process-local EIRebuild test policy. Retail files are untouched.
namespace MilesTestPluginFilter
{
inline decltype(&LoadLibraryA) originalLoad = nullptr;
inline LONG blockedLoads = 0;
inline HMODULE WINAPI LoadCodec(LPCSTR path)
{
    const char* name = path;
    if (path)
        for (const char* p = path; *p; ++p)
            if (*p == '\\' || *p == '/') name = p + 1;
    if (name && _stricmp(name, "SpellAddonX.asi") == 0)
    {
        InterlockedIncrement(&blockedLoads);
        OutputDebugStringA("EIRebuild test: Miles SpellAddonX.asi load excluded\n");
        SetLastError(ERROR_MOD_NOT_FOUND);
        return nullptr;
    }
    return originalLoad(path);
}
inline void Install()
{
    if (originalLoad) return;
    auto module = reinterpret_cast<unsigned char*>(GetModuleHandleA("mss32.dll"));
    if (!module) throw std::runtime_error("Miles module not loaded");
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(module + dos->e_lfanew);
    auto rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) throw std::runtime_error("Miles imports missing");
    auto desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(module + rva);
    for (; desc->Name; ++desc)
    {
        if (_stricmp(reinterpret_cast<char*>(module + desc->Name), "kernel32.dll") != 0)
            continue;
        if (!desc->OriginalFirstThunk) continue;
        auto names = reinterpret_cast<IMAGE_THUNK_DATA*>(module + desc->OriginalFirstThunk);
        auto slots = reinterpret_cast<IMAGE_THUNK_DATA*>(module + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots)
        {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto entry = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(module + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<char*>(entry->Name), "LoadLibraryA") != 0) continue;
            if (slots->u1.Function != reinterpret_cast<ULONG_PTR>(&LoadLibraryA))
                throw std::runtime_error("Miles LoadLibraryA already intercepted");
            DWORD protection = 0;
            if (!VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), PAGE_READWRITE, &protection))
                throw std::runtime_error("Cannot protect Miles import slot");
            originalLoad = &LoadLibraryA;
            InterlockedExchangePointer(reinterpret_cast<PVOID*>(&slots->u1.Function),
                reinterpret_cast<PVOID>(&LoadCodec));
            DWORD unused;
            if (!VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), protection, &unused))
                throw std::runtime_error("Cannot restore Miles import protection");
            return;
        }
    }
    throw std::runtime_error("Miles LoadLibraryA import not found");
}
}
