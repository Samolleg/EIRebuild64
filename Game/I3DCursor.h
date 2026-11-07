#ifndef __I3DCURSOR_H__
#define __I3DCURSOR_H__
#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
///////////////////////////////////////////////////////////////////////////////////////////////////
#include "Win32Classes.h"
namespace OwnedRender { struct E3CursorStrip; }
///////////////////////////////////////////////////////////////////////////////////////////////////
struct SCursor2DInfo
{
    std::shared_ptr<const OwnedRender::E3CursorStrip> image;
    CSize sizeCursor;                       // Размер одного курсора
    DWORD dwAnimNumb;                       // Количество фаз анимаций курсора
    DWORD dwCurAnim;                        // Текущая фаза анимации
    CPoint ptHotSpot;                       // Активная точка курсора

    SCursor2DInfo(const char *szImageFileName, int x = 0, int y = 0);
    void GetCursorRect(CRect &rect);
};
///////////////////////////////////////////////////////////////////////////////////////////////////
class CI3DCursor
{
    friend struct LiveUiAccess;
private:
    BOOL m_isShow;
    SCursor2DInfo *m_pCursorInfo;
    int m_nScipCounter;
    int m_eCursorType;
    void Publish();

    CRect m_rectBack;
    CRect m_rectDstCur;
    CRect m_rectSrcCur; // rect в картинки с фазами курсора
    CRect m_rectScreen;

    CSimplyArray<SCursor2DInfo *, SCursor2DInfo *> m_aCursorInfo;
    CPoint m_ptCursor;
    CPoint m_ptLockedCursor;
    CPoint m_ptLastInc;
    CPoint m_ptLastCursor;
    bool m_isLock;
    bool m_isInitialized;
    DWORD m_dwAnimLastTime;

private:
    CPoint GetDrawCursorPos() { return m_ptCursor - m_pCursorInfo->ptHotSpot; }
    bool UpdateCursorState();

public:
    // Add cursor image to I3DCursor support
    void LoadCursor(const char *szName, int x = 0, int y = 0);

public:
    // Construction/Dectructon
    CI3DCursor();
    void Init();
    void Fini();
    void StartProcessing();

    // Draw support
    void BeginDraw(); // Call this function before draw cursor to render surface
    void EndDraw();   // Call this function after flip render surface
    void Draw();      // Draw cursor to render surface

    // Other functions
    int SetMode(int nType); // возвращает предыдущий тип курсора
    int GetMode() const { return m_eCursorType; }
    void Show(BOOL bShow);
    bool IsShown() const { return m_isShow != FALSE; }

    CPoint GetPos() const { return m_ptCursor; }
    void SetPos(CPoint ptPos);
    void LockPos(bool bLock = true);

    CPoint GetIncrement();
    void ResetIncrement() { m_ptLastInc = CPoint(0, 0); }

    void GetToolTipCursorRect(CRect &rect) { rect = m_rectDstCur; }
};
///////////////////////////////////////////////////////////////////////////////////////////////////
extern CI3DCursor theCursor;
///////////////////////////////////////////////////////////////////////////////////////////////////
#endif // __I3DCURSOR_H__