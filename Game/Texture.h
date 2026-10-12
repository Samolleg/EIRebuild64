#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "GameBase.h"
#include "mmpFormat.h"
#include "SmartPtr.h"
#include "dx11/Surface.h"

#include <d3d11.h>
#include <wrl/client.h>
/////////////////////////////////////////////////////////////////////////////////////
class CTexture;
typedef CTexture *LPTEXTURE;
//
class CGraphicsProcessor;
class CResourceManager;
class CRenderer;
class CDXGISurface;

/////////////////////////////////////////////////////////////////////////////////////
const DWORD TEX_LOADED = MAKE_FLAG(CGB_LAST_BIT, 0);   // texture already loaded in system memory
const DWORD TEX_INSYSTEM = MAKE_FLAG(CGB_LAST_BIT, 1); // texture already in system memory (loaded)
const DWORD TEX_INVIDEO = MAKE_FLAG(CGB_LAST_BIT,
                                    2); // texture already in video (local/non-local) memory
const DWORD TEX_STATIC = MAKE_FLAG(CGB_LAST_BIT,
                                   3); // static texture (just load and don't do anythig with it)
const DWORD TEX_PAINT = MAKE_FLAG(
    CGB_LAST_BIT, 4); // texture for assemble personal textures (armor, skin, wounds, etc.)
const DWORD TEX_DYNAMIC = MAKE_FLAG(CGB_LAST_BIT,
                                    5); // procedural texture. for SW shadows, AVI-textures, etc.
const DWORD TEX_SHADOW = MAKE_FLAG(CGB_LAST_BIT, 6); // shadow-rendering texture.
const DWORD TEX_SYSMEM = MAKE_FLAG(CGB_LAST_BIT, 7); // non-managed texture in system memory
const DWORD TEX_RVIDEO = MAKE_FLAG(CGB_LAST_BIT, 8); // render surface equal to video
const DWORD TEX_RSYSTEM = MAKE_FLAG(
    CGB_LAST_BIT,
    9); // render surface equal to system. requires Load operation after unlock render.
const DWORD TEX_SHADOWCOPY = MAKE_FLAG(CGB_LAST_BIT, 10); // this texture need shadow memory copy !

const DWORD TEX_ALPHA0 = MAKE_FLAG(CGB_LAST_BIT, 11);
const DWORD TEX_ALPHA1 = MAKE_FLAG(CGB_LAST_BIT, 12);
const DWORD TEX_ALPHA4 = MAKE_FLAG(CGB_LAST_BIT, 13);
const DWORD TEX_ALPHA8 = MAKE_FLAG(CGB_LAST_BIT, 14);

const DWORD TEX_CANT_UNLOAD = MAKE_FLAG(CGB_LAST_BIT, 15);

const DWORD TEX_STAGING = MAKE_LAST_BIT(CGB_LAST_BIT, 16);
/////////////////////////////////////////////////////////////////////////////////////
class CTexture : public CGameBase
{
private:
    CString szName;         // texture name
    DWORD dwSizeX, dwSizeY; // texture sizes x and y
    int nTotalSize;         // texture's estimated size (in bytes)
    CBYTEArray userdata;


    DXGI_FORMAT m_texFormat;
    std::unique_ptr<CDXGISurface> pSurface; // surface
private:
    void SetupTextureFlags(D3D11_TEXTURE2D_DESC *tex2d);
    // surface filling
    static int CopyToSurface(CDXGISurface *pDXDDS, SMMPFileHeader *pHdr, DWORD dwFromLevel);
    int FillSurface(CDXGISurface *pDXDDS, SMMPFileHeader *pHdr, DWORD dwFromLevel);
    BOOL FillSurface(CRenderer *pRend, LPCSTR pszMain, LPCSTR pszAlpha);
public:
    // LRU segment counter
    DWORD dwLRUSegment; // segment number, then this texture was used last time
public:
    CTexture();
    virtual ~CTexture();
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    BOOL Create(CRenderer *pDD, SMMPFileHeader *pHdr);
    // pixel format functions
    void SetTextureFormat(const DXGI_FORMAT &ddpf) { m_texFormat = ddpf; }
    DXGI_FORMAT GetPixelFormat() { return m_texFormat; }
    // texture procedural modification support
    // 'size' functions
    DWORD GetSizeX() const { return dwSizeX; }
    DWORD GetSizeY() const { return dwSizeY; }
    void SetTotalSize(int newTotalSize) { nTotalSize = newTotalSize; }
    int GetTotalSize() const { return nTotalSize; }
    void SetSize(DWORD dwNewSizeX, DWORD dwNewSizeY)
    {
        dwSizeX = dwNewSizeX;
        dwSizeY = dwNewSizeY;
    }
    // 'get-private-data' functions

    CDXGISurface *GetSurface() const { return pSurface.get(); }
    BOOL Load(CRenderer *pRend, LPCSTR pszBitmapName);
    BOOL Load(CRenderer *pRend, SMMPFileHeader *pHdr, DWORD dwFromLevel);
    BOOL Load(CRenderer *pRend, CResourceManager *pManager, LPCSTR pszName, DWORD dwFromLevel);
    void Unload();
    // LRU functions
    void SetLRUSegment(DWORD dwNewSegment) { dwLRUSegment = dwNewSegment; }
    DWORD GetLRUSegment() const { return dwLRUSegment; }
    //
    bool IsUserDataEqual(const CBYTEArray &userdata1) const;
    // name functions
    void SetName(LPCSTR pszName) { szName = pszName; }
    virtual LPCSTR GetName() const { return szName; }
    // identification functions
    virtual LPCSTR GetGameClassName() const { return "CTexture"; }
    virtual EClassIDs WhoAmI() const { return HPC_TEXTURE; }
};
typedef CTexture ITexture;
typedef CSmartPtr<ITexture> ITexturePtr;
/////////////////////////////////////////////////////////////////////////////////////
