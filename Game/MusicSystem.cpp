#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "MusicSystem.h"
#include "SoundManager.h"
#include "randomgen.h"
#include "WorldClient.h"
#include "ModPackage.h"
#include "ModRuntime.h"
#include "GameJournal.h"
//----------------------------------------------------------------------------------------------------------

const LPCSTR PSZ_MUSICINI_FILENAME = "music.reg";

const LPCSTR PSZ_MP3EXT = ".mp3";

const int N_CALMMUSIC_REPLAY_MIN = 2 * 60 * 1000;
const int N_CALMMUSIC_REPLAY_MAX = 3 * 60 * 1000;

//----------------------------------------------------------------------------------------------------------

CMusicSystem::CMusicSystem()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif //_DEBUG
    Clear();
}

void CMusicSystem::Clear()
{
    mode = EMM_EMPTY;
    themes.RemoveAll();
    szCreditsMusic = szMainMenuMusic = szForcedMusic = "none";
    bForcedMusic = FALSE;
    dwNextPlayTime = 0;
    bCombatState = FALSE;
    bZoneModeStartPause = FALSE;
    szLastCombatMusic = "none";
}

void CMusicSystem::Destroy()
{
    Clear();
}

BOOL CMusicSystem::Create()
{
    CFileRegistry fReg;
    if (!fReg.Open(CString(theWorld.GetResourcePath()) + PSZ_MUSICINI_FILENAME))
        return ReportError("CMusicSystem::Create()\nUnable to open file \"" +
                           CString(PSZ_MUSICINI_FILENAME) + "\"");

    szCreditsMusic = fReg.GetString("Common", "Credits", "");
    szMainMenuMusic = fReg.GetString("Common", "MainMenu", "");

    CStringArray szSections;
    fReg.GetSections(szSections);
    for (int i = 0; i < szSections.GetSize(); i++)
    {
        if (szSections[i].CompareNoCase("common") == 0)
            continue;
        SAllodTheme newTheme;
        newTheme.szAllodName = szSections[i];
        newTheme.bIsDungeon = FALSE;
        newTheme.szBriefingMusic = fReg.GetString(szSections[i], "Briefing", "");
        newTheme.szConstructorMusic = fReg.GetString(szSections[i], "Constructor", "");
        fReg.GetStringArray(szSections[i], "CalmOpen", newTheme.szCalmTheme);
        fReg.GetStringArray(szSections[i], "Combat", newTheme.szCombatTheme);
        themes.Add(newTheme);
        newTheme.bIsDungeon = TRUE;
        fReg.GetStringArray(szSections[i], "CalmDungeon", newTheme.szCalmTheme);
        themes.Add(newTheme);
    }
    return TRUE;
}

void CMusicSystem::FadeOutMusic()
{
    theWorld.GetSoundManager()->FadeOutStreaming();
}

void CMusicSystem::FadeInMusic()
{
    theWorld.GetSoundManager()->FadeInStreaming();
}

// ---------------------------------------------------------------------------------------------------------

BOOL CMusicSystem::UseZoneSpecificMusic()
{
    const auto* package = ModRuntime::Current();
    return package && package->SpellAddon().Feature39() == 2;
}

CString CMusicSystem::ResolveZoneMusicName(const CString& zone, const CString& allod)
{
    if (zone == "gz20g")
        return "final";
    if (!UseZoneSpecificMusic())
        return allod;
    // R32: the installed aligned ASCII compare leaves EDX four bytes into gz20*.
    // Preserve this confirmed selector (including absent themes), with owned lifetime.
    return zone.Left(4) == "gz20" ? zone.Mid(4) : zone;
}

void CMusicSystem::SetZoneMode(LPCSTR pszAllodName, BOOL bIsDungeon)
{
    theWorld.GetSoundManager()->EndStreaming(EM_INSTANT);
    bZoneModeStartPause = TRUE;
    for (int i = 0; i < themes.GetSize(); i++)
    {
        if (themes[i].szAllodName.CompareNoCase(pszAllodName) == 0 &&
            (themes[i].bIsDungeon == bIsDungeon))
        {
            nCurrentTheme = i;
            mode = EMM_ZONE;
            dwNextPlayTime = 1;
            theWorld.GetSoundManager()->EndStreaming(EM_INSTANT);
            bCombatState = FALSE;
            szLastCombatMusic = "";
            GameJournal::Event("music", "zone_theme", "selector=%s dungeon=%d index=%d",
                               GameJournal::Quote(pszAllodName).c_str(), bIsDungeon, i);
            return;
        }
    }
    GameJournal::Event("music", "zone_theme_missing", "selector=%s dungeon=%d index=-1 silent=%d",
                       GameJournal::Quote(pszAllodName).c_str(), bIsDungeon, UseZoneSpecificMusic());
    if (UseZoneSpecificMusic())
        return; // Keep the early stop/pause and all other old fields, as the ASI JMP does.
    ReportError("CMusicSystem::SetZoneMode()\nUnable to find sound theme for allod \"" +
                CString(pszAllodName) + "\"");
}

void CMusicSystem::SetMuteMode()
{
    mode = EMM_EMPTY;
    theWorld.GetSoundManager()->EndStreaming(EM_INSTANT);
}

void CMusicSystem::SetBriefingMode(LPCSTR pszAllodName)
{
    mode = EMM_BRIEFING;
    theWorld.GetSoundManager()->EndStreaming(EM_INSTANT);

    CString szBriefingMusic;
    int i = 0;
    for (; i < themes.GetSize(); i++)
        if (themes[i].szAllodName.CompareNoCase(pszAllodName) == 0)
        {
            szBriefingMusic = themes[i].szBriefingMusic;
            break;
        }
    if (i == themes.GetSize())
    {
        GameJournal::Event("music", "briefing_theme_missing", "selector=%s index=-1",
                           GameJournal::Quote(pszAllodName).c_str());
        return;
    }

    GameJournal::Event("music", "briefing_track", "selector=%s index=%d track=%s",
                       GameJournal::Quote(pszAllodName).c_str(), i,
                       GameJournal::Quote(szBriefingMusic + PSZ_MP3EXT).c_str());
    theWorld.GetSoundManager()->BeginStreaming(szBriefingMusic + PSZ_MP3EXT, EM_INSTANT);
}

void CMusicSystem::SetConstructorMode(LPCSTR pszAllodName)
{
    mode = EMM_CONSTRUCTOR;
    theWorld.GetSoundManager()->EndStreaming(EM_INSTANT);

    CString szConstructorMusic;
    int i = 0;
    for (; i < themes.GetSize(); i++)
        if (themes[i].szAllodName.CompareNoCase(pszAllodName) == 0)
        {
            szConstructorMusic = themes[i].szConstructorMusic;
            break;
        }
    if (i == themes.GetSize())
    {
        GameJournal::Event("music", "constructor_theme_missing", "selector=%s index=-1",
                           GameJournal::Quote(pszAllodName).c_str());
        return;
    }

    GameJournal::Event("music", "constructor_track", "selector=%s index=%d track=%s",
                       GameJournal::Quote(pszAllodName).c_str(), i,
                       GameJournal::Quote(szConstructorMusic + PSZ_MP3EXT).c_str());
    theWorld.GetSoundManager()->BeginStreaming(szConstructorMusic + PSZ_MP3EXT, EM_INSTANT);
}

void CMusicSystem::SetCreditsMode()
{
    mode = EMM_CREDITS;
    theWorld.GetSoundManager()->EndStreaming(EM_INSTANT);

    theWorld.GetSoundManager()->BeginStreaming(szCreditsMusic + PSZ_MP3EXT, EM_INSTANT);
}

void CMusicSystem::SetMainMenuMode()
{
    mode = EMM_MAINMENU;
    theWorld.GetSoundManager()->EndStreaming(EM_INSTANT);

    theWorld.GetSoundManager()->BeginStreaming(szMainMenuMusic + PSZ_MP3EXT, EM_INSTANT);
}

void CMusicSystem::BeginScriptMusic(LPCSTR pszScriptMusic)
{
    if (mode != EMM_ZONE)
        return;
    szForcedMusic = CString(pszScriptMusic) + PSZ_MP3EXT;
    theWorld.GetSoundManager()->BeginStreaming(szForcedMusic, EM_INSTANT);
    while (theWorld.GetSoundManager()->IsStreamingSwitching())
        Sleep(0);
    theWorld.GetSoundManager()->EndStreaming(EM_ENDOFTRACK);
    bForcedMusic = TRUE;
}

// --------------------------------------- Segment
// ---------------------------------------------------------

int CMusicSystem::Segment()
{
    CSoundManager *pSndMng = theWorld.GetSoundManager();

    if (mode != EMM_ZONE || bZoneModeStartPause == TRUE)
        return TRUE;

    CString szCurPlayingMusic;
    szCurPlayingMusic = pSndMng->GetStreamingName();

    if (bForcedMusic)
    {
        if (szCurPlayingMusic != szForcedMusic)
        {
            bForcedMusic = FALSE;
            bCombatState = FALSE;
            dwNextPlayTime = timeGetTime() +
                             random.Get(N_CALMMUSIC_REPLAY_MIN, N_CALMMUSIC_REPLAY_MAX);
        }
        else
            return TRUE;
    }

    BOOL bNewCombatState = (theWorld.GetDangerState() == WDS_COMBAT);

    /* TEST
    bNewCombatState = TRUE;
    static DWORD dwTime = timeGetTime();
    if ( timeGetTime() - dwTime > 15*1000 )
            bNewCombatState = random.Get(0,1);
    if ( timeGetTime() - dwTime > 30*1000 )
            bNewCombatState = FALSE;
    */

    if (bCombatState)
    {
        if (bNewCombatState == FALSE)
        {
            pSndMng->EndStreaming(EM_SAFE);
            dwNextPlayTime = timeGetTime() +
                             random.Get(N_CALMMUSIC_REPLAY_MIN, N_CALMMUSIC_REPLAY_MAX);
        }
    }
    else
    {
        if (bNewCombatState == TRUE)
        {
            if (szCurPlayingMusic != szLastCombatMusic || szLastCombatMusic == "")
            {
                if (themes[nCurrentTheme].szCombatTheme.GetSize() == 1)
                    szLastCombatMusic = themes[nCurrentTheme].szCombatTheme[0];
                else
                    szLastCombatMusic = themes[nCurrentTheme].szCombatTheme[random.Get(
                        0, themes[nCurrentTheme].szCombatTheme.GetSize() - 1)];
                szLastCombatMusic += PSZ_MP3EXT;
            }
            GameJournal::Event("music", "combat_track", "selector=%s dungeon=%d index=%d track=%s",
                               GameJournal::Quote(themes[nCurrentTheme].szAllodName).c_str(),
                               themes[nCurrentTheme].bIsDungeon, nCurrentTheme,
                               GameJournal::Quote(szLastCombatMusic).c_str());
            pSndMng->BeginStreaming(szLastCombatMusic, EM_FAST);
        }
        else
        {
            if (szCurPlayingMusic == "")
            {
                szLastCombatMusic = "";
                if (dwNextPlayTime == 0)
                    dwNextPlayTime = timeGetTime() +
                                     random.Get(N_CALMMUSIC_REPLAY_MIN, N_CALMMUSIC_REPLAY_MAX);
                if (timeGetTime() >= dwNextPlayTime)
                {
                    dwNextPlayTime = 0;
                    if (themes[nCurrentTheme].szCalmTheme.GetSize() != 0)
                    {
                        CString szPlayName;
                        if (themes[nCurrentTheme].szCalmTheme.GetSize() == 1)
                            szPlayName = themes[nCurrentTheme].szCalmTheme[0];
                        else
                            szPlayName = themes[nCurrentTheme].szCalmTheme[random.Get(
                                0, themes[nCurrentTheme].szCalmTheme.GetSize() - 1)];
                        szPlayName += PSZ_MP3EXT;
                        GameJournal::Event("music", "calm_track", "selector=%s dungeon=%d index=%d track=%s",
                                           GameJournal::Quote(themes[nCurrentTheme].szAllodName).c_str(),
                                           themes[nCurrentTheme].bIsDungeon, nCurrentTheme,
                                           GameJournal::Quote(szPlayName).c_str());
                        pSndMng->BeginStreaming(szPlayName, EM_INSTANT);
                        while (pSndMng->IsStreamingSwitching())
                            Sleep(0);
                        pSndMng->EndStreaming(EM_ENDOFTRACK);
                    }
                }
            }
        }
    }

    bCombatState = bNewCombatState;

    return TRUE;
}
