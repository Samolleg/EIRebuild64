#pragma once

#include "resource.h"

#include <list>

// --------------------------------------------------------------------------------------------
// namespace, which represent all global variables and functions
// --------------------------------------------------------------------------------------------
namespace NGame {
// Windows identification variables
extern HWND hWnd;           // window handler
extern HINSTANCE hInstance; // current instance
extern bool bNeedRender; // crap
// reporting function...
int MessageBox(LPCSTR pszText, UINT uType = MB_OK);
double GetFreeDiskSpace(const char *pszDrive);
// common use functions
BOOL CheckPreviousApp(LPCSTR pszMainClass, LPCSTR pszMainTitle);
void DrawFrame(bool bFlip = true, bool bDraw2D = true);
void DestroyInputMessages();
void PlayMoviesSequence(LPCSTR pszSection, bool bInitialClear = true,
                        bool bClearSurfaceBetweenMovies = true, bool bStopAllSounds = true,
                        bool bUseOverlays = true);
void EnableFlip(bool bVal = true);
bool IsFlipEnable();
// leaving program
void AllowQuit(bool bAllow);
bool QuitProgram(int nExitCode);
// memory dumping
bool DumpMemUsage(bool bByFile = true, const char *pszFileName = 0);
}; // namespace NGame
