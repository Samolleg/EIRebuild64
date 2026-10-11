#include "embedded_device.h"
#include "E1Surface.h"
#include "E1DeviceAccess.h"
#include "E1RenderGate.h"
#include <atomic>
#include <thread>
#include <cstdio>
#include <stdexcept>

namespace {
using Microsoft::WRL::ComPtr;
int checks = 0;
void check(bool value) { ++checks; if (!value) throw checks; }
struct Targets {
    ComPtr<ID3D11Texture2D> color, depth;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    Targets(ID3D11Device* device, UINT width, UINT height) {
        D3D11_TEXTURE2D_DESC d{};
        d.Width=width; d.Height=height; d.ArraySize=d.MipLevels=1;
        d.SampleDesc.Count=1; d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
        d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        check(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&color)));
        check(SUCCEEDED(device->CreateRenderTargetView(color.Get(),nullptr,&rtv)));
        d.Format=DXGI_FORMAT_D24_UNORM_S8_UINT; d.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        check(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&depth)));
        check(SUCCEEDED(device->CreateDepthStencilView(depth.Get(),nullptr,&dsv)));
    }
};
struct Consumer {
    ID3D11Device* device;
    Targets* expected;
    UINT width, height;
    unsigned calls=0;
    bool fail=false;
    OwnedRender::E1Surface publication;
    static HRESULT visit(void* user,const owned::EmbeddedDeviceView& v) {
        auto& self=*static_cast<Consumer*>(user); ++self.calls;
        check(v.device==self.device && v.color==self.expected->rtv.Get() &&
            v.depth==self.expected->dsv.Get() && v.width==self.width && v.height==self.height);
        if(self.fail)return E_ABORT;
        check(OwnedRender::E1BindTargets(nullptr,v)==S_OK);
        ComPtr<ID3D11RenderTargetView> boundColor;
        ComPtr<ID3D11DepthStencilView> boundDepth;
        v.context->OMGetRenderTargets(1,&boundColor,&boundDepth);
        check(boundColor.Get()==v.color && boundDepth.Get()==v.depth);
        UINT count=1;D3D11_VIEWPORT viewport{};
        v.context->RSGetViewports(&count,&viewport);
        check(count==1 && viewport.Width==float(v.width) && viewport.Height==float(v.height));
        float green[]{0,1,0,1},depth=0.25f;
        check(OwnedRender::E1ClearColor(green,v)==S_OK);
        check(OwnedRender::E1ClearDepth(&depth,v)==S_OK);
        check(OwnedRender::E1ClearColor(nullptr,v)==E_INVALIDARG);
        check(OwnedRender::E1ClearDepth(nullptr,v)==E_INVALIDARG);
        check(self.publication.Complete(self.expected->color.Get()));
        const auto value=OwnedRender::E1SurfaceValue(self.publication.Id());
        ComPtr<ID3D11Texture2D> resolved;
        check(value && SUCCEEDED(OwnedRender::E1ResolveSurface(nullptr,v.device,value->id,value->generation,resolved)));
        check(resolved.Get()==self.expected->color.Get());
        return S_OK;
    }
};
struct OutputConsumer {
    ID3D11DeviceContext* context;
    ID3D11Texture2D* expected;
    unsigned calls=0;
    HRESULT result=S_OK;
    static HRESULT visit(void* user,ID3D11DeviceContext* context,ID3D11Texture2D* texture) {
        auto& self=*static_cast<OutputConsumer*>(user);++self.calls;
        check(context==self.context && texture==self.expected);
        return self.result;
    }
};
void verifyPixels(ID3D11Device* device,ID3D11DeviceContext* context,Targets& target) {
    D3D11_TEXTURE2D_DESC d{}; target.color->GetDesc(&d);
    d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;
    check(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&readback)));
    context->CopyResource(readback.Get(),target.color.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)));
    bool green=true;
    for(UINT y=0;y<d.Height;++y)for(UINT x=0;x<d.Width;++x)
        green=green && reinterpret_cast<const UINT*>(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch)[x]==0xff00ff00;
    context->Unmap(readback.Get(),0); check(green);
}
}
int main() try {
    OwnedRender::E1RenderGate gate;
    {
        const auto mainFrame=gate.Lock();
        const auto nestedMovie=gate.Lock();
        check(mainFrame.owns_lock() && nestedMovie.owns_lock());
        bool skipped=false;
        std::thread loading([&] {
            const auto attempt=gate.Try();
            skipped=!attempt.owns_lock();
        });
        // Join while owning a frame, as in a nested loading-screen teardown.
        loading.join();
        check(skipped);
    }
    try { const auto access=gate.Lock(); throw 7; } catch(int) {}
    bool recovered=false;
    std::thread recovery([&] {const auto access=gate.Try();recovered=access.owns_lock();});
    recovery.join();check(recovered);
    unsigned shared=0;
    const auto producer=[&] {
        for(unsigned n=0;n<10000;++n) {const auto access=gate.Lock();++shared;}
    };
    std::thread a(producer),b(producer);a.join();b.join();check(shared==20000);
    ComPtr<ID3D11Device> device,foreign;
    ComPtr<ID3D11DeviceContext> context,foreignContext,deferred;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)));
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&foreign,nullptr,&foreignContext)));
    check(SUCCEEDED(device->CreateDeferredContext(0,&deferred)));
    Targets scene(device.Get(),8,4),ui(device.Get(),16,8),alien(foreign.Get(),8,4);
    std::atomic<unsigned> serializedFrames{0},failedFrames{0};
    const auto gpuProducer=[&](Targets& target) {
        for(unsigned n=0;n<32;++n) {
            const auto access=gate.Lock();
            const HRESULT hr=owned::visit_embedded_device(device.Get(),context.Get(),
                target.rtv.Get(),target.dsv.Get(),nullptr,
                [](void*,const owned::EmbeddedDeviceView& view) -> HRESULT {
                    OwnedRender::E1BindTargets(nullptr,view);
                    std::this_thread::yield();
                    ComPtr<ID3D11RenderTargetView> bound;
                    view.context->OMGetRenderTargets(1,&bound,nullptr);
                    if(bound.Get()!=view.color)return E_UNEXPECTED;
                    float green[]{0,1,0,1};
                    return OwnedRender::E1ClearColor(green,view);
                });
            if(SUCCEEDED(hr))++serializedFrames;else ++failedFrames;
        }
    };
    std::thread sceneProducer(gpuProducer,std::ref(scene)),uiProducer(gpuProducer,std::ref(ui));
    sceneProducer.join();uiProducer.join();
    check(serializedFrames==64 && failedFrames==0);
    verifyPixels(device.Get(),context.Get(),scene);
    verifyPixels(device.Get(),context.Get(),ui);
    OutputConsumer output{context.Get(),ui.color.Get()};
    const auto visitOutput=[&](ID3D11DeviceContext* c,ID3D11RenderTargetView* target) {
        return owned::visit_embedded_output(device.Get(),c,target,&output,&OutputConsumer::visit);
    };
    // Output selection is independent of the currently bound world target.
    context->OMSetRenderTargets(1,scene.rtv.GetAddressOf(),scene.dsv.Get());
    check(visitOutput(context.Get(),ui.rtv.Get())==S_OK);
    check(output.calls==1);
    check(visitOutput(nullptr,ui.rtv.Get())==DXGI_ERROR_INVALID_CALL);
    check(visitOutput(context.Get(),nullptr)==DXGI_ERROR_INVALID_CALL);
    check(visitOutput(foreignContext.Get(),ui.rtv.Get())==E_INVALIDARG);
    check(visitOutput(deferred.Get(),ui.rtv.Get())==E_INVALIDARG);
    check(visitOutput(context.Get(),alien.rtv.Get())==E_INVALIDARG);
    check(owned::visit_embedded_output(device.Get(),context.Get(),ui.rtv.Get(),nullptr,nullptr)==E_INVALIDARG);
    check(output.calls==1);
    output.result=E_ABORT;
    check(visitOutput(context.Get(),ui.rtv.Get())==E_ABORT);
    check(output.calls==2);
    ComPtr<ID3D11Texture2D> captured;
    bool usedBound=false,different=false;
    context->OMSetRenderTargets(1,scene.rtv.GetAddressOf(),nullptr);
    check(OwnedRender::E1CaptureSource(context.Get(),ui.color.Get(),captured,usedBound,different)==S_OK);
    check(captured.Get()==scene.color.Get() && usedBound && different);
    context->OMSetRenderTargets(1,ui.rtv.GetAddressOf(),nullptr);
    check(OwnedRender::E1CaptureSource(context.Get(),ui.color.Get(),captured,usedBound,different)==S_OK);
    check(captured.Get()==ui.color.Get() && usedBound && !different);
    context->OMSetRenderTargets(0,nullptr,nullptr);
    check(OwnedRender::E1CaptureSource(context.Get(),ui.color.Get(),captured,usedBound,different)==S_OK);
    check(captured.Get()==ui.color.Get() && !usedBound && !different);
    check(OwnedRender::E1CaptureSource(nullptr,ui.color.Get(),captured,usedBound,different)==E_INVALIDARG);
    check(!captured && !usedBound && !different);
    check(OwnedRender::E1CaptureSource(context.Get(),nullptr,captured,usedBound,different)==E_INVALIDARG);
    Consumer consumer{device.Get(),&scene,8,4};
    const auto call=[&](ID3D11DeviceContext* c,ID3D11RenderTargetView* r,ID3D11DepthStencilView* d) {
        return owned::visit_embedded_device(device.Get(),c,r,d,&consumer,&Consumer::visit);
    };
    check(call(context.Get(),scene.rtv.Get(),scene.dsv.Get())==S_OK);
    verifyPixels(device.Get(),context.Get(),scene);
    const auto old=OwnedRender::E1SurfaceValue(consumer.publication.Id());
    // The interface-sized targets are selected at the next call, not cached
    // from the preceding world-sized draw. This is an offscreen seam test.
    consumer.expected=&ui;consumer.width=16;consumer.height=8;
    check(call(context.Get(),ui.rtv.Get(),ui.dsv.Get())==S_OK);
    verifyPixels(device.Get(),context.Get(),ui);
    ComPtr<ID3D11Texture2D> resolved;
    check(FAILED(OwnedRender::E1ResolveSurface(nullptr,device.Get(),old->id,old->generation,resolved)) && !resolved);
    const unsigned accepted=consumer.calls;
    check(call(context.Get(),nullptr,ui.dsv.Get())==DXGI_ERROR_INVALID_CALL);
    check(call(context.Get(),ui.rtv.Get(),nullptr)==DXGI_ERROR_INVALID_CALL);
    check(call(nullptr,ui.rtv.Get(),ui.dsv.Get())==DXGI_ERROR_INVALID_CALL);
    check(call(foreignContext.Get(),ui.rtv.Get(),ui.dsv.Get())==E_INVALIDARG);
    check(call(deferred.Get(),ui.rtv.Get(),ui.dsv.Get())==E_INVALIDARG);
    check(call(context.Get(),alien.rtv.Get(),scene.dsv.Get())==E_INVALIDARG);
    check(call(context.Get(),scene.rtv.Get(),alien.dsv.Get())==E_INVALIDARG);
    check(call(context.Get(),scene.rtv.Get(),ui.dsv.Get())==E_INVALIDARG);
    check(owned::visit_embedded_device(device.Get(),context.Get(),ui.rtv.Get(),ui.dsv.Get(),nullptr,nullptr)==E_INVALIDARG);
    check(consumer.calls==accepted);
    consumer.fail=true;
    check(call(context.Get(),ui.rtv.Get(),ui.dsv.Get())==E_ABORT);
    check(consumer.calls==accepted+1);
    consumer.publication.BeginWrite();
    check(!OwnedRender::E1SurfaceValue(consumer.publication.Id()));
    std::printf("E1DeviceCheck: %d checks PASS (offscreen WARP; no Renderer lifecycle or game frame)\n",checks);
    return 0;
} catch(int n) { std::printf("E1DeviceCheck failed: %d\n",n); return 1; }
