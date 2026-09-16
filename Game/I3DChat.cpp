#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DChat.h"
#include "WorldInterface.h"
#include "ChatServer.h"
/////////////////////////////////////////////////////////////////////////////////////
const int CHAT_SHIFT = 16;
const int CHAT_LINE_X = 780;
const int CHAT_LINE_Y = 17;
const int CHAT_REMOVE_TIME = 20000;
const int CHAT_NUM_LINES = 12;
/////////////////////////////////////////////////////////////////////////////////////
SChatLine::SChatLine()
{
    dwAppearTime = 0;
    pSurface = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
SChatLine::~SChatLine()
{
    if (pSurface)
        delete pSurface;
    pSurface = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DChat::CI3DChat(CInterface3D *pI)
    : CI3DObject(pI)
{
    AddSurface(dbgnew CI3DSurface(pI3D, 10, 590 - CHAT_LINE_Y, 10 + CHAT_LINE_X, 590, BS_VISIBLE));
    bEntering = false;
    nCursorPos = 0;
    RedrawChatPhrase();
}
/////////////////////////////////////////////////////////////////////////////////////
class CNeedDeleteChatLineFunctional
{
    DWORD dwCurrTime;

public:
    explicit CNeedDeleteChatLineFunctional(const DWORD dwCurrentTime)
        : dwCurrTime(dwCurrentTime)
    {
    }
    bool operator()(const SChatLine &line) const
    {
        return (int)dwCurrTime - (int)line.dwAppearTime > CHAT_REMOVE_TIME;
    }
};
/////////////////////////////////////////////////////////////////////////////////////
void CI3DChat::OnIdle()
{
    DWORD dwCurrentTime = timeGetTime();
    mLines.remove_if(CNeedDeleteChatLineFunctional(dwCurrentTime));
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DChat::Draw2D()
{
    int n = 0;
    std::list<SChatLine>::const_iterator it;
    for (n = 0, it = mLines.begin(); it != mLines.end(); ++it, ++n)
    {
        it->pSurface->Move(CPoint(10, 10 + n * CHAT_SHIFT));
        it->pSurface->Draw();
    }
    CI3DObject::Draw2D();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DChat::NewPhrase(LPCTSTR pszPhrase, int nOrder)
{
    int nFont = 1;

    int nLines;
    std::list<SChatLine>::const_iterator it;
    for (nLines = 0, it = mLines.begin(); it != mLines.end(); ++it, ++nLines)
        ;

    CStringArray szFText, szFText2;
    CString szWholePhrase(pszPhrase);
    /*
    int nWidth = pI3D->TextSize( szWholePhrase, nFont ).cx;
    if ( nWidth <= CHAT_LINE_X )
            szFText.Add( szWholePhrase );
    else
    */
    {
        CString szChar;
        CString szWord, szWord2;
        CString szLine, szLine2;
        unsigned int iChar;
        LPCTSTR ptr = (LPCTSTR)szWholePhrase;
        int nLine = 0;
        bool bKey = false;
        while (_tcsnextc(ptr) != 0)
        {
            if (isleadbyte(*ptr))
            {
                szChar = *ptr;
                szChar += *(ptr + 1);
            }
            else
                szChar = *ptr;

            iChar = _tcsnextc(ptr);
            if (bKey)
            {
                if (iChar >= '0' && iChar < '8')
                    szWord2 += szChar;
                else
                    szWord2 += "7";
                bKey = false;
            }
            else
                switch (iChar)
                {
                case '^':
                    szWord2 += szChar;
                    bKey = true;
                    break;

                case ' ':
                    szWord += szChar;
                    szLine += szWord;
                    szWord.Empty();

                    szWord2 += szChar;
                    szLine2 += szWord2;
                    szWord2.Empty();
                    break;

                default:
                    if (pI3D->TextSize(szLine + szWord + szChar, nFont).cx > CHAT_LINE_X)
                    {
                        if (!szLine.IsEmpty())
                        {
                            szFText.Add(szLine);
                            szFText2.Add(szLine2);
                        }
                        szLine.Empty();
                        szLine2.Empty();
                        if (pI3D->TextSize(szWord + szChar, nFont).cx > CHAT_LINE_X)
                        {
                            if (!szWord.IsEmpty())
                            {
                                szFText.Add(szWord);
                                szFText2.Add(szWord2);
                            }
                            szWord.Empty();
                            szWord2.Empty();
                        }
                    }
                    szWord += szChar;
                    szWord2 += szChar;
                    break;
                }
            ptr = _tcsinc(ptr);
        }
        if (bKey)
            szWord2 += "7";
        if (!szLine.IsEmpty() || !szWord.IsEmpty())
        {
            szFText.Add(szLine + szWord);
            szFText2.Add(szLine2 + szWord2);
        }
    }

    for (int i = 0; i < nLines + szFText.GetSize() - CHAT_NUM_LINES; i++)
        mLines.pop_front();
    DWORD dwCurrentTime = timeGetTime();
    COLORREF cr = RGB(255, 255, 255);
    for (int i = 0; i < szFText.GetSize(); i++)
    {
        mLines.emplace_back();
        SChatLine &line = mLines.back();

        line.szText = szFText[i];
        line.dwAppearTime = dwCurrentTime;
        std::unique_ptr<CI3DSurface> pSurf = std::make_unique<CI3DSurface>(
            pI3D, 0, 0, pI3D->TextSize(szFText[i], nFont).cx + 3,
            CHAT_LINE_Y); //, BS_VISIBLE );
#ifndef NOGETDC
        CDC *pDC = pSurf->GetDC();
        if (!pDC)
            return;
        pSurf->Clear();
#endif // NOGETDC
        int nBegin = 0;
        int nEnd = 0;
        int nShift = 0;
        CString szPart;
        CString szWord;
        while ((nEnd = szFText2[i].Find("^", nBegin)) != -1)
        {
            szWord = szFText2[i].Mid(nBegin, nEnd - nBegin);
#ifndef NOGETDC
            pSurf->DrawText(szWord, CRect(nShift, 0, CHAT_LINE_X + 3, CHAT_LINE_Y), nFont, cr,
                            DT_NOPREFIX);
#endif // NOGETDC
            szPart += szWord;
            nShift = pI3D->TextSize(szPart, nFont).cx;
            nEnd++;
            int nColor = szFText2[i][nEnd] - '0';
            cr = RGB((nColor & 4) ? 255 : 0, (nColor & 2) ? 255 : 0, (nColor & 1) ? 255 : 0);
            nBegin = nEnd + 1;
        }
        szWord = szFText2[i].Mid(nBegin);
#ifndef NOGETDC
        if (!szWord.IsEmpty())
            pSurf->DrawText(szWord, CRect(nShift, 0, CHAT_LINE_X + 3, CHAT_LINE_Y), nFont, cr,
                            DT_NOPREFIX);
        pSurf->ReleaseDC();
#endif // NOGETDC
        line.pSurface = pSurf.get();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DChat::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    if (!bEntering)
        return;
    SetProcessed();
    switch (nChar)
    {
    case 13: {
        if (!szChatPhrase.IsEmpty() && IWorldConnector.IsLMP())
        {
            CChatCSay *pObj = dbgnew CChatCSay();
            pObj->szText = szChatPhrase;
            pChatClient->Command(pObj, 60);
        }
        bEntering = false;
        RedrawChatPhrase();
    }
    break;
    case VK_ESCAPE:
        bEntering = false;
        RedrawChatPhrase();
        break;
    case VK_BACK:
        if (nCursorPos > 0)
        {
            szChatPhrase = szChatPhrase.Left(nCursorPos - 1) + szChatPhrase.Mid(nCursorPos);
            --nCursorPos;
        }
        RedrawChatPhrase();
        break;
    case VK_DELETE:
        if (nCursorPos < szChatPhrase.GetLength())
            szChatPhrase = szChatPhrase.Left(nCursorPos) + szChatPhrase.Mid(nCursorPos + 1);
        RedrawChatPhrase();
        break;
    case VK_LEFT:
        --nCursorPos;
        RedrawChatPhrase();
        break;
    case VK_RIGHT:
        ++nCursorPos;
        RedrawChatPhrase();
        break;
    case VK_HOME:
        nCursorPos = 0;
        RedrawChatPhrase();
        break;
    case VK_END:
        nCursorPos = szChatPhrase.GetLength();
        RedrawChatPhrase();
        break;
    }
}
////////////////////////////////////////////////////////////////////////////////
void CI3DChat::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    if (!bEntering)
        return;
    switch (nChar)
    {
    case VK_BACK:
    case VK_TAB:
    case VK_ESCAPE:
    case 13:
        break;

    default: {
        int nFont = 1;
        CString szNew = szChatPhrase.Left(nCursorPos) + (char)nChar + szChatPhrase.Mid(nCursorPos);
        if (!pI3D->IsCtrlDown() && szChatPhrase.GetLength() < 200 &&
            pI3D->TextSize(szNew, nFont).cx < CHAT_LINE_X)
        {
            szChatPhrase = szNew;
            nCursorPos++;
        }
    }
        RedrawChatPhrase();
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DChat::SetEntering(bool bSet)
{
    bEntering = bSet;
    if (bSet)
    {
        szChatPhrase.Empty();
        nCursorPos = 0;
    }
    RedrawChatPhrase();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DChat::RedrawChatPhrase()
{
    if (!bEntering)
    {
        surfaces[0]->Show(FALSE);
        return;
    }
    else
        surfaces[0]->Show(TRUE);

    if (nCursorPos > szChatPhrase.GetLength())
        nCursorPos = szChatPhrase.GetLength();
    if (nCursorPos < 0)
        nCursorPos = 0;

    int nFont = 1;

    CI3DSurface *pSurf = surfaces[0];
#ifndef NOGETDC
    CDC *pDC = pSurf->GetDC();
    if (!pDC)
        return;
    pSurf->Clear();
    pSurf->DrawText(szChatPhrase, CRect(0, 0, CHAT_LINE_X + 3, CHAT_LINE_Y), nFont,
                    RGB(255, 255, 255), DT_LEFT | DT_NOPREFIX);
    CSize sizeCursor = pI3D->TextSize(szChatPhrase.Left(nCursorPos), nFont);
    if (sizeCursor.cx < CHAT_LINE_X)
    {
        CPen pen(PS_SOLID, 1, RGB(255, 255, 255));
        CPen *pOldPen = pDC->SelectObject(&pen);
        CPoint p1(sizeCursor.cx, 0);
        CPoint p2(sizeCursor.cx, CHAT_LINE_Y - 1);
        pI3D->VirtualToScreenLocal(p1);
        pI3D->VirtualToScreenLocal(p2);
        pDC->MoveTo(p1.x, p1.y);
        pDC->LineTo(p2.x, p2.y);
        pDC->SelectObject(pOldPen);
    }
    pSurf->ReleaseDC();
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DChat::ClearLines()
{
    mLines.clear();
}
/////////////////////////////////////////////////////////////////////////////////////
