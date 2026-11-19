#pragma once
#include <windows.h>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

namespace SaveHistory {
struct Slot {
    const char *label = nullptr;
    std::string head;
    int age = 0;
};

inline Slot Identify(const char *name)
{
    const bool isolated = _strnicmp(name, "mcp_", 4) == 0;
    const char *slot = isolated ? name + 4 : name;
    if (_strnicmp(slot, "save", 4) != 0 || !slot[4])
        return {};
    const int age = slot[5] == '1' ? 1 : slot[5] == '2' ? 2 : 0;
    if (slot[5] && (!age || slot[6]))
        return {};
    for (const auto &kind : {Slot{"save_auto_enter", "savea"}, Slot{"save_auto_exit", "saveb"}, Slot{"save_quick", "saveq"}}) {
        if (_strnicmp(slot, kind.head.c_str(), 5) == 0)
            return {kind.label, isolated ? "mcp_" + kind.head : kind.head, age};
    }
    return {};
}

// Serialize publication and recovery, including a second game process.
class FamilyLock {
    HANDLE handle;
public:
    explicit FamilyLock(const std::string &head)
        : handle(CreateMutexA(nullptr, FALSE, ("Local\\EIRebuild64.SaveHistory." + head).c_str()))
    {
        if (!handle)
            throw std::system_error(GetLastError(), std::system_category(), "save history mutex");
        const DWORD result = WaitForSingleObject(handle, INFINITE);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
            const DWORD error = GetLastError();
            CloseHandle(handle);
            throw std::system_error(error, std::system_category(), "lock save history");
        }
    }
    FamilyLock(const FamilyLock &) = delete;
    FamilyLock &operator=(const FamilyLock &) = delete;
    ~FamilyLock() { ReleaseMutex(handle); CloseHandle(handle); }
};

namespace fs = std::filesystem;
inline void Move(const fs::path &from, const fs::path &to)
{
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::system_error(GetLastError(), std::system_category(), "rotate save history");
}

// The pending file is a complete, flushed envelope. Each rename consumes its
// source. After interruption the missing source identifies the completed step.
inline void FinishPending(const fs::path &root, const std::string &head)
{
    const auto current = root / head / "slot.sav";
    const auto pending = root / head / "slot.pending";
    if (!fs::exists(pending))
        return;
    if (fs::exists(current)) {
        const auto first = root / (head + "1") / "slot.sav";
        fs::create_directories(first.parent_path());
        if (fs::exists(first)) {
            const auto second = root / (head + "2") / "slot.sav";
            fs::create_directories(second.parent_path());
            Move(first, second);
        }
        Move(current, first);
    }
    Move(pending, current);
}
}
