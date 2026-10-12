#include "stdafx.h"
#include <vector>;
#include <SimpleMath.h>

#include <wincodec.h> // Optional
#include <ScreenGrab.h>

#include "ConstantBuffer.h"
#include "DXShader.h"
#include "RendererDX11.h"
#include "../OwnedRender/E1SurfaceBridge.h"
#include "../OwnedRender/E1RendererFacade.h"
#include "../OwnedRender/E1SurfaceFacade.h"
#include "../FrameDiagnostics.h"
#include "ModShadowDX11.h"
#include "ModWorldBackgroundDX11.h"
#include "UIColorKeyBlit.h"
#include "../OwnedRender/E1TerrainState.h"
#include "../OwnedRender/E1Terrain.h"
#include "../OwnedRender/E1TerrainSubmission.h"
#include "../GameJournal.h"

#include "batchers.h"
#include "Utils.h"
#include "Vertex.h"
#include "EffectProcessor.h"
#include "../Renderer.h"

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d3d11.lib")

using Microsoft::WRL::ComPtr;

float G_CLEAR_DEPTH_VALUE = 1.0;
D3D11_CULL_MODE G_CULL_MODE = D3D11_CULL_MODE::D3D11_CULL_BACK;
D3D11_COMPARISON_FUNC G_DEPTH_FUNC = D3D11_COMPARISON_FUNC::D3D11_COMPARISON_LESS_EQUAL;
const int G_BUFFER_QUALITY = 0; // default 0
const int G_BUFFER_SAMPLES = 1; // default 1
const int G_VSYNC = 0;

bool CDX11::RenderStateCheck()
{
    bool res = fake_dx7.render_state == fake_dx7_prev.render_state;
    if (!res)
    {
        SetRenderStates();
    }
    fake_dx7_prev.render_state = fake_dx7.render_state;
    return res;
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
    bool alpha_test_enable = rs[D3DRENDERSTATE_ALPHATESTENABLE];
    float alpharef = rs[D3DRENDERSTATE_ALPHAREF] / 256.;

    bool useSpecular = rs[D3DRENDERSTATE_SPECULARENABLE];
    this->GetConstantBuffers()->cb_ps_pixelshader.data.useSpecular = useSpecular;

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
    this->SetAlphaRef(alpha_test_enable ? alpharef : 0);
    if(modShadows && modShadows->active)modShadows->Bind();
    return true;
}


int CDX11::DrawParticles(LPVOID pVertices, DWORD dwVertexCount, LPWORD pwIndices,
                         DWORD dwIndexCount, DWORD dwFlags)
{
    // Need additional scale in projection space (screen_dx)
    if (!dwIndexCount)
        return 0;
    RenderStateCheck();

    std::shared_ptr<VertexBuffer<ParticleVertex>> vertexBuf = m_caches->CreateOrUpdate(
        static_cast<ParticleVertex *>(pVertices), dwVertexCount);

    this->deviceContext->IASetInputLayout(m_shaders->vs_particles.GetInputLayout());
    this->deviceContext->VSSetShader(m_shaders->vs_particles.GetShader(), NULL, 0);
    return DrawIndexedPrimitive(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, vertexBuf.get(), pwIndices,
                                dwIndexCount);
}

int CDX11::DrawSimplePrimitive(LPVOID pVertices, DWORD dwVertexCount,
                               LPWORD pIndices, DWORD dwIndexCount, DWORD dwFlags)
{
    //Terrain, Prints, FHP - no extra lighting
    if (!dwIndexCount)
        return 0;
    RenderStateCheck();
    
    std::shared_ptr<VertexBuffer<STLVertex1>> vertexBuf = m_caches->CreateOrUpdate(
        static_cast<STLVertex1 *>(pVertices), dwVertexCount);

    this->deviceContext->IASetInputLayout(m_shaders->vs_simple.GetInputLayout());
    this->deviceContext->VSSetShader(m_shaders->vs_simple.GetShader(), NULL, 0);
    return DrawIndexedPrimitive(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, vertexBuf.get(), pIndices,
                                dwIndexCount);
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
    state.captured_fog_enable = fog.valid && fog.enabled;
    const auto& color = GetConstantBuffers()->cb_ps_pixelshader.data.fogColor;
    const auto byte = [](float v) { return DWORD((std::max)(0.f, (std::min)(1.f, v)) * 255.f + .5f); };
    state.captured_fog_color = (byte(color.x) << 16) | (byte(color.y) << 8) | byte(color.z);
    return state;
}

bool CDX11::SubmitOwnedTerrain(EIRenderTerrainFrameV5 packet)
{
    unsigned width=0,height=0;
    if(!ownedWorldActive || (modShadows&&modShadows->active) || GetTerrainShadowSize(width,height))return false;
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

bool CDX11::SubmitC56Figure(const owned::NativeGeometryView& view)
{
    const auto fallback = [this](std::uint64_t& reason) {
        ++c56WorldFigureFallbacks;
        ++reason;
        return false;
    };
    if (!ownedWorldActive || !rendererFacade || !rendererFacade->Ready())
        return fallback(c56WorldFigureUnavailable);
    if (!view.positions || !view.normals || !view.components || !view.indices)
        return fallback(c56WorldFigureInvalidView);
    ID3D11ShaderResourceView* primary = ownedRenderer.FigurePrimaryTexture();
    ID3D11ShaderResourceView* secondary = ownedRenderer.FigureSecondaryTexture();
    if (secondary) return fallback(c56WorldFigureMultipleTextures);
    owned::FixedState state{};
    for (const auto& item : fake_dx7.render_state)
        if (item.first >= 0 && static_cast<std::size_t>(item.first) < state.render.size())
            state.render[item.first] = item.second;
    const auto alphaMode = GetConstantBuffers()->cb_ps_pixelshader.data.alphaMode;
    state.stage[0][D3DTSS_COLOROP] = D3DTOP_MODULATE;
    state.stage[0][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
    state.stage[0][D3DTSS_COLORARG2] = D3DTA_DIFFUSE;
    state.stage[0][D3DTSS_ALPHAOP] = alphaMode == 0 ? D3DTOP_SELECTARG1 :
        alphaMode == 2 ? D3DTOP_SELECTARG2 : D3DTOP_MODULATE;
    state.stage[0][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
    state.stage[0][D3DTSS_ALPHAARG2] = D3DTA_DIFFUSE;
    const HRESULT hr = rendererFacade->QueueNativeDirect(view, state, primary);
    {
        // RS02c: one-time sample per category (white characters). Remove when settled.
        static unsigned sampled[3] = {};
        const EIRenderFigureV4 figure = ownedRenderer.FigureValues();
        const unsigned category = figure.category < 3 ? figure.category : 2;
        if (sampled[category] < 4) {
            ++sampled[category];
            D3D11_TEXTURE2D_DESC desc{};
            D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
            if (primary) {
                primary->GetDesc(&srv);
                Microsoft::WRL::ComPtr<ID3D11Resource> resource;
                primary->GetResource(&resource);
                Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
                if (resource && SUCCEEDED(resource.As(&texture))) texture->GetDesc(&desc);
            }
            GameJournal::Event("RENDER", "figure_sample",
                "category=%u hr=%08lx srv=%d fmt=%u dim=%u size=%ux%u mips=%u array=%u flags=%u morphs=%u light=%u diffuse=%.3f,%.3f,%.3f emissive=%.3f,%.3f,%.3f specular=%.3f,%.3f,%.3f,%.3f alpha=%.3f alphaMode=%u",
                category, static_cast<unsigned long>(hr), primary ? 1 : 0,
                static_cast<unsigned>(desc.Format), static_cast<unsigned>(srv.ViewDimension),
                desc.Width, desc.Height, desc.MipLevels, desc.ArraySize, view.flags,
                view.morph_count, view.lighting.enabled,
                view.lighting.material_diffuse[0], view.lighting.material_diffuse[1], view.lighting.material_diffuse[2],
                view.lighting.emissive[0], view.lighting.emissive[1], view.lighting.emissive[2],
                view.lighting.specular[0], view.lighting.specular[1], view.lighting.specular[2], view.lighting.specular[3],
                view.lighting.material_alpha, static_cast<unsigned>(alphaMode));
        }
    }
    if (SUCCEEDED(hr)) { ++c56WorldFigurePrimitives; return true; }
    return fallback(c56WorldFigureQueueFailed);
}

int CDX11::DrawFigurePrimitive(LPVOID pVertices, DWORD dwVertexCount, LPWORD pIndices, DWORD dwIndexCount,
                  DWORD dwFlags)
{
    if (!dwIndexCount)
        return 0;
    if (ownedWorldActive)
        ++c56WorldFigureLegacyDraws;
    RenderStateCheck();

    std::shared_ptr<VertexBuffer<PNTVertex>> vertexBuf = m_caches->CreateOrUpdate(static_cast<PNTVertex *>(pVertices), dwVertexCount);

    this->deviceContext->IASetInputLayout(m_shaders->vs_main.GetInputLayout());
    this->deviceContext->VSSetShader(m_shaders->vs_main.GetShader(), NULL, 0);
    return DrawIndexedPrimitive(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, vertexBuf.get(), pIndices,
                                dwIndexCount);
}

template <typename TVertexType>
int CDX11::DrawIndexedPrimitive(D3D11_PRIMITIVE_TOPOLOGY primitive,
                                VertexBuffer<TVertexType> *vbVertices, LPWORD pIndices,
                                DWORD dwIndexCount)
{
    std::shared_ptr<VertexBuffer<WORD>> idxBuf = m_caches->CreateOrUpdate(pIndices, dwIndexCount);

    this->deviceContext->IASetPrimitiveTopology(primitive);
    this->deviceContext->PSSetSamplers(0, 1, this->samplerState.GetAddressOf());
    this->deviceContext->PSSetSamplers(1, 1, this->samplerState.GetAddressOf());
    if(!modShadows || !modShadows->active)
        this->deviceContext->PSSetShader(m_shaders->ps_main.GetShader(), NULL, 0);

    // update const buffer
    m_cbs->cb_vs_vertexshader.ApplyChanges();
    m_cbs->cb_vs_vertexlights.ApplyChanges();
    m_cbs->cb_ps_pixelshader.ApplyChanges();

    this->deviceContext->VSSetConstantBuffers(
        0, 1, GetConstantBuffers()->cb_vs_vertexshader.GetAddressOf());
    this->deviceContext->VSSetConstantBuffers(
        1, 1, GetConstantBuffers()->cb_vs_vertexlights.GetAddressOf());
    this->deviceContext->PSSetConstantBuffers(
        0, 1, GetConstantBuffers()->cb_ps_pixelshader.GetAddressOf());
    UINT offset = 0;
    this->deviceContext->IASetVertexBuffers(0, 1, vbVertices->GetAddressOf(),
                                            vbVertices->StridePtr(), &offset);
    this->deviceContext->IASetIndexBuffer(idxBuf->Get(), DXGI_FORMAT_R16_UINT, 0);
    if(modShadows && modShadows->terrainPass)modShadows->BindTerrainResource();
    if(ownedWorldActive && (!modShadows||!modShadows->active))
        IfFailedThrowHR(ownedRenderer.BindWorldPixels(theRenderer.IsAddFog()!=0));
    this->deviceContext->DrawIndexed(dwIndexCount, 0, 0);
    const uint32_t triangles = primitive == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST ? dwIndexCount / 3 :
        primitive == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP && dwIndexCount > 2 ? dwIndexCount - 2 : 0;
    IfFailedThrowHR(ownedRenderer.RecordDraw(triangles));
    return TRV_VISIBLE_NOCLIP;
}

template <typename TVertexType>
int CDX11::DrawPrimitive(D3D11_PRIMITIVE_TOPOLOGY primitive, VertexBuffer<TVertexType> *vbVertices)
{
    return 0;
}

void CDX11::BeginFrame()
{
    IfFailedThrowHR(rendererFacade->BeginFrame());
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
    c56WorldFigureFallbacks = 0;
    c56WorldFigureLegacyDraws = 0;
    c56WorldFigureUnavailable = 0;
    c56WorldFigureInvalidView = 0;
    c56WorldFigureMultipleTextures = 0;
    c56WorldFigureQueueFailed = 0;
    std::copy_n(frame.world_to_clip, ownedWorldToClip.size(), ownedWorldToClip.begin());
    c56WorldPassRan = false;
    IfFailedThrowHR(ownedRenderer.BeginWorld(frame, skyConsumed));
    ownedWorldActive = true;
    RefreshOwnedTargets();
    SetRenderStates();
    return skyConsumed != 0;
}

void CDX11::RunC56WorldPass()
{
    if (!ownedWorldActive || c56WorldPassRan || !rendererFacade)
        return;
    owned::FixedState frame;
    for (const auto& value : fake_dx7.render_state)
        if (value.first >= 0 && static_cast<std::size_t>(value.first) < frame.render.size())
            frame.render[value.first] = value.second;
    const HRESULT pass = rendererFacade->Visit([&](owned::Renderer& renderer) {
        return renderer.flush_native_static(frame, ownedWorldToClip);
    });
    IfFailedThrowHR(pass);
    c56WorldPassRan = true;
    GameJournal::Event("RENDER", "world_pass",
        "owner=c56 path=flush_native_static position=after_scene_publication before=transparent_terrain,world_effects missing=shadow_casters,game_particles");
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
    // Finish the same c56 world pass before the caller starts drawing the
    // interface. composite_world drains queued native geometry and performs
    // the renderer-owned world composition; Present remains the frame owner.
    const HRESULT composite = rendererFacade->Visit([](owned::Renderer& renderer) {
        return renderer.composite_world();
    });
    IfFailedThrowHR(composite);
    GameJournal::Event("RENDER", "world_model_owner",
        "owner=c56_device path=c56_native_queue submitted=%llu legacy_draws=%llu submission_fallbacks=%llu fallback_unavailable=%llu fallback_invalid_view=%llu fallback_multiple_textures=%llu fallback_queue_failed=%llu missing=%s",
        static_cast<unsigned long long>(c56WorldFigurePrimitives),
        static_cast<unsigned long long>(c56WorldFigureLegacyDraws),
        static_cast<unsigned long long>(c56WorldFigureFallbacks),
        static_cast<unsigned long long>(c56WorldFigureUnavailable),
        static_cast<unsigned long long>(c56WorldFigureInvalidView),
        static_cast<unsigned long long>(c56WorldFigureMultipleTextures),
        static_cast<unsigned long long>(c56WorldFigureQueueFailed),
        c56WorldFigureFallbacks ? "figure" : "none");
    c56WorldFigurePrimitives = 0;
    c56WorldFigureFallbacks = 0;
    c56WorldFigureLegacyDraws = 0;
    c56WorldFigureUnavailable = 0;
    c56WorldFigureInvalidView = 0;
    c56WorldFigureMultipleTextures = 0;
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

BOOL CDX11::BindTexture(DWORD dwIndex, ID3D11ShaderResourceView *pTexture)
{
    GetConstantBuffers()->cb_ps_pixelshader.data.isTexture[dwIndex].x = pTexture ? 1 : 0;
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
    modBackground.reset();modShadows.reset();
    m_effproc.reset();m_caches.reset();m_batchers.reset();
    uiColorKey.reset();
    this->spriteBatch.reset();
    this->spriteFont.reset();
    this->m_states.reset();

    m_cbs = 0;
    m_shaders = 0;

    if (this->deviceContext)
    {
        this->deviceContext->ClearState();
        this->deviceContext->Flush();
    }
    this->deviceContext.Reset();
    // ReportLiveObjects();
    depthStencilBuffer.Reset();
    depthStencilState.Reset();depthStencilStateDisableWrite.Reset();depthStencilStateDisable.Reset();
    blendState.Reset();blendStateDisable.Reset();rasterizerState.Reset();rasterizerStateNoCull.Reset();
    samplerState.Reset();blackTextureView.Reset();blackTexture.Reset();myTexture.Reset();myTexture2.Reset();
    this->device.Reset();
    ownedRenderer.Shutdown();
}

bool CDX11::Initialize(HWND hWnd, DWORD dwWidth, DWORD dwHeight)
{
    if (!InitializeDX(hWnd, dwWidth, dwHeight))
        return false;

    if (!m_shaders->Initialize(this->device))
        return false;

    if (!m_cbs->Initialize(this->device, this->deviceContext))
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
    const HRESULT timingAttached = frameTiming.Attach(device.Get(), deviceContext.Get());
    GameJournal::Event("RENDER", "frame_owner",
        "frame=c56 hwnd=%p menu_panels=c56-blit world_terrain=E1-submit-or-CDX11-fallback world_figures=E1-submit-or-CDX11-fallback effects=CDX11-on-c56 shadows=CDX11-on-c56 timing_gpu=0x%08lX",
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

    rtbd = { 0 };
    rtbd.BlendEnable = false;

    blendDesc = { 0 };
    blendDesc.RenderTarget[0] = rtbd;
    IfFailedThrowHR(
        this->device->CreateBlendState(&blendDesc, this->blendStateDisable.GetAddressOf()));

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

    // Create sampler description for sampler state
    D3D11_SAMPLER_DESC sampDesc;
    ZeroMemory(&sampDesc, sizeof(sampDesc));

    // D3D11_FILTER_MIN_MAG_MIP_LINEAR
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    // sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    // sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    // sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    IfFailedThrowHR(this->device->CreateSamplerState(&sampDesc, this->samplerState.GetAddressOf()));

    D3D11_TEXTURE2D_DESC blackTextureDesc;
    DefaultSurfaceDesc(&blackTextureDesc);
    blackTextureDesc.Width = 32;
    blackTextureDesc.Height = 32;
    // blackTextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    blackTextureDesc.Format = DXGI_FORMAT_BC1_UNORM;

    IfFailedThrowHR(
        this->device->CreateTexture2D(&blackTextureDesc, NULL, this->blackTexture.GetAddressOf()));

    IfFailedThrowHR(this->device->CreateShaderResourceView(blackTexture.Get(), NULL,
                                                           this->blackTextureView.GetAddressOf()));

    m_states = std::make_unique<DirectX::CommonStates>(this->device.Get());
    m_caches = std::make_unique<CVertexCaches>(this->device.Get(), this->deviceContext.Get());
    m_batchers = std::make_unique<CBatchers>();
    return true;
}

bool CDX11::ReportLiveObjects()
{
    ComPtr<ID3D11Debug> d3dDebug;
    if (SUCCEEDED(this->device.As(&d3dDebug)))
    {
        d3dDebug->ReportLiveDeviceObjects(D3D11_RLDO_SUMMARY | D3D11_RLDO_DETAIL |
                                          D3D11_RLDO_IGNORE_INTERNAL);
        return true;
    }
    return false;
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

void CDX11::SetAlphaRef(float value)
{
    m_cbs->cb_ps_pixelshader.data.alphaRef = value;
};
void CDX11::BeginModShadows() {
    if(!modShadows) modShadows=std::make_shared<ModShadowDX11>(device.Get(),deviceContext.Get());
    modShadows->Begin(m_width,m_height);
}
void CDX11::EndModShadows() { if(modShadows)modShadows->End(); }
std::vector<WORD> CDX11::ReadModShadowAlpha(ID3D11ShaderResourceView *source) {
    if(!modShadows)modShadows=std::make_shared<ModShadowDX11>(device.Get(),deviceContext.Get());
    return modShadows->ReadAlpha(source);
}

void CDX11::UpdateTerrainShadow(unsigned w,unsigned h,const unsigned char *pixels){
    if(!modShadows)modShadows=std::make_shared<ModShadowDX11>(device.Get(),deviceContext.Get());
    modShadows->UpdateTerrain(w,h,pixels);
}
void CDX11::SetTerrainShadowPass(bool enabled){
    modShadows->SetTerrainPass(enabled);
}

bool CDX11::GetTerrainShadowSize(unsigned &width,unsigned &height) const {
    if(!modShadows || !modShadows->terrainPass)return false;
    width=modShadows->TerrainWidth();height=modShadows->TerrainHeight();
    return width && height;
}

void CDX11::ResetWorldBackground(){modBackground.reset();}
void CDX11::DrawWorldBackground(const std::shared_ptr<ModWorldBackground::FrameSnapshot>& snapshot,const WorldBackground::VisualEnvironment& environment,const std::array<float,4>& depthRow,unsigned long fogColor,bool transparentPass){
    if(!snapshot)return;
    try{if(!modBackground)modBackground=std::make_shared<ModWorldBackgroundDX11>(device.Get(),deviceContext.Get());modBackground->Frame(snapshot,environment,depthRow,fogColor,transparentPass);}
    catch(const std::exception& e){GameJournal::AllocationFailureInCatch("world_background");OutputDebugStringA(e.what());modBackground.reset();if(snapshot->publication)snapshot->publication->Reset(environment.epoch);}
}
