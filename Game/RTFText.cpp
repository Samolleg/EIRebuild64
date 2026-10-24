// RTFText.cpp: implementation of the CI3DTetxRTF class.
//
/////////////////////////////////////////////////////////////////////////////////////////////////
#include "StdAfx.h"
#include "OwnedRender/E2Gdi.h"
#include "owned_ui_layers.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "RTFText.h"
#include "Renderer.h"
#include "CMappedResource.h"
#include <richedit.h>
#include "WorldInterface.h"

namespace NGame {
extern HWND hWnd;           // main window handler
extern HINSTANCE hInstance; // aplication instance
}; // namespace NGame

static DWORD CALLBACK RTFStreamInCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb)
{
    CFile *pFile = reinterpret_cast<CFile *>(dwCookie);
    *pcb = pFile->Read(pbBuff, cb);
    return 0;
}

CI3DTetxRTF::CI3DTetxRTF(CInterface3D *pI3D, CRect rc)
    : CI3DSurface(pI3D, rc)
{
    hRichEdInstance = 0;
    Init();
    colorKey = RGB(255, 255, 255);
    m_fScrollSpeed = 0;
    m_ptOffset = CPoint(0, 0);
    m_nAddScroll = 0;
}

CI3DTetxRTF::~CI3DTetxRTF()
{
    ::DestroyWindow(m_hRichEditCtrl);
    if (hRichEdInstance)
    {
        ::FreeLibrary(hRichEdInstance);
        hRichEdInstance = 0;
    }
}

void CI3DTetxRTF::Init()
{
    hRichEdInstance = ::LoadLibrary("Riched20.dll");
    VERIFY(hRichEdInstance);
    m_hRichEditCtrl = ::CreateWindowEx(0, RICHEDIT_CLASSA, 0, WS_CHILD | ES_MULTILINE, 0, 0, 0, 0,
                                       NGame::hWnd, 0, NGame::hInstance, 0);
    VERIFY(m_hRichEditCtrl);
}

bool CI3DTetxRTF::LoadFromFile(const char *pszFileName)
{
    CFile cFile(pszFileName, CFile::modeRead | CFile::typeBinary);

    EDITSTREAM es;
    es.dwCookie = reinterpret_cast<DWORD_PTR>(&cFile);
    es.pfnCallback = RTFStreamInCallback;
    m_nTextLenght = ::SendMessage(m_hRichEditCtrl, // handle to destination window
                                  EM_STREAMIN,     // message to send
                                  (WPARAM)SF_RTF,  // format options
                                  (LPARAM)&es      // data (EDITSTREAM *)
    );
    m_ptOffset.y = 0;
    RenderToDC();
    return true;
}

bool CI3DTetxRTF::LoadFromRes(CMappedResource *pRes, const char *pszFileName)
{
    DWORD dwLen;
    BYTE *pData = pRes->GetFile(pszFileName, &dwLen);
    if (pData)
        LoadFromMemory(pData, dwLen);
    pRes->CloseFile(pData);
    return pData != 0;
}

bool CI3DTetxRTF::LoadFromMemory(BYTE *pData, int nLen)
{
    CMemFile cFile(pData, nLen);

    EDITSTREAM es;
    es.dwCookie = reinterpret_cast<DWORD_PTR>(&cFile);
    es.pfnCallback = RTFStreamInCallback;
    m_nTextLenght = ::SendMessage(m_hRichEditCtrl, // handle to destination window
                                  EM_STREAMIN,     // message to send
                                  (WPARAM)SF_RTF,  // format options
                                  (LPARAM)&es      // data (EDITSTREAM *)
    );
    m_ptOffset.y = 0;
    RenderToDC();
    return true;
}

void CI3DTetxRTF::RenderToDC()
{
#ifndef NOGETDC
    CDC *pDC = GetDC();
    Clear();

    SendMessage(m_hRichEditCtrl,  // handle to destination window
                EM_SETBKGNDCOLOR, // message to send
                (WPARAM)0,        // system color option
                (LPARAM)colorKey  // color (COLORREF)
    );

    FORMATRANGE fr;

    long lLSX = pDC->GetDeviceCaps(LOGPIXELSX);
    long lPageWidth = ::MulDiv(rect.Width(), 1440, lLSX);

    long lLSY = pDC->GetDeviceCaps(LOGPIXELSY);
    long lPageHeight = ::MulDiv(rect.Height(), 1440, lLSY);

    const int N_ADD = 1000;
    CRect rcPage(0, 0, lPageWidth, lPageHeight + m_ptOffset.y + N_ADD);
    rcPage -= m_ptOffset;

    // Format the text and render it to the DC.
    fr.hdc = pDC->m_hDC;
    fr.hdcTarget = pDC->m_hDC;
    fr.rc = rcPage;
    fr.rcPage = rcPage;
    fr.chrg.cpMin = 0;
    fr.chrg.cpMax = -1;

    int nLastChar = OwnedRender::E2SendMessage(m_hRichEditCtrl, // handle to destination window
                                  EM_FORMATRANGE,  // message to send
                                  (WPARAM)TRUE,    // render option
                                  (LPARAM)&fr      // device data (FORMATRANGE *)
    );

    if (m_nAddScroll > 0)
    {
        if (m_ptOffset.y > N_ADD + lPageHeight + m_nAddScroll)
        {
            m_fScrollSpeed = 0;
            m_nAddScroll = 0;
        }
    }
    else if (nLastChar >= m_nTextLenght)
    {
        m_nAddScroll = m_ptOffset.y;
    }
    ReleaseDC();
#endif // NOGETDC
}

void CI3DTetxRTF::Draw()
{
    if (m_fScrollSpeed)
    {
        float fTime = IWorldClient.GetPreciseTime();
        m_ptOffset.y += (fTime - m_fLastTime) * m_fScrollSpeed;
        m_fLastTime = fTime;
        RenderToDC();
    }
    CI3DSurface::Draw();
}

void CI3DTetxRTF::SetScrollSpeed(float fSpeed, bool bHideStart)
{
    m_nAddScroll = 0;
    if (bHideStart)
    {
#ifndef NOGETDC
        CDC *pDC = GetDC();
        long lLSY = pDC->GetDeviceCaps(LOGPIXELSY);
        long lPageHeight = ::MulDiv(rect.Height(), 1440, lLSY);
        m_ptOffset.y = -lPageHeight;
        ReleaseDC();
#endif // NOGETDC
    }
    else
        m_ptOffset.y = 0;

    m_fScrollSpeed = fSpeed;
    m_fLastTime = IWorldClient.GetPreciseTime();
}