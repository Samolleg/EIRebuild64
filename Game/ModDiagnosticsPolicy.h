#pragma once
#include <windows.h>
#include <string>
namespace ModDiagnosticsPolicy {
inline DWORD Dword(LSTATUS status,DWORD type,DWORD bytes,DWORD value,DWORD fallback) {
    return status==ERROR_SUCCESS && type==REG_DWORD && bytes==sizeof(value) ? value : fallback;
}
// The same native two-output sequence is shared with the headless RAM component.
// Exceptions deliberately propagate; this does not invent transactional recovery.
template<class Remove,class Save,class Deploy,class Loot>
void Recover(const std::string& name,Remove remove,Save save,Deploy deploy,Loot loot) {
    remove();save(name);deploy();loot();remove();save("lootall-"+name);deploy();
}
}
