#include "StdAfx.h"
#include "dx11/RendererDX11.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "TextureManager.h"
#include <set>
#include <array>

#include "StringProcessing.h"
#include "GraphicsProcessor.h"
#include "ResourceManager.h"
#include "mmpFormat.h"
#include "Texture.h"

#include "Renderer.h"
#include "GeneralSettings.h"
#include "dx11/RendererDX11.h"
#include "dx11/Utils.h"
#include "dx11/Surface.h"
#include "dx11/MmpRgbToBgra.h"

void CTexture::SetupTextureFlags(D3D11_TEXTURE2D_DESC *tex2d)
{
    // Graphical debugging (ALT+F5) seems to be broken by:
    // Non-standard texture creation flags, in particular D3D11_CPU_ACCESS_WRITE / READ
    // DXGI-compatible swapchain flags?
    // Maybe DXGI surfaces? Actually this commit has some and they seem to work fine.
    /*if (CheckFlag(TEX_STAGING))
    {
        tex2d.Usage = D3D11_USAGE::D3D11_USAGE_STAGING;
        tex2d.BindFlags = 0;
        tex2d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    }*/
    // tex2d.CPUAccessFlags = D3D11_CPU_ACCESS_FLAG::D3D11_CPU_ACCESS_READ |
    //                       D3D11_CPU_ACCESS_FLAG::D3D11_CPU_ACCESS_WRITE;
    // if (CheckFlag(TEX_DYNAMIC) || CheckFlag(TEX_PAINT)) // DYNAMIC texture: procedural texture
    //{
    //    tex2d.Usage = D3D11_USAGE::D3D11_USAGE_DYNAMIC;
    //    tex2d.CPUAccessFlags = D3D11_CPU_ACCESS_FLAG::D3D11_CPU_ACCESS_WRITE;
    //}
    // tex2d.Usage = D3D11_USAGE::D3D11_USAGE_DYNAMIC;
    // create surface

    // tex2d.Usage = D3D11_USAGE::D3D11_USAGE_DEFAULT;
    /*tex2d.CPUAccessFlags = D3D11_CPU_ACCESS_FLAG::D3D11_CPU_ACCESS_READ |
                           D3D11_CPU_ACCESS_FLAG::D3D11_CPU_ACCESS_WRITE;*/
    // if (CheckFlag(TEX_DYNAMIC) || CheckFlag(TEX_PAINT)) // DYNAMIC texture: procedural texture
    //{
    //    tex2d.Usage = D3D11_USAGE::D3D11_USAGE_DYNAMIC;
    //    tex2d.CPUAccessFlags = D3D11_CPU_ACCESS_FLAG::D3D11_CPU_ACCESS_WRITE;
    //}

    // set hints about texture opaque/static/dynamic
    // if (CheckFlag(TEX_STATIC)) // STATIC texture: for terrain, trees, buildings, etc.
    // non-changable
    //    ddsd.ddsCaps.dwCaps2 |= DDSCAPS2_OPAQUE;
    // else if (CheckFlag(TEX_PAINT)) // PAINT texture: for unit's personal textures with assembled
    //                               // armor layers, skin, wounds, etc.
    //    ddsd.ddsCaps.dwCaps2 |= DDSCAPS2_HINTSTATIC;
    // else if (CheckFlag(TEX_DYNAMIC)) // DYNAMIC texture: procedural texture
    //    ddsd.ddsCaps.dwCaps2 |= DDSCAPS2_HINTDYNAMIC;
    // else if (CheckFlag(TEX_SHADOW)) // SHADOW texture: procedural SW shadows
    //    ddsd.ddsCaps.dwCaps2 |= DDSCAPS2_HINTDYNAMIC;
    // create surface
}

/////////////////////////////////////////////////////////////////////////////////////
// BOOL CTexture::Create( CRenderer* pRend, SMMPFileHeader* pHdr )
// create surface from data in MMP file header
BOOL CTexture::Create(CRenderer *pRend, SMMPFileHeader *pHdr)
{
    D3D11_TEXTURE2D_DESC tex2d;
    pRend->GetD3D()->DefaultSurfaceDesc(&tex2d);
    tex2d.Width = pHdr->dwDimentionX;
    tex2d.Height = pHdr->dwDimentionY;
    tex2d.Format = m_texFormat;
    tex2d.MipLevels = pHdr->dwMipMapLevel;
    // SetupTextureFlags

    pSurface = std::make_unique<CDXGISurface>(pRend->GetD3D(), &tex2d);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// fill surface from MMP data
int CTexture::FillSurface(CDXGISurface *pDXDDS, SMMPFileHeader *pHdr, DWORD dwFromLevel)
{
    // used method
    // if MMP in DXT# format
    return CopyToSurface(pDXDDS, pHdr, dwFromLevel);

    //

    if ((pHdr->dwFormat & 0x00FFFFFF) == MMP_FORMAT_DXTN)
    {
        // Handle DXT
        // if ((ddpfFormat.dwFlags & DDPF_FOURCC) &&
        //    (pHdr->dwFormat ==
        //     ddpfFormat.dwFourCC)) // our best format matches DXT#, then simply copy data
        //    return CopyToSurface(pDXDDS, pHdr, dwFromLevel);
        if (true)
            return CopyToSurface(pDXDDS, pHdr, dwFromLevel);
        else
        {
            // Unpack DXT to RGBA because this is RGBA texture or something
            // get tftid from texture format
            // tidTextureID = TFTID_8888;
            ETypeIDs tidTextureID;
            EDXTDecodeTypes dtDecodeType;
            switch (pHdr->dwFormat)
            {
            case MMP_FORMAT_DXT1: {
                switch (tidTextureID)
                {
                case TFTID_565:
                    dtDecodeType = DXT1_TO_565;
                    break;
                case TFTID_555:
                    dtDecodeType = DXT1_TO_555;
                    break;
                case TFTID_5551:
                    dtDecodeType = DXT1_TO_1555;
                    break;
                }
            }
            break;
            case MMP_FORMAT_DXT2:
                if (tidTextureID == TFTID_4444)
                    dtDecodeType = DXT2_TO_4444;
                break;
            case MMP_FORMAT_DXT3:
                if (tidTextureID == TFTID_4444)
                    dtDecodeType = DXT3_TO_4444;
                break;
            }
            // then we have decode type, perform decoding
            // return UnpackToSurface(pDXDDS, pHdr, dwFromLevel, dtDecodeType);
            assert(0);
        }
    }
    assert(0);
    // handle RGBA, 16 / 8
    //  Exception for 8888 textures for cards that not supports them
    // if (pHdr->dwFormat == MMP_FORMAT_8888 && ddpfFormat.dwRGBBitCount == 32)
    //  return CopyToSurfaceDecoding8To4(pDXDDS, pHdr, dwFromLevel);
    //  RGBA MMP
    // return CopyToSurface(pDXDDS, pHdr, dwFromLevel);
}
/////////////////////////////////////////////////////////////////////////////////////
// fill surface from bitmap(s)
BOOL CTexture::FillSurface(CRenderer *pRend, LPCSTR pszMain, LPCSTR pszAlpha)
{
    if (CheckFlag(TEX_LOADED))
        return TRUE;
    ASSERT(0);
    SetFlag(TEX_LOADED);
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CTexture::Load( CRenderer* pRend, LPSTR pszBitmapName )
// load texture and convert it to desired format
BOOL CTexture::Load(CRenderer *pRend, LPCSTR pszBitmapName)
{
    CString szMain, szAlpha;
    CStringArray szStrings;
    //
    if (CheckFlag(TEX_LOADED) && (szName == pszBitmapName))
    {
        ReportWarning("CTexture::Load.\n Texture \"%s\" already loaded.\n", pszBitmapName);
        return TRUE;
    }
    //
    TRACE1("Loading texture \"%s\" from bitmap.\n", szName);
    // compose main and alpha bitmap names
    NStrProc::SplitString(pszBitmapName, szStrings, '\\');
    szMain = szStrings[szStrings.GetUpperBound()];
    szStrings.RemoveAt(szStrings.GetUpperBound());
    szAlpha = "a_" + szMain;
    szStrings.Add(szAlpha);
    NStrProc::ComposeString(szAlpha, szStrings, '\\');
    szStrings.RemoveAt(szStrings.GetUpperBound());
    szStrings.Add(szMain);
    NStrProc::ComposeString(szMain, szStrings, '\\');
    //
    return FillSurface(pRend, szMain, szAlpha);
}

std::set<int> formats;
/////////////////////////////////////////////////////////////////////////////////////
// complete texture loading with all data structures creating/destroying
// 1. create system memory surface
// 2. create video memory surface
// 3. load testure data from memory pointer to render surface (may unpack DXT#, if need)
// 4. perform transfer from render surface to video
BOOL CTexture::Load(CRenderer *pRend, SMMPFileHeader *pHdr, DWORD dwFromLevel)
{

    if (CheckFlag(TEX_LOADED))
        return TRUE;
    // ASSERT(ddpfFormat.dwRGBBitCount == 16);
    // formats.insert(pHdr->dwBitDepth);
    LPBYTE lpData = reinterpret_cast<LPBYTE>(pHdr->GetMipLevelData(dwFromLevel));
    DWORD dwNumMipLevels = pHdr->dwMipMapLevel - dwFromLevel;
    DWORD dwNewSizeX = pHdr->dwDimentionX >> dwFromLevel;
    DWORD dwNewSizeY = pHdr->dwDimentionY >> dwFromLevel;
    // adjust dimentions with 'need rescale 512' flag
    if (pHdr->dwFormat == MMP_FORMAT_PNT3)
        dwNumMipLevels = 1;
    // correct mip-map using
    if (theSettings.nMipmapping == 0)
        dwNumMipLevels = 1;
    dwSizeX = dwNewSizeX;
    dwSizeY = dwNewSizeY;
    D3D11_TEXTURE2D_DESC tex2d;
    pRend->GetD3D()->DefaultSurfaceDesc(&tex2d);
    tex2d.Width = dwSizeX;
    tex2d.Height = dwSizeY;
    tex2d.Format = m_texFormat;
    tex2d.MipLevels = dwNumMipLevels;
    // SetupTextureFlags
    pSurface = std::make_unique<CDXGISurface>(pRend->GetD3D(), &tex2d);

    std::string debugName;
    if (szName)
    {
        debugName = "tex2d_" + szName;
        SetDebugObjectNameEx(pSurface->GetTexture().Get(), debugName);
        debugName = "sres_" + szName;
        SetDebugObjectNameEx(pSurface->GetShaderResource().Get(), debugName);
    }
    else
    {
        DirectX::SetDebugObjectName(pSurface->GetTexture().Get(), "tex2d_nameless");
        DirectX::SetDebugObjectName(pSurface->GetShaderResource().Get(), "tex2d_nameless");
    }

    // filling surface
    nTotalSize = FillSurface(pSurface.get(), pHdr, dwFromLevel);
    // set flags about alpha format
    ResetFlag(TEX_ALPHA0 | TEX_ALPHA1 | TEX_ALPHA4 | TEX_ALPHA8);
    if (pHdr->dwABits == 0)
        SetFlag(TEX_ALPHA0);
    else if (pHdr->dwABits == 1)
        SetFlag(TEX_ALPHA1);
    else if (pHdr->dwABits == 4)
        SetFlag(TEX_ALPHA4);
    else if (pHdr->dwABits == 8)
        SetFlag(TEX_ALPHA8);
    // set flag, that we've already load this texture
    SetFlag(TEX_LOADED);

    userdata.SetSize(pHdr->dwUserDataShift);
    memcpy(userdata.GetData(), pHdr->GetUsedData(), pHdr->dwUserDataShift);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTexture::Load(CRenderer *pRend, CResourceManager *pManager, LPCSTR pszName, DWORD dwFromLevel)
{
    SMMPFileHeader *pHdr = reinterpret_cast<SMMPFileHeader *>(pManager->GetFile(pszName));
    BOOL bRetVal = Load(pRend, pHdr, dwFromLevel);
    pManager->CloseFile((BYTE *)pHdr);
    return bRetVal;
}
/////////////////////////////////////////////////////////////////////////////////////
int CTexture::CopyToSurface(CDXGISurface *pDXDDS, SMMPFileHeader *pHdr, DWORD dwFromLevel)
{
    LPBYTE lpData = reinterpret_cast<LPBYTE>(pHdr->GetMipLevelData(dwFromLevel));
    DWORD dwNewSizeX = pHdr->dwDimentionX >> dwFromLevel;
    DWORD dwNewSizeY = pHdr->dwDimentionY >> dwFromLevel;
    // fill mip surfaces
    int nTotalSize = 0;
    D3D11_TEXTURE2D_DESC destination = {};
    pDXDDS->GetSurfaceDesc(&destination);
    for (int i = int(dwFromLevel); i < int(pHdr->dwMipMapLevel) &&
         i - int(dwFromLevel) < int(destination.MipLevels); i++)
    {
        int sub_index = i - int(dwFromLevel);

        D3D11_SUBRESOURCE_DATA srd = { 0 };
        std::vector<DWORD> converted;
        const bool packedRgb = (pHdr->dwFormat != MMP_FORMAT_8888 || pHdr->dwABits == 0) &&
            (pHdr->dwBitDepth == 16 || (pHdr->dwBitDepth == 32 && pHdr->dwABits == 0)) &&
            (pHdr->dwFormat & 0x00FFFFFF) != MMP_FORMAT_DXTN;
        if (packedRgb) {
            if (!MmpRgbToBgra::Convert(*pHdr, lpData, size_t(dwNewSizeX) * dwNewSizeY, converted))
                return nTotalSize;
            srd.pSysMem = converted.data();
            srd.SysMemPitch = dwNewSizeX * sizeof(DWORD);
        } else {
            srd.pSysMem = lpData;
            srd.SysMemPitch = pHdr->dwBitDepth * dwNewSizeX / 8;
        }
        int linePitch = pHdr->dwBitDepth * dwNewSizeX / 8;
        // alex_k: the original just uses ^ pitch for loading into surface.
        // no idea why additional checks for DXT[n] are needed. How does EI/DX7 handle it?
        // Is pDX11 data sparse whereas dx7 is packed?
        // This loads MIPs correctly too (if you place break, all mips > 1 become black)
        if ((pHdr->dwFormat & 0x00FFFFFF) == MMP_FORMAT_DXTN)
            srd.SysMemPitch = ((dwNewSizeX + 3) / 4) * 16;
        if (pHdr->dwFormat == MMP_FORMAT_DXT1)
            srd.SysMemPitch = ((dwNewSizeX + 3) / 4) * 8;
        srd.SysMemSlicePitch = 0;
        pDXDDS->UpdateTexture(sub_index, nullptr, &srd);
        auto depthPitch = linePitch * dwNewSizeY;
        // get next mip-map surface
        lpData = reinterpret_cast<LPBYTE>(pHdr->GetMipLevelData(i + 1));
        // lpData = reinterpret_cast<LPBYTE>(reinterpret_cast<int>(lpData) + depthPitch); //same as
        nTotalSize += depthPitch;
        // correct sizes
        dwNewSizeX >>= 1;
        dwNewSizeY >>= 1;
    }

    return nTotalSize;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTexture::Unload()
{

    nTotalSize = 0;
    //
    userdata.RemoveAll();
    pSurface = 0;
    ResetFlag(TEX_LOADED);
}
/////////////////////////////////////////////////////////////////////////////////////
// un-inlined
CTexture::CTexture()
    : CGameBase()
{
    Create();

#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
CTexture::~CTexture()
{
    Destroy();
}
BOOL CTexture::Create()
{
    szName.Empty();

    pSurface = 0;

    dwSizeX = dwSizeY = 0;
    nTotalSize = 0;
    SetLRUSegment(0);

    userdata.RemoveAll();

    return TRUE;
}
void CTexture::Destroy()
{

    pSurface = 0;
    SetLRUSegment(0);
    ResetAllFlags();
}
/////////////////////////////////////////////////////////////////////////////////////
bool CTexture::IsUserDataEqual(const CBYTEArray &userdata1) const
{
    if (userdata.GetSize() != userdata1.GetSize())
        return false;
    return memcmp(userdata.GetData(), userdata1.GetData(), userdata.GetSize()) == 0;
}

