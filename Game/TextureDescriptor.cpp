#include "stdafx.h"
#include "WorldClient.h"
#include "TextureManager.h"

BOOL CTextureDescriptor::FinalDataLoading()
{
    if (pTexture)
    {
        CGameBase *pTex = pTexture;
        if (!pTex->CheckFlag(TEX_LOADED))
            return theWorld.GetTextureManager()->LoadTexture(dynamic_cast<CTexture *>(pTex));
        else
            return TRUE;
    }
    else
        return ReportError("Final loading data to NULL texture!\n");
}

void CTextureDescriptor::SetName(LPCSTR pszName)
{
    SetDesc(TCTID_STATIC_TEXTURE, pszName);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CSmartTexture::SetDesc( ETypeIDs tid, LPCSTR pszName )
// set name to texture and create 'tid' texture object if need
void CTextureDescriptor::SetDesc(ETypeIDs tid, LPCSTR pszName)
{
    if (pTexture)
        pTexture = 0;

    CTextureManager *pTM = theWorld.GetTextureManager();
    CTexture *pTex = pTM->FindTexture(tid, pszName);
    if (!pTex)
        pTex = pTM->CreateTexture(tid);
    pTex->SetName(pszName);
    pTexture = pTex;
}