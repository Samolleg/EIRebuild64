#pragma once
// RTFText.h: interface for the CI3DTetxRTF class.
//
//////////////////////////////////////////////////////////////////////

#include "I3DObject.h"

class CInterface3D;
class CMappedResource;

class CI3DTetxRTF : public CI3DSurface
{
    HWND m_hRichEditCtrl;
    float m_fLastTime;
    CPoint m_ptOffset;
    float m_fScrollSpeed;
    int m_nTextLenght;
    int m_nAddScroll;
    //
    HINSTANCE hRichEdInstance;

    void Init(); // call after create main window
    void RenderToDC();

public:
    CI3DTetxRTF(CInterface3D *pI3D, CRect rc);
    virtual ~CI3DTetxRTF();

    bool LoadFromFile(const char *pszFileName);
    bool LoadFromMemory(BYTE *pData, int nLen);
    bool LoadFromRes(CMappedResource *pRes, const char *pszFileName);

    void SetScrollSpeed(float fSpeed, bool bHideStart = true); // измеряется в twip / mill sec
    float GetScrollSpeed() { return m_fScrollSpeed; }
    virtual void Draw();
};
