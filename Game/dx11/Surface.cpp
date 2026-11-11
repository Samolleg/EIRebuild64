#include "stdafx.h"
#include "../GameJournal.h"
#include "surface.h"
#include <io.h>
#include "RendererDX11.h"
#include "../FrameDiagnostics.h"
#include "../CorePerf.h"
#include "PostProcess.h"
#include "RetailModalFade.h"
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include "../OwnedRender/E1SurfaceFacade.h"
#include "../OwnedRender/E1TextLifetime.h"
#include "hud_layout_context.h"
#include "MmpRgbToBgra.h"
#include <limits>
#include <DirectXTex.h>

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

DXGI_FORMAT OwnedRgbSurfaceFormat(DXGI_FORMAT format)
{
    switch (format) {
    case DXGI_FORMAT_B5G6R5_UNORM:
    case DXGI_FORMAT_B5G5R5A1_UNORM:
    case DXGI_FORMAT_B4G4R4A4_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    default:
        return format;
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
    ownedSurface.BeginWrite();
    D3D11_TEXTURE2D_DESC normalized = *ptex2d;
    opaquePadding = normalized.Format == DXGI_FORMAT_B8G8R8X8_UNORM;
    normalized.Format = OwnedRgbSurfaceFormat(normalized.Format);
    if (!CreateTexture(&normalized)) {
        if (pDX11->D3D12Selected())
            throw dbgnew CDXException("Direct3D 12: cannot create surface %ux%u format=%u mips=%u.\n",
                normalized.Width, normalized.Height, unsigned(normalized.Format), normalized.MipLevels);
        throw dbgnew CDXException("Can't create surface.\n");
    }

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
    bool nativeD3D12 = false;
    IfFailedThrowHR(pDX11->GetRendererFacade().Visit([&](owned::Renderer& renderer) {
        nativeD3D12 = renderer.d3d12_backend(); return S_OK;
    }));
    if (nativeD3D12) {
        if (!ptex2d || !ptex2d->Width || !ptex2d->Height ||
            ptex2d->Width > UINT((std::numeric_limits<LONG>::max)() / 4) ||
            ptex2d->Height > UINT((std::numeric_limits<LONG>::max)()) ||
            size_t(ptex2d->Height) > (std::numeric_limits<size_t>::max)() / (size_t(ptex2d->Width) * 4) ||
            (ptex2d->Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
             ptex2d->Format != DXGI_FORMAT_B5G6R5_UNORM &&
             ptex2d->Format != DXGI_FORMAT_BC1_UNORM &&
             ptex2d->Format != DXGI_FORMAT_BC2_UNORM &&
             ptex2d->Format != DXGI_FORMAT_BC3_UNORM) ||
            ptex2d->ArraySize != 1 || ptex2d->SampleDesc.Count != 1 || hDc || uiRecordingDc)
            return FALSE;
        auto image = std::make_shared<owned::SurfaceImage>();
        image->width = ptex2d->Width; image->height = ptex2d->Height;
        image->pitch = LONG(image->width * 4);
        image->format.dwSize = sizeof(image->format);
        image->format.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
        image->format.dwRGBBitCount = 32;
        image->format.dwRBitMask = 0x00ff0000; image->format.dwGBitMask = 0x0000ff00;
        image->format.dwBBitMask = 0x000000ff; image->format.dwRGBAlphaBitMask = 0xff000000;
        image->cpu_pinned = true;
        image->pixels.resize(size_t(image->pitch) * image->height, 0);
        ownedSurface.BeginWrite();
        cpuImage = std::move(image); cpuDesc = *ptex2d;
        cpuUploadFormat = ptex2d->Format;
        // Save thumbnails request RGB565; their file writer performs the packing.
        // Native source storage and GDI always use canonical BGRA pixels.
        cpuDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        texture.Reset(); textureView.Reset(); pixelReadbackStaging.Reset();
        if (!PublishSurface()) return FALSE;
        return ExtractSurfaceParams();
    }
    ownedSurface.BeginWrite();
    pDX11->CreateTexture(ptex2d, texture);
    pDX11->CreateShaderResource(texture, textureView);
    return ExtractSurfaceParams();
}
/////////////////////////////////////////////////////////////////////////////////////
bool CDXGISurface::PublishSurface(bool fullyOpaque) const noexcept
{
    return cpuImage ? ownedSurface.CompleteCpu(*cpuImage, fullyOpaque) :
                      ownedSurface.Complete(texture.Get(), fullyOpaque);
}

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
        // alex_k: no idea why this check is needed. How does EI/DX7 handle it? Is pDX11 data
        // sparse whereas dx7 is packed? this loads MIPs correctly too (if you place break, all mips
        // > 1 become black)
        if (pHdr->dwBitDepth == 4)
            srd.SysMemPitch = pHdr->dwBitDepth * dwNewSizeX / 2;
        auto depthPitch = pHdr->dwBitDepth * dwNewSizeX / 8 * dwNewSizeY;
        srd.SysMemSlicePitch = 0;
        pDXDDS->UpdateTexture(sub_index, nullptr, &srd);
        lpData += depthPitch;
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
    else if (pHdr->dwFormat != MMP_FORMAT_8888 && pHdr->dwBitDepth != 16 &&
             !(pHdr->dwBitDepth == 32 && pHdr->dwABits == 0))
        return ReportError("Can't load picture from unsupported RGB format.\n");

    D3D11_TEXTURE2D_DESC tex2d;
    pDX11->DefaultSurfaceDesc(&tex2d);
    tex2d.Width = pHdr->dwDimentionX;
    tex2d.Height = pHdr->dwDimentionY;
    tex2d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    tex2d.MipLevels = pHdr->dwMipMapLevel;
    if (!CreateTexture(&tex2d)) return FALSE;
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
    if (cpuImage) {
        if (!src || !src->cpuImage || hDc || src->hDc ||
            dwSurfaceWidth != src->dwSurfaceWidth || dwSurfaceHeight != src->dwSurfaceHeight)
            IfFailedThrowHR(E_INVALIDARG);
        auto pixels = src->cpuImage->pixels;
        ownedSurface.BeginWrite();
        cpuImage->pixels = std::move(pixels);
        IfFailedThrowHR(PublishSurface() ? S_OK : E_FAIL);
        return;
    }
    ownedSurface.BeginWrite();
    pDX11->GetDeviceContext()->CopyResource(this->GetTexture().Get(), src->GetTexture().Get());
    ownedSurface.Complete(texture.Get());
}

void CDXGISurface::CopySubresourceRegion(UINT dstSubresource, UINT dstX, UINT dstY, UINT dstZ,
                                         CDXGISurface *src, UINT srcSubresource, D3D11_BOX *pBox)
{
    if (cpuImage) {
        if (!src || !src->cpuImage || hDc || src->hDc || dstSubresource || srcSubresource || dstZ)
            IfFailedThrowHR(E_INVALIDARG);
        const D3D11_BOX box = pBox ? *pBox :
            D3D11_BOX{0, 0, 0, src->dwSurfaceWidth, src->dwSurfaceHeight, 1};
        if (box.front || box.back != 1 || box.left >= box.right || box.top >= box.bottom ||
            box.right > src->dwSurfaceWidth || box.bottom > src->dwSurfaceHeight ||
            dstX > dwSurfaceWidth || dstY > dwSurfaceHeight ||
            box.right - box.left > dwSurfaceWidth - dstX ||
            box.bottom - box.top > dwSurfaceHeight - dstY)
            IfFailedThrowHR(E_INVALIDARG);
        // A local copy keeps overlapping/self copies and prior submissions immutable.
        auto pixels = cpuImage->pixels;
        const auto& source = static_cast<const owned::SurfaceImage&>(*src->cpuImage).pixels;
        for (UINT row = 0; row < box.bottom - box.top; ++row)
            memcpy(pixels.data() + size_t(row + dstY) * cpuImage->pitch + size_t(dstX) * 4,
                source.data() + size_t(row + box.top) * src->cpuImage->pitch + size_t(box.left) * 4,
                size_t(box.right - box.left) * 4);
        ownedSurface.BeginWrite();
        cpuImage->pixels = std::move(pixels);
        IfFailedThrowHR(PublishSurface() ? S_OK : E_FAIL);
        return;
    }
    ownedSurface.BeginWrite();
    pDX11->GetDeviceContext()->CopySubresourceRegion(this->GetTexture().Get(), dstSubresource, dstX,
                                                     dstY, dstZ, src->GetTexture().Get(),
                                                     srcSubresource, pBox);
    ownedSurface.Complete(texture.Get());
}

void CDXGISurface::GetPixel(int nX, int nY)
{
}

HRESULT CDXGISurface::CopyPixelBuffer(DWORD *destination, size_t pixelCount)
{
    if (!destination || pixelCount < size_t(dwSurfaceWidth) * dwSurfaceHeight)
        return E_INVALIDARG;
    if (cpuImage) {
        const auto& pixels = static_cast<const owned::SurfaceImage&>(*cpuImage).pixels;
        for (UINT y = 0; y < dwSurfaceHeight; ++y)
            memcpy(destination + size_t(y) * dwSurfaceWidth,
                pixels.data() + size_t(y) * cpuImage->pitch, size_t(dwSurfaceWidth) * sizeof(DWORD));
        return S_OK;
    }
    if (!pixelReadbackStaging)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        GetSurfaceDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        HRESULT hr = pDX11->GetDevice()->CreateTexture2D(&desc, nullptr,
                                                      pixelReadbackStaging.GetAddressOf());
        if (hr == E_OUTOFMEMORY) GameJournal::AllocationFailure("texture_readback_staging");
        if (FAILED(hr))
            return hr;
    }
    auto context = pDX11->GetDeviceContext();
    context->CopyResource(pixelReadbackStaging.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = context->Map(pixelReadbackStaging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr))
        return hr;
    const size_t rowBytes = size_t(dwSurfaceWidth) * sizeof(DWORD);
    if (mapped.RowPitch < rowBytes)
    {
        context->Unmap(pixelReadbackStaging.Get(), 0);
        return E_FAIL;
    }
    for (DWORD y = 0; y < dwSurfaceHeight; ++y)
        memcpy(destination + size_t(y) * dwSurfaceWidth,
               static_cast<const BYTE *>(mapped.pData) + size_t(y) * mapped.RowPitch,
               rowBytes);
    context->Unmap(pixelReadbackStaging.Get(), 0);
    return S_OK;
}

std::unique_ptr<DWORD[]> CDXGISurface::GetPixelBuffer()
{
    if (cpuImage) {
        const size_t count = size_t(dwSurfaceWidth) * dwSurfaceHeight;
        auto pixels = std::make_unique<DWORD[]>(count);
        if (FAILED(CopyPixelBuffer(pixels.get(), count))) return nullptr;
        return pixels;
    }
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
    const size_t rowBytes = size_t(dwSurfaceWidth) * sizeof(DWORD);
    if (srd.RowPitch < rowBytes)
    {
        pDX11->GetDeviceContext()->Unmap(staging.Get(), 0);
        return nullptr;
    }
    std::unique_ptr<DWORD[]> pixelBuffer = std::make_unique<DWORD[]>(pixelCount);
    for (DWORD y = 0; y < dwSurfaceHeight; ++y)
        memcpy(pixelBuffer.get() + size_t(y) * dwSurfaceWidth,
               static_cast<const BYTE *>(srd.pData) + size_t(y) * srd.RowPitch, rowBytes);
    pDX11->GetDeviceContext()->Unmap(staging.Get(), 0);

    return pixelBuffer;
}

namespace {
struct OwnedSurfaceEffect {
    ID3D11ShaderResourceView* source = nullptr;
    ID3D11Texture2D* destination = nullptr;
    const RECT* viewport = nullptr;
    bool fade = false;
};

HRESULT RunOwnedSurfaceEffect(void* opaque, const owned::EmbeddedDeviceView& view)
{
#ifdef EID3D11_EMBEDDED
    auto& effect = *static_cast<OwnedSurfaceEffect*>(opaque);
    if (!view.device || !view.context || !effect.source || !effect.destination)
        return E_INVALIDARG;
    D3D11_TEXTURE2D_DESC description{};
    effect.destination->GetDesc(&description);
    description.BindFlags |= D3D11_BIND_RENDER_TARGET;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> target;
    HRESULT hr = view.device->CreateTexture2D(&description, nullptr, target.GetAddressOf());
    if (FAILED(hr)) return hr;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> targetView;
    hr = view.device->CreateRenderTargetView(target.Get(), nullptr, targetView.GetAddressOf());
    if (FAILED(hr)) return hr;

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> oldTarget;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> oldDepth;
    view.context->OMGetRenderTargets(1, oldTarget.GetAddressOf(), oldDepth.GetAddressOf());
    D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    view.context->RSGetViewports(&viewportCount, oldViewports);
    view.context->OMSetRenderTargets(1, targetView.GetAddressOf(), nullptr);
    if (effect.viewport) {
        D3D11_VIEWPORT viewport{};
        viewport.TopLeftX = static_cast<float>(effect.viewport->left);
        viewport.TopLeftY = static_cast<float>(effect.viewport->top);
        viewport.Width = static_cast<float>(effect.viewport->right);
        viewport.Height = static_cast<float>(effect.viewport->bottom);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        view.context->RSSetViewports(1, &viewport);
    }

    Microsoft::WRL::ComPtr<ID3D11PixelShader> fadeShader;
    if (effect.fade) {
        Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
        hr = RetailModalFade::Compile(bytecode.GetAddressOf());
        if (SUCCEEDED(hr))
            hr = view.device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(),
                nullptr, fadeShader.GetAddressOf());
    }
    if (SUCCEEDED(hr)) {
        try {
            DirectX::BasicPostProcess postProcess(view.device);
            postProcess.SetSourceTexture(effect.source);
            postProcess.SetEffect(DirectX::BasicPostProcess::Effect::Copy);
            postProcess.Process(view.context, [context = view.context, shader = fadeShader.Get()] {
                if (shader) context->PSSetShader(shader, nullptr, 0);
            });
        } catch (...) {
            hr = E_FAIL;
        }
    }
    view.context->OMSetRenderTargets(1, oldTarget.GetAddressOf(), oldDepth.Get());
    if (viewportCount) view.context->RSSetViewports(viewportCount, oldViewports);
    if (FAILED(hr)) return hr;
    view.context->CopyResource(effect.destination, target.Get());
    return S_OK;
#else
    (void)opaque; (void)view;
    return E_NOTIMPL;
#endif
}

HRESULT ApplyOwnedSurfaceEffect(OwnedRender::RendererFacadeCore<owned::Renderer>& facade,
    ID3D11ShaderResourceView* source, ID3D11Texture2D* destination,
    const RECT* viewport, bool fade)
{
    OwnedSurfaceEffect effect{source, destination, viewport, fade};
    return facade.Visit([&](owned::Renderer& renderer) {
        return renderer.visit_embedded_device(&effect, &RunOwnedSurfaceEffect);
    });
}
}

void CDXGISurface::Fade()
{
    if (cpuImage) {
        if (hDc || uiRecordingDc) IfFailedThrowHR(DXGI_ERROR_INVALID_CALL);
        // Detach from completed publications before modifying the source bytes.
        auto pixels = cpuImage->pixels;
        auto* bytes = pixels.data();
        for (UINT y = 0; y < dwSurfaceHeight; ++y) {
            for (UINT x = 0; x < dwSurfaceWidth; ++x) {
                auto* pixel = bytes + size_t(y) * cpuImage->pitch + size_t(x) * 4;
                const BYTE gray = BYTE(RetailModalFade::Gray(pixel[2], pixel[1], pixel[0]));
                pixel[0] = pixel[1] = pixel[2] = gray;
            }
        }
        ownedSurface.BeginWrite();
        cpuImage->pixels = std::move(pixels);
        IfFailedThrowHR(PublishSurface() ? S_OK : E_FAIL);
        return;
    }
    ownedSurface.BeginWrite();
    const HRESULT hr = ApplyOwnedSurfaceEffect(pDX11->GetRendererFacade(),
        textureView.Get(), texture.Get(), nullptr, true);
    if (FAILED(hr)) throw hr;
    ownedSurface.Complete(texture.Get());
}
void CDXGISurface::StretchTo(const CDXGISurface *src, const RECT &rect)
{
    // The historical argument named src is the actual destination.
    if (cpuImage) {
        if (!src || !src->cpuImage || hDc || uiRecordingDc || src->hDc || src->uiRecordingDc)
            IfFailedThrowHR(E_INVALIDARG);
        // Preserve the source API's viewport convention: right/bottom are
        // extents, including when left/top specify a destination offset.
        if (rect.right <= 0 || rect.bottom <= 0 ||
            rect.right > (std::numeric_limits<LONG>::max)() / 4)
            IfFailedThrowHR(E_INVALIDARG);
        const auto& sourcePixels = static_cast<const owned::SurfaceImage&>(*cpuImage).pixels;
        DirectX::Image source{};
        source.width = dwSurfaceWidth; source.height = dwSurfaceHeight;
        source.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        source.rowPitch = size_t(cpuImage->pitch);
        source.slicePitch = sourcePixels.size();
        source.pixels = const_cast<std::uint8_t*>(sourcePixels.data());
        DirectX::ScratchImage resized;
        IfFailedThrowHR(DirectX::Resize(source, size_t(rect.right), size_t(rect.bottom),
            DirectX::TEX_FILTER_LINEAR | DirectX::TEX_FILTER_FORCE_NON_WIC, resized));
        const auto* scaled = resized.GetImage(0, 0, 0);
        if (!scaled) IfFailedThrowHR(E_FAIL);
        auto pixels = src->cpuImage->pixels;
        const auto left = (std::max)(std::int64_t(0), std::int64_t(rect.left));
        const auto top = (std::max)(std::int64_t(0), std::int64_t(rect.top));
        const auto right = (std::min)(std::int64_t(src->dwSurfaceWidth),
            std::int64_t(rect.left) + rect.right);
        const auto bottom = (std::min)(std::int64_t(src->dwSurfaceHeight),
            std::int64_t(rect.top) + rect.bottom);
        if (left >= right || top >= bottom) return;
        for (auto y = top; y < bottom; ++y)
            memcpy(pixels.data() + size_t(y) * src->cpuImage->pitch + size_t(left) * 4,
                scaled->pixels + size_t(y - rect.top) * scaled->rowPitch + size_t(left - rect.left) * 4,
                size_t(right - left) * 4);
        src->ownedSurface.BeginWrite();
        src->cpuImage->pixels = std::move(pixels);
        IfFailedThrowHR(src->PublishSurface() ? S_OK : E_FAIL);
        return;
    }
    src->ownedSurface.BeginWrite();
    const HRESULT hr = ApplyOwnedSurfaceEffect(pDX11->GetRendererFacade(),
        textureView.Get(), src->texture.Get(), &rect, false);
    if (FAILED(hr)) throw hr;
    src->ownedSurface.Complete(src->texture.Get());
}
/////////////////////////////////////////////////////////////////////////////////////
std::uint64_t CDXGISurface::OwnedSurfaceGeneration() const noexcept
{
    const auto value=OwnedRender::E1SurfaceValue(ownedSurface.Id());
    return value?value->generation:0;
}

CDXGISurface::~CDXGISurface()
{
    owned::owned_text_retire_surface(OwnedSurfaceId());
    if(uiRecordingDc)owned::ui_layer_abort(uiRecordingDc);
    if (cpuImage) CloseCpuDC();
    if(hDc && texture){
        Microsoft::WRL::ComPtr<IDXGISurface1> surface;
        if(SUCCEEDED(texture.As(&surface)))surface->ReleaseDC(nullptr);
    }
}

void CDXGISurface::CloseCpuDC() noexcept
{
    if (!hDc) return;
    SelectObject(hDc, cpuPreviousBitmap);
    DeleteObject(cpuBitmap);
    DeleteDC(hDc);
    hDc = nullptr; cpuBitmap = nullptr; cpuPreviousBitmap = nullptr; cpuDcPixels = nullptr;
}

HDC CDXGISurface::AcquirePixelsDC()
{
    CorePerf::Scope timing(CorePerf::UiPixelsAcquire);
    if (cpuImage) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = LONG(dwSurfaceWidth);
        info.bmiHeader.biHeight = -LONG(dwSurfaceHeight);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        HDC dc = CreateCompatibleDC(nullptr);
        if (!dc) IfFailedThrowHR(E_OUTOFMEMORY);
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &cpuDcPixels, nullptr, 0);
        if (!bitmap) { DeleteDC(dc); IfFailedThrowHR(E_OUTOFMEMORY); }
        HGDIOBJ previous = SelectObject(dc, bitmap);
        if (!previous || previous == HGDI_ERROR) {
            DeleteObject(bitmap); DeleteDC(dc); cpuDcPixels = nullptr;
            IfFailedThrowHR(E_FAIL);
        }
        const auto& pixels = static_cast<const owned::SurfaceImage&>(*cpuImage).pixels;
        memcpy(cpuDcPixels, pixels.data(), pixels.size());
        ownedSurface.BeginWrite();
        cpuBitmap = bitmap; cpuPreviousBitmap = previous; hDc = dc;
        return hDc;
    }
    Microsoft::WRL::ComPtr<IDXGISurface1> surface;
    IfFailedThrowHR(texture.As(&surface));
    ownedSurface.BeginWrite();
    IfFailedThrowHR(surface->GetDC(FALSE,&hDc));
    return hDc;
}

void CDXGISurface::ReleasePixelsDC()
{
    CorePerf::Scope timing(CorePerf::UiPixelsRelease);
    if (cpuImage) {
        GdiFlush();
        try {
            // SurfacePixels detaches shared snapshots before the write.
            memcpy(cpuImage->pixels.data(), cpuDcPixels, cpuImage->pixels.size());
        } catch (...) { CloseCpuDC(); throw; }
        CloseCpuDC();
        IfFailedThrowHR(PublishSurface() ? S_OK : E_FAIL);
        return;
    }
    Microsoft::WRL::ComPtr<IDXGISurface1> surface;
    IfFailedThrowHR(texture.As(&surface));
    IfFailedThrowHR(surface->ReleaseDC(nullptr));
    hDc=nullptr;
    IfFailedThrowHR(ownedSurface.Complete(texture.Get())?S_OK:E_FAIL);
}

HDC CDXGISurface::PromoteUiLayer(void* context,const std::uint8_t* recipe,std::size_t size) noexcept
{
    auto& surface=*static_cast<CDXGISurface*>(context);
    try{
        const HDC target=surface.AcquirePixelsDC();
        if(owned::ui_layer_replay(recipe,size,target))return target;
        surface.ReleasePixelsDC();
    }catch(...){
        GameJournal::Event("render","ui_text_promotion_failed","surface=%llu",surface.OwnedSurfaceId());
    }
    return nullptr;
}

void CDXGISurface::RecordedSurfaceReset(const RECT& rect) noexcept
{
    if(uiRecordingDc && rect.left<=0 && rect.top<=0 &&
       rect.right>=LONG(dwSurfaceWidth) && rect.bottom>=LONG(dwSurfaceHeight))uiRecordingReset=true;
}

BOOL CDXGISurface::AcquireDC(HDC *pDC)
{
    FrameDiagnostics::Scope timing(FrameDiagnostics::GetDC);
    if(!pDC || hDc || uiRecordingDc)return FALSE;
    bool recordText=false;
    IfFailedThrowHR(pDX11->GetRendererFacade().Visit([&](owned::Renderer& renderer) {
        // Pane Draw precedes Draw2D: rasterize at its final composition scale.
        const auto previous = owned::widescreen::hud_blit;
        owned::widescreen::HudComposition pane;
        if (owned::owned_text_pane_surface(OwnedSurfaceId()) && owned::embedded_hud_layout().surface_composition(owned::HudPanel::character_info,0,pane))
            owned::widescreen::hud_blit = pane;
        ownedTextDc=owned::owned_text_claim_dc(OwnedSurfaceId(),GetWidth(),GetHeight(),renderer.owned_text_raster_scale());
        owned::widescreen::hud_blit = previous;
        recordText=renderer.owned_ui_text_enabled();return S_OK;
    }));
    if(recordText || ownedTextDc){
        uiRecordingReset=false;
        uiRecordingDc=owned::ui_layer_begin(this,&PromoteUiLayer);
        if(!uiRecordingDc){
            GameJournal::Event("render","ui_text_recording_unavailable","surface=%llu",OwnedSurfaceId());
            IfFailedThrowHR(E_FAIL);
        }
        *pDC=uiRecordingDc;
    }else *pDC=AcquirePixelsDC();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CDXGISurface::ReleaseDC( HDC hDC )
BOOL CDXGISurface::ReleaseDC(HDC hDC)
{
    FrameDiagnostics::Scope timing(FrameDiagnostics::ReleaseDC);
    if(!uiRecordingDc){
        if(!hDc || hDC!=hDc)return FALSE;
        ReleasePixelsDC();return TRUE;
    }
    if(hDC!=uiRecordingDc)return FALSE;
    const auto completed=owned::ui_layer_end(uiRecordingDc,uiRecipe);
    uiRecordingDc=nullptr;
    if(ownedTextDc){
        bool success=!completed.promoted && completed.failure==owned::UiLayerFailure::none;
        // Typed text does not write pixels. Publish the real backing surface's
        // identity too, including after screen/resource recreation, so the blit
        // can reach owned_text_consume with a nonzero current generation.
        if(success && !OwnedRender::E1SurfaceValue(OwnedSurfaceId())) {
            success=PublishSurface();
            if(!success) {
                static bool reported=false;
                if(!reported) {
                    reported=true;
                    GameJournal::Event("RENDER","owned_text_surface_unavailable",
                        "surface=%llu reason=surface_publication",OwnedSurfaceId());
                }
            }
        }
        owned::owned_text_finish_dc(OwnedSurfaceId(),OwnedSurfaceGeneration(),success);
        if(completed.promoted)ReleasePixelsDC();
        ownedTextDc=false;uiRecipe.clear();uiLayer={};
        return TRUE;
    }
    if(completed.promoted){
        ReleasePixelsDC();uiLayer={};
    }else if(completed.failure==owned::UiLayerFailure::none){
        if(owned::ui_layer_unchanged(uiLayer,uiRecipe,OwnedSurfaceGeneration()))return TRUE;
        const HDC target=AcquirePixelsDC();
        const bool replayed=owned::ui_layer_replay(uiRecipe.data(),uiRecipe.size(),target);
        ReleasePixelsDC();
        IfFailedThrowHR(replayed?S_OK:E_FAIL);
        uiLayer.recipe.swap(uiRecipe);
        uiLayer.generation=OwnedSurfaceGeneration();
        // A full clear makes this recipe independent of previous surface pixels.
        // Incremental and promoted drawing is never treated as a fixed point.
        uiLayer.stable=uiRecordingReset;
    }
    if(completed.failure!=owned::UiLayerFailure::none){
        uiLayer={};
        GameJournal::Event("render","ui_text_recording_failed","surface=%llu reason=%s",
            OwnedSurfaceId(),owned::ui_layer_failure_name(completed.failure));
        IfFailedThrowHR(E_FAIL);
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CDXGISurface::GetSurfaceDesc( LPDDSURFACEDESC2 pDDSD )
// get surface description
BOOL CDXGISurface::GetSurfaceDesc(D3D11_TEXTURE2D_DESC *ptex2d)
{
    if (!ptex2d) return FALSE;
    if (cpuImage) { *ptex2d = cpuDesc; return TRUE; }
    if (!texture) return FALSE;
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
    if (!pDestRect) return E_INVALIDARG;
    DDCOLORKEY colorKey{};
    const DDCOLORKEY* key = nullptr;
    if (dwFlags & DDBLT_KEYSRCOVERRIDE) {
        if (pDDBltFx) colorKey = pDDBltFx->ddckSrcColorkey;
        key = &colorKey;
    }
    const HRESULT result = pDX11->BlitPublishedSurface(OwnedSurfaceId(), *pDestRect, pSrcRect, key);
    if (result == E_HANDLE) {
        // A missing/stale publication is transient; do not abort the UI frame.
        static bool reportedUnavailable = false;
        if (!reportedUnavailable) {
            reportedUnavailable = true;
            GameJournal::Event("RENDER", "surface_blit_skipped",
                "surface=%llu reason=publication_unavailable",
                static_cast<unsigned long long>(OwnedSurfaceId()));
        }
        return S_OK;
    }
    return result;
}
HRESULT CDXGISurface::BlitPublishedToFacade(OwnedRender::E1RendererFacade& facade,
    const RECT& destination, const RECT* source, const DDCOLORKEY* colorKey,
    bool interfaceSpace, bool nativeCanvas)
{
    if (!pDX11 || (!texture && !cpuImage) || !facade.FrameActive()) return E_UNEXPECTED;
    if (cpuImage) return OwnedRender::E1BlitPublishedSurface(facade, nullptr, nullptr,
        OwnedSurfaceId(), destination, source, colorKey, interfaceSpace, nativeCanvas);
    auto device = pDX11->GetDevice();
    auto context = pDX11->GetDeviceContext();
    if (!device || !context) return E_UNEXPECTED;
    return OwnedRender::E1BlitPublishedSurface(facade, device.Get(), context.Get(),
        OwnedSurfaceId(), destination, source, colorKey, interfaceSpace, nativeCanvas);
}
HRESULT CDXGISurface::BltFromRenderSurface(LPRECT pDestRect, LPRECT pSrcRect, DWORD dwFlags,
                                           LPDDBLTFX pDDBltFx, bool composedOutput)
{
    auto& facade = pDX11->GetRendererFacade();
    if (composedOutput) {
        const HRESULT ready = facade.Visit([](owned::Renderer& renderer) -> HRESULT {
            HRESULT hr = renderer.composite_world();
            if (SUCCEEDED(hr)) hr = renderer.flush_native();
            if (SUCCEEDED(hr)) hr = renderer.flush_ui();
            return hr;
        });
        if (FAILED(ready)) return ready;
    }
    if (cpuImage) {
        if (hDc || uiRecordingDc) return DXGI_ERROR_INVALID_CALL;
        OwnedRender::E1SurfaceCpuImage image;
        bool mask = false;
        const HRESULT captured = facade.Visit([&](owned::Renderer& renderer) {
            mask = renderer.embedded_hit_test_frame();
            return OwnedRender::E1CaptureNativeTarget(renderer, dwSurfaceWidth, dwSurfaceHeight, image);
        });
        // The caller tests FAILED before reading pixels. An unavailable output
        // must invalidate this capture attempt rather than expose an older frame.
        if (captured != S_OK) return FAILED(captured) ? captured : E_PENDING;
        ownedSurface.BeginWrite();
        cpuImage->pixels = std::move(image.pixels);
        if (!PublishSurface()) return E_FAIL;
        captureUsedBoundTarget = false;
        captureDifferentFromBufferZero = mask;
        return S_OK;
    }
    struct Capture { CDXGISurface* surface; bool composed; bool mask = false; } capture{this, composedOutput};
    const auto copy = [](void* user, ID3D11DeviceContext* context, ID3D11Texture2D* backbuffer) -> HRESULT {
            auto& request = *static_cast<Capture*>(user);
            auto& self = *request.surface;
            self.captureUsedBoundTarget = self.captureDifferentFromBufferZero = false;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> source;
            if (request.composed || request.mask) {
                source = backbuffer; // explicit lossless mask, or the composed output
                self.captureDifferentFromBufferZero = request.mask;
            } else {
                const HRESULT selected = OwnedRender::E1CaptureSource(context, backbuffer, source,
                    self.captureUsedBoundTarget, self.captureDifferentFromBufferZero);
                if (FAILED(selected)) return selected;
            }
            D3D11_TEXTURE2D_DESC sourceDesc = {}, destinationDesc = {};
            source->GetDesc(&sourceDesc);
            self.texture->GetDesc(&destinationDesc);
            if (sourceDesc.Width == destinationDesc.Width &&
                sourceDesc.Height == destinationDesc.Height &&
                sourceDesc.Format == destinationDesc.Format &&
                sourceDesc.SampleDesc.Count == destinationDesc.SampleDesc.Count &&
                sourceDesc.SampleDesc.Quality == destinationDesc.SampleDesc.Quality &&
                sourceDesc.MipLevels == destinationDesc.MipLevels &&
                sourceDesc.ArraySize == destinationDesc.ArraySize) {
                self.ownedSurface.BeginWrite();
                context->CopyResource(self.texture.Get(), source.Get());
            } else {
                OwnedRender::E1SurfaceCpuImage image;
                const HRESULT captured = OwnedRender::E1CaptureTarget(context, source.Get(),
                    destinationDesc.Width, destinationDesc.Height, destinationDesc.Format, image);
                if (FAILED(captured)) return captured;
                self.ownedSurface.BeginWrite();
                context->UpdateSubresource(self.texture.Get(), 0, nullptr,
                    image.pixels.data(), static_cast<UINT>(image.pitch), 0);
            }
            self.ownedSurface.Complete(self.texture.Get());
            return S_OK;
        };
    return facade.Visit([&](owned::Renderer& renderer) {
        capture.mask = renderer.embedded_hit_test_frame();
        return renderer.visit_embedded_output(&capture, copy);
    });
}
void CDXGISurface::UpdateTexture(UINT subresource_index, D3D11_BOX *region,
                                 D3D11_SUBRESOURCE_DATA *srd, DWORD sourceMmpFormat)
{
    if (cpuImage) {
        // SurfaceImage uploads level zero; the author's native uploader owns
        // GPU mip generation. Source mip uploads do not replace level zero.
        if (subresource_index != 0) return;
        const D3D11_BOX box = region ? *region :
            D3D11_BOX{0, 0, 0, dwSurfaceWidth, dwSurfaceHeight, 1};
        if (hDc || !srd || !srd->pSysMem || box.front != 0 || box.back != 1 ||
            box.left >= box.right || box.top >= box.bottom ||
            box.right > dwSurfaceWidth || box.bottom > dwSurfaceHeight)
            IfFailedThrowHR(E_INVALIDARG);
        const UINT width = box.right - box.left;
        if (cpuUploadFormat == DXGI_FORMAT_BC1_UNORM ||
            cpuUploadFormat == DXGI_FORMAT_BC2_UNORM ||
            cpuUploadFormat == DXGI_FORMAT_BC3_UNORM) {
            // Keep the source BC format separate from canonical CPU/GDI storage.
            // The native executor uploads the completed BGRA image and owns mips.
            if (region) IfFailedThrowHR(E_INVALIDARG);
            const size_t blockBytes = cpuUploadFormat == DXGI_FORMAT_BC1_UNORM ? 8 : 16;
            const size_t pitch = size_t((width + 3) / 4) * blockBytes;
            if (srd->SysMemPitch < pitch) IfFailedThrowHR(E_INVALIDARG);
            DirectX::Image compressed{};
            compressed.width = width; compressed.height = box.bottom - box.top;
            compressed.format = cpuUploadFormat;
            compressed.rowPitch = srd->SysMemPitch;
            compressed.slicePitch = compressed.rowPitch * ((compressed.height + 3) / 4);
            compressed.pixels = const_cast<BYTE*>(static_cast<const BYTE*>(srd->pSysMem));
            DirectX::ScratchImage decoded;
            IfFailedThrowHR(DirectX::Decompress(compressed, DXGI_FORMAT_B8G8R8A8_UNORM, decoded));
            const auto* image = decoded.GetImage(0, 0, 0);
            if (!image) IfFailedThrowHR(E_FAIL);
            auto pixels = cpuImage->pixels;
            for (UINT y = 0; y < compressed.height; ++y)
                memcpy(pixels.data() + size_t(y) * cpuImage->pitch,
                    image->pixels + size_t(y) * image->rowPitch, size_t(width) * 4);
            ownedSurface.BeginWrite();
            cpuImage->pixels = std::move(pixels);
            IfFailedThrowHR(PublishSurface() ? S_OK : E_FAIL);
            return;
        }
        SMMPFileHeader format;
        format.InitializeDefaultFormat(sourceMmpFormat);
        if (format.dwBitDepth != 16 && format.dwBitDepth != 32)
            IfFailedThrowHR(E_INVALIDARG);
        const size_t rowBytes = size_t(width) * (format.dwBitDepth / 8);
        const size_t sourcePitch = srd->SysMemPitch ? srd->SysMemPitch : rowBytes;
        if (sourcePitch < rowBytes) IfFailedThrowHR(E_INVALIDARG);
        // Convert into a detached copy before invalidating the completed image.
        auto pixels = cpuImage->pixels;
        std::vector<DWORD> converted;
        for (UINT row = 0; row < box.bottom - box.top; ++row) {
            if (!MmpRgbToBgra::Convert(format,
                static_cast<const BYTE*>(srd->pSysMem) + size_t(row) * sourcePitch, width, converted))
                IfFailedThrowHR(E_INVALIDARG);
            if (opaquePadding) for (DWORD& pixel : converted) pixel |= 0xff000000u;
            memcpy(pixels.data() + size_t(row + box.top) * cpuImage->pitch + size_t(box.left) * 4,
                converted.data(), size_t(width) * sizeof(DWORD));
        }
        ownedSurface.BeginWrite();
        cpuImage->pixels = std::move(pixels);
        IfFailedThrowHR(PublishSurface() ? S_OK : E_FAIL);
        return;
    }
    ownedSurface.BeginWrite();
    D3D11_TEXTURE2D_DESC destination = {};
    texture->GetDesc(&destination);
    if (destination.Format == DXGI_FORMAT_B8G8R8A8_UNORM &&
        (sourceMmpFormat != MMP_FORMAT_8888 || opaquePadding)) {
        SMMPFileHeader format;
        format.InitializeDefaultFormat(sourceMmpFormat);
        const UINT left = region ? region->left : 0;
        const UINT top = region ? region->top : 0;
        const UINT width = region ? region->right - region->left : destination.Width;
        const UINT height = region ? region->bottom - region->top : destination.Height;
        if (srd && srd->pSysMem && width && height) {
            const size_t sourceBytesPerPixel = format.dwBitDepth / 8;
            const size_t sourcePitch = srd->SysMemPitch ? srd->SysMemPitch : width * sourceBytesPerPixel;
            std::vector<DWORD> converted(size_t(width) * height);
            std::vector<DWORD> convertedRow;
            for (UINT row = 0; row < height; ++row) {
                if (!MmpRgbToBgra::Convert(format,
                    static_cast<const BYTE*>(srd->pSysMem) + size_t(row) * sourcePitch,
                    width, convertedRow)) {
                    ownedSurface.Complete(texture.Get());
                    return;
                }
                if (opaquePadding)
                    for (DWORD& pixel : convertedRow) pixel |= 0xff000000u;
                memcpy(converted.data() + size_t(row) * width, convertedRow.data(), width * sizeof(DWORD));
            }
            D3D11_SUBRESOURCE_DATA normalized = *srd;
            normalized.pSysMem = converted.data();
            normalized.SysMemPitch = width * sizeof(DWORD);
            D3D11_BOX normalizedRegion = {left, top, 0, left + width, top + height, 1};
            pDX11->UpdateTexture(texture.Get(), subresource_index, &normalizedRegion, &normalized);
        }
    } else {
        pDX11->UpdateTexture(this->texture.Get(), subresource_index, region, srd);
    }
    ownedSurface.Complete(texture.Get());
}
/////////////////////////////////////////////////////////////////////////////////////
HRESULT CDXGISurface::Clear(DWORD dwColorFill)
{
    if (cpuImage) {
        if (!hDc) return E_UNEXPECTED;
        RECT rect = {0, 0, LONG(dwSurfaceWidth), LONG(dwSurfaceHeight)};
        HBRUSH brush = CreateSolidBrush(dwColorFill);
        if (!brush) return E_OUTOFMEMORY;
        const bool cleared = FillRect(hDc, &rect, brush) != 0;
        DeleteObject(brush);
        return cleared ? S_OK : E_FAIL;
    }
#ifdef DXGI_ENABLED
    ASSERT(hDc);
    // dwColorFill = 0x000000FF;
    D3D11_TEXTURE2D_DESC tex2d;
    GetSurfaceDesc(&tex2d);
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
HRESULT CDXGISurface::SaveThumbnailFile(LPCSTR path, UINT rows) noexcept
{
    if (!path || !rows || rows > dwSurfaceHeight || !dwSurfaceWidth) return E_INVALIDARG;
    std::string temporary;
    try {
        const size_t count = size_t(dwSurfaceWidth) * dwSurfaceHeight;
        std::vector<DWORD> pixels(count);
        const HRESULT captured = CopyPixelBuffer(pixels.data(), pixels.size());
        if (FAILED(captured)) return captured;
        std::vector<WORD> packed(size_t(dwSurfaceWidth) * rows);
        for (size_t i = 0; i < packed.size(); ++i) {
            const DWORD pixel = pixels[i];
            packed[i] = WORD(((pixel >> 19) & 31u) << 11 |
                             ((pixel >> 10) & 63u) << 5 | ((pixel >> 3) & 31u));
        }
        // Prepare the complete image before replacing the previous thumbnail.
        temporary = std::string(path) + ".thumbnail-" + std::to_string(GetCurrentProcessId()) + ".tmp";
        {
            CFile file; CFileException error;
            if (!file.Open(temporary.c_str(), CFile::modeCreate | CFile::modeWrite, &error))
                return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
            file.Write(packed.data(), UINT(packed.size() * sizeof(WORD)));
            file.Close();
        }
        if (!MoveFileExA(temporary.c_str(), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const DWORD error = GetLastError();
            DeleteFileA(temporary.c_str());
            return HRESULT_FROM_WIN32(error);
        }
        return S_OK;
    } catch (CException* error) {
        error->Delete();
        if (!temporary.empty()) DeleteFileA(temporary.c_str());
        return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
    } catch (...) {
        if (!temporary.empty()) DeleteFileA(temporary.c_str());
        return E_OUTOFMEMORY;
    }
}

HRESULT CDXGISurface::LoadThumbnailFile(LPCSTR path, UINT rows) noexcept
{
    if (!path || !rows || rows > dwSurfaceHeight || !dwSurfaceWidth) return E_INVALIDARG;
    try {
        CFile file; CFileException error;
        if (!file.Open(path, CFile::modeRead, &error))
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        const ULONGLONG bytes = file.GetLength();
        const size_t row16 = size_t(dwSurfaceWidth) * sizeof(WORD);
        const size_t row32 = size_t(dwSurfaceWidth) * sizeof(DWORD);
        const bool rgb565 = bytes == row16 * rows;
        // Read raw BGRA dumps produced by the broken writer too. Their row
        // padding is inferred from the complete file, never treated as pixels.
        if (!rgb565 && (bytes < row32 * rows || bytes % rows || bytes > row32 * rows * 4u))
            return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
        const size_t pitch = rgb565 ? row16 : size_t(bytes / rows);
        std::vector<BYTE> source(size_t(bytes), BYTE{});
        if (file.Read(source.data(), UINT(source.size())) != source.size())
            return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
        file.Close();
        std::vector<DWORD> pixels(size_t(dwSurfaceWidth) * rows);
        if (rgb565) {
            SMMPFileHeader format; format.InitializeDefaultFormat(MMP_FORMAT_5650);
            std::vector<DWORD> row;
            for (UINT y = 0; y < rows; ++y) {
                if (!MmpRgbToBgra::Convert(format, source.data() + size_t(y) * pitch, dwSurfaceWidth, row))
                    return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
                memcpy(pixels.data() + size_t(y) * dwSurfaceWidth, row.data(), row32);
            }
        } else {
            for (UINT y = 0; y < rows; ++y)
                memcpy(pixels.data() + size_t(y) * dwSurfaceWidth, source.data() + size_t(y) * pitch, row32);
            // Save pictures are opaque, including old BGRX dumps.
            for (DWORD& pixel : pixels) pixel |= 0xff000000u;
        }
        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = pixels.data(); data.SysMemPitch = UINT(row32);
        D3D11_BOX region{0, 0, 0, dwSurfaceWidth, rows, 1};
        UpdateTexture(0, &region, &data, MMP_FORMAT_8888);
        return S_OK;
    } catch (CException* error) { error->Delete(); return HRESULT_FROM_WIN32(ERROR_READ_FAULT); }
      catch (...) { return E_OUTOFMEMORY; }
}
