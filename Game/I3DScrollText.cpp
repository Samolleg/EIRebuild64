// I3DScrollText.cpp: implementation of the CI3DScrollText class.
//
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DMain.h"
#include "I3DScrollText.h"
#include "WorldClient.h"

const int N_DEFAULT_FONT = 1;
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CI3DScrollText::CI3DScrollText(CInterface3D *pI, LPCSTR szText, CRect rect, float fDepth)
    : CI3DScrollBar(pI, rect, fDepth, 0, 20)
{
    AddSurface(dbgnew CI3DSurface(pI3D, rect));
    SetRect(rect);
    SetText(szText);
    SetPos(0);
    p_eAlign = AT_JUSTIFY;
    //	DrawCurrentTextPart(0);
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CI3DScrollText::~CI3DScrollText()
{
    RemoveAllText();

    // Clear old lines
    for (int i = 0; i < m_aLines.GetSize(); i++)
        delete m_aLines[i];
    m_aLines.RemoveAll();
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CI3DScrollText::AddText(LPCSTR pszText)
{
    int nPos;
    int nOldRange, nNewRange;
    SetPosToEnd();
    GetPos(nPos);
    nOldRange = GetRange();
    AddLexems(CString(pszText));
    RecalculateRange();
    nNewRange = GetRange();
    SetPos(nPos + nNewRange - nOldRange);
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CI3DScrollText::SetText(LPCSTR pszText)
{
    RemoveAllText();
    AddLexems(CString(pszText));
    RecalculateRange();
    SetPos(0);
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CI3DScrollText::SetRect(CRect rect)
{
    CI3DScrollBar::SetRect(rect);

    CI3DSurface *pSurface = GetSurface(0);
    if (pSurface)
        pSurface->Move(rect.TopLeft());

    m_rectText = CRect(CPoint(0, 0), rect.Size());
    m_rectText.right -= 14;
    RecalculateRange();
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DScrollText::Draw(CRenderer *pRend)
{
    int nPos;
    if (GetPos(nPos))
        DrawCurrentTextPart(nPos);

    return CI3DScrollBar::Draw(pRend);
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CI3DScrollText::DrawCurrentTextPart(int nPos)
{
#ifndef NOGETDC
    CI3DSurface *pSurface = GetSurface(0);
    CDC *pDC = pSurface->GetDC();
    pSurface->Clear();

    if (m_aLines.GetSize() <= 0)
    {
        pSurface->ReleaseDC();
        return;
    }

    int nHieght = 0;
    int i = 0;
    for (; i < m_aLines.GetSize() && nHieght < nPos; i++)
    {
        nHieght += m_aLines[i]->nHieght;
    }
    ASSERT(i < m_aLines.GetSize()); // Internal error, report to Epik

    int nLine = i - 1;
    if (nLine < 0)
        nLine = 0;
    int nIncriment = 0;
    if (nPos != 0)
        nIncriment = (nHieght - m_aLines[nLine]->nHieght) - nPos;

    int aFontHieght[NUM_TT_FONTS];
    for (i = 0; i < NUM_TT_FONTS; i++)
        aFontHieght[i] = pI3D->TextSize("W", i).cy;

    SBasicLexem *pCurLexem;
    int nFont = N_DEFAULT_FONT;
    COLORREF colorCurrent = RGB(228, 215, 167);
    if (m_aLexems[m_aLines[nLine]->nLexemIndex]->m_eType != LT_STYLE)
    {
        for (i = m_aLines[nLine]->nLexemIndex; i > 0; i--)
            if (m_aLexems[i]->m_eType == LT_STYLE)
            {
                pCurLexem = (SStyleLexem *)m_aLexems[i];
                colorCurrent = ((SStyleLexem *)pCurLexem)->colorText;
                nFont = ((SStyleLexem *)pCurLexem)->nFont;
                break;
            }
    }

    CRect rectLine, rectWord;
    rectLine = m_rectText;
    rectLine.top = nIncriment;
    nHieght = 0;

    do
    {
        rectLine.bottom = rectLine.top + m_aLines[nLine]->nHieght;

        int nLexem = m_aLines[nLine]->nLexemIndex;
        int nLastLexem;
        if (nLine == m_aLines.GetUpperBound())
            nLastLexem = m_aLexems.GetSize();
        else
            nLastLexem = m_aLines[nLine + 1]->nLexemIndex;

        int nSpace = 0;
        for (i = nLexem; i < nLastLexem; i++)
            if (m_aLexems[i]->m_eType == LT_WORD)
                nSpace += ((SWordLexem *)m_aLexems[i])->nLenght;

        if (p_eAlign == AT_LEFT ||
            (m_aLexems[nLastLexem - 1]->m_eType == LT_STYLE &&
             ((SStyleLexem *)m_aLexems[nLastLexem - 1])->isNewLine) ||
            nLastLexem - nLexem < 2 || nLastLexem == m_aLexems.GetSize())
            nSpace = aFontHieght[nFont] / 3;
        else
            nSpace = (m_rectText.Width() - nSpace) / (nLastLexem - nLexem - 1);

        rectWord = rectLine;
        for (; nLexem < nLastLexem; nLexem++)
        {
            pCurLexem = m_aLexems[nLexem];
            switch (pCurLexem->m_eType)
            {
            case LT_WORD: {
                SWordLexem *pLexem = (SWordLexem *)pCurLexem;
                pSurface->DrawText(pLexem->szWord, rectWord, nFont, colorCurrent, DT_LEFT);
                rectWord.left += pLexem->nLenght + nSpace;
            }
            break;
            case LT_STYLE: {
                SStyleLexem *pLexem = (SStyleLexem *)pCurLexem;
                colorCurrent = pLexem->colorText;
                nFont = pLexem->nFont;
            }
            break;
            }
        }

        nHieght += m_aLines[nLine]->nHieght;
        rectLine.top += m_aLines[nLine]->nHieght;
        nLine++;
    } while ((nHieght < m_rectText.Height() - nIncriment) && nLine < m_aLines.GetSize());

    pSurface->ReleaseDC();
#endif // NOGETDC
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CI3DScrollText::RemoveAllText()
{
    for (int i = 0; i < m_aLexems.GetSize(); i++)
        delete m_aLexems[i];
    m_aLexems.RemoveAll();
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CI3DScrollText::AddLexems(const CString &szText)
{
    CString szLexem;
    COLORREF colorCurrent = RGB(228, 215, 167);
    int nCurFont = N_DEFAULT_FONT;
    ELexemType nLexemType;
    SBasicLexem *pCurLexem;

    for (int nTextPos = 0; nTextPos < szText.GetLength() && nTextPos != -1;)
    {
        pCurLexem = NULL;
        do
        {
            nTextPos = NextLexem(szText, nTextPos, szLexem);
            nLexemType = LT_UNKNOW;

            if (nTextPos == -1)
                break;

            if (szLexem.GetLength())
            {
                // Check for right special symbols
                if (szLexem[0] == '\n' ||
                    (szLexem[0] == '\a' && (szLexem[1] == 'C' || szLexem[1] == 'F')))
                {
                    nLexemType = LT_STYLE;
                    if (!pCurLexem)
                    {
                        pCurLexem = dbgnew SStyleLexem(nCurFont);
                        ((SStyleLexem *)pCurLexem)->nFont = nCurFont;
                        ((SStyleLexem *)pCurLexem)->colorText = colorCurrent;
                        ((SStyleLexem *)pCurLexem)->isNewLine = false;
                    }

                    if (szLexem[0] == '\n')
                        ((SStyleLexem *)pCurLexem)->isNewLine = true;
                    else
                        switch (szLexem[1])
                        {
                        case 'C':
                            ASSERT(szLexem.GetLength() == 5); // Wrong \aCrgb format
                            ((SStyleLexem *)pCurLexem)->colorText = RGB(szLexem[2], szLexem[3],
                                                                        szLexem[4]);
                            colorCurrent = ((SStyleLexem *)pCurLexem)->colorText;
                            break;

                        case 'F':
                            ASSERT(szLexem.GetLength() == 3); // Wrong \aFn format
                            ((SStyleLexem *)pCurLexem)->nFont = szLexem[2] - '0';
                            ((SStyleLexem *)pCurLexem)->isNewLine = m_aLexems.GetSize() != 0;
                            nCurFont = szLexem[2] - '0';
                            break;
                        }
                }
                else
                    nLexemType = LT_WORD;
            }
        } while (nLexemType == LT_STYLE);

        if (pCurLexem)
            m_aLexems.Add(pCurLexem); // Add style lexem

        if (nLexemType == LT_WORD)
        {
            pCurLexem = dbgnew SWordLexem;
            ((SWordLexem *)pCurLexem)->nLenght = pI3D->TextSize(szLexem, nCurFont).cx;
            ((SWordLexem *)pCurLexem)->szWord = szLexem;
            m_aLexems.Add(pCurLexem); // Add word lexem
        }
    }
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int CI3DScrollText::NextLexem(const CString &szText, int nStart, CString &szLexem)
{
    int nLastPos;

    // Skip spaces
    for (; nStart < szText.GetLength() && (szText[nStart] == ' ' || szText[nStart] == '\t');
         nStart++)
        ;

    if (nStart >= szText.GetLength())
        return -1;

    // Extract lexem
    CString szTemp = CString(szText.operator LPCSTR() + nStart);
    if (szTemp[0] == '\a')
    {
        if (szTemp[1] == 'F')
            nLastPos = 3;
        else if (szTemp[1] == 'C')
            nLastPos = 5;
        else
            ReportError("ScrollText::Incorrect special symbol '%c'", szTemp[1]);
    }
    else if (szTemp[0] == '\n')
        nLastPos = 1;
    else
        nLastPos = szTemp.FindOneOf(" \t\a\n");

    if (nLastPos == -1)
        nLastPos = szTemp.GetLength();

    szLexem = szTemp.Left(nLastPos);

    return nLastPos + nStart;
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CI3DScrollText::RecalculateRange()
{
    int nMaxWidth = m_rectText.Width();
    int nWidth;
    int nHieght = 0;
    int nFont = N_DEFAULT_FONT;

    int aFontHieght[NUM_TT_FONTS];
    for (int i = 0; i < NUM_TT_FONTS; i++)
        aFontHieght[i] = pI3D->TextSize("W", i).cy;

    // Clear old lines
    for (int i = 0; i < m_aLines.GetSize(); i++)
        delete m_aLines[i];
    m_aLines.RemoveAll();

    for (int i = 0; i < m_aLexems.GetSize(); i++)
    {
        nWidth = 0;

        SScrollTextLine *pLine = dbgnew SScrollTextLine;
        pLine->nHieght = aFontHieght[nFont];
        pLine->nLexemIndex = i;
        m_aLines.Add(pLine);

        for (; i < m_aLexems.GetSize() && nWidth < nMaxWidth; i++)
            switch (m_aLexems[i]->m_eType)
            {
            case LT_WORD: {
                SWordLexem *pWord = (SWordLexem *)m_aLexems[i];
                nWidth += pWord->nLenght + aFontHieght[nFont] / 3; // Word lenght + minimum space
                if (nWidth >= nMaxWidth)
                {
                    if (pLine->nLexemIndex != i)
                        i--;
                    nHieght += aFontHieght[nFont];
                    goto EndFor;
                }
            }
            break;
            case LT_STYLE: {
                SStyleLexem *pWord = (SStyleLexem *)m_aLexems[i];
                if (pWord->isNewLine)
                {
                    nHieght += aFontHieght[nFont];
                    nFont = pWord->nFont;
                    ASSERT(nWidth); // Кто-то хочет вывести пустую строку

                    /*							if ( nWidth == 0 )
                                                                            {
                                                                                    ASSERT(0); //
                       Кто-то хочет вывести пустую строку
                                                                                    m_aLines[m_aLines.GetUpperBound()]->nLexemIndex++;
                                                                            }
                                                                            else*/
                    goto EndFor;
                }
            }
            break;
            }
        nHieght += aFontHieght[nFont];
    EndFor:;
    }

    SetRange(nHieght - m_rectText.Height());
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
