#include "stdafx.h"
#include <vector>;
#include <SimpleMath.h>

#include <wincodec.h> // Optional
#include <ScreenGrab.h>

#include "ConstantBuffer.h"
#include "RendererDX11.h"
#include "embedded_hud_visual.h"
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

#include "Utils.h"
#include "Vertex.h"
#include "../Renderer.h"
#include "../OwnedRender/E1VisualFrame.h"
#include "../OwnedRender/E4Camp.h"

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d3d11.lib")

using Microsoft::WRL::ComPtr;

float G_CLEAR_DEPTH_VALUE = 1.0;
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
    D3D11_VIEWPORT viewport{}; UINT count = 1;
    deviceContext->RSGetViewports(&count, &viewport);
    if (count != 1 || viewport.TopLeftX < 0 || viewport.TopLeftY < 0 ||
        viewport.Width < 1 || viewport.Height < 1) return {};
    state.replay_frame_state = true;
    state.captured_viewport = {UINT(viewport.TopLeftX), UINT(viewport.TopLeftY),
        UINT(viewport.Width), UINT(viewport.Height)};
    state.captured_depth_range = {viewport.MinDepth, viewport.MaxDepth};
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
    D3D11_VIEWPORT viewport{}; UINT count = 1;
    deviceContext->RSGetViewports(&count, &viewport);
    state.viewport = {DWORD(viewport.TopLeftX), DWORD(viewport.TopLeftY),
        DWORD(viewport.Width), DWORD(viewport.Height), viewport.MinDepth, viewport.MaxDepth};
    const auto& pixel = GetConstantBuffers()->cb_ps_pixelshader.data;
    const bool textured = bool(boundFigureTextures[0]);
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
        hr = rendererFacade->Visit([&](owned::Renderer& renderer) {
            return renderer.draw_embedded_fixed_function(type, vertices, count,
                indices, indexCount, state, texture);
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
    if (OwnedRender::E4::OwnsDraw() ||
        (ownedRenderer.FigureActive() && ownedRenderer.FigureValues().object_id == 0))
        state.render[D3DRENDERSTATE_FOGENABLE] = FALSE;
    state.render[D3DRENDERSTATE_SPECULARENABLE] = (model.flags & 8u) ? TRUE : FALSE;
    const auto& pixel = GetConstantBuffers()->cb_ps_pixelshader.data;
    auto* primary = ownedRenderer.FigureActive() ? ownedRenderer.FigurePrimaryTexture() : boundFigureTextures[0].Get();
    auto* secondary = ownedRenderer.FigureActive() ? ownedRenderer.FigureSecondaryTexture() : boundFigureTextures[1].Get();
    state.stage[0][D3DTSS_COLOROP] = primary ? D3DTOP_MODULATE : D3DTOP_SELECTARG2;
    state.stage[0][D3DTSS_COLORARG1] = D3DTA_TEXTURE; state.stage[0][D3DTSS_COLORARG2] = D3DTA_DIFFUSE;
    state.stage[0][D3DTSS_ALPHAOP] = !primary ? D3DTOP_SELECTARG2 : pixel.alphaMode == 0 ?
        D3DTOP_SELECTARG1 : pixel.alphaMode == 2 ? D3DTOP_SELECTARG2 : D3DTOP_MODULATE;
    state.stage[0][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE; state.stage[0][D3DTSS_ALPHAARG2] = D3DTA_DIFFUSE;
    state.stage[0][D3DTSS_ADDRESSU] = state.stage[0][D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
    state.stage[0][D3DTSS_MINFILTER] = state.stage[0][D3DTSS_MAGFILTER] = D3DTFG_LINEAR;
    state.stage[0][D3DTSS_MIPFILTER] = D3DTFP_LINEAR;
    const auto byte = [](float v) {return DWORD((std::max)(0.f, (std::min)(1.f, v))*255.f+.5f);};
    state.render[D3DRENDERSTATE_FOGCOLOR] = (byte(pixel.fogColor.x)<<16) |
        (byte(pixel.fogColor.y)<<8) | byte(pixel.fogColor.z);
    const HRESULT hr = rendererFacade->Visit([&](owned::Renderer& renderer) {
        if (camp && !lighting && !secondary) {
            const HRESULT flushed = renderer.has_pending_ui() ? renderer.flush_ui() : S_OK;
            if (FAILED(flushed)) return flushed;
            owned::Renderer::NativeQueueCheckpoint checkpoint;
            if (!renderer.checkpoint_native(checkpoint)) return E_FAIL;
            const HRESULT queued = renderer.queue_native_direct(model, state, primary);
            if (FAILED(queued)) renderer.rollback_native(checkpoint);
            return queued;
        }
        return renderer.queue_embedded_figure(model, state, primary, secondary, lighting, lights);
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
        if (!boundFigureTextures[0]) {
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
        hr = boundFigureTextures[0] ? OwnedRender::E1ParticleSurfaceImage(device.Get(),
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
            RefreshOwnedTargets();
            SetRenderStates();
            for (UINT stage = 0; stage < boundFigureTextures.size(); ++stage) {
                ID3D11ShaderResourceView* resource = boundFigureTextures[stage].Get();
                deviceContext->PSSetShaderResources(stage, 1, &resource);
            }
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
    if (SUCCEEDED(hr) && useTexture && boundFigureTextures[0])
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
            if (boundFigureTextures[0]) hr = OwnedRender::E1ParticleSurfaceImage(device.Get(),
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

void CDX11::BeginFrame()
{
    ReportC56FigureSkipSummary();
    // c73 separates authoritative engine frames from D3D scene brackets.
    // Reopen world composition and advance per-frame caches before BeginScene
    // or any clear/draw; Present intentionally leaves the prior frame sealed.
    IfFailedThrowHR(rendererFacade->Visit([](owned::Renderer& renderer) {
        return renderer.begin_engine_frame();
    }));
    IfFailedThrowHR(rendererFacade->BeginFrame());
    owned::bridge_set_screen_rectangle_sink(this, &SubmitScreenRectangleToC56);
    frameTiming.Begin();
    RefreshOwnedTargets();
    IfFailedThrowHR(rendererFacade->VisitDevice(&background_color,
        [](void *user, const owned::EmbeddedDeviceView& view) -> HRESULT {
            const auto *color = static_cast<const DirectX::XMFLOAT4 *>(user);
            view.context->ClearRenderTargetView(view.color, reinterpret_cast<const float *>(color));
            view.context->ClearDepthStencilView(view.depth,
                D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, G_CLEAR_DEPTH_VALUE, 0);
            return S_OK;
        }));
    SetRenderStates();
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
    for (UINT stage = 0; stage < boundFigureTextures.size(); ++stage) {
        ID3D11ShaderResourceView* resource = boundFigureTextures[stage].Get();
        deviceContext->PSSetShaderResources(stage, 1, &resource);
    }
    const int state = FAILED(pass) ? 3 : visuals ? (owned ? 2 : 1) : 0;
    if (state != c56WorldPassState) {
        c56WorldPassState = state;
        GameJournal::Event("RENDER", "world_pass",
            "owner=c56 owned_visual_frame=%s hr=%08lx", state == 2 ? "drawn" : state == 0 ? "not_requested" : "rejected", static_cast<unsigned long>(pass));
    }
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
    IfFailedThrowHR(composite);
    ReportC56FigureSkipSummary();
    GameJournal::Event("RENDER", "world_model_owner",
        "owner=c56 path=c56_native_queue submitted=%llu skipped=%llu",
        static_cast<unsigned long long>(c56WorldFigurePrimitives),
        static_cast<unsigned long long>(c56WorldFigureQueueFailed));
    c56WorldFigurePrimitives = 0;
    c56WorldFigureQueueFailed = 0;
    ownedWorldActive = false;
    RefreshOwnedTargets();
    SetRenderStates();
}

void CDX11::RefreshOwnedTargets()
{
    IfFailedThrowHR(rendererFacade->VisitDevice(nullptr, &OwnedRender::E1BindTargets));
}

void CDX11::EndFrame(bool present)
{
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
    FrameDiagnostics::Presented(presented);
    if (SUCCEEDED(presented) && (++frameSerial % 120u) == 0) {
        const auto sample = frameTiming.Read();
        GameJournal::Event("RENDER", "frame_timing",
            "owner=c56 frame=%llu cpu_ms_median=%.3f cpu_ms_p95=%.3f cpu_n=%u gpu_ms_median=%.3f gpu_ms_p95=%.3f gpu_n=%u gpu_status=0x%08lX dropped=%llu invalid=%llu",
            static_cast<unsigned long long>(frameSerial), sample.cpu.median, sample.cpu.p95,
            sample.cpu.count, sample.gpu.median, sample.gpu.p95, sample.gpu.count,
            static_cast<unsigned long>(sample.gpuStatus),
            static_cast<unsigned long long>(sample.dropped),
            static_cast<unsigned long long>(sample.invalid));
    }
    IfFailedThrowHR(presented);
}

void CDX11::ClearColorBuffer(DWORD value)
{
    DirectX::XMFLOAT4 color = BGRAtoFloat4(value);
    IfFailedThrowHR(rendererFacade->VisitDevice(&color, &OwnedRender::E1ClearColor));
}

void CDX11::ClearZBuffer(DWORD dwValue)
{
    float depth = float(dwValue);
    IfFailedThrowHR(rendererFacade->VisitDevice(&depth, &OwnedRender::E1ClearDepth));
}

BOOL CDX11::BindTexture(DWORD dwIndex, ID3D11ShaderResourceView *pTexture,
                          std::uint64_t surfaceId)
{
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

bool CDX11::Initialize(HWND hWnd, DWORD dwWidth, DWORD dwHeight)
{
    if (!InitializeDX(hWnd, dwWidth, dwHeight))
        return false;

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
    IfFailedThrowHR(rendererFacade->VisitDevice(this,
        [](void* user, const owned::EmbeddedDeviceView& view) -> HRESULT {
            auto& self = *static_cast<CDX11*>(user);
            self.device = view.device;
            self.deviceContext = view.context;
            self.m_width = view.width; self.m_height = view.height;
            return OwnedRender::E1BindTargets(nullptr, view);
        }));
    ownedRenderer.embeddedUi = [this](ID3D11ShaderResourceView* image, const RECT& destination,
                                      const RECT* source, unsigned flags) -> HRESULT {
        if (!rendererFacade) return S_FALSE;
        return rendererFacade->Visit([&](owned::Renderer& renderer) {
            return renderer.blit_srv(image, destination, source, (flags & EI_RENDERER_UI_OPAQUE) != 0);
        });
    };
    const HRESULT timingAttached = frameTiming.Attach(device.Get(), deviceContext.Get());
    GameJournal::Event("RENDER", "frame_owner",
        "frame=c56 hwnd=%p menu_panels=c56-blit world_terrain=c56 world_figures=c56 game_particles=c56 visfog=c56 fog=c56 triangles=c56 effects=category-migration shadows=c56 timing_gpu=0x%08lX",
        hWnd, static_cast<unsigned long>(timingAttached));
    IfFailedThrowHR(rendererFacade->VisitOutput(this,
        [](void* user, ID3D11DeviceContext*, ID3D11Texture2D* output) -> HRESULT {
            D3D11_TEXTURE2D_DESC desc{}; output->GetDesc(&desc);
            if (!desc.Width || !desc.Height) return E_INVALIDARG;
            auto& self = *static_cast<CDX11*>(user);
            self.m_width = desc.Width; self.m_height = desc.Height;
            return S_OK;
        }));
    dwWidth = m_width; dwHeight = m_height;

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

    spriteBatch = std::make_unique<OwnedSpriteBatch>(ownedRenderer);
    // spriteBatch->Begin();
    // spriteFont = std::make_unique<DirectX::SpriteFont>(this->device.Get(),
    // L"Data\\comic_sans_ms_16.spritefont"); spriteFont->SetDefaultCharacter(L'Z');

    m_states = std::make_unique<DirectX::CommonStates>(this->device.Get());
    return true;
}

void CDX11::MakeScreenShot()
{
    // Capture the composed output while its renderer-owned lease is valid.
    rendererFacade->VisitOutput(nullptr,
        [](void*, ID3D11DeviceContext* context, ID3D11Texture2D* output) -> HRESULT {
            return DirectX::SaveWICTextureToFile(context, output,
                GUID_ContainerFormatJpeg, L"SCREENSHOT.JPG");
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

