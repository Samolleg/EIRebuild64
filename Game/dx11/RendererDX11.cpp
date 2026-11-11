#include "stdafx.h"
#include "../RuntimePaths64.h"
#include <vector>;
#include <SimpleMath.h>

#include <wincodec.h> // Optional
#include <ScreenGrab.h>
#include <DirectXTex.h>

#include "ConstantBuffer.h"
#include "RendererDX11.h"
#include "../LiveObserver.h"
#include "embedded_hud_visual.h"
#include "embedded_arena.h"
#include "embedded_curse.h"
#include "model_draw.h"
#include "../OwnedRender/E1SurfaceBridge.h"
#include "../OwnedRender/E1RendererFacade.h"
#include "../OwnedRender/E3Cursor.h"
#include "../OwnedRender/E1SurfaceFacade.h"
#include "../FrameDiagnostics.h"

#include "UIColorKeyBlit.h"
#include "../OwnedRender/E1TerrainState.h"
#include "../OwnedRender/E1Terrain.h"
#include "../OwnedRender/E1TerrainSubmission.h"
#include "../GameJournal.h"
#include "../GeneralSettings.h"
#include <cmath>

#include "Utils.h"
#include "Vertex.h"
#include "../Renderer.h"
#include "../OwnedRender/E1VisualFrame.h"
#include "../OwnedRender/E4Camp.h"
#include "../OwnedRender/E1World.h"

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d3d11.lib")

using Microsoft::WRL::ComPtr;

float G_CLEAR_DEPTH_VALUE = 1.0;
void CDX11::RetireOwnedWorld() noexcept {
    const auto access = ownedRenderer.LockFrame();
    if (!rendererFacade || !rendererFacade->Ready()) return;
    const HRESULT hr = rendererFacade->Visit([](owned::Renderer& renderer) {
        const HRESULT retired = renderer.retire_embedded_world();
        // World retirement has drained GPU records; immutable catalog data belongs to the resources.
        if (SUCCEEDED(retired)) renderer.retire_world_map_preview(false);
        return retired;
    });
    if (FAILED(hr)) GameJournal::Event("ERROR", "world_retire", "hr=%08lx", static_cast<unsigned long>(hr));
    else GameJournal::MemorySnapshot("world_retired");
}
Microsoft::WRL::ComPtr<IDXGIAdapter> CDX11::ReadOwnedAdapter() noexcept {
    const auto access = ownedRenderer.LockFrame();
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (rendererFacade && rendererFacade->Ready()) rendererFacade->Visit([&](owned::Renderer& renderer) {
        adapter = renderer.graphics_adapter(); return S_OK;
    });
    return adapter;
}
owned::RendererMemoryCounters CDX11::ReadOwnedMemoryCounters() noexcept {
    const auto access = ownedRenderer.LockFrame();
    owned::RendererMemoryCounters counters;
    if (rendererFacade && rendererFacade->Ready()) rendererFacade->Visit([&](owned::Renderer& renderer) {
        counters = renderer.read_memory_counters(); return S_OK;
    });
    return counters;
}
D3D11_CULL_MODE G_CULL_MODE = D3D11_CULL_MODE::D3D11_CULL_BACK;
D3D11_COMPARISON_FUNC G_DEPTH_FUNC = D3D11_COMPARISON_FUNC::D3D11_COMPARISON_LESS_EQUAL;

bool CDX11::SetItemColorSum(bool active)
{
    const bool previous = itemColorSum;
    itemColorSum = active;
    return previous;
}

bool CDX11::SetRenderStates()
{
    // Native packets capture fake_dx7 state at submission. D3D12 creates its
    // PSOs from those values; no D3D11 immediate state belongs to this path.
    if (rendererFacade) {
        bool nativeD3D12 = false;
        const HRESULT hr = rendererFacade->Visit([&](owned::Renderer& renderer) {
            nativeD3D12 = renderer.d3d12_backend();
            return S_OK;
        });
        IfFailedThrowHR(hr);
        if (nativeD3D12) return true;
    }
    D3D11_RENDER_TARGET_BLEND_DESC rtbd = { 0 };
    auto &rs = fake_dx7.render_state;
    // true/false
    rtbd.BlendEnable = rs[D3DRENDERSTATE_ALPHABLENDENABLE];
    // see D3D11_BLEND_ZERO - these match up to 11
    assert(rs[D3DRENDERSTATE_SRCBLEND] < 12 && rs[D3DRENDERSTATE_DESTBLEND] < 12);
    rtbd.SrcBlend = (D3D11_BLEND)rs[D3DRENDERSTATE_SRCBLEND];
    rtbd.DestBlend = (D3D11_BLEND)rs[D3DRENDERSTATE_DESTBLEND];
    rtbd.BlendOp = D3D11_BLEND_OP::D3D11_BLEND_OP_ADD;

    rtbd.SrcBlendAlpha = D3D11_BLEND::D3D11_BLEND_ONE;
    rtbd.DestBlendAlpha = D3D11_BLEND::D3D11_BLEND_ZERO;
    rtbd.BlendOpAlpha = D3D11_BLEND_OP::D3D11_BLEND_OP_ADD;
    rtbd.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE::D3D11_COLOR_WRITE_ENABLE_ALL;

    D3D11_BLEND_DESC blendDesc = { 0 };
    blendDesc.RenderTarget[0] = rtbd;

    IfFailedThrowHR(
        this->device->CreateBlendState(&blendDesc, blendState.ReleaseAndGetAddressOf()));
    this->deviceContext->OMSetBlendState(blendState.Get(), NULL, 0xFFFFFFFF);


    D3DCULL cullState = (D3DCULL)rs[D3DRENDERSTATE_CULLMODE];
    if (cullState == D3DCULL::D3DCULL_NONE)
        this->deviceContext->RSSetState(this->rasterizerStateNoCull.Get());
    else
        this->deviceContext->RSSetState(this->rasterizerState.Get());

    if (rs[D3DRENDERSTATE_ZENABLE] == D3DZB_FALSE)
    {
        this->deviceContext->OMSetDepthStencilState(depthStencilStateDisable.Get(), 0);
    }
    else
    {
        this->deviceContext->OMSetDepthStencilState((rs[D3DRENDERSTATE_ZWRITEENABLE] == TRUE)
                                                        ? this->depthStencilState.Get()
                                                        : this->depthStencilStateDisableWrite.Get(),
                                                    0);
    }

    return true;
}


owned::NativeOwnedFixedState CDX11::CaptureOwnedTerrainMaterial()
{
    auto state = OwnedRender::E1TerrainMaterialState(fake_dx7.render_state,
        GetConstantBuffers()->cb_ps_pixelshader.data.alphaMode);
    D3DVIEWPORT7 viewport{};
    if (!rendererFacade || FAILED(rendererFacade->Visit([&](owned::Renderer& renderer) {
        return renderer.read_embedded_viewport(viewport);
    })) || !viewport.dwWidth || !viewport.dwHeight) return {};
    state.replay_frame_state = true;
    state.captured_viewport = {UINT(viewport.dwX), UINT(viewport.dwY),
        UINT(viewport.dwWidth), UINT(viewport.dwHeight)};
    state.captured_depth_range = {viewport.dvMinZ, viewport.dvMaxZ};
    const auto fog = owned::bridge_world_fog_snapshot();
    state.captured_fog_enable = theRenderer.IsAddFog() && fog.valid && fog.enabled;
    const auto& color = GetConstantBuffers()->cb_ps_pixelshader.data.fogColor;
    const auto byte = [](float v) { return DWORD((std::max)(0.f, (std::min)(1.f, v)) * 255.f + .5f); };
    state.captured_fog_color = (byte(color.x) << 16) | (byte(color.y) << 8) | byte(color.z);
    return state;
}

bool CDX11::SubmitOwnedTerrain(EIRenderTerrainFrameV5 packet)
{

    if(!ownedWorldActive)return false;
    uint32_t states[256]={};
    states[D3DRENDERSTATE_ZENABLE]=D3DZB_TRUE;states[D3DRENDERSTATE_ZWRITEENABLE]=TRUE;
    states[D3DRENDERSTATE_ZFUNC]=D3DCMP_LESSEQUAL;states[D3DRENDERSTATE_CULLMODE]=D3DCULL_CCW;
    states[D3DRENDERSTATE_FILLMODE]=D3DFILL_SOLID;states[D3DRENDERSTATE_SRCBLEND]=D3DBLEND_ONE;
    states[D3DRENDERSTATE_DESTBLEND]=D3DBLEND_ZERO;states[D3DRENDERSTATE_TEXTUREFACTOR]=0xffffffff;
    states[D3DRENDERSTATE_ALPHAFUNC]=D3DCMP_GREATEREQUAL;
    for(const auto& value:fake_dx7.render_state)if(value.first>=0&&value.first<256)states[value.first]=value.second;
    uint32_t stages[32]={};stages[D3DTSS_COLOROP]=D3DTOP_MODULATE;
    stages[D3DTSS_COLORARG1]=D3DTA_TEXTURE;stages[D3DTSS_COLORARG2]=D3DTA_DIFFUSE;
    stages[D3DTSS_ALPHAOP]=D3DTOP_SELECTARG1;stages[D3DTSS_ALPHAARG1]=D3DTA_TEXTURE;
    stages[D3DTSS_ADDRESSU]=stages[D3DTSS_ADDRESSV]=D3DTADDRESS_WRAP;
    stages[D3DTSS_MINFILTER]=stages[D3DTSS_MAGFILTER]=D3DTFG_LINEAR;stages[D3DTSS_MIPFILTER]=D3DTFP_LINEAR;
    packet.render_states=states;packet.render_state_count=256;packet.texture_states=stages;packet.texture_state_count=32;
    return ownedRenderer.SubmitTerrain(packet);
}

CDX11::OwnedTerrainPassResult CDX11::SubmitC56TerrainPass(bool transparent)
{
    if (!ownedWorldActive || !rendererFacade || !rendererFacade->Ready())
        return OwnedTerrainPassResult::unavailable;
    OwnedRender::E1TerrainPassSource pass;
    if (!OwnedRender::E1PrepareTerrainPass(transparent, pass))
        return OwnedTerrainPassResult::unavailable;

    OwnedRender::E1TerrainSubmission submission;
    const HRESULT hr = rendererFacade->Visit([&](owned::Renderer& renderer) {
        return OwnedRender::E1SubmitTerrainPass(renderer, transparent, submission);
    });
    if (SUCCEEDED(hr) && submission.completed) {
        return OwnedTerrainPassResult::submitted;
    }

    GameJournal::Event("RENDER", "terrain_c56_failed",
        "transparent=%u hr=%08lx queued=%llu reason=%s", transparent ? 1u : 0u,
        static_cast<unsigned long>(hr), static_cast<unsigned long long>(submission.queued),
        submission.failure ? submission.failure : "renderer_unavailable");
    return OwnedTerrainPassResult::failed;
}

owned::FixedState CDX11::CaptureC56MaterialState()
{
    owned::FixedState state;
    for (const auto& item : fake_dx7.render_state)
        if (item.first >= 0 && std::size_t(item.first) < state.render.size())
            state.render[item.first] = item.second;
    IfFailedThrowHR(rendererFacade->Visit([&](owned::Renderer& renderer) {
        return renderer.read_embedded_viewport(state.viewport);
    }));
    const auto& pixel = GetConstantBuffers()->cb_ps_pixelshader.data;
    const bool textured = bool(boundFigureTextures[0]) || boundParticleSurface != 0;
    state.stage[0][D3DTSS_COLOROP] = textured ? D3DTOP_MODULATE : D3DTOP_SELECTARG2;
    state.stage[0][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
    state.stage[0][D3DTSS_COLORARG2] = D3DTA_DIFFUSE;
    state.stage[0][D3DTSS_ALPHAOP] = !textured ? D3DTOP_SELECTARG2 :
        pixel.alphaMode == 0 ? D3DTOP_SELECTARG1 : pixel.alphaMode == 2 ? D3DTOP_SELECTARG2 : D3DTOP_MODULATE;
    state.stage[0][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
    state.stage[0][D3DTSS_ALPHAARG2] = D3DTA_DIFFUSE;
    state.stage[0][D3DTSS_ADDRESSU] = state.stage[0][D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
    state.stage[0][D3DTSS_MINFILTER] = state.stage[0][D3DTSS_MAGFILTER] = D3DTFG_LINEAR;
    state.stage[0][D3DTSS_MIPFILTER] = D3DTFP_LINEAR;
    const auto fog = owned::bridge_world_fog_snapshot();
    state.render[D3DRENDERSTATE_FOGENABLE] = ownedWorldActive && theRenderer.IsAddFog() && fog.valid && fog.enabled;
    const auto byte = [](float v) { return DWORD((std::max)(0.f, (std::min)(1.f, v)) * 255.f + .5f); };
    state.render[D3DRENDERSTATE_FOGCOLOR] = (byte(pixel.fogColor.x) << 16) |
        (byte(pixel.fogColor.y) << 8) | byte(pixel.fogColor.z);
    return state;
}

bool CDX11::SubmitC74FixedFunction(D3DPRIMITIVETYPE type, const owned::FixedFunctionVertex* vertices, DWORD count,
    const WORD* indices, DWORD indexCount, const SHMatrix& projection, bool untextured)
{
    HRESULT hr = E_UNEXPECTED;
    if (rendererFacade && rendererFacade->FrameActive()) {
        auto state = CaptureC56MaterialState();
        // SHMatrix is row-major M*p; the proxy shader uses row-vector mul(p,M).
        const auto transposed = DirectX::XMMatrixTranspose(sh_to_xm(projection));
        std::memcpy(&state.transform[D3DTRANSFORMSTATE_PROJECTION], &transposed, sizeof(D3DMATRIX));
        auto* texture = untextured ? nullptr : boundFigureTextures[0].Get();
        if (untextured) {
            state.stage[0][D3DTSS_COLOROP] = state.stage[0][D3DTSS_ALPHAOP] = D3DTOP_SELECTARG2;
            state.stage[0][D3DTSS_COLORARG2] = state.stage[0][D3DTSS_ALPHAARG2] = D3DTA_DIFFUSE;
            state.render[D3DRENDERSTATE_SHADEMODE] = D3DSHADE_FLAT;
        }
        std::shared_ptr<owned::SurfaceImage> surface;
        hr = !untextured && !device && boundParticleSurface ? OwnedRender::E1ParticleSurfaceImage(device.Get(),
            deviceContext.Get(), boundParticleSurface, surface) : S_OK;
        if (SUCCEEDED(hr)) hr = rendererFacade->Visit([&](owned::Renderer& renderer) {
            return renderer.draw_embedded_fixed_function(type, vertices, count,
                indices, indexCount, state, texture, surface.get());
        });
        RestoreHostBindings();
    }
    if (SUCCEEDED(hr)) return true;
    static unsigned reports = 0;
    if (reports++ < 8)
        GameJournal::Event("RENDER", "fixed_function_skipped",
            "owner=c74 type=%lu vertices=%lu indices=%lu hr=%08lx",
            DWORD(type), count, indexCount, static_cast<unsigned long>(hr));
    return false;
}
bool CDX11::SubmitC56ScreenRectangle(const owned::ScreenRectangleRequest& request)
{
    HRESULT hr = E_UNEXPECTED;
    if (rendererFacade && rendererFacade->FrameActive()) {
        const D3DVIEWPORT7 viewport{0, 0, m_width, m_height, 0.0f, 1.0f};
        hr = rendererFacade->Visit([&](owned::Renderer& renderer) {
            return renderer.queue_screen_rectangle(request, viewport);
        });
        RestoreHostBindings();
    }
    if (SUCCEEDED(hr)) return true;
    static bool reported = false;
    if (!reported) {
        reported = true;
        GameJournal::Event("RENDER", "screen_rectangle_skipped",
            "owner=c56 hr=%08lx", static_cast<unsigned long>(hr));
    }
    return false;
}

namespace {
bool SubmitScreenRectangleToC56(void* context,
                                const owned::ScreenRectangleRequest& request) noexcept
{
    return context && static_cast<CDX11*>(context)->SubmitC56ScreenRectangle(request);
}
}

bool CDX11::SubmitHudVisual(const owned::HudPublishedVisual& visual,
    const owned::EmbeddedHudVisualInput& input)
{
    if(!rendererFacade || !rendererFacade->FrameActive())return false;
    return owned::hud_embedded_submit_visual(visual,input,this,
        [](void* context,const owned::NativeGeometryView& view) {
            auto* self=static_cast<CDX11*>(context);
            auto state=self->CaptureC56MaterialState();
            state.render[D3DRENDERSTATE_FOGENABLE]=FALSE;
            state.stage[0][D3DTSS_COLOROP]=D3DTOP_MODULATE;
            state.stage[0][D3DTSS_COLORARG1]=D3DTA_TEXTURE;
            state.stage[0][D3DTSS_COLORARG2]=D3DTA_DIFFUSE;
            state.stage[0][D3DTSS_ALPHAOP]=D3DTOP_MODULATE;
            state.stage[0][D3DTSS_ALPHAARG1]=D3DTA_TEXTURE;
            state.stage[0][D3DTSS_ALPHAARG2]=D3DTA_DIFFUSE;
            const auto hr=self->rendererFacade->Visit([&](owned::Renderer& renderer) {
                const auto flushed=renderer.has_pending_ui()?renderer.flush_ui():S_OK;
                if(FAILED(flushed))return flushed;
                owned::Renderer::NativeQueueCheckpoint checkpoint;
                if(!renderer.checkpoint_native(checkpoint))return E_FAIL;
                const auto result=renderer.queue_native_owned(view,state);
                if(FAILED(result))renderer.rollback_native(checkpoint);
                return result;
            });
            self->RestoreHostBindings();return SUCCEEDED(hr);
        });
}

bool CDX11::SubmitHudPortrait(const owned::EmbeddedHudPortraitInput& input)
{
    if(!rendererFacade || !rendererFacade->FrameActive())return false;
    return owned::hud_embedded_submit_portrait(input,this,
        [](void* context,const owned::NativeGeometryView& view) {
            auto* self=static_cast<CDX11*>(context);
            auto state=self->CaptureC56MaterialState();
            state.render[D3DRENDERSTATE_FOGENABLE]=FALSE;
            state.render[D3DRENDERSTATE_SPECULARENABLE]=(view.flags&8u)?TRUE:FALSE;
            const bool textured=view.owned_texture.identity!=0;
            state.stage[0][D3DTSS_COLOROP]=state.stage[0][D3DTSS_ALPHAOP]=textured?D3DTOP_MODULATE:D3DTOP_SELECTARG2;
            state.stage[0][D3DTSS_COLORARG1]=state.stage[0][D3DTSS_ALPHAARG1]=D3DTA_TEXTURE;
            state.stage[0][D3DTSS_COLORARG2]=state.stage[0][D3DTSS_ALPHAARG2]=D3DTA_DIFFUSE;
            const auto hr=self->rendererFacade->Visit([&](owned::Renderer& renderer) {
                const auto flushed=renderer.has_pending_ui()?renderer.flush_ui():S_OK;
                if(FAILED(flushed))return flushed;
                owned::Renderer::NativeQueueCheckpoint checkpoint;
                if(!renderer.checkpoint_native(checkpoint))return E_FAIL;
                const auto result=textured?renderer.queue_native_owned(view,state):
                    renderer.queue_native_direct(view,state,nullptr);
                if(FAILED(result))renderer.rollback_native(checkpoint);
                return result;
            });
            self->RestoreHostBindings();return SUCCEEDED(hr);
        });
}

bool CDX11::SubmitC74Figure(const owned::NativeGeometryView& view,
    const owned::NativeWorldLighting* lighting,
    const std::vector<owned::RetainedWorldPointLight>& lights)
{
    if (!rendererFacade || !rendererFacade->FrameActive()) {
        ++c56FigureSkippedNoFrame;
        return false;
    }
    auto state = CaptureC56MaterialState();
    const bool camp = OwnedRender::E4::OwnsDraw();
    auto model = view;
    if (camp && itemColorSum && state.render[D3DRENDERSTATE_SPECULARENABLE]) model.flags |= 8u;
    if (OwnedRender::E4::OwnsDraw() || owned::model_draw_is_preview() ||
        (ownedRenderer.FigureActive() && ownedRenderer.FigureValues().object_id == 0))
        state.render[D3DRENDERSTATE_FOGENABLE] = FALSE;
    state.render[D3DRENDERSTATE_SPECULARENABLE] = (model.flags & 8u) ? TRUE : FALSE;
    const auto& pixel = GetConstantBuffers()->cb_ps_pixelshader.data;
    auto* primary = ownedRenderer.FigureActive() ? ownedRenderer.FigurePrimaryTexture() : boundFigureTextures[0].Get();
    auto* secondary = ownedRenderer.FigureActive() ? ownedRenderer.FigureSecondaryTexture() : boundFigureTextures[1].Get();
    bool nativeD3D12 = false;
    if (FAILED(rendererFacade->Visit([&](owned::Renderer& renderer) {
        nativeD3D12 = renderer.d3d12_backend();
        return S_OK;
    }))) return false;
    const auto primaryId = ownedRenderer.FigureActive() && ownedRenderer.FigurePrimarySurface() ?
        ownedRenderer.FigurePrimarySurface() : boundParticleSurface;
    const auto secondaryId = ownedRenderer.FigureActive() && ownedRenderer.FigureSecondarySurface() ?
        ownedRenderer.FigureSecondarySurface() : boundSecondarySurface;
    const auto primarySurface = nativeD3D12 ? OwnedRender::E1SurfaceValue(primaryId) : nullptr;
    const auto secondarySurface = nativeD3D12 ? OwnedRender::E1SurfaceValue(secondaryId) : nullptr;
    if (nativeD3D12 && ((primaryId && !primarySurface) ||
        (secondaryId && !secondarySurface))) return false;
    const bool textured = primary || bool(primarySurface);
    state.stage[0][D3DTSS_COLOROP] = textured ? D3DTOP_MODULATE : D3DTOP_SELECTARG2;
    state.stage[0][D3DTSS_COLORARG1] = D3DTA_TEXTURE; state.stage[0][D3DTSS_COLORARG2] = D3DTA_DIFFUSE;
    state.stage[0][D3DTSS_ALPHAOP] = !textured ? D3DTOP_SELECTARG2 : pixel.alphaMode == 0 ?
        D3DTOP_SELECTARG1 : pixel.alphaMode == 2 ? D3DTOP_SELECTARG2 : D3DTOP_MODULATE;
    state.stage[0][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE; state.stage[0][D3DTSS_ALPHAARG2] = D3DTA_DIFFUSE;
    state.stage[0][D3DTSS_ADDRESSU] = state.stage[0][D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
    state.stage[0][D3DTSS_MINFILTER] = state.stage[0][D3DTSS_MAGFILTER] = D3DTFG_LINEAR;
    state.stage[0][D3DTSS_MIPFILTER] = D3DTFP_LINEAR;
    const auto byte = [](float v) {return DWORD((std::max)(0.f, (std::min)(1.f, v))*255.f+.5f);};
    state.render[D3DRENDERSTATE_FOGCOLOR] = (byte(pixel.fogColor.x)<<16) |
        (byte(pixel.fogColor.y)<<8) | byte(pixel.fogColor.z);
    const HRESULT hr = rendererFacade->Visit([&](owned::Renderer& renderer) {
        if (camp && !lighting && !secondary && !nativeD3D12) {
            const HRESULT flushed = renderer.has_pending_ui() ? renderer.flush_ui() : S_OK;
            if (FAILED(flushed)) return flushed;
            owned::Renderer::NativeQueueCheckpoint checkpoint;
            if (!renderer.checkpoint_native(checkpoint)) return E_FAIL;
            const HRESULT queued = renderer.queue_native_direct(model, state, primary);
            if (FAILED(queued)) renderer.rollback_native(checkpoint);
            return queued;
        }
        return renderer.queue_embedded_figure(model, state, primary, secondary, lighting, lights,
            primarySurface.get(), secondarySurface.get());
    });
    RestoreHostBindings();
    if (SUCCEEDED(hr)) {++c56WorldFigurePrimitives; return true;}
    if (camp) campPopupFailed = true;
    ++c56WorldFigureQueueFailed; ++c56FigureSkippedRenderError; c56FigureLastRenderError = hr;
    return false;
}

bool CDX11::SubmitC56Figure(const owned::NativeGeometryView& source)
{
    // Generic native callers already supplied their current light constants.
    // Figure wrappers above this boundary now supply raw source facts instead.
    auto view = source;
    owned::NativeWorldLighting lighting;
    std::vector<owned::RetainedWorldPointLight> lights;
    if (view.lighting.enabled) {
        const auto& buffers = *GetConstantBuffers();
        const auto& vertex = buffers.cb_vs_vertexshader.data;
        const auto& sun = buffers.cb_vs_vertexlights.data.sunlight;
        lighting.ambient = {vertex.dcvAmbient.r, vertex.dcvAmbient.g, vertex.dcvAmbient.b, 1};
        lighting.sunlight_diffuse = {sun.dcMaterialDiffuse.r, sun.dcMaterialDiffuse.g, sun.dcMaterialDiffuse.b, 1};
        SHMatrix world; memcpy(&world, view.lighting.object_to_world.data(), sizeof(world));
        const auto direction = world.RotateVector(CPlot3D(-sun.rd.x,-sun.rd.y,-sun.rd.z));
        lighting.sunlight_direction = {direction.x,direction.y,direction.z,0};
        view.lighting.material_diffuse = {1,1,1};
        view.lighting.emissive = {vertex.dvEmissive.x,vertex.dvEmissive.y,vertex.dvEmissive.z,0};
        for (unsigned i=0; i<(std::min)(vertex.nlights,DWORD(16)); ++i) {
            const auto& value = buffers.cb_vs_vertexlights.data.lights[i];
            if (!(value.rcpr_range2>0)) continue;
            owned::RetainedWorldPointLight light;
            light.source_id=int(i)+1; light.center={value.position.x,value.position.y,value.position.z};
            light.color={value.dcMaterialDiffuse.r,value.dcMaterialDiffuse.g,value.dcMaterialDiffuse.b};
            light.radius=std::sqrt(1.f/value.rcpr_range2); lights.push_back(light);
        }
    }
    return SubmitC74Figure(view, view.lighting.enabled ? &lighting : nullptr, lights);
}
void CDX11::ReportC56FigureNoMatrix()
{
    ++c56FigureSkippedNoMatrix;
}
void CDX11::ReportC56FigureSkipSummary()
{
    if(!c56FigureSkippedNoMatrix && !c56FigureSkippedNoFrame && !c56FigureSkippedRenderError)
        return;
    static ULONGLONG lastSummaryTick = 0;
    const ULONGLONG now = GetTickCount64();
    if (lastSummaryTick && now - lastSummaryTick < 5000)
        return;
    lastSummaryTick = now;
    GameJournal::Event("RENDER","figure_skipped",
        "owner=c56 no_matrix=%llu no_frame=%llu render_error=%llu hr=%08lx",
        static_cast<unsigned long long>(c56FigureSkippedNoMatrix),
        static_cast<unsigned long long>(c56FigureSkippedNoFrame),
        static_cast<unsigned long long>(c56FigureSkippedRenderError),
        static_cast<unsigned long>(c56FigureLastRenderError));
    c56FigureSkippedNoMatrix=0;
    c56FigureSkippedNoFrame=0;
    c56FigureSkippedRenderError=0;
    c56FigureLastRenderError=S_OK;
}
bool CDX11::SubmitC56GameParticles(const owned::GameParticleView& view)
{
    HRESULT hr = E_UNEXPECTED;
    if (ownedWorldActive && rendererFacade && rendererFacade->FrameActive()) {
        owned::FixedState state = CaptureC56MaterialState();
        // Particle clip extents are logical host pixels, not the scaled target.
        state.viewport.dwX = state.viewport.dwY = 0;
        state.viewport.dwWidth = m_width;
        state.viewport.dwHeight = m_height;
        // The host represents stage 0 with alphaMode, as for c56 figures.
        const auto alphaMode = GetConstantBuffers()->cb_ps_pixelshader.data.alphaMode;
        state.stage[0][D3DTSS_COLOROP] = D3DTOP_MODULATE;
        state.stage[0][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        state.stage[0][D3DTSS_COLORARG2] = D3DTA_DIFFUSE;
        state.stage[0][D3DTSS_ALPHAOP] = alphaMode == 0 ? D3DTOP_SELECTARG1 :
            alphaMode == 2 ? D3DTOP_SELECTARG2 : D3DTOP_MODULATE;
        state.stage[0][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        state.stage[0][D3DTSS_ALPHAARG2] = D3DTA_DIFFUSE;
        if (!boundFigureTextures[0] && !boundParticleSurface) {
            state.stage[0][D3DTSS_COLOROP] = D3DTOP_SELECTARG2;
            state.stage[0][D3DTSS_ALPHAOP] = D3DTOP_SELECTARG2;
        }
        const auto& fog = GetConstantBuffers()->cb_ps_pixelshader.data.fogColor;
        const auto byte = [](float v) { return DWORD((std::max)(0.f, (std::min)(1.f, v)) * 255.f + .5f); };
        state.render[D3DRENDERSTATE_FOGCOLOR] = (byte(fog.x) << 16) | (byte(fog.y) << 8) | byte(fog.z);
        if (view.additive) {
            state.render[D3DRENDERSTATE_SRCBLEND] = D3DBLEND_SRCALPHA;
            state.render[D3DRENDERSTATE_DESTBLEND] = D3DBLEND_ONE;
        }
        std::shared_ptr<owned::SurfaceImage> texture;
        hr = (boundFigureTextures[0] || boundParticleSurface) ? OwnedRender::E1ParticleSurfaceImage(device.Get(),
            deviceContext.Get(), boundParticleSurface, texture) : S_OK;
        if (SUCCEEDED(hr)) {
            hr = rendererFacade->Visit([&](owned::Renderer& renderer) {
                // Drain the host's queued UI/native geometry before this
                // immediate pass, preserving the proxy's submission order.
                HRESULT flushed = renderer.has_pending_ui() ? renderer.flush_ui() : S_OK;
                if (SUCCEEDED(flushed)) flushed = renderer.flush_native();
                if (FAILED(flushed)) return flushed;
                return renderer.draw_game_particles(view, state,
                    texture.get());
            });
            // c56 uses the same device, but its shaders/targets/state differ.
            // Restore the host bindings for subsequent figure draws.
            RestoreHostBindings();
        }
    }
    if (SUCCEEDED(hr)) return true;
    static bool reportedSkipped = false;
    if (!reportedSkipped) {
        reportedSkipped = true;
        GameJournal::Event("RENDER", "game_particles_skipped",
            "owner=c56 reason=c56_rejected hr=%08lx", static_cast<unsigned long>(hr));
    }
    return false;
}

owned::FixedState CDX11::CaptureC74EffectState()
{
    auto state = CaptureC56MaterialState();
    // c74 effect coordinates use logical host pixels, independent of render scale.
    state.viewport.dwX = state.viewport.dwY = 0;
    state.viewport.dwWidth = m_width; state.viewport.dwHeight = m_height;
    return state;
}
HRESULT CDX11::PrepareC74Effect(bool requireWorld, bool useTexture, owned::FixedState& state,
    std::shared_ptr<owned::SurfaceImage>& texture)
{
    if (!rendererFacade || !rendererFacade->FrameActive() || (requireWorld && !ownedWorldActive))
        return E_UNEXPECTED;
    state = CaptureC74EffectState();
    HRESULT hr = rendererFacade->Visit([](owned::Renderer& renderer) {
        HRESULT flushed = renderer.has_pending_ui() ? renderer.flush_ui() : S_OK;
        return FAILED(flushed) ? flushed : renderer.flush_native();
    });
    if (SUCCEEDED(hr) && useTexture && (boundFigureTextures[0] || boundParticleSurface))
        hr = OwnedRender::E1ParticleSurfaceImage(device.Get(), deviceContext.Get(), boundParticleSurface, texture);
    return hr;
}
bool CDX11::FinishC74Effect(HRESULT hr, const char* event)
{
    if (rendererFacade && rendererFacade->FrameActive()) RestoreHostBindings();
    if (SUCCEEDED(hr)) return true;
    static std::map<std::string, HRESULT> failures;
    const auto found = failures.find(event);
    if (found == failures.end() || found->second != hr) {
        failures[event] = hr;
        GameJournal::Event("RENDER", event, "owner=c74 reason=renderer_rejected hr=%08lx", static_cast<unsigned long>(hr));
    }
    return false;
}
bool CDX11::SubmitCurseVisual(const owned::EmbeddedCurseInput& input,ID3D11ShaderResourceView* texture,std::uint64_t surfaceId)
{
    owned::FixedState state;std::shared_ptr<owned::SurfaceImage> unused;
    HRESULT hr=PrepareC74Effect(true,false,state,unused);
    bool published=false;
    if(SUCCEEDED(hr))hr=rendererFacade->Visit([&](owned::Renderer& renderer){
        std::shared_ptr<owned::SurfaceImage> image;
        if(renderer.d3d12_backend()&&surfaceId) {
            const HRESULT resolved=OwnedRender::E1ParticleSurfaceImage(nullptr,nullptr,surfaceId,image);
            if(FAILED(resolved))return resolved;
        }
        published=true;return renderer.draw_embedded_curse(input,state,texture,image.get());
    });
    LiveObserver::ObserveCurse(input,hr,published);
    return FinishC74Effect(hr,"curse_visual_skipped");
}
bool CDX11::SubmitGolemVisual(const owned::EmbeddedGolemInput& input,const std::array<ID3D11ShaderResourceView*,6>& textures,const std::array<std::uint64_t,6>& surfaceIds)
{
    owned::FixedState state;std::shared_ptr<owned::SurfaceImage> unused;
    HRESULT hr=PrepareC74Effect(true,false,state,unused);
    bool published=false;
    if(SUCCEEDED(hr))hr=rendererFacade->Visit([&](owned::Renderer& renderer){
        std::array<std::shared_ptr<owned::SurfaceImage>,6> retained;
        std::array<owned::Renderer::EmbeddedEffectTexture,6> materials{};
        for(size_t i=0;i<materials.size();++i) {
            if(renderer.d3d12_backend()) {
                if(surfaceIds[i]) {
                    const HRESULT resolved=OwnedRender::E1ParticleSurfaceImage(nullptr,nullptr,surfaceIds[i],retained[i]);
                    if(FAILED(resolved))return resolved;
                    materials[i].image=retained[i].get();
                }
            } else materials[i].srv=textures[i];
        }
        published=true;return renderer.draw_embedded_golem(input,state,materials);
    });
    LiveObserver::ObserveGolem(input,hr,published);
    return FinishC74Effect(hr,"golem_visual_skipped");
}
bool CDX11::SubmitArenaVisual(const owned::EmbeddedArenaInput& input,
    const std::array<ID3D11ShaderResourceView*,8>& textures,const std::array<std::uint64_t,8>& surfaceIds)
{
    owned::FixedState state;std::shared_ptr<owned::SurfaceImage> unused;
    HRESULT hr=PrepareC74Effect(true,false,state,unused);
    bool published=false;
    if(SUCCEEDED(hr))hr=rendererFacade->Visit([&](owned::Renderer& renderer) {
        std::array<std::shared_ptr<owned::SurfaceImage>,8> retained;
        std::array<owned::Renderer::EmbeddedEffectTexture,8> materials{};
        for(size_t i=0;i<materials.size();++i) {
            if(renderer.d3d12_backend()) {
                if(surfaceIds[i]) {
                    const HRESULT resolved=OwnedRender::E1ParticleSurfaceImage(nullptr,nullptr,surfaceIds[i],retained[i]);
                    if(FAILED(resolved))return resolved;
                    materials[i].image=retained[i].get();
                }
            } else materials[i].srv=textures[i];
        }
        published=true;return renderer.draw_embedded_arena(input,state,materials);
    });
    LiveObserver::ObserveArena(input,hr,published);
    return FinishC74Effect(hr,"arena_visual_skipped");
}
bool CDX11::SubmitC74Ribbon(const owned::EmbeddedRibbonInput& input)
{
    owned::FixedState state; std::shared_ptr<owned::SurfaceImage> texture;
    HRESULT hr = PrepareC74Effect(!input.interface_pass, true, state, texture);
    if (SUCCEEDED(hr)) hr = rendererFacade->Visit([&](owned::Renderer& renderer) {
        return renderer.draw_embedded_ribbon(input, state, texture.get());
    });
    return FinishC74Effect(hr, "ribbon_skipped");
}
bool CDX11::SubmitC74Selection(const owned::GameSelectionView& view)
{
    owned::FixedState state; std::shared_ptr<owned::SurfaceImage> texture;
    HRESULT hr = PrepareC74Effect(true, false, state, texture);
    if (SUCCEEDED(hr)) hr = rendererFacade->Visit([&](owned::Renderer& renderer) {
        return renderer.draw_game_selection(view, state, nullptr);
    });
    return FinishC74Effect(hr, "selection_skipped");
}
bool CDX11::SubmitC74Print(const owned::EmbeddedPrintInput& input)
{
    if (!input.index_count) return true;
    owned::FixedState state; std::shared_ptr<owned::SurfaceImage> texture;
    HRESULT hr = S_OK;
    if (c74Prints.runs.empty()) hr = PrepareC74Effect(true, true, state, texture);
    else {
        if (!ownedWorldActive || !rendererFacade || !rendererFacade->FrameActive()) hr = E_UNEXPECTED;
        else {
            state = CaptureC74EffectState();
            if (boundFigureTextures[0] || boundParticleSurface) hr = OwnedRender::E1ParticleSurfaceImage(device.Get(),
                deviceContext.Get(), boundParticleSurface, texture);
        }
    }
    if (SUCCEEDED(hr)) hr = c74Prints.append(input, state, std::move(texture));
    return FinishC74Effect(hr, "terrain_print_skipped");
}
bool CDX11::FlushC74Prints()
{
    if (c74Prints.runs.empty()) return true;
    HRESULT hr = E_UNEXPECTED;
    if (ownedWorldActive && rendererFacade && rendererFacade->FrameActive())
        hr = rendererFacade->Visit([&](owned::Renderer& renderer) { return renderer.draw_embedded_prints(c74Prints); });
    c74Prints.clear();
    return FinishC74Effect(hr, "terrain_print_run_skipped");
}
bool CDX11::SubmitC74CombatText(const owned::EmbeddedCombatTextInput& input)
{
    owned::FixedState state; std::shared_ptr<owned::SurfaceImage> texture;
    HRESULT hr = PrepareC74Effect(false, true, state, texture);
    if (SUCCEEDED(hr)) hr = rendererFacade->Visit([&](owned::Renderer& renderer) {
        return renderer.draw_embedded_combat_text(input, state, texture.get());
    });
    return FinishC74Effect(hr, "combat_text_skipped");
}

bool CDX11::BeginSimulation()
{
    if (!rendererFacade || !rendererFacade->Ready()) return false;
    const auto access = ownedRenderer.LockFrame();
    const HRESULT hr = rendererFacade->Visit([](owned::Renderer& renderer) {
        return renderer.begin_embedded_simulation();
    });
    IfFailedThrowHR(hr);
    return hr == S_OK;
}
void CDX11::FinishSimulation() noexcept
{
    if (!rendererFacade || !rendererFacade->Ready()) return;
    const auto access = ownedRenderer.LockFrame();
    (void)rendererFacade->Visit([](owned::Renderer& renderer) {
        renderer.finish_embedded_frame();
        return S_OK;
    });
}

void CDX11::BeginFrame()
{
    struct FailedPhase {
        CDX11& owner;
        bool complete = false;
        ~FailedPhase() { if (!complete) owner.FinishSimulation(); }
    } phase{*this};
    ReportC56FigureSkipSummary();
    // c73 separates authoritative engine frames from D3D scene brackets.
    // Reopen world composition and advance per-frame caches before BeginScene
    // or any clear/draw; Present intentionally leaves the prior frame sealed.
    IfFailedThrowHR(rendererFacade->Visit([](owned::Renderer& renderer) {
        renderer.enable_gpu_journal();
        return renderer.begin_engine_frame();
    }));
    IfFailedThrowHR(rendererFacade->BeginFrame());
    owned::bridge_set_screen_rectangle_sink(this, &SubmitScreenRectangleToC56);
    frameTiming.Begin();
    RefreshOwnedTargets();
    IfFailedThrowHR(rendererFacade->Visit([&](owned::Renderer& renderer) {
        return renderer.clear_embedded_targets(reinterpret_cast<const float*>(&background_color),
            &G_CLEAR_DEPTH_VALUE);
    }));
    SetRenderStates();
    phase.complete = true;
}

bool CDX11::BeginWorld(const EIRenderWorldFrameV2& frame)
{
    uint32_t skyConsumed = 0;
    c56WorldFigurePrimitives = 0;
    c56WorldFigureQueueFailed = 0;
    std::copy_n(frame.world_to_clip, ownedWorldToClip.size(), ownedWorldToClip.begin());
    c56WorldPassRan = false;
    IfFailedThrowHR(ownedRenderer.BeginWorld(frame, skyConsumed));
    ownedWorldActive = true;
    OwnedRender::E1OwnedVisualFrameNewDraw();
    RefreshOwnedTargets();
    SetRenderStates();
    return skyConsumed != 0;
}

void CDX11::SetC56HitTestFrame(bool begin)
{
    const auto access = ownedRenderer.LockFrame();
    if (!rendererFacade) return;
    const HRESULT result = rendererFacade->Visit([&](owned::Renderer& renderer) {
        renderer.hit_test_frame(begin);
        return S_OK;
    });
    if (FAILED(result))
        GameJournal::Event("RENDER", "global_map_mask_failed", "boundary=%s hr=%08lx",
            begin ? "begin" : "end", static_cast<unsigned long>(result));
}

void CDX11::RunC56WorldPass(const OwnedRender::E1VisualInputs* visuals)
{
    if (!ownedWorldActive || c56WorldPassRan || !rendererFacade)
        return;
    owned::FixedState frame;
    for (const auto& value : fake_dx7.render_state)
        if (value.first >= 0 && static_cast<std::size_t>(value.first) < frame.render.size())
            frame.render[value.first] = value.second;
    frame.viewport.dwX = frame.viewport.dwY = 0;
    frame.viewport.dwWidth = m_width;
    frame.viewport.dwHeight = m_height;
    const auto fogState = CaptureC56MaterialState();
    frame.render[D3DRENDERSTATE_FOGENABLE] = fogState.render[D3DRENDERSTATE_FOGENABLE];
    frame.render[D3DRENDERSTATE_FOGCOLOR] = fogState.render[D3DRENDERSTATE_FOGCOLOR];
    bool owned = false;
    const HRESULT pass = rendererFacade->Visit([&](owned::Renderer& renderer) {
        // The owned-visual frame draws shadows, point lights and every claimed
        // visual, and flushes the static set itself; without it (or when it
        // was rejected before the flush) the static set is flushed here.
        if (visuals)
            owned = OwnedRender::E1RunOwnedVisualFrame(renderer, frame, *visuals);
        HRESULT hr = S_OK;
        if (!visuals || !OwnedRender::E1OwnedVisualFrameFlushedStatics())
            hr = renderer.flush_native_static(frame, ownedWorldToClip);
        // Complete queued colour before the host draws remaining overlays.
        if (SUCCEEDED(hr)) hr = renderer.flush_native();
        if (FAILED(hr)) renderer.discard_native();
        return hr;
    });
    if (FAILED(pass)) OwnedRender::E1OwnedVisualFrameNewDraw();
    c56WorldPassRan = true;
    RefreshOwnedTargets();
    SetRenderStates();
    // The native pass already captured surface identities in its packets.
    // Only D3D11 restores immediate SRV bindings after the owned world pass.
    if (!D3D12Selected()) {
        for (UINT stage = 0; stage < boundFigureTextures.size(); ++stage) {
            ID3D11ShaderResourceView* resource = boundFigureTextures[stage].Get();
            deviceContext->PSSetShaderResources(stage, 1, &resource);
        }
    }
    const int state = FAILED(pass) ? 3 : visuals ? (owned ? 2 : 1) : 0;
    if (state != c56WorldPassState) {
        c56WorldPassState = state;
        GameJournal::Event("RENDER", "world_pass",
            "owner=c56 owned_visual_frame=%s hr=%08lx", state == 2 ? "drawn" : state == 0 ? "not_requested" : "rejected", static_cast<unsigned long>(pass));
    }
}

bool CDX11::SubmitMovie(const void* owner, std::shared_ptr<const Video::Frame> frame,
                       const Video::Presentation& presentation)
{
    const auto access = ownedRenderer.LockFrame();
    if (!owner || !frame || !rendererFacade || !rendererFacade->Ready() ||
        frame->format != Video::Frame::Format::BGRA8 || !frame->width || !frame->height ||
        frame->width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        frame->height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        frame->stride < size_t(frame->width) * 4 ||
        frame->pixels.size() < size_t(frame->stride) * frame->height) return false;
    const bool ownFrame = !rendererFacade->FrameActive();
    try {
        auto image = std::make_shared<owned::SurfaceImage>();
        image->id = reinterpret_cast<std::uintptr_t>(this);
        if (!++movieGeneration) ++movieGeneration;
        image->generation = movieGeneration;
        image->width = frame->width; image->height = frame->height;
        image->pitch = LONG(frame->stride);
        image->format.dwSize = sizeof(image->format);
        image->format.dwFlags = DDPF_RGB;
        image->format.dwRGBBitCount = 32;
        image->format.dwRBitMask = 0x00ff0000;
        image->format.dwGBitMask = 0x0000ff00;
        image->format.dwBBitMask = 0x000000ff;
        image->pixels = frame->pixels;
        if (ownFrame) { OwnedRender::E1BeginFrame(); BeginFrame(); }
        movieOwner = owner;
        const HRESULT hr = rendererFacade->Visit([&](owned::Renderer& renderer) {
            renderer.set_embedded_movie_image(presentation.loading ? image : nullptr);
            if (presentation.loading) renderer.mark_load_picture();
            // Composition, aspect fit and bars belong to the renderer.
            return renderer.present_image(*image, nullptr, nullptr);
        });
        if (FAILED(hr)) {
            GameJournal::Event("render", "movie_submit_failed", "hr=%08lx", static_cast<unsigned long>(hr));
            if (ownFrame) EndFrame(false);
            return false;
        }
        // A credits/UI draw owns its enclosing frame; never close it here.
        if (ownFrame) EndFrame(presentation.present);
        return true;
    } catch (...) {
        if (ownFrame && rendererFacade->FrameActive()) {
            rendererFacade->DiscardFrame(); frameTiming.End(false);
        }
        GameJournal::Event("render", "movie_submit_failed");
        return false;
    }
}
void CDX11::ClearMovie(const void* owner)
{
    const auto access = ownedRenderer.LockFrame();
    if (movieOwner != owner) return;
    movieOwner = nullptr;
    if (rendererFacade) rendererFacade->Visit([](owned::Renderer& renderer) {
        renderer.set_embedded_movie_image(nullptr);
        return S_OK;
    });
}

void CDX11::MarkLoadPicture()
{
    if (!rendererFacade) return;
    rendererFacade->Visit([](owned::Renderer& renderer) {
        renderer.mark_load_picture();
        return S_OK;
    });
}

HRESULT CDX11::PresentLoadingImage(const std::vector<unsigned char>& bgrx, UINT width, UINT height,
                                   std::uint64_t id, std::uint64_t generation)
{
    if (!rendererFacade || !width || !height || bgrx.size() < size_t(width) * height * 4)
        return E_INVALIDARG;
    owned::SurfaceImage image;
    image.id = id;
    image.generation = generation;
    image.width = width;
    image.height = height;
    image.pitch = LONG(width * 4);
    image.format.dwSize = sizeof(image.format);
    image.format.dwFlags = DDPF_RGB;
    image.format.dwRGBBitCount = 32;
    image.format.dwRBitMask = 0x00ff0000;
    image.format.dwGBitMask = 0x0000ff00;
    image.format.dwBBitMask = 0x000000ff;
    image.pixels = bgrx;
    return rendererFacade->Visit([&](owned::Renderer& renderer) {
        return renderer.present_image(image, nullptr, nullptr);
    });
}

void CDX11::SubmitC56PointLights(
    const std::vector<owned::RetainedWorldPointLight>& lights, double worldTime)
{
    if (!ownedWorldActive || !rendererFacade)
        return;
    const HRESULT hr = rendererFacade->Visit([&](owned::Renderer& renderer) {
        return renderer.set_native_point_lights(lights, worldTime) ? S_OK : E_FAIL;
    });
    IfFailedThrowHR(hr);
}

void CDX11::EndWorld()
{
    FlushC74Prints();
    // Finish the same c56 world pass before the caller starts drawing the
    // interface. composite_world drains queued native geometry and performs
    // the renderer-owned world composition; Present remains the frame owner.
    const HRESULT composite = rendererFacade->Visit([](owned::Renderer& renderer) {
        return renderer.composite_world();
    });
    if (FAILED(composite))
        GameJournal::Event("ERROR", "world_composite", "operation=composite_world hr=%08lx",
            static_cast<unsigned long>(composite));
    IfFailedThrowHR(composite);
    ReportC56FigureSkipSummary();
    if (RuntimeDiagnostics::Enabled()) GameJournal::Event("RENDER", "world_model_owner",
        "owner=c56 path=c56_native_queue submitted=%llu skipped=%llu",
        static_cast<unsigned long long>(c56WorldFigurePrimitives),
        static_cast<unsigned long long>(c56WorldFigureQueueFailed));
    c56WorldFigurePrimitives = 0;
    c56WorldFigureQueueFailed = 0;
    ownedWorldActive = false;
    RefreshOwnedTargets();
    SetRenderStates();
}

void CDX11::BeginInterface()
{
    // Interface-only 3D must follow world composition, just like the mission HUD.
    IfFailedThrowHR(rendererFacade->Visit([](owned::Renderer& renderer) {
        return renderer.composite_world();
    }));
    RefreshOwnedTargets();
    SetRenderStates();
}

void CDX11::RefreshOwnedTargets()
{
    IfFailedThrowHR(rendererFacade->Visit([](owned::Renderer& renderer) {
        return renderer.bind_embedded_targets();
    }));
}

void CDX11::EndFrame(bool present)
{
    // Close capture-only frames and all early/failed presentation exits.
    struct FinishPhase {
        CDX11& owner;
        ~FinishPhase() { owner.FinishSimulation(); }
    } finishPhase{*this};
    ReportC56FigureSkipSummary();
    owned::bridge_set_screen_rectangle_sink(nullptr, nullptr);
    if (!present) {
        const HRESULT discarded = rendererFacade->DiscardFrame();
        frameTiming.End(false);
        IfFailedThrowHR(discarded);
        return;
    }
    FrameDiagnostics::Scope timing(FrameDiagnostics::Present);
    if (m_bMakeScreenShot)
    {
        MakeScreenShot();
        m_bMakeScreenShot = false;
    }
    IfFailedThrowHR(rendererFacade->Visit([](owned::Renderer& renderer) {
        return OwnedRender::E3QueueCursor(renderer, renderer.cursor_scale_percent(), renderer.load_cursor_wanted());
    }));
    const HRESULT presented = rendererFacade->EndFrame();
    frameTiming.End(SUCCEEDED(presented));
    if (SUCCEEDED(presented)) owned::frame_waits::frame.presented = true;
    FrameDiagnostics::Presented(presented);
    if (RuntimeDiagnostics::Enabled() && SUCCEEDED(presented) && (++frameSerial % 120u) == 0) {
        const auto sample = frameTiming.Read();
        if (RuntimeDiagnostics::Enabled()) GameJournal::Event("RENDER", "frame_timing",
            "owner=c56 frame=%llu cpu_ms_median=%.3f cpu_ms_p95=%.3f cpu_n=%u gpu_ms_median=%.3f gpu_ms_p95=%.3f gpu_n=%u gpu_status=0x%08lX dropped=%llu invalid=%llu",
            static_cast<unsigned long long>(frameSerial), sample.cpu.median, sample.cpu.p95,
            sample.cpu.count, sample.gpu.median, sample.gpu.p95, sample.gpu.count,
            static_cast<unsigned long>(sample.gpuStatus),
            static_cast<unsigned long long>(sample.dropped),
            static_cast<unsigned long long>(sample.invalid));
    }
    const ULONGLONG now = GetTickCount64();
    if (RuntimeDiagnostics::Enabled() && now - lastGpuPassReport >= 5000) {
        std::string passes, gpuWaits;
        bool fgActive = false, sceneThread = false;
        const std::string cpuWaits = owned::frame_waits::take_cpu_report();
        rendererFacade->Visit([&](owned::Renderer& renderer) {
            passes = renderer.take_gpu_pass_report(&gpuWaits);
            fgActive = renderer.frame_generation_stats().active;
            sceneThread = renderer.scene_mode();
            return S_OK;
        });
        const auto sample = frameTiming.Read();
        if (RuntimeDiagnostics::Enabled()) GameJournal::Event("RENDER", "gpu_passes",
            "backend=%s window_ms=%llu counters=window_totals pass_samples=frames_with_pass frame_timing_scope=tail240 frame_timing_gpu_ms_median=%.3f frame_timing_gpu_ms_p95=%.3f frame_timing_gpu_n=%u %s",
            D3D12Selected() ? "d3d12" : "d3d11", now - lastGpuPassReport,
            sample.gpu.median, sample.gpu.p95, sample.gpu.count, passes.c_str());
        if (RuntimeDiagnostics::Enabled()) GameJournal::Event("RENDER", "frame_waits",
            "backend=%s fg_active_at_report=%u scene_thread_at_report=%u window_ms=%llu %s %s", D3D12Selected() ? "d3d12" : "d3d11", unsigned(fgActive), unsigned(sceneThread),
            now - lastGpuPassReport, cpuWaits.c_str(), gpuWaits.c_str());
        lastGpuPassReport = now;
    }
    IfFailedThrowHR(presented);
}

bool CDX11::FrameGenerationFps(double& output, double& real)
{
    const auto access = ownedRenderer.LockFrame();
    owned::D3D12Frame::FrameGenerationStats stats;
    if (!rendererFacade || FAILED(rendererFacade->Visit([&](owned::Renderer& renderer) {
        stats = renderer.frame_generation_stats();
        return S_OK;
    }))) return false;
    const ULONGLONG now = GetTickCount64();
    const bool ready = stats.active && fpsSampleTime && now > fpsSampleTime &&
        stats.real_presents >= fpsReal && stats.generated_presents >= fpsGenerated;
    if (ready) {
        real = double(stats.real_presents - fpsReal) * 1000.0 / double(now - fpsSampleTime);
        output = real + double(stats.generated_presents - fpsGenerated) * 1000.0 / double(now - fpsSampleTime);
    }
    fpsSampleTime = stats.active ? now : 0;
    fpsReal = stats.real_presents;
    fpsGenerated = stats.generated_presents;
    return ready;
}

void CDX11::ClearColorBuffer(DWORD value)
{
    DirectX::XMFLOAT4 color = BGRAtoFloat4(value);
    IfFailedThrowHR(rendererFacade->Visit([&](owned::Renderer& renderer) {
        return renderer.clear_embedded_targets(reinterpret_cast<const float*>(&color), nullptr);
    }));
}

void CDX11::ClearZBuffer(DWORD dwValue)
{
    float depth = float(dwValue);
    IfFailedThrowHR(rendererFacade->Visit([&](owned::Renderer& renderer) {
        return renderer.clear_embedded_targets(nullptr, &depth);
    }));
}

BOOL CDX11::BindTexture(DWORD dwIndex, ID3D11ShaderResourceView *pTexture,
                          std::uint64_t surfaceId)
{
    bool nativeD3D12 = false;
    if (!rendererFacade || FAILED(rendererFacade->Visit([&](owned::Renderer& renderer) {
        nativeD3D12 = renderer.d3d12_backend();
        return S_OK;
    }))) return FALSE;
    if (nativeD3D12) {
        // Completed CPU surfaces have an identity without a D3D11 SRV.
        // Reject raw SRVs before changing the previously bound texture.
        if (pTexture || dwIndex >= boundFigureTextures.size()) return FALSE;
        if (surfaceId && !OwnedRender::E1SurfaceValue(surfaceId)) return FALSE;
        if (dwIndex == 0) boundParticleSurface = surfaceId;
        else boundSecondarySurface = surfaceId;
        boundFigureTextures[dwIndex].Reset();
        return TRUE;
    }
    if (dwIndex == 0) boundParticleSurface = pTexture ? surfaceId : 0;
    if (dwIndex < boundFigureTextures.size())
        boundFigureTextures[dwIndex] = pTexture;
    if (pTexture)
    {
        this->deviceContext->PSSetShaderResources(dwIndex, 1, &pTexture);
    }
    else
    {
        // disable textures
        ID3D11ShaderResourceView *nullSRV[1] = { nullptr };
        this->deviceContext->PSSetShaderResources(dwIndex, 1, nullSRV);
    };
    return TRUE;
}

void CDX11::SetBackgroundColor(DWORD value)
{
    this->background_color = BGRAtoFloat4(value);
}

void CDX11::SetFogColor(DWORD value)
{
    m_cbs->cb_ps_pixelshader.data.fogColor = BGRAtoFloat4(value);
}

void CDX11::ScreenShot()
{
    m_bMakeScreenShot = true;
}

HRESULT CDX11::BlitPublishedSurface(std::uint64_t surfaceId,
    const RECT& destination, const RECT* source, const DDCOLORKEY* colorKey)
{
    return OwnedRender::E1BlitPublishedSurface(*rendererFacade, device.Get(),
        deviceContext.Get(), surfaceId, destination, source, colorKey, true, true);
}

CDX11::~CDX11()
{
    Video::SetMovieRenderer(nullptr); // Clear all movie sources before the device.
    owned::camp_inventory_shutdown_icons();
    owned::camp_inventory_set_popup_sink(nullptr,nullptr);
    uiColorKey.reset();
    this->spriteBatch.reset();
    this->m_states.reset();

    m_cbs = 0;

    if (this->deviceContext)
    {
        this->deviceContext->ClearState();
        this->deviceContext->Flush();
    }
    this->deviceContext.Reset();
    depthStencilBuffer.Reset();
    depthStencilState.Reset();depthStencilStateDisableWrite.Reset();depthStencilStateDisable.Reset();
    blendState.Reset();rasterizerState.Reset();rasterizerStateNoCull.Reset();
    this->device.Reset();
    OwnedRender::E3CursorStop();
    ownedRenderer.Shutdown();
}

void CDX11::SetGammaCorrection(float brightness, float contrast, float gamma) noexcept {
    const auto access = ownedRenderer.LockFrame();
    if (!rendererFacade || !rendererFacade->Ready()) return;
    const auto bounded = [](float value) {
        return std::isfinite(value) ? std::clamp(value, -1.f, 1.f) * .5f : 0.f;
    };
    brightness = bounded(brightness); contrast = bounded(contrast); gamma = bounded(gamma);
    float slope = std::abs(contrast) * 4.f + 1.f;
    if (contrast < 0.f) slope = 1.f / slope;
    const float offset = (1.f - slope) * .5f;
    const float exponent = gamma == 0.f ? 1.f : 1.f / (1.f + gamma * (gamma > 0.f ? 5.f : .5f));
    // The source game's display ramp: gamma, centred contrast, then brightness.
    // Preserve its float intermediate clamps and truncation to a 16-bit ramp.
    std::array<WORD, 256> ramp{};
    for (unsigned i = 0; i != ramp.size(); ++i) {
        float value = static_cast<float>(std::pow(double(i) * double(1.f / 255.f), exponent) * slope + offset);
        value = std::clamp(value, 0.f, 1.f) + brightness;
        value = std::clamp(value, 0.f, 1.f);
        ramp[i] = static_cast<WORD>(double(value) * 65280.0);
    }
    rendererFacade->Visit([&](owned::Renderer& renderer) {
        renderer.set_gamma_ramp(ramp.data(), ramp.data(), ramp.data());
        return S_OK;
    });
}

bool CDX11::Initialize(HWND hWnd, DWORD dwWidth, DWORD dwHeight)
{
    if (!InitializeDX(hWnd, dwWidth, dwHeight))
        return false;
    SetGammaCorrection(theSettings.fBrightness, theSettings.fContrast, theSettings.fGamma);

    Video::SetMovieRenderer(this);
    return true;
}

bool CDX11::InitializeDX(HWND hWnd, DWORD dwWidth, DWORD dwHeight)
{
    // std::vector<AdapterData> adapters = AdapterReader::GetAdapters();

    /*if (adapters.size() < 1)
    {
        ReportError("No IDXGI Adapters found.");
        return false;
    }*/
    rendererFacade = std::make_shared<OwnedRender::E1RendererFacade>();
    HRESULT hr = rendererFacade->Initialize(hWnd, dwWidth, dwHeight);
    if (FAILED(hr))
    {
        ReportError(hr, "Cannot initialize the embedded c56 renderer.");
        throw hr;
    }
    bool nativeD3D12 = false;
    IfFailedThrowHR(rendererFacade->Visit([&](owned::Renderer& renderer) {
        nativeD3D12 = renderer.d3d12_backend();
        return S_OK;
    }));
    if (!nativeD3D12) {
        IfFailedThrowHR(rendererFacade->VisitDevice(this,
            [](void* user, const owned::EmbeddedDeviceView& view) -> HRESULT {
                auto& self = *static_cast<CDX11*>(user);
                self.device = view.device;
                self.deviceContext = view.context;
                self.m_width = view.width; self.m_height = view.height;
                return OwnedRender::E1BindTargets(nullptr, view);
            }));
    }
    ownedRenderer.embeddedUi = [this](ID3D11ShaderResourceView* image, const RECT& destination,
                                      const RECT* source, unsigned flags) -> HRESULT {
        if (!rendererFacade) return S_FALSE;
        return rendererFacade->Visit([&](owned::Renderer& renderer) {
            return renderer.blit_srv(image, destination, source, (flags & EI_RENDERER_UI_OPAQUE) != 0);
        });
    };
    HRESULT timingAttached = S_FALSE;
    if (nativeD3D12) {
        IfFailedThrowHR(rendererFacade->Visit([&](owned::Renderer& renderer) {
            timingAttached = frameTiming.AttachNative(renderer.native_profile_timestamps());
            return S_OK;
        }));
    } else timingAttached = frameTiming.Attach(device.Get(), deviceContext.Get());
    GameJournal::Event("RENDER", "frame_owner",
        "frame=c56 hwnd=%p menu_panels=c56-blit world_terrain=c56 world_figures=c56 game_particles=c56 visfog=c56 fog=c56 triangles=c56 effects=category-migration shadows=c56 timing_gpu=0x%08lX",
        hWnd, static_cast<unsigned long>(timingAttached));
    IfFailedThrowHR(rendererFacade->Visit([&](owned::Renderer& renderer) {
        UINT width = 0, height = 0;
        const HRESULT result = renderer.read_embedded_output_size(width, height);
        if (SUCCEEDED(result)) { m_width = width; m_height = height; }
        return result;
    }));
    dwWidth = m_width; dwHeight = m_height;

    // Native D3D12 submissions translate fixed state inside the renderer.
    // Legacy state objects and context operations belong to D3D11 only.
    if (!nativeD3D12) {
        // Create Blend State
        D3D11_RENDER_TARGET_BLEND_DESC rtbd = { 0 };
        rtbd.BlendEnable = true;
        // dst (render target), src (pixel shader) pixels
        // dst.rgb = src.a * src.rgb + (1 - src.a) * dst.rgb
        rtbd.SrcBlend = D3D11_BLEND::D3D11_BLEND_SRC_ALPHA;
        rtbd.DestBlend = D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA;
        rtbd.BlendOp = D3D11_BLEND_OP::D3D11_BLEND_OP_ADD;

        // dst.a = 1 * src.a + 0 * dst.a;
        rtbd.SrcBlendAlpha = D3D11_BLEND::D3D11_BLEND_ONE;
        rtbd.DestBlendAlpha = D3D11_BLEND::D3D11_BLEND_ZERO;
        rtbd.BlendOpAlpha = D3D11_BLEND_OP::D3D11_BLEND_OP_ADD;
        rtbd.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE::D3D11_COLOR_WRITE_ENABLE_ALL;

        D3D11_BLEND_DESC blendDesc = { 0 };
        blendDesc.RenderTarget[0] = rtbd;
        IfFailedThrowHR(this->device->CreateBlendState(&blendDesc, this->blendState.GetAddressOf()));

        // Create depth stencil state
        D3D11_DEPTH_STENCIL_DESC depthstencildesc;
        ZeroMemory(&depthstencildesc, sizeof(D3D11_DEPTH_STENCIL_DESC));
        depthstencildesc.DepthEnable = true;
        depthstencildesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK::D3D11_DEPTH_WRITE_MASK_ALL;
        depthstencildesc.DepthFunc = G_DEPTH_FUNC;

        IfFailedThrowHR(
            this->device->CreateDepthStencilState(&depthstencildesc, depthStencilState.GetAddressOf()));

        ZeroMemory(&depthstencildesc, sizeof(D3D11_DEPTH_STENCIL_DESC));
        depthstencildesc.DepthEnable = true;
        depthstencildesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK::D3D11_DEPTH_WRITE_MASK_ZERO;
        depthstencildesc.DepthFunc = G_DEPTH_FUNC;

        IfFailedThrowHR(
            this->device->CreateDepthStencilState(&depthstencildesc,
                                                  this->depthStencilStateDisableWrite.GetAddressOf()));

        ZeroMemory(&depthstencildesc, sizeof(D3D11_DEPTH_STENCIL_DESC));
        depthstencildesc.DepthEnable = true;
        depthstencildesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK::D3D11_DEPTH_WRITE_MASK_ZERO;
        depthstencildesc.DepthFunc = D3D11_COMPARISON_FUNC::D3D11_COMPARISON_ALWAYS;
        IfFailedThrowHR(
            this->device->CreateDepthStencilState(&depthstencildesc,
                                                  this->depthStencilStateDisable.GetAddressOf()));

        // Create the Viewport
        D3D11_VIEWPORT viewport;
        ZeroMemory(&viewport, sizeof(D3D11_VIEWPORT));

        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        viewport.Width = dwWidth;
        viewport.Height = dwHeight;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        // Set the Viewport
        this->deviceContext->RSSetViewports(1, &viewport);

        // Create Rasterizer State
        D3D11_RASTERIZER_DESC rasterizerDesc;
        ZeroMemory(&rasterizerDesc, sizeof(D3D11_RASTERIZER_DESC));

        rasterizerDesc.FillMode = D3D11_FILL_MODE::D3D11_FILL_SOLID;
        bool ccw = true;
        rasterizerDesc.FrontCounterClockwise = ccw;
        rasterizerDesc.CullMode = G_CULL_MODE;
        IfFailedThrowHR(
            this->device->CreateRasterizerState(&rasterizerDesc, this->rasterizerState.GetAddressOf()));

        ZeroMemory(&rasterizerDesc, sizeof(D3D11_RASTERIZER_DESC));
        rasterizerDesc.FillMode = D3D11_FILL_MODE::D3D11_FILL_SOLID;
        rasterizerDesc.FrontCounterClockwise = ccw;
        rasterizerDesc.CullMode = D3D11_CULL_MODE::D3D11_CULL_NONE;
        IfFailedThrowHR(
            this->device->CreateRasterizerState(&rasterizerDesc,
                                                this->rasterizerStateNoCull.GetAddressOf()));

    }

    spriteBatch = std::make_unique<OwnedSpriteBatch>(ownedRenderer);
    // spriteBatch->Begin();
    // spriteFont = std::make_unique<DirectX::SpriteFont>(this->device.Get(),
    // L"Data\\comic_sans_ms_16.spritefont"); spriteFont->SetDefaultCharacter(L'Z');

    if (!nativeD3D12) m_states = std::make_unique<DirectX::CommonStates>(this->device.Get());
    return true;
}

void CDX11::MakeScreenShot()
{
    bool nativeD3D12 = false;
    std::vector<std::uint8_t> pixels;
    UINT width = 0, height = 0;
    HRESULT hr = rendererFacade->Visit([&](owned::Renderer& renderer) {
        nativeD3D12 = renderer.d3d12_backend();
        return nativeD3D12 ? renderer.read_embedded_d3d12_output(pixels, width, height) : S_OK;
    });
    if (nativeD3D12) {
        if (hr == S_OK) {
            DirectX::Image image{};
            image.width = width; image.height = height;
            image.format = DXGI_FORMAT_B8G8R8A8_UNORM;
            image.rowPitch = std::size_t(width) * 4u;
            image.slicePitch = pixels.size(); image.pixels = pixels.data();
            hr = DirectX::SaveToWICFile(image, DirectX::WIC_FLAGS_NONE,
                GUID_ContainerFormatJpeg,
                (RuntimePaths64::Directory(L"Screenshots64") / L"SCREENSHOT.JPG").c_str());
        }
        if (hr != S_OK) GameJournal::Event("RENDER", "screenshot_failed",
            "backend=d3d12 hr=%08lx", static_cast<unsigned long>(hr));
        return;
    }
    if (FAILED(hr)) return;
    // Capture the composed output while its renderer-owned lease is valid.
    rendererFacade->VisitOutput(nullptr,
        [](void*, ID3D11DeviceContext* context, ID3D11Texture2D* output) -> HRESULT {
            return DirectX::SaveWICTextureToFile(context, output,
                GUID_ContainerFormatJpeg, (RuntimePaths64::Directory(L"Screenshots64") / L"SCREENSHOT.JPG").c_str());
        });
}

void CDX11::UpdateTexture(ID3D11Texture2D *texture, UINT subresource_index, D3D11_BOX *region,
                          D3D11_SUBRESOURCE_DATA *srd)
{
    //...well that's embarrassing
    //...reads <texture height> amount of rows...
    this->deviceContext->UpdateSubresource(texture, subresource_index, region, srd->pSysMem,
                                           srd->SysMemPitch, srd->SysMemSlicePitch);
}

void CDX11::DefaultSurfaceDesc(D3D11_TEXTURE2D_DESC *ptex2d)
{
    ZeroMemory(ptex2d, sizeof(*ptex2d));
    D3D11_TEXTURE2D_DESC &tex2d = *ptex2d;
    tex2d.Usage = D3D11_USAGE_DEFAULT;
    tex2d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    tex2d.MipLevels = 1;
    tex2d.ArraySize = 1;
    tex2d.SampleDesc.Count = 1;
    tex2d.SampleDesc.Quality = 0;
    tex2d.CPUAccessFlags = 0;
    tex2d.MiscFlags = 0;
}

void CDX11::CreateShaderResource(Microsoft::WRL::ComPtr<ID3D11Texture2D> texture,
                                 Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> &textureView)
{
    IfFailedThrowHR(this->device->CreateShaderResourceView(texture.Get(), nullptr,
                                                           textureView.ReleaseAndGetAddressOf()));
}

void CDX11::CreateTexture(D3D11_TEXTURE2D_DESC *ptex2d,
                          Microsoft::WRL::ComPtr<ID3D11Texture2D> &texture)
{
    IfFailedThrowHR(this->device->CreateTexture2D(ptex2d, 0, texture.ReleaseAndGetAddressOf()));
}

