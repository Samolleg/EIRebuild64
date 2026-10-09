#include "stdafx.h"
#include "GameJournal.h"

#include <string>
#include <fstream>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "Notification.h"
#include "GameMain.h"
#include "EngineDiagnostics.h"
#include "Console.h"
#include "adFormat.h"
#include "ModCliTool.h"

// DirectX error to string conversion
#include "Notification.inl"

// -------------------------------------------------------------------------------------------------
// Report an error or warning
// -------------------------------------------------------------------------------------------------

static std::string FormatTime(const SYSTEMTIME &time)
{
    char buffer[32] = { 0 };
    sprintf(buffer, "%02d.%02d.%02d %02d:%02d:%02d", time.wDay, time.wMonth, time.wYear % 100,
            time.wHour, time.wMinute, time.wSecond);
    return buffer;
}

static void TraceString(const char *str)
{
    EngineDiagnostics::Note(str);
    SYSTEMTIME time;
    GetLocalTime(&time);

    std::stringstream ss;
    ss << "[" << FormatTime(time) << "] " << str;

    NConsole::game << ss.str() << std::endl;

    // Duplicate info in file
#ifdef __NOTIFY_FILE__
    static bool isFirst = true;

    std::ios_base::openmode mode = isFirst ? std::ios::ate : std::ios::app;
    std::ofstream file("errors.txt", mode);
    if (file.is_open())
        file << ss.str();

    isFirst = false;
#endif
}

static void ReportMessage(const char *reportType, const char *buffer, const char *additionalInfo,
                          bool plainError)
{
    if (plainError && !strcmp(reportType, "Error")) GameJournal::MemoryErrorText(buffer);
    GameJournal::Event("ERROR", "notification", "type=%s text=%s extra=%s", GameJournal::Quote(reportType).c_str(), GameJournal::Quote(buffer).c_str(), GameJournal::Quote(additionalInfo).c_str());
    GameJournal::FlushCritical();
    if (ModCliTool::Active())
    {
        printf("[%s]: %s%s\n", reportType, buffer, additionalInfo);
        if (!strcmp(reportType, "Error") &&
            ModCliTool::StopAfterError(plainError, EngineDiagnostics::AntiCrashEnabled()))
            std::exit(-1); // Proven original CRT exit; no exception/fault policy substitute.
        return;
    }
    if (!strcmp(reportType, "Error"))
    {
        const bool suppressed = plainError && EngineDiagnostics::AntiCrashEnabled();
        if (suppressed)
            EngineDiagnostics::Report(buffer, true);
        else
        {
            const std::string message = std::string(buffer) + additionalInfo;
            NGame::MessageBox(message.c_str(), MB_OK | MB_ICONERROR);
        }
        const std::uint32_t count = ++ModCliTool::ErrorCount();
        if (!suppressed && count < 0x80000000u && count >= 1u)
            std::exit(-1);
        return;
    }
    std::stringstream ss;
    ss << "[" << reportType << "]: " << buffer << additionalInfo << std::endl;
    TraceString(ss.str().c_str());
}

static void Report(const char *reportType, const char *additionalInfo, const char *format,
                   va_list argList, bool plainError = false)
{
    char buffer[1024] = { 0 };
    wvsprintf(buffer, format, argList);
    ReportMessage(reportType, buffer, additionalInfo, plainError);
}

bool ReportError(const char *format, ...)
{
    va_list argList;
    va_start(argList, format);
    Report("Error", "", format, argList, true);
    va_end(argList);

    return false;
}

bool ReportError(HRESULT code, const char *format, ...)
{
    if (code == E_OUTOFMEMORY) GameJournal::AllocationFailure("ReportError_hresult");
    char buffer[1024] = { 0 };
    sprintf(buffer, " (0x%.8lX) %s", code, ErrorToString(code));

    va_list argList;
    va_start(argList, format);
    Report("Error", buffer, format, argList);
    va_end(argList);

    return false;
}

bool ReportError(HRESULT code)
{
    if (code == E_OUTOFMEMORY) GameJournal::AllocationFailure("ReportError_hresult");
    char buffer[1024] = { 0 };
    sprintf(buffer, "(0x%.8lX) %s", code, ErrorToString(code));
    ReportMessage("Error", buffer, "", false);

    return false;
}

void ReportWarning(const char *format, ...)
{
    va_list argList;
    va_start(argList, format);
    Report("Warning", "", format, argList);
    va_end(argList);
}

void ReportWarning(HRESULT code, const char *format, ...)
{
    char buffer[1024] = { 0 };
    sprintf(buffer, " (0x%.8lX) %s", code, ErrorToString(code));

    va_list argList;
    va_start(argList, format);
    Report("Warning", buffer, format, argList);
    va_end(argList);
}

void ReportWarning(HRESULT code)
{
    ReportWarning(code, "");
}

// -------------------------------------------------------------------------------------------------
// CGameException
// -------------------------------------------------------------------------------------------------

CGameException::CGameException(const char *format, ...)
{
    char buffer[1024] = { 0 };
    va_list argList;
    va_start(argList, format);
    wvsprintf(buffer, format, argList);
    va_end(argList);

    m_errorString = buffer;
}

// -------------------------------------------------------------------------------------------------
// CDXException
// -------------------------------------------------------------------------------------------------

CDXException::CDXException(const char *format, ...)
{
    char buffer[1024] = { 0 };
    va_list argList;
    va_start(argList, format);
    wvsprintf(buffer, format, argList);
    va_end(argList);

    m_errorString = buffer;
}

CDXException::CDXException(HRESULT code, const char *format, ...)
{
    char buffer1[1024] = { 0 };
    sprintf(buffer1, " (0x%.8lX) %s", code, ErrorToString(code));

    char buffer2[1024] = { 0 };
    va_list argList;
    va_start(argList, format);
    wvsprintf(buffer2, format, argList);
    va_end(argList);

    m_errorString = string(buffer1) + string(buffer2);
}

// -------------------------------------------------------------------------------------------------
// AnimationFlagsToString
// -------------------------------------------------------------------------------------------------

const char *AnimationFlagsToString(DWORD flags)
{
    static char buffer[1024] = { 0 };
    string szString;

    // State
    switch (flags & ADB_STATES)
    {
    case ADB_STATE_NEUTRAL:
        szString = "State Neutral.";
        break;
    case ADB_STATE_ATTACK:
        szString = "State Attack.";
        break;
    case ADB_STATE_WARRY:
        szString = "State Warry.";
        break;
    case ADB_STATE_LIE:
        szString = "State Lie.";
        break;
    default:
        szString = "State Unknown.";
    }

    // action
    switch (flags & ADB_ACTIONS)
    {
    case ADB_ACTION_SPECIAL:
        szString += " Action Special.";
        break;
    case ADB_ACTION_ATTACK:
        szString += " Action Attack.";
        break;
    case ADB_ACTION_CAST:
        szString += " Action Cast.";
        break;
    case ADB_ACTION_RUN:
        szString += " Action Run.";
        break;
    case ADB_ACTION_WALK:
        szString += " Action Walk.";
        break;
    case ADB_ACTION_IDLE:
        szString += " Action Idle.";
        break;
    case ADB_ACTION_DEATH:
        szString += " Action Death.";
        break;
    case ADB_ACTION_SUFFER:
        szString += " Action Suffer.";
        break;
    case ADB_ACTION_CROSS:
        szString += " Action Cross.";
        break;
    default:
        szString += " Action Unknown.";
    }

    // modifiers
    if ((flags & ADB_ACTIONS) == ADB_ACTION_ATTACK || (flags & ADB_ACTIONS) == ADB_ACTION_CAST ||
        (flags & ADB_ACTIONS) == ADB_ACTION_SUFFER || (flags & ADB_ACTIONS) == ADB_ACTION_DEATH)
    {
        if (flags & ADB_MODIFIERS)
            szString += " Has direction.";
    }

    if ((flags & ADB_ACTIONS) == ADB_ACTION_IDLE)
    {
        switch (flags & ADB_MODIFIERS)
        {
        case ADB_MODIFIER_IDLE_STAY:
            szString += " Modifier Stay.";
            break;
        case ADB_MODIFIER_IDLE_LOOK:
            szString += " Modifier LookAround.";
            break;
        default:
            szString += " Modifier Unknown.";
        }
    }

    if ((flags & ADB_ACTIONS) == ADB_ACTION_CROSS)
    {
        switch (flags & ADB_MODIFIERS)
        {
        case ADB_MODIFIER_CROSS_TO_NEUTRAL:
            szString += " Modifier cross-to-neutral.";
            break;
        case ADB_MODIFIER_CROSS_TO_ATTACK:
            szString += " Modifier cross-to-attack.";
            break;
        case ADB_MODIFIER_CROSS_TO_WARRY:
            szString += " Modifier cross-to-warry.";
            break;
        case ADB_MODIFIER_CROSS_TO_LIE:
            szString += " Modifier cross-to-lie.";
            break;
        default:
            break;
        }
    }

    if ((flags & ADB_ACTIONS) == ADB_ACTION_SPECIAL)
    {
        if (flags & ADB_MODIFIERS)
        {
            int nSpecialIndex = (flags & ADB_MODIFIERS) / ADB_MODIFIER_SPECIAL_1;
            char index[64] = { 0 };
            sprintf(index, " Modifier Special %d", nSpecialIndex);
            szString += index;
        }
    }

    // Stage
    switch (flags & ADB_STAGES)
    {
    case ADB_STAGE_START:
        szString += " Stage Start.";
        break;
    case ADB_STAGE_CYCLE:
        szString += " Stage Cycle.";
        break;
    case ADB_STAGE_END:
        szString += " Stage End.";
        break;
    case ADB_STAGE_UNIQUE:
        szString += " Stage Unique.";
        break;
    default:
        szString += " Stage Unknown.";
    }

    // Weapon
    szString += " Weapons: [ ";
    if (flags & ADB_WEAPON_SWORD)
        szString += "sword ";
    else if (flags & ADB_WEAPON_AXE)
        szString += "axe ";
    else if (flags & ADB_WEAPON_DAGGER)
        szString += "dagger ";
    else if (flags & ADB_WEAPON_SPEAR)
        szString += "spear ";
    else if (flags & ADB_WEAPON_HAMMER)
        szString += "hammer ";
    else if (flags & ADB_WEAPON_BOW)
        szString += "bow ";
    else if (flags & ADB_WEAPON_CROSSBOW)
        szString += "crossbow ";
    else
        szString += "none";
    szString += " ]";

    strcpy(buffer, szString.c_str());

    return buffer;
}
