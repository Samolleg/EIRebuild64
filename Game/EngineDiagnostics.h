#pragma once
#include <windows.h>
namespace EngineDiagnostics {
void Initialize(bool quiet = false);
void Note(const char *text);
void Event(const char *format, ...);
LONG WINAPI Filter(EXCEPTION_POINTERS *info);
void Caught(const char *text, DWORD code = 0xE0000135);
bool AntiCrashEnabled();
void Report(const char *text, bool error);
void ItemCreationError(const char *reason, const char *name);
int GetLogLevel(bool console);
void SetLogLevel(bool console, int level);
}
