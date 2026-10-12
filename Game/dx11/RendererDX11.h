#pragma once
#include <CommonStates.h>
#include <dxgidebug.h>

#include "vertexbuffer.h"
#include "SpriteBatch.h"
#include "OwnedRendererClient.h"
#include "../OwnedRender/E1RendererFacade.h"
namespace OwnedRender { struct E1VisualInputs; }
#include "SpriteFont.h"
#include "FakeDX7.h"
#include "Caches.h"
#include "DirectXHelpers.h"

namespace owned { struct NativeGeometryView; struct ScreenRectangleRequest; }
#include "../OwnedRender/E2FrameTiming.h"
#include <camp_inventory.h>
#include <WICTextureLoader.h>
#include <wrl/client.h>
#include <d3d11.h>
#include <array>

// #define DRAWABLE_BACKBUFFER
namespace owned { struct NativeOwnedFixedState; }
namespace owned { class Renderer; }
namespace OwnedRender { template<class Backend> class RendererFacadeCore; }
class UIColorKeyBlit;
class CDXGISurface;
class CConstantBuffers;
class CShadersMain;
class FakeD3D7State;
class CEffectProcessor;
class CBatchers;

class CRenderState
{
    uint32_t alphaRef;   
};

class CDX11
{
private:
    bool itemColorSum = false; // Per-draw item state, never a configuration option.
    std::shared_ptr<UIColorKeyBlit> uiColorKey;
    DWORD m_width, m_height;
    DirectX::XMFLOAT4 background_color;
    BOOL m_bMakeScreenShot = 0;

    std::unique_ptr<CConstantBuffers> m_cbs = std::make_unique<CConstantBuffers>();
    std::unique_ptr<CShadersMain> m_shaders = std::make_unique<CShadersMain>();   
    std::unique_ptr<CEffectProcessor> m_effproc = std::make_unique<CEffectProcessor>(this);    

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext;


    Microsoft::WRL::ComPtr<ID3D11Texture2D> depthStencilBuffer;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthStencilState;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthStencilStateDisableWrite;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthStencilStateDisable;

    Microsoft::WRL::ComPtr<ID3D11BlendState> blendState;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blendStateDisable;

    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerStateNoCull;

    Microsoft::WRL::ComPtr<ID3D11SamplerState> samplerState;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> blackTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> blackTextureView;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> myTexture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> myTexture2;
    std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 2> boundFigureTextures;

    std::shared_ptr<OwnedRender::RendererFacadeCore<owned::Renderer>> rendererFacade;
    OwnedRender::E2FrameTiming frameTiming;
    std::uint64_t frameSerial = 0;
    std::uint64_t c56WorldFigurePrimitives = 0;
    std::uint64_t c56WorldFigureFallbacks = 0;
    std::uint64_t c56WorldFigureLegacyDraws = 0;
    std::uint64_t c56WorldFigureUnavailable = 0;
    std::uint64_t c56WorldFigureInvalidView = 0;
    std::uint64_t c56WorldFigureMultipleTextures = 0;
    std::uint64_t c56WorldFigureQueueFailed = 0;
    OwnedRendererClient ownedRenderer;
    std::uint64_t boundParticleSurface = 0;
    bool ownedWorldActive = false;
    bool campPopupActive = false, campPopupFailed = false;
    std::array<float, 16> ownedWorldToClip{};
    bool c56WorldPassRan = false;
    int c56WorldPassState = -1;
    std::unique_ptr<OwnedSpriteBatch> spriteBatch;
    std::unique_ptr<DirectX::SpriteFont> spriteFont;
    std::unique_ptr<DirectX::CommonStates> m_states;
    std::unique_ptr<CVertexCaches> m_caches;
    std::unique_ptr<CBatchers> m_batchers;
    FakeD3D7State fake_dx7, fake_dx7_prev;

    void MakeScreenShot();
    void RefreshOwnedTargets();
    bool InitializeDX(HWND hWnd, DWORD dwWidth, DWORD dwHeight);
    bool ReportLiveObjects();

public:
    enum class OwnedTerrainPassResult { unavailable, submitted, failed };
    bool Initialize(HWND hWnd, DWORD dwWidth, DWORD dwHeight);
    ~CDX11();






    void BeginFrame();
    bool BeginWorld(const EIRenderWorldFrameV2& frame);
    void EndWorld();
    void RunC56WorldPass(const OwnedRender::E1VisualInputs* visuals = nullptr);
    void SubmitC56PointLights(const std::vector<owned::RetainedWorldPointLight>& lights,
                              double worldTime);
    bool IsOwnedWorldActive() const { return ownedWorldActive; }
    void EndFrame(bool present = true);
    void ClearColorBuffer(DWORD value);
    void ClearZBuffer(DWORD value);
    owned::FixedState CaptureC56MaterialState();
    bool SubmitC56Triangles(const owned::NativeGeometryView& view);
    bool SubmitC56ScreenRectangle(const owned::ScreenRectangleRequest& request);
    bool SubmitC56Figure(const owned::NativeGeometryView& view);
    bool SubmitC56GameParticles(const owned::GameParticleView& view);
    bool SubmitC56CampFigure(const owned::NativeGeometryView& view);
    bool SubmitC56CampPopup(owned::CampRenderRequest,owned::CampPopupDraw,void*);
    void RestoreHostBindings();
    // A loading picture (wait screen, movie frame): the renderer keeps it on
    // screen while the next world prepares, as retail shows the load screen.
    void MarkLoadPicture();
    void FlushInterfaceBeforeLegacyDraw();
    // Presents a full-screen BGRX image (movie or wait-screen frame) through
    // the renderer, fitted to the output and kept as the loading picture.
    HRESULT PresentLoadingImage(const std::vector<unsigned char>& bgrx, UINT width, UINT height,
                                std::uint64_t id, std::uint64_t generation);
    bool SubmitOwnedTerrain(EIRenderTerrainFrameV5 packet);
    OwnedTerrainPassResult SubmitC56TerrainPass(bool transparent);
    owned::NativeOwnedFixedState CaptureOwnedTerrainMaterial();

    int TriggerRedraw();
    template <typename TVertexType>
    int DrawIndexedPrimitive(D3D11_PRIMITIVE_TOPOLOGY primitive,
 VertexBuffer<TVertexType> *vbVertices,
                             LPWORD pwIndices, DWORD dwIndexCount);
    template <typename TVertexType>
    int DrawPrimitive(D3D11_PRIMITIVE_TOPOLOGY primitive, VertexBuffer<TVertexType> *vbVertices);

    BOOL BindTexture(DWORD dwIndex, ID3D11ShaderResourceView *pTexture,
                     std::uint64_t surfaceId = 0);
    ID3D11ShaderResourceView *GetBoundFigureTexture(DWORD dwIndex) const
    {
        return dwIndex < static_cast<DWORD>(boundFigureTextures.size()) ? boundFigureTextures[dwIndex].Get() : nullptr;
    }
    void SetAlphaRef(float value);
    void SetBackgroundColor(DWORD value);
    void SetFogColor(DWORD value);
    void ScreenShot();
    bool RenderStateCheck();
    bool SetRenderStates();
    bool SetItemColorSum(bool active);
    void CreateShaderResource(Microsoft::WRL::ComPtr<ID3D11Texture2D> texture,
                              Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> &textureView);
    void CreateTexture(D3D11_TEXTURE2D_DESC *ptex2d,
                       Microsoft::WRL::ComPtr<ID3D11Texture2D> &texture);
    void UpdateTexture(ID3D11Texture2D *texture, UINT subresource_index, D3D11_BOX *region,
                       D3D11_SUBRESOURCE_DATA *srd);
    void GetCurrentMode(DWORD &widthOut, DWORD &heightOut)
    {
        widthOut = m_width;
        heightOut = m_height;
    };
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> GetDeviceContext() { return deviceContext; };
    Microsoft::WRL::ComPtr<ID3D11Device> GetDevice() { return device; };
    OwnedSpriteBatch *GetSpriteBatch() { return spriteBatch.get(); };
    OwnedRendererClient &GetOwnedRenderer() { return ownedRenderer; };
    auto &GetRendererFacade() { return *rendererFacade; };
    HRESULT BlitPublishedSurface(std::uint64_t surfaceId, const RECT& destination,
                                 const RECT* source, const DDCOLORKEY* colorKey);
    DirectX::CommonStates *GetCommonStates() { return m_states.get(); };
    CConstantBuffers *GetConstantBuffers() { return m_cbs.get(); };
    CEffectProcessor *GetEffectProcessor() { return m_effproc.get(); }
    CShadersMain *GetShaders() { return m_shaders.get(); };
    CBatchers *GetBatchers() { return m_batchers.get(); };

    void SetFakeRenderState(int key, DWORD value) { fake_dx7.SetRenderState(key, value); };
    void GetFakeRenderState(int key, DWORD *value) { fake_dx7.GetRenderState(key, value); };
    void DefaultSurfaceDesc(D3D11_TEXTURE2D_DESC *tex2d);    
};

const int N_QUAD_VERTICES = 4;
