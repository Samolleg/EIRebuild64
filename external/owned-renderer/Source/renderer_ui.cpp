// Renderer UI: legacy draw frames and HUD asset frames, camp popup, cursor, blits, present_image, character pane text, capture, surface retirement and pending blits.
#include <intrin.h>
#include "embedded_figure_shader.h"
#include "point_shadow_policy.h"
#include "cursor_image.h"
#include "shadow_softness.h"
#include "shadow_foliage.h"
#include "contact_blend.h"
#include "native_instance_flags.h"
#include "terrain_cliff.h"
#include "ground_cover_surface.h"
#include "shadow_far_retention.h"
#include "liquid_surface_shader.h"
#include "world_fog.h"
#include "ambient_occlusion.h"
#include "game_particles.h"
#include "sky_clouds.h"
#include "cave_lighting.h"
#include "terrain_material.h"
#include "native_shader.h"
#include "owned_terrain_decode.h"
#include "renderer.h"
#include "graphics_settings.h"
#include "ui_language.h"
#include "game_focus.h"
#include "cursor_confinement.h"
#include "combat_ui.h"
#include "world_texture_sampler.h"
#include "texture_mips.h"
#include "character_pane_owned_text.h"
#include "owned_resource_runtime.h"
#include "neighbour_maps.h"
#include "native_prebuilt.h"
#include "neighbour_client.h"
#include "neighbour_piece.h"
#include "static_lod.h"
#include "owned_resource_store.h"
#include "res_mapping.h"
#include "res_cache_release.h"
#include "engine_bridge.h"
#include "camp_inventory.h"
#include "retained_static_scene.h"
#include "widescreen.h"
#include "test_harness.h"
#include "x64_remote_link.h"
#include "dlss_frame.h"
#include "stall_watchdog.h"
#include <optional>
#include <sstream>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <objbase.h>
#include <bcrypt.h>
#include <psapi.h>
#include <malloc.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iterator>
#include <thread>
#include <deque>
#include <map>
#include <tuple>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <unordered_set>
#include "renderer_internal.h"
#include "scene_sinks.h"
#include "scene_commands_ui.h"

namespace owned {
namespace {
constexpr std::size_t kMaximumCharacterPaneAtlasPixels =
    8u * 1024u * 1024u;
struct AttributionQpcScope{
 std::uint64_t* total{};std::int64_t start{};
 AttributionQpcScope(bool enabled,std::uint64_t& ticks,std::uint64_t& calls):total(enabled?&ticks:nullptr),start(total?perf_now():0){if(total)++calls;}
 ~AttributionQpcScope(){if(!total)return;const auto end=perf_now();if(end>=start)*total+=static_cast<std::uint64_t>(end-start);}
};
// Scene mode: game-side blits (all made from one source surface) into
// typed quads. A GPU quad names its home by the source's handle; the
// texture/view pair moves into the command's residency. make_scene_blit never
// sets a text asset (character pane text queues as its own command).
template<class Blit> void ui_quads(std::vector<Blit>& pendings,const scene_ui::SurfaceHandle& source,
                                   std::vector<scene_ui::Quad>& quads,scene_ui::Residency& residency){
 quads.resize(pendings.size());
 for(std::size_t index=0;index<pendings.size();++index){
  auto& pending=pendings[index];auto& quad=quads[index];
  scene_ui::quad_values(pending,quad);
  if(pending.texture||pending.srv||pending.gpu){  // core mode: a GPU home only the host holds
   quad.home=source;
   residency.add({source,nullptr,std::move(pending.texture),std::move(pending.srv)});
  }
 }
}
// Scene thread: a typed quad back into the renderer's queued blit.
template<class Blit> void ui_blit(scene_ui::Quad& quad,const scene_ui::Residency& residency,Blit& pending){
 scene_ui::blit_values(quad,pending);
 if(const auto* home=residency.find(quad.home)){pending.texture=home->texture;pending.srv=home->srv;pending.d3d12=home->d3d12;}
}
}  // namespace

HRESULT Renderer::draw_hud_asset_frames(const std::vector<HudAssetFrame>* captured) try {
 if(hud_frames_frame_==native_frame_index_)return S_OK;
 // Live panel reads are game memory: only with no captured copy, which
 // means the game thread itself (render_thread off).
 std::vector<HudAssetFrame> live;if(!captured){widescreen_hud_asset_frames(live);captured=&live;}
 const auto& frames=*captured;
 if(frames.empty()){hud_frames_frame_=native_frame_index_;return S_OK;}
 const auto& config=debug_overlay_.config();
 const float border_scale=configured_integer(config,"hud_border_scale",100)/100.f;
 const DWORD alpha=DWORD(std::lround(255.f*(100-configured_integer(config,"hud_border_transparency",0))/100.f))<<24;
 if(!alpha || border_scale<=0.f){hud_frames_frame_=native_frame_index_;return S_OK;}
 const auto failure=[&](const char* reason){
  if(!hud_frame_texture_failed_)bridge_report_event("hud_asset_frame_unavailable",reason);
  hud_frame_texture_failed_=true;return S_OK;
 };
 if(hud_frame_texture_failed_)return S_OK;
 if(!hud_frame_texture_) {
  ResourceStoreConfig mounts;if(!owned_resource_runtime_mounts(mounts))return S_OK;
  ResourceStore store;ResourceFailure why;ResourceBlob blob;
  auto texture=std::make_shared<TextureCpuAsset>();
  if(!store.open(mounts,why) || !store.resolve("battle00.mmp",blob,why) || !parse_texture_asset(blob,*texture,why))
   return failure(why.message.c_str());
  hud_frame_texture_=std::move(texture);
 }
 const auto& asset=*hud_frame_texture_;NativeOwnedTextureFormat format{};
 switch(asset.format) {
 case MmpFormat::dxt1:format=NativeOwnedTextureFormat::dxt1;break;
 case MmpFormat::dxt3:format=NativeOwnedTextureFormat::dxt3;break;
 case MmpFormat::dxt5:format=NativeOwnedTextureFormat::dxt5;break;
 case MmpFormat::rgb565:format=NativeOwnedTextureFormat::rgb565;break;
 case MmpFormat::argb1555:format=NativeOwnedTextureFormat::argb1555;break;
 case MmpFormat::argb4444:format=NativeOwnedTextureFormat::argb4444;break;
 case MmpFormat::argb8888:format=NativeOwnedTextureFormat::argb8888;break;
 default:return failure("Unsupported HUD frame atlas format; owned frames omitted.");
 }
 std::vector<NativeOwnedTextureMip> mips; mips.reserve(asset.mips.size());
 for(const auto& mip:asset.mips)mips.push_back({asset.source->data()+mip.byte_offset,mip.width,mip.height,mip.bytes});
 NativeOwnedTextureView view{};view.identity=asset.identity.container_incarnation;view.generation=1;
 view.format=format;view.mips=mips.data();view.mip_count=UINT(mips.size());view.resource=&asset.identity;
 view.value_source=1;view.opaque_mips=asset.opaque_mips;
 ComPtr<ID3D11ShaderResourceView> texture;D3D12TextureRef texture12;std::uint64_t backing=0;
 if(FAILED(d3d12_?get_native_owned_texture_d3d12(view,texture12,backing,nullptr):get_native_owned_texture(view,texture,backing,nullptr)))
  return failure("HUD frame atlas upload failed; owned frames omitted.");
 // Independently authored screen geometry. The shipped atlas uses one full
 // strip per side, clockwise U and inward V, with mitred corners.
 struct HudVertex {float x,y,z,rhw;DWORD diffuse,specular;float u,v;};
 std::vector<HudVertex> vertices;vertices.reserve(frames.size()*4*6);
 for(const auto& f:frames) {
  const auto& b=f.bounds;
  const float ul=(f.portrait?27.5f:4.5f)/256.f,ur=90.5f/256.f;
  const float vo=1.f-(f.portrait?96.5f:55.5f)/256.f,vi=1.f-(f.portrait?101.5f:60.5f)/256.f;
  const float l=std::round(b.left),t=std::round(b.top),r=std::round(b.left+b.width),bottom=std::round(b.top+b.height);
  const float w=(std::min)(std::round(f.thickness*border_scale),(std::min)(r-l,bottom-t)*.5f);
  if(w<=0.f)continue;
  const float x[]={l,l+w,r,r-w,r,r-w,l,l+w};
  const float y[]={t,t+w,t,t+w,bottom,bottom-w,bottom,bottom-w};
  for(unsigned edge=0;edge<4;++edge)if(f.edges&(1u<<edge)) {
   const unsigned next=(edge+1)%4,indices[]={edge*2,edge*2+1,next*2,edge*2+1,next*2+1,next*2};
   for(unsigned j=0;j<6;++j){const unsigned index=indices[j];const bool end=index/2==next;
    const DWORD frame_alpha=DWORD(std::round(float(alpha>>24)*f.opacity))<<24;
    vertices.push_back({x[index],y[index],0.f,1.f,frame_alpha|0x00ffffffu,0u,end?ur:ul,(index&1)?vi:vo});}
  }
 }
 if(vertices.empty()){hud_frames_frame_=native_frame_index_;return S_OK;}
 FixedState state;state.viewport={0,0,canvas_width(),canvas_height(),0.f,1.f};
 state.render[D3DRENDERSTATE_ZENABLE]=D3DZB_FALSE;state.render[D3DRENDERSTATE_ZWRITEENABLE]=FALSE;
 state.render[D3DRENDERSTATE_CULLMODE]=D3DCULL_NONE;state.render[D3DRENDERSTATE_ALPHABLENDENABLE]=TRUE;
 state.render[D3DRENDERSTATE_SRCBLEND]=D3DBLEND_SRCALPHA;state.render[D3DRENDERSTATE_DESTBLEND]=D3DBLEND_INVSRCALPHA;
 state.render[D3DRENDERSTATE_ALPHATESTENABLE]=FALSE;state.render[D3DRENDERSTATE_FOGENABLE]=FALSE;
 state.stage[0][D3DTSS_COLOROP]=state.stage[0][D3DTSS_ALPHAOP]=D3DTOP_MODULATE;
 state.stage[0][D3DTSS_COLORARG1]=state.stage[0][D3DTSS_ALPHAARG1]=D3DTA_TEXTURE;
 state.stage[0][D3DTSS_COLORARG2]=state.stage[0][D3DTSS_ALPHAARG2]=D3DTA_DIFFUSE;
 if(d3d12_){
  // D3D12 backend: the same quads through the UI pass (the Vertex layout is
  // XYZRHW, diffuse, specular, one UV), over the canvas viewport as draw() maps them.
  Constants block{};block.viewport[2]=(float)canvas_width();block.viewport[3]=(float)canvas_height();
  block.texture_factor[0]=block.texture_factor[1]=block.texture_factor[2]=block.texture_factor[3]=1.0f;block.transformed=TRUE;block.textured=TRUE;
  block.color_op=state.stage[0][D3DTSS_COLOROP];block.alpha_op=state.stage[0][D3DTSS_ALPHAOP];
  block.color_arg1=state.stage[0][D3DTSS_COLORARG1];block.color_arg2=state.stage[0][D3DTSS_COLORARG2];
  block.alpha_arg1=state.stage[0][D3DTSS_ALPHAARG1];block.alpha_arg2=state.stage[0][D3DTSS_ALPHAARG2];
  block.alpha_func=D3DCMP_ALWAYS;block.hud_opacity=1.0f;
  D3D12UiDraw quads;quads.texture=texture12.get();quads.program=D3D12UiProgram::game;
  quads.blend=fixed_blend_choice(TRUE,D3DBLEND_SRCALPHA,D3DBLEND_INVSRCALPHA,false);
  quads.sampler=world_texture_sampler(state.stage[0][D3DTSS_MINFILTER],state.stage[0][D3DTSS_MAGFILTER],state.stage[0][D3DTSS_MIPFILTER],
   state.stage[0][D3DTSS_ADDRESSU],state.stage[0][D3DTSS_ADDRESSV],false,
   1u<<configured_integer(debug_overlay_.config(),"anisotropy",3)).description;
  quads.triangle_list=true;quads.vertex_count=UINT(vertices.size());
  D3D12UiPass pass;pass.vertices=vertices.data();pass.vertex_bytes=UINT(vertices.size()*sizeof(HudVertex));pass.vertex_stride=sizeof(HudVertex);
  pass.constants=&block;pass.constant_block_bytes=sizeof(Constants);pass.constant_blocks=1;pass.draws=&quads;pass.draw_count=1;
  const HRESULT drawn=d3d12_->draw_ui(pass);
  if(SUCCEEDED(drawn))hud_frames_frame_=native_frame_index_;
  return drawn;
 }
 // draw() binds everything else it reads; nothing found is restored (frame_plan.h).
 (void)plan_pass("hud_asset_frames",rtv_.Get(),dsv_.Get());
 context_->GSSetShader(nullptr,nullptr,0);context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);
 const HRESULT hr=draw(D3DPT_TRIANGLELIST,D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_SPECULAR|D3DFVF_TEX1,
             vertices.data(),UINT(vertices.size()),nullptr,0,state,nullptr,SemanticKind::ui,0,0,{},texture.Get());
 if(SUCCEEDED(hr))hud_frames_frame_=native_frame_index_;
 return hr;
} catch(const std::bad_alloc&) {return E_OUTOFMEMORY;}

HRESULT Renderer::draw_frame_take(std::vector<LegacyDrawPacket>& packets,std::vector<std::uint8_t>& vertex_bytes,std::vector<WORD>& indices){
 if(!scene_queue())return draw_frame(packets,vertex_bytes,indices);
 if(packets.empty())return S_OK;
 // Core mode: each texture's pending write goes to the host first (ring
 // order), and the packets name the surfaces by handle.
 if(core_mode_){
  scene_ui::DrawFrame command;
  try{
   command.textures.resize(packets.size());
   for(std::size_t index=0;index<packets.size();++index){
    auto& packet=packets[index];
    if(!packet.texture)continue;
    SurfaceImage* image=packet.texture;packet.texture=nullptr;
    if(SUCCEEDED(core_surface_write(*image)))command.textures[index]=scene_ui::surface_handle(*image);
   }
  }catch(...){
   for(auto& packet:packets)packet.texture=nullptr;  // never a dangling game pointer
   command.textures.clear();
  }
  command.packets=std::move(packets);command.vertex_bytes=std::move(vertex_bytes);command.indices=std::move(indices);
  scene_ui_pending_=true;
  core_post_command(command,"draw_frame");
  packets.clear();vertex_bytes.clear();indices.clear();
  return S_OK;
 }
 // Each distinct texture becomes one pixel-free snapshot with its GPU home
 // current at this point of the stream (game-thread upload if needed).
 // The payload names each packet's texture by handle; the snapshots stay in
 // the command's residency.
 scene_ui::DrawFrame command;scene_ui::Residency residency;
 try{
  command.textures.resize(packets.size());
  for(std::size_t index=0;index<packets.size();++index){
   auto& packet=packets[index];
   if(!packet.texture)continue;
   SurfaceImage* original=packet.texture;packet.texture=nullptr;
   scene_ui::SurfaceHandle handle=scene_ui::surface_handle(*original);
   if(!residency.find(handle)){
    auto snapshot=scene_surface(original);if(!snapshot)continue;
    handle=scene_ui::surface_handle(*snapshot);residency.add({handle,std::move(snapshot)});
   }
   command.textures[index]=handle;
  }
 }catch(...){
  scene_sync("draw_frame_allocation");
  for(auto& packet:packets)packet.texture=nullptr;  // never a dangling game pointer
  command.textures.clear();
 }
 command.packets=std::move(packets);command.vertex_bytes=std::move(vertex_bytes);command.indices=std::move(indices);
 scene_ui_pending_=true;
 scene_post_frame([this,command=std::move(command),residency=std::move(residency)]() mutable{
  ui_run_draw_frame(command,residency);});
 packets.clear();vertex_bytes.clear();indices.clear();
 return S_OK;
}

HRESULT Renderer::draw_frame(const std::vector<LegacyDrawPacket>& packets,const std::vector<std::uint8_t>& vertex_bytes,const std::vector<WORD>& indices){SCENE_ENTRY("draw_frame");
 for(const auto& packet:packets){camp::VisualScope placement(packet.camp_placement,packet.camp_placement_active);const void* vertices=vertex_bytes.data()+packet.vertex_offset;const WORD* packetIndices=packet.index_count?indices.data()+packet.index_offset:nullptr;HRESULT hr=draw(packet.type,packet.fvf,vertices,packet.vertex_count,packetIndices,packet.index_count,packet.state,packet.texture,packet.semantic_kind,packet.caller_rva,packet.legacy_draw_count,packet.hud_composition);if(FAILED(hr))return hr;}return S_OK;
}

// Fixed embedded camp artwork, independent of native resource caches.
HRESULT Renderer::load_camp_art(){
 HMODULE module=nullptr;
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
     reinterpret_cast<LPCWSTR>(&append_shader_groups),&module))return E_FAIL;
 const HRESULT apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 if(FAILED(apartment)&&apartment!=RPC_E_CHANGED_MODE)return apartment;
 struct Apartment { bool release; ~Apartment(){if(release)CoUninitialize();} } scope{SUCCEEDED(apartment)};
 ComPtr<IWICImagingFactory> factory;
 HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
 if(FAILED(hr))return hr;
 // Three fixed embedded camp assets share the existing device lifetime.
 const wchar_t* names[]{L"EID3D11_CAMP_FRAME",L"EID3D11_CAMP_CONTROLS",L"EID3D11_CAMP_CONSTRUCTION"};
 for(unsigned asset=0;asset<3;++asset){
  const HRSRC resource=FindResourceW(module,names[asset],MAKEINTRESOURCEW(10));
  const HGLOBAL loaded=resource?LoadResource(module,resource):nullptr;
  auto* bytes=loaded?static_cast<BYTE*>(LockResource(loaded)):nullptr;
  const DWORD size=resource?SizeofResource(module,resource):0;
  if(!bytes||!size)return E_FAIL;
  ComPtr<IWICStream> stream;ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> converter;
  if(FAILED(hr=factory->CreateStream(&stream))||FAILED(hr=stream->InitializeFromMemory(bytes,size))||
     FAILED(hr=factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder))||
     FAILED(hr=decoder->GetFrame(0,&frame)))return hr;
  UINT width=0,height=0;
  if(FAILED(hr=frame->GetSize(&width,&height)))return hr;
  if(width!=(asset==2?1254u:1536u)||height!=(asset==2?1254u:1024u))return E_INVALIDARG;
  if(FAILED(hr=factory->CreateFormatConverter(&converter))||
     FAILED(hr=converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return hr;
  std::vector<BYTE> pixels(static_cast<size_t>(width)*height*4);
  if(FAILED(hr=converter->CopyPixels(nullptr,width*4,static_cast<UINT>(pixels.size()),pixels.data())))return hr;
  if(d3d12_){
   D3D12TextureRef& art=camp_art_d3d12_[asset];
   if(FAILED(hr=d3d12_->create_texture(width,height,DXGI_FORMAT_B8G8R8A8_UNORM,art))||
      FAILED(hr=d3d12_->write_texture(*art,RECT{0,0,LONG(width),LONG(height)},pixels.data(),width*4))){art.reset();return hr;}
   continue;
  }
  D3D11_TEXTURE2D_DESC td{};td.Width=width;td.Height=height;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;
  td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.Usage=D3D11_USAGE_IMMUTABLE;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  const D3D11_SUBRESOURCE_DATA initial{pixels.data(),width*4,0};
  ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11ShaderResourceView> srv;
  if(FAILED(hr=device_->CreateTexture2D(&td,&initial,&texture))||
     FAILED(hr=device_->CreateShaderResourceView(texture.Get(),nullptr,&srv)))return hr;
  if(asset==0){camp_art_texture_=std::move(texture);camp_art_srv_=std::move(srv);}
  else if(asset==1){camp_control_texture_=std::move(texture);camp_control_srv_=std::move(srv);}
  else{camp_construction_texture_=std::move(texture);camp_construction_srv_=std::move(srv);}
 }
 return S_OK;
}

HRESULT Renderer::begin_camp_popup(bool clear) {
 if(scene_queue()&&core_mode_){core_post_command(scene_ui::BeginCampPopup{clear},"begin_camp_popup");return S_OK;}
 if(scene_queue()){scene_post_frame([this,command=scene_ui::BeginCampPopup{clear}]{report_scene_failure("begin_camp_popup",begin_camp_popup(command.clear));});return S_OK;}
 // D3D12 backend: the popup target through the executor; native draws and
 // UI blits go there until end_camp_popup (as rtv_/dsv_ do on D3D11).
 if(d3d12_){
  if(camp_popup_active_||(!clear&&!camp_popup_ready_))return E_UNEXPECTED;
  HRESULT hr=composite_world();if(FAILED(hr))return hr;
  if(camp_popup_width_!=output_width_||camp_popup_height_!=output_height_||!camp_popup_d3d12_){
   reset_camp_popup();
   if(FAILED(hr=d3d12_->create_target(output_width_,output_height_,camp_popup_d3d12_))){reset_camp_popup();return hr;}
   camp_popup_width_=output_width_;camp_popup_height_=output_height_;
  }
  d3d12_->set_target(camp_popup_d3d12_.get());
  const float background[]{0,0,0,1};
  d3d12_->clear_target(clear?background:nullptr,true);
  camp_popup_active_=true;camp_popup_ready_=false;
  return S_OK;
 }
 if(camp_popup_active_||!context_||!device_||(!clear&&!camp_popup_ready_))return E_UNEXPECTED;
 HRESULT hr=composite_world();if(FAILED(hr))return hr;
 if(camp_popup_width_!=output_width_||camp_popup_height_!=output_height_||!camp_popup_rtv_){
  reset_camp_popup();
  D3D11_TEXTURE2D_DESC desc{};desc.Width=output_width_;desc.Height=output_height_;desc.MipLevels=desc.ArraySize=1;
  desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
  if(FAILED(hr=device_->CreateTexture2D(&desc,nullptr,&camp_popup_color_))||
     FAILED(hr=device_->CreateRenderTargetView(camp_popup_color_.Get(),nullptr,&camp_popup_rtv_))||
     FAILED(hr=device_->CreateShaderResourceView(camp_popup_color_.Get(),nullptr,&camp_popup_srv_))){reset_camp_popup();return hr;}
  desc.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
  if(FAILED(hr=device_->CreateTexture2D(&desc,nullptr,&camp_popup_depth_))||
     FAILED(hr=device_->CreateDepthStencilView(camp_popup_depth_.Get(),nullptr,&camp_popup_dsv_))){reset_camp_popup();return hr;}
  camp_popup_width_=output_width_;camp_popup_height_=output_height_;
 }
 // The game's target state returns to these at the popup's end. The popup
 // restores no pipeline state: every pass after it sets its own depth state
 // and viewport (frame_plan.h).
 camp_popup_saved_rtv_=rtv_;camp_popup_saved_dsv_=dsv_;
 // Prior frame's popup may still be bound for sampling after the UI flush.
 ID3D11ShaderResourceView* empty=nullptr;context_->PSSetShaderResources(0,1,&empty);
 rtv_=camp_popup_rtv_;dsv_=camp_popup_dsv_;camp_popup_active_=true;camp_popup_ready_=false;
 const PlanPass& pass=plan_pass("camp_popup",rtv_.Get(),dsv_.Get());
 bind_plan_targets(pass);
 if(clear){const float background[]{0,0,0,1};context_->ClearRenderTargetView(plan_rtv(pass),background);}
 context_->ClearDepthStencilView(plan_dsv(pass),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);
 return S_OK;
}
void Renderer::end_camp_popup(bool complete) noexcept {
 if(scene_queue()&&core_mode_){core_post_command(scene_ui::EndCampPopup{complete},"end_camp_popup");return;}
 if(scene_queue()){scene_post_frame([this,command=scene_ui::EndCampPopup{complete}]{end_camp_popup(command.complete);});return;}
 if(d3d12_){if(camp_popup_active_){d3d12_->set_target(nullptr);camp_popup_active_=false;camp_popup_ready_=complete;}return;}
 if(!camp_popup_active_)return;
 rtv_=camp_popup_saved_rtv_;dsv_=camp_popup_saved_dsv_;
 context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),dsv_.Get());
 camp_popup_saved_rtv_.Reset();camp_popup_saved_dsv_.Reset();
 camp_popup_active_=false;camp_popup_ready_=complete;
}
void Renderer::reset_camp_popup() noexcept {
 if(scene_queue()&&core_mode_){core_post_command(scene_ui::ResetCampPopup{},"reset_camp_popup");return;}
 if(scene_queue()){scene_post([this,command=scene_ui::ResetCampPopup{}]{(void)command;reset_camp_popup();});return;}
 if(camp_popup_active_)end_camp_popup(false);
 camp_popup_srv_.Reset();camp_popup_rtv_.Reset();camp_popup_dsv_.Reset();
 camp_popup_color_.Reset();camp_popup_depth_.Reset();
 camp_popup_d3d12_.reset();
 camp_popup_width_=camp_popup_height_=0;camp_popup_ready_=false;
}

void Renderer::queue_camp_decoration(const CampDecoration& frame, bool include_popup) noexcept {
 const auto previous_size=pending_blits_.size();
 const bool publish_popup=include_popup&&frame.card_count&&camp_popup_ready_&&(camp_popup_srv_||camp_popup_d3d12_);
 if(include_popup)camp_popup_ready_=false;
 if(include_popup&&frame.card_count&&!publish_popup)camp_inventory_popup_failed();
 try{
  const bool title=frame.title_plate.right>frame.title_plate.left;
  if((title||frame.count||frame.thumb_count||frame.divider_count||publish_popup)&&!camp_art_attempted_){
   camp_art_attempted_=true;if(FAILED(load_camp_art()))camp_inventory_art_failed();
  }
  const auto nine_slice=[&](const RECT& r,bool control,float frame_corner){
   const auto& texture=control?camp_control_texture_:camp_art_texture_;
   const auto& srv=control?camp_control_srv_:camp_art_srv_;
   const auto& art12=camp_art_d3d12_[control?1:0];
   if(!srv&&!art12)return;
   constexpr float frame_x[]{44,202,1334,1492},frame_y[]{78,232,792,946};
   constexpr float control_x[]{527,559,959,991},control_y[]{219,251,346,378};
   const auto* xs=control?control_x:frame_x;
   const auto* ys=control?control_y:frame_y;
   const float extent=float((std::min)(r.right-r.left,r.bottom-r.top));
   const float corner=control?(std::min)({20.f/3*frame.pixel_unit,float(r.bottom-r.top)/3,float(r.right-r.left)*.5f}):
       (std::min)(frame_corner*frame.pixel_unit,extent*.5f);
   const float dx[]{float(r.left),r.left+corner,r.right-corner,float(r.right)};
   const float dy[]{float(r.top),r.top+corner,r.bottom-corner,float(r.bottom)};
   for(unsigned y=0;y<3;++y)for(unsigned x=0;x<3;++x){
    if(!control&&x==1&&y==1)continue;
    if(dx[x+1]<=dx[x]||dy[y+1]<=dy[y])continue;
    PendingBlit piece{};piece.gpu=true;piece.texture=texture;piece.srv=srv;piece.d3d12=art12;
    piece.width=1536;piece.height=1024;
    piece.destination={LONG(std::lround(dx[x])),LONG(std::lround(dy[y])),LONG(std::lround(dx[x+1])),LONG(std::lround(dy[y+1]))};
    piece.u0=xs[x]/1536;piece.u1=xs[x+1]/1536;piece.v0=ys[y]/1024;piece.v1=ys[y+1]/1024;
    pending_blits_.push_back(std::move(piece));
   }
  };
  // Decoration is consumed before the native root title/font blits.
  if(title)nine_slice(frame.title_plate,true,0);
  for(unsigned panel=0;panel<frame.count;++panel){
   nine_slice(frame.panels[panel].bounds,false,frame.panels[panel].corner);
  }
  for(unsigned thumb=0;thumb<frame.thumb_count;++thumb)nine_slice(frame.thumbs[thumb],true,0);
  if(camp_control_srv_||camp_art_d3d12_[1])for(unsigned divider=0;divider<frame.divider_count;++divider){
   PendingBlit piece{};piece.gpu=true;piece.texture=camp_control_texture_;piece.srv=camp_control_srv_;piece.d3d12=camp_art_d3d12_[1];
   piece.width=1536;piece.height=1024;piece.destination=frame.dividers[divider].bounds;
   piece.quarter_turn=frame.dividers[divider].quarter_turn;
   piece.u0=1036.f/1536;piece.u1=1504.f/1536;piece.v0=265.f/1024;piece.v1=356.f/1024;
   pending_blits_.push_back(std::move(piece));
  }
  if(publish_popup)for(unsigned card=0;card<frame.card_count;++card){
   const auto& r=frame.cards[card];
   PendingBlit popup{};popup.gpu=true;popup.force_opaque_alpha=true;
   popup.texture=camp_popup_color_;popup.srv=camp_popup_srv_;popup.d3d12=camp_popup_d3d12_;
   popup.width=camp_popup_width_;popup.height=camp_popup_height_;popup.destination=r;
   // Card bounds use canvas pixels; the popup target uses output pixels.
   // Normalize through the canvas so Apply preserves the rendered crop.
   popup.u0=float(r.left)/canvas_width();popup.u1=float(r.right)/canvas_width();
   popup.v0=float(r.top)/canvas_height();popup.v1=float(r.bottom)/canvas_height();
   pending_blits_.push_back(std::move(popup));
   nine_slice(r,false,15);
  }
  if(publish_popup&&frame.construction_icon_visible&&(camp_construction_srv_||camp_art_d3d12_[2])){
   PendingBlit icon{};icon.gpu=true;icon.texture=camp_construction_texture_;icon.srv=camp_construction_srv_;icon.d3d12=camp_art_d3d12_[2];
   icon.width=icon.height=1254;icon.destination=frame.construction_icon;
   icon.u0=icon.v0=0;icon.u1=icon.v1=1;
   pending_blits_.push_back(std::move(icon));
  }
  if(include_popup)camp_inventory_popup_published(publish_popup);
 }catch(const std::bad_alloc&){pending_blits_.resize(previous_size);camp_inventory_popup_published(false);camp_inventory_popup_failed();camp_inventory_art_failed();}
}

// The game cursor is queued after all other UI, including camp decoration.
HRESULT Renderer::queue_cursor(std::uint64_t cache_key,const std::vector<std::uint32_t>& pixels,UINT width,UINT height,const RECT& destination){
 if(scene_queue()){
  scene_ui::Cursor command{cache_key,{},width,height,destination};
  try{command.pixels=pixels;}catch(...){return E_OUTOFMEMORY;}
  if(width&&height)scene_ui_pending_=true;
  if(core_mode_){core_post_command(command,"queue_cursor");return S_OK;}
  scene_post_frame([this,command=std::move(command)]{
   report_scene_failure("queue_cursor",queue_cursor(command.cache_key,command.pixels,command.width,command.height,command.destination));});
  return S_OK;
 }
 pending_cursor_={};cursor_pending_=false;
 if((!d3d12_&&(!swap_||!context_))||!width||!height||pixels.size()!=(size_t)width*height)return S_OK;
 RECT clipped{std::max<LONG>(0,destination.left),std::max<LONG>(0,destination.top),std::min<LONG>((LONG)output_width_,destination.right),std::min<LONG>((LONG)output_height_,destination.bottom)};
 if(clipped.right<=clipped.left||clipped.bottom<=clipped.top)return S_OK;
 const auto cached=overlay_textures_.find(cache_key);
 const bool cacheHit=cached!=overlay_textures_.end()&&cached->second.resident()&&cached->second.generation==1&&cached->second.width==width&&cached->second.height==height;
 const float dw=(float)(destination.right-destination.left),dh=(float)(destination.bottom-destination.top);
 try{PendingBlit pending{};pending.native_canvas=false;pending.source_id=cache_key;pending.cache_key=cache_key;pending.generation=1;pending.cached=cacheHit;pending.source=RECT{0,0,(LONG)width,(LONG)height};pending.destination=clipped;pending.width=width;pending.height=height;pending.pitch=width*4;
  pending.u0=(clipped.left-destination.left)/dw;pending.v0=(clipped.top-destination.top)/dh;pending.u1=(clipped.right-destination.left)/dw;pending.v1=(clipped.bottom-destination.top)/dh;
  if(!cacheHit)pending.pixels=pixels;pending_cursor_=std::move(pending);cursor_pending_=true;}
 catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 return S_OK;
}

bool Renderer::interface_scaled_ps(){
 if(interface_scaled_ps_)return true;
 if(interface_scaled_ps_failed_||!device_||device_->GetFeatureLevel()<D3D_FEATURE_LEVEL_11_0)return false;
 const auto program=world_support_shader_program(WorldSupportShader::interface_scaled_ps);
 ComPtr<ID3DBlob> blob,err;
 if(FAILED(compile_shader(program,&blob,&err))||FAILED(create_shader(device_.Get(),blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&interface_scaled_ps_))){interface_scaled_ps_failed_=true;return false;}
 return true;
}
bool Renderer::interface_text_ps(){
 if(interface_text_ps_)return true;
 if(interface_text_ps_failed_||!device_||device_->GetFeatureLevel()<D3D_FEATURE_LEVEL_11_0)return false;
 const ShaderProgram program=world_support_shader_program(WorldSupportShader::interface_text_ps);
 ComPtr<ID3DBlob> blob,err;
 if(FAILED(compile_shader(program,&blob,&err))||FAILED(create_shader(device_.Get(),blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&interface_text_ps_))){interface_text_ps_failed_=true;return false;}
 return true;
}
// Clip through the final composition, before deriving source UVs. Relocating or
// shrinking an interface surface must not discard pixels at its old position.
static RECT clip_ui_destination(const RECT& destination,const widescreen::HudComposition& h,UINT width,UINT height) noexcept {
 const float left=h.pivot_x+(0.f-h.offset_x-h.pivot_x)/h.scale;
 const float top=h.pivot_y+(0.f-h.offset_y-h.pivot_y)/h.scale_y;
 const float right=h.pivot_x+(float(width)-h.offset_x-h.pivot_x)/h.scale;
 const float bottom=h.pivot_y+(float(height)-h.offset_y-h.pivot_y)/h.scale_y;
 return {std::max(destination.left,LONG(std::floor(left))),std::max(destination.top,LONG(std::floor(top))),
         std::min(destination.right,LONG(std::ceil(right))),std::min(destination.bottom,LONG(std::ceil(bottom)))};
}
// Scene mode, game thread: blit() up to its queued quad. Camp pieces and
// decoration, clipping and the CPU conversion run here against the game-side
// mirror of the overlay cache; the scene thread only queues the quads.
HRESULT Renderer::make_scene_blit(const SurfaceImage& source,const RECT& destination,const RECT* source_rect,const DDCOLORKEY* source_key,bool interface_space,bool native_canvas,std::vector<PendingBlit>& output){
 if(overlay_mirror_reset_.exchange(false,std::memory_order_acq_rel))overlay_mirror_.clear();
 const bool released_gpu=released_surface_gpu_blit(source,source_key);
 if(source.cpu_released&&!released_gpu&&!source.gpu_captured)return E_UNEXPECTED;
 if((!core_mode_&&!d3d12_&&(!swap_||!context_))||(!released_gpu&&!source.gpu_captured&&(source.pixels.empty()||(source.format.dwFlags&DDPF_FOURCC)||!(source.format.dwFlags&DDPF_RGB))))return S_OK;
 CampDecoration camp_frame;
 if(camp_inventory_take_decoration(camp_frame)){scene_ui_pending_=true;if(core_mode_)core_post_command(scene_ui::CampDecorationFrame{camp_frame},"camp_decoration");else scene_post_frame([this,command=scene_ui::CampDecorationFrame{camp_frame}]{queue_camp_decoration(command.frame);});}
 RECT initial_source=source_rect?*source_rect:RECT{0,0,LONG(source.width),LONG(source.height)};
 CampBlitPiece pieces[kCampBlitPieces];const int piece_count=camp_inventory_blit(destination,initial_source,pieces);
 if(piece_count>=0){
  struct BlitScope{bool previous=camp::remapping_blit;BlitScope(){camp::remapping_blit=true;}~BlitScope(){camp::remapping_blit=previous;}} remapping;
  for(int i=0;i<piece_count;++i){const HRESULT result=make_scene_blit(source,pieces[i].destination,&pieces[i].source,source_key,false,native_canvas,output);if(FAILED(result))return result;}
  return S_OK;
 }
 RECT sr=source_rect?*source_rect:RECT{0,0,(LONG)source.width,(LONG)source.height};sr.left=std::clamp<LONG>(sr.left,0,(LONG)source.width);sr.top=std::clamp<LONG>(sr.top,0,(LONG)source.height);sr.right=std::clamp<LONG>(sr.right,sr.left,(LONG)source.width);sr.bottom=std::clamp<LONG>(sr.bottom,sr.top,(LONG)source.height);
 const LONG dw=destination.right-destination.left,dh=destination.bottom-destination.top,sw=sr.right-sr.left,sh=sr.bottom-sr.top;if(dw<=0||dh<=0||sw<=0||sh<=0)return S_OK;
 const widescreen::HudComposition composition=interface_space&&widescreen::interface_owned?widescreen::hud_blit:widescreen::HudComposition{};
 RECT clipped=clip_ui_destination(destination,composition,native_canvas?canvas_width():output_width_,native_canvas?canvas_height():output_height_);if(clipped.right<=clipped.left||clipped.bottom<=clipped.top)return S_OK;
 PendingBlit pending{};
 if((source.gpu_captured||released_gpu)&&(source.srv||(core_mode_&&source.gpu_captured))){
  pending.native_canvas=native_canvas;pending.source_id=source.id;pending.source_is_surface=true;pending.gpu=true;pending.force_opaque_alpha=!source_key;pending.destination=clipped;pending.composition=composition;pending.width=source.width;pending.height=source.height;pending.texture=source.texture;pending.srv=source.srv;
  const float du0=(float)(clipped.left-destination.left)/dw,dv0=(float)(clipped.top-destination.top)/dh,du1=(float)(clipped.right-destination.left)/dw,dv1=(float)(clipped.bottom-destination.top)/dh;
  pending.u0=(sr.left+du0*sw)/source.width;pending.v0=(sr.top+dv0*sh)/source.height;pending.u1=(sr.left+du1*sw)/source.width;pending.v1=(sr.top+dv1*sh)/source.height;
 }else{
  auto hashValue=[](std::uint64_t& seed,std::uint64_t value){seed^=value+0x9e3779b97f4a7c15ull+(seed<<6)+(seed>>2);};std::uint64_t cacheKey=source.id;hashValue(cacheKey,(std::uint32_t)sr.left);hashValue(cacheKey,(std::uint32_t)sr.top);hashValue(cacheKey,(std::uint32_t)sr.right);hashValue(cacheKey,(std::uint32_t)sr.bottom);hashValue(cacheKey,source_key?source_key->dwColorSpaceLowValue:0);hashValue(cacheKey,source_key?source_key->dwColorSpaceHighValue:0);hashValue(cacheKey,source_key?1:0);
  const auto mirror=overlay_mirror_.find(cacheKey);
  const bool cachedMatches=mirror!=overlay_mirror_.end()&&mirror->second.surface_id==source.id&&mirror->second.width==(UINT)sw&&mirror->second.height==(UINT)sh&&EqualRect(&mirror->second.source,&sr)&&mirror->second.keyed==(source_key!=nullptr)&&(!source_key||(mirror->second.color_key.dwColorSpaceLowValue==source_key->dwColorSpaceLowValue&&mirror->second.color_key.dwColorSpaceHighValue==source_key->dwColorSpaceHighValue));
  RECT update{};const OverlayRefresh refresh=overlay_refresh(cachedMatches,cachedMatches?mirror->second.generation:0,source,sr,update);const bool cacheHit=refresh==OverlayRefresh::reuse;const bool partial=refresh==OverlayRefresh::partial;if(!partial)update=RECT{0,0,sw,sh};const LONG uw=update.right-update.left,uh=update.bottom-update.top;
  const UINT bytes=std::max<UINT>(1,source.format.dwRGBBitCount/8);const bool directBgra=bytes==4&&source.format.dwRBitMask==0x00ff0000&&source.format.dwGBitMask==0x0000ff00&&source.format.dwBBitMask==0x000000ff;const bool forceOpaqueAlpha=directBgra&&!source_key;std::vector<std::uint32_t> pixels;
  if(!cacheHit){
   const LONG ux=sr.left+update.left,uy=sr.top+update.top;try{pixels.resize((size_t)std::max<LONG>(uw,0)*std::max<LONG>(uh,0));}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
   if(uw<=0||uh<=0){}else if(forceOpaqueAlpha){const auto* first=source.pixels.data()+(size_t)uy*source.pitch+(size_t)ux*bytes;copy_direct_bgra_rows(first,source.pitch,pixels.data(),(UINT)uw,(UINT)uh);}
   else{
    const std::uint32_t rgbMask=source.format.dwRBitMask|source.format.dwGBitMask|source.format.dwBBitMask;const std::uint8_t* const sourcePixels=source.pixels.data();
    const UINT loaded=std::min<UINT>(bytes,4);const auto load=[loaded](const std::uint8_t* p){std::uint32_t raw{};switch(loaded){case 4:std::memcpy(&raw,p,4);break;case 3:std::memcpy(&raw,p,3);break;case 2:std::memcpy(&raw,p,2);break;default:std::memcpy(&raw,p,1);break;}return raw;};
    const bool keyed=source_key!=nullptr;const std::uint32_t keyLow=keyed?source_key->dwColorSpaceLowValue:0,keyHigh=keyed?source_key->dwColorSpaceHighValue:0;
    for(LONG y=0;y<uh;++y){const std::uint8_t* row=sourcePixels+(size_t)(uy+y)*source.pitch+(size_t)ux*bytes;std::uint32_t* out=pixels.data()+(size_t)y*uw;
     if(directBgra){for(LONG x=0;x<uw;++x,row+=4){std::uint32_t raw;std::memcpy(&raw,row,4);const std::uint32_t keyValue=raw&rgbMask;const bool keyMatch=keyed&&keyValue>=keyLow&&keyValue<=keyHigh;out[x]=(raw&0x00ffffffu)|(keyMatch?0u:0xff000000u);}continue;}
     for(LONG x=0;x<uw;++x,row+=bytes){const std::uint32_t raw=load(row);const std::uint32_t keyValue=raw&rgbMask;const bool keyMatch=keyed&&keyValue>=keyLow&&keyValue<=keyHigh;const auto a=keyMatch?0u:255u;const auto r=expand_channel(raw,source.format.dwRBitMask,0),g=expand_channel(raw,source.format.dwGBitMask,0),b=expand_channel(raw,source.format.dwBBitMask,0);out[x]=(std::uint32_t(a)<<24)|(std::uint32_t(r)<<16)|(std::uint32_t(g)<<8)|b;}}
   }
  }
  pending.native_canvas=native_canvas;pending.source_id=source.id;pending.source_is_surface=true;pending.cache_key=cacheKey;pending.generation=source.generation;pending.base_generation=partial?mirror->second.generation:0;pending.partial=partial;pending.update=update;pending.keyed=source_key!=nullptr;pending.cached=cacheHit;pending.force_opaque_alpha=forceOpaqueAlpha;pending.source=sr;if(source_key)pending.color_key=*source_key;pending.destination=clipped;pending.composition=composition;pending.width=(UINT)sw;pending.height=(UINT)sh;pending.pitch=(UINT)std::max<LONG>(uw,1)*4;pending.u0=(float)(clipped.left-destination.left)/dw;pending.v0=(float)(clipped.top-destination.top)/dh;pending.u1=(float)(clipped.right-destination.left)/dw;pending.v1=(float)(clipped.bottom-destination.top)/dh;pending.pixels=std::move(pixels);
  if(!cacheHit){
   try{OverlayMirror next{};next.surface_id=source.id;next.generation=source.generation;next.width=(UINT)sw;next.height=(UINT)sh;next.source=sr;next.keyed=source_key!=nullptr;if(source_key)next.color_key=*source_key;overlay_mirror_[cacheKey]=next;}
   catch(...){overlay_mirror_.clear();}
  }
 }
 try{output.push_back(std::move(pending));}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 return S_OK;
}

HRESULT Renderer::blit_srv(ID3D11ShaderResourceView* image,const RECT& destination,const RECT* source_rect,bool opaque){
 if(!swap_||!context_||!image)return S_OK;
 ComPtr<ID3D11Resource> resource;image->GetResource(&resource);
 ComPtr<ID3D11Texture2D> texture;if(!resource||FAILED(resource.As(&texture)))return E_INVALIDARG;
 D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
 RECT sr=source_rect?*source_rect:RECT{0,0,(LONG)desc.Width,(LONG)desc.Height};
 sr.left=std::clamp<LONG>(sr.left,0,(LONG)desc.Width);sr.top=std::clamp<LONG>(sr.top,0,(LONG)desc.Height);
 sr.right=std::clamp<LONG>(sr.right,sr.left,(LONG)desc.Width);sr.bottom=std::clamp<LONG>(sr.bottom,sr.top,(LONG)desc.Height);
 const LONG dw=destination.right-destination.left,dh=destination.bottom-destination.top,sw=sr.right-sr.left,sh=sr.bottom-sr.top;
 if(dw<=0||dh<=0||sw<=0||sh<=0)return S_OK;
 const widescreen::HudComposition composition=widescreen::interface_owned?widescreen::hud_blit:widescreen::HudComposition{};
 const RECT clipped=clip_ui_destination(destination,composition,canvas_width(),canvas_height());
 if(clipped.right<=clipped.left||clipped.bottom<=clipped.top)return S_OK;
 try{
  PendingBlit pending{};pending.native_canvas=true;pending.source_id=reinterpret_cast<std::uintptr_t>(image);
  pending.gpu=true;pending.force_opaque_alpha=opaque;pending.destination=clipped;pending.composition=composition;
  pending.width=desc.Width;pending.height=desc.Height;pending.texture=texture;pending.srv=image;
  const float du0=(float)(clipped.left-destination.left)/dw,dv0=(float)(clipped.top-destination.top)/dh,
   du1=(float)(clipped.right-destination.left)/dw,dv1=(float)(clipped.bottom-destination.top)/dh;
  pending.u0=(sr.left+du0*sw)/desc.Width;pending.v0=(sr.top+dv0*sh)/desc.Height;
  pending.u1=(sr.left+du1*sw)/desc.Width;pending.v1=(sr.top+dv1*sh)/desc.Height;
  pending_blits_.push_back(std::move(pending));
 }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 return S_OK;
}

HRESULT Renderer::blit(const SurfaceImage& source,const RECT& destination,const RECT* source_rect,const DDCOLORKEY* source_key,bool interface_space,bool native_canvas){
 if(scene_queue()){
  std::vector<PendingBlit> pendings;
  const HRESULT hr=make_scene_blit(source,destination,source_rect,source_key,interface_space,native_canvas,pendings);
  if(FAILED(hr)||pendings.empty())return hr;
  scene_ui::Blits command;scene_ui::Residency residency;
  try{ui_quads(pendings,scene_ui::surface_handle(source),command.quads,residency);}
  catch(...){overlay_mirror_.clear();return E_OUTOFMEMORY;}  // the mirror must not claim quads that never queued
  scene_ui_pending_=true;
  if(core_mode_){core_post_command(command,"blit");return S_OK;}
  scene_post_frame([this,command=std::move(command),residency=std::move(residency)]() mutable{
   ui_run_blits(command.quads,residency);});
  return S_OK;
 }
 PerfScope perf(&perf_blit_ticks_);if(perf_enabled())++perf_blit_calls_;
 const bool attribution_profile=profiler_.attribution_enabled();AttributionQpcScope queue_profile(attribution_profile,native_frame_stats_.ui_blit_queue_cpu_qpc,native_frame_stats_.ui_blit_queue_calls);
 const bool released_gpu=released_surface_gpu_blit(source,source_key);
 // Only CPU-converted blits need a restored source mirror.
 if(source.cpu_released&&!released_gpu&&!source.gpu_captured)return E_UNEXPECTED;
 if((!d3d12_&&(!swap_||!context_))||(!released_gpu&&!source.gpu_captured&&(source.pixels.empty()||(source.format.dwFlags&DDPF_FOURCC)||!(source.format.dwFlags&DDPF_RGB))))return S_OK;
#ifndef EID3D11_EMBEDDED
 CampDecoration camp_frame;
 if(camp_inventory_take_decoration(camp_frame))queue_camp_decoration(camp_frame);
#endif
 RECT initial_source=source_rect?*source_rect:RECT{0,0,LONG(source.width),LONG(source.height)};
 CampBlitPiece pieces[kCampBlitPieces];const int piece_count=camp_inventory_blit(destination,initial_source,pieces);
 if(piece_count>=0){
  struct BlitScope{bool previous=camp::remapping_blit;BlitScope(){camp::remapping_blit=true;}~BlitScope(){camp::remapping_blit=previous;}} remapping;
  for(int i=0;i<piece_count;++i){const HRESULT result=blit(source,pieces[i].destination,&pieces[i].source,source_key,false,native_canvas);if(FAILED(result))return result;}
  return S_OK;
 }
 RECT sr=source_rect?*source_rect:RECT{0,0,(LONG)source.width,(LONG)source.height};sr.left=std::clamp<LONG>(sr.left,0,(LONG)source.width);sr.top=std::clamp<LONG>(sr.top,0,(LONG)source.height);sr.right=std::clamp<LONG>(sr.right,sr.left,(LONG)source.width);sr.bottom=std::clamp<LONG>(sr.bottom,sr.top,(LONG)source.height);const LONG dw=destination.right-destination.left,dh=destination.bottom-destination.top,sw=sr.right-sr.left,sh=sr.bottom-sr.top;if(dw<=0||dh<=0||sw<=0||sh<=0)return S_OK;
// Unkeyed copies preserve RGB without consuming framebuffer alpha.
 const widescreen::HudComposition composition=interface_space&&widescreen::interface_owned?widescreen::hud_blit:widescreen::HudComposition{};RECT clipped=clip_ui_destination(destination,composition,native_canvas?canvas_width():output_width_,native_canvas?canvas_height():output_height_);if(clipped.right<=clipped.left||clipped.bottom<=clipped.top)return S_OK;
 if((source.gpu_captured||released_gpu)&&(d3d12_?bool(source.d3d12):bool(source.srv))){try{PendingBlit pending{};pending.native_canvas=native_canvas;pending.source_id=source.id;pending.source_is_surface=true;pending.gpu=true;pending.force_opaque_alpha=!source_key;pending.destination=clipped;pending.composition=composition;pending.width=source.width;pending.height=source.height;pending.texture=source.texture;pending.srv=source.srv;pending.d3d12=source.d3d12;
#ifdef EID3D11_EMBEDDED
if(source_key){const HRESULT keyed=gpu_blit_key_.convert(device_.Get(),context_.Get(),source.srv.Get(),source.width,source.height,source.format.dwRBitMask,source.format.dwGBitMask,source.format.dwBBitMask,source_key->dwColorSpaceLowValue,source_key->dwColorSpaceHighValue,pending.texture,pending.srv);if(FAILED(keyed))return keyed;}
#endif
const float du0=(float)(clipped.left-destination.left)/dw,dv0=(float)(clipped.top-destination.top)/dh,du1=(float)(clipped.right-destination.left)/dw,dv1=(float)(clipped.bottom-destination.top)/dh;pending.u0=(sr.left+du0*sw)/source.width;pending.v0=(sr.top+dv0*sh)/source.height;pending.u1=(sr.left+du1*sw)/source.width;pending.v1=(sr.top+dv1*sh)/source.height;pending_blits_.push_back(std::move(pending));}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}return S_OK;}
 auto hashValue=[](std::uint64_t& seed,std::uint64_t value){seed^=value+0x9e3779b97f4a7c15ull+(seed<<6)+(seed>>2);};std::uint64_t cacheKey=source.id;hashValue(cacheKey,(std::uint32_t)sr.left);hashValue(cacheKey,(std::uint32_t)sr.top);hashValue(cacheKey,(std::uint32_t)sr.right);hashValue(cacheKey,(std::uint32_t)sr.bottom);hashValue(cacheKey,source_key?source_key->dwColorSpaceLowValue:0);hashValue(cacheKey,source_key?source_key->dwColorSpaceHighValue:0);hashValue(cacheKey,source_key?1:0);auto cached=overlay_textures_.find(cacheKey);
 // Refresh only what the source changed since the cached generation.
 const bool cachedMatches=cached!=overlay_textures_.end()&&cached->second.texture&&cached->second.srv&&cached->second.surface_id==source.id&&cached->second.width==(UINT)sw&&cached->second.height==(UINT)sh&&EqualRect(&cached->second.source,&sr)&&cached->second.keyed==(source_key!=nullptr)&&(!source_key||(cached->second.color_key.dwColorSpaceLowValue==source_key->dwColorSpaceLowValue&&cached->second.color_key.dwColorSpaceHighValue==source_key->dwColorSpaceHighValue));
 RECT update{};const OverlayRefresh refresh=overlay_refresh(cachedMatches,cachedMatches?cached->second.generation:0,source,sr,update);const bool cacheHit=refresh==OverlayRefresh::reuse;const bool partial=refresh==OverlayRefresh::partial;if(!partial)update=RECT{0,0,sw,sh};const LONG uw=update.right-update.left,uh=update.bottom-update.top;

 const UINT bytes=std::max<UINT>(1,source.format.dwRGBBitCount/8);const bool directBgra=bytes==4&&source.format.dwRBitMask==0x00ff0000&&source.format.dwGBitMask==0x0000ff00&&source.format.dwBBitMask==0x000000ff;const bool forceOpaqueAlpha=directBgra&&!source_key;std::vector<std::uint32_t> pixels;if(!cacheHit){AttributionQpcScope conversion_profile(attribution_profile,native_frame_stats_.ui_blit_conversion_cpu_qpc,native_frame_stats_.ui_blit_conversion_calls);if(attribution_profile)native_frame_stats_.ui_blit_conversion_pixels+=static_cast<std::uint64_t>(uw)*static_cast<std::uint64_t>(uh);if(!ui_blit_pixel_spares_.empty()){pixels=std::move(ui_blit_pixel_spares_.back());ui_blit_pixel_spares_.pop_back();}const LONG ux=sr.left+update.left,uy=sr.top+update.top;try{pixels.resize((size_t)uw*uh);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}if(uw<=0||uh<=0){}else if(forceOpaqueAlpha){const auto* first=source.pixels.data()+(size_t)uy*source.pitch+(size_t)ux*bytes;copy_direct_bgra_rows(first,source.pitch,pixels.data(),(UINT)uw,(UINT)uh);}else{const std::uint32_t rgbMask=source.format.dwRBitMask|source.format.dwGBitMask|source.format.dwBBitMask;const std::uint8_t* const sourcePixels=source.pixels.data();
  // Fixed-size loads: a per-pixel variable-length memcpy call cost more than the conversion.
  const UINT loaded=std::min<UINT>(bytes,4);const auto load=[loaded](const std::uint8_t* p){std::uint32_t raw{};switch(loaded){case 4:std::memcpy(&raw,p,4);break;case 3:std::memcpy(&raw,p,3);break;case 2:std::memcpy(&raw,p,2);break;default:std::memcpy(&raw,p,1);break;}return raw;};
  const bool keyed=source_key!=nullptr;const std::uint32_t keyLow=keyed?source_key->dwColorSpaceLowValue:0,keyHigh=keyed?source_key->dwColorSpaceHighValue:0;
  for(LONG y=0;y<uh;++y){const std::uint8_t* row=sourcePixels+(size_t)(uy+y)*source.pitch+(size_t)ux*bytes;std::uint32_t* out=pixels.data()+(size_t)y*uw;
   if(directBgra){for(LONG x=0;x<uw;++x,row+=4){std::uint32_t raw;std::memcpy(&raw,row,4);const std::uint32_t keyValue=raw&rgbMask;const bool keyMatch=keyed&&keyValue>=keyLow&&keyValue<=keyHigh;out[x]=(raw&0x00ffffffu)|(keyMatch?0u:0xff000000u);}continue;}
   for(LONG x=0;x<uw;++x,row+=bytes){const std::uint32_t raw=load(row);const std::uint32_t keyValue=raw&rgbMask;const bool keyMatch=keyed&&keyValue>=keyLow&&keyValue<=keyHigh;const auto a=keyMatch?0u:255u;const auto r=expand_channel(raw,source.format.dwRBitMask,0),g=expand_channel(raw,source.format.dwGBitMask,0),b=expand_channel(raw,source.format.dwBBitMask,0);out[x]=(std::uint32_t(a)<<24)|(std::uint32_t(r)<<16)|(std::uint32_t(g)<<8)|b;}}}}
 try{PendingBlit pending{};pending.native_canvas=native_canvas;pending.source_id=source.id;pending.source_is_surface=true;pending.cache_key=cacheKey;pending.generation=source.generation;pending.base_generation=partial?cached->second.generation:0;pending.partial=partial;pending.update=update;pending.keyed=source_key!=nullptr;pending.cached=cacheHit;pending.force_opaque_alpha=forceOpaqueAlpha;pending.source=sr;if(source_key)pending.color_key=*source_key;pending.destination=clipped;pending.composition=composition;pending.width=(UINT)sw;pending.height=(UINT)sh;pending.pitch=(UINT)std::max<LONG>(uw,1)*4;pending.u0=(float)(clipped.left-destination.left)/dw;pending.v0=(float)(clipped.top-destination.top)/dh;pending.u1=(float)(clipped.right-destination.left)/dw;pending.v1=(float)(clipped.bottom-destination.top)/dh;pending.pixels=std::move(pixels);pending_blits_.push_back(std::move(pending));}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}return S_OK;
}

// The fill behind a presented picture: black, or a darkened blur of the
// picture cropped to the screen's aspect (cover_*), drawn first from the
// picture's own texture over the whole output.
Renderer::PendingBlit Renderer::make_presentation_fill(const PendingBlit& picture,const widescreen::PresentationFit& fit) const{
 // Share the queued picture's texture identity, without copying its CPU
 // upload vector (the fill has no second upload).
 PendingBlit fill{};fill.native_canvas=false;
 fill.source_id=picture.source_id;fill.cache_key=picture.cache_key;fill.generation=picture.generation;fill.revision=picture.revision;
 fill.keyed=picture.keyed;fill.gpu=picture.gpu;fill.cached=true;fill.force_opaque_alpha=true;fill.presentation_fill=true;
 fill.source_is_surface=picture.source_is_surface;fill.source=picture.source;fill.color_key=picture.color_key;
 fill.width=picture.width;fill.height=picture.height;fill.pitch=picture.pitch;fill.texture=picture.texture;fill.srv=picture.srv;
 fill.destination=RECT{0,0,(LONG)output_width_,(LONG)output_height_};
 const bool blurred=configured_integer(debug_overlay_.config(),"pillarbox_fill",1)!=0;
 fill.backdrop=blurred;fill.diffuse=blurred?0xff707070u:0xff000000u;
 const float du=picture.u1-picture.u0,dv=picture.v1-picture.v0;
 fill.u0=picture.u0+du*fit.cover_u0;fill.u1=picture.u0+du*fit.cover_u1;
 fill.v0=picture.v0+dv*fit.cover_v0;fill.v1=picture.v0+dv*fit.cover_v1;
 return fill;
}

HRESULT Renderer::present_image(const SurfaceImage& source,const RECT* source_rect,const DDCOLORKEY* source_key,const RECT* placed){
 const RECT full{0,0,(LONG)output_width_,(LONG)output_height_};
 RECT sr=source_rect?*source_rect:RECT{0,0,(LONG)source.width,(LONG)source.height};
 const LONG sw=sr.right-sr.left,sh=sr.bottom-sr.top;
 if(sw<=0||sh<=0||!output_width_||!output_height_)
  return placed?blit(source,*placed,source_rect,source_key):blit(source,full,source_rect,source_key,false,false);
 const widescreen::PresentationFit fit=widescreen::presentation_fit((float)sw,(float)sh,(float)output_width_,(float)output_height_);
 // A placed picture keeps the game's own rectangle (surface coordinates, as
 // blit) and always gets the fill: the rest of the screen is not the game's
 // picture, and the world composite would otherwise show through it.
 const bool filled=placed||fit.boxed;
 const RECT picture_rect=placed?*placed:fit.boxed?RECT{fit.left,fit.top,fit.right,fit.bottom}:full;
 const bool picture_canvas=placed!=nullptr;
 if(scene_queue()){
  std::vector<PendingBlit> pendings;
  const HRESULT hr=make_scene_blit(source,picture_rect,&sr,source_key,false,picture_canvas,pendings);
  if(FAILED(hr))return hr;
  scene_ui::PresentImage command;scene_ui::Residency residency;
  command.fit=fit;command.filled=filled;command.fitted=!placed;
  try{ui_quads(pendings,scene_ui::surface_handle(source),command.quads,residency);}
  catch(...){overlay_mirror_.clear();return E_OUTOFMEMORY;}  // the mirror must not claim quads that never queued
  scene_ui_pending_=true;
  // The game's progress movie behind this picture, read here (game thread)
  // for the load reveal's capture_load_progress_movie on the scene thread.
  command.movie_valid=!placed&&bridge_progress_movie(command.movie);
  if(core_mode_){core_post_command(command,"present_image");return S_OK;}
  scene_post_frame([this,command=std::move(command),residency=std::move(residency)]() mutable{
   ui_run_present_image(command,residency);
  });
  return S_OK;
 }
 load_picture_this_frame_=true; // the load reveal keeps a copy of this frame
 if(!placed){
  load_picture_fitted_=true;     // composed here (renderer_load_progress.cpp redraws progress frames the same way)
  // Scene mode, inline on the game thread: the scene thread's present reads this copy.
  if(scene_mode()&&!on_scene_thread())progress_movie_input_valid_=bridge_progress_movie(progress_movie_input_);
 }
 if(!filled)return blit(source,full,&sr,source_key,false,false);
 const std::size_t before=pending_blits_.size();
 HRESULT hr=blit(source,picture_rect,&sr,source_key,false,picture_canvas);
 if(FAILED(hr)||pending_blits_.size()!=before+1)return hr;
 try{PendingBlit fill=make_presentation_fill(pending_blits_.back(),fit);pending_blits_.insert(pending_blits_.end()-1,std::move(fill));}
 catch(const std::bad_alloc&){pending_blits_.resize(before);return E_OUTOFMEMORY;}
 return S_OK;
}

#ifdef EID3D11_TESTING
std::size_t Renderer::test_pending_character_pane_quads() const noexcept {
 std::size_t count=0;for(const auto& pending:pending_blits_)if(pending.ui_epoch)++count;return count;
}
std::size_t Renderer::test_pending_character_pane_upload_bytes() const noexcept {
 std::size_t bytes=0;for(const auto& pending:pending_blits_)if(pending.ui_epoch){const std::size_t pixels=pending.text_asset?pending.text_asset->coverage.size():pending.pixels.size();if(pixels>(std::numeric_limits<std::size_t>::max)()/sizeof(std::uint32_t))return (std::numeric_limits<std::size_t>::max)();const std::size_t upload=pixels*sizeof(std::uint32_t);if(upload>(std::numeric_limits<std::size_t>::max)()-bytes)return (std::numeric_limits<std::size_t>::max)();bytes+=upload;}return bytes;
}
bool Renderer::test_first_character_pane_destination(
 RECT& destination) const noexcept {
 destination={};for(const auto& pending:pending_blits_)if(pending.ui_epoch){destination=pending.destination;return true;}return false;
}
bool Renderer::test_character_pane_overlay_cache_empty(
 std::uint64_t source_identity) const noexcept {
 const auto found=overlay_textures_.find(source_identity^0x4350474c59504831ull);return found==overlay_textures_.end()||(!found->second.texture&&!found->second.srv);
}
#endif

// A retained text surface (item card) presented where its blit would land:
// the same camp decoration and piece mapping as blit(), then the glyphs.
// Card frames keep their own composed scale (observe_scale false).
HRESULT Renderer::queue_retained_owned_text(
 const CharacterPaneTextFrame& frame,std::uint64_t ui_epoch,
 std::uint64_t frame_serial,const RECT& destination,
 const RECT* source_rect,float& composed_scale,const CharacterPaneTextFit* fit){
 composed_scale=0.f;
 if(!frame.width||!frame.height||!output_height_||!canvas_height())return E_INVALIDARG;
 CampDecoration camp_frame;
 if(camp_inventory_take_decoration(camp_frame)){
  if(scene_queue()){scene_ui_pending_=true;
   if(core_mode_)core_post_command(scene_ui::CampDecorationFrame{camp_frame},"camp_decoration");
   else scene_post_frame([this,command=scene_ui::CampDecorationFrame{camp_frame}]{queue_camp_decoration(command.frame);});}
  else queue_camp_decoration(camp_frame);
 }
 const RECT initial_source=source_rect?*source_rect:RECT{0,0,LONG(frame.width),LONG(frame.height)};
 const auto scale_of=[&](const RECT& d,const RECT& s,bool interface_space){
  const LONG sh=s.bottom-s.top,dh=d.bottom-d.top;if(sh<=0||dh<=0)return 0.f;
  const float composition=interface_space&&widescreen::interface_owned?widescreen::hud_blit.scale_y:1.f;
  return float(double(dh)/sh*composition*double(output_height_)/canvas_height());};
 CampBlitPiece pieces[kCampBlitPieces];const int piece_count=camp_inventory_blit(destination,initial_source,pieces);
 if(piece_count<0){
  composed_scale=scale_of(destination,initial_source,true);
  return queue_character_pane_text(frame,ui_epoch,frame_serial,destination,source_rect,true,false,fit);
 }
 struct BlitScope{bool previous=camp::remapping_blit;BlitScope(){camp::remapping_blit=true;}~BlitScope(){camp::remapping_blit=previous;}} remapping;
 for(int i=0;i<piece_count;++i){
  if(composed_scale<=0.f)composed_scale=scale_of(pieces[i].destination,pieces[i].source,false);
  const HRESULT result=queue_character_pane_text(frame,ui_epoch,frame_serial,pieces[i].destination,&pieces[i].source,false,false,fit);
  if(FAILED(result))return result;
 }
 return S_OK;
}

HRESULT Renderer::queue_character_pane_text(
 const CharacterPaneTextFrame& frame,std::uint64_t ui_epoch,
 std::uint64_t frame_serial,const RECT& destination,
 const RECT* source_rect,bool interface_space,bool observe_scale,const CharacterPaneTextFit* fit){
 if(scene_queue()){
  // The payload names each glyph atlas by key; the atlases stay in the
  // command's residency.
  auto command=std::make_unique<scene_ui::CharacterPaneText>();scene_ui::Residency residency;
  try{
   command->frame=frame;command->assets.resize(command->frame.quads.size());
   for(std::size_t index=0;index<command->frame.quads.size();++index){
    auto& quad=command->frame.quads[index];
    command->assets[index]=scene_ui::Residency::key_of(quad.asset.get());
    residency.add_glyph_asset(std::move(quad.asset));quad.asset=nullptr;
   }
  }catch(...){return E_OUTOFMEMORY;}
  command->ui_epoch=ui_epoch;command->frame_serial=frame_serial;command->destination=destination;
  command->has_source=source_rect!=nullptr;command->source=command->has_source?*source_rect:RECT{};
  command->interface_space=interface_space;command->observe_scale=observe_scale;
  command->has_fit=fit!=nullptr;if(fit)command->fit=*fit;
  scene_ui_pending_=true;
  // Core mode: each atlas the host does not hold yet goes ahead of the text.
  if(core_mode_){
   try{
    for(const auto& asset:residency.glyph_assets){
     auto& sent=core_glyphs_sent_[asset->identity];
     if(sent.first==asset->generation&&sent.second==asset->revision)continue;
     scene_ui::GlyphAtlas atlas;atlas.asset=*asset;
     core_post_command(atlas,"glyph_atlas");
     sent={asset->generation,asset->revision};
    }
   }catch(...){return E_OUTOFMEMORY;}
   core_post_command(*command,"character_pane_text");
   return S_OK;
  }
  scene_post_frame([this,command=std::move(command),residency=std::move(residency)]{
   auto& text=*command;
   for(std::size_t index=0;index<text.frame.quads.size()&&index<text.assets.size();++index)
    text.frame.quads[index].asset=residency.glyph_asset(text.assets[index]);
   report_scene_failure("queue_character_pane_text",queue_character_pane_text(text.frame,text.ui_epoch,text.frame_serial,text.destination,text.has_source?&text.source:nullptr,text.interface_space,text.observe_scale,text.has_fit?&text.fit:nullptr));});
  return S_OK;
 }
 PerfScope perf(&perf_blit_ticks_);if(perf_enabled())++perf_blit_calls_;
 const bool attribution_profile=profiler_.attribution_enabled();AttributionQpcScope queue_profile(attribution_profile,native_frame_stats_.ui_blit_queue_cpu_qpc,native_frame_stats_.ui_blit_queue_calls);
#ifdef EID3D11_TESTING
 const bool unscoped_test=!ui_epoch&&!frame_serial;
#else
 constexpr bool unscoped_test=false;
#endif
 // D3D12 owns its executor, not a D3D11 swap chain/context. Keep the
 // author's text validation and quad layout for either backend.
 const auto invalid=[&](const char* reason)->HRESULT {
  if(d3d12_){
   char detail[256]{};std::snprintf(detail,sizeof(detail),"operation=queue_character_pane_text reason=%s hr=0x%08lx epoch=%llu serial=%llu size=%ux%u",
    reason,static_cast<unsigned long>(E_INVALIDARG),static_cast<unsigned long long>(ui_epoch),static_cast<unsigned long long>(frame_serial),frame.width,frame.height);
   bridge_report_event("d3d12_failed",detail);
  }
  return E_INVALIDARG;
 };
 if(!d3d12_&&(!swap_||!context_))return invalid("device_context");
 if((!ui_epoch||!frame_serial)&&!unscoped_test)return invalid("frame_identity");
 if(!frame.width||!frame.height)return invalid("frame_dimensions");
 RECT sr=source_rect?*source_rect:RECT{0,0,(LONG)frame.width,(LONG)frame.height};sr.left=std::clamp<LONG>(sr.left,0,(LONG)frame.width);sr.top=std::clamp<LONG>(sr.top,0,(LONG)frame.height);sr.right=std::clamp<LONG>(sr.right,sr.left,(LONG)frame.width);sr.bottom=std::clamp<LONG>(sr.bottom,sr.top,(LONG)frame.height);const LONG dw=destination.right-destination.left,dh=destination.bottom-destination.top,sw=sr.right-sr.left,sh=sr.bottom-sr.top;if(dw<=0||dh<=0||sw<=0||sh<=0)return S_OK;
 const widescreen::HudComposition composition=interface_space&&widescreen::interface_owned?widescreen::hud_blit:widescreen::HudComposition{};
 const std::size_t checkpoint=pending_blits_.size();
 try{
  std::unordered_set<std::uint64_t> queued_assets;
  queued_assets.reserve(frame.quads.size());
  for(const auto& command:frame.quads){
   if(!command.asset||!command.asset->identity||!command.asset->generation||!command.asset->revision||!command.asset->width||!command.asset->height||command.asset->width>UINT_MAX/4u||command.asset->width>(std::numeric_limits<std::size_t>::max)()/command.asset->height||command.asset->coverage.size()!=static_cast<std::size_t>(command.asset->width)*command.asset->height||command.asset->coverage.size()>kMaximumCharacterPaneAtlasPixels){pending_blits_.resize(checkpoint);return invalid("glyph_asset");}
   if(command.raster_scale>0.f){
    // Output-size glyph: snap the stock pen to whole output pixels and place
    // the raster 1:1, so every glyph keeps its shape at any UI scale.
    if(!command.color)continue;
    const LONG source_width=command.source.right-command.source.left,source_height=command.source.bottom-command.source.top;
    if(source_width<=0||source_height<=0||command.source.left<0||command.source.top<0||command.source.right>(LONG)command.asset->width||command.source.bottom>(LONG)command.asset->height){pending_blits_.resize(checkpoint);return invalid("glyph_source");}
    const double kx=double(dw)/sw,ky=double(dh)/sh,sx=double(output_width_)/canvas_width(),sy=double(output_height_)/canvas_height();
    const auto map_x=[&](double p){return double(widescreen::compose_x(float(destination.left+(p-sr.left)*kx),composition))*sx;};
    const auto map_y=[&](double p){return double(widescreen::compose_y(float(destination.top+(p-sr.top)*ky),composition))*sy;};
    // A fitted card frame (CharacterPaneTextFit): surface positions scaled
    // about the block top, glyphs composed that much smaller; a line still
    // ending below the visible surface is left out whole.
    const double f=fit?double(fit->scale):1.0;
    const auto fx=[&](double p){return fit?double(fit->center_x)+(p-fit->center_x)*f:p;};
    const auto fy=[&](double p){return fit?double(fit->top)+(p-fit->top)*f:p;};
    if(fit&&fy(command.clip.bottom)>double(fit->bottom)+.5)continue;
    const double ex=kx*composition.scale*sx*f,ey=ky*composition.scale_y*sy*f,rs=command.raster_scale;
    // Per axis: a horizontal-only difference (uneven canvas rounding) keeps
    // glyph heights 1:1. The next raster uses the scale composed here, so a
    // pane blit that scales its surface converges to 1:1 too (#185).
    const bool exact_x=std::abs(ex-rs)<=rs*1e-4,exact_y=std::abs(ey-rs)<=rs*1e-4,exact=exact_x&&exact_y;
    const double gx=exact_x?1.0:ex/rs,gy=exact_y?1.0:ey/rs;
    if(!exact_y&&observe_scale){
     if(text_scale_mismatch_reports_<4){++text_scale_mismatch_reports_;char detail[128];
      std::snprintf(detail,sizeof(detail),"raster=%.4f composed_x=%.4f composed_y=%.4f",rs,ex,ey);bridge_append_probe_event("owned_text_scale",detail);}
     observed_text_scale_=float(ey);
    }
    const LONG ax=std::lround(map_x(fx(command.anchor.x)))+std::lround(command.shift.x*ex),ay=std::lround(map_y(fy(command.anchor.y)))+std::lround(command.shift.y*ey);
    RECT glyph{ax+std::lround(command.offset.x*gx),ay+std::lround(command.offset.y*gy),0,0};
    glyph.right=glyph.left+std::max<LONG>(1,std::lround(source_width*gx));glyph.bottom=glyph.top+std::max<LONG>(1,std::lround(source_height*gy));
    const double clip_left=std::max(fx(command.clip.left),double(sr.left)),clip_top=std::max(fy(command.clip.top),double(sr.top)),
     clip_right=std::min(fx(command.clip.right),double(sr.right)),clip_bottom=std::min(fy(command.clip.bottom),double(sr.bottom));
    if(clip_right<=clip_left||clip_bottom<=clip_top)continue;
    const RECT visible{std::max<LONG>({glyph.left,std::lround(map_x(clip_left)),0}),std::max<LONG>({glyph.top,std::lround(map_y(clip_top)),0}),
     std::min<LONG>({glyph.right,std::lround(map_x(clip_right)),(LONG)output_width_}),std::min<LONG>({glyph.bottom,std::lround(map_y(clip_bottom)),(LONG)output_height_})};
    if(visible.right<=visible.left||visible.bottom<=visible.top)continue;
    const double glyph_width=double(glyph.right-glyph.left),glyph_height=double(glyph.bottom-glyph.top);
    std::uint64_t cache_key=command.asset->identity^0x4350474c59504831ull;auto cached=overlay_textures_.find(cache_key);bool cache_hit=cached!=overlay_textures_.end()&&cached->second.resident()&&cached->second.generation==command.asset->generation&&cached->second.revision==command.asset->revision&&cached->second.width==command.asset->width&&cached->second.height==command.asset->height;if(!cache_hit&&!queued_assets.insert(cache_key).second)cache_hit=true;
    PendingBlit pending{};pending.source_id=command.asset->identity;pending.cache_key=cache_key;pending.generation=command.asset->generation;pending.revision=command.asset->revision;pending.ui_epoch=ui_epoch;pending.frame_serial=frame_serial;pending.cached=cache_hit;pending.batchable_text=command.atlas_page_index!=static_cast<std::size_t>(-1);pending.source=command.source;pending.destination=visible;pending.composition=composition;pending.width=command.asset->width;pending.height=command.asset->height;pending.pitch=command.asset->width*4u;pending.diffuse=command.color;
    pending.native_canvas=false;pending.output_space=true;pending.filtered_text=!exact;
    pending.u0=float((command.source.left+(visible.left-glyph.left)*source_width/glyph_width)/command.asset->width);pending.u1=float((command.source.left+(visible.right-glyph.left)*source_width/glyph_width)/command.asset->width);
    pending.v0=float((command.source.top+(visible.top-glyph.top)*source_height/glyph_height)/command.asset->height);pending.v1=float((command.source.top+(visible.bottom-glyph.top)*source_height/glyph_height)/command.asset->height);
    if(!cache_hit)pending.text_asset=command.asset;
    pending_blits_.push_back(std::move(pending));
    continue;
   }
   // Solid quads (underlines) of a fitted card frame: their rectangle scaled
   // as the glyph positions are.
   RECT placed=command.destination;
   if(fit){
    const double f=fit->scale;
    if(fit->top+(placed.bottom-fit->top)*f>double(fit->bottom)+.5)continue;
    placed={std::lround(fit->center_x+(placed.left-fit->center_x)*f),std::lround(fit->top+(placed.top-fit->top)*f),
     std::lround(fit->center_x+(placed.right-fit->center_x)*f),std::lround(fit->top+(placed.bottom-fit->top)*f)};
    if(placed.bottom<=placed.top)placed.bottom=placed.top+1;
   }
   RECT pane_clip{std::max(placed.left,sr.left),std::max(placed.top,sr.top),std::min(placed.right,sr.right),std::min(placed.bottom,sr.bottom)};if(pane_clip.right<=pane_clip.left||pane_clip.bottom<=pane_clip.top||!command.color)continue;
   const LONG command_width=placed.right-placed.left,command_height=placed.bottom-placed.top,asset_source_width=command.source.right-command.source.left,asset_source_height=command.source.bottom-command.source.top;if(command_width<=0||command_height<=0||asset_source_width<=0||asset_source_height<=0||command.source.left<0||command.source.top<0||command.source.right>(LONG)command.asset->width||command.source.bottom>(LONG)command.asset->height){pending_blits_.resize(checkpoint);return invalid("solid_quad_source");}
   RECT mapped{destination.left+MulDiv(pane_clip.left-sr.left,dw,sw),destination.top+MulDiv(pane_clip.top-sr.top,dh,sh),destination.left+MulDiv(pane_clip.right-sr.left,dw,sw),destination.top+MulDiv(pane_clip.bottom-sr.top,dh,sh)};RECT clipped=clip_ui_destination(mapped,composition,canvas_width(),canvas_height());if(clipped.right<=clipped.left||clipped.bottom<=clipped.top)continue;
   const float source_u0=(command.source.left+(pane_clip.left-placed.left)*(float)asset_source_width/command_width)/command.asset->width,source_v0=(command.source.top+(pane_clip.top-placed.top)*(float)asset_source_height/command_height)/command.asset->height,source_u1=(command.source.left+(pane_clip.right-placed.left)*(float)asset_source_width/command_width)/command.asset->width,source_v1=(command.source.top+(pane_clip.bottom-placed.top)*(float)asset_source_height/command_height)/command.asset->height;
   const float mapped_width=(float)(mapped.right-mapped.left),mapped_height=(float)(mapped.bottom-mapped.top);if(mapped_width<=0||mapped_height<=0)continue;
   std::uint64_t cache_key=command.asset->identity^0x4350474c59504831ull;auto cached=overlay_textures_.find(cache_key);bool cache_hit=cached!=overlay_textures_.end()&&cached->second.resident()&&cached->second.generation==command.asset->generation&&cached->second.revision==command.asset->revision&&cached->second.width==command.asset->width&&cached->second.height==command.asset->height;if(!cache_hit&&!queued_assets.insert(cache_key).second)cache_hit=true;
   PendingBlit pending{};pending.source_id=command.asset->identity;pending.cache_key=cache_key;pending.generation=command.asset->generation;pending.revision=command.asset->revision;pending.ui_epoch=ui_epoch;pending.frame_serial=frame_serial;pending.cached=cache_hit;pending.batchable_text=command.atlas_page_index!=static_cast<std::size_t>(-1);pending.source=command.source;pending.destination=clipped;pending.composition=composition;pending.width=command.asset->width;pending.height=command.asset->height;pending.pitch=command.asset->width*4u;pending.diffuse=command.color;pending.u0=source_u0+(clipped.left-mapped.left)*(source_u1-source_u0)/mapped_width;pending.v0=source_v0+(clipped.top-mapped.top)*(source_v1-source_v0)/mapped_height;pending.u1=source_u1-(mapped.right-clipped.right)*(source_u1-source_u0)/mapped_width;pending.v1=source_v1-(mapped.bottom-clipped.bottom)*(source_v1-source_v0)/mapped_height;
   if(!cache_hit)pending.text_asset=command.asset;
   pending_blits_.push_back(std::move(pending));
  }
 }catch(const std::bad_alloc&){pending_blits_.resize(checkpoint);return E_OUTOFMEMORY;}
 combat_ui_place_spell_list(ui_epoch,frame_serial,destination,sr,composition,canvas_width(),canvas_height());
 return S_OK;
}

HRESULT Renderer::capture(SurfaceImage& destination,const RECT& destination_rect,const RECT& native_source_rect){
 if(core_mode_)return core_capture(destination,destination_rect,native_source_rect);
 if(scene_queue()){
  // The destination stays game-thread state: its GPU home is created and its
  // generation advanced here; the scene thread composes the world, renders
  // queued UI and copies the back buffer region into that texture in order.
  const RECT source_rect{MulDiv(native_source_rect.left,output_width_,canvas_width()),MulDiv(native_source_rect.top,output_height_,canvas_height()),MulDiv(native_source_rect.right,output_width_,canvas_width()),MulDiv(native_source_rect.bottom,output_height_,canvas_height())};
  if(destination.cpu_released)return E_UNEXPECTED;
  if(!swap_||!device_||!context_)return E_FAIL;
  const LONG source_width=source_rect.right-source_rect.left,source_height=source_rect.bottom-source_rect.top,destination_width=destination_rect.right-destination_rect.left,destination_height=destination_rect.bottom-destination_rect.top;if(source_width<=0||source_height<=0||destination_width<=0||destination_height<=0)return E_INVALIDARG;
  RECT clipped_destination{std::clamp<LONG>(destination_rect.left,0,(LONG)destination.width),std::clamp<LONG>(destination_rect.top,0,(LONG)destination.height),std::clamp<LONG>(destination_rect.right,0,(LONG)destination.width),std::clamp<LONG>(destination_rect.bottom,0,(LONG)destination.height)};if(clipped_destination.right<=clipped_destination.left||clipped_destination.bottom<=clipped_destination.top)return E_INVALIDARG;
  const bool stretched=source_width!=destination_width||source_height!=destination_height;
  if(!stretched){  // stretched: the CPU readback path below (SCENE_ENTRY)
   const POINT source_origin=capture_stretch_source_point(source_rect,destination_rect,clipped_destination.left,clipped_destination.top);
   const LONG copy_width=std::min<LONG>({clipped_destination.right-clipped_destination.left,(LONG)output_width_-source_origin.x}),copy_height=std::min<LONG>({clipped_destination.bottom-clipped_destination.top,(LONG)output_height_-source_origin.y});
   if(source_origin.x<0||source_origin.y<0||copy_width<=0||copy_height<=0)return E_INVALIDARG;
   // A published GPU home is never written in place (copy-on-write).
   if(destination.texture_shared){const HRESULT detached=scene_detach_surface_texture(destination,"capture");if(FAILED(detached))return detached;}
   D3D11_TEXTURE2D_DESC td{};if(destination.texture)destination.texture->GetDesc(&td);
   if(!destination.texture||td.Width!=destination.width||td.Height!=destination.height||td.Format!=DXGI_FORMAT_B8G8R8A8_UNORM){
    destination.texture.Reset();destination.srv.Reset();update_surface_memory_accounting(destination);
    td={};td.Width=destination.width;td.Height=destination.height;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    if(FAILED(device_->CreateTexture2D(&td,nullptr,&destination.texture)))return E_FAIL;
    if(FAILED(device_->CreateShaderResourceView(destination.texture.Get(),nullptr,&destination.srv))){update_surface_memory_accounting(destination);return E_FAIL;}
    update_surface_memory_accounting(destination);
   }
   scene_ui::Capture command;command.target=scene_ui::surface_handle(destination);
   command.box_left=(UINT)source_origin.x;command.box_top=(UINT)source_origin.y;
   command.box_right=(UINT)(source_origin.x+copy_width);command.box_bottom=(UINT)(source_origin.y+copy_height);
   command.x=(UINT)clipped_destination.left;command.y=(UINT)clipped_destination.top;
   scene_ui::Residency residency;
   try{residency.add({command.target,nullptr,destination.texture,nullptr});}catch(...){return E_OUTOFMEMORY;}
   scene_ui_pending_=false;scene_native_pending_=false;
   scene_post_frame([this,command,residency=std::move(residency)]{
    const auto* home=residency.find(command.target);
    ComPtr<ID3D11Texture2D> back;HRESULT hr=home?capture_source(back):E_UNEXPECTED;
    if(SUCCEEDED(hr)){ID3D11ShaderResourceView* null_view=nullptr;context_->PSSetShaderResources(0,1,&null_view);
     const D3D11_BOX box{command.box_left,command.box_top,0,command.box_right,command.box_bottom,1};
     context_->CopySubresourceRegion(home->texture.Get(),0,command.x,command.y,0,back.Get(),0,&box);}
    report_scene_failure("capture",hr);});
   advance_surface_generation(destination,nullptr);destination.uploaded_generation=destination.generation;destination.uploaded_shape=surface_upload_shape(destination);clear_surface_dirty(destination);destination.gpu_captured=true;update_surface_memory_accounting(destination);
   return S_OK;
  }
 }
 SCENE_ENTRY("capture");
 const RECT source_rect{MulDiv(native_source_rect.left,output_width_,canvas_width()),MulDiv(native_source_rect.top,output_height_,canvas_height()),MulDiv(native_source_rect.right,output_width_,canvas_width()),MulDiv(native_source_rect.bottom,output_height_,canvas_height())};
 if(destination.cpu_released)return E_UNEXPECTED;
 if(!swap_||!device_||!context_)return E_FAIL;
 ComPtr<ID3D11Texture2D> back;HRESULT hr=capture_source(back);if(FAILED(hr))return hr;
 const LONG source_width=source_rect.right-source_rect.left,source_height=source_rect.bottom-source_rect.top,destination_width=destination_rect.right-destination_rect.left,destination_height=destination_rect.bottom-destination_rect.top;if(source_width<=0||source_height<=0||destination_width<=0||destination_height<=0)return E_INVALIDARG;
 RECT clipped_destination{std::clamp<LONG>(destination_rect.left,0,(LONG)destination.width),std::clamp<LONG>(destination_rect.top,0,(LONG)destination.height),std::clamp<LONG>(destination_rect.right,0,(LONG)destination.width),std::clamp<LONG>(destination_rect.bottom,0,(LONG)destination.height)};if(clipped_destination.right<=clipped_destination.left||clipped_destination.bottom<=clipped_destination.top)return E_INVALIDARG;
 ID3D11ShaderResourceView* null_view=nullptr;context_->PSSetShaderResources(0,1,&null_view);
 const bool stretched=source_width!=destination_width||source_height!=destination_height;
 if(!stretched){const POINT source_origin=capture_stretch_source_point(source_rect,destination_rect,clipped_destination.left,clipped_destination.top);const LONG copy_width=std::min<LONG>({clipped_destination.right-clipped_destination.left,(LONG)output_width_-source_origin.x}),copy_height=std::min<LONG>({clipped_destination.bottom-clipped_destination.top,(LONG)output_height_-source_origin.y});if(source_origin.x<0||source_origin.y<0||copy_width<=0||copy_height<=0)return E_INVALIDARG;if(destination.texture_shared&&FAILED(hr=detach_shared_surface_texture(destination,"capture")))return hr;D3D11_TEXTURE2D_DESC td{};if(destination.texture)destination.texture->GetDesc(&td);if(!destination.texture||td.Width!=destination.width||td.Height!=destination.height||td.Format!=DXGI_FORMAT_B8G8R8A8_UNORM){destination.texture.Reset();destination.srv.Reset();update_surface_memory_accounting(destination);td={};td.Width=destination.width;td.Height=destination.height;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;if(FAILED(device_->CreateTexture2D(&td,nullptr,&destination.texture)))return E_FAIL;if(FAILED(device_->CreateShaderResourceView(destination.texture.Get(),nullptr,&destination.srv))){update_surface_memory_accounting(destination);return E_FAIL;}update_surface_memory_accounting(destination);}D3D11_BOX box{(UINT)source_origin.x,(UINT)source_origin.y,0,(UINT)(source_origin.x+copy_width),(UINT)(source_origin.y+copy_height),1};context_->CopySubresourceRegion(destination.texture.Get(),0,(UINT)clipped_destination.left,(UINT)clipped_destination.top,0,back.Get(),0,&box);advance_surface_generation(destination,nullptr);destination.uploaded_generation=destination.generation;destination.uploaded_shape=surface_upload_shape(destination);clear_surface_dirty(destination);destination.gpu_captured=true;update_surface_memory_accounting(destination);return S_OK;}
 if(!(destination.format.dwFlags&DDPF_RGB)||destination.format.dwFlags&DDPF_FOURCC)return E_NOTIMPL;if(destination.gpu_captured&&FAILED(hr=readback(destination)))return hr;
 RECT clipped_source{std::clamp<LONG>(source_rect.left,0,(LONG)output_width_),std::clamp<LONG>(source_rect.top,0,(LONG)output_height_),std::clamp<LONG>(source_rect.right,0,(LONG)output_width_),std::clamp<LONG>(source_rect.bottom,0,(LONG)output_height_)};if(clipped_source.right<=clipped_source.left||clipped_source.bottom<=clipped_source.top)return E_INVALIDARG;
 D3D11_TEXTURE2D_DESC back_description{};back->GetDesc(&back_description);if(back_description.Format!=DXGI_FORMAT_B8G8R8A8_UNORM||back_description.SampleDesc.Count!=1)return E_NOTIMPL;D3D11_TEXTURE2D_DESC staging_description=back_description;staging_description.Width=clipped_source.right-clipped_source.left;staging_description.Height=clipped_source.bottom-clipped_source.top;staging_description.MipLevels=staging_description.ArraySize=1;staging_description.Usage=D3D11_USAGE_STAGING;staging_description.BindFlags=0;staging_description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;staging_description.MiscFlags=0;ComPtr<ID3D11Texture2D> staging;if(FAILED(hr=device_->CreateTexture2D(&staging_description,nullptr,&staging)))return hr;D3D11_BOX source_box{(UINT)clipped_source.left,(UINT)clipped_source.top,0,(UINT)clipped_source.right,(UINT)clipped_source.bottom,1};context_->CopySubresourceRegion(staging.Get(),0,0,0,0,back.Get(),0,&source_box);
 D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(hr=context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))return hr;const DWORD bytes=std::max<DWORD>(1,destination.format.dwRGBBitCount/8);const size_t required=(size_t)destination.pitch*destination.height;std::uint8_t* pixels=nullptr;try{if(destination.pixels.size()<required)destination.pixels.resize(required);pixels=destination.pixels.data();}catch(const std::bad_alloc&){context_->Unmap(staging.Get(),0);return E_OUTOFMEMORY;}for(LONG y=clipped_destination.top;y<clipped_destination.bottom;++y)for(LONG x=clipped_destination.left;x<clipped_destination.right;++x){const POINT source=capture_stretch_source_point(source_rect,destination_rect,x,y);if(source.x<clipped_source.left||source.x>=clipped_source.right||source.y<clipped_source.top||source.y>=clipped_source.bottom)continue;const auto* row=reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(mapped.pData)+(size_t)(source.y-clipped_source.top)*mapped.RowPitch);const std::uint32_t bgra=row[source.x-clipped_source.left];const BYTE b=(BYTE)bgra,g=(BYTE)(bgra>>8),r=(BYTE)(bgra>>16),a=(BYTE)(bgra>>24);const DWORD raw=pack_channel(r,destination.format.dwRBitMask)|pack_channel(g,destination.format.dwGBitMask)|pack_channel(b,destination.format.dwBBitMask)|pack_channel(a,destination.format.dwRGBAlphaBitMask);std::memcpy(pixels+(size_t)y*destination.pitch+(size_t)x*bytes,&raw,std::min<DWORD>(bytes,4));}context_->Unmap(staging.Get(),0);destination.gpu_captured=false;mark_surface_dirty(destination,clipped_destination);advance_surface_generation(destination,&clipped_destination);update_surface_memory_accounting(destination);return S_OK;
}

HRESULT Renderer::readback(SurfaceImage& image){if(core_mode_)return core_readback_surface(image);SCENE_ENTRY("readback");
 if(!image.gpu_captured||!image.texture)return S_OK;if(!device_||!context_)return E_FAIL;
 D3D11_TEXTURE2D_DESC sourceDesc{};image.texture->GetDesc(&sourceDesc);if(sourceDesc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM)return E_NOTIMPL;
 D3D11_TEXTURE2D_DESC stagingDesc=sourceDesc;stagingDesc.Usage=D3D11_USAGE_STAGING;stagingDesc.BindFlags=0;stagingDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;stagingDesc.MiscFlags=0;
 ComPtr<ID3D11Texture2D> staging;HRESULT hr=device_->CreateTexture2D(&stagingDesc,nullptr,&staging);if(FAILED(hr))return hr;context_->CopyResource(staging.Get(),image.texture.Get());
 D3D11_MAPPED_SUBRESOURCE mapped{};hr=context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped);if(FAILED(hr))return hr;
 const UINT bytes=std::max<UINT>(1,image.format.dwRGBBitCount/8);const size_t required=(size_t)image.pitch*image.height;std::uint8_t* pixels=nullptr;try{if(image.pixels.size()<required)image.pixels.resize(required);pixels=image.pixels.data();}catch(const std::bad_alloc&){context_->Unmap(staging.Get(),0);return E_OUTOFMEMORY;}
 const UINT copyWidth=std::min(image.width,sourceDesc.Width),copyHeight=std::min(image.height,sourceDesc.Height);const PackedChannelTables tables(image.format);for(UINT y=0;y<copyHeight;++y){const auto* source=reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(mapped.pData)+(size_t)y*mapped.RowPitch);pack_bgra_row(pixels+(size_t)y*image.pitch,source,copyWidth,std::min<UINT>(bytes,4),tables);}
 context_->Unmap(staging.Get(),0);clear_surface_dirty(image);update_surface_memory_accounting(image);return S_OK;
}

void Renderer::retire_surface(std::uint64_t surface_id) noexcept {
 if(!surface_id)return;
 if(scene_queue()){
  for(auto entry=overlay_mirror_.begin();entry!=overlay_mirror_.end();){
   if(entry->second.surface_id==surface_id)entry=overlay_mirror_.erase(entry);else ++entry;}
  if(core_mode_){core_post_command(scene_ui::RetireSurface{surface_id},"retire_surface");return;}
  scene_post([this,command=scene_ui::RetireSurface{surface_id}]{retire_surface(command.surface_id);});
  return;
 }
 scene_mip_textures_.erase(surface_id);
 // D3D12: a material profile keyed by this surface (note_material_texture).
 if(d3d12_&&!material_profiles_.empty()&&material_profiles_.erase((2ull<<62)|surface_id))++material_profiles_revision_;
 // A completed Blt owns its queued pixels even if its source is released
 // before the UI flush. Keep cache hits alive only through those queued draws.
 for(auto& pending:pending_blits_){
  if(!pending.source_is_surface||pending.source_id!=surface_id)continue;
  pending.source_destroyed=true;
  if((pending.cached||pending.partial)&&!pending.gpu){
   const auto cached=overlay_textures_.find(pending.cache_key);
   if(cached!=overlay_textures_.end()){
    pending.texture=cached->second.texture;pending.srv=cached->second.srv;pending.d3d12=cached->second.d3d12;
    pending.gpu=pending.cached;
   }
  }
 }
 for(auto entry=overlay_textures_.begin();entry!=overlay_textures_.end();){
  if(entry->second.surface_id==surface_id)entry=overlay_textures_.erase(entry);
  else ++entry;
 }
 // Its owned texture copies and terrain array slices. A surface id is never
 // issued again, so nothing can look them up any more: a map revisit's new
 // surfaces took new entries while these stayed resident. Queued and recorded
 // draws keep their own references. A detached static drawn after this
 // re-enters the cache from its retained copy (detach precedes the removal
 // that destroys the surface, so its retainer found this entry first).
 {std::lock_guard<std::mutex> guard(native_owned_textures_lock_);
 for(auto entry=native_owned_textures_.begin();entry!=native_owned_textures_.end();){
  if(entry->second.surface_id!=surface_id){++entry;continue;}
  drop_native_texture_array_slots(entry->second.backing,true);
  entry=native_owned_textures_.erase(entry);
 }}
 drop_native_texture_array_slots(surface_id,false);
}

void Renderer::drop_native_texture_array_slots(std::uint64_t id,bool immutable) noexcept{
 for(auto group=native_texture_arrays_.begin();group!=native_texture_arrays_.end();){
  if(group->first.immutable_values!=immutable){++group;continue;}
  auto& pages=group->second;
  for(auto page=pages.begin();page!=pages.end();){
   if(page->slots.erase(id)&&page->used)--page->used;
   if(!page->used&&page->next)page=pages.erase(page);else ++page;
  }
  if(pages.empty())group=native_texture_arrays_.erase(group);else ++group;
 }
}

// Two frames into a new world (trim_native_mesh_cache), like the geometry
// sweep: owned values (archive members, generated MMPs, detached copies) not
// looked up since the world began and not drawn through a material group
// since then. Warm records draw through their group without a lookup, so a
// group selected in this world keeps its texture (D3D11: the group holds it;
// D3D12: groups span textures, each record holds its own, texture12). Live client-surface entries
// go with their surface (retire_surface), never by age: a detach needs them.
// A retired entry is re-created from its view's CPU source on its next use.
void Renderer::retire_world_textures(std::uint64_t since) noexcept try{
 std::unordered_set<ID3D11ShaderResourceView*> drawn;
 std::unordered_set<const D3D12Texture*> drawn12;
 for(const auto& bucket:native_state_buckets_){
  if(!bucket.occupied||bucket.selection_epoch<since)continue;
  if(bucket.texture_srv)drawn.insert(bucket.texture_srv.Get());
  if(bucket.texture_d3d12)drawn12.insert(bucket.texture_d3d12.get());
 }
 std::uint64_t retired=0;
 std::lock_guard<std::mutex> guard(native_owned_textures_lock_);
 for(auto entry=native_owned_textures_.begin();entry!=native_owned_textures_.end();){
  const auto& value=entry->second;
  if(value.camp_surface_alias||(value.surface_id&&!value.detached_copy)||value.last_used>=since||
     drawn.count(value.srv.Get())||drawn12.count(value.d3d12.get())){++entry;continue;}
  drop_native_texture_array_slots(value.backing,true);
  neighbour_texture_resident(entry->first,false);
  entry=native_owned_textures_.erase(entry);++retired;
 }
 if(retired){
  char detail[96];std::snprintf(detail,sizeof(detail),"retired=%llu resident=%llu",
   static_cast<unsigned long long>(retired),static_cast<unsigned long long>(native_owned_textures_.size()));
  bridge_append_probe_event("owned_texture_world_sweep",detail);
 }
}catch(...){}

void Renderer::retire_camp_texture(std::uint64_t surface_id,std::uint64_t generation) noexcept {
 if(scene_queue()&&core_mode_){core_post_command(scene_ui::RetireCampTexture{surface_id,generation},"retire_camp_texture");return;}
 if(scene_queue()){scene_post([this,command=scene_ui::RetireCampTexture{surface_id,generation}]{retire_camp_texture(command.surface_id,command.generation);});return;}
 {std::lock_guard<std::mutex> guard(native_owned_textures_lock_);
 for(auto entry=native_owned_textures_.begin();entry!=native_owned_textures_.end();){
  const auto& value=entry->second;
  if(value.camp_surface_alias&&value.surface_id==surface_id&&value.generation==generation)
   entry=native_owned_textures_.erase(entry);
  else ++entry;
 }}
 // Queued buckets retain their COM references until normal submission/discard.
 // Unreferenced buckets can retire now, without flushing another pass.
 trim_native_material_groups();
}

void Renderer::invalidate_camp_popup() noexcept{
 if(scene_queue()){
 #if !defined(EID3D11_EMBEDDED) && !defined(EID3D11_SHADER_TOOL)
 if(core_mode_){core_post_command(scene_core::InvalidateCampPopup{},"invalidate_camp_popup");return;}
#endif
  scene_post([this]{camp_popup_ready_=false;});return;
 }
 camp_popup_ready_=false;
}

HRESULT Renderer::flush_ui(){
 if(scene_queue()){scene_ui_pending_=false;if(core_mode_){core_post_command(scene_ui::FlushUi{},"flush_ui");return S_OK;}scene_post_frame([this,command=scene_ui::FlushUi{}]{(void)command;report_scene_failure("flush_ui",flush_ui());});return S_OK;}
 SceneRendererTimingScope scene_timing(SceneRendererTimingKind::ui_flush);
 return render_pending_blits();
}

void Renderer::write_ui_vertices(void* destination,const std::vector<UINT>& vertex_starts) const noexcept{
 auto* vertices=static_cast<Vertex*>(destination);
 for(std::size_t index=0;index<pending_blits_.size();++index){
  const auto& pending=pending_blits_[index];const auto& r=pending.destination;auto* quad=vertices+vertex_starts[index];
  // An anchored HUD widget's surfaces compose at the UI scale around its pivot.
  const auto& composition=pending.composition;
  const float sx=pending.native_canvas?float(output_width_)/canvas_width():1.f,sy=pending.native_canvas?float(output_height_)/canvas_height():1.f;
  // Output-space quads (output-size glyphs) are already placed in pixels.
  const float x0=pending.output_space?float(r.left):widescreen::compose_x(float(r.left),composition)*sx,y0=pending.output_space?float(r.top):widescreen::compose_y(float(r.top),composition)*sy;
  const float x1=pending.output_space?float(r.right):widescreen::compose_x(float(r.right),composition)*sx,y1=pending.output_space?float(r.bottom):widescreen::compose_y(float(r.bottom),composition)*sy;
  const Vertex top_left{x0,y0,0,1,pending.diffuse,0xffffffffu,pending.u0,pending.quarter_turn?pending.v1:pending.v0};
  const Vertex top_right{x1,y0,0,1,pending.diffuse,0xffffffffu,pending.quarter_turn?pending.u0:pending.u1,pending.v0};
  const Vertex bottom_left{x0,y1,0,1,pending.diffuse,0xffffffffu,pending.quarter_turn?pending.u1:pending.u0,pending.v1};
  const Vertex bottom_right{x1,y1,0,1,pending.diffuse,0xffffffffu,pending.u1,pending.quarter_turn?pending.v0:pending.v1};
  quad[0]=top_left;quad[1]=top_right;quad[2]=bottom_left;
  if(pending.batchable_text){quad[3]=bottom_left;quad[4]=top_right;quad[5]=bottom_right;}else quad[3]=bottom_right;
 }
}

HRESULT Renderer::render_pending_blits(){
#ifdef EID3D11_TESTING
 test_last_character_pane_draw_calls_=0;
#endif
 pending_blits_.erase(std::remove_if(pending_blits_.begin(),pending_blits_.end(),[](const PendingBlit& pending){return pending.ui_epoch&&!bridge_character_pane_text_epoch_current(pending.ui_epoch,pending.frame_serial);}),pending_blits_.end());
 if(scene_mode()&&!pending_blits_.empty()){
  // Scene thread: the game thread chose cached/partial quads against its
  // mirror of overlay_textures_. A quad whose texture is missing or at
  // another generation (an earlier drop or failure) is dropped for this
  // frame and the mirror restarts, instead of drawing an empty texture.
  try{
   std::unordered_map<std::uint64_t,std::uint64_t> created;std::vector<std::uint8_t> drop(pending_blits_.size(),0);bool any=false;
   for(std::size_t index=0;index<pending_blits_.size();++index){
    const auto& pending=pending_blits_[index];
    if(pending.gpu||pending.source_destroyed||pending.presentation_fill)continue;
    if(!pending.cached&&!pending.partial){created[pending.cache_key]=pending.generation;continue;}
    const std::uint64_t expected=pending.partial?pending.base_generation:pending.generation;
    bool usable=false;
    const auto made=created.find(pending.cache_key);
    if(made!=created.end())usable=made->second==expected;
    else{const auto found=overlay_textures_.find(pending.cache_key);
     usable=found!=overlay_textures_.end()&&found->second.texture&&found->second.srv&&found->second.generation==expected;}
    if(!usable){drop[index]=1;any=true;}
    else if(pending.partial)created[pending.cache_key]=pending.generation;
   }
   if(any){
    std::size_t write=0;
    for(std::size_t index=0;index<pending_blits_.size();++index)if(!drop[index]){if(write!=index)pending_blits_[write]=std::move(pending_blits_[index]);++write;}
    pending_blits_.resize(write);
    overlay_mirror_reset_.store(true,std::memory_order_release);
   }
  }catch(...){}
  if(pending_blits_.empty())return S_OK;
 }
 // A CPU quad that must upload but carries no content (no converted pixels,
 // no game-format rows, or too few of them) cannot render. Omit it, with its
 // presentation fill, and say so, instead of failing the whole flush: the
 // game retries a failed flush, and a retired source's quad created and
 // released a transient texture on every retry (hang of 1a1e929e).
 {
  const auto no_content=[](const PendingBlit& p){
   if(p.gpu||p.presentation_fill||p.cached||p.text_asset)return false;
   const RECT u=p.partial?p.update:RECT{0,0,(LONG)p.width,(LONG)p.height};
   if(u.right<=u.left||u.bottom<=u.top)return false; // nothing to upload
   return p.pixels.empty();
  };
  std::size_t write=0,omitted=0;
  std::vector<std::uint64_t> omitted_keys; // a later partial refresh of an omitted quad has no base either
  const auto omitted_base=[&](const PendingBlit& p){
   return p.partial&&!p.gpu&&!p.source_destroyed&&std::find(omitted_keys.begin(),omitted_keys.end(),p.cache_key)!=omitted_keys.end();
  };
  for(std::size_t index=0;index<pending_blits_.size();++index){
   const bool fill_of_omitted=pending_blits_[index].presentation_fill&&index+1<pending_blits_.size()&&
    (no_content(pending_blits_[index+1])||omitted_base(pending_blits_[index+1]));
   if(fill_of_omitted||no_content(pending_blits_[index])||omitted_base(pending_blits_[index])){
    if(!pending_blits_[index].presentation_fill){
     ++omitted;
     try{omitted_keys.push_back(pending_blits_[index].cache_key);}catch(...){}
     static std::uint64_t reported=0;const auto count=++reported;
     if(count<=8||!(count&(count-1))){
      const auto& p=pending_blits_[index];char detail[224];
      std::snprintf(detail,sizeof(detail),"source=%llu %ux%u partial=%d retired=%d count=%llu (the quad is not drawn this frame)",
       static_cast<unsigned long long>(p.source_id),p.width,p.height,p.partial?1:0,p.source_destroyed?1:0,
       static_cast<unsigned long long>(count));
      bridge_report_event("ui_quad_omitted",detail);
     }
    }
    continue;
   }
   if(write!=index)pending_blits_[write]=std::move(pending_blits_[index]);
   ++write;
  }
  if(write!=pending_blits_.size()){
   pending_blits_.resize(write);
   if(omitted)overlay_mirror_reset_.store(true,std::memory_order_release); // its cache entry was never written
  }
 }
 if(pending_blits_.empty())return S_OK;
 if(d3d12_){ // the composite marks the interface boundary, as on D3D11
  (void)composite_world();
  // Named, not silent (a flicker report): quads recorded while the world
  // target is still bound land in the world image, not the output.
  if(world_d3d12_bound_)d3d12_->omit("interface quads recorded into the world target (no composite before them)",false);
  const HRESULT hr=render_pending_blits_d3d12();
  if(FAILED(hr))d3d12_->omit("interface quads (the UI batch failed before drawing)",false);
  return hr;
 }
 const HRESULT composite_hr=composite_world();if(FAILED(composite_hr))return composite_hr;
 transition_profile_pass("ui_composition");
 if(!device_||!context_)return E_FAIL;

 std::vector<ID3D11ShaderResourceView*> views;
 std::vector<std::uint32_t> text_atlas_pixels;
 try{views.reserve(pending_blits_.size());}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 for(auto& pending:pending_blits_){
  // A presentation fill precedes its picture. Upload only the picture;
  // its pending quad retains the resource even after source retirement.
  if(pending.presentation_fill){views.push_back(nullptr);continue;}
  ID3D11ShaderResourceView* view=pending.srv.Get();
  if(!pending.gpu){
   OverlayTexture transient;
   const auto surface_id=pending.source_is_surface?pending.source_id:0;
   if(pending.source_destroyed&&pending.partial){
    transient.texture=pending.texture;transient.srv=pending.srv;
    transient.surface_id=surface_id;transient.generation=pending.base_generation;
    transient.width=pending.width;transient.height=pending.height;
   }
   auto& cached=pending.source_destroyed?transient:overlay_textures_[pending.cache_key];
   // A partial refresh requires the texture measured at queue time.
   if(pending.partial&&(!cached.texture||!cached.srv||cached.width!=pending.width||cached.height!=pending.height||cached.surface_id!=surface_id||cached.generation<pending.base_generation||cached.generation>pending.generation))return E_UNEXPECTED;
   if(!cached.texture||!cached.srv||cached.surface_id!=surface_id||cached.width!=pending.width||cached.height!=pending.height){
    cached={};cached.surface_id=surface_id;cached.width=pending.width;cached.height=pending.height;
    D3D11_TEXTURE2D_DESC td{};td.Width=pending.width;td.Height=pending.height;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    HRESULT hr=device_->CreateTexture2D(&td,nullptr,&cached.texture);if(FAILED(hr))return hr;
#ifdef EID3D11_TESTING
    if(pending.text_asset&&test_fail_character_pane_text_srv_creation_){test_fail_character_pane_text_srv_creation_=false;cached={};return E_OUTOFMEMORY;}
#endif
    hr=device_->CreateShaderResourceView(cached.texture.Get(),nullptr,&cached.srv);if(FAILED(hr)){cached={};return hr;}
   }
   if(!pending.cached){
    const std::uint32_t* upload=pending.pixels.data();
    if(pending.text_asset){
     if(pending.text_asset->identity!=pending.source_id||pending.text_asset->generation!=pending.generation||pending.text_asset->revision!=pending.revision||pending.text_asset->width!=pending.width||pending.text_asset->height!=pending.height||pending.text_asset->coverage.size()!=static_cast<std::size_t>(pending.width)*pending.height||pending.text_asset->coverage.size()>kMaximumCharacterPaneAtlasPixels)return E_INVALIDARG;
     try{text_atlas_pixels.resize(pending.text_asset->coverage.size());}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
     for(std::size_t index=0;index<pending.text_asset->coverage.size();++index)text_atlas_pixels[index]=(static_cast<std::uint32_t>(pending.text_asset->coverage[index])<<24u)|0x00ffffffu;
     upload=text_atlas_pixels.data();
    }
    if(pending.partial){
     const RECT& u=pending.update;
     if(u.right>u.left&&u.bottom>u.top){if(!upload)return E_INVALIDARG;const D3D11_BOX box{(UINT)u.left,(UINT)u.top,0,(UINT)u.right,(UINT)u.bottom,1};context_->UpdateSubresource(cached.texture.Get(),0,&box,upload,pending.pitch,0);}
    }else{
     if(!upload)return E_INVALIDARG;
     context_->UpdateSubresource(cached.texture.Get(),0,nullptr,upload,pending.pitch,0);
    }
    cached.surface_id=pending.source_is_surface?pending.source_id:0;cached.generation=pending.generation;cached.revision=pending.revision;cached.source=pending.source;cached.keyed=pending.keyed;cached.color_key=pending.color_key;
   }
   view=cached.srv.Get();
   if(pending.source_destroyed){pending.texture=cached.texture;pending.srv=cached.srv;pending.gpu=true;}
  }
  if(!view)return E_FAIL;
  views.push_back(view);
 }
 // present_image inserts the two quads together. Resolve their shared
 // uploaded view after all uploads, including destroyed-source transients.
 for(std::size_t index=0;index<pending_blits_.size();++index){
  if(!pending_blits_[index].presentation_fill)continue;
  if(index+1>=views.size()||pending_blits_[index+1].presentation_fill||!views[index+1])return E_UNEXPECTED;
  views[index]=views[index+1];
 }

 std::vector<UINT> vertex_starts;
 std::vector<UINT> vertex_counts;
 std::uint64_t required_vertices=0;
 try{vertex_starts.reserve(pending_blits_.size());vertex_counts.reserve(pending_blits_.size());}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 for(const auto& pending:pending_blits_){const UINT count=pending.batchable_text?6u:4u;if(required_vertices>(std::numeric_limits<UINT>::max)()-count)return E_OUTOFMEMORY;vertex_starts.push_back(static_cast<UINT>(required_vertices));vertex_counts.push_back(count);required_vertices+=count;}
 if(required_vertices>(std::numeric_limits<UINT>::max)()/sizeof(Vertex))return E_OUTOFMEMORY;
 const UINT vertex_count=static_cast<UINT>(required_vertices);
 const UINT vertex_bytes=vertex_count*sizeof(Vertex);
 if(!dynamic_vertices_||vertex_bytes>dynamic_vertex_capacity_){
  dynamic_vertex_capacity_=std::max<UINT>(4u*1024u*1024u,vertex_bytes);
  D3D11_BUFFER_DESC description{};description.ByteWidth=dynamic_vertex_capacity_;description.Usage=D3D11_USAGE_DYNAMIC;description.BindFlags=D3D11_BIND_VERTEX_BUFFER;description.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  dynamic_vertices_.Reset();HRESULT hr=device_->CreateBuffer(&description,nullptr,&dynamic_vertices_);if(FAILED(hr))return hr;
  dynamic_vertex_offset_=0;discard_vertices_=true;
 }
 if(dynamic_vertex_offset_+vertex_bytes>dynamic_vertex_capacity_){dynamic_vertex_offset_=0;discard_vertices_=true;}
 const UINT vertex_offset=dynamic_vertex_offset_;
 D3D11_MAPPED_SUBRESOURCE mapped{};HRESULT hr=context_->Map(dynamic_vertices_.Get(),0,discard_vertices_?D3D11_MAP_WRITE_DISCARD:D3D11_MAP_WRITE_NO_OVERWRITE,0,&mapped);if(FAILED(hr))return hr;
 write_ui_vertices(static_cast<std::uint8_t*>(mapped.pData)+vertex_offset,vertex_starts);
 context_->Unmap(dynamic_vertices_.Get(),0);
 dynamic_vertex_offset_=(vertex_offset+vertex_bytes+15u)&~15u;discard_vertices_=false;

 FixedState overlay{};overlay.viewport.dwWidth=output_width_;overlay.viewport.dwHeight=output_height_;overlay.render[D3DRENDERSTATE_ZENABLE]=D3DZB_FALSE;overlay.render[D3DRENDERSTATE_ZWRITEENABLE]=FALSE;overlay.render[D3DRENDERSTATE_CULLMODE]=D3DCULL_NONE;overlay.render[D3DRENDERSTATE_ALPHABLENDENABLE]=TRUE;overlay.render[D3DRENDERSTATE_SRCBLEND]=D3DBLEND_SRCALPHA;overlay.render[D3DRENDERSTATE_DESTBLEND]=D3DBLEND_INVSRCALPHA;overlay.stage[0][D3DTSS_COLOROP]=D3DTOP_MODULATE;
 auto upload_constants=[&](DWORD alpha_operation,float opacity){
  Constants constants{};constants.world=overlay.transform[D3DTRANSFORMSTATE_WORLD];constants.view=overlay.transform[D3DTRANSFORMSTATE_VIEW];constants.projection=overlay.transform[D3DTRANSFORMSTATE_PROJECTION];constants.viewport[2]=(float)output_width_;constants.viewport[3]=(float)output_height_;constants.texture_factor[0]=constants.texture_factor[1]=constants.texture_factor[2]=constants.texture_factor[3]=1.0f;constants.transformed=TRUE;constants.textured=TRUE;constants.color_op=overlay.stage[0][D3DTSS_COLOROP];constants.alpha_op=alpha_operation;constants.color_arg1=overlay.stage[0][D3DTSS_COLORARG1];constants.color_arg2=overlay.stage[0][D3DTSS_COLORARG2];constants.alpha_arg1=overlay.stage[0][D3DTSS_ALPHAARG1];constants.alpha_arg2=overlay.stage[0][D3DTSS_ALPHAARG2];constants.alpha_func=D3DCMP_ALWAYS;constants.hud_opacity=opacity;constants.texture_factor[3]=opacity;
  return upload_draw_constants(&constants,sizeof(constants));
 };
 remember_debug_projection(overlay,SemanticKind::ui);apply_state(overlay,true);
 const UINT stride=sizeof(Vertex);ID3D11Buffer* buffer=dynamic_vertices_.Get();context_->IASetInputLayout(layout_.Get());context_->IASetVertexBuffers(0,1,&buffer,&stride,&vertex_offset);context_->IASetIndexBuffer(nullptr,DXGI_FORMAT_UNKNOWN,0);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);context_->VSSetShader(vs_.Get(),nullptr,0);context_->PSSetShader(ps_.Get(),nullptr,0);bind_draw_constants();D3D11_VIEWPORT viewport{0,0,(float)target_width(),(float)target_height(),0,1};context_->RSSetViewports(1,&viewport);bind_plan_targets(plan_pass("ui_composition",rtv_.Get(),dsv_.Get()));
 ComPtr<ID3D11SamplerState> backdrop_sampler;
 DWORD current_alpha_operation=0;float current_opacity=-1.f;PerfScope draw_perf(&perf_draw_ticks_);std::size_t physical_draws=0;
 for(std::size_t index=0;index<pending_blits_.size();){
  apply_state(overlay,true,pending_blits_[index].composition);
  const DWORD alpha_operation=ui_blit_alpha_operation(pending_blits_[index].force_opaque_alpha);
  const float opacity=pending_blits_[index].composition.opacity;
  if(alpha_operation!=current_alpha_operation || opacity!=current_opacity){if(FAILED(hr=upload_constants(alpha_operation,opacity)))return hr;current_alpha_operation=alpha_operation;current_opacity=opacity;}
  auto* view=views[index];context_->PSSetShaderResources(0,1,&view);
  const bool output_space=pending_blits_[index].output_space;
  const bool text_filtered=output_space&&pending_blits_[index].filtered_text&&interface_text_ps();
  const bool interface_scaled=!output_space&&widescreen::composition_scaled(pending_blits_[index].composition)&&interface_scaled_ps();
  if(interface_scaled)context_->PSSetShader(interface_scaled_ps_.Get(),nullptr,0);
  if(text_filtered)context_->PSSetShader(interface_text_ps_.Get(),nullptr,0);
  const bool backdrop=pending_blits_[index].backdrop&&backdrop_ps_;
  if(backdrop){
   if(!backdrop_sampler){
    const auto sampling=world_texture_sampler(D3DTFN_LINEAR,D3DTFG_LINEAR,D3DTFP_LINEAR,
     D3DTADDRESS_CLAMP,D3DTADDRESS_CLAMP,false,1u<<configured_integer(debug_overlay_.config(),"anisotropy",3));
    auto& cached=sampler_states_[sampling.key];
    if(!cached&&FAILED(hr=device_->CreateSamplerState(&sampling.description,&cached)))return hr;
    backdrop_sampler=cached;
   }
   context_->PSSetShader(backdrop_ps_.Get(),nullptr,0);context_->PSSetSamplers(0,1,backdrop_sampler.GetAddressOf());
  }
  std::size_t end=index+1u;
  if(pending_blits_[index].batchable_text)while(end<pending_blits_.size()&&pending_blits_[end].batchable_text&&views[end]==view&&pending_blits_[end].output_space==output_space&&pending_blits_[end].filtered_text==pending_blits_[index].filtered_text&&widescreen::same_composition(pending_blits_[end].composition,pending_blits_[index].composition)&&ui_blit_alpha_operation(pending_blits_[end].force_opaque_alpha)==alpha_operation)++end;
  const std::size_t logical_draws=end-index;
  if(logical_draws>(std::numeric_limits<UINT>::max)()/2u)return E_OUTOFMEMORY;
  profiler_.add_draw(static_cast<UINT>(logical_draws)*2u,static_cast<UINT>(logical_draws));profiler_.add_batch();
  if(pending_blits_[index].batchable_text){if(logical_draws>(std::numeric_limits<UINT>::max)()/6u)return E_OUTOFMEMORY;context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context_->Draw(static_cast<UINT>(logical_draws)*6u,vertex_starts[index]);}
  else{context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);context_->Draw(vertex_counts[index],vertex_starts[index]);}
  // Back to the sampler apply_state chose for this blit (no read-back).
  if(backdrop){context_->PSSetShader(ps_.Get(),nullptr,0);context_->PSSetSamplers(0,1,&applied_sampler_);}
  if(interface_scaled||text_filtered)context_->PSSetShader(ps_.Get(),nullptr,0);
  ++physical_draws;
#ifdef EID3D11_TESTING
  if(pending_blits_[index].batchable_text||pending_blits_[index].ui_epoch)
   ++test_last_character_pane_draw_calls_;
#endif
  index=end;
 }
 if(perf_enabled())perf_draw_calls_+=physical_draws;
 context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
 // Keep only this batch's buffers: storage follows the previous UI batch.
 ui_blit_pixel_spares_.clear();
 for(auto& pending:pending_blits_)if(pending.pixels.capacity()){try{ui_blit_pixel_spares_.push_back(std::move(pending.pixels));}catch(const std::bad_alloc&){break;}}
 pending_blits_.clear();return S_OK;
}


// x64 host, renderer_backend=d3d12: render_pending_blits' upload and draw
// half on the D3D12 executor, from the same filtered quads. CPU quads keep
// their overlay cache entries (the D3D12 texture in OverlayTexture::d3d12);
// GPU quads carry their surface's D3D12 texture from the residency.
HRESULT Renderer::render_pending_blits_d3d12(){
 std::vector<const D3D12Texture*> views;
 std::vector<std::uint32_t> text_atlas_pixels;
 try{views.reserve(pending_blits_.size());}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 for(auto& pending:pending_blits_){
  if(pending.presentation_fill){views.push_back(nullptr);continue;}
  const D3D12Texture* view=pending.d3d12.get();
  if(!pending.gpu){
   OverlayTexture transient;
   const auto surface_id=pending.source_is_surface?pending.source_id:0;
   if(pending.source_destroyed&&pending.partial){
    transient.d3d12=pending.d3d12;
    transient.surface_id=surface_id;transient.generation=pending.base_generation;
    transient.width=pending.width;transient.height=pending.height;
   }
   auto& cached=pending.source_destroyed?transient:overlay_textures_[pending.cache_key];
   if(pending.partial&&(!cached.d3d12||cached.width!=pending.width||cached.height!=pending.height||cached.surface_id!=surface_id||cached.generation<pending.base_generation||cached.generation>pending.generation))return E_UNEXPECTED;
   if(!cached.d3d12||cached.surface_id!=surface_id||cached.width!=pending.width||cached.height!=pending.height){
    cached={};cached.surface_id=surface_id;cached.width=pending.width;cached.height=pending.height;
    const HRESULT hr=d3d12_->create_texture(pending.width,pending.height,DXGI_FORMAT_B8G8R8A8_UNORM,cached.d3d12);
    if(FAILED(hr)){cached={};return hr;}
   }
   if(!pending.cached){
    const std::uint32_t* upload=pending.pixels.data();
    if(pending.text_asset){
     if(pending.text_asset->identity!=pending.source_id||pending.text_asset->generation!=pending.generation||pending.text_asset->revision!=pending.revision||pending.text_asset->width!=pending.width||pending.text_asset->height!=pending.height||pending.text_asset->coverage.size()!=static_cast<std::size_t>(pending.width)*pending.height||pending.text_asset->coverage.size()>kMaximumCharacterPaneAtlasPixels)return E_INVALIDARG;
     try{text_atlas_pixels.resize(pending.text_asset->coverage.size());}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
     for(std::size_t index=0;index<pending.text_asset->coverage.size();++index)text_atlas_pixels[index]=(static_cast<std::uint32_t>(pending.text_asset->coverage[index])<<24u)|0x00ffffffu;
     upload=text_atlas_pixels.data();
    }
    const RECT whole{0,0,LONG(pending.width),LONG(pending.height)};
    const RECT& u=pending.partial?pending.update:whole;
    if(u.right>u.left&&u.bottom>u.top){
     // A partial quad's pixels hold only its update rect (as UpdateSubresource reads them).
     if(!upload)return E_INVALIDARG;
     const HRESULT hr=d3d12_->write_texture(*cached.d3d12,u,upload,pending.pitch);if(FAILED(hr))return hr;
    }
    cached.surface_id=pending.source_is_surface?pending.source_id:0;cached.generation=pending.generation;cached.revision=pending.revision;cached.source=pending.source;cached.keyed=pending.keyed;cached.color_key=pending.color_key;
   }
   view=cached.d3d12.get();
   if(pending.source_destroyed){pending.d3d12=cached.d3d12;pending.gpu=true;}
  }
  if(!view)return E_FAIL;
  views.push_back(view);
 }
 for(std::size_t index=0;index<pending_blits_.size();++index){
  if(!pending_blits_[index].presentation_fill)continue;
  if(index+1>=views.size()||pending_blits_[index+1].presentation_fill||!views[index+1])return E_UNEXPECTED;
  views[index]=views[index+1];
 }
 std::vector<UINT> vertex_starts;
 std::vector<Vertex> vertices;
 std::vector<Constants> constants;
 std::vector<D3D12UiDraw> draws;
 try{
  std::uint64_t required=0;vertex_starts.reserve(pending_blits_.size());
  for(const auto& pending:pending_blits_){vertex_starts.push_back(static_cast<UINT>(required));required+=pending.batchable_text?6u:4u;}
  if(required>(std::numeric_limits<UINT>::max)()/sizeof(Vertex))return E_OUTOFMEMORY;
  vertices.resize(static_cast<std::size_t>(required));
  draws.reserve(pending_blits_.size());
 }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 write_ui_vertices(vertices.data(),vertex_starts);
 // Frame generation's full-screen interface signal: the quads' cover.
 for(std::size_t index=0;index<pending_blits_.size();++index){
  const auto& pending=pending_blits_[index];
  const Vertex* quad=vertices.data()+vertex_starts[index];  // top left, top right, bottom left, bottom right (text: its first glyph)
  RECT clip{};const bool clipped=ui_clip_rect(pending.composition,{},clip);
  float x0=std::min(quad[0].x,quad[3].x),x1=std::max(quad[0].x,quad[3].x),y0=std::min(quad[0].y,quad[3].y),y1=std::max(quad[0].y,quad[3].y);
  if(clipped){x0=std::max(x0,float(clip.left));y0=std::max(y0,float(clip.top));x1=std::min(x1,float(clip.right));y1=std::min(y1,float(clip.bottom));}
  mark_interface_cells(x0,y0,x1,y1);
 }
 FixedState overlay{};overlay.stage[0][D3DTSS_COLOROP]=D3DTOP_MODULATE;
 // The same configured color filtering and mip limits as the D3D11 path.
 const UINT anisotropy=1u<<configured_integer(debug_overlay_.config(),"anisotropy",3);
 const D3D11_SAMPLER_DESC sampler=world_texture_sampler(overlay.stage[0][D3DTSS_MINFILTER],overlay.stage[0][D3DTSS_MAGFILTER],
  overlay.stage[0][D3DTSS_MIPFILTER],overlay.stage[0][D3DTSS_ADDRESSU],overlay.stage[0][D3DTSS_ADDRESSV],false,anisotropy).description;
 const D3D11_SAMPLER_DESC backdrop_sampler=world_texture_sampler(D3DTFN_LINEAR,D3DTFG_LINEAR,D3DTFP_LINEAR,
  D3DTADDRESS_CLAMP,D3DTADDRESS_CLAMP,false,anisotropy).description;
 DWORD current_alpha_operation=0;float current_opacity=-1.f;
 try{
  for(std::size_t index=0;index<pending_blits_.size();){
   const auto& first=pending_blits_[index];
   const DWORD alpha_operation=ui_blit_alpha_operation(first.force_opaque_alpha);
   const float opacity=first.composition.opacity;
   if(alpha_operation!=current_alpha_operation||opacity!=current_opacity||constants.empty()){
    Constants block{};block.world=overlay.transform[D3DTRANSFORMSTATE_WORLD];block.view=overlay.transform[D3DTRANSFORMSTATE_VIEW];block.projection=overlay.transform[D3DTRANSFORMSTATE_PROJECTION];
    block.viewport[2]=(float)output_width_;block.viewport[3]=(float)output_height_;
    block.texture_factor[0]=block.texture_factor[1]=block.texture_factor[2]=1.0f;block.transformed=TRUE;block.textured=TRUE;
    block.color_op=overlay.stage[0][D3DTSS_COLOROP];block.alpha_op=alpha_operation;block.color_arg1=overlay.stage[0][D3DTSS_COLORARG1];block.color_arg2=overlay.stage[0][D3DTSS_COLORARG2];
    block.alpha_arg1=overlay.stage[0][D3DTSS_ALPHAARG1];block.alpha_arg2=overlay.stage[0][D3DTSS_ALPHAARG2];block.alpha_func=D3DCMP_ALWAYS;block.hud_opacity=opacity;block.texture_factor[3]=opacity;
    constants.push_back(block);current_alpha_operation=alpha_operation;current_opacity=opacity;
   }
   const bool output_space=first.output_space;
   std::size_t end=index+1u;
   if(first.batchable_text)while(end<pending_blits_.size()&&pending_blits_[end].batchable_text&&views[end]==views[index]&&pending_blits_[end].output_space==output_space&&pending_blits_[end].filtered_text==first.filtered_text&&widescreen::same_composition(pending_blits_[end].composition,first.composition)&&ui_blit_alpha_operation(pending_blits_[end].force_opaque_alpha)==alpha_operation)++end;
   D3D12UiDraw draw;draw.texture=views[index];
   draw.program=first.backdrop?D3D12UiProgram::backdrop:output_space&&first.filtered_text?D3D12UiProgram::interface_text:
    !output_space&&widescreen::composition_scaled(first.composition)?D3D12UiProgram::interface_scaled:D3D12UiProgram::game;
   draw.blend=fixed_blend_choice(TRUE,D3DBLEND_SRCALPHA,D3DBLEND_INVSRCALPHA,opacity<1.f);
   draw.sampler=first.backdrop?backdrop_sampler:sampler;
   draw.scissor=ui_clip_rect(first.composition,{},draw.scissor_rect);
   draw.constants=static_cast<UINT>(constants.size()-1);
   draw.first_vertex=vertex_starts[index];
   draw.triangle_list=first.batchable_text;
   draw.vertex_count=first.batchable_text?static_cast<UINT>(end-index)*6u:4u;
   draws.push_back(draw);
   index=end;
  }
 }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 D3D12UiPass pass;
 pass.vertices=vertices.data();pass.vertex_bytes=static_cast<UINT>(vertices.size()*sizeof(Vertex));pass.vertex_stride=sizeof(Vertex);
 pass.constants=constants.data();pass.constant_block_bytes=sizeof(Constants);pass.constant_blocks=static_cast<UINT>(constants.size());
 pass.draws=draws.data();pass.draw_count=draws.size();
 const HRESULT hr=d3d12_->draw_ui(pass);
 if(perf_enabled())perf_draw_calls_+=draws.size();
 ui_blit_pixel_spares_.clear();
 for(auto& pending:pending_blits_)if(pending.pixels.capacity()){try{ui_blit_pixel_spares_.push_back(std::move(pending.pixels));}catch(const std::bad_alloc&){break;}}
 pending_blits_.clear();
 return hr;
}

// Frame generation: the cells of the 64x36 output grid this opaque
// interface rectangle (output pixels) covers whole.
void Renderer::mark_interface_cells(float x0,float y0,float x1,float y1) noexcept{
 if(!output_width_||!output_height_||x1<=x0||y1<=y0)return;
 const float cw=float(output_width_)/float(kUiCoverageColumns),ch=float(output_height_)/float(kUiCoverageRows);
 const int c0=std::max(0,int(std::ceil(x0/cw-1e-3f))),c1=std::min(int(kUiCoverageColumns),int(std::floor(x1/cw+1e-3f)));
 const int r0=std::max(0,int(std::ceil(y0/ch-1e-3f))),r1=std::min(int(kUiCoverageRows),int(std::floor(y1/ch+1e-3f)));
 if(c1<=c0||r1<=r0)return;
 const std::uint64_t bits=(c1-c0>=64?~0ull:((1ull<<(c1-c0))-1ull))<<c0;
 for(int row=r0;row<r1;++row)ui_quad_cells_[row]|=bits;
}
float Renderer::interface_coverage() const noexcept{
 unsigned cells=0;
 for(const std::uint64_t row:ui_quad_cells_){std::uint64_t v=row;while(v){v&=v-1;++cells;}}
 return float(cells)/float(kUiCoverageColumns*kUiCoverageRows);
}

// x64 host, renderer_backend=d3d12: the stock screen rectangle (fades,
// frames) as UI quads over a white texel, with the D3D11 path's game-pixel
// to output mapping and source-alpha blend. grid_step > 0 adds the HUD
// editor's alignment grid and centre guides (draw_screen_rectangle's).
HRESULT Renderer::draw_screen_rectangle_d3d12(const ScreenRectangleRequest& request,const D3DVIEWPORT7& viewport,float grid_step){
 ScreenRectangleQuad request_quads[5]{};
 const unsigned quad_count=screen_rectangle_quads(request,request_quads);
 if(!quad_count)return S_OK;
 if(!d3d12_white_){
  HRESULT hr=d3d12_->create_texture(1,1,DXGI_FORMAT_B8G8R8A8_UNORM,d3d12_white_);if(FAILED(hr))return hr;
  const std::uint32_t white=0xffffffffu;
  if(FAILED(hr=d3d12_->write_texture(*d3d12_white_,RECT{0,0,1,1},&white,4))){d3d12_white_.reset();return hr;}
 }
 std::vector<ScreenRectangleQuad> quads;std::vector<Vertex> vertices;
 try{
  quads.assign(request_quads,request_quads+quad_count);
  if(grid_step>0){
   for(float x=0;x<viewport.dwWidth;x+=grid_step)quads.push_back({int(x),0,int(x)+1,int(viewport.dwHeight),0x40808080u});
   for(float y=0;y<viewport.dwHeight;y+=grid_step)quads.push_back({0,int(y),int(viewport.dwWidth),int(y)+1,0x40808080u});
   // Brighter two-pixel guides identify the centre independently of grid spacing.
   const int centre_x=int(viewport.dwWidth)/2,centre_y=int(viewport.dwHeight)/2;
   quads.push_back({centre_x-1,0,centre_x+1,int(viewport.dwHeight),0xC0FFCC66u});
   quads.push_back({0,centre_y-1,int(viewport.dwWidth),centre_y+1,0xC0FFCC66u});
  }
  vertices.resize(quads.size()*6u);
 }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 const float sx=float(output_width_)/float(std::max<DWORD>(1,viewport.dwWidth)),sy=float(output_height_)/float(std::max<DWORD>(1,viewport.dwHeight));
 FixedState overlay{};overlay.stage[0][D3DTSS_COLOROP]=D3DTOP_MODULATE;
 Constants block{};block.viewport[2]=(float)output_width_;block.viewport[3]=(float)output_height_;
 block.texture_factor[0]=block.texture_factor[1]=block.texture_factor[2]=block.texture_factor[3]=1.0f;block.transformed=TRUE;block.textured=TRUE;
 block.color_op=D3DTOP_MODULATE;block.alpha_op=D3DTOP_MODULATE;block.color_arg1=overlay.stage[0][D3DTSS_COLORARG1];block.color_arg2=overlay.stage[0][D3DTSS_COLORARG2];
 block.alpha_arg1=overlay.stage[0][D3DTSS_ALPHAARG1];block.alpha_arg2=overlay.stage[0][D3DTSS_ALPHAARG2];block.alpha_func=D3DCMP_ALWAYS;block.hud_opacity=1.0f;
 const D3D11_SAMPLER_DESC sampler=world_texture_sampler(D3DTFN_POINT,D3DTFG_POINT,D3DTFP_NONE,
  D3DTADDRESS_CLAMP,D3DTADDRESS_CLAMP,false,1u<<configured_integer(debug_overlay_.config(),"anisotropy",3)).description;
 // One triangle-list draw for every quad (they share texture, blend and program).
 for(std::size_t index=0;index<quads.size();++index){
  const auto& q=quads[index];
  const float x0=(float(q.left)-float(viewport.dwX))*sx,x1=(float(q.right)-float(viewport.dwX))*sx;
  const float y0=(float(q.top)-float(viewport.dwY))*sy,y1=(float(q.bottom)-float(viewport.dwY))*sy;
  const Vertex a{x0,y0,0,1,q.color,0xffffffffu,0,0},b{x1,y0,0,1,q.color,0xffffffffu,1,0};
  const Vertex c{x0,y1,0,1,q.color,0xffffffffu,0,1},d{x1,y1,0,1,q.color,0xffffffffu,1,1};
  Vertex* quad=vertices.data()+index*6u;
  quad[0]=a;quad[1]=b;quad[2]=d;quad[3]=a;quad[4]=d;quad[5]=c;
 }
 D3D12UiDraw draw;
 draw.texture=d3d12_white_.get();draw.program=D3D12UiProgram::game;
 draw.blend=fixed_blend_choice(TRUE,D3DBLEND_SRCALPHA,D3DBLEND_INVSRCALPHA,false);
 draw.sampler=sampler;draw.triangle_list=true;draw.first_vertex=0;draw.vertex_count=static_cast<UINT>(vertices.size());
 D3D12UiPass pass;
 pass.vertices=vertices.data();pass.vertex_bytes=static_cast<UINT>(vertices.size()*sizeof(Vertex));pass.vertex_stride=sizeof(Vertex);
 pass.constants=&block;pass.constant_block_bytes=sizeof(Constants);pass.constant_blocks=1;
 pass.draws=&draw;pass.draw_count=1;
 return d3d12_->draw_ui(pass);
}

// ---- Queued UI bodies (standard-mode closures and the core-mode host) -------
void Renderer::ui_run_blits(std::vector<scene_ui::Quad>& quads,const scene_ui::Residency& residency){
 try{for(auto& quad:quads){PendingBlit pending{};ui_blit(quad,residency,pending);pending_blits_.push_back(std::move(pending));}}
 catch(...){report_scene_failure("blit",E_OUTOFMEMORY);}
}
void Renderer::ui_run_present_image(scene_ui::PresentImage& command,const scene_ui::Residency& residency){
 load_picture_this_frame_=true;if(command.fitted)load_picture_fitted_=true;
 if(command.fitted){progress_movie_input_=std::move(command.movie);progress_movie_input_valid_=command.movie_valid;}
 try{
  const bool fill=command.filled&&command.quads.size()==1;
  for(std::size_t index=0;index<command.quads.size();++index){
   PendingBlit pending{};ui_blit(command.quads[index],residency,pending);
   if(fill&&index==0)pending_blits_.push_back(make_presentation_fill(pending,command.fit));
   pending_blits_.push_back(std::move(pending));
  }
 }catch(...){report_scene_failure("present_image",E_OUTOFMEMORY);}
}
void Renderer::ui_run_draw_frame(scene_ui::DrawFrame& command,const scene_ui::Residency& residency){
 for(std::size_t index=0;index<command.packets.size()&&index<command.textures.size();++index)
  command.packets[index].texture=residency.image(command.textures[index]);
 report_scene_failure("draw_frame",draw_frame(command.packets,command.vertex_bytes,command.indices));
}

// ---- Core mode (S3): the game process's surface writes ----------------------
// Game process: the surface's pending write (whole, or the dirty rows at full
// pitch) as a SurfaceWrite; the host mirrors it and uploads it on its device.
// The CPU pixels stay here: nothing can restore released bytes from the host
// yet, so core mode never releases them (listed for S4/S5).
#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
HRESULT Renderer::core_surface_write(SurfaceImage& image) { return E_NOTIMPL; }
#else
HRESULT Renderer::core_surface_write(SurfaceImage& image){
 const bool shape_same=same_surface_upload_shape(image.uploaded_shape,surface_upload_shape(image));
 if(image.uploaded_generation==image.generation&&!image.dirty_full&&!image.dirty_rect_valid&&shape_same)return S_OK;
 if(image.pixels.empty()||image.pitch<=0||image.cpu_released)return E_FAIL;
 try{
  scene_ui::SurfaceWrite write;
  write.id=image.id;write.generation=image.generation;write.width=image.width;write.height=image.height;
  write.pitch=image.pitch;write.format=image.format;write.has_color_key=image.has_color_key;write.color_key=image.color_key;
  write.static_mip_hint=image.static_mip_hint;write.world_mip_use=image.world_mip_use;write.mip_source_used=image.mip_source_used;
  const bool partial=shape_same&&image.uploaded_generation&&!image.dirty_full&&image.dirty_rect_valid&&
   !(image.format.dwFlags&DDPF_FOURCC)&&image.dirty_rect.bottom>image.dirty_rect.top;
  const std::uint8_t* pixels=static_cast<const SurfacePixels&>(image.pixels).data();
  if(partial){
   const std::size_t first=std::size_t(image.dirty_rect.top),rows=std::size_t(image.dirty_rect.bottom-image.dirty_rect.top);
   const std::size_t offset=first*std::size_t(image.pitch),bytes=rows*std::size_t(image.pitch);
   if(offset+bytes>image.pixels.size())return E_FAIL;
   write.whole=false;write.dirty=image.dirty_rect;write.first_row=std::uint32_t(first);write.rows=std::uint32_t(rows);
   write.bytes.assign(pixels+offset,pixels+offset+bytes);
  }else{
   write.whole=true;write.rows=image.height;
   write.bytes.assign(pixels,pixels+image.pixels.size());
  }
  core_post_command(write,"surface_write");
 }catch(...){return E_OUTOFMEMORY;}
 image.uploaded_generation=image.generation;image.uploaded_shape=surface_upload_shape(image);
 clear_surface_dirty(image);
 return S_OK;
}
#endif

#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
std::shared_ptr<SurfaceImage> Renderer::core_surface_snapshot(std::uint64_t id,std::uint64_t generation) { return {}; }
#else
std::shared_ptr<SurfaceImage> Renderer::core_surface_snapshot(std::uint64_t id,std::uint64_t generation){
 const auto found=core_surfaces_.find(id);
 const SurfaceImage* image=found!=core_surfaces_.end()?found->second.get():nullptr;
 if(!image||image->generation!=generation||!(image->srv||image->d3d12)){
  static std::uint64_t misses=0;
  if(++misses<=16||!(misses&(misses-1))){
   char detail[160];std::snprintf(detail,sizeof(detail),"surface=%llu generation=%llu host_generation=%llu misses=%llu (omitted)",
    static_cast<unsigned long long>(id),static_cast<unsigned long long>(generation),
    static_cast<unsigned long long>(image?image->generation:0),static_cast<unsigned long long>(misses));
   bridge_report_event("core_surface_unresolved",detail);
  }
  return nullptr;
 }
 return surface_snapshot(*image);
}
#endif

#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
HRESULT Renderer::core_resolve_surface(std::uint64_t id,std::uint64_t generation,ComPtr<ID3D11Texture2D>& texture,
 SurfaceImage*& image) noexcept { return E_NOTIMPL; }
#else
HRESULT Renderer::core_resolve_surface(std::uint64_t id,std::uint64_t generation,ComPtr<ID3D11Texture2D>& texture,
 SurfaceImage*& image) noexcept{
 texture.Reset();image=nullptr;
 const auto found=core_surfaces_.find(id);
 // D3D12 backend: an image with its pixels (the caller uploads it).
 if(found==core_surfaces_.end()||!found->second||found->second->generation!=generation||(d3d12_?!found->second->d3d12&&found->second->cpu_released:!found->second->texture))return E_PENDING;
 texture=found->second->texture;image=found->second.get();
 return S_OK;
}
#endif

// Host: one command's handles through the surface table (a handle whose
// generation the host does not hold is omitted, logged by core_surface_snapshot).
#if defined(EID3D11_EMBEDDED) || defined(EID3D11_SHADER_TOOL)
scene_ui::Residency Renderer::core_residency(const std::vector<scene_ui::SurfaceHandle>& handles) { return {}; }
#else
scene_ui::Residency Renderer::core_residency(const std::vector<scene_ui::SurfaceHandle>& handles){
 scene_ui::Residency residency;
 for(const auto& handle:handles){
  if(!handle.present||residency.find(handle))continue;
  auto snapshot=core_surface_snapshot(handle.id,handle.generation);
  if(!snapshot)continue;
  ComPtr<ID3D11Texture2D> texture=snapshot->texture;ComPtr<ID3D11ShaderResourceView> srv=snapshot->srv;
  D3D12TextureRef d3d12=snapshot->d3d12;
  residency.add({handle,std::move(snapshot),std::move(texture),std::move(srv),std::move(d3d12)});
 }
 return residency;
}
#endif

namespace scene_ui {

// Host: the game surface's mirror takes the write, then the host's own
// upload path converts and uploads it (detach/unshare and mips as there).
void SurfaceWrite::run(Renderer& renderer){
 auto& entry=renderer.core_surfaces_[id];
 const bool same=entry&&entry->width==width&&entry->height==height&&entry->pitch==pitch&&
  std::memcmp(&entry->format,&format,sizeof(format))==0;
 if(!same){
  if(!whole){
   renderer.core_surfaces_.erase(id);
   char detail[128];std::snprintf(detail,sizeof(detail),"surface=%llu: a partial write without its base (omitted until a whole write)",
    static_cast<unsigned long long>(id));
   bridge_report_event("core_surface_write_failed",detail);
   return;
  }
  entry=std::make_shared<SurfaceImage>();
  entry->id=id;entry->width=width;entry->height=height;entry->pitch=pitch;entry->format=format;
 }
 SurfaceImage& image=*entry;
 if(whole){
  image.pixels=bytes;
  mark_surface_dirty_full(image);
 }else{
  const std::size_t offset=std::size_t(first_row)*std::size_t(pitch);
  if(offset+bytes.size()>image.pixels.size()){
   bridge_report_event("core_surface_write_failed","partial rows outside the mirror (omitted)");
   return;
  }
  std::memcpy(image.pixels.data()+offset,bytes.data(),bytes.size());
  mark_surface_dirty(image,dirty);
 }
 image.has_color_key=has_color_key;image.color_key=color_key;
 image.static_mip_hint=static_mip_hint;image.world_mip_use=world_mip_use;image.mip_source_used=mip_source_used;
 image.generation=generation;
 Renderer::report_scene_failure("surface_write",renderer.upload(image));
}

void Blits::run(Renderer& renderer){
 std::vector<SurfaceHandle> handles;handles.reserve(quads.size());
 for(const auto& quad:quads)handles.push_back(quad.home);
 const Residency residency=renderer.core_residency(handles);
 renderer.ui_run_blits(quads,residency);
}
void PresentImage::run(Renderer& renderer){
 std::vector<SurfaceHandle> handles;handles.reserve(quads.size());
 for(const auto& quad:quads)handles.push_back(quad.home);
 const Residency residency=renderer.core_residency(handles);
 renderer.ui_run_present_image(*this,residency);
}
void DrawFrame::run(Renderer& renderer){
 const Residency residency=renderer.core_residency(textures);
 renderer.ui_run_draw_frame(*this,residency);
}
void Cursor::run(Renderer& renderer){
 Renderer::report_scene_failure("queue_cursor",renderer.queue_cursor(cache_key,pixels,width,height,destination));
}
void RetireSurface::run(Renderer& renderer){
 renderer.core_surfaces_.erase(surface_id);
 renderer.retire_surface(surface_id);
}
void FlushUi::run(Renderer& renderer){Renderer::report_scene_failure("flush_ui",renderer.flush_ui());}
void ScreenRectangle::run(Renderer& renderer){
 Renderer::report_scene_failure("queue_screen_rectangle",renderer.queue_screen_rectangle(request,viewport));
}
// begin_scene_game's HUD border layer at the tooltip boundary.
void HudFrameLayer::run(Renderer& renderer){
 HRESULT hr=renderer.flush_native();
 if(SUCCEEDED(hr))hr=renderer.composite_world();
 if(SUCCEEDED(hr))hr=renderer.render_pending_blits();
 if(SUCCEEDED(hr))hr=renderer.draw_hud_asset_frames(&frames);
 Renderer::report_scene_failure("hud_frame_layer",hr);
}

// ---- core mode: camp icons and prepared figures (claude/s3-store) ----
// The host icon worker's alpha fact for a bound camp icon surface
// (Host/core_camp.cpp; the game process's build answers false).
bool core_camp_icon_opaque(std::uint64_t id,std::uint64_t generation,bool& opaque) noexcept;
namespace {
// The host's prepared handles per prepared identity: what the game process
// keeps in its object plan (the renderer validates every handle against its
// slot's identity, so a new handle only allocates and a stale one only
// misses). One world's identities; a new world epoch starts over.
struct PreparedKey{
 std::int32_t id;std::uint64_t lifecycle,epoch,plan;std::uint32_t part,set;bool terrain;
 bool operator<(const PreparedKey& o) const noexcept{
  return std::tie(id,lifecycle,epoch,plan,part,set,terrain)<std::tie(o.id,o.lifecycle,o.epoch,o.plan,o.part,o.set,o.terrain);
 }
};
struct PreparedHandles{NativePreparedMeshHandle mesh;NativePreparedMaterialHandle material,wound;};
std::map<PreparedKey,PreparedHandles> g_core_prepared;
std::uint64_t g_core_prepared_epoch=0;
}
void Packet::run(Renderer& renderer){
 if(renderer.d3d12_){
  // D3D12 backend: interface packets are queued: the owned HUD meshes and
  // every plain packet after the frame's world composite (the interface 3D;
  // the D3D11 frame draws it after the composite as well), with a bound
  // surface texture or an owned mip chain. World packets wait for the world
  // (named, the marker colour).
  const bool interface_time=renderer.scene_composited_||view.semantic_kind==SemanticKind::ui;
  const bool plain=!camp_surface_present&&!prepared_handles&&!view.world.enabled&&!view.static_submission&&
   !view.prepared_submission&&!view.depth_submission&&
   (!view.owned_texture.identity||(!view.owned_texture.surface&&!mips.empty()));
  // A camp icon packet (the popup's item icons) samples its host-decoded
  // icon through the owned texture cache, as on D3D11.
  const bool camp=camp_surface_present&&!prepared_handles&&view.owned_texture.camp_surface_alias;
  // A plain world packet queues as well: the flush draws its terrain
  // batches and names every other world batch by kind (not the whole packet).
  if(!(plain||(camp&&interface_time))){
   // Named by the first unported kind, so a log says which packets they are.
   const char* name=
    prepared_handles||view.prepared_submission?(interface_time?"scene_packet (interface: prepared)":"scene_packet (world: prepared)"):
    view.static_submission?(interface_time?"scene_packet (interface: static submission)":"scene_packet (world: static submission)"):
    view.depth_submission?(interface_time?"scene_packet (interface: depth submission)":"scene_packet (world: depth submission)"):
    view.world.enabled?(interface_time?"scene_packet (interface: world-lit)":"scene_packet (world: world-lit)"):
    camp_surface_present?(interface_time?"scene_packet (interface: camp surface)":"scene_packet (world: camp surface)"):
    (interface_time?"scene_packet (interface: surface-owned texture)":"scene_packet (world: surface-owned texture)");
   renderer.d3d12_->omit(name,!interface_time);
   return;
  }
  if(!interface_time){
   renderer.d3d12_->count("world packets queued");
   std::shared_ptr<SurfaceImage> world_bound=bound.present?renderer.core_surface_snapshot(bound.id,bound.generation):nullptr;
   std::shared_ptr<SurfaceImage> world_wound=wound.present?renderer.core_surface_snapshot(wound.id,wound.generation):nullptr;
   submit_scene_packet(renderer,*this,std::move(world_bound),std::move(world_wound));
   return;
  }
  if(camp){
   bool opaque=false;
   if(core_camp_icon_opaque(camp_surface.id,camp_surface.generation,opaque)){
    camp_surface.alpha_fully_opaque=opaque;view.owned_texture.opaque_mips=opaque?1u:0u;
   }
   renderer.d3d12_->count("interface packets queued (camp icons)");
   submit_scene_packet(renderer,*this,nullptr,nullptr);
   return;
  }
  std::shared_ptr<SurfaceImage> bound_image=bound.present?renderer.core_surface_snapshot(bound.id,bound.generation):nullptr;
  renderer.d3d12_->count(view.semantic_kind==SemanticKind::ui?"interface packets queued (owned HUD meshes)":"interface packets queued (after the composite)");
  std::shared_ptr<SurfaceImage> wound_image=wound.present?renderer.core_surface_snapshot(wound.id,wound.generation):nullptr;
  submit_scene_packet(renderer,*this,std::move(bound_image),std::move(wound_image));
  return;
 }
 if(prepared_handles){
  const auto& identity=view.prepared_identity;
  if(identity.world_epoch!=g_core_prepared_epoch){g_core_prepared.clear();g_core_prepared_epoch=identity.world_epoch;}
  auto& handles=g_core_prepared[{identity.signed_id,identity.lifecycle_generation,identity.world_epoch,
   identity.plan_generation,identity.part_index,identity.set_index,identity.terrain_source}];
  if(prepared_handles&1u)view.prepared_mesh=&handles.mesh;
  if(prepared_handles&2u)view.prepared_material=&handles.material;
  if(prepared_handles&4u)view.prepared_wound_material=&handles.wound;
 }
 if(camp_surface_present){
  bool opaque=false;
  if(core_camp_icon_opaque(camp_surface.id,camp_surface.generation,opaque)){
   camp_surface.alpha_fully_opaque=opaque;view.owned_texture.opaque_mips=opaque?1u:0u;
  }
  submit_scene_packet(renderer,*this,nullptr,nullptr);
  return;
 }
 std::shared_ptr<SurfaceImage> bound_image=bound.present?renderer.core_surface_snapshot(bound.id,bound.generation):nullptr;
 std::shared_ptr<SurfaceImage> wound_image=wound.present?renderer.core_surface_snapshot(wound.id,wound.generation):nullptr;
 submit_scene_packet(renderer,*this,std::move(bound_image),std::move(wound_image));
}

void Capture::run(Renderer& renderer){
 auto& entry=renderer.core_surfaces_[target.id];
 if(renderer.d3d12_){
  // The D3D12 frame drawn so far into the surface's own D3D12 texture.
  if(!entry||entry->width!=width||entry->height!=height||!entry->d3d12){
   entry=std::make_shared<SurfaceImage>();entry->id=target.id;entry->width=width;entry->height=height;entry->pitch=LONG(width*4);
   entry->format.dwSize=sizeof(DDPIXELFORMAT);entry->format.dwFlags=DDPF_RGB;entry->format.dwRGBBitCount=32;
   entry->format.dwRBitMask=0x00ff0000;entry->format.dwGBitMask=0x0000ff00;entry->format.dwBBitMask=0x000000ff;
   if(FAILED(renderer.d3d12_->create_texture(width,height,DXGI_FORMAT_B8G8R8A8_UNORM,entry->d3d12))){
    renderer.core_surfaces_.erase(target.id);Renderer::report_scene_failure("capture",E_FAIL);return;
   }
  }
  // The zone hit-test frame: its own target, flushed (capture_source's rule).
  const bool hit_test=renderer.hit_test_active_&&renderer.hit_test12_;
  HRESULT hr=hit_test?renderer.flush_native():renderer.render_pending_blits();
  if(SUCCEEDED(hr))hr=renderer.d3d12_->capture(*entry->d3d12,x,y,RECT{LONG(box_left),LONG(box_top),LONG(box_right),LONG(box_bottom)},
                                               hit_test?renderer.hit_test12_.get():nullptr);
  if(SUCCEEDED(hr)){entry->generation=target.generation;entry->uploaded_generation=target.generation;entry->gpu_captured=true;++entry->texture_serial;}
  Renderer::report_scene_failure("capture",hr);
  return;
 }
 if(!entry||entry->width!=width||entry->height!=height||!entry->texture){
  entry=std::make_shared<SurfaceImage>();entry->id=target.id;entry->width=width;entry->height=height;entry->pitch=LONG(width*4);
  entry->format.dwSize=sizeof(DDPIXELFORMAT);entry->format.dwFlags=DDPF_RGB;entry->format.dwRGBBitCount=32;
  entry->format.dwRBitMask=0x00ff0000;entry->format.dwGBitMask=0x0000ff00;entry->format.dwBBitMask=0x000000ff;
  D3D11_TEXTURE2D_DESC td{};td.Width=width;td.Height=height;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;
  td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  if(!renderer.device_||FAILED(renderer.device_->CreateTexture2D(&td,nullptr,&entry->texture))||
     FAILED(renderer.device_->CreateShaderResourceView(entry->texture.Get(),nullptr,&entry->srv))){
   renderer.core_surfaces_.erase(target.id);Renderer::report_scene_failure("capture",E_FAIL);return;
  }
 }
 ComPtr<ID3D11Texture2D> back;HRESULT hr=renderer.capture_source(back);
 if(SUCCEEDED(hr)){
  ID3D11ShaderResourceView* null_view=nullptr;renderer.context_->PSSetShaderResources(0,1,&null_view);
  const D3D11_BOX box{box_left,box_top,0,box_right,box_bottom,1};
  renderer.context_->CopySubresourceRegion(entry->texture.Get(),0,x,y,0,back.Get(),0,&box);
  entry->generation=target.generation;entry->uploaded_generation=target.generation;entry->gpu_captured=true;
  ++entry->texture_serial;
 }
 Renderer::report_scene_failure("capture",hr);
}

void CampDecorationFrame::run(Renderer& renderer){renderer.queue_camp_decoration(frame);}
void BeginCampPopup::run(Renderer& renderer){Renderer::report_scene_failure("begin_camp_popup",renderer.begin_camp_popup(clear));}
void EndCampPopup::run(Renderer& renderer){renderer.end_camp_popup(complete);}
void ResetCampPopup::run(Renderer& renderer){renderer.reset_camp_popup();}
void RetireCampTexture::run(Renderer& renderer){renderer.retire_camp_texture(surface_id,generation);}

void GlyphAtlas::run(Renderer& renderer){
 try{renderer.core_glyphs_[asset.identity]=std::make_shared<const CharacterPaneGlyphAsset>(std::move(asset));}
 catch(...){Renderer::report_scene_failure("glyph_atlas",E_OUTOFMEMORY);}
}
void CharacterPaneText::run(Renderer& renderer){
 for(std::size_t index=0;index<frame.quads.size()&&index<assets.size();++index){
  const auto& key=assets[index];
  const auto found=key.present?renderer.core_glyphs_.find(key.identity):renderer.core_glyphs_.end();
  if(found!=renderer.core_glyphs_.end()&&found->second->generation==key.generation&&found->second->revision==key.revision)
   frame.quads[index].asset=found->second;
 }
 Renderer::report_scene_failure("queue_character_pane_text",
  renderer.queue_character_pane_text(frame,ui_epoch,frame_serial,destination,has_source?&source:nullptr,interface_space,observe_scale,has_fit?&fit:nullptr));
}

}  // namespace scene_ui
} // namespace owned

namespace owned {

#ifdef EID3D11_EMBEDDED
#include "embedded_figure_material.inl"
#endif
} // namespace owned
