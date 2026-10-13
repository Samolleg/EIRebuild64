#include "StdAfx.h"
#include "GameJournal.h"
#include <typeinfo>
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DMain.h"
#include "I3DObject.h"
#include "I3DToolTip.h"
#include "I3DScreen.h"
#include "I3DCursor.h"
#include "WorldClient.h"
/////////////////////////////////////////////////////////////////////////////////////
CI3DScreen::CI3DScreen(CInterface3D *pI)
    : CI3DObject(pI)
{
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DScreen::~CI3DScreen()
{
    RemoveAllHandlers();
    RemoveAllObjects();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreen::AddHandler(CI3DHandler *pHandler)
{
    if (pHandler)
        pHandlers.push_back(pHandler);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreen::RemoveAllHandlers()
{
    for (auto ci = pHandlers.begin(); ci != pHandlers.end(); ++ci)
        delete *ci;
    pHandlers.clear();
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DObject *CI3DScreen::AddObject(CI3DObject *pObj)
{
    if (pObj)
        pObjects.Add(pObj);
    return pObj;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreen::RemoveAllObjects()
{
    for (int i = 0; i < pObjects.GetSize(); i++)
        delete pObjects[i];
    pObjects.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DScreen::Draw(CRenderer *pRend)
{
    if (!pI3D->IsRendered())
        return TRUE;
    pI3D->GetRenderer()->SetTexturingAlphaMode(AM_MULTIPLY);
    CI3DObject::Draw(pRend);
    for (int i = 0; i < pObjects.GetSize(); i++)
        if (pObjects[i]->IsVisible())
            pObjects[i]->Draw(pRend);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreen::DrawTips(CRenderer *pRend)
{
    if (!pI3D->IsRendered())
        return;

    DWORD dwAlpha;
    pI3D->GetRenderer()->GetRenderState(D3DRENDERSTATE_ALPHAREF, &dwAlpha);
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHAREF, 1);

    CI3DObject::DrawTips(pRend);
    for (int i = 0; i < pObjects.GetSize(); i++)
        if (pObjects[i]->IsVisible())
            pObjects[i]->DrawTips(pRend);

    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHAREF, dwAlpha);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreen::Draw2D()
{
    if (!pI3D->IsRendered())
        return;
    CI3DObject::Draw2D();
    for (int i = 0; i < pObjects.GetSize(); i++)
        if (pObjects[i]->IsVisible())
            pObjects[i]->Draw2D();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreen::Draw2DTips()
{
    if (!pI3D->IsRendered())
        return;
    CI3DObject::Draw2DTips();
    for (int i = 0; i < pObjects.GetSize(); i++)
        if (pObjects[i]->IsVisible())
            pObjects[i]->Draw2DTips();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreen::OnIdle()
{
    for (int i = 0; i < pObjects.GetSize(); i++)
        pObjects[i]->OnIdle();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreen::OnLButtonDown(UINT nFlags, CPoint point)
{
    SetProcessed(false);
    std::vector<CI3DHandler *>::const_iterator ci;
    for (ci = pHandlers.begin(); ci != pHandlers.end(); ++ci)
    {
        (*ci)->SetProcessed(false);
        (*ci)->OnLButtonDown(nFlags, point);
        if ((*ci)->GetProcessed())
        {
            SetProcessed();
            return;
        }
    }
    for (int i = 0; i < pObjects.GetSize(); i++)
        if (pObjects[i]->IsVisible() && pObjects[i]->IsInside(point))
        {
            pObjects[i]->SetProcessed(false);
            pObjects[i]->OnLButtonDown(nFlags, point);
            if (pObjects[i]->GetProcessed())
            {
                SetProcessed();
                return;
            }
        }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreen::OnLButtonUp(UINT nFlags, CPoint point)
{
    SetProcessed(false);
    std::vector<CI3DHandler *>::const_iterator ci;
    for (ci = pHandlers.begin(); ci != pHandlers.end(); ++ci)
    {
        (*ci)->SetProcessed(false);
        (*ci)->OnLButtonUp(nFlags, point);
        if ((*ci)->GetProcessed())
        {
            SetProcessed();
            return;
        }
    }
    for (int i = 0; i < pObjects.GetSize(); i++)
        if (pObjects[i]->IsVisible() && pObjects[i]->IsInside(point))
        {
            pObjects[i]->SetProcessed(false);
            pObjects[i]->OnLButtonUp(nFlags, point);
            if (pObjects[i]->GetProcessed())
            {
                SetProcessed();
                return;
            }
        }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreen::OnMouseMove(UINT nFlags, CPoint point)
{
    SetProcessed(false);
    std::vector<CI3DHandler *>::const_iterator ci;
    for (ci = pHandlers.begin(); ci != pHandlers.end(); ++ci)
    {
        (*ci)->SetProcessed(false);
        (*ci)->OnMouseMove(nFlags, point);
        if ((*ci)->GetProcessed())
        {
            SetProcessed();
            return;
        }
    }
    for (int i = 0; i < pObjects.GetSize(); i++)
        if (pObjects[i]->IsVisible() && pObjects[i]->IsInside(point))
        {
            pObjects[i]->SetProcessed(false);
            pObjects[i]->OnMouseMove(nFlags, point);
            if (pObjects[i]->GetProcessed())
            {
                SetProcessed();
                return;
            }
        }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreen::OnRButtonDown(UINT nFlags, CPoint point)
{
    SetProcessed(false);
    std::vector<CI3DHandler *>::const_iterator ci;
    for (ci = pHandlers.begin(); ci != pHandlers.end(); ++ci)
    {
        (*ci)->SetProcessed(false);
        (*ci)->OnRButtonDown(nFlags, point);
        if ((*ci)->GetProcessed())
        {
            SetProcessed();
            return;
        }
    }
    for (int i = 0; i < pObjects.GetSize(); i++)
        if (pObjects[i]->IsVisible() && pObjects[i]->IsInside(point))
        {
            pObjects[i]->SetProcessed(false);
            pObjects[i]->OnRButtonDown(nFlags, point);
            if (pObjects[i]->GetProcessed())
            {
                SetProcessed();
                return;
            }
        }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreen::OnRButtonUp(UINT nFlags, CPoint point)
{
    SetProcessed(false);
    std::vector<CI3DHandler *>::const_iterator ci;
    for (ci = pHandlers.begin(); ci != pHandlers.end(); ++ci)
    {
        (*ci)->SetProcessed(false);
        (*ci)->OnRButtonUp(nFlags, point);
        if ((*ci)->GetProcessed())
        {
            SetProcessed();
            return;
        }
    }
    for (int i = 0; i < pObjects.GetSize(); i++)
        if (pObjects[i]->IsVisible() && pObjects[i]->IsInside(point))
        {
            pObjects[i]->SetProcessed(false);
            pObjects[i]->OnRButtonUp(nFlags, point);
            if (pObjects[i]->GetProcessed())
            {
                SetProcessed();
                return;
            }
        }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreen::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    SetProcessed(false);
    std::vector<CI3DHandler *>::const_iterator ci;
    for (ci = pHandlers.begin(); ci != pHandlers.end(); ++ci)
    {
        (*ci)->SetProcessed(false);
        (*ci)->OnLButtonDblClk(nFlags, point);
        if ((*ci)->GetProcessed())
        {
            SetProcessed();
            return;
        }
    }
    for (int i = 0; i < pObjects.GetSize(); i++)
        if (pObjects[i]->IsVisible() && pObjects[i]->IsInside(point))
        {
            pObjects[i]->SetProcessed(false);
            pObjects[i]->OnLButtonDblClk(nFlags, point);
            if (pObjects[i]->GetProcessed())
            {
                SetProcessed();
                return;
            }
        }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreen::OnRButtonDblClk(UINT nFlags, CPoint point)
{
    SetProcessed(false);
    std::vector<CI3DHandler *>::const_iterator ci;
    for (ci = pHandlers.begin(); ci != pHandlers.end(); ++ci)
    {
        (*ci)->SetProcessed(false);
        (*ci)->OnRButtonDblClk(nFlags, point);
        if ((*ci)->GetProcessed())
        {
            SetProcessed();
            return;
        }
    }
    for (int i = 0; i < pObjects.GetSize(); i++)
        if (pObjects[i]->IsVisible() && pObjects[i]->IsInside(point))
        {
            pObjects[i]->SetProcessed(false);
            pObjects[i]->OnRButtonDblClk(nFlags, point);
            if (pObjects[i]->GetProcessed())
            {
                SetProcessed();
                return;
            }
        }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DScreen::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    SetProcessed(false);
    std::vector<CI3DHandler *>::const_iterator ci;
    for (ci = pHandlers.begin(); ci != pHandlers.end(); ++ci)
    {
        (*ci)->SetProcessed(false);
        (*ci)->OnMouseWheel(nFlags, zDelta, pt);
        if ((*ci)->GetProcessed())
        {
            SetProcessed();
            return TRUE;
        }
    }
    for (int i = 0; i < pObjects.GetSize(); i++)
        if (pObjects[i]->IsVisible() && pObjects[i]->IsInside(pt))
        {
            pObjects[i]->SetProcessed(false);
            pObjects[i]->OnMouseWheel(nFlags, zDelta, pt);
            if (pObjects[i]->GetProcessed())
            {
                SetProcessed();
                return TRUE;
            }
        }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreen::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    GameJournal::Event("INPUT", "key", "class=%s key=%u repeat=%u flags=%u", GameJournal::Quote(typeid(*this).name()).c_str(), nChar, nRepCnt, nFlags);
    SetProcessed(false);
    std::vector<CI3DHandler *>::const_iterator ci;
    for (ci = pHandlers.begin(); ci != pHandlers.end(); ++ci)
    {
        (*ci)->SetProcessed(false);
        (*ci)->OnKeyDown(nChar, nRepCnt, nFlags);
        if ((*ci)->GetProcessed())
        {
            SetProcessed();
            return;
        }
    }
    for (int i = 0; i < pObjects.GetSize(); i++)
        if (pObjects[i]->IsVisible())
        {
            pObjects[i]->SetProcessed(false);
            pObjects[i]->OnKeyDown(nChar, nRepCnt, nFlags);
            if (pObjects[i]->GetProcessed())
            {
                SetProcessed();
                return;
            }
        }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreen::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    SetProcessed(false);
    std::vector<CI3DHandler *>::const_iterator ci;
    for (ci = pHandlers.begin(); ci != pHandlers.end(); ++ci)
    {
        (*ci)->SetProcessed(false);
        (*ci)->OnChar(nChar, nRepCnt, nFlags);
        if ((*ci)->GetProcessed())
        {
            SetProcessed();
            return;
        }
    }
    for (int i = 0; i < pObjects.GetSize(); i++)
        if (pObjects[i]->IsVisible())
        {
            pObjects[i]->SetProcessed(false);
            pObjects[i]->OnChar(nChar, nRepCnt, nFlags);
            if (pObjects[i]->GetProcessed())
            {
                SetProcessed();
                return;
            }
        }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreen::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    SetProcessed(false);
    std::vector<CI3DHandler *>::const_iterator ci;
    for (ci = pHandlers.begin(); ci != pHandlers.end(); ++ci)
    {
        (*ci)->SetProcessed(false);
        (*ci)->OnKeyUp(nChar, nRepCnt, nFlags);
        if ((*ci)->GetProcessed())
        {
            SetProcessed();
            return;
        }
    }
    for (int i = 0; i < pObjects.GetSize(); i++)
        if (pObjects[i]->IsVisible())
        {
            pObjects[i]->SetProcessed(false);
            pObjects[i]->OnKeyUp(nChar, nRepCnt, nFlags);
            if (pObjects[i]->GetProcessed())
            {
                SetProcessed();
                return;
            }
        }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreen::ResetInputState()
{
    std::vector<CI3DHandler *>::const_iterator ci;
    for (ci = pHandlers.begin(); ci != pHandlers.end(); ++ci)
        (*ci)->ResetInputState();
    for (int i = 0; i < pObjects.GetSize(); i++)
        pObjects[i]->ResetInputState();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreen::MemoryBackup(BOOL bShow)
{
    CI3DObject::MemoryBackup(bShow);
    for (int i = 0; i < pObjects.GetSize(); i++)
        if (pObjects[i]->IsVisible())
            pObjects[i]->MemoryBackup(bShow);
}
/////////////////////////////////////////////////////////////////////////////////////
