// Integration check: production PhysX GPU trajectory -> production native mesh draw -> pixels.
#define NOMINMAX
#include "../Game/OwnedRender/E1PhysicsDebris.h"
#include "../Game/OwnedRender/E1VisualFrame.h"
#include "embedded_host.h"
#include "retained_static_scene.h"
#include "renderer.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
static void require(bool ok,const char* step){if(!ok)throw std::runtime_error(step);}
static void hr(HRESULT value,const char* step){if(FAILED(value)){std::cerr<<step<<" hr="<<std::hex<<value<<std::dec<<'\n';throw std::runtime_error(step);}}
struct Image{std::vector<std::uint8_t> pixels;UINT width=0,height=0;};
static HRESULT Read11(void* context,ID3D11DeviceContext* dc,ID3D11Texture2D* source){
    auto& image=*static_cast<Image*>(context);if(!source)return E_FAIL;
    ComPtr<ID3D11Device> device;source->GetDevice(&device);D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
    image.width=desc.Width;image.height=desc.Height;desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> staging;HRESULT result=device->CreateTexture2D(&desc,nullptr,&staging);if(FAILED(result))return result;
    dc->CopyResource(staging.Get(),source);D3D11_MAPPED_SUBRESOURCE mapped{};result=dc->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped);if(FAILED(result))return result;
    image.pixels.resize(std::size_t(image.width)*image.height*4);
    for(UINT y=0;y<image.height;++y)std::memcpy(image.pixels.data()+std::size_t(y)*image.width*4,static_cast<const char*>(mapped.pData)+std::size_t(y)*mapped.RowPitch,image.width*4);
    dc->Unmap(staging.Get(),0);return S_OK;
}
static void save(const fs::path& path,const Image& image){
    BITMAPFILEHEADER f{};BITMAPINFOHEADER h{};h.biSize=sizeof(h);h.biWidth=image.width;h.biHeight=-LONG(image.height);h.biPlanes=1;h.biBitCount=32;h.biCompression=BI_RGB;h.biSizeImage=DWORD(image.pixels.size());
    f.bfType=0x4d42;f.bfOffBits=sizeof(f)+sizeof(h);f.bfSize=f.bfOffBits+h.biSizeImage;
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(&f),sizeof(f));out.write(reinterpret_cast<const char*>(&h),sizeof(h));out.write(reinterpret_cast<const char*>(image.pixels.data()),image.pixels.size());require(bool(out),"save image");
}
int wmain(int argc,wchar_t** argv)try{
    require(argc==4,"usage: check game-root output backend(11|12)");const fs::path game=fs::absolute(argv[1]),out=fs::absolute(argv[2]);const bool dx12=std::wstring(argv[3])==L"12";
    fs::create_directories(out);const auto config=out/L"renderer.ini";
    {std::ofstream ini(config);ini<<"[setup]\nfirst_run_done=1\n[graphics]\nrenderer_backend="<<(dx12?1:0)<<"\nrender_thread=1\nframe_generation=0\n";}
    owned::EmbeddedHostPaths paths;paths.renderer_config=config.wstring();paths.world_map_layouts=(game/L"MODS/Dejavu/world-map-layouts.json").wstring();
    paths.mounts.archives={(game/L"Base/Res/figures.res").wstring(),(game/L"Base/Res/textures.res").wstring(),(game/L"Base/Res/menus.res").wstring()};
    owned::embedded_host_configuration().configure(std::move(paths));
    WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"PhysicsFxRenderCheck";RegisterClassW(&cls);
    // Deliberately never show the diagnostic window.
    HWND window=CreateWindowExW(0,cls.lpszClassName,L"Physics FX check",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,cls.hInstance,nullptr);require(window!=nullptr,"hidden window");
    auto renderer=std::make_unique<owned::Renderer>();
    hr(dx12?renderer->ensure(window,640,480):renderer->ensure_for_test_warp(window,640,480),"renderer initialize");
    PhysicsFx::World physics;std::vector<PhysicsFx::Triangle> terrain={{{-14,-14,0},{14,-14,0},{14,14,0}},{{-14,-14,0},{14,14,0},{-14,14,0}}};
    require(physics.AddPatch(1,{0,0,0},terrain)&&physics.Burst(1,{0,0,0},{0,0,1},1,42)==64,"PhysX GPU burst");
    OwnedRender::E1VisualInputs frame;frame.lightingValid=true;frame.ambient={.08f,.08f,.08f,1};frame.sunDiffuse={.9f,.9f,.9f,1};frame.sunDirection={.4f,-.4f,.82f,0};
    // An orthographic close view, +Z up, the ground recedes with +Y.
    frame.worldToClip={.55f,0,0,0, 0,.35f,.8f,-.25f, 0,.04f,-.03f,.5f, 0,0,0,1};
    frame.fog=[](const owned::ObjectBorderFogInputs&,const float*)->unsigned char{return 255;};
    // Deliberately varied texels exercise native UV sampling on both backends.
    const std::uint32_t texels[]={0xffc6bca4,0xff615d52,0xff8d8778,0xffe0d8c6};
    const owned::NativeOwnedTextureMip mip{reinterpret_cast<const std::uint8_t*>(texels),2,2,sizeof(texels)};
    frame.debrisTexture.identity=0x5048595358544558ull;frame.debrisTexture.generation=1;
    frame.debrisTexture.format=owned::NativeOwnedTextureFormat::argb8888;
    frame.debrisTexture.mips=&mip;frame.debrisTexture.mip_count=1;frame.debrisTexture.opaque_mips=1;
    owned::FixedState state;state.viewport.dwWidth=640;state.viewport.dwHeight=480;state.viewport.dvMinZ=0;state.viewport.dvMaxZ=1;
    state.render[D3DRENDERSTATE_FILLMODE]=D3DFILL_SOLID;state.render[D3DRENDERSTATE_FOGENABLE]=FALSE;
    owned::NativeWorldLighting light;light.ambient=frame.ambient;light.sunlight_direction=frame.sunDirection;light.sunlight_diffuse=frame.sunDiffuse;
    Image previous;unsigned changes=0;
    for(int tick=0;tick<=60;++tick){
        if(tick)physics.Tick(false);
        if(!physics.Error().empty())throw std::runtime_error(physics.Error());
        require(physics.Statistics().gpuDynamics&&physics.Statistics().gpuBroadPhase,"GPU simulation active");
        if(tick!=0&&tick!=6&&tick!=18&&tick!=40&&tick!=60)continue;
        renderer->begin_scene();hr(renderer->bind_embedded_targets(),"bind targets");const float color[]={.025f,.035f,.055f,1},depth=1;
        hr(renderer->clear_embedded_targets(color,&depth),"clear targets");require(renderer->set_native_world_lighting(light),"lighting");
        frame.debris=physics.Sample(1);require(OwnedRender::E1DrawPhysicsDebris(*renderer,state,frame),"native debris submission");
        require(renderer->embedded_render_thread_active(),"embedded scene worker active");
        hr(renderer->queue_embedded_batch(owned::Renderer::EmbeddedBatch::geometry),"native worker flush");
        hr(renderer->queue_embedded_batch(owned::Renderer::EmbeddedBatch::composite),"worker composition");
        hr(renderer->queue_embedded_batch(owned::Renderer::EmbeddedBatch::end_scene),"worker frame end");
        hr(renderer->finish_embedded_scene_commands(),"worker result before readback");
        Image image;
        if(dx12)hr(renderer->read_embedded_d3d12_output(image.pixels,image.width,image.height),"read D3D12 pixels");
        else hr(renderer->visit_embedded_output(&image,&Read11),"read D3D11 pixels");
        require(image.pixels.size()==std::size_t(image.width)*image.height*4&&!image.pixels.empty(),"image dimensions");
        unsigned colored=0;for(std::size_t p=0;p<image.pixels.size();p+=4)if(image.pixels[p]>45&&image.pixels[p+1]>45&&image.pixels[p+2]>45)++colored;
        save(out/(L"frame-"+std::to_wstring(tick)+L".bmp"),image);
        std::cout<<"FRAME "<<tick<<" stones="<<frame.debris.size()<<" pixels="<<colored<<'\n';
        require(colored>8,"visible stone pixels");if(!previous.pixels.empty()&&image.pixels!=previous.pixels)++changes;
        previous=std::move(image);
    }
    require(changes>=3,"moving rendered physics");
    renderer.reset();DestroyWindow(window);std::cout<<"RESULT PASS native D3D"<<(dx12?12:11)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
