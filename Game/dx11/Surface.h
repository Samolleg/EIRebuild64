#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include "../mmpFormat.h"
#define DXGI_ENABLED

class CDX11;

class CDXGISurface
{
private:
    CDX11 *pDX11;
    DWORD dwSurfaceWidth;  // surface width
    DWORD dwSurfaceHeight; // surface height
    DWORD dwSurfaceBPP;    // surface bit depth
    DXGI_FORMAT tfFormat = DXGI_FORMAT::DXGI_FORMAT_UNKNOWN;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture = 0;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> textureView = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> pixelReadbackStaging;
    bool captureUsedBoundTarget = false;
    bool captureDifferentFromBufferZero = false;
    HDC hDc;
    BOOL CreateTexture(D3D11_TEXTURE2D_DESC *ptex2d);
    BOOL ExtractSurfaceParams();

public:
    CDXGISurface(CDX11 *pDXDirectDraw, DWORD dwWidth, DWORD dwHeight);
    CDXGISurface(CDX11 *pDXDirectDraw, D3D11_TEXTURE2D_DESC *ptex2d);
    CDXGISurface(CDX11 *pDXDirectDraw, const SMMPFileHeader *pHdr, bool bInVideo = false);
    // texture surface functions: priority and LOD. NOTE: use it ONLY with automatic managed
    // TEXTURES wraps for surface description functions
    BOOL GetSurfaceDesc(D3D11_TEXTURE2D_DESC *pDDSD);
    BOOL GetTextureFormat(DXGI_FORMAT &pDDPixelFormat);
    // access to surface's width/height/bpp, previously extracted during creating
    DWORD GetWidth() const { return dwSurfaceWidth; };
    DWORD GetHeight() const { return dwSurfaceHeight; };
    DWORD GetBPP() const { return dwSurfaceBPP; };
    // lock/unlock wraps
    BOOL AcquireDC(HDC* pDC);
    BOOL ReleaseDC(HDC hDC);
    // blit functions
    // general blit functions
    // special blit to and from primary/render surfaces
    HRESULT BltToRenderSurface(LPRECT pDestRect, LPRECT pSrcRect, DWORD dwFlags,
                               LPDDBLTFX pDDBltFx = 0);
    HRESULT BltFromRenderSurface(LPRECT pDestRect, LPRECT pSrcRect, DWORD dwFlags,
                                 LPDDBLTFX pDDBltFx = 0);
    void UpdateTexture(UINT subresource_index, D3D11_BOX *region, D3D11_SUBRESOURCE_DATA *srd);
    // surface clearing
    HRESULT Clear(DWORD dwColorFill);
    // load picture
    BOOL Load(LPCSTR pszFileName, bool bInVideo = false);
    BOOL Load(FILE *file, bool bInVideo = false);
    BOOL Load(const SMMPFileHeader *pHdr, bool bInVideo = false);
    void DefaultSurfaceDesc(D3D11_TEXTURE2D_DESC *ptex2d);
    void DumpToFile(LPCSTR path, UINT rows);
    void LoadFromFileDump(LPCSTR path, UINT linePitch, UINT rows);
    void LoadFromByteDump(void *src, UINT linePitch, UINT rows);
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