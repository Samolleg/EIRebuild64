#include "stdafx.h"
#include <vector>;
#include <SimpleMath.h>

#include <wincodec.h> // Optional
#include <ScreenGrab.h>

#include "ConstantBuffer.h"
#include "DXShader.h"
#include "RendererDX11.h"
#include "../FrameDiagnostics.h"
#include "ModShadowDX11.h"
#include "ModWorldBackgroundDX11.h"
#include "UIColorKeyBlit.h"

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

bool CDX11::SubmitOwnedFigure(EIRenderMeshV4 mesh,EIRenderFigureV4 packet)
{
    if(!ownedWorldActive||!ownedRenderer.FigureActive())return false;
    if(modShadows&&modShadows->active) {
        ownedRenderer.FigureFallback("legacy_shadow_pass");return false;
    }
    uint32_t states[256]={};
    states[D3DRENDERSTATE_ZENABLE]=D3DZB_TRUE;states[D3DRENDERSTATE_ZWRITEENABLE]=TRUE;
    states[D3DRENDERSTATE_ZFUNC]=D3DCMP_LESSEQUAL;states[D3DRENDERSTATE_CULLMODE]=D3DCULL_CCW;
    states[D3DRENDERSTATE_FILLMODE]=D3DFILL_SOLID;states[D3DRENDERSTATE_SRCBLEND]=D3DBLEND_ONE;
    states[D3DRENDERSTATE_DESTBLEND]=D3DBLEND_ZERO;states[D3DRENDERSTATE_TEXTUREFACTOR]=0xffffffff;
    states[D3DRENDERSTATE_ALPHAFUNC]=D3DCMP_GREATEREQUAL;
    for(const auto& pair:fake_dx7.render_state)if(pair.first>=0&&pair.first<256)states[pair.first]=pair.second;
    // Both compatibility and native PS use the same source alphaRef/256.
    // Retain the actual alpha mode and threshold byte.
    states[D3DRENDERSTATE_ALPHAFUNC]=D3DCMP_GREATEREQUAL;
    uint32_t stages[32]={};
    stages[D3DTSS_COLOROP]=D3DTOP_MODULATE;
    stages[D3DTSS_COLORARG1]=D3DTA_TEXTURE;stages[D3DTSS_COLORARG2]=D3DTA_DIFFUSE;
    stages[D3DTSS_ALPHAARG1]=D3DTA_TEXTURE;stages[D3DTSS_ALPHAARG2]=D3DTA_DIFFUSE;
    const auto alphaMode=GetConstantBuffers()->cb_ps_pixelshader.data.alphaMode;
    stages[D3DTSS_ALPHAOP]=alphaMode==0?D3DTOP_SELECTARG1:alphaMode==2?D3DTOP_SELECTARG2:D3DTOP_MODULATE;
    stages[D3DTSS_ADDRESSU]=stages[D3DTSS_ADDRESSV]=D3DTADDRESS_WRAP;
    stages[D3DTSS_MINFILTER]=stages[D3DTSS_MAGFILTER]=D3DTFG_LINEAR;
    stages[D3DTSS_MIPFILTER]=D3DTFP_LINEAR;
    packet.render_states=states;packet.render_state_count=256;
    packet.texture_states=stages;packet.texture_state_count=32;
    ID3D11ShaderResourceView* images[2]={};deviceContext->PSGetShaderResources(0,2,images);
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> primary,secondary;
    primary.Attach(images[0]);secondary.Attach(images[1]);
    packet.primary=primary.Get();packet.secondary=secondary.Get();
    return ownedRenderer.SubmitFigure(mesh,packet);
}

int CDX11::DrawFigurePrimitive(LPVOID pVertices, DWORD dwVertexCount, LPWORD pIndices, DWORD dwIndexCount,
                  DWORD dwFlags)
{
    if (!dwIndexCount)
        return 0;
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
    IfFailedThrowHR(ownedRenderer.BeginFrame((FLOAT *)&background_color, G_CLEAR_DEPTH_VALUE));
    RefreshOwnedTargets();
    SetRenderStates();
}

bool CDX11::BeginWorld(const EIRenderWorldFrameV2& frame)
{
    uint32_t skyConsumed = 0;
    IfFailedThrowHR(ownedRenderer.BeginWorld(frame, skyConsumed));
    ownedWorldActive = true;
    RefreshOwnedTargets();
    SetRenderStates();
    return skyConsumed != 0;
}

void CDX11::EndWorld()
{
    IfFailedThrowHR(ownedRenderer.EndWorld());
    ownedWorldActive = false;
    RefreshOwnedTargets();
    SetRenderStates();
}

void CDX11::RefreshOwnedTargets()
{
    EIRenderD3D11BridgeV1 bridge = {};
    IfFailedThrowHR(ownedRenderer.AcquireBridge(bridge));
    // Device/context/swapchain remain immutable while the existing loading
    // worker presents and the main thread creates resources. Refresh targets.
    bridge.device->Release();
    bridge.context->Release();
    bridge.swap_chain->Release();
    renderTargetView.Reset();
    depthStencilView.Reset();
    renderTargetView.Attach(bridge.color_target);
    depthStencilView.Attach(bridge.depth_target);
    ComPtr<ID3D11Resource> resource;
    renderTargetView->GetResource(resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> target;
    IfFailedThrowHR(resource.As(&target));
    D3D11_TEXTURE2D_DESC desc = {};
    target->GetDesc(&desc);
    D3D11_VIEWPORT viewport = {0, 0, float(desc.Width), float(desc.Height), 0, 1};
    deviceContext->OMSetRenderTargets(1, renderTargetView.GetAddressOf(), depthStencilView.Get());
    deviceContext->RSSetViewports(1, &viewport);
}

void CDX11::EndFrame()
{
    FrameDiagnostics::Scope timing(FrameDiagnostics::Present);
    if (m_bMakeScreenShot)
    {
        MakeScreenShot();
        m_bMakeScreenShot = false;
    }
    const HRESULT presented = ownedRenderer.Present();
    FrameDiagnostics::Presented(presented);
    IfFailedThrowHR(presented);
}

void CDX11::ClearColorBuffer(DWORD value)
{
    DirectX::XMFLOAT4 color = BGRAtoFloat4(value);
    this->deviceContext->ClearRenderTargetView(this->renderTargetView.Get(), (FLOAT *)&color);
}

void CDX11::ClearZBuffer(DWORD dwValue)
{
    this->deviceContext->ClearDepthStencilView(this->depthStencilView.Get(),
                                               D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, dwValue, 0);
}

BOOL CDX11::BindTexture(DWORD dwIndex, ID3D11ShaderResourceView *pTexture)
{
    GetConstantBuffers()->cb_ps_pixelshader.data.isTexture[dwIndex].x = pTexture ? 1 : 0;
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

HRESULT CDX11::DrawColorKeySurface(ID3D11ShaderResourceView* image,
    const RECT& destination, const RECT* source, COLORREF key)
{
    spriteBatch->End();
    const HRESULT result = ownedRenderer.DrawUi(image, destination, source,
        EI_RENDERER_UI_COLOR_KEY | EI_RENDERER_UI_OPAQUE | EI_RENDERER_UI_POINT, key);
    spriteBatch->Begin();
    return result;
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
    this->swapchain.Reset();
    // ReportLiveObjects();
    renderTargetView.Reset();depthStencilView.Reset();depthStencilBuffer.Reset();
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
    HRESULT hr = ownedRenderer.Initialize(hWnd, dwWidth, dwHeight);
    if (FAILED(hr))
    {
        ReportError(hr, "Cannot initialize the shared renderer. Place the source-API RDDraw.dll next to EIRebuild.exe.");
        throw hr;
    }
    EIRenderD3D11BridgeV1 bridge = {};
    IfFailedThrowHR(ownedRenderer.AcquireBridge(bridge));
    device.Attach(bridge.device);
    deviceContext.Attach(bridge.context);
    swapchain.Attach(bridge.swap_chain);
    renderTargetView.Attach(bridge.color_target);
    depthStencilView.Attach(bridge.depth_target);

    // Window policy belongs to the DLL. Use its actual target extent before
    // creating the viewport; the bootstrap settings may still be 800x600.
    DXGI_SWAP_CHAIN_DESC output = {};
    IfFailedThrowHR(swapchain->GetDesc(&output));
    if (!output.BufferDesc.Width || !output.BufferDesc.Height)
        IfFailedThrowHR(E_INVALIDARG);
    dwWidth = m_width = output.BufferDesc.Width;
    dwHeight = m_height = output.BufferDesc.Height;

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
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = swapchain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (SUCCEEDED(hr))
    {
        hr = DirectX::SaveWICTextureToFile(deviceContext.Get(), backBuffer.Get(),
                                           GUID_ContainerFormatJpeg, L"SCREENSHOT.JPG");
    }
    // IfFailedThrowHR(hr);
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
