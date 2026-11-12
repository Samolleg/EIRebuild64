#include "Game/OwnedRender/E1Surface.h"
#include "renderer.h"
#include "retained_static_submission.h"
#include <cstdio>
#include <stdexcept>
#include <cstring>
// Only process-wide telemetry is outside this focused publication test.
namespace owned {
// Fail if this surface/foliage test unexpectedly enters world pose or mounts.
int g_fur_mode = 0;
bool owned_resource_runtime_mounts(ResourceStoreConfig&, std::uint64_t*) noexcept { std::abort(); }
VisualPreparedResources::PoseKey owned_visual_pose_key(const RetainedVisualSample&) noexcept { std::abort(); }
bool owned_visual_pose_key_matches(const VisualPreparedResources::PoseKey&,const RetainedVisualSample&,const double*) noexcept { std::abort(); }
void update_surface_memory_accounting(SurfaceImage&) noexcept {}
void release_surface_memory_accounting(SurfaceImage&) noexcept {}
void bridge_append_probe_event(const char*,const char*) noexcept {}
}
static int checks=0;
static void check(bool ok){++checks;if(!ok)throw std::runtime_error("check "+std::to_string(checks));}
int main() try {
 using namespace owned; using namespace OwnedRender;
 Microsoft::WRL::ComPtr<ID3D11Device> device;
 check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,nullptr)));
 D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=2;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 const uint32_t pixels[]{0xffffffff,0xffffffff,0,0xffffffff};
 D3D11_SUBRESOURCE_DATA data{pixels,8,16}; Microsoft::WRL::ComPtr<ID3D11Texture2D> gpu;
 check(SUCCEEDED(device->CreateTexture2D(&d,&data,&gpu)));
 TextureAlphaPixels raw;raw.data=reinterpret_cast<const uint8_t*>(pixels);raw.bytes=16;raw.width=raw.height=2;raw.pitch=8;raw.pixel_bytes=4;raw.alpha_mask=0xff000000;
 auto mask=texture_alpha_cut_mask(raw);check(mask&&(*mask)[0]==4);
 E1Surface owner;check(owner.Complete(gpu.Get(),false,mask)); auto surface=E1SurfaceValue(owner.Id());
 TextureAlphaPixels published;check(visual_surface_alpha_pixels(*surface,published));check(texture_alpha_texel_cut(published,0,1));check(!texture_alpha_texel_cut(published,0,0));
 auto figure=std::make_shared<FigureCpuAsset>();figure->shape_count=1;figure->position_groups=1;figure->positions.resize(1);figure->positions[0].z[0]=2;
 figure->components.resize(3);figure->indices={0,1,2};figure->texture_coordinates.resize(1);figure->texture_coordinates[0].u=.1f;figure->texture_coordinates[0].v=.6f;
 auto skeleton=std::make_shared<ModelCpuAsset>();skeleton->links.resize(1);skeleton->links[0].parent_index=UINT_MAX;
 VisualAssemblyAsset tree;tree.skeleton=skeleton;tree.scale={1,1,1};tree.parts.resize(1);tree.parts[0].skeleton_node=0;tree.parts[0].geometry=figure;tree.parts[0].surface=surface;
 sway_tree_assembly(tree);check(tree.sways);check((retained_object_space_sway_flags(tree,tree.parts[0])&0x200)!=0);
 // Opaque publication must discard a stale mask and clear foliage evidence.
 check(owner.Complete(gpu.Get(),true,mask));tree.parts[0].surface=E1SurfaceValue(owner.Id());check(!tree.parts[0].surface->alpha_cut_mask);sway_tree_assembly(tree);check(!tree.sways);
 check(owner.Complete(gpu.Get()));surface=E1SurfaceValue(owner.Id());check(!surface->alpha_cut_mask);check(!visual_surface_alpha_pixels(*surface,published));
 SurfaceImage cpu;cpu.width=cpu.height=2;cpu.pitch=8;cpu.format.dwSize=sizeof(cpu.format);cpu.format.dwFlags=DDPF_RGB|DDPF_ALPHAPIXELS;cpu.format.dwRGBBitCount=32;cpu.format.dwRBitMask=0xff0000;cpu.format.dwGBitMask=0xff00;cpu.format.dwBBitMask=0xff;cpu.format.dwRGBAlphaBitMask=0xff000000;cpu.pixels.resize(16);memcpy(cpu.pixels.data(),pixels,16);
 check(owner.CompleteCpu(cpu));tree.parts[0].surface=E1SurfaceValue(owner.Id());sway_tree_assembly(tree);check(tree.sways);
 memset(cpu.pixels.data(),255,16);check(owner.CompleteCpu(cpu));tree.parts[0].surface=E1SurfaceValue(owner.Id());sway_tree_assembly(tree);check(!tree.sways);
 // BC1/2/3 use the same alpha parser as actual uploads.
 for(auto format:{TextureAlphaFormat::bc1,TextureAlphaFormat::bc2,TextureAlphaFormat::bc3}){uint8_t block[16]{};if(format==TextureAlphaFormat::bc1)memset(block+4,255,4);raw.data=block;raw.bytes=16;raw.width=raw.height=4;raw.pitch=16;raw.format=format;auto cut=texture_alpha_cut_mask(raw);check(cut&&(*cut)[0]==255&&(*cut)[1]==255);}
 printf("Tree sway publication: %d checks PASS\n",checks);return 0;
} catch(const std::exception& e){printf("FAIL %s\n",e.what());return 1;}
