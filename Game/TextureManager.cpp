#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "TextureManager.h"

#include "mmpFormat.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "WorldClient.h"
#include "Renderer.h"
/////////////////////////////////////////////////////////////////////////////////////
CTextureManager::CTextureManager()
{
    nTotalLRUSize = 0;
    wStartMipMapLevel = 0;
    pRend = 0;

#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CTextureManager::~CTextureManager()
{
    Destroy();
    SetInterface(0);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTextureManager::SetInterface( CRenderer* pNewRend )
// set IDirect3DDevice# interface for further use during texture creating/loading/etc.
void CTextureManager::SetInterface(CRenderer *pNewRend)
{
    pRend = pNewRend;
    if (pRend)
    {
        switch (theSettings.nTextureQuality)
        {
        case 0:
            wStartMipMapLevel = 0;
            break;
        case 1:
            wStartMipMapLevel = 1;
            break;
        case 2:
            wStartMipMapLevel = 2;
            break;
        default:
            wStartMipMapLevel = 3;
        }
        nAllowedLRUSize = 1024 * 1024 * 128;
    }
    else
    {
        wStartMipMapLevel = 2;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CTextureManager::Create()
// create internal data and open resource
BOOL CTextureManager::Create()
{
    if (CResourceManager::Create() == FALSE)
        return FALSE;
    // delete unit layer textures
    DeleteUnitLayerTextures();
    //
    CString szResourcesPath = ((CWorld *)GetWorld())->GetResourcePath();
    BOOL bCheck = AddGraphicsResources("textures.res");
    bCheck = AddGraphicsResources("redress.res") && bCheck;
    bCheck = AddNewResource(szResourcesPath + "textures.res") && bCheck;
    bCheck = AddNewResource(szResourcesPath + "redress.res") && bCheck;
    bCheck = AddNewResource(szResourcesPath + "menus.res") && bCheck;
    return bCheck;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTextureManager::Destroy()
{
    pShadowBufferTexture = 0;
    //
    LPTEXTURE lpTexture = 0;
    POSITION pos = lru.GetHeadPosition();
    // destroy LRU map
    while (pos)
    {
        lpTexture = lru.GetNext(pos);
        lpTexture->Destroy();
        delete lpTexture;
    }
    lru.RemoveAll();
    nTotalLRUSize = 0;
    //
    DeleteUnitLayerTextures();
    pRend = 0;
    //
    CResourceManager::Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
void CTextureManager::DeleteUnitLayerTextures()
{
    CFileFind find;
    CString szMaskName = CString(((CWorld *)GetWorld())->GetWorkPath()) + "textures\\*.mmp";
    BOOL bCheck = find.FindFile(szMaskName);
    //
    while (bCheck)
    {
        bCheck = find.FindNextFile();
        if (DeleteFile(find.GetFilePath()) == 0)
            ReportWarning("Can't delete file \"%s\" due to %d.\n", find.GetFileName(),
                          GetLastError());
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE CTextureManager::CreateInstant( ETypeIDs tidTypeID )
// create data instant with desired type ID
LPGAMEBASE CTextureManager::CreateInstant(ETypeIDs tidTypeID)
{
    LPTEXTURE pTexture = dbgnew CTexture();
    if (pTexture == NULL)
        return NULL;
    pTexture->SetTypeID(tidTypeID);

    return pTexture;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE CTextureManager::CreateData( ETypeIDs tidTypeID, int id )
// create data via CResourceManager and set type flag to it
LPGAMEBASE CTextureManager::CreateData(ETypeIDs tidTypeID, int id)
{
    LPGAMEBASE lpData = CResourceManager::CreateData(tidTypeID, id);
    // set type flag
    switch (tidTypeID)
    {
    case TCTID_STATIC_TEXTURE:
        lpData->SetFlag(TEX_STATIC);
        break;
    case TCTID_PAINT_TEXTURE:
        lpData->SetFlag(TEX_PAINT);
        break;
    case TCTID_DYNAMIC_TEXTURE:
        lpData->SetFlag(TEX_DYNAMIC);
        break;
    case TCTID_SHADOW_TEXTURE:
        lpData->SetFlag(TEX_SHADOW);
        break;
    default:
        lpData->SetFlag(TEX_STATIC);
    }

    return lpData;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTextureManager::Release(LPGAMEBASE lpData)
{
    if (!lpData || lpData->IsReferenced())
        return;

    LPGAMEBASE pValue = NULL;
    int key = lpData->GetID();

    if (hash.Lookup(key, pValue))
    {
        pValue = RemoveInstantAt(pValue->GetID());
        if (!lpData->CheckFlag(TEX_LOADED | TEX_INSYSTEM | TEX_INVIDEO))
        {
            lpData->Destroy();
            lpData->SetManager(NULL);
            MoveToTrash(lpData);

            return;
        }
        // place it in the LRU list
        lru.AddTail((LPTEXTURE)lpData);
        nTotalLRUSize += ((LPTEXTURE)lpData)->GetTotalSize();
    }
    else // this is an texture from the LRU or 'missed' one
    {
        lpData->Destroy();
        lpData->SetManager(NULL);
        MoveToTrash(lpData);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// get texture from LRU list by name and type
LPTEXTURE CTextureManager::GetFromLRU(ETypeIDs tidTypeID, LPCSTR pszName)
{
    POSITION pos = lru.GetHeadPosition();
    POSITION lastpos = pos;
    LPTEXTURE lpTexture = NULL;

    while (pos)
    {
        lpTexture = lru.GetNext(pos);
        if ((lpTexture->GetTypeID() == tidTypeID) && (strcmp(lpTexture->GetName(), pszName) == 0))
        {
            TRACE1("Get texture \"%s\" from LRU map.\n", lpTexture->GetName());
            lru.RemoveAt(lastpos); // remove from LRU list
            nTotalLRUSize -= lpTexture->GetTotalSize();
            RegisterInstant(lpTexture); // register in main hash table
            return lpTexture;
        }
        lastpos = pos;
    }

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPBYTE CTextureManager::GetMMPData( LPCSTR pszMMPName )
// get mip-map data from mapped resource
LPBYTE CTextureManager::GetMMPData(LPCSTR pszMMPName)
{
    return GetFile(pszMMPName);
}
/////////////////////////////////////////////////////////////////////////////////////
// LPTEXTURE CTextureManager::CreateTexture( ETypeIDs tid, LPCSTR pszName )
// create texture load data from file
LPTEXTURE CTextureManager::CreateTexture(ETypeIDs tid, LPCSTR pszName)
{
    LPTEXTURE pTexture = FindTexture(tid, pszName);
    if (!pTexture)
        pTexture = (LPTEXTURE)CreateData(tid);
    pTexture->SetName(pszName);

    return pTexture;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPTEXTURE CTextureManager::CreateTexture( ETypeIDs tidTypeID )
// create texture by typeID
LPTEXTURE CTextureManager::CreateTexture(ETypeIDs tidTypeID)
{
    return (LPTEXTURE)CreateData(tidTypeID);
}
DWORD textureTypeToMMPFormat(ETypeIDs eTextureTypeID)
{
    switch (eTextureTypeID)
    {
    case TFTID_565:
        return MMP_FORMAT_5650;
    case TFTID_5551:
        return MMP_FORMAT_5551;
    case TFTID_4444:
        return MMP_FORMAT_4444;
    case TFTID_8888:
        return MMP_FORMAT_8888;
    default:
        ReportError("Can't find texture format %d.\n", eTextureTypeID);
        return -1;
    }
}

/////////////////////////////////////////////////////////////////////////////////////
LPTEXTURE CTextureManager::CreateInterfaceTexture(DWORD dwSizeX, DWORD dwSizeY,
                                                  ETypeIDs eTextureTypeID)
{
    LPTEXTURE lpTexture = (LPTEXTURE)CreateData(eTextureTypeID);
    SMMPFileHeader hdr;
    memset(&hdr, 0, sizeof(SMMPFileHeader));
    lpTexture->SetSize(dwSizeX, dwSizeY);

    DWORD mmpFormat = textureTypeToMMPFormat(eTextureTypeID);
    if (mmpFormat == 0)
        return nullptr;
    DXGI_FORMAT texFormat;
    if (pRend->FindTextureFormat(mmpFormat, texFormat))
        lpTexture->SetTextureFormat(texFormat);
    hdr.dwFormat = mmpFormat;
    hdr.InitializeDefaultFormat(mmpFormat);
    hdr.dwSignature = MMP_SIGNATURE;
    hdr.dwDimentionX = dwSizeX;
    hdr.dwDimentionY = dwSizeY;
    hdr.dwMipMapLevel = 1;
    // create surfaces
    if (lpTexture->Create(pRend, &hdr) == FALSE)
    {
        lpTexture->Release();
        return 0;
    }

    return lpTexture;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPTEXTURE CTextureManager::CreateShadowTexture()
// create 5551 format texture with 64x64 dimentions for rendering shadows
LPTEXTURE CTextureManager::CreateShadowTexture()
{
    LPTEXTURE lpTexture = (LPTEXTURE)CreateData(TFTID_ALPHA4);
    DXGI_FORMAT texFormat;
    SMMPFileHeader hdr;
    //
    lpTexture->SetSize(64, 64);
    if (pRend->FindTextureFormat(MMP_FORMAT_4444, texFormat))
        lpTexture->SetTextureFormat(texFormat);
    else
        return reinterpret_cast<LPTEXTURE>(
            ReportError("Can't find texture format %d.\n", TFTID_4444));
    // fill mmp header to create surfaces
    hdr.InitializeDefaultFormat4444();
    hdr.dwSignature = MMP_SIGNATURE;
    hdr.dwDimentionX = 64;
    hdr.dwDimentionY = 64;
    hdr.dwMipMapLevel = 1;
    //
    lpTexture->ResetFlag(TEX_STATIC | TEX_PAINT | TEX_DYNAMIC);
    lpTexture->SetFlag(TEX_SHADOW);
    // create surfaces
    if (lpTexture->Create(pRend, &hdr) == FALSE)
    {
        lpTexture->Release();
        return 0;
    }
    //
    return lpTexture;
}
/////////////////////////////////////////////////////////////////////////////////////
LPTEXTURE CTextureManager::GetShadowBuffer()
{
    if (pShadowBufferTexture)
        return pShadowBufferTexture;
    //
    LPTEXTURE lpTexture = (LPTEXTURE)CreateData(TFTID_ALPHA4);
    DXGI_FORMAT texFormat;
    SMMPFileHeader hdr;
    //
    lpTexture->SetSize(64, 64);
    if (pRend->FindTextureFormat(MMP_FORMAT_4444, texFormat))
        lpTexture->SetTextureFormat(texFormat);
    else
        return reinterpret_cast<LPTEXTURE>(
            ReportError("Can't find texture format %d.\n", TFTID_4444));
    // fill mmp header to create surfaces
    hdr.InitializeDefaultFormat4444();
    hdr.dwSignature = MMP_SIGNATURE;
    hdr.dwDimentionX = 64;
    hdr.dwDimentionY = 64;
    hdr.dwMipMapLevel = 1;
    //
    lpTexture->ResetFlag(TEX_STATIC | TEX_PAINT | TEX_DYNAMIC | TEX_SHADOW);
    lpTexture->SetFlag(TEX_SYSMEM);
    // create surfaces
    if (lpTexture->Create(pRend, &hdr) == FALSE)
    {
        lpTexture->Release();
        return 0;
    }
    //
    pShadowBufferTexture = lpTexture;
    //
    return pShadowBufferTexture;
}
/////////////////////////////////////////////////////////////////////////////////////
// load texture in accordance with it's file name
BOOL CTextureManager::LoadTexture(LPTEXTURE lpTexture)
{
    if (lpTexture == 0)
    {
        ReportWarning("CTextureManager::LoadTexture.\n Can't load texture to NULL pointer.\n");
        return FALSE;
    }
    if (lpTexture->CheckFlag(TEX_LOADED))
    {
        ReportWarning("Load already loaded texture \"%s\".\n", lpTexture->GetName());
        return TRUE;
    }

    ETypeIDs tidTextureFormat = TID_EMPTY;
    BOOL bCheck = FALSE;
    DXGI_FORMAT texFormat;
    // open file
    CFileFind find;
    CString szString = theSettings.szWorkPath + "textures\\" + lpTexture->GetName() + ".bmp";
    DWORD dwTextureFormat;
    if (find.FindFile(szString)) // first, trying to open .bmp file
    {
        switch (lpTexture->GetTypeID())
        {
        case TTID_WATER_MAIN:
        case TTID_TERRAIN:
            dwTextureFormat = MMP_FORMAT_5650;
            break;
        case TTID_UNIT_MAIN:
        case TTID_UNIT_WEAPON:
        case TTID_HOUSE_MAIN:
        case TTID_OBJECT_MAIN:
        case TTID_TREE_MAIN:
            dwTextureFormat = MMP_FORMAT_5551;
            break;
        case TTID_SPELL_MAIN:
        case TTID_ANIMATED1:
            dwTextureFormat = MMP_FORMAT_4444;
            break;
        default:
            dwTextureFormat = MMP_FORMAT_5650;
        }
        if (pRend->FindTextureFormat(dwTextureFormat, texFormat))
            lpTexture->SetTextureFormat(texFormat);
        else
            return ReportError("CTextureManager::LoadTexture.\n Can't find desired texture format "
                               "%d for texture \"%s\".\n",
                               tidTextureFormat, lpTexture->GetName());
        //
        return lpTexture->Load(pRend, szString);
    }
    else
    {
        szString = CString(lpTexture->GetName()) + ".mmp";
        LPMMPFILEHEADER lpHdr = reinterpret_cast<SMMPFileHeader *>(GetFile(szString));
        if (lpHdr)
        {
            if (pRend->FindBestTextureFormat(lpHdr, texFormat))
                lpTexture->SetTextureFormat(texFormat);
            else
            {
                if (lpHdr->dwFormat == MMP_FORMAT_8888 &&
                    pRend->FindTextureFormat(MMP_FORMAT_4444, texFormat))
                    lpTexture->SetTextureFormat(texFormat);
                else
                    return ReportError("CTextureManager::LoadTexture.\n Can't find texture format "
                                       "for texture \"%s\".\n",
                                       lpTexture->GetName());
            }
            BOOL bRetVal;
            if (wStartMipMapLevel >= lpHdr->dwMipMapLevel)
                bRetVal = lpTexture->Load(pRend, lpHdr, lpHdr->dwMipMapLevel - 1);
            else
                bRetVal = lpTexture->Load(pRend, lpHdr, wStartMipMapLevel);
            CloseFile((BYTE *)lpHdr);
            return bRetVal;
        }
        else
        {
            CFile file;
            szString.Format("%stextures\\%s.mmp", ((CWorld *)GetWorld())->GetWorkPath(),
                            lpTexture->GetName());
            if (file.Open(szString, CFile::modeRead) == FALSE)
            {
                ReportWarning("CTextureManager::LoadTexture.\n Can't find texture \"%s\"\n",
                              lpTexture->GetName());
                return TRUE;
            }
            LPBYTE lpData = dbgnew BYTE[file.GetLength()];
            file.Read(lpData, file.GetLength());
            file.Close();

            if (pRend->FindBestTextureFormat(reinterpret_cast<SMMPFileHeader *>(lpData), texFormat))
                lpTexture->SetTextureFormat(texFormat);
            else
                return ReportError("CTextureManager::LoadTexture.\n Can't find texture format for "
                                   "texture \"%s\".\n",
                                   lpTexture->GetName());
            lpTexture->Load(pRend, reinterpret_cast<SMMPFileHeader *>(lpData), wStartMipMapLevel);

            delete[] lpData;

            return TRUE;
        }
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// load texture from mmp file in accordance with it's file name and full directory path
BOOL CTextureManager::LoadTexture(LPTEXTURE lpTexture, LPCSTR pszDirectoryName)
{
    if (lpTexture == 0)
    {
        ReportWarning("CTextureManager::LoadTexture.\n Can't load texture to NULL pointer.\n");
        return FALSE;
    }
    if (lpTexture->CheckFlag(TEX_LOADED))
    {
        ReportWarning("Load already loaded texture \"%s\".\n", lpTexture->GetName());
        return TRUE;
    }

    CString szString;
    CFile file;

    szString.Format("%s%s.mmp", pszDirectoryName, lpTexture->GetName());
    if (file.Open(szString, CFile::modeRead) == FALSE)
    {
        ReportWarning(
            "CTextureManager::LoadTexture.\n Can't find texture \"%s\" in directory \"%s\"\n",
            lpTexture->GetName(), pszDirectoryName);
        return TRUE;
    }

    LPBYTE lpData = dbgnew BYTE[file.GetLength()];
    file.Read(lpData, file.GetLength());
    file.Close();

    DXGI_FORMAT texFormat;
    if (pRend->FindBestTextureFormat(reinterpret_cast<SMMPFileHeader *>(lpData), texFormat))
        lpTexture->SetTextureFormat(texFormat);
    else
        return ReportError(
            "CTextureManager::LoadTexture.\n Can't find texture format for texture \"%s\".\n",
            lpTexture->GetName());
    lpTexture->Load(pRend, reinterpret_cast<SMMPFileHeader *>(lpData), wStartMipMapLevel);

    delete[] lpData;
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CTextureManager::GetUserData( LPCSTR pszFileName, CBYTEArray& data )
// get user data from MMP file
BOOL CTextureManager::GetUserData(LPCSTR pszFileName, CBYTEArray &data)
{
    SMMPFileHeader *pHdr = NULL;
    SMMPFileHeader hdr;
    CString szString;
    CFile file;
    // first, try to load from file
    szString.Format("%stextures\\%s.mmp", GetWorld()->GetWorkPath(), pszFileName);
    if (file.Open(szString, CFile::modeRead) != FALSE)
    {
        file.Read(&hdr, sizeof(hdr));
        data.SetSize(hdr.dwUserDataShift); // ARRAY BOUNDS CHECK
        if (hdr.dwUserDataShift != 0)
            file.Read(data.GetData(), hdr.dwUserDataShift);
        file.Close();

        return TRUE;
    }
    // then, try to load from resource
    szString.Format("%s%s", pszFileName, ".mmp");
    pHdr = (SMMPFileHeader *)GetFile(szString);
    if (pHdr)
    {
        data.SetSize(pHdr->dwUserDataShift); // ARRAY BOUNDS CHECK
        if (pHdr->dwUserDataShift)
            memcpy(data.GetData(), pHdr->GetUsedData(), pHdr->dwUserDataShift);
        CloseFile((BYTE *)pHdr);

        return TRUE;
    }
    // requested data not found!
    data.SetSize(0); // ARRAY BOUNDS CHECK
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CTextureManager::LoadTextures()
// load all textures in accordance with it's file name
BOOL CTextureManager::LoadTextures()
{
    ETypeIDs tidTextureFormat = TID_EMPTY;
    BOOL bCheck = FALSE;

#ifdef _DEBUG
    if (!pRend)
        return ReportError("CTextureManager::LoadTextures.\n No valid CRenderer interface.\n");
#endif // _DEBUG

    //
    CHashMapIterator<int, LPGAMEBASE> hmi;
    LPGAMEBASE lpData = hmi.GetFirst(&hash);
    while (lpData)
    {
        LoadTexture(static_cast<LPTEXTURE>(lpData));
        lpData = hmi.GetNext();
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CTextureManager::RestoreTextures()
// restore all textures via 'Restore' method call
BOOL CTextureManager::RestoreTextures()
{
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTextureManager::ReleaseTextures()
{
    CManagerIterator mni;
    LPGAMEBASE lpData = mni.GetFirst(this);
    //
    while (lpData)
    {
        ((LPTEXTURE)lpData)->Release();
        lpData = mni.GetNext();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// LPTEXTURE CTextureManager::FindTexture( ETypeIDs tidTextureTypeID, LPCSTR pszName )
// search texture by typeID and name
LPTEXTURE CTextureManager::FindTexture(ETypeIDs tidTextureTypeID, LPCSTR pszName)
{
    CManagerIterator mni;
    LPGAMEBASE lpData = mni.GetFirst(this);
    // first, try to find in existed textures
    while (lpData)
    {
        if ((lpData->GetTypeID() == tidTextureTypeID) && (strcmp(lpData->GetName(), pszName) == 0))
            return (LPTEXTURE)lpData;
        lpData = mni.GetNext();
    }
    // then, in LRU map
    return GetFromLRU(tidTextureTypeID, pszName);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTextureManager::RemoveLRUTextures( DWORD dwCurrentSegmentCount )
// removes LPU textures from video memory
void CTextureManager::RemoveLRUTextures(DWORD dwCurrentSegmentCount)
{
    POSITION pos = lru.GetHeadPosition();
    POSITION lastpos = pos;
    LPTEXTURE lpTexture = NULL;
    //
    while (pos)
    {
        lpTexture = lru.GetNext(pos);
        if (lpTexture->GetLRUSegment() - dwCurrentSegmentCount > 15)
        {
            delete lpTexture;
            lru.RemoveAt(lastpos);
        }
        lastpos = pos;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTextureManager::RemoveAllLRUTextures()
// remove all textures from LRU, regardless to their segment count
void CTextureManager::RemoveAllLRUTextures()
{
    POSITION pos = lru.GetHeadPosition();
    while (pos)
    {
        CTexture *pTexture = lru.GetNext(pos);
        delete pTexture; // physically destroy data
    }
    nTotalLRUSize = 0;
    lru.RemoveAll();
    /*
            if ( pVis )
                    pRend->EvictManagedTextures();
    */
}
/////////////////////////////////////////////////////////////////////////////////////
// LPTEXTURE CTextureManager::FindInLRU( ETypeIDs tidTypeID )
// find texture with such type in LRU map
// NOTE: don't remove it from LRU
LPTEXTURE CTextureManager::FindInLRU(ETypeIDs tidTypeID)
{
    POSITION pos = lru.GetHeadPosition();
    LPTEXTURE lpTexture = NULL;

    while (pos)
    {
        lpTexture = lru.GetNext(pos);
        if (lpTexture->GetTypeID() == tidTypeID)
            return lpTexture;
    }

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPTEXTURE CTextureManager::FindInLRU( ETypeIDs tidTypeID, LPCSTR pszName )
// find texture with such type and name in LRU map
// NOTE: don't remove it from LRU
LPTEXTURE CTextureManager::FindInLRU(ETypeIDs tidTypeID, LPCSTR pszName)
{
    POSITION pos = lru.GetHeadPosition();
    LPTEXTURE lpTexture = NULL;

    while (pos)
    {
        lpTexture = lru.GetNext(pos);
        if ((lpTexture->GetTypeID() == tidTypeID) && (strcmp(lpTexture->GetName(), pszName) == 0))
            return lpTexture;
    }

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
int CTextureManager::PutLayers(DWORD *pResultLayer, const CStringArray &szTextures,
                               DWORD dwTextureDimention)
{
    CGraphicsProcessor *pGraphicsProcessor = theRenderer.GetGraphicsProcessor();
    int i, nCount = 0;
    //
    for (i = 0; i < szTextures.GetSize(); ++i)
    {
        SMMPFileHeader *pMMPHeader = (SMMPFileHeader *)GetFile(szTextures[i]);
        if (pMMPHeader)
        {
            BYTE *pBaseData = (BYTE *)pMMPHeader;
            if ((pMMPHeader->dwDimentionX != dwTextureDimention) ||
                (pMMPHeader->dwDimentionY != dwTextureDimention))
            {
                CloseFile(pBaseData);
                return ReportError("Texture \"%s\" dimentions are incorrect.\n", (LPCSTR)szTextures[i]);
            }
            if (pMMPHeader->dwFormat == MMP_FORMAT_PNT3)
            {
                pGraphicsProcessor->PutOneLayer(pResultLayer,
                                                (DWORD *)pMMPHeader->GetMipLevelData(0),
                                                dwTextureDimention * dwTextureDimention, true);
            }
            else
            {
                pGraphicsProcessor->PutOneLayer(pResultLayer,
                                                (DWORD *)pMMPHeader->GetMipLevelData(0),
                                                dwTextureDimention * dwTextureDimention, false);
            }
            nCount++;
            CloseFile(pBaseData);
        }
        else
            ReportWarning("Can't find texture \"%s\" for layer composition.\n", szTextures[i]);
    }
    //
    return nCount;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTextureManager::ComposeTexture(CString &szResultName, const STextureSetDescription &set,
                                     const CString &szDesiredName, const CString &szAlternateName,
                                     DWORD dwNumMipLevels, DWORD dwBGColor, DWORD dwResultFormat)
{
    // Match the installed addon's HD names and atlas geometry in both precache and live assembly.
    if ((set.characterAtlas == 1 || set.characterAtlas == 2) &&
        ModRuntime::Current() && ModRuntime::Current()->CombatSetting("JMVGraphics"))
    {
        STextureSetDescription hd;
        hd.data.Copy(set.data);
        hd.characterAtlas = set.characterAtlas + 2;
        hd.dwTextureDimention = 1024;
        for (int slot = 0; slot < 4; ++slot) hd.equipmentSlots[slot] = set.equipmentSlots[slot];
        for (int layer = 0; layer < set.szTextures.GetSize(); ++layer)
        {
            CString name = set.szTextures[layer];
            if (name.GetLength() < 10 || name.Right(4).CompareNoCase(".mmp") != 0)
                return ReportError("Invalid character layer name: %s", (LPCSTR)name);
            name = name.Left(name.GetLength() - 4);
            if (name.Find("skin_") < 0) name = CString("unhuma") + name.Mid(6);
            name += set.characterAtlas == 1 ? ".x4.mmp" : ".x2.mmp";
            hd.szTextures.Add(name);
        }
        return ComposeTexture(szResultName, hd, szDesiredName, szAlternateName,
                              dwNumMipLevels, dwBGColor, MMP_FORMAT_8888);
    }

    CString szResultFileName(theWorld.GetWorkPath() + "textures\\" + szDesiredName + ".mmp");
    SMMPFileHeader hdr;
    CBYTEArray data;
    CFile file;
    // first, check for such texture already exist
    szResultName = szDesiredName;
    // check primary name texture
    if (file.Open(szResultFileName, CFile::modeRead))
    {
        // read user data
        file.Read(&hdr, sizeof(hdr));
        data.SetSize(hdr.dwUserDataShift); // ARRAY BOUNDS CHECK
        if (hdr.dwUserDataShift != 0)
            file.Read(data.GetData(), hdr.dwUserDataShift);
        file.Close();
        // compare user data
        if ((hdr.dwDimentionX == set.dwTextureDimention) &&
            (hdr.dwDimentionY == set.dwTextureDimention) &&
            (hdr.dwFormat == dwResultFormat) &&
            (data.GetSize() == set.data.GetSize()) &&
            (memcmp(data.GetData(), set.data.GetData(), data.GetSize()) == 0))
            return TRUE;
    }
    else if (szDesiredName != szAlternateName)
    {
        // check alternate name texture
        szResultName = szAlternateName;
        szResultFileName = theWorld.GetWorkPath() + "textures\\" + szAlternateName + ".mmp";
        if (file.Open(szResultFileName, CFile::modeRead))
        {
            // read user data
            file.Read(&hdr, sizeof(hdr));
            data.SetSize(hdr.dwUserDataShift); // ARRAY BOUNDS CHECK
            if (hdr.dwUserDataShift != 0)
                file.Read(data.GetData(), hdr.dwUserDataShift);
            file.Close();
            // compare user data
            if ((hdr.dwDimentionX == set.dwTextureDimention) &&
            (hdr.dwDimentionY == set.dwTextureDimention) &&
            (hdr.dwFormat == dwResultFormat) &&
            (data.GetSize() == set.data.GetSize()) &&
                (memcmp(data.GetData(), set.data.GetData(), data.GetSize()) == 0))
                return TRUE;
            szResultName = szDesiredName;
            szResultFileName = theWorld.GetWorkPath() + "textures\\" + szDesiredName + ".mmp";
        }
    }
    //
    if (set.szTextures.GetSize() == 0)
    {
        DeleteFile(szResultFileName);
        return FALSE;
    }
    // compose texture
    DWORD *pResultLayer = mm_malloc<DWORD>(set.dwTextureDimention * set.dwTextureDimention);
    MemSetDWord(pResultLayer, dwBGColor, set.dwTextureDimention * set.dwTextureDimention);
    int nCount = 0;
    if (set.characterAtlas == 4)
    {
        // Exact source quadrant layout: helm/shield above right/left weapon.
        std::vector<DWORD> layer(512 * 512);
        for (int slot = 0; slot < 4; ++slot)
        {
            int index = set.equipmentSlots[slot];
            if (index < 0 || index >= set.szTextures.GetSize()) continue;
            auto hdrLayer = reinterpret_cast<SMMPFileHeader *>(GetFile(set.szTextures[index]));
            if (!hdrLayer) { ReportWarning("Missing HD equipment layer %s", (LPCSTR)set.szTextures[index]); continue; }
            if (hdrLayer->dwDimentionX != 512 || hdrLayer->dwDimentionY != 512 ||
                (hdrLayer->dwFormat != MMP_FORMAT_PNT3 && hdrLayer->dwFormat != MMP_FORMAT_PAINT32))
            {
                CloseFile(reinterpret_cast<BYTE *>(hdrLayer));
                mm_free(pResultLayer);
                return ReportError("Invalid HD equipment layer %s", (LPCSTR)set.szTextures[index]);
            }
            std::fill(layer.begin(), layer.end(), 0);
            theRenderer.GetGraphicsProcessor()->PutOneLayer(layer.data(),
                reinterpret_cast<DWORD *>(hdrLayer->GetMipLevelData(0)), 512 * 512,
                hdrLayer->dwFormat == MMP_FORMAT_PNT3);
            CloseFile(reinterpret_cast<BYTE *>(hdrLayer));
            int x = (slot == 1 || slot == 3) ? 512 : 0;
            int y = slot < 2 ? 512 : 0;
            for (int row = 0; row < 512; ++row)
                memcpy(pResultLayer + (y + row) * 1024 + x, layer.data() + row * 512, 512 * sizeof(DWORD));
            ++nCount;
        }
    }
    else nCount = PutLayers(pResultLayer, set.szTextures, set.dwTextureDimention);
    // check for any textures exist on this layer
    if (!nCount)
    {
        mm_free(pResultLayer);
        return FALSE;
    }
    // generate mip levels and write result texture
    hdr.dwSignature = MMP_SIGNATURE;
    hdr.InitializeDefaultFormat(dwResultFormat);
    hdr.dwMipMapLevel = dwNumMipLevels;
    hdr.dwDimentionX = set.dwTextureDimention;
    hdr.dwDimentionY = set.dwTextureDimention;
    hdr.dwUserDataShift = set.data.GetSize();
    //
    ETextureFormats texFormat = ETextureFormats(-1);
    switch (dwResultFormat)
    {
    case MMP_FORMAT_5650:
        texFormat = TEX_FORMAT_565;
        break;
    case MMP_FORMAT_5551:
        texFormat = TEX_FORMAT_1555;
        break;
    case MMP_FORMAT_4444:
        texFormat = TEX_FORMAT_4444;
        break;
    }
    //
    if (file.Open(szResultFileName, CFile::modeWrite | CFile::modeCreate) == FALSE)
        return ReportError("Can't open file \"%s\" to write result texture.\n", szResultFileName);
    // write header and user data
    file.Write(&hdr, sizeof(hdr));
    if (hdr.dwUserDataShift)
        file.Write(set.data.GetData(), hdr.dwUserDataShift);
    //
    DWORD dwNewSizeX = hdr.dwDimentionX;
    DWORD dwNewSizeY = hdr.dwDimentionY;
    WORD *pResultMipLevel = mm_malloc<WORD>(dwNewSizeX * dwNewSizeY);
    DWORD *pMipLevel = mm_malloc<DWORD>(dwNewSizeX * dwNewSizeY);
    // 0th mip level
    CGraphicsProcessor *pGraphicsProcessor = theRenderer.GetGraphicsProcessor();
    if (dwResultFormat == MMP_FORMAT_8888)
        file.Write(pResultLayer, sizeof(DWORD) * dwNewSizeX * dwNewSizeY);
    else
    {
        pGraphicsProcessor->Convert32To16Bit(pResultMipLevel, pResultLayer, texFormat, dwNewSizeX * dwNewSizeY);
        file.Write(pResultMipLevel, sizeof(WORD) * dwNewSizeX * dwNewSizeY);
    }
    // generate 1st and 2nd mip levels
    for (int i = 1; i < dwNumMipLevels; ++i)
    {
        dwNewSizeX >>= 1;
        dwNewSizeY >>= 1;

        pGraphicsProcessor->GenerateMipmapLevel(pMipLevel, pResultLayer, dwNewSizeX);
        if (dwResultFormat == MMP_FORMAT_8888)
            file.Write(pMipLevel, sizeof(DWORD) * dwNewSizeX * dwNewSizeY);
        else
        {
            pGraphicsProcessor->Convert32To16Bit(pResultMipLevel, pMipLevel, texFormat, dwNewSizeX * dwNewSizeY);
            file.Write(pResultMipLevel, sizeof(WORD) * dwNewSizeX * dwNewSizeY);
        }

        DWORD *pTmp = pMipLevel;
        pMipLevel = pResultLayer;
        pResultLayer = pTmp;
    }
    //
    file.Close();
    //
    mm_free(pResultMipLevel);
    mm_free(pResultLayer);
    mm_free(pMipLevel);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTextureManager::ComposeLayer(const STextureSetDescription &set, const CString &szFileName,
                                   DWORD dwBGColor)
{
    CString szResultFileName(theWorld.GetWorkPath() + "textures\\" + szFileName + ".mmp");
    SMMPFileHeader hdr;
    CFile file;
    // first, check for such texture already exist
    if (file.Open(szResultFileName, CFile::modeRead))
    {
        CBYTEArray data;
        // read user data
        file.Read(&hdr, sizeof(hdr));
        data.SetSize(hdr.dwUserDataShift); // ARRAY BOUNDS CHECK
        if (hdr.dwUserDataShift != 0)
            file.Read(data.GetData(), hdr.dwUserDataShift);
        file.Close();
        // compare user data
        if ((data.GetSize() == set.data.GetSize()) &&
            (memcmp(data.GetData(), set.data.GetData(), data.GetSize()) == 0))
            return FALSE;
    }
    // compose texture
    DWORD *pResultLayer = mm_malloc<DWORD>(set.dwTextureDimention * set.dwTextureDimention);
    MemSetDWord(pResultLayer, dwBGColor, set.dwTextureDimention * set.dwTextureDimention);
    int nCount = PutLayers(pResultLayer, set.szTextures, set.dwTextureDimention);
    // check for any textures exist on this layer
    if (!nCount)
    {
        mm_free(pResultLayer);
        return FALSE;
    }
    // precalculate as layer w/o RLE encoding
    CGraphicsProcessor *pGraphicsProcessor = theRenderer.GetGraphicsProcessor();
    pGraphicsProcessor->PrecalculateAsLayer(pResultLayer,
                                            set.dwTextureDimention * set.dwTextureDimention);
    // compose header
    hdr.dwSignature = MMP_SIGNATURE;
    hdr.InitializeDefaultFormatPNT2();
    hdr.dwMipMapLevel = 1;
    hdr.dwDimentionX = set.dwTextureDimention;
    hdr.dwDimentionY = set.dwTextureDimention;
    // write result layer
    if (file.Open(szResultFileName, CFile::modeCreate | CFile::modeWrite) != FALSE)
    {
        hdr.dwUserDataShift = set.data.GetSize();
        file.Write(&hdr, sizeof(hdr));
        file.Write(set.data.GetData(), hdr.dwUserDataShift);
        file.Write(pResultLayer, set.dwTextureDimention * set.dwTextureDimention * sizeof(DWORD));
        file.Close();
        //
        mm_free(pResultLayer);
    }
    else
    {
        ReportError("Can't open file \"%s\" to write result layer texture.\n", szResultFileName);
        mm_free(pResultLayer);
        return FALSE;
    }
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
ITexture *CTextureManager::GetTexture(LPCSTR pszName)
{
    CManagerIterator mni;
    LPGAMEBASE pData = mni.GetFirst(this);
    // first, try to find in existed textures
    while (pData)
    {
        if (strcmp(pData->GetName(), pszName) == 0)
            return static_cast<ITexture *>(pData);
        pData = mni.GetNext();
    }
    // then, find in LRU
    return GetFromLRU(pszName);
}
/////////////////////////////////////////////////////////////////////////////////////
ITexture *CTextureManager::GetFromLRU(LPCSTR pszName)
{
    POSITION pos = lru.GetHeadPosition();
    POSITION lastpos = pos;
    ITexture *pTexture = NULL;

    while (pos)
    {
        pTexture = lru.GetNext(pos);
        if (strcmp(pTexture->GetName(), pszName) == 0)
        {
            TRACE1("Get texture \"%s\" from LRU map.\n", pTexture->GetName());
            lru.RemoveAt(lastpos);     // remove from LRU list
            RegisterInstant(pTexture); // register in main hash table
            return pTexture;
        }
        lastpos = pos;
    }

    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
ITexture *CTextureManager::GetAndLoadTexture(ETypeIDs tid, LPCSTR pszName)
{
    ITexture *pTexture = GetTexture(pszName);
    if (pTexture != 0)
        return pTexture;
    // create new texture and load data for it
    pTexture = CreateTexture(tid, pszName);
    LoadTexture(pTexture);
    return pTexture;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CTextureManager::ReloadAllTextures()
{
    bool bRetVal = true;
    // restore manin texture
    CManagerIterator it;
    CTexture *pTexture = (CTexture *)it.GetFirst(this);
    while (pTexture)
    {
        pTexture->ResetFlag(TEX_LOADED);
        bRetVal = LoadTexture(pTexture) && bRetVal;
        pTexture = (CTexture *)it.GetNext();
    }
    // restore textures in the LRU map
    POSITION pos = lru.GetHeadPosition();
    while (pos)
    {
        pTexture = lru.GetNext(pos);
        pTexture->ResetFlag(TEX_LOADED);
        bRetVal = LoadTexture(pTexture) && bRetVal;
    }
    //
    return bRetVal;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTextureManager::ShrinkLRU()
{
    POSITION pos = lru.GetHeadPosition();
    POSITION lastpos = pos;
    while ((nTotalLRUSize > nAllowedLRUSize) && (pos != 0))
    {
        CTexture *pTexture = lru.GetNext(pos);
        lru.RemoveAt(lastpos); // remove from LRU list
        nTotalLRUSize -= pTexture->GetTotalSize();
        delete pTexture; // physically destroy data
        lastpos = pos;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CTextureManager::UnloadAllTextures()
{
    return;
    CManagerIterator mi;
    CTexture *pTexture = static_cast<CTexture *>(mi.GetFirst(this));
    while (pTexture)
    {
        if (pTexture->CheckFlag(TEX_CANT_UNLOAD) == 0)
            pTexture->Unload();
        pTexture = static_cast<CTexture *>(mi.GetNext());
    }
}
/////////////////////////////////////////////////////////////////////////////////////
