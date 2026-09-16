#pragma once

#include "Unit.h"
// --------------------------------------------------------------------------------------------
// Interface for the CI3DMapObjTranslation class.
// --------------------------------------------------------------------------------------------
class CI3DMapObjTranslation
{
protected:
    LPUNIT m_pMapObject;

public:
    CI3DMapObjTranslation();
    ~CI3DMapObjTranslation();

    void ShowBodyPart(LPCTSTR pszName, bool bShow);
    void ShowAllBodyParts(bool bShow);

    void PlaySpecialAnmation(int nAnimNumb);
    void PlayIdleAnmation();

    void Draw(CRenderer *pRend);

    void Init(LPCSTR szTemplateName, LPCSTR szPrimTexture, LPCSTR szSecTexture);
    void SetRelativeToScreen(float xScr, float yScr, float zDist);
    void SetOrientation(const CQuaternion &q) { m_pMapObject->SetOrientation(q); }
};
