#pragma once
namespace owned {struct EmbeddedArenaInput;struct EmbeddedGolemInput;}
namespace owned {struct HudPublishedVisual; struct EmbeddedHudVisualInput; struct EmbeddedHudPortraitInput;}
#include <CommonStates.h>
#include <dxgidebug.h>

#include "SpriteBatch.h"
#include "OwnedRendererClient.h"
#include "../OwnedRender/E1RendererFacade.h"
namespace OwnedRender { struct E1VisualInputs; }
#include "FakeDX7.h"
#include "../MoviePlayback.h"
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
class FakeD3D7State;

class CDX11 : private Video::MovieRenderer
{
private:
    const void* movieOwner = nullptr;
    std::uint64_t movieGeneration = 0;
    bool SubmitMovie(const void*, std::shared_ptr<const Video::Frame>, const Video::Presentation&) override;
    void ClearMovie(const void*) override;
    bool itemColorSum = false; // Per-draw item state, never a configuration option.
    std::shared_ptr<UIColorKeyBlit> uiColorKey;
    DWORD m_width, m_height;
    DirectX::XMFLOAT4 background_color;
    BOOL m_bMakeScreenShot = 0;

    std::unique_ptr<CConstantBuffers> m_cbs = std::make_unique<CConstantBuffers>();
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext;


    Microsoft::WRL::ComPtr<ID3D11Texture2D> depthStencilBuffer;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthStencilState;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthStencilStateDisableWrite;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthStencilStateDisable;

    Microsoft::WRL::ComPtr<ID3D11BlendState> blendState;

    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerStateNoCull;

    std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 2> boundFigureTextures;

    std::shared_ptr<OwnedRender::RendererFacadeCore<owned::Renderer>> rendererFacade;
    OwnedRender::E2FrameTiming frameTiming;
    std::uint64_t frameSerial = 0;
    std::uint64_t c56WorldFigurePrimitives = 0;
    std::uint64_t c56WorldFigureQueueFailed = 0;
    std::uint64_t c56FigureSkippedNoMatrix = 0;
    std::uint64_t c56FigureSkippedNoFrame = 0;
    std::uint64_t c56FigureSkippedRenderError = 0;
    HRESULT c56FigureLastRenderError = S_OK;
    OwnedRendererClient ownedRenderer;
    std::uint64_t boundParticleSurface = 0;
    bool ownedWorldActive = false;
    owned::EmbeddedPrintBatch c74Prints;
    owned::FixedState CaptureC74EffectState();
    HRESULT PrepareC74Effect(bool requireWorld, bool useTexture, owned::FixedState&,
        std::shared_ptr<owned::SurfaceImage>&);
    bool FinishC74Effect(HRESULT, const char* event);
    bool campPopupActive = false, campPopupFailed = false;
    std::array<float, 16> ownedWorldToClip{};
    bool c56WorldPassRan = false;
    int c56WorldPassState = -1;
    std::unique_ptr<OwnedSpriteBatch> spriteBatch;
    std::unique_ptr<DirectX::CommonStates> m_states;
    FakeD3D7State fake_dx7;

    void MakeScreenShot();
    void RefreshOwnedTargets();
    bool InitializeDX(HWND hWnd, DWORD dwWidth, DWORD dwHeight);

public:
    enum class OwnedTerrainPassResult { unavailable, submitted, failed };
    std::uint64_t ModelPreviewFrame() const noexcept { return frameSerial; }
    bool Initialize(HWND hWnd, DWORD dwWidth, DWORD dwHeight);
    void SetGammaCorrection(float brightness, float contrast, float gamma) noexcept;
    ~CDX11();






    void BeginFrame();
    bool BeginWorld(const EIRenderWorldFrameV2& frame);
    void EndWorld();
    void BeginInterface();
    void RunC56WorldPass(const OwnedRender::E1VisualInputs* visuals = nullptr);
    void SetC56HitTestFrame(bool begin);
    void SubmitC56PointLights(const std::vector<owned::RetainedWorldPointLight>& lights,
                              double worldTime);
    bool IsOwnedWorldActive() const { return ownedWorldActive; }
    void EndFrame(bool present = true);
    void ClearColorBuffer(DWORD value);
    void ClearZBuffer(DWORD value);
    owned::FixedState CaptureC56MaterialState();
    bool SubmitC74FixedFunction(D3DPRIMITIVETYPE type, const owned::FixedFunctionVertex* vertices, DWORD count,
        const WORD* indices, DWORD indexCount, const SHMatrix& projection, bool untextured = false);
    bool SubmitC56ScreenRectangle(const owned::ScreenRectangleRequest& request);
    bool SubmitHudPortrait(const owned::EmbeddedHudPortraitInput& input);
    bool SubmitC56Figure(const owned::NativeGeometryView& view);
    bool SubmitC74Figure(const owned::NativeGeometryView& view,
        const owned::NativeWorldLighting* lighting,
        const std::vector<owned::RetainedWorldPointLight>& lights);
    bool SubmitHudVisual(const owned::HudPublishedVisual&, const owned::EmbeddedHudVisualInput&);
    void ReportC56FigureNoMatrix();
    void ReportC56FigureSkipSummary();
    bool SubmitC56GameParticles(const owned::GameParticleView& view);
    bool SubmitGolemVisual(const owned::EmbeddedGolemInput&,const std::array<ID3D11ShaderResourceView*,6>&);
    bool SubmitArenaVisual(const owned::EmbeddedArenaInput&,const std::array<ID3D11ShaderResourceView*,8>&);
    bool SubmitC74Ribbon(const owned::EmbeddedRibbonInput&);
    bool SubmitC74Selection(const owned::GameSelectionView&);
    bool SubmitC74Print(const owned::EmbeddedPrintInput&);
    bool FlushC74Prints();
    bool SubmitC74CombatText(const owned::EmbeddedCombatTextInput&);
    bool SubmitC56CampFigure(const owned::NativeGeometryView& view);
    bool FlushC56CampFigures();
    bool SubmitC56CampPopup(owned::CampRenderRequest,owned::CampPopupDraw,void*);
    void RestoreHostBindings();
    // A loading picture (wait screen, movie frame): the renderer keeps it on
    // screen while the next world prepares, as retail shows the load screen.
    void MarkLoadPicture();
    // Presents a full-screen BGRX image (movie or wait-screen frame) through
    // the renderer, fitted to the output and kept as the loading picture.
    HRESULT PresentLoadingImage(const std::vector<unsigned char>& bgrx, UINT width, UINT height,
                                std::uint64_t id, std::uint64_t generation);
    bool SubmitOwnedTerrain(EIRenderTerrainFrameV5 packet);
    OwnedTerrainPassResult SubmitC56TerrainPass(bool transparent);
    owned::NativeOwnedFixedState CaptureOwnedTerrainMaterial();

    BOOL BindTexture(DWORD dwIndex, ID3D11ShaderResourceView *pTexture,
                     std::uint64_t surfaceId = 0);
    ID3D11ShaderResourceView *GetBoundFigureTexture(DWORD dwIndex) const
    {
        return dwIndex < static_cast<DWORD>(boundFigureTextures.size()) ? boundFigureTextures[dwIndex].Get() : nullptr;
    }
    void SetBackgroundColor(DWORD value);
    void SetFogColor(DWORD value);
    void ScreenShot();
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
    void RetireOwnedWorld() noexcept;
    owned::RendererMemoryCounters ReadOwnedMemoryCounters() noexcept;
    HRESULT BlitPublishedSurface(std::uint64_t surfaceId, const RECT& destination,
                                 const RECT* source, const DDCOLORKEY* colorKey);
    DirectX::CommonStates *GetCommonStates() { return m_states.get(); };
    CConstantBuffers *GetConstantBuffers() { return m_cbs.get(); };

    void SetFakeRenderState(int key, DWORD value) { fake_dx7.SetRenderState(key, value); };
    void GetFakeRenderState(int key, DWORD *value) { fake_dx7.GetRenderState(key, value); };
    void DefaultSurfaceDesc(D3D11_TEXTURE2D_DESC *tex2d);    
};

