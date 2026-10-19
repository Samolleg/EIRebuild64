// I3DMapObjTranslation.cpp: implementation of the CI3DMapObjTranslation class.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DFigure.h"
#include "WideScreen.h"
#include "I3DMapObjTranslation.h"
#include "WorldClient.h"
#include "Object3DClientSpecific.h"
#include "LightManager.h"
#include "MapObjectManager.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CI3DMapObjTranslation::CI3DMapObjTranslation()
{
    m_pMapObject = NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DMapObjTranslation::~CI3DMapObjTranslation()
{
    if (m_pMapObject)
        m_pMapObject->Release();
    m_pMapObject = NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DMapObjTranslation::Init(LPCSTR szTemplateName, LPCSTR szPrimTexture, LPCSTR szSecTexture)
{
    CWorldClient *lpWorld = &theWorld;
    m_pMapObject = (CUnit *)lpWorld->GetMapObjectManager()->CreateData(MOTID_MONSTER, -2000000000);
    m_pMapObject->AddRef();
    m_pMapObject->SetFlag(UNIT_DONTMOVE);

    CObject3DClientSpecific *lpObjectSpecific = (CObject3DClientSpecific *)
                                                    m_pMapObject->GetObjectSpecific();

    m_pMapObject->SetCP(CPlot3D(0, 0, 3));

    CStringArray szstrarr;
    m_pMapObject->LoadByTemplate(szTemplateName, szstrarr);
    lpObjectSpecific->SetMaterials();
    m_pMapObject->LoadAnimation(szTemplateName);
    m_pMapObject->InitAnimation();
    lpObjectSpecific->LoadTextures(szPrimTexture, szSecTexture);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DMapObjTranslation::Draw(CRenderer *pRend)
{
    DWORD oldamb;
    oldamb = pRend->GetAmbientColor();
    pRend->SetAmbientColor(1, 1, 1);

    m_pMapObject->SetFlag(OBJ_USE_DEFAULT_FOG);
    m_pMapObject->SetExactPosition(timeGetTime() / 67.0f);
    m_pMapObject->Draw(pRend);
    m_pMapObject->ResetFlag(OBJ_USE_DEFAULT_FOG);

    pRend->SetAmbientColor(oldamb);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DMapObjTranslation::PlaySpecialAnmation(int nAnimNumb)
{
    CUnit *lpUnit = (CUnit *)m_pMapObject;
    const SADEntry *pEntry = lpUnit->GetSpecialAnimationEntry(nAnimNumb + 1);
    lpUnit->SetNextAnimation(pEntry, TRUE);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DMapObjTranslation::PlayIdleAnmation()
{
    CMapObjectManager *lpMapObjectManager = (CMapObjectManager *)theWorld.GetMapObjectManager();
    CUnit *lpUnit = (CUnit *)m_pMapObject;
    CString szUnitMaskName = lpUnit->GetMaskName();
    SADEntry *lpEntry = lpMapObjectManager->GetAnimationEntry(szUnitMaskName,
                                                              ADB_STATE_ATTACK | ADB_STAGE_CYCLE |
                                                                  ADB_ACTION_IDLE |
                                                                  ADB_MODIFIER_IDLE_STAY);
    lpUnit->SetNextAnimation(lpEntry, TRUE);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DMapObjTranslation::SetRelativeToScreen(float xScr, float yScr, float zDist)
{
    const auto p=owned::widescreen::native_camera_point(WideScreen::PlaceX(xScr),WideScreen::PlaceY(yScr),zDist);
    m_pMapObject->SetCP(CPlot3D(p.x,p.y,p.z));
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DMapObjTranslation::ShowBodyPart(LPCTSTR pszName, bool bShow)
{
    CFigure *pFig = m_pMapObject->FindFigure(pszName);
    if (pFig)
    {
        if (bShow)
            pFig->ResetFlag(FIG_HIDDEN);
        else
            pFig->SetFlag(FIG_HIDDEN);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DMapObjTranslation::ShowAllBodyParts(bool bShow)
{
    CFigure *pFig = 0;
    for (int i = 0; i < m_pMapObject->GetNumFigures(); i++)
    {
        pFig = m_pMapObject->GetFigure(i);
        if (pFig)
        {
            if (bShow)
                pFig->ResetFlag(FIG_HIDDEN);
            else
                pFig->SetFlag(FIG_HIDDEN);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
