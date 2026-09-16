#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "ResourceManager.h"
#include "Texture.h"
/////////////////////////////////////////////////////////////////////////////////////
struct STextureSetDescription
{
    // user data
    CBYTEArray data;
    // texture names
    CStringArray szTextures;
    // resulting texture dimention
    DWORD dwTextureDimention;
};
/////////////////////////////////////////////////////////////////////////////////////
// class CTextureManager : public CResourceManager
// data manager, which manages all textures in game
// currently we have a 4 general types of textures:
// 		1. STATIC texture: load only once, then use it w/o any changes
// 		2. PAINT texture: compose from pre-processed pictures. Can be modified (but not too
// frequently)
// 		3. DYNAMIC texture: procedural texture, which can be modified each frame
// 		4. SHADOW texture: shadow rendering texture. can be modified several hundred times
// per frame
class CTextureManager : public CResourceManager
{
private:
    CList<LPTEXTURE, LPTEXTURE> lru; // LRU list
    int nTotalLRUSize;               // total size of the LRU textures (in bytes)
    int nAllowedLRUSize;             // total allowed size of LRU textures
    CSmartPtr<CTexture> pShadowBufferTexture;
    //
    CRenderer *pRend; // CRenderer obejct pointer
    //
    WORD wStartMipMapLevel; // recommended start mip-map level
private:
    virtual BOOL Create();
    virtual LPGAMEBASE CreateInstant(ETypeIDs tidTypeID);
    //
    int PutLayers(DWORD *pResultLayer, const CStringArray &szTextures, DWORD dwTextureDimention);

public:
    CTextureManager();
    virtual ~CTextureManager();
    // create/destroy
    virtual void Destroy();
    void DeleteUnitLayerTextures();
    // set interface function...
    void SetInterface(CRenderer *pNewRend);
    //
    BOOL LoadTexture(LPTEXTURE lpTexture);
    BOOL LoadTexture(LPTEXTURE lpTexture, LPCSTR pszDirectoryName);
    BOOL LoadTextures();
    LPBYTE GetMMPData(LPCSTR pszMMPName);
    BOOL RestoreTextures();
    void ReleaseTextures();
    // texture creating/releasing
    LPTEXTURE CreateTexture(ETypeIDs tid, LPCSTR pszName); // create and load texture by type/name
    LPTEXTURE CreateTexture(ETypeIDs tidTypeID);           // create texture with specified type
    LPTEXTURE CreateTexture(DDSURFACEDESC2 &ddsd);
    LPTEXTURE CreateInterfaceTexture(DWORD dwSizeX, DWORD dwSizeY,
                                     ETypeIDs eTextureTypeID = TFTID_5551);
    LPTEXTURE CreateShadowRenderingTexture();
    LPTEXTURE CreateShadowTexture();
    LPTEXTURE GetShadowBuffer();
    virtual LPGAMEBASE CreateData(ETypeIDs tidTypeID, int id = -1);
    virtual void Release(LPGAMEBASE lpData);
    //
    BOOL ComposeTexture(CString &szResultName, const STextureSetDescription &set,
                        const CString &szDesiredName, const CString &szAlternateName,
                        DWORD dwNumMipLevels, DWORD dwBGColor = 0,
                        DWORD dwResultFormat = MMP_FORMAT_5551);
    BOOL ComposeLayer(const STextureSetDescription &set, const CString &szResultFileName,
                      DWORD dwBGColor = 0);
    //
    void RemoveLRUTextures(DWORD dwCurrentSegmentCount); // removes LPU textures from video memory
    void ShrinkLRU();
    void RemoveAllLRUTextures();
    int GetNumLRUElements() const { return lru.GetCount(); }
    //
    void UnloadAllTextures();
    // searching existed textures ( in main hash map and in LRU list )
    LPTEXTURE FindTexture(ETypeIDs tidTypeID, LPCSTR pszName);
    LPTEXTURE GetFromLRU(ETypeIDs tidTypeID, LPCSTR pszName);
    LPTEXTURE FindInLRU(ETypeIDs tidTypeID);
    LPTEXTURE FindInLRU(ETypeIDs tidTypeID, LPCSTR pszName);
    ITexture *GetTexture(LPCSTR pszName);
    ITexture *GetFromLRU(LPCSTR pszName);
    //
    ITexture *GetAndLoadTexture(ETypeIDs tid, LPCSTR pszName);
    //
    bool ReloadAllTextures();
    //
    BOOL GetUserData(LPCSTR pszFileName, CBYTEArray &data);
    // resource description string
    virtual LPCSTR GetResourceDescription() const { return "textures"; };
    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CTextureManager"; };
    virtual EClassIDs WhoAmI() const { return HPC_TEXTUREMANAGER; }
};
/////////////////////////////////////////////////////////////////////////////////////
typedef CTextureManager *LPTEXTUREMANAGER;
