#pragma once

#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <windows.h>
#include <io.h>

#if (_MSC_VER >= 1700)
#define CLASS_ENUM enum class
#else
#define CLASS_ENUM enum
#endif

// Text functions
bool WideToAnsi(const std::wstring &src, std::string &dest, uint32_t codePage = CP_ACP);
std::string WideToAnsi(const std::wstring &src, uint32_t codePage = CP_ACP);

bool AnsiToWide(const std::string &src, std::wstring &dest, uint32_t codePage = CP_ACP);
std::wstring AnsiToWide(const std::string &src, uint32_t codePage = CP_ACP);

std::string ToLower(const std::string &str);
std::wstring ToLower(const std::wstring &str);
std::string ToUpper(const std::string &str);
std::wstring ToUpper(const std::wstring &str);

void AddTrailingSlash(std::string &s);
void AddTrailingSlash(std::wstring &s);

bool StartsWith(const std::string &s, const std::string &suffix);
bool StartsWith(const std::wstring &s, const std::wstring &suffix);
bool EndsWith(const std::string &s, const std::string &suffix);
bool EndsWith(const std::wstring &s, const std::wstring &suffix);

std::wstring GetDirectoryName(const std::wstring &path);
std::string GetDirectoryName(const std::string &path);

// Registry fucntions
bool RegGetRawValue(HKEY hRoot, const std::wstring &key, const std::wstring &name,
                    std::vector<char> &value, uint32_t &type, uint32_t flags);

bool RegSetRawValue(HKEY hRoot, const std::wstring &key, const std::wstring &name,
                    const std::vector<char> &value, uint32_t type, uint32_t flags);

std::wstring RegGetString(HKEY hRoot, const std::wstring &key, const std::wstring &name,
                          const std::wstring &defaultValue = L"", uint32_t flags = 0);

bool RegSetString(HKEY hRoot, const std::wstring &key, const std::wstring &name,
                  const std::wstring &value, uint32_t flags = 0);

std::vector<std::wstring> RegGetMultiString(HKEY hRoot, const std::wstring &key,
                                            const std::wstring &name, uint32_t flags = 0);

bool RegSetMultiString(HKEY hRoot, const std::wstring &key, const std::wstring &name,
                       const std::vector<std::wstring> &value, uint32_t flags = 0);

uint32_t RegGetDword(HKEY hRoot, const std::wstring &key, const std::wstring &name,
                     uint32_t defaultValue, uint32_t flags = 0);

bool RegSetDword(HKEY hRoot, const std::wstring &key, const std::wstring &name, uint32_t value,
                 uint32_t flags = 0);

static inline bool RegGetBool(HKEY hRoot, const std::wstring &key, const std::wstring &name,
                              bool defaultValue = false, uint32_t flags = 0)
{
    return RegGetDword(hRoot, key, name, defaultValue ? 1 : 0, flags) != 0;
}

static inline bool RegSetBool(HKEY hRoot, const std::wstring &key, const std::wstring &name,
                              bool value, uint32_t flags = 0)
{
    return RegSetDword(hRoot, key, name, value ? 1 : 0, flags);
}

// File functions
bool ReadFileFull(const std::wstring &filename, std::string &result);
bool ReadFileFull(const std::wstring &filename, std::vector<char> &result);
bool WriteFileFull(const std::wstring &filename, const void *data, size_t size);
bool WriteFileFull(const std::wstring &filename, const std::string &value);
bool WriteFileFull(const std::wstring &filename, const std::vector<char> &value);

static inline bool FileExists(const std::wstring &filename)
{
    uint32_t attrs = GetFileAttributesW(filename.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static inline bool FileExists(const std::string &filename)
{
    uint32_t attrs = GetFileAttributesA(filename.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static inline bool DirExists(const std::wstring &filename)
{
    uint32_t attrs = GetFileAttributesW(filename.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static inline bool DirExists(const std::string &filename)
{
    uint32_t attrs = GetFileAttributesA(filename.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring GetUniqPath(std::wstring dir, const std::wstring &prefix = L"",
                         const std::wstring &suffix = L"");

static inline std::string GetUniqPath(const std::string &dir, const std::string &prefix = "",
                                      const std::string &suffix = "")
{
    return WideToAnsi(GetUniqPath(AnsiToWide(dir), AnsiToWide(prefix), AnsiToWide(suffix)));
}

bool GetFullPath(const std::string &path, std::string &result);
bool GetFullPath(const std::wstring &path, std::wstring &result);

// Clipboard functions
bool ClipboardSetText(const std::wstring &text);
std::wstring ClipboardGetText();

// Other functions
void ErrorMsgBoxNoExit(const char *format, ...);
void ErrorMsgBoxWideNoExit(const wchar_t *format, ...);
void __declspec(noreturn) ErrorMsgBox(uint32_t exitCode, const char *format, ...);
void __declspec(noreturn) ErrorMsgBoxWide(uint32_t exitCode, const wchar_t *format, ...);
std::wstring GetCurrentModulePath();
#define VERIFY(cond) \
    if (!(cond))     \
    abort()
uint32_t GetCrc32(const void *data, size_t size, uint32_t crc = 0);

// Version helper
struct SVersion
{
    int Major;
    int Minor;
    int Patch;
    int Build;

    explicit SVersion(int major = 0, int minor = 0, int patch = 0, int build = 0)
    {
        Set(major, minor, patch, build);
    }
    explicit SVersion(const char *version) { Set(version); }
    explicit SVersion(const std::string &version) { Set(version.c_str()); }

    bool operator==(const SVersion &b) const
    {
        return Major == b.Major && Minor == b.Minor && Patch == b.Patch && Build == b.Build;
    }
    bool operator>(const SVersion &b) const
    {
        return (Major > b.Major) || (Major == b.Major && Minor > b.Minor) ||
               (Major == b.Major && Minor == b.Minor && Patch > b.Patch) ||
               (Major == b.Major && Minor == b.Minor && Patch == b.Patch && Build > b.Build);
    }
    bool operator<(const SVersion &b) const { return b > *this; }
    bool operator<=(const SVersion &b) const { return b > *this || b == *this; }
    bool operator>=(const SVersion &b) const { return *this > b || *this == b; }

    std::string ToString() const
    {
        char buf[100];
        sprintf_s(buf, "%d.%d.%d.%d", Major, Minor, Patch, Build);
        return buf;
    }

private:
    void Set(int major, int minor, int patch, int build);
    void Set(const char *version);
};

// Simple critical section wrapper
class CriticalSection
{
public:
    CriticalSection() { InitializeCriticalSection(&_lock); }
    ~CriticalSection() { DeleteCriticalSection(&_lock); }
    void Lock() { EnterCriticalSection(&_lock); }
    bool TryLock() { return TryEnterCriticalSection(&_lock) != 0; }
    void Unlock() { LeaveCriticalSection(&_lock); }

private:
    CRITICAL_SECTION _lock;
};

class SectionLock
{
public:
    SectionLock(CriticalSection &section)
        : _section(&section)
    {
        _section->Lock();
    }
    ~SectionLock() { _section->Unlock(); }

private:
    CriticalSection *_section;
};

// Logging
class CLogger
{
public:
    CLASS_ENUM ELogLevel : int{ LL_DEBUG = 0, LL_INFO,     LL_WARNING,
                                LL_ERROR,     LL_DISABLED, LL_MAX_LEVEL = LL_DISABLED };

    virtual ~CLogger() {}

    void Stop()
    {
        SectionLock lock(_critSec);
        if (!_started)
            return;
        StopLocked();
        _started = false;
    }

    void Flush()
    {
        SectionLock lock(_critSec);
        if (!_started)
            return;
        FlushLocked();
    }

    ELogLevel GetLevel()
    {
        SectionLock lock(_critSec);
        return _level;
    }

    void SetLevel(ELogLevel level)
    {
        SectionLock lock(_critSec);
        _level = level;
    }

    static ELogLevel LevelFromInt(int level)
    {
        if (level < 0)
            return ELogLevel::LL_DEBUG;
        if (level > (int)ELogLevel::LL_MAX_LEVEL)
            return ELogLevel::LL_MAX_LEVEL;
        return (ELogLevel)level;
    }

    bool Started() { return _started; }
    void EnterCrashMode() { InterlockedExchange(&_crashMode, 1); }
    size_t RecentLineCount() const { return _recentCount; }
    const char *RecentLine(size_t index) const
    {
        return index < _recentCount
            ? _recent[(_recentPos + RecentLines - _recentCount + index) % RecentLines] : nullptr;
    }

    bool IsDebug() const { return _level <= ELogLevel::LL_DEBUG; }
    bool IsInfo() const { return _level <= ELogLevel::LL_INFO; }
    bool IsWarning() const { return _level <= ELogLevel::LL_WARNING; }
    bool IsError() const { return _level <= ELogLevel::LL_ERROR; }

    void Debug(const char *section, const char *format, ...);
    void Info(const char *section, const char *format, ...);
    void Warning(const char *section, const char *format, ...);
    void Error(const char *section, const char *format, ...);

    void WithTimestamp(bool enable = true)
    {
        SectionLock lock(_critSec);
        _withTimestamp = enable;
    };
    void WithLevel(bool enable = true)
    {
        SectionLock lock(_critSec);
        _withLevel = enable;
    };
    void WithSection(bool enable = true)
    {
        SectionLock lock(_critSec);
        _withSection = enable;
    };
    void WithNewLine(bool enable = true)
    {
        SectionLock lock(_critSec);
        _withNewLine = enable;
    }

    void SetShortTimestamp(bool value)
    {
        SectionLock lock(_critSec);
        _shortTimestamp = value;
    }
    void SetShortLevel(bool value)
    {
        SectionLock lock(_critSec);
        _shortLevel = value;
    }
    void SetSeparator(const std::string &separator)
    {
        SectionLock lock(_critSec);
        _separator = separator;
    }

protected:
    CLogger()
        : _file(nullptr)
        , _started(false)
        , _level(ELogLevel::LL_INFO)
        , _withTimestamp(false)
        , _withLevel(false)
        , _withSection(false)
        , _withNewLine(false)
        , _shortTimestamp(false)
        , _shortLevel(false)
        , _separator("\t")
        , _crashMode(0), _recentPos(0), _recentCount(0)
    {
        memset(_recent, 0, sizeof(_recent));
    }

    bool Start(ELogLevel level)
    {
        if (_started)
            return false;

        SectionLock lock(_critSec);
        bool result = StartLocked();
        if (result)
        {
            _level = level;
            _started = true;
        }
        return result;
    }

    virtual bool StartLocked() = 0;
    virtual void StopLocked() = 0;
    virtual void FlushLocked() = 0;
    virtual void WriteLineLocked(const char *line, size_t length) = 0;
    bool InCrashMode() const { return _crashMode != 0; }

private:
    FILE *_file;
    bool _started;
    ELogLevel _level;
    CriticalSection _critSec;
    bool _withTimestamp;
    bool _withLevel;
    bool _withSection;
    bool _withNewLine;
    bool _shortTimestamp;
    bool _shortLevel;
    std::string _separator;
    volatile LONG _crashMode;
    static const size_t RecentLines = 24, RecentLineSize = 200;
    char _recent[RecentLines][RecentLineSize];
    size_t _recentPos, _recentCount;
    void StoreRecentLocked(const char *line, size_t length);

    void Write(const char *levelStr, const char *section, const char *format, va_list args);
};

class CFileLogger : public CLogger
{
public:
    CFileLogger()
        : CLogger()
        , _file(nullptr), _rawFile(nullptr)
    {
        WithTimestamp();
        WithLevel();
        WithSection();
        WithNewLine();
    }

    ~CFileLogger()
    {
        if (_file)
            fclose(_file);
    }

    bool Start(const std::wstring &path, const std::wstring &mode = L"a",
               ELogLevel level = ELogLevel::LL_INFO)
    {
        if (Started())
            return false;

        _path = path;
        _mode = mode;
        return CLogger::Start(level);
    }

private:
    bool StartLocked() override
    {
        assert(!Started() && !_file);
        _file = _wfopen(_path.c_str(), _mode.c_str());
        if (_file) {
            intptr_t handle = _get_osfhandle(_fileno(_file));
            _rawFile = handle != -1 ? reinterpret_cast<HANDLE>(handle) : nullptr;
        }
        return _file != nullptr;
    }

    void StopLocked() override
    {
        assert(Started() && _file);
        fflush(_file);
        fclose(_file);
        _file = nullptr;
        _rawFile = nullptr;
    }

    void FlushLocked() override
    {
        assert(Started() && _file);
        fflush(_file);
    }

    void WriteLineLocked(const char *line, size_t length) override
    {
        assert(Started() && _file);
        if (InCrashMode()) {
            if (_rawFile) {
                DWORD written = 0;
                WriteFile(_rawFile, line, static_cast<DWORD>(length), &written, nullptr);
            }
        } else fwrite(line, 1, length, _file);
    }

    FILE *_file;
    HANDLE _rawFile;
    std::wstring _path;
    std::wstring _mode;
};
