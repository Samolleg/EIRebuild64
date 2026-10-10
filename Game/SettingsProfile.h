#pragma once
#include <string>
#include <vector>
#include <cstdint>

// ReStarter profile is the sole settings store. No Windows registry fallback.
namespace SettingsProfile {
void Initialize();
std::wstring Path();
std::string Summary();
std::wstring Section(const std::wstring& formerKey);
std::wstring Wide(const char* text);
std::string Ansi(const std::wstring& text);
bool GetRaw(const std::wstring& section, const std::wstring& name,
            std::vector<char>& data, uint32_t& type);
bool SetRaw(const std::wstring& section, const std::wstring& name,
            const std::vector<char>& data, uint32_t type);
uint32_t Dword(const std::wstring& section, const std::wstring& name, uint32_t fallback = 0);
bool SetDword(const std::wstring& section, const std::wstring& name, uint32_t value);
std::wstring String(const std::wstring& section, const std::wstring& name,
                    const std::wstring& fallback = L"");
std::vector<std::wstring> MultiString(const std::wstring& section, const std::wstring& name);
std::vector<std::wstring> Sections(const std::wstring& base);
std::vector<std::wstring> Entries(const std::wstring& section);
bool Delete(const std::wstring& section, const std::wstring* name);
}
