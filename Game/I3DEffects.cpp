// I3DEffects.cpp: implementation of the CI3DEffects class.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Renderer.h"
#include "I3DEffects.h"
#include "I3DScreen.h"
#include "I3DMain.h"
#include "I3DFigure.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
const int nStartAlpha = 0;
const int nFinalAlpha = 255;

CI3DEffects::CI3DEffects()
{
    m_fFadeoutValue = 0;
    m_isFadeout = false;
    m_isCashe = false;
}

void CI3DEffects::FadeoutOff()
{
    m_isFadeout = false;
}

BOOL CI3DEffects::Draw(CRenderer *pRend)
{
    CRect rectScreen = pRend->GetWindowRect();

    if (m_isCashe)
    {
        int nBorder = 0.217f * float(rectScreen.Height());
        pRend->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_FALSE);
        pRend->DrawFrameSelection(0, 0, rectScreen.Width(), nBorder, char(255), false);
        pRend->DrawFrameSelection(0, rectScreen.Height() - nBorder, rectScreen.Width(),
                                  rectScreen.Height(), char(255), false);
        pRend->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_TRUE);
    }

    if (m_isFadeout)
    {
        if ((m_fFadeoutValue < float(nFinalAlpha) && m_fFadeoutStep > 0) ||
            (m_fFadeoutValue > float(nStartAlpha) && m_fFadeoutStep < 0))
        {
            m_fFadeoutValue += m_fFadeoutStep * (timeGetTime() - m_nLastTime);
            m_nLastTime = timeGetTime();
        }

        if (m_fFadeoutValue > nFinalAlpha)
            m_fFadeoutValue = nFinalAlpha;
        else if (m_fFadeoutValue < nStartAlpha)
        {
            FadeoutOff();
            return true;
        }

        pRend->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_FALSE);
        CRect rectScreen = pRend->GetWindowRect();
        pRend->DrawFrameSelection(0, 0, rectScreen.Width(), rectScreen.Height(),
                                  int(m_fFadeoutValue), false);
        pRend->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_TRUE);
    }
    return true;
}

void CI3DEffects::Draw2D()
{
}

void CI3DEffects::Fadeout(int nMilliSec)
{
    m_fFadeoutStep = float(nFinalAlpha - nStartAlpha) / nMilliSec;
    if (nMilliSec > 0)
        m_fFadeoutValue = nStartAlpha;
    else
        m_fFadeoutValue = nFinalAlpha;

    m_nLastTime = timeGetTime();
    m_isFadeout = true;
}

void CI3DEffects::Cashe(bool isShow)
{
    m_isCashe = isShow;
}