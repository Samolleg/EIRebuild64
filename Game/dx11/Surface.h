#pragma once
#include "../OwnedRender/E1Surface.h"
#include "owned_ui_layers.h"
#include <d3d11.h>
#include <wrl/client.h>
#include "../mmpFormat.h"
#define DXGI_ENABLED

class CDX11;
namespace owned { class Renderer; struct SurfaceImage; }
namespace OwnedRender { template<class Backend> class RendererFacadeCore; }

class CDXGISurface
{
private:
    mutable OwnedRender::E1Surface ownedSurface;
    CDX11 *pDX11;
    DWORD dwSurfaceWidth;  // surface width
    DWORD dwSurfaceHeight; // surface height
    DWORD dwSurfaceBPP;    // surface bit depth
    DXGI_FORMAT tfFormat = DXGI_FORMAT::DXGI_FORMAT_UNKNOWN;
    bool opaquePadding = false;
    std::shared_ptr<owned::SurfaceImage> cpuImage;
    D3D11_TEXTURE2D_DESC cpuDesc{};
    DXGI_FORMAT cpuUploadFormat = DXGI_FORMAT_UNKNOWN;
    HBITMAP cpuBitmap = nullptr;
    HGDIOBJ cpuPreviousBitmap = nullptr;
    void* cpuDcPixels = nullptr;
    void CloseCpuDC() noexcept;
    bool PublishSurface(bool fullyOpaque = false,
        std::shared_ptr<const std::vector<std::uint8_t>> alphaCutMask = {}) const noexcept;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture = 0;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> textureView = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> pixelReadbackStaging;
    bool captureUsedBoundTarget = false;
    bool captureDifferentFromBufferZero = false;
    HDC hDc = nullptr;
    HDC uiRecordingDc = nullptr;
    bool ownedTextDc = false;
    bool uiRecordingReset = false;
    owned::UiLayerState uiLayer;
    std::vector<std::uint8_t> uiRecipe;
    HDC AcquirePixelsDC();
    void ReleasePixelsDC();
    static HDC PromoteUiLayer(void* context,const std::uint8_t* recipe,std::size_t size) noexcept;
    BOOL CreateTexture(D3D11_TEXTURE2D_DESC *ptex2d);
    BOOL ExtractSurfaceParams();

public:
    CDXGISurface(CDX11 *pDXDirectDraw, DWORD dwWidth, DWORD dwHeight);
    CDXGISurface(CDX11 *pDXDirectDraw, D3D11_TEXTURE2D_DESC *ptex2d);
    CDXGISurface(CDX11 *pDXDirectDraw, const SMMPFileHeader *pHdr, bool bInVideo = false);
    ~CDXGISurface();
    // texture surface functions: priority and LOD. NOTE: use it ONLY with automatic managed
    // TEXTURES wraps for surface description functions
    BOOL GetSurfaceDesc(D3D11_TEXTURE2D_DESC *pDDSD);
    BOOL GetTextureFormat(DXGI_FORMAT &pDDPixelFormat);
    // access to surface's width/height/bpp, previously extracted during creating
    std::uint64_t OwnedSurfaceId() const noexcept { return ownedSurface.Id(); }
    std::uint64_t OwnedSurfaceGeneration() const noexcept;
    bool EnsureOwnedSurfacePublished() const noexcept
    {
        return OwnedRender::E1SurfaceValue(ownedSurface.Id()) != nullptr ||
               PublishSurface();
    }
    // Complete a full CPU upload with its verified alpha evidence. Other writes
    // publish without that proof and remain conservative.
    bool PublishUploadedOpacity(bool fullyOpaque,
        std::shared_ptr<const std::vector<std::uint8_t>> alphaCutMask = {}) noexcept
    {
        return PublishSurface(fullyOpaque, std::move(alphaCutMask));
    }
    DWORD GetWidth() const { return dwSurfaceWidth; };
    DWORD GetHeight() const { return dwSurfaceHeight; };
    DWORD GetBPP() const { return dwSurfaceBPP; };
    // lock/unlock wraps
    BOOL AcquireDC(HDC* pDC);
    BOOL ReleaseDC(HDC hDC);
    void RecordedSurfaceReset(const RECT& rect) noexcept;
    // blit functions
    // general blit functions
    // special blit to and from primary/render surfaces
    HRESULT BltToRenderSurface(LPRECT pDestRect, LPRECT pSrcRect, DWORD dwFlags,
                               LPDDBLTFX pDDBltFx = 0);
    HRESULT BlitPublishedToFacade(OwnedRender::RendererFacadeCore<owned::Renderer>& facade,
        const RECT& destination, const RECT* source, const DDCOLORKEY* colorKey = nullptr,
        bool interfaceSpace = true, bool nativeCanvas = true);
    HRESULT BltFromRenderSurface(LPRECT pDestRect, LPRECT pSrcRect, DWORD dwFlags,
                                 LPDDBLTFX pDDBltFx = 0, bool composedOutput = false);
    void UpdateTexture(UINT subresource_index, D3D11_BOX *region, D3D11_SUBRESOURCE_DATA *srd,
                       DWORD sourceMmpFormat = MMP_FORMAT_8888);
    // surface clearing
    HRESULT Clear(DWORD dwColorFill);
    // load picture
    BOOL Load(LPCSTR pszFileName, bool bInVideo = false);
    BOOL Load(FILE *file, bool bInVideo = false);
    BOOL Load(const SMMPFileHeader *pHdr, bool bInVideo = false);
    void DefaultSurfaceDesc(D3D11_TEXTURE2D_DESC *ptex2d);
    HRESULT SaveThumbnailFile(LPCSTR path, UINT rows) noexcept;
    HRESULT LoadThumbnailFile(LPCSTR path, UINT rows) noexcept;
    // Assuming src is copy compatible without checks!
    void CopyResource(CDXGISurface *src);
    // void CopyResource(ID3D11Texture2D *src);
    void CopySubresourceRegion(UINT dstSubresource, UINT dstX, UINT dstY, UINT dstZ,
                               CDXGISurface *src, UINT srcSubresource, D3D11_BOX *pBox);
    void GetPixel(int nX, int nY);
    std::unique_ptr<DWORD[]> GetPixelBuffer();
    HRESULT CopyPixelBuffer(DWORD *destination, size_t pixelCount);
    bool CaptureUsedBoundTarget() const { return captureUsedBoundTarget; }
    bool CaptureDifferentFromBufferZero() const { return captureDifferentFromBufferZero; }
    void Fade();
    void StretchTo(const CDXGISurface *src, const RECT& rect);
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> GetShaderResource() const
    {
        return textureView;
    };
    Microsoft::WRL::ComPtr<ID3D11Texture2D> GetTexture() const { return texture; };
};