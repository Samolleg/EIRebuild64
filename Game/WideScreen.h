#pragma once
#include "GeneralSettings.h"

// Source-level counterpart of addon Patches/WideScreen.cpp (base 0458524).
namespace WideScreen
{
inline int Effective(int w, int h) { return w >= 800 && h >= 600 && w * 3 > h * 4 ? h * 800 / 600 : w; }
inline int Effective() { return Effective(theSettings.dwWidth, theSettings.dwHeight); }
inline int ScreenShift() { return (int(theSettings.dwWidth) - Effective()) / 2; }
inline int VirtualShift() { return (ScreenShift() * 800 + Effective() - 1) / Effective(); }
inline int currentShift = 0;
class Scope
{
    int previous;
public:
    explicit Scope(int side) : previous(currentShift) { currentShift = side * VirtualShift(); }
    ~Scope() { currentShift = previous; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
}
