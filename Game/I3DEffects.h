#pragma once
// I3DEffects.h: interface for the CI3DEffects class.
//
//////////////////////////////////////////////////////////////////////

class CI3DFigure;
class CRenderer;

class CI3DEffects
{
private:
    float m_fFadeoutStep;
    float m_fFadeoutValue;
    bool m_isFadeout;
    int m_nLastTime;

    bool m_isCashe;

public:
    CI3DEffects();
    ~CI3DEffects() = default;

    BOOL Draw(CRenderer *pRend);
    void Draw2D();

    void Fadeout(int nMilliSec);
    void FadeoutOff();
    void Cashe(bool isShow);
};
