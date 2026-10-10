#include "stdafx.h"
#include "Common.h"
#include "../Game/ErrorDialog.h"
#include "../Game/SettingsProfile.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <wtypes.h>
#include <wchar.h>

using namespace std;

static void RemoveTrailingNullChar(wstring &s)
{
    if (!s.empty() && s[s.length() - 1] == L'\0')
        s.resize(s.size() - 1);
}

static void RemoveTrailingNullChar(string &s)
{
    if (!s.empty() && s[s.length() - 1] == '\0')
        s.resize(s.size() - 1);
}

bool WideToAnsi(const wstring &src, string &dest, uint32_t codePage)
{
    int lengthDest = WideCharToMultiByte(codePage, 0, src.c_str(), (int)src.length(), nullptr, 0,
                                         nullptr, nullptr);
    if (lengthDest <= 0)
        return false;

    vector<char> buf(lengthDest + 1, '\0');
    lengthDest = WideCharToMultiByte(codePage, 0, src.c_str(), (int)src.length(), buf.data(),
                                     lengthDest, nullptr, nullptr);
    if (lengthDest <= 0)
        return false;

    dest.assign(buf.data(), lengthDest);
    RemoveTrailingNullChar(dest);
    return true;
}

string WideToAnsi(const wstring &src, uint32_t codePage)
{
    string result;
    WideToAnsi(src, result, codePage);
    return result;
}

bool AnsiToWide(const string &src, wstring &dest, uint32_t codePage)
{
    int lengthDest = MultiByteToWideChar(codePage, 0, src.c_str(), (int)src.length(), nullptr, 0);
    if (lengthDest <= 0)
        return false;

    vector<wchar_t> buf(lengthDest + 1, L'\0');
    lengthDest = MultiByteToWideChar(codePage, 0, src.c_str(), (int)src.length(), buf.data(),
                                     lengthDest);
    if (lengthDest <= 0)
        return false;

    dest.assign(buf.data(), lengthDest);
    RemoveTrailingNullChar(dest);
    return true;
}

wstring AnsiToWide(const string &src, uint32_t codePage)
{
    wstring result;
    AnsiToWide(src, result, codePage);
    return result;
}

string ToLower(const string &str)
{
    vector<char> buf(str.length() + 1);
    strcpy(&buf[0], str.c_str());
    CharLowerA(&buf[0]);
    return string(buf.begin(), buf.end() - 1);
}

wstring ToLower(const wstring &str)
{
    vector<wchar_t> buf(str.length() + 1);
    wcscpy(&buf[0], str.c_str());
    CharLowerW(&buf[0]);
    return wstring(buf.begin(), buf.end() - 1);
}

string ToUpper(const string &str)
{
    vector<char> buf(str.length() + 1);
    strcpy(&buf[0], str.c_str());
    CharUpperA(&buf[0]);
    return string(buf.begin(), buf.end() - 1);
}

wstring ToUpper(const wstring &str)
{
    vector<wchar_t> buf(str.length() + 1);
    wcscpy(&buf[0], str.c_str());
    CharUpperW(&buf[0]);
    return wstring(buf.begin(), buf.end() - 1);
}

void AddTrailingSlash(string &s)
{
    if (!s.empty() && s[s.length() - 1] != '\\')
        s += '\\';
}

void AddTrailingSlash(wstring &s)
{
    if (!s.empty() && s[s.length() - 1] != L'\\')
        s += L'\\';
}

bool StartsWith(const string &s, const string &suffix)
{
    if (suffix.empty() || s.length() < suffix.length())
        return false;

    for (size_t i = 0; i < suffix.size(); i++)
        if (s[i] != suffix[i])
            return false;

    return true;
}

bool StartsWith(const wstring &s, const wstring &suffix)
{
    if (suffix.empty() || s.length() < suffix.length())
        return false;

    for (size_t i = 0; i < suffix.size(); i++)
        if (s[i] != suffix[i])
            return false;

    return true;
}

bool EndsWith(const string &s, const string &suffix)
{
    if (suffix.empty() || s.length() < suffix.length())
        return false;

    size_t i = s.length() - suffix.length();
    for (size_t j = 0; j < suffix.length(); j++)
    {
        if (s[i] != suffix[j])
            return false;
        i++;
    }
    return true;
}

bool EndsWith(const wstring &s, const wstring &suffix)
{
    if (suffix.empty() || s.length() < suffix.length())
        return false;

    size_t i = s.length() - suffix.length();
    for (size_t j = 0; j < suffix.length(); j++)
    {
        if (s[i] != j)
            return false;
        i++;
    }
    return true;
}

wstring GetDirectoryName(const wstring &path)
{
    if (path.empty() || path[path.length() - 1] == L'\\')
        return path;
    auto pos = path.rfind(L'\\');
    if (pos == path.npos)
        return wstring();
    wstring result = path;
    result.replace(result.begin() + pos + 1, result.end(), wstring());
    return result;
}

string GetDirectoryName(const string &path)
{
    if (path.empty() || path[path.length() - 1] == '\\')
        return path;
    auto pos = path.rfind('\\');
    if (pos == path.npos)
        return string();
    string result = path;
    result.replace(result.begin() + pos + 1, result.end(), string());
    return result;
}

bool RegGetRawValue(HKEY, const wstring &key, const wstring &name, vector<char> &value,
                    uint32_t &type, uint32_t) {
    return SettingsProfile::GetRaw(SettingsProfile::Section(key), name, value, type);
}

bool RegSetRawValue(HKEY, const wstring &key, const wstring &name, const vector<char> &value,
                    uint32_t type, uint32_t) {
    return SettingsProfile::SetRaw(SettingsProfile::Section(key), name, value, type);
}

wstring RegGetString(HKEY hRoot, const wstring &key, const wstring &name,
                     const wstring &defaultValue, uint32_t flags)
{
    vector<char> data;
    uint32_t type;
    if (!RegGetRawValue(hRoot, key, name, data, type, flags) ||
        !(type == REG_SZ || type == REG_EXPAND_SZ))
        return defaultValue;

    wstring result((const wchar_t *)&data[0], data.size() / sizeof(wchar_t));
    RemoveTrailingNullChar(result);
    return result;
}

bool RegSetString(HKEY hRoot, const wstring &key, const wstring &name, const wstring &value,
                  uint32_t flags)
{
    vector<char> data;
    data.resize((value.length() + 1) * sizeof(value[0]));
    memcpy(&data[0], value.c_str(), data.size());
    return RegSetRawValue(hRoot, key, name, data, REG_SZ, flags);
}

vector<wstring> RegGetMultiString(HKEY hRoot, const wstring &key, const wstring &name,
                                  uint32_t flags)
{
    vector<char> data;
    uint32_t type;
    if (!RegGetRawValue(hRoot, key, name, data, type, flags) || type != REG_MULTI_SZ)
        return vector<wstring>();

    vector<wstring> result;
    wchar_t *buf = (wchar_t *)&data[0];
    size_t size = data.size() / sizeof(wchar_t);
    for (size_t i = 0; i < size; i++)
    {
        size_t start = i;
        while (i < size && buf[i] != L'\0')
            i++;
        if (i > start)
            result.push_back(wstring(buf + start, i - start));
    }
    return result;
}

bool RegSetMultiString(HKEY hRoot, const wstring &key, const wstring &name,
                       const vector<wstring> &value, uint32_t flags)
{
    vector<char> data;
    size_t size = 0;
    for (auto it = value.cbegin(); it != value.cend(); ++it)
        size += (it->length() + 1) * sizeof((*it)[0]);
    data.resize(size);
    size = 0;
    for (auto it = value.cbegin(); it != value.cend(); ++it)
    {
        size_t elemSize = (it->length() + 1) * sizeof((*it)[0]);
        memcpy(&data[size], it->c_str(), elemSize);
        size += elemSize;
    }
    return RegSetRawValue(hRoot, key, name, data, REG_MULTI_SZ, flags);
}

uint32_t RegGetDword(HKEY hRoot, const wstring &key, const wstring &name, uint32_t defaultValue,
                     uint32_t flags)
{
    vector<char> data;
    uint32_t type, result = defaultValue;
    if (RegGetRawValue(hRoot, key, name, data, type, flags) && type == REG_DWORD &&
        data.size() == sizeof(uint32_t))
    {
        memcpy(&result, &data[0], sizeof(result));
    }
    return result;
}

bool RegSetDword(HKEY hRoot, const wstring &key, const wstring &name, uint32_t value,
                 uint32_t flags)
{
    vector<char> data;
    data.resize(sizeof(value));
    memcpy(&data[0], &value, data.size());
    return RegSetRawValue(hRoot, key, name, data, REG_DWORD, flags);
}

bool ReadFileFull(const wstring &filename, vector<char> &result)
{
    HANDLE hFile = CreateFileW(filename.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER largeSize;
    if (!GetFileSizeEx(hFile, &largeSize) || largeSize.QuadPart > MAXDWORD)
    {
        CloseHandle(hFile);
        return false;
    }

    size_t size = (uint32_t)largeSize.QuadPart;
    result.resize(size);

    DWORD bytesRead;
    BOOL res = ReadFile(hFile, &result[0], size, &bytesRead, nullptr);
    CloseHandle(hFile);

    if (!res || bytesRead != size)
        return false;

    return true;
}

bool ReadFileFull(const wstring &filename, string &result)
{
    vector<char> data;
    bool res = ReadFileFull(filename, data);
    result.assign(data.begin(), data.end());
    return res;
}

bool WriteFileFull(const wstring &filename, const void *data, size_t size)
{
    HANDLE hFile = CreateFileW(filename.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    DWORD bytesWritten;
    BOOL res = WriteFile(hFile, data, size, &bytesWritten, nullptr);
    CloseHandle(hFile);

    if (!res || bytesWritten != size)
        return false;

    return true;
}

bool WriteFileFull(const wstring &filename, const vector<char> &value)
{
    return WriteFileFull(filename, &value[0], value.size());
}

bool WriteFileFull(const wstring &filename, const string &value)
{
    return WriteFileFull(filename, value.c_str(), value.length());
}

static string GetFileDescription(const void *block)
{
    struct LANGANDCODEPAGE
    {
        uint16_t language;
        uint16_t codePage;
    } *translate;
    uint32_t iLen;

    if (!VerQueryValueA(block, "\\VarFileInfo\\Translation", (LPVOID *)&translate, &iLen))
        return string();

    char *buffer = nullptr;
    uint32_t bufLen = 0;
    for (size_t i = 0; i < (iLen / sizeof(struct LANGANDCODEPAGE)); i++)
    {
        char subBlock[100];
        sprintf_s(subBlock, "\\StringFileInfo\\%04x%04x\\FileDescription", translate[i].language,
                  translate[i].codePage);

        if (VerQueryValueA(block, subBlock, (void **)&buffer, &bufLen))
            break;
    }
    if (buffer == nullptr || bufLen == 0)
        return string();

    string result(buffer, buffer + bufLen);
    RemoveTrailingNullChar(result);
    return result;
}

static string GetFileVersionString(const void *block)
{
    VS_FIXEDFILEINFO *info;
    uint32_t len;
    if (!VerQueryValueA(block, "\\", (void **)&info, &len))
        return string();

    char buf[100];
    sprintf_s(buf, "v%d.%d.%d.%d", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
              HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
    return string(buf);
}

static string GetAppName(HMODULE hModule)
{
    char path[MAX_PATH] = "\0";
    if (!GetModuleFileNameA(hModule, path, sizeof(path)))
        return string();

    DWORD temp;
    uint32_t len = GetFileVersionInfoSizeA(path, &temp);
    if (len == 0)
        return string();

    vector<uint8_t> block(len);
    if (!GetFileVersionInfoA(path, 0, len, &block[0]))
        return string();

    string appName = GetFileDescription(&block[0]);
    string ver = GetFileVersionString(&block[0]);
    if (!ver.empty())
        appName += " " + ver;
    return appName;
}

static HMODULE GetCurrentModule()
{
    HMODULE hModule;
    BOOL succ = GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   __FUNCTION__, &hModule);
    return succ ? hModule : nullptr;
}

static string GetCurrentAppName()
{
    HMODULE hModule = GetCurrentModule();
    return hModule != nullptr ? GetAppName(hModule) : string();
}

wstring GetCurrentModulePath()
{
    HMODULE hModule = GetCurrentModule();
    if (hModule == nullptr)
        return wstring();

    wchar_t buf[MAX_PATH];
    if (GetModuleFileNameW(hModule, buf, _countof(buf)) == 0)
        return wstring();

    return wstring(buf);
}

wstring GetUniqPath(wstring dir, const wstring &prefix, const wstring &suffix)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    AddTrailingSlash(dir);
    for (int i = 0; i < 1000; i++)
    {
        wchar_t name[100];
        swprintf_s(name, L"%s%02d.%02d.%d-%d%s", prefix.c_str(), st.wDay, st.wMonth, st.wYear, i,
                   suffix.c_str());
        wstring result = dir + name;
        if (GetFileAttributesW(result.c_str()) == INVALID_FILE_ATTRIBUTES)
            return result;
    }
    return wstring();
}

bool GetFullPath(const std::string &path, std::string &result)
{
    vector<char> buffer(16, L'\0');
    size_t size = GetFullPathNameA(path.c_str(), buffer.size(), &buffer[0], nullptr);
    if (size > buffer.size())
    {
        buffer.resize(size);
        size = GetFullPathNameA(path.c_str(), buffer.size(), &buffer[0], nullptr);
    }
    if (size > 0)
    {
        assert(size <= buffer.size());
        result.assign(&buffer[0]);
        return true;
    }
    return false;
}

bool GetFullPath(const std::wstring &path, std::wstring &result)
{
    vector<wchar_t> buffer(16, L'\0');
    size_t size = GetFullPathNameW(path.c_str(), buffer.size(), &buffer[0], nullptr);
    if (size > buffer.size())
    {
        buffer.resize(size);
        size = GetFullPathNameW(path.c_str(), buffer.size(), &buffer[0], nullptr);
    }
    if (size > 0)
    {
        assert(size <= buffer.size());
        result.assign(&buffer[0]);
        return true;
    }
    return false;
}

bool ClipboardSetText(const std::wstring &text)
{
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE,
                               (text.length() + 1) * sizeof(wchar_t));
    if (!hMem)
        return false;

    auto buf = (LPWSTR)GlobalLock(hMem);
    if (!buf)
    {
        GlobalFree(hMem);
        return false;
    }

    memcpy(buf, text.c_str(), (text.length() + 1) * sizeof(wchar_t));
    GlobalUnlock(hMem);

    if (!OpenClipboard(nullptr))
    {
        GlobalFree(hMem);
        return false;
    }

    EmptyClipboard();
    bool result = SetClipboardData(CF_UNICODETEXT, hMem) != nullptr;
    CloseClipboard();
    return result;
}

std::wstring ClipboardGetText()
{
    if (!OpenClipboard(nullptr))
        return std::wstring();

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData)
    {
        CloseClipboard();
        return std::wstring();
    }

    auto buf = (wchar_t *)GlobalLock(hData);
    if (!buf)
    {
        CloseClipboard();
        return std::wstring();
    }

    wstring result(buf);

    GlobalUnlock(hData);
    CloseClipboard();
    return result;
}

void ErrorMsgBoxNoExit(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    char buf[1024] = { 0 };
    vsprintf_s(buf, format, args);
    ErrorDialog::ShowA(GetActiveWindow(), buf, GetCurrentAppName().c_str(), MB_OK | MB_ICONERROR);
    va_end(args);
}

void ErrorMsgBoxWideNoExit(const wchar_t *format, ...)
{
    va_list args;
    va_start(args, format);
    wchar_t buf[1024] = { 0 };
    vswprintf_s(buf, format, args);
    auto appName = AnsiToWide(GetCurrentAppName());
    ErrorDialog::ShowW(GetActiveWindow(), buf, appName.c_str(), MB_OK | MB_ICONERROR);
    va_end(args);
}

void __declspec(noreturn) ErrorMsgBox(uint32_t exitCode, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    char buf[1024] = { 0 };
    vsprintf_s(buf, format, args);
    ErrorDialog::ShowA(GetActiveWindow(), buf, GetCurrentAppName().c_str(), MB_OK | MB_ICONERROR, false);
    va_end(args);
    ExitProcess(exitCode);
}

// Без Wide будет невозможно вызвать эту или прошлую функцию из ассемблерной вставки
// https://docs.microsoft.com/ru-ru/cpp/assembler/inline/calling-cpp-functions-in-inline-assembly
// Ansi вариант функции вызывается в функции Handler (Patch.cpp)
void __declspec(noreturn) ErrorMsgBoxWide(uint32_t exitCode, const wchar_t *format, ...)
{
    va_list args;
    va_start(args, format);
    wchar_t buf[1024] = { 0 };
    vswprintf_s(buf, format, args);
    auto appName = AnsiToWide(GetCurrentAppName());
    ErrorDialog::ShowW(GetActiveWindow(), buf, appName.c_str(), MB_OK | MB_ICONERROR, false);
    va_end(args);
    ExitProcess(exitCode);
}

static uint32_t Crc32ForByte(uint32_t b)
{
    for (int i = 0; i < 8; i++)
        b = (b & 1 ? 0 : (uint32_t)0xEDB88320ul) ^ b >> 1;
    return b ^ (uint32_t)0xFF000000ul;
}

uint32_t GetCrc32(const void *data, size_t size, uint32_t crc)
{
    static uint32_t table[0x100];
    if (!*table)
    {
        for (size_t i = 0; i < 256; i++)
            table[i] = Crc32ForByte(i);
    }

    for (size_t i = 0; i < size; i++)
        crc = table[(uint8_t)crc ^ ((uint8_t *)data)[i]] ^ crc >> 8;
    return crc;
}

void SVersion::Set(int major, int minor, int patch, int build)
{
    if (major < 0)
        throw invalid_argument("major");
    if (minor < 0)
        throw invalid_argument("minor");
    if (patch < 0)
        throw invalid_argument("patch");
    if (build < 0)
        throw invalid_argument("build");
    Major = major;
    Minor = minor;
    Patch = patch;
    Build = build;
}

void SVersion::Set(const char *version)
{
    if (!version)
        throw invalid_argument("version");

    for (int pos = 0, dots = 0; version[pos] != '\0'; pos++)
    {
        char c = version[pos];
        if (c >= '0' && c <= '9')
            continue;
        if (c != '.' || pos == 0 || version[pos - 1] == '.' || dots >= 3)
            throw invalid_argument("version");
        dots++;
    }

    Major = Minor = Patch = Build = 0;
    sscanf(version, "%d.%d.%d.%d", &Major, &Minor, &Patch, &Build);
}

void CLogger::Debug(const char *section, const char *format, ...)
{
    if (!IsDebug())
        return;
    va_list args;
    va_start(args, format);
    Write(_shortLevel ? "DBG" : "DEBUG", section, format, args);
    va_end(args);
}

void CLogger::Info(const char *section, const char *format, ...)
{
    if (!IsInfo())
        return;
    va_list args;
    va_start(args, format);
    Write(_shortLevel ? "INF" : "INFO", section, format, args);
    va_end(args);
}

void CLogger::Warning(const char *section, const char *format, ...)
{
    if (!IsWarning())
        return;
    va_list args;
    va_start(args, format);
    Write(_shortLevel ? "WRN" : "WARNING", section, format, args);
    va_end(args);
}

void CLogger::Error(const char *section, const char *format, ...)
{
    if (!IsError())
        return;
    va_list args;
    va_start(args, format);
    Write(_shortLevel ? "ERR" : "ERROR", section, format, args);
    va_end(args);
}

void CLogger::Write(const char *levelStr, const char *section, const char *format, va_list args)
{
    if (!_started)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    char buf[2048];
    int bufSize = _countof(buf), len = 0;

    if (_withTimestamp)
    {
        if (_shortTimestamp)
        {
            len += sprintf_s(buf + len, bufSize - len, "[%02d:%02d:%02d]", st.wHour, st.wMinute,
                             st.wSecond);
        }
        else
        {
            len += sprintf_s(buf + len, bufSize - len, "[%02d.%02d.%d %02d:%02d:%02d.%03d]",
                             st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond,
                             st.wMilliseconds);
        }
    }
    if (_withLevel)
        len += sprintf_s(buf + len, bufSize - len, "[%s]", levelStr);
    if (_withSection)
        len += sprintf_s(buf + len, bufSize - len, "[%s]", section);
    if (_withTimestamp || _withLevel || _withSection)
        len += sprintf_s(buf + len, bufSize - len, _separator.c_str());

    len += vsprintf_s(buf + len, bufSize - len, format, args);

    if (_withNewLine)
        len += sprintf_s(buf + len, bufSize - len, "\n");

    if (InCrashMode()) {
        if (!_critSec.TryLock()) return;
    } else _critSec.Lock();
    StoreRecentLocked(buf, len);
    WriteLineLocked(buf, len);
    _critSec.Unlock();
}

void CLogger::StoreRecentLocked(const char *line, size_t length)
{
    if (length >= RecentLineSize) length = RecentLineSize - 1;
    while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) --length;
    memcpy(_recent[_recentPos], line, length);
    _recent[_recentPos][length] = 0;
    _recentPos = (_recentPos + 1) % RecentLines;
    if (_recentCount < RecentLines) ++_recentCount;
}
