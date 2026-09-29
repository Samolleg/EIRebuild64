#pragma once
#include <windows.h>
namespace EngineDiagnostics {
void Initialize(bool quiet = false);
void Note(const char *text);
void Event(const char *format, ...);
LONG WINAPI Filter(EXCEPTION_POINTERS *info);
void Caught(const char *text);
}
