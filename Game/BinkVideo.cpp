#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "BinkVideo.h"

#include "GameMain.h"
#include "I3DCursor.h"
#include "WorldClient.h"
#include "SoundManager.h"
#include "TextureManager.h"
/////////////////////////////////////////////////////////////////////////////////////
CBinkVideo::CBinkVideo()
#ifdef __VIDEO__
    : hBink(0)
#endif // __VIDEO__
{
}
/////////////////////////////////////////////////////////////////////////////////////
CBinkVideo::~CBinkVideo()
{
    StopPlaying();
}
/////////////////////////////////////////////////////////////////////////////////////
bool CBinkVideo::Play(LPCSTR lpszName, DWORD dwFramePause, bool bAllowSkipMovie,
                      bool bAllowSkipFrame, bool bClearPrimarySurface, bool bStopAllSounds,
                      bool bUseOverlays)
{
    bool bInterupted = false;
    if (theWorld.GetTextureManager())
        theWorld.GetTextureManager()->UnloadAllTextures();

#ifdef __VIDEO__
#endif // __VIDEO__

    return !bInterupted;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CBinkVideo::BeginPlay(LPCSTR pszName, bool bStopAllSounds, bool bHideCursor)
{
    if (theWorld.GetTextureManager())
        theWorld.GetTextureManager()->UnloadAllTextures();
#ifdef __VIDEO__
#else
    return false;
#endif // __VIDEO__
}
/////////////////////////////////////////////////////////////////////////////////////
bool CBinkVideo::PlayNextFrame(bool bBlitToPrimary, bool bFreezeOnLastFrame)
{
#ifdef __VIDEO__
#else
    return false;
#endif // __VIDEO__
}
/////////////////////////////////////////////////////////////////////////////////////
void CBinkVideo::StopPlaying()
{
#ifdef __VIDEO__
#endif // __VIDEO__

    if (bWasCursorHided)
    {
        // theCursor.Flip(true);
        theCursor.Show(TRUE);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CBinkVideo::SetScreenPlace(CPoint ptTopLeft)
{
#ifdef __VIDEO__
#endif // __VIDEO__
}

int CBinkVideo::GetFramesCount()
{
#ifdef __VIDEO__
#else
    return 0;
#endif // __VIDEO__
}

void CBinkVideo::DrawFrame(int nFrame, bool bBlitToSurface, bool bToPrimary)
{
#ifdef __VIDEO__
#endif // __VIDEO__
}
