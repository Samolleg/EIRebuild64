// I3DScrollText.h: interface for the CI3DScrollText class.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "I3DScrollBar.h"
class CI3DObjectOutput;

enum ELexemType
{
    LT_UNKNOW = 0,
    LT_STYLE,
    LT_WORD
};
enum EAlignType
{
    AT_UNKNOW = 0,
    AT_LEFT,
    AT_JUSTIFY
};

struct SBasicLexem
{
    ELexemType m_eType;
    SBasicLexem() { m_eType = LT_UNKNOW; }
    virtual ~SBasicLexem() {}
};

struct SStyleLexem : SBasicLexem
{
    bool isNewLine;
    int nFont;
    COLORREF colorText;

    SStyleLexem(int nNewFont)
    {
        m_eType = LT_STYLE;
        isNewLine = false;
        colorText = RGB(220, 200, 1);
        nFont = nNewFont;
    }
    virtual ~SStyleLexem() {}
};

struct SWordLexem : SBasicLexem
{
    CString szWord;
    int nLenght;
    SWordLexem()
    {
        m_eType = LT_WORD;
        nLenght = 0;
    };
    virtual ~SWordLexem() {}
};

typedef CArray<SBasicLexem *, SBasicLexem *> CBasicLexemPtrArray;

struct SScrollTextLine
{
    int nHieght;
    int nLexemIndex;
};

typedef CArray<SScrollTextLine *, SScrollTextLine *> CScrollTextLinePtrArray;

class CI3DScrollText : public CI3DScrollBar
{
    friend struct E3HudAccess;
private:
    int NextLexem(const CString &szText, int nStart, CString &szLexem);
    void AddLexems(const CString &szText); // эта функция не изменяет szText
    void RecalculateRange();
    CSize TextSize(CString szStr, int nFont);
    void DrawCurrentTextPart(int nPos);

    // Geometry data
    CRect m_rectText;
    float m_textScale = 1.f;
    CI3DObjectOutput* messageOutput=nullptr;

    // Text data
    CBasicLexemPtrArray m_aLexems;
    CScrollTextLinePtrArray m_aLines;

public:
    EAlignType p_eAlign;
    CI3DScrollText(CInterface3D *pI, LPCSTR szText, CRect rect, float fDepth = 10);
    virtual ~CI3DScrollText();

    void RemoveAllText();
    void AddText(LPCSTR pszText);
    void SetText(LPCSTR pszText);
    virtual BOOL Draw(CRenderer *pRend);
    virtual void SetRect(CRect rect);
    void SetTextLayout(CRect rect, float textScale);
};
