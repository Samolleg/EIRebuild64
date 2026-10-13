// BinkVideo.h: interface for the CBinkVideo class.
//
//////////////////////////////////////////////////////////////////////
#ifndef __BINKVIDEO_
#define __BINKVIDEO_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

class CBinkVideo
{
private:
struct Player;
    Player *player;
    CBinkVideo(const CBinkVideo &) = delete;
    CBinkVideo &operator=(const CBinkVideo &) = delete;
    CRect rectOnScreen;
    bool bWasCursorHided;

public:
    CBinkVideo();
    ~CBinkVideo();

    static bool DecoderAvailable();
    // Frames of this movie are the wait screen the renderer holds during loads.
    void MarkLoadingPicture();

    static bool Play(LPCSTR lpszName, DWORD dwFramePause = 0, bool bAllowSkipMovie = true,
                     bool bAllowSkipFrame = true, bool bClearPrimarySurface = true,
                     bool bStopAllSounds = true, bool bUseOverlays = true);

    bool BeginPlay(LPCSTR pszName, bool bStopAllSounds = true, bool bHideCursor = true);
    bool PlayNextFrame(bool bBlitToPrimary, bool bFreezeOnLastFrame = false);
    void StopPlaying();

    int GetFramesCount();
    void DrawFrame(int nFrame, bool bBlitToSurface, bool bToPrimary);

    void SetScreenRect(CRect rcRect) { rectOnScreen = rcRect; }
    void SetScreenPlace(CPoint ptTopLeft);
};

#endif // __BINKVIDEO_
