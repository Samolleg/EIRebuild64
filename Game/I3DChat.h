#pragma once

#include "I3DObject.h"
#include <vector>
/////////////////////////////////////////////////////////////////////////////////////
struct SChatLine
{
    CString szWho;         // who is speaking
    CString szText;        // text itself
    DWORD dwAppearTime;    // time when message arrived
    CI3DSurface *pSurface; // surface to output

    SChatLine();
    ~SChatLine();
};
/////////////////////////////////////////////////////////////////////////////////////
class CI3DChat : public CI3DObject
{
private:
    std::list<SChatLine> mLines;
    bool bEntering;
    CString szChatPhrase;
    int nCursorPos;

    void RedrawChatPhrase();

public:
    CI3DChat(CInterface3D *pI);

    void ClearLines();
    void NewPhrase(LPCTSTR pszPhrase, int nOrder);
    void SetEntering(bool bSet);
    bool IsEntering() { return bEntering; }

    virtual void OnIdle();
    virtual void Draw2D();
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    virtual void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
};
