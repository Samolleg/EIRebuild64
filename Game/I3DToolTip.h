#pragma once

#include "I3DObject.h"

enum ETipType
{
    TT_HIDE = 0,
    TT_WAIT,
    TT_INC,
    TT_DEC,
    TT_SHOW
};

class CI3DToolTip : public CI3DObject
{
    friend struct E3HudAccess;
    unsigned long long ownedTextRevision = 0, ownedClockRevision = 0;
    int ownedClockMinute = -1;
protected:
    void Update();
    // CString m_szText;
    CStringArray szStrings; // for multi-line
    int nStringHeight;
    int nFirstLineWidth;
    int nHeaderLines;

    int nKbdAction;
    const char *pszCurrentKeyName;

    CI3DPolyArea *m_pArea;
    CSize m_sizeTip;

    float m_fIdleTime;
    ETipType m_eMode;

public:
    CI3DToolTip(CInterface3D *pI3D, CI3DPolyArea *pArea, LPCTSTR pszText, int nHeader = -1,
                int nAction = 0);

    bool IsPointInArea(CPoint &point) { return TRUE; } // m_pArea->PtInArea(point) == TRUE; }
    bool IsActive() { return m_eMode != TT_HIDE && m_eMode != TT_WAIT; }
    void Activate() { m_eMode = TT_WAIT; }
    void Deactivate() { m_eMode = TT_DEC; }
    void Process(CPoint point);

    const char* JournalText() const noexcept { return szStrings.GetSize() ? (LPCSTR)szStrings[0] : ""; }
    void UpdateText(LPCTSTR pszText);

    BOOL Draw(CRenderer *pRend);
    void Draw2D();
};