#pragma once

#include "GameBase.h"
#include "SoundManager.h"

enum EMusicMode
{
    EMM_EMPTY,
    EMM_ZONE,
    EMM_BRIEFING,
    EMM_CONSTRUCTOR,
    EMM_MAINMENU,
    EMM_CREDITS
};

struct SAllodTheme
{
    CString szAllodName;
    BOOL bIsDungeon;
    CString szBriefingMusic, szConstructorMusic;
    CStringArray szCalmTheme, szCombatTheme;

    SAllodTheme &operator=(SAllodTheme &a)
    {
        szAllodName = a.szAllodName;
        bIsDungeon = a.bIsDungeon;
        szBriefingMusic = a.szBriefingMusic, szConstructorMusic = a.szConstructorMusic;
        szCalmTheme.Copy(a.szCalmTheme);
        szCombatTheme.Copy(a.szCombatTheme);
        return *this;
    }
};

// ---------------------------------------------------------------------------------------------------------

class CMusicSystem : public CGameBase
{
private:
    // Data from .INI file
    CArray<SAllodTheme, SAllodTheme &> themes;
    CString szCreditsMusic, szMainMenuMusic;

    // Current mode
    EMusicMode mode;

    // Data for current mode
    int nCurrentTheme;
    BOOL bCombatState;
    BOOL bZoneModeStartPause;
    CString szLastCombatMusic;

    CString szForcedMusic;
    BOOL bForcedMusic;

    DWORD dwNextPlayTime;

public:
    DWORD GetScriptNextPlayTime() const { return dwNextPlayTime; }
    BOOL GetScriptForced() const { return bForcedMusic; }
    const CString& GetScriptMusic() const { return szForcedMusic; }
    // Constr/desctr
    CMusicSystem();
    ~CMusicSystem() { Destroy(); }
    // Create/destroy
    virtual BOOL Create();
    virtual void Destroy();

    // Segment processing
    virtual int Segment();
    // SpellAddon f39 mode 2: explicit selectors, independent of volatile CRT registers.
    static BOOL UseZoneSpecificMusic();
    static CString ResolveZoneMusicName(const CString& zone, const CString& allod);
    void SetZoneMode(LPCSTR pszAllodName, BOOL bIsDungeon);
    void UnpauseZoneMode() { bZoneModeStartPause = FALSE; }
    void SetBriefingMode(LPCSTR pszAllodName);
    void SetConstructorMode(LPCSTR pszAllodName);
    void SetMainMenuMode();
    void SetCreditsMode();
    void SetMuteMode();
    void BeginScriptMusic(LPCSTR pszScriptMusic); // without extension, works only in zone mode
    void FadeOutMusic();
    void FadeInMusic();

    // class identification functions
    LPCSTR GetGameClassName() const { return "CMusicSystem"; }
    EClassIDs WhoAmI() const { return HPC_SOUNDMANAGER; }

private:
    void Clear();
};
