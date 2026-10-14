#include "StdAfx.h"
#include "../dx11/RendererDX11.h"
#include "../dx11/ConstantBuffer.h"
#include "../GameJournal.h"

void CDX11::RestoreHostBindings()
{
    RefreshOwnedTargets();
    SetRenderStates();
    for (UINT stage=0;stage<boundFigureTextures.size();++stage) {
        ID3D11ShaderResourceView* texture=boundFigureTextures[stage].Get();
        deviceContext->PSSetShaderResources(stage,1,&texture);
    }
}

bool CDX11::SubmitC56CampFigure(const owned::NativeGeometryView& view)
{
    HRESULT hr=E_UNEXPECTED;
    if(rendererFacade&&rendererFacade->FrameActive()) {
        owned::FixedState state;
        for(const auto& entry:fake_dx7.render_state)
            if(entry.first>=0&&std::size_t(entry.first)<state.render.size())state.render[entry.first]=entry.second;
        D3D11_VIEWPORT viewport{};UINT count=1;
        deviceContext->RSGetViewports(&count,&viewport);
        state.viewport={DWORD(viewport.TopLeftX),DWORD(viewport.TopLeftY),DWORD(viewport.Width),DWORD(viewport.Height),viewport.MinDepth,viewport.MaxDepth};
        state.render[D3DRENDERSTATE_FOGENABLE]=FALSE;
        const auto mode=GetConstantBuffers()->cb_ps_pixelshader.data.alphaMode;
        state.stage[0][D3DTSS_COLOROP]=boundFigureTextures[0]?D3DTOP_MODULATE:D3DTOP_SELECTARG2;
        state.stage[0][D3DTSS_COLORARG1]=D3DTA_TEXTURE;state.stage[0][D3DTSS_COLORARG2]=D3DTA_DIFFUSE;
        state.stage[0][D3DTSS_ALPHAOP]=!boundFigureTextures[0]?D3DTOP_SELECTARG2:mode==0?D3DTOP_SELECTARG1:mode==2?D3DTOP_SELECTARG2:D3DTOP_MODULATE;
        state.stage[0][D3DTSS_ALPHAARG1]=D3DTA_TEXTURE;state.stage[0][D3DTSS_ALPHAARG2]=D3DTA_DIFFUSE;
        state.stage[0][D3DTSS_ADDRESSU]=state.stage[0][D3DTSS_ADDRESSV]=D3DTADDRESS_WRAP;
        state.stage[0][D3DTSS_MINFILTER]=state.stage[0][D3DTSS_MAGFILTER]=D3DTFG_LINEAR;
        state.stage[0][D3DTSS_MIPFILTER]=D3DTFP_LINEAR;
        auto materialView=view;
        if(itemColorSum&&state.render[D3DRENDERSTATE_SPECULARENABLE])materialView.flags|=8u;
        hr=rendererFacade->Visit([&](owned::Renderer& renderer) {
            HRESULT result=renderer.has_pending_ui()?renderer.flush_ui():S_OK;
            if(SUCCEEDED(result)) {
                if(boundFigureTextures[1])
                    result=renderer.queue_embedded_figure(materialView,state,boundFigureTextures[0].Get(),boundFigureTextures[1].Get(),nullptr,{});
                else {
                    owned::Renderer::NativeQueueCheckpoint checkpoint;
                    if(!renderer.checkpoint_native(checkpoint))return E_FAIL;
                    result=renderer.queue_native_direct(materialView,state,boundFigureTextures[0].Get());
                    if(FAILED(result))renderer.rollback_native(checkpoint);
                }
            }
            return result;
        });
    }
    if(FAILED(hr)) {
        campPopupFailed=true;
        static bool reported=false;
        if(!reported){reported=true;GameJournal::Event("RENDER","camp_draw_skipped","owner=c56 hr=%08lx",static_cast<unsigned long>(hr));}
    }
    return SUCCEEDED(hr);
}

bool CDX11::FlushC56CampFigures()
{
    if(!rendererFacade||!rendererFacade->FrameActive())return false;
    const HRESULT hr=rendererFacade->Visit([](owned::Renderer& renderer){
        const HRESULT result=renderer.flush_native();
        if(FAILED(result))renderer.discard_native();
        return result;
    });
    if(FAILED(hr)){campPopupFailed=true;GameJournal::Event("RENDER","camp_batch_failed","hr=%08lx",static_cast<unsigned long>(hr));}
    RestoreHostBindings();return SUCCEEDED(hr);
}

bool CDX11::SubmitC56CampPopup(owned::CampRenderRequest request,owned::CampPopupDraw draw,void* data)
{
    if(!rendererFacade)return false;
    if(request==owned::CampRenderRequest::invalidate_popup)
        return SUCCEEDED(rendererFacade->Visit([](owned::Renderer& renderer){renderer.invalidate_camp_popup();return S_OK;}));
    if(!rendererFacade->FrameActive()||campPopupActive)return false;
    const auto flush=[&](owned::Renderer& renderer){
        HRESULT hr=renderer.has_pending_ui()?renderer.flush_ui():S_OK;
        return SUCCEEDED(hr)?renderer.flush_native():hr;
    };
    if(FAILED(rendererFacade->Visit(flush)))return false;
    if(request==owned::CampRenderRequest::begin_interface) {
        const HRESULT hr=rendererFacade->Visit([](owned::Renderer& renderer){
            renderer.invalidate_camp_popup();return renderer.composite_world();
        });
        RestoreHostBindings();return SUCCEEDED(hr);
    }
    if(!draw)return false;
    const HRESULT began=rendererFacade->Visit([&](owned::Renderer& renderer){return renderer.begin_camp_popup(request==owned::CampRenderRequest::clear_popup);});
    if(FAILED(began))return false;
    const DWORD oldDepth=fake_dx7.render_state[D3DRENDERSTATE_ZENABLE];
    const DWORD oldWrite=fake_dx7.render_state[D3DRENDERSTATE_ZWRITEENABLE];
    const DWORD oldFunction=fake_dx7.render_state[D3DRENDERSTATE_ZFUNC];
    campPopupActive=true;campPopupFailed=false;
    fake_dx7.render_state[D3DRENDERSTATE_ZENABLE]=D3DZB_TRUE;
    fake_dx7.render_state[D3DRENDERSTATE_ZWRITEENABLE]=TRUE;
    fake_dx7.render_state[D3DRENDERSTATE_ZFUNC]=D3DCMP_LESSEQUAL;
    bool complete=false;
    try {RestoreHostBindings();draw(data);complete=!campPopupFailed&&SUCCEEDED(rendererFacade->Visit(flush));}
    catch(...) {complete=false;}
    rendererFacade->Visit([&](owned::Renderer& renderer){
        if(!complete)renderer.discard_native();
        renderer.end_camp_popup(complete);return S_OK;
    });
    fake_dx7.render_state[D3DRENDERSTATE_ZENABLE]=oldDepth;
    fake_dx7.render_state[D3DRENDERSTATE_ZWRITEENABLE]=oldWrite;
    fake_dx7.render_state[D3DRENDERSTATE_ZFUNC]=oldFunction;
    campPopupActive=false;
    RestoreHostBindings();
    if(!complete)owned::camp_inventory_popup_failed();
    return complete;
}
