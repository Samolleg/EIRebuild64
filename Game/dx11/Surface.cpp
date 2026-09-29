#include "stdafx.h"
#include "surface.h"
#include <io.h>
#include "RendererDX11.h"
#include "EffectProcessor.h"

size_t BitsPerPixel(DXGI_FORMAT fmt)
{
    switch (static_cast<int>(fmt))
    {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
        return 128;

    case DXGI_FORMAT_R32G32B32_TYPELESS:
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
        return 96;

    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
    case DXGI_FORMAT_Y416:
    case DXGI_FORMAT_Y210:
    case DXGI_FORMAT_Y216:
        return 64;

    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R16G16_TYPELESS:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
    case DXGI_FORMAT_R8G8_B8G8_UNORM:
    case DXGI_FORMAT_G8R8_G8B8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_AYUV:
    case DXGI_FORMAT_Y410:
    case DXGI_FORMAT_YUY2:
        /*case XBOX_DXGI_FORMAT_R10G10B10_7E3_A2_FLOAT:
        case XBOX_DXGI_FORMAT_R10G10B10_6E4_A2_FLOAT:
        case XBOX_DXGI_FORMAT_R10G10B10_SNORM_A2_UNORM:*/
        return 32;

    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
        /*case XBOX_DXGI_FORMAT_D16_UNORM_S8_UINT:
        case XBOX_DXGI_FORMAT_R16_UNORM_X8_TYPELESS:
        case XBOX_DXGI_FORMAT_X16_TYPELESS_G8_UINT:
        case WIN10_DXGI_FORMAT_V408:*/
        return 24;

    case DXGI_FORMAT_R8G8_TYPELESS:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_B5G6R5_UNORM:
    case DXGI_FORMAT_B5G5R5A1_UNORM:
    case DXGI_FORMAT_A8P8:
    case DXGI_FORMAT_B4G4R4A4_UNORM:
        /*case WIN10_DXGI_FORMAT_P208:
        case WIN10_DXGI_FORMAT_V208:*/
        return 16;

    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_420_OPAQUE:
    case DXGI_FORMAT_NV11:
        return 12;

    case DXGI_FORMAT_R8_TYPELESS:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_A8_UNORM:
    case DXGI_FORMAT_AI44:
    case DXGI_FORMAT_IA44:
    case DXGI_FORMAT_P8:
        // case XBOX_DXGI_FORMAT_R4G4_UNORM:
        return 8;

    case DXGI_FORMAT_R1_UNORM:
        return 1;

    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
        return 4;

    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS:
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return 8;

    default:
        return 0;
    }
}

/////////////////////////////////////////////////////////////////////////////////////
CDXGISurface::CDXGISurface(CDX11 *pDXDirectDraw, DWORD dwWidth, DWORD dwHeight)
    : pDX11(pDXDirectDraw)
    , dwSurfaceWidth(0)
    , dwSurfaceHeight(0)
    , dwSurfaceBPP(0)
    , texture(0)
    , textureView(0)
{
    D3D11_TEXTURE2D_DESC tex2d;
    pDX11->DefaultSurfaceDesc(&tex2d);
    tex2d.Width = dwWidth;
    tex2d.Height = dwHeight;
    tex2d.Format = DXGI_FORMAT::DXGI_FORMAT_B8G8R8A8_UNORM;
    // tex2d.Format = DXGI_FORMAT::DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    // create GDI-drawable surface
#ifdef DXGI_ENABLED
    tex2d.MiscFlags = D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
    tex2d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
#else
    ddsd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
#endif
    if (!CreateTexture(&tex2d))
        throw dbgnew CDXException("Can't create DDSurface %d:%d with default BPP.\n", dwWidth,
                                  dwHeight);
    //
    ExtractSurfaceParams();
}
/////////////////////////////////////////////////////////////////////////////////////
CDXGISurface::CDXGISurface(CDX11 *pDXDirectDraw, D3D11_TEXTURE2D_DESC *ptex2d)
    : pDX11(pDXDirectDraw)
    , dwSurfaceWidth(0)
    , dwSurfaceHeight(0)
    , dwSurfaceBPP(0)
    , texture(0)
    , textureView(0)
{
    pDX11->CreateTexture(ptex2d, texture);
    pDX11->CreateShaderResource(texture, textureView);

    ExtractSurfaceParams();
}
/////////////////////////////////////////////////////////////////////////////////////
CDXGISurface::CDXGISurface(CDX11 *pDXDirectDraw, const SMMPFileHeader *pHdr, bool bInVideo)
    : pDX11(pDXDirectDraw)
    , dwSurfaceWidth(0)
    , dwSurfaceHeight(0)
    , dwSurfaceBPP(0)
    , texture(0)
    , textureView(0)
{
    if (!Load(pHdr, bInVideo))
        throw dbgnew CDXException("Can't load picture to surface.\n");
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CDXGISurface::CreateTexture(D3D11_TEXTURE2D_DESC *ptex2d)
{
    pDX11->CreateTexture(ptex2d, texture);
    pDX11->CreateShaderResource(texture, textureView);
    return ExtractSurfaceParams();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CDXGISurface::ExtractSurfaceParams()
{
    D3D11_TEXTURE2D_DESC tex2d;
    // reset existed surface params
    dwSurfaceWidth = 0;
    dwSurfaceHeight = 0;
    dwSurfaceBPP = 0;
    // get dd surface desc to extract new surface params
    if (GetSurfaceDesc(&tex2d) == FALSE)
        return ReportError(
            "CDXGISurface::ExtractSurfaceParams.\n Can't get surface description.\n");
    // extract surface params from tex2d and ddpf
    dwSurfaceWidth = tex2d.Width;
    dwSurfaceHeight = tex2d.Height;
    dwSurfaceBPP = BitsPerPixel(tex2d.Format);
    tfFormat = tex2d.Format;
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CDXGISurface::Load( LPCSTR pszFileName )
// load surface data from single MMP file by name
BOOL CDXGISurface::Load(LPCSTR pszFileName, bool bInVideo)
{
    FILE *file = fopen(pszFileName, "rb");

    if (!file)
        return ReportError("Can't open file \"%s\" to read.\n", pszFileName);
    if (Load(file, bInVideo) == FALSE)
        return ReportError("Can't load file \"%s\".\n", pszFileName);
    fclose(file);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CDXGISurface::Load( CFile& file )
// load surface data from already opened file
BOOL CDXGISurface::Load(FILE *file, bool bInVideo)
{
    int nFileLength = _filelength(_fileno(file));
    BYTE *pData = dbgnew BYTE[nFileLength]; // allocate temporary buffer
    // check for successfull buffer allocation
    if (pData == 0)
        return ReportError("Can't allocate data to load MMP file.\n");
    // read file to temporary buffer and 'Load' from it. delete temporary buffer after load
    fread(pData, 1, nFileLength, file);
    BOOL bCheck = Load(reinterpret_cast<SMMPFileHeader *>(pData),
                       bInVideo); // load data from temporary buffer
    delete[] pData;
    //
    return bCheck;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CDXGISurface::Load( SMMPFileHeader* pHdr )
// load picture from mapped MMP file. If dd surface was not created, create it with dimentions from
// MMP file.

int CopyToSurface(CDXGISurface *pDXDDS, const SMMPFileHeader *pHdr, DWORD dwFromLevel)
{
    LPBYTE lpData = reinterpret_cast<LPBYTE>(pHdr->GetMipLevelData(dwFromLevel));
    DWORD dwNewSizeX = pHdr->dwDimentionX >> dwFromLevel;
    DWORD dwNewSizeY = pHdr->dwDimentionY >> dwFromLevel;
    // fill mip surfaces
    int nTotalSize = 0;
    for (int i = int(dwFromLevel); i < int(pHdr->dwMipMapLevel); i++)
    {
        int sub_index = D3D11CalcSubresource(i, 0, pHdr->dwMipMapLevel);

        D3D11_SUBRESOURCE_DATA srd = { 0 };
        srd.pSysMem = lpData;
        srd.SysMemPitch = pHdr->dwBitDepth * dwNewSizeX / 8;
        // alex_k: no idea why this check is needed. How does EI/DX7 handle it? Is pDX11 data
        // sparse whereas dx7 is packed? this loads MIPs correctly too (if you place break, all mips
        // > 1 become black)
        if (pHdr->dwBitDepth == 4)
            srd.SysMemPitch = pHdr->dwBitDepth * dwNewSizeX / 2;
        auto depthPitch = pHdr->dwBitDepth * dwNewSizeX / 8 * dwNewSizeY;
        srd.SysMemSlicePitch = 0;
        pDXDDS->UpdateTexture(sub_index, nullptr, &srd);
        lpData = reinterpret_cast<LPBYTE>(reinterpret_cast<int>(lpData) + depthPitch);
        nTotalSize += depthPitch;
        // get next mip-map surface
        // correct sizes
        dwNewSizeX >>= 1;
        dwNewSizeY >>= 1;
    }

    return nTotalSize;
}

BOOL CDXGISurface::Load(const SMMPFileHeader *pHdr, bool bInVideo)
{
    ETextureFormats texformat = ETextureFormats(-1);
    // check for header valids
    if (!pHdr)
        return ReportError("Can't load picture from NULL pointer.\n");
    else if (pHdr->dwFormat != MMP_FORMAT_8888)
        return ReportError("Can't load picture from non-8888 format.\n");

    D3D11_TEXTURE2D_DESC tex2d;
    pDX11->DefaultSurfaceDesc(&tex2d);
    tex2d.Width = pHdr->dwDimentionX;
    tex2d.Height = pHdr->dwDimentionY;
    tex2d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    tex2d.MipLevels = pHdr->dwMipMapLevel;
    CreateTexture(&tex2d);
    CopyToSurface(this, pHdr, 0);
    return TRUE;
}
void CDXGISurface::DefaultSurfaceDesc(D3D11_TEXTURE2D_DESC *ptex2d)
{
    pDX11->DefaultSurfaceDesc(ptex2d);
}

// void CDXGISurface::CopyResource(ID3D11Texture2D *src)
//{
//     pDX11->GetDeviceContext()->CopyResource(this->GetTexture().Get(), src);
// }

void CDXGISurface::CopyResource(CDXGISurface *src)
{
    pDX11->GetDeviceContext()->CopyResource(this->GetTexture().Get(), src->GetTexture().Get());
}

void CDXGISurface::CopySubresourceRegion(UINT dstSubresource, UINT dstX, UINT dstY, UINT dstZ,
                                         CDXGISurface *src, UINT srcSubresource, D3D11_BOX *pBox)
{
    pDX11->GetDeviceContext()->CopySubresourceRegion(this->GetTexture().Get(), dstSubresource, dstX,
                                                     dstY, dstZ, src->GetTexture().Get(),
                                                     srcSubresource, pBox);
}

void CDXGISurface::GetPixel(int nX, int nY)
{
}

std::unique_ptr<DWORD[]> CDXGISurface::GetPixelBuffer()
{
    int pixelCount = dwSurfaceWidth * dwSurfaceHeight;
    // assume 32 bit;
    // create cpu-copyable texture
    D3D11_TEXTURE2D_DESC tex2d;
    GetSurfaceDesc(&tex2d);
    tex2d.Usage = D3D11_USAGE::D3D11_USAGE_STAGING;
    tex2d.BindFlags = 0;
    tex2d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    tex2d.MiscFlags = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    pDX11->CreateTexture(&tex2d, staging);

    // copy texture to staging
    pDX11->GetDeviceContext()->CopyResource(staging.Get(), this->GetTexture().Get());

    D3D11_MAPPED_SUBRESOURCE srd = {};
    HRESULT hr;
    hr = pDX11->GetDeviceContext()->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &srd);
    if (FAILED(hr))
        return nullptr;
    std::unique_ptr<DWORD[]> pixelBuffer = std::make_unique<DWORD[]>(pixelCount);
    memcpy(pixelBuffer.get(), srd.pData, srd.RowPitch * dwSurfaceHeight);
    pDX11->GetDeviceContext()->Unmap(staging.Get(), 0);

    return pixelBuffer;
}

void CDXGISurface::Fade()
{
    pDX11->GetEffectProcessor()->Fade(this->GetShaderResource().Get());
}
void CDXGISurface::StretchTo(const CDXGISurface *src, const RECT &rect)
{
    auto effproc = pDX11->GetEffectProcessor();
    effproc->StretchTo(this->GetShaderResource().Get(), src->GetShaderResource().Get(), rect);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CDXGISurface::AcquireDC(HDC *pDC)
{
#ifdef DXGI_ENABLED
    Microsoft::WRL::ComPtr<IDXGISurface1> surf;
    IfFailedThrowHR(texture->QueryInterface(IID_PPV_ARGS(&surf)));
    surf->GetDC(FALSE, pDC);
    hDc = *pDC;
    return TRUE;
#else
    return 0;
#endif
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CDXGISurface::ReleaseDC( HDC hDC )
BOOL CDXGISurface::ReleaseDC(HDC hDC)
{
#ifdef DXGI_ENABLED
    Microsoft::WRL::ComPtr<IDXGISurface1> surf;
    texture->QueryInterface(IID_PPV_ARGS(&surf));
    surf->ReleaseDC(nullptr);
    hDc = 0;
    return TRUE;
#else
    return 0;
#endif
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CDXGISurface::GetSurfaceDesc( LPDDSURFACEDESC2 pDDSD )
// get surface description
BOOL CDXGISurface::GetSurfaceDesc(D3D11_TEXTURE2D_DESC *ptex2d)
{
    texture->GetDesc(ptex2d);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CDXGISurface::GetPixelFormat( LPDDPIXELFORMAT pDDPF )
// get pixel format from surface
BOOL CDXGISurface::GetTextureFormat(DXGI_FORMAT &texFormat)
{
    D3D11_TEXTURE2D_DESC desc;
    GetSurfaceDesc(&desc);
    texFormat = desc.Format;
    return TRUE;
}
HRESULT CDXGISurface::BltToRenderSurface(LPRECT pDestRect, LPRECT pSrcRect, DWORD dwFlags,
                                         LPDDBLTFX pDDBltFx)
{
    if (dwFlags & DDBLT_KEYSRCOVERRIDE)
        return pDX11->DrawColorKeySurface(textureView.Get(), *pDestRect, pSrcRect,
            pDDBltFx ? pDDBltFx->ddckSrcColorkey.dwColorSpaceLowValue : 0);
    pDX11->GetSpriteBatch()->Draw(textureView.Get(), *pDestRect, pSrcRect);
    return S_OK;
}
HRESULT CDXGISurface::BltFromRenderSurface(LPRECT pDestRect, LPRECT pSrcRect, DWORD dwFlags,
                                           LPDDBLTFX pDDBltFx)
{
    auto swapchain = pDX11->GetSwapChain();
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer;
    IfFailedThrowHR(swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)));
    pDX11->GetDeviceContext()->CopyResource(texture.Get(), backbuffer.Get());
    return S_OK;
}
void CDXGISurface::UpdateTexture(UINT subresource_index, D3D11_BOX *region,
                                 D3D11_SUBRESOURCE_DATA *srd)
{
    pDX11->UpdateTexture(this->texture.Get(), subresource_index, region, srd);
}
/////////////////////////////////////////////////////////////////////////////////////
HRESULT CDXGISurface::Clear(DWORD dwColorFill)
{
#ifdef DXGI_ENABLED
    ASSERT(hDc);
    // dwColorFill = 0x000000FF;
    D3D11_TEXTURE2D_DESC tex2d;
    texture->GetDesc(&tex2d);
    // std::unique_ptr<DWORD[]> data = std::make_unique<DWORD[]>(tex2d.Width * tex2d.Height);
    // for (int i = 0; i < tex2d.Width; ++i)
    //{
    //    for (int j = 0; j < tex2d.Height; ++j)
    //    {
    //        *pSrc = dwColorFill;
    //        pSrc++;
    //    }
    //}
    RECT rect = { 0, 0, dwSurfaceWidth, dwSurfaceHeight };
    HBRUSH brush = CreateSolidBrush(dwColorFill);
    FillRect(hDc, &rect, brush);
    DeleteObject(&brush);
#endif
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CDXGISurface::DumpToFile(LPCSTR path, UINT rows)
{
    // create to-cpu copyable texture
    D3D11_TEXTURE2D_DESC tex2d;
    GetSurfaceDesc(&tex2d);
    tex2d.Usage = D3D11_USAGE::D3D11_USAGE_STAGING;
    tex2d.BindFlags = 0;
    tex2d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    pDX11->CreateTexture(&tex2d, staging);

    // copy texture to staging
    pDX11->GetDeviceContext()->CopyResource(staging.Get(), this->GetTexture().Get());

    // read staging, write to file
    D3D11_MAPPED_SUBRESOURCE ResourceDesc = {};
    pDX11->GetDeviceContext()->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &ResourceDesc);

    CFile file;
    file.Open(path, CFile::modeCreate | CFile::modeWrite);
    file.Write(ResourceDesc.pData, ResourceDesc.RowPitch * rows);
    file.Close();

    pDX11->GetDeviceContext()->Unmap(staging.Get(), 0);
};

void CDXGISurface::LoadFromFileDump(LPCSTR path, UINT linePitch, UINT rows)
{
    CFile file;
    file.Open(path, CFile::modeRead);
    // read file into buffer
    const int depthPitch = linePitch * rows;
    std::unique_ptr<BYTE[]> buf = std::make_unique<BYTE[]>(depthPitch);
    auto read = file.Read(buf.get(), depthPitch);
    file.Close();

    this->LoadFromByteDump(buf.get(), linePitch, rows);
}

void CDXGISurface::LoadFromByteDump(void *src, UINT linePitch, UINT rows)
{
    D3D11_SUBRESOURCE_DATA srd = {};
    srd.pSysMem = src;
    srd.SysMemPitch = linePitch;
    D3D11_BOX region = { 0, 0, 0, dwSurfaceWidth, rows, 1 };
    this->UpdateTexture(0, &region, &srd);
};