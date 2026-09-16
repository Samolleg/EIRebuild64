#pragma once
#include "SmartPtr.h"
#include "GameBase.h"
#include "TypeIDs.h"

class CTextureDescriptor
{
    friend class CRenderer;

    CSmartPtr<CGameBase> pTexture;
    BOOL FinalDataLoading();

public:
    void Clear() { pTexture = 0; }
    void SetName(LPCSTR pszName);
    void SetDesc(ETypeIDs tid, LPCSTR pszName);
};