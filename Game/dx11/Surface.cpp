#include "stdafx.h"
#include "../GameJournal.h"
#include "surface.h"
#include <io.h>
#include "RendererDX11.h"
#include "../FrameDiagnostics.h"
#include "PostProcess.h"
#include "RetailModalFade.h"
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include "../OwnedRender/E1SurfaceFacade.h"
#include "MmpRgbToBgra.h"

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
    pDX11->CreateTexture(&normalized, texture);
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
    ownedSurface.BeginWrite();
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
    else if (pHdr->dwFormat != MMP_FORMAT_8888 && pHdr->dwBitDepth != 16 &&
             !(pHdr->dwBitDepth == 32 && pHdr->dwABits == 0))
        return ReportError("Can't load picture from unsupported RGB format.\n");

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
    ownedSurface.BeginWrite();
    pDX11->GetDeviceContext()->CopyResource(this->GetTexture().Get(), src->GetTexture().Get());
    ownedSurface.Complete(texture.Get());
}

void CDXGISurface::CopySubresourceRegion(UINT dstSubresource, UINT dstX, UINT dstY, UINT dstZ,
                                         CDXGISurface *src, UINT srcSubresource, D3D11_BOX *pBox)
{
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
    ownedSurface.BeginWrite();
    const HRESULT hr = ApplyOwnedSurfaceEffect(pDX11->GetRendererFacade(),
        textureView.Get(), texture.Get(), nullptr, true);
    if (FAILED(hr)) throw hr;
    ownedSurface.Complete(texture.Get());
}
void CDXGISurface::StretchTo(const CDXGISurface *src, const RECT &rect)
{
    // The historical argument named src is the actual destination.
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
    if(uiRecordingDc)owned::ui_layer_abort(uiRecordingDc);
    if(hDc && texture){
        Microsoft::WRL::ComPtr<IDXGISurface1> surface;
        if(SUCCEEDED(texture.As(&surface)))surface->ReleaseDC(nullptr);
    }
}

HDC CDXGISurface::AcquirePixelsDC()
{
    Microsoft::WRL::ComPtr<IDXGISurface1> surface;
    IfFailedThrowHR(texture.As(&surface));
    ownedSurface.BeginWrite();
    IfFailedThrowHR(surface->GetDC(FALSE,&hDc));
    return hDc;
}

void CDXGISurface::ReleasePixelsDC()
{
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
        recordText=renderer.owned_ui_text_enabled();return S_OK;
    }));
    if(recordText){
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
    if (!pDX11 || !texture || !facade.FrameActive()) return E_UNEXPECTED;
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
    struct Capture { CDXGISurface* surface; bool composed; } capture{this, composedOutput};
    return facade.VisitOutput(&capture,
        [](void* user, ID3D11DeviceContext* context, ID3D11Texture2D* backbuffer) -> HRESULT {
            auto& request = *static_cast<Capture*>(user);
            auto& self = *request.surface;
            self.captureUsedBoundTarget = self.captureDifferentFromBufferZero = false;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> source;
            if (request.composed) source = backbuffer;
            else {
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
        });
}
void CDXGISurface::UpdateTexture(UINT subresource_index, D3D11_BOX *region,
                                 D3D11_SUBRESOURCE_DATA *srd, DWORD sourceMmpFormat)
{
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
