#pragma once
#include "stdafx.h"
#include <ostream>
#include <string>

namespace NConsole {

enum EFilter
{
    NONE = 0,
    AI = 1,
    EVENT = 2,
    GRAPHICS = 4,
    RPG = 8,
    UNITACKS = 16,
    NET = 32,
    ALL = 0xffffffff,
};
// current filter mode
extern EFilter eCurrentFilter;

// main processing function
extern void Process(const char *pszCommand);
// Headless component: local arguments/output/visibility only; does not run Process.
std::string CheckDrawAllCommand();
void RunAutoExec(const char* original);
float ObjectsDrawRadius();
float LandscapeDrawRadius();
bool ApplyNoGui(bool& show);

class CConsoleStream;
typedef void (*PConsoleManip)(CConsoleStream &stream);

extern ostream ai, event, graphics, rpg, unitAcks, net, all;
extern ostream game;
}; // namespace NConsole
