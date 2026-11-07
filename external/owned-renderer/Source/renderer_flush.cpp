// Renderer flush (D3D11 submission): flush_native, legacy draws and their state, game effect draws, the constant/upload rings.
#include <intrin.h>
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
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <unordered_set>
#include "renderer_internal.h"

namespace owned {
namespace {

// The fast combiner modes of nativeCombineColor/nativeCombineAlpha
// (native_shader.h) for one stage-0 operation: 1 texture*diffuse, 2 texture,
// 3 diffuse (the current value), 0 the general selector chains. Arguments
// as nativeStageArg reads them: source 0 or 1 (current is the diffuse at
// stage 0) the diffuse, 2 the texture; a complement or alpha-replicate bit
// takes the general chains. nativeColorResult/nativeAlphaResult keep the
// current value for every operation other than 2, 3, 4, 5 and 7.
static UINT native_fast_combiner_mode(DWORD op,DWORD arg1,DWORD arg2) noexcept{
 const auto kind=[](DWORD arg)->int{if(arg&0x30u)return 0;const DWORD source=arg&15u;return source<=1u?1:source==2u?2:0;};
 switch(op){
 case 2:return kind(arg1)==2?2u:kind(arg1)==1?3u:0u;  // select argument 1
 case 3:return kind(arg2)==2?2u:kind(arg2)==1?3u:0u;  // select argument 2
 case 4:return kind(arg1)+kind(arg2)==3&&kind(arg1)!=0&&kind(arg2)!=0?1u:0u; // modulate texture and diffuse
 case 5:case 7:return 0u;                               // modulate 2x, add
 default:return 3u;
 }
}
}  // namespace

bool Renderer::ui_clip_rect(const widescreen::HudComposition& hud,const camp::Clip& clip,RECT& rectangle) const noexcept{
 if(!clip.enabled&&!hud.clip_native_screen)return false;
 // Both contracts describe native-canvas pixels. Their intersection keeps
 // framed camp previews inside their cells and HUD apertures intact.
 camp::Rect bounds{0.f,0.f,float(canvas_width()),float(canvas_height())};
 if(hud.clip_native_screen){
  if(hud.clip_right>=0.f)bounds={hud.clip_left,hud.clip_top,hud.clip_right,hud.clip_bottom};
  else bounds={widescreen::compose_x(0.f,hud),widescreen::compose_y(0.f,hud),
               widescreen::compose_x(float(canvas_width()),hud),widescreen::compose_y(float(canvas_height()),hud)};
 }
 if(clip.enabled){
  bounds.left=(std::max)(bounds.left,clip.bounds.left);bounds.top=(std::max)(bounds.top,clip.bounds.top);
  bounds.right=(std::max)(bounds.left,(std::min)(bounds.right,clip.bounds.right));
  bounds.bottom=(std::max)(bounds.top,(std::min)(bounds.bottom,clip.bounds.bottom));
 }
 const float sx=float(target_width())/float(canvas_width()),sy=float(target_height())/float(canvas_height());
 const auto x=[&](float value){return (std::max)(0.f,(std::min)(float(target_width()),value*sx));};
 const auto y=[&](float value){return (std::max)(0.f,(std::min)(float(target_height()),value*sy));};
 // An empty clip stays empty: rounding a zero-width clip outward left a
 // one-pixel column of a collapsed panel's body (#186).
 const bool empty=bounds.right<=bounds.left || bounds.bottom<=bounds.top;
 rectangle=empty?RECT{0,0,0,0}:RECT{LONG(std::floor(x(bounds.left))),LONG(std::floor(y(bounds.top))),
  LONG(std::ceil(x(bounds.right))),LONG(std::ceil(y(bounds.bottom)))};
 return true;
}

HRESULT Renderer::apply_state(const FixedState& s,bool,const widescreen::HudComposition& hud,const camp::Clip& clip){
 restore_camp_clip();
 context_->RSSetScissorRects(0,nullptr);
 const DWORD cm=s.render[D3DRENDERSTATE_CULLMODE];
 const bool wireframe=s.render[D3DRENDERSTATE_FILLMODE]==D3DFILL_WIREFRAME;
 // Cache the actual D3D11 descriptor choices, including scissor enablement.
 const std::uint64_t rasterKey=(wireframe?4u:0u)|(cm==D3DCULL_NONE?0u:cm==D3DCULL_CW?1u:2u);
 const auto create_raster=[&](bool scissor,ComPtr<ID3D11RasterizerState>& rs){
  if(rs)return S_OK;
  const D3D11_RASTERIZER_DESC rd=owned::fixed_raster_desc(cm,wireframe,scissor);
  const HRESULT hr=device_->CreateRasterizerState(&rd,&rs);
  if(FAILED(hr)&&clip.enabled&&!camp_raster_failure_reported_){
   camp_raster_failure_reported_=true;
   bridge_append_probe_event("camp_item_clip_raster_failed",
    "D3D11 framed-item rasterizer creation failed; the affected camp icon draw is omitted");
  }
  return hr;
 };
 auto& unclipped=raster_states_[rasterKey];
 HRESULT hr=create_raster(false,unclipped);if(FAILED(hr)){context_->RSSetState(nullptr);return hr;}
 context_->RSSetState(unclipped.Get());
  RECT rectangle{};
  if(ui_clip_rect(hud,clip,rectangle)){
   auto& clipped=raster_states_[rasterKey|8u];
   if(FAILED(hr=create_raster(true,clipped)))return hr;
   camp_unclipped_raster_=unclipped.Get();
   context_->RSSetState(clipped.Get());context_->RSSetScissorRects(1,&rectangle);
 }
 const std::uint64_t depthKey=(std::uint64_t(s.render[D3DRENDERSTATE_ZENABLE])<<32)|(std::uint64_t(s.render[D3DRENDERSTATE_ZWRITEENABLE])<<16)|s.render[D3DRENDERSTATE_ZFUNC];auto& ds=depth_states_[depthKey];if(!ds){const D3D11_DEPTH_STENCIL_DESC dd=owned::fixed_depth_desc(s.render[D3DRENDERSTATE_ZENABLE],s.render[D3DRENDERSTATE_ZWRITEENABLE],s.render[D3DRENDERSTATE_ZFUNC]);
  // A failed state omits the draw (named once) instead of drawing with the D3D default.
  const HRESULT created=device_->CreateDepthStencilState(&dd,&ds);
  if(FAILED(created)){static bool reported=false;if(!reported){reported=true;bridge_append_probe_event("render_state_creation_failed","depth-stencil state creation failed; draws needing it are omitted");}return created;}}
 context_->OMSetDepthStencilState(ds.Get(),0);
 const bool faded=hud.opacity<1.f;
 const owned::FixedBlend choice=owned::fixed_blend_choice(s.render[D3DRENDERSTATE_ALPHABLENDENABLE],s.render[D3DRENDERSTATE_SRCBLEND],s.render[D3DRENDERSTATE_DESTBLEND],faded);
 const std::uint64_t blendKey=(std::uint64_t(choice.enabled)<<32)|(std::uint64_t(choice.source)<<16)|choice.destination;auto& bs=blend_states_[blendKey];if(!bs){const D3D11_BLEND_DESC bd=owned::fixed_blend_desc(choice);
  const HRESULT created=device_->CreateBlendState(&bd,&bs);
  if(FAILED(created)){static bool reported=false;if(!reported){reported=true;bridge_append_probe_event("render_state_creation_failed","blend state creation failed; draws needing it are omitted");}return created;}}
 float factor[4]={1,1,1,1};context_->OMSetBlendState(bs.Get(),factor,0xffffffffu);
 const auto sampling=world_texture_sampler(s.stage[0][D3DTSS_MINFILTER],
  s.stage[0][D3DTSS_MAGFILTER],s.stage[0][D3DTSS_MIPFILTER],
  s.stage[0][D3DTSS_ADDRESSU],s.stage[0][D3DTSS_ADDRESSV],!scene_composited_,
  1u<<(flush_anisotropy_>=0?flush_anisotropy_:configured_integer(debug_overlay_.config(),"anisotropy",3)));
 auto& sampler=sampler_states_[sampling.key];
 if(!sampler){const HRESULT hr=device_->CreateSamplerState(&sampling.description,&sampler);
  if(FAILED(hr))OutputDebugStringA("EID3D11 sampler creation failed\n");}
 context_->PSSetSamplers(0,1,sampler.GetAddressOf());
 applied_sampler_=sampler.Get(); // a pass that swaps s0 for one draw puts this back (render_pending_blits)
 return S_OK;
}

HRESULT Renderer::draw(D3DPRIMITIVETYPE type,DWORD fvf,const void* data,DWORD count,const WORD* indices,DWORD index_count,const FixedState& st,SurfaceImage* texture,SemanticKind semantic_kind,std::uint32_t caller_rva,std::uint32_t legacy_draw_count,const widescreen::HudComposition& hud_composition,ID3D11ShaderResourceView* owned_texture){SCENE_ENTRY("draw");
 if(!device_||!data||!count)return E_INVALIDARG;bool tl=(fvf&D3DFVF_POSITION_MASK)==D3DFVF_XYZRHW,xyz=(fvf&D3DFVF_POSITION_MASK)==D3DFVF_XYZ;if(!tl&&!xyz)return E_NOTIMPL;const auto* p=(const std::uint8_t*)data;UINT stride=legacy_fvf_stride(fvf);bool normal=(fvf&D3DFVF_NORMAL)!=0,diff=(fvf&D3DFVF_DIFFUSE)!=0,spec=(fvf&D3DFVF_SPECULAR)!=0;DWORD tc=(fvf&D3DFVF_TEXCOUNT_MASK)>>D3DFVF_TEXCOUNT_SHIFT;
 std::vector<Vertex> v(count);for(DWORD n=0;n<count;n++){const auto* q=p+n*stride;std::memcpy(&v[n].x,q,12);q+=12;v[n].w=tl?*(const float*)q:1;if(tl)q+=4;if(normal)q+=12;v[n].diffuse=diff?*(const DWORD*)q:0xffffffffu;if(diff)q+=4;v[n].specular=spec?*(const DWORD*)q:0xffffffffu;if(spec)q+=4;v[n].u=tc?*(const float*)q:0;v[n].v=tc?*(const float*)(q+4):0;}
 return draw_fixed_function(type,v,indices,index_count,st,texture,semantic_kind,caller_rva,
  legacy_draw_count,hud_composition,owned_texture,tl,spec);
}

#ifdef EID3D11_EMBEDDED
HRESULT Renderer::draw_embedded_fixed_function(D3DPRIMITIVETYPE type,const FixedFunctionVertex* data,
 DWORD count,const WORD* indices,DWORD index_count,const FixedState& st,
 ID3D11ShaderResourceView* texture){SCENE_ENTRY("draw_embedded_fixed_function");
 if(!device_||!data||!count||count>65536u||(!indices&&index_count)||(indices&&!index_count))return E_INVALIDARG;
 const DWORD elements=indices?index_count:count;
 switch(type){
 case D3DPT_POINTLIST:break;
 case D3DPT_LINELIST:if(elements%2u)return E_INVALIDARG;break;
 case D3DPT_LINESTRIP:if(elements<2u)return E_INVALIDARG;break;
 case D3DPT_TRIANGLELIST:if(elements%3u)return E_INVALIDARG;break;
 case D3DPT_TRIANGLESTRIP:if(elements<3u)return E_INVALIDARG;break;
 default:return E_NOTIMPL;
 }
 for(DWORD i=0;i<index_count;++i)if(indices[i]>=count)return E_INVALIDARG;
 if(texture){ComPtr<ID3D11Device> actual;texture->GetDevice(&actual);
  if(actual.Get()!=device_.Get())return E_INVALIDARG;}
 std::vector<Vertex> vertices;
 try{vertices.resize(count);}catch(...){return E_OUTOFMEMORY;}
 std::memcpy(vertices.data(),data,count*sizeof(Vertex));
 for(const auto& v:vertices)
  if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)||!std::isfinite(v.w)||
     !std::isfinite(v.u)||!std::isfinite(v.v))return E_INVALIDARG;
 // Flush earlier native/UI work at this exact stream position. SCENE_ENTRY
 // drains queued work; no caller memory or texture is used asynchronously.
 HRESULT hr=flush_native();if(FAILED(hr))return hr;
 if(has_pending_ui()&&FAILED(hr=flush_ui()))return hr;
 return draw_fixed_function(type,vertices,indices,index_count,st,nullptr,
  SemanticKind::simple_geometry,0,1,{},texture,false,true);
}
#endif

HRESULT Renderer::draw_fixed_function(D3DPRIMITIVETYPE type,std::vector<FixedFunctionVertex>& v,
 const WORD* indices,DWORD index_count,const FixedState& st,SurfaceImage* texture,
 SemanticKind semantic_kind,std::uint32_t caller_rva,std::uint32_t legacy_draw_count,
 const widescreen::HudComposition& hud_composition,ID3D11ShaderResourceView* owned_texture,
 bool tl,bool spec){
 const DWORD count=static_cast<DWORD>(v.size());
 CampClipScope clip_scope{*this};
 PerfScope perf(&perf_draw_ticks_);if(perf_enabled())++perf_draw_calls_;
 if(semantic_kind==SemanticKind::unknown)semantic_kind=current_semantic_context().kind;if(semantic_kind==SemanticKind::ui){const HRESULT hr=composite_world();if(FAILED(hr))return hr;}if(!scene_composited_)scene_updated_=true;remember_debug_projection(st,semantic_kind);char callerPass[64]{};const char* semanticPass="legacy_unclassified";if(semantic_kind==SemanticKind::figure)semanticPass="legacy_figures";else if(semantic_kind==SemanticKind::simple_geometry)semanticPass="legacy_simple_geometry";else if(semantic_kind==SemanticKind::ui)semanticPass="ui_composition";else if(profiler_.capture_active()&&caller_rva){std::snprintf(callerPass,sizeof(callerPass),"legacy_rva_%08X",caller_rva);semanticPass=callerPass;}transition_profile_pass(semanticPass);
 const std::uint64_t primitiveCount=type==D3DPT_TRIANGLELIST?(indices&&index_count?index_count:count)/3:type==D3DPT_TRIANGLESTRIP?(indices&&index_count?index_count:count)>2?(indices&&index_count?index_count:count)-2:0:0;profiler_.add_draw(primitiveCount,legacy_draw_count);profiler_.add_batch();
 if(tl&&widescreen::composition_changed(hud_composition)){
  const float px=st.viewport.dwX+hud_composition.pivot_x*st.viewport.dwWidth/canvas_width();
  const float py=st.viewport.dwY+hud_composition.pivot_y*st.viewport.dwHeight/canvas_height();
  auto h=hud_composition;h.pivot_x=px;h.pivot_y=py;
  h.offset_x*=st.viewport.dwWidth/float(canvas_width());h.offset_y*=st.viewport.dwHeight/float(canvas_height());
  h.rotation_x=st.viewport.dwX+h.rotation_x*st.viewport.dwWidth/float(canvas_width());
  h.rotation_y=st.viewport.dwY+h.rotation_y*st.viewport.dwHeight/float(canvas_height());
  for(auto& vertex:v)widescreen::compose_point(vertex.x,vertex.y,h);
 }
 if(tl&&camp::composing&&camp::primary_output)for(auto& vertex:v){const float x=vertex.x,y=vertex.y;vertex.x=camp::visual.a*x+camp::visual.b*y+camp::visual.x;vertex.y=camp::visual.c*x+camp::visual.d*y+camp::visual.y;}
 const UINT vertexBytes=(UINT)(v.size()*sizeof(Vertex));
 if(!dynamic_vertices_||vertexBytes>dynamic_vertex_capacity_){dynamic_vertex_capacity_=std::max<UINT>(4u*1024u*1024u,vertexBytes);D3D11_BUFFER_DESC vd{};vd.ByteWidth=dynamic_vertex_capacity_;vd.Usage=D3D11_USAGE_DYNAMIC;vd.BindFlags=D3D11_BIND_VERTEX_BUFFER;vd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;dynamic_vertices_.Reset();HRESULT createHr=device_->CreateBuffer(&vd,nullptr,&dynamic_vertices_);if(FAILED(createHr))return createHr;dynamic_vertex_offset_=0;discard_vertices_=true;}
 if(dynamic_vertex_offset_+vertexBytes>dynamic_vertex_capacity_){dynamic_vertex_offset_=0;discard_vertices_=true;}
 const UINT vertexOffset=dynamic_vertex_offset_;D3D11_MAPPED_SUBRESOURCE vertexMap{};HRESULT hr=context_->Map(dynamic_vertices_.Get(),0,discard_vertices_?D3D11_MAP_WRITE_DISCARD:D3D11_MAP_WRITE_NO_OVERWRITE,0,&vertexMap);if(FAILED(hr))return hr;std::memcpy(static_cast<std::uint8_t*>(vertexMap.pData)+vertexOffset,v.data(),vertexBytes);context_->Unmap(dynamic_vertices_.Get(),0);dynamic_vertex_offset_=(vertexOffset+vertexBytes+15u)&~15u;discard_vertices_=false;
 UINT indexOffset=0;if(indices&&index_count){const UINT indexBytes=index_count*2;if(!dynamic_indices_||indexBytes>dynamic_index_capacity_){dynamic_index_capacity_=std::max<UINT>(1024u*1024u,indexBytes);D3D11_BUFFER_DESC id{};id.ByteWidth=dynamic_index_capacity_;id.Usage=D3D11_USAGE_DYNAMIC;id.BindFlags=D3D11_BIND_INDEX_BUFFER;id.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;dynamic_indices_.Reset();if(FAILED(hr=device_->CreateBuffer(&id,nullptr,&dynamic_indices_)))return hr;dynamic_index_offset_=0;discard_indices_=true;}if(dynamic_index_offset_+indexBytes>dynamic_index_capacity_){dynamic_index_offset_=0;discard_indices_=true;}indexOffset=dynamic_index_offset_;D3D11_MAPPED_SUBRESOURCE indexMap{};if(FAILED(hr=context_->Map(dynamic_indices_.Get(),0,discard_indices_?D3D11_MAP_WRITE_DISCARD:D3D11_MAP_WRITE_NO_OVERWRITE,0,&indexMap)))return hr;std::memcpy(static_cast<std::uint8_t*>(indexMap.pData)+indexOffset,indices,indexBytes);context_->Unmap(dynamic_indices_.Get(),0);dynamic_index_offset_=(indexOffset+indexBytes+3u)&~3u;discard_indices_=false;}
 Constants c{};c.hud_opacity=hud_composition.opacity;c.combiner_padding=hit_test_active_?1u:0u;c.world=st.transform[D3DTRANSFORMSTATE_WORLD];c.view=st.transform[D3DTRANSFORMSTATE_VIEW];c.projection=st.transform[D3DTRANSFORMSTATE_PROJECTION];if(!tl&&camp::composing&&camp::primary_output)camp::compose_clip(&c.projection._11,camp::visual,float(canvas_width()),float(canvas_height()),true);c.viewport[0]=(float)st.viewport.dwX;c.viewport[1]=(float)st.viewport.dwY;c.viewport[2]=(float)std::max<DWORD>(1,st.viewport.dwWidth);c.viewport[3]=(float)std::max<DWORD>(1,st.viewport.dwHeight);
 // The upscaler jitter (jitter_clip): untransformed vertices through the
 // projection (row vectors: columns x and y take dx, dy times column w),
 // pre-transformed ones through their pixel-to-NDC mapping.
 {float dx=0,dy=0;if(upscaler_jitter_ndc(dx,dy)){
  if(tl){c.viewport[0]-=dx*c.viewport[2]*.5f;c.viewport[1]+=dy*c.viewport[3]*.5f;}
  else{float* p=&c.projection._11;for(int row=0;row<4;++row){p[row*4]+=dx*p[row*4+3];p[row*4+1]+=dy*p[row*4+3];}}}}
 DWORD fog=st.render[D3DRENDERSTATE_FOGCOLOR],tf=st.render[D3DRENDERSTATE_TEXTUREFACTOR];c.fog_color[0]=((fog>>16)&255)/255.f;c.fog_color[1]=((fog>>8)&255)/255.f;c.fog_color[2]=(fog&255)/255.f;c.fog_color[3]=1;c.texture_factor[0]=((tf>>16)&255)/255.f;c.texture_factor[1]=((tf>>8)&255)/255.f;c.texture_factor[2]=(tf&255)/255.f;c.texture_factor[3]=((tf>>24)&255)/255.f;c.transformed=tl;c.textured=texture!=nullptr || owned_texture!=nullptr;c.color_op=st.stage[0][D3DTSS_COLOROP];c.alpha_op=st.stage[0][D3DTSS_ALPHAOP];c.color_arg1=st.stage[0][D3DTSS_COLORARG1];c.color_arg2=st.stage[0][D3DTSS_COLORARG2];c.alpha_arg1=st.stage[0][D3DTSS_ALPHAARG1];c.alpha_arg2=st.stage[0][D3DTSS_ALPHAARG2];c.alpha_test=st.render[D3DRENDERSTATE_ALPHATESTENABLE]!=0;c.alpha_func=st.render[D3DRENDERSTATE_ALPHAFUNC];c.alpha_ref=(st.render[D3DRENDERSTATE_ALPHAREF]&255)/256.f;c.fog_enable=st.render[D3DRENDERSTATE_FOGENABLE]!=0;c.specular_enable=spec&&st.render[D3DRENDERSTATE_SPECULARENABLE]!=0;
 if(!tl)widescreen::compose_hud_clip(&c.projection._11,hud_composition,float(canvas_width()),float(canvas_height()),true);
 if(FAILED(hr=upload_draw_constants(&c,sizeof(c))))return hr;const camp::Clip clip=camp::composing&&camp::primary_output?camp::visual.clip:camp::Clip{};if(FAILED(hr=apply_state(st,texture!=nullptr || owned_texture!=nullptr,hud_composition,clip)))return hr;if(texture){texture->world_mip_use=!scene_composited_;if(scene_composited_)texture->static_mip_hint=false;if(FAILED(hr=upload(*texture)))return hr;context_->PSSetShaderResources(0,1,texture->srv.GetAddressOf());}else{context_->PSSetShaderResources(0,1,&owned_texture);}
 UINT sz=sizeof(Vertex),off=vertexOffset;ID3D11Buffer* vertexBuffer=dynamic_vertices_.Get();context_->IASetInputLayout(layout_.Get());context_->IASetVertexBuffers(0,1,&vertexBuffer,&sz,&off);D3D11_PRIMITIVE_TOPOLOGY top=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;if(type==D3DPT_TRIANGLESTRIP)top=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;else if(type==D3DPT_LINELIST)top=D3D11_PRIMITIVE_TOPOLOGY_LINELIST;else if(type==D3DPT_LINESTRIP)top=D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;else if(type==D3DPT_POINTLIST)top=D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;context_->IASetPrimitiveTopology(top);context_->VSSetShader(vs_.Get(),nullptr,0);context_->PSSetShader(ps_.Get(),nullptr,0);D3D11_VIEWPORT vp=target_viewport();context_->RSSetViewports(1,&vp);context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),dsv_.Get());if(indices&&index_count){context_->IASetIndexBuffer(dynamic_indices_.Get(),DXGI_FORMAT_R16_UINT,indexOffset);context_->DrawIndexed(index_count,0,0);}else context_->Draw(count,0);if(texture)sampled_surface_pixels(*texture);return S_OK;
}

// Game particles: the stock draw order is computed on the CPU from the
// completed points (one dot-product pair each); expansion to quads, clipping,
// texturing and fog run on the GPU. Failure omits the call's particles.
// The pipeline objects are made on the first particle call or by the load
// hold's pipeline warm-up (renderer_warmup.cpp), whichever comes first.
HRESULT Renderer::ensure_game_particle_pipeline(){
 if(d3d12_)return S_OK; // D3D12 backend: the executor builds the same programs' pipelines
 if(game_particle_vs_&&game_particle_ps_&&game_particle_constants_&&game_ribbon_vs_&&game_selection_vs_&&game_mesh_vs_&&game_glyph_vs_)return S_OK;
 if(game_particle_pipeline_failed_||feature_level_<D3D_FEATURE_LEVEL_11_0)return E_FAIL;
 game_particle_pipeline_failed_=true;
 ComPtr<ID3DBlob> vertex,pixel,ribbon,selection,mesh,glyph,error;
 if(FAILED(compile_shader(world_support_shader_program(WorldSupportShader::game_particle_vs),&vertex,&error))||FAILED(compile_shader(world_support_shader_program(WorldSupportShader::game_particle_ps),&pixel,&error))||
    FAILED(compile_shader(world_support_shader_program(WorldSupportShader::game_ribbon_vs),&ribbon,&error))||FAILED(compile_shader(world_support_shader_program(WorldSupportShader::game_selection_vs),&selection,&error))||
    FAILED(compile_shader(world_support_shader_program(WorldSupportShader::game_mesh_vs),&mesh,&error))||FAILED(compile_shader(world_support_shader_program(WorldSupportShader::game_glyph_vs),&glyph,&error))){
  if(error)OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
  bridge_report_event("game_particles_pipeline","shader_compile_failed");return E_FAIL;}
 D3D11_BUFFER_DESC constants{};constants.ByteWidth=sizeof(GameParticleConstants);constants.Usage=D3D11_USAGE_DYNAMIC;
 constants.BindFlags=D3D11_BIND_CONSTANT_BUFFER;constants.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
 HRESULT hr=create_shader(device_.Get(),vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,&game_particle_vs_);
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,&game_particle_ps_);
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),ribbon->GetBufferPointer(),ribbon->GetBufferSize(),nullptr,&game_ribbon_vs_);
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),selection->GetBufferPointer(),selection->GetBufferSize(),nullptr,&game_selection_vs_);
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),mesh->GetBufferPointer(),mesh->GetBufferSize(),nullptr,&game_mesh_vs_);
 if(SUCCEEDED(hr))hr=create_shader(device_.Get(),glyph->GetBufferPointer(),glyph->GetBufferSize(),nullptr,&game_glyph_vs_);
 if(SUCCEEDED(hr))hr=device_->CreateBuffer(&constants,nullptr,&game_particle_constants_);
 if(FAILED(hr)){game_particle_vs_.Reset();game_particle_ps_.Reset();game_ribbon_vs_.Reset();game_selection_vs_.Reset();game_mesh_vs_.Reset();game_glyph_vs_.Reset();game_particle_constants_.Reset();
  bridge_report_event("game_particles_pipeline","resource_creation_failed");return hr;}
 game_particle_pipeline_failed_=false;
 return S_OK;
}

HRESULT Renderer::draw_game_particles(const GameParticleView& view,const FixedState& st,SurfaceImage* texture){
 PERF06_SCOPE("particle_submit");
 if(scene_queue()){
  if(!view.particles||!view.count)return E_INVALIDARG;
  scene_cmd::GameParticles command;
  try{command.particles.assign(view.particles,view.particles+view.count);}catch(...){return E_OUTOFMEMORY;}
  command.view=view;command.view.particles=nullptr;command.view.count=0;command.state=st;
  command.texture=scene_surface(texture);
  scene_native_pending_=true;
  scene_post_command(std::move(command));
  return S_OK;
 }
 PerfScope perf(&perf_draw_ticks_);if(perf_enabled())++perf_draw_calls_;
 if(!device_||!context_||!view.particles||!view.count)return E_INVALIDARG;
 if(view.interface_pass){const HRESULT hr=composite_world();if(FAILED(hr))return hr;}
 if(HRESULT created=ensure_game_particle_pipeline();FAILED(created))return created;
 try{
  if(game_particle_order_.capacity()/4>(std::max)(view.count,kGameParticleBatch))
   std::vector<std::uint32_t>().swap(game_particle_order_);
  game_particle_order_.clear();
  game_particle_draw_order(view.object_to_clip.data(),view.particles,view.count,game_particle_order_);
 }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 const UINT count=static_cast<UINT>(game_particle_order_.size());
 if(!count)return S_OK;  // every particle was behind the near plane
 try{game_effect_elements_.resize(count);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 for(UINT i=0;i<count;++i){
  const GameParticle& p=view.particles[game_particle_order_[i]];GameParticleGpu& g=game_effect_elements_[i];
  g.center_radius[0]=p.x;g.center_radius[1]=p.y;g.center_radius[2]=p.z;g.center_radius[3]=p.radius;
  g.uv[0]=p.u0;g.uv[1]=p.v0;g.uv[2]=p.u1;g.uv[3]=p.v1;g.color=p.color;g.padding[0]=g.padding[1]=g.padding[2]=0;
 }
 GameEffectDraw draw{};draw.object_to_clip=view.object_to_clip.data();
 draw.clip_half_width=view.clip_half_width;draw.clip_half_height=view.clip_half_height;
 draw.element_count=count;draw.vertex_count=count*6u;draw.vertex_shader=game_particle_vs_.Get();draw.vertex_program=WorldSupportShader::game_particle_vs;
 draw.fog=true;draw.reflection_depth=true;draw.profile_pass="owned_game_particles";
 return draw_game_effect(draw,st,texture);
}

void Renderer::game_effect_constants(const GameEffectDraw& draw,const FixedState& st,bool textured,GameParticleConstants& c) noexcept{
 c={};
 std::memcpy(c.object_to_clip,draw.object_to_clip,sizeof(c.object_to_clip));
 jitter_clip(c.object_to_clip);
 c.clip_half[0]=draw.clip_half_width;c.clip_half[1]=draw.clip_half_height;c.clip_half[2]=widescreen::interface_owned?widescreen::make_layout((float)output_width_,(float)output_height_).clip_x:1.0f;
 c.viewport[0]=(float)st.viewport.dwX;c.viewport[1]=(float)st.viewport.dwY;
 c.viewport[2]=(float)std::max<DWORD>(1,st.viewport.dwWidth);c.viewport[3]=(float)std::max<DWORD>(1,st.viewport.dwHeight);
 const DWORD fog=st.render[D3DRENDERSTATE_FOGCOLOR],tf=st.render[D3DRENDERSTATE_TEXTUREFACTOR];
 c.fog_color[0]=((fog>>16)&255)/255.f;c.fog_color[1]=((fog>>8)&255)/255.f;c.fog_color[2]=(fog&255)/255.f;c.fog_color[3]=1;
 c.texture_factor[0]=((tf>>16)&255)/255.f;c.texture_factor[1]=((tf>>8)&255)/255.f;c.texture_factor[2]=(tf&255)/255.f;c.texture_factor[3]=((tf>>24)&255)/255.f;
 c.color_op=st.stage[0][D3DTSS_COLOROP];c.alpha_op=st.stage[0][D3DTSS_ALPHAOP];
 c.color_arg1=st.stage[0][D3DTSS_COLORARG1];c.color_arg2=st.stage[0][D3DTSS_COLORARG2];
 c.alpha_arg1=st.stage[0][D3DTSS_ALPHAARG1];c.alpha_arg2=st.stage[0][D3DTSS_ALPHAARG2];
 c.alpha_test=st.render[D3DRENDERSTATE_ALPHATESTENABLE]!=0;c.alpha_func=st.render[D3DRENDERSTATE_ALPHAFUNC];
 c.alpha_ref=(st.render[D3DRENDERSTATE_ALPHAREF]&255)/256.f;c.textured=textured;c.first=0;c.count=draw.ribbon_points;
 // World fog as on world surfaces, only for world particles with fog on.
 // Additive light is attenuated; modulating blends have no neutral to fog.
 const DWORD source=st.render[D3DRENDERSTATE_SRCBLEND],destination=st.render[D3DRENDERSTATE_DESTBLEND];
 const bool blended=st.render[D3DRENDERSTATE_ALPHABLENDENABLE]!=0;
 c.fog_mode=!draw.fog||scene_composited_||st.render[D3DRENDERSTATE_FOGENABLE]==0?0u:
  blended&&destination==D3DBLEND_ONE?2u:
  blended&&(source==D3DBLEND_ZERO||source==D3DBLEND_DESTCOLOR)?0u:1u;
 // Reflection depth coverage; modulating blends (darkening) stay out (3).
 const bool modulating=blended&&(source==D3DBLEND_ZERO||source==D3DBLEND_DESTCOLOR);
 c.coverage_mode=modulating?3u:!blended||destination!=D3DBLEND_ONE?0u:source==D3DBLEND_SRCALPHA?1u:2u;
 c.padding[0]=draw.ribbon_antialias?1u:0u;
}

// Shared tail of the game effect draws (particles, lightning ribbons): uploads
// game_effect_elements_, translates the call's fixed-function state and draws
// at the call's stream position.
HRESULT Renderer::draw_game_effect(const GameEffectDraw& draw,const FixedState& st,SurfaceImage* texture){
 const UINT count=draw.element_count;
 if(!count||game_effect_elements_.size()<count)return E_INVALIDARG;
 if(d3d12_)return draw_game_effect_d3d12(draw,st,texture);
 HRESULT hr=S_OK;
 if(!game_particle_buffer_||count>game_particle_capacity_){
  // Sized by demand; one discard-mapped buffer per call keeps earlier draws intact.
  const UINT capacity=grow_capacity(count,1024u,UINT_MAX/sizeof(GameParticleGpu));
  D3D11_BUFFER_DESC desc{};desc.ByteWidth=capacity*sizeof(GameParticleGpu);desc.Usage=D3D11_USAGE_DYNAMIC;
  desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
  desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=sizeof(GameParticleGpu);
  ComPtr<ID3D11Buffer> buffer;ComPtr<ID3D11ShaderResourceView> srv;
  if(FAILED(hr=device_->CreateBuffer(&desc,nullptr,&buffer)))return hr;
  D3D11_SHADER_RESOURCE_VIEW_DESC view_desc{};view_desc.Format=DXGI_FORMAT_UNKNOWN;
  view_desc.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;view_desc.Buffer.NumElements=capacity;
  if(FAILED(hr=device_->CreateShaderResourceView(buffer.Get(),&view_desc,&srv)))return hr;
  game_particle_buffer_=std::move(buffer);game_particle_srv_=std::move(srv);game_particle_capacity_=capacity;
 }
 if(!scene_composited_)scene_updated_=true;
 transition_profile_pass(draw.profile_pass);
 D3D11_MAPPED_SUBRESOURCE mapped{};
 if(FAILED(hr=context_->Map(game_particle_buffer_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return hr;
 const std::size_t bytes=static_cast<std::size_t>(count)*sizeof(GameParticleGpu);
 std::memcpy(mapped.pData,game_effect_elements_.data(),bytes);
 context_->Unmap(game_particle_buffer_.Get(),0);
 profiler_.add_upload(bytes);
 GameParticleConstants c{};game_effect_constants(draw,st,texture!=nullptr,c);
 const DWORD source=st.render[D3DRENDERSTATE_SRCBLEND],destination=st.render[D3DRENDERSTATE_DESTBLEND];
 const bool blended=st.render[D3DRENDERSTATE_ALPHABLENDENABLE]!=0;
 if(FAILED(hr=context_->Map(game_particle_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return hr;
 std::memcpy(mapped.pData,&c,sizeof(c));context_->Unmap(game_particle_constants_.Get(),0);
 if(c.fog_mode&&FAILED(hr=bind_world_fog()))return hr;
 apply_state(st,texture!=nullptr);
 ID3D11ShaderResourceView* texture_view=nullptr;
 if(texture){texture->world_mip_use=!scene_composited_;if(scene_composited_)texture->static_mip_hint=false;
  if(FAILED(hr=upload(*texture)))return hr;texture_view=texture->srv.Get();}
 context_->PSSetShaderResources(0,1,&texture_view);
 ID3D11ShaderResourceView* particles=game_particle_srv_.Get();context_->VSSetShaderResources(7,1,&particles);
 context_->IASetInputLayout(nullptr);context_->IASetVertexBuffers(0,0,nullptr,nullptr,nullptr);
 context_->IASetIndexBuffer(nullptr,DXGI_FORMAT_UNKNOWN,0);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 context_->VSSetShader(draw.vertex_shader,nullptr,0);context_->PSSetShader(game_particle_ps_.Get(),nullptr,0);
 context_->GSSetShader(nullptr,nullptr,0);context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);
 context_->VSSetConstantBuffers(0,1,game_particle_constants_.GetAddressOf());context_->PSSetConstantBuffers(0,1,game_particle_constants_.GetAddressOf());
 D3D11_VIEWPORT viewport=target_viewport();context_->RSSetViewports(1,&viewport);
 // Blended world particles also feed the bloom glow target (cleared on
 // first use each frame): additive ones are light (magic), alpha-blended
 // ones by their flame key (game_particles.h: torch and campfire flames are
 // alpha-blended), so fire and magic bloom though the scene colour stays
 // below the non-emissive ceiling. Modulating blends only darken.
 ID3D11RenderTargetView* targets[2]={rtv_.Get(),nullptr};
 if(!scene_composited_&&blended&&c.coverage_mode!=3u&&configured_integer(debug_overlay_.config(),"bloom",0)>0){
  if(!glow_||glow_width_!=width_||glow_height_!=height_){
   glow_.Reset();glow_rtv_.Reset();glow_srv_.Reset();glow_width_=width_;glow_height_=height_;
   D3D11_TEXTURE2D_DESC d{};d.Width=width_;d.Height=height_;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
   d.Format=kGlowFormat;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
   if(FAILED(device_->CreateTexture2D(&d,nullptr,&glow_))||FAILED(device_->CreateRenderTargetView(glow_.Get(),nullptr,&glow_rtv_))||
      FAILED(device_->CreateShaderResourceView(glow_.Get(),nullptr,&glow_srv_))){
    glow_.Reset();glow_rtv_.Reset();glow_srv_.Reset();bridge_report_event("bloom_glow","target_creation_failed");}
  }
  if(glow_rtv_){
   if(glow_frame_!=native_frame_index_){const float black[4]{};context_->ClearRenderTargetView(glow_rtv_.Get(),black);glow_frame_=native_frame_index_;}
   targets[1]=glow_rtv_.Get();
  }
 }
 context_->OMSetRenderTargets(targets[1]?2:1,targets,dsv_.Get());
 profiler_.add_draw(static_cast<std::uint64_t>(draw.vertex_count/3u),1);profiler_.add_batch();
 context_->Draw(draw.vertex_count,0);
 if(targets[1])context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),dsv_.Get());
 if(draw.reflection_depth&&c.coverage_mode!=3u&&!scene_composited_&&particle_depth_wanted())draw_game_particle_depth(draw.vertex_count);
 if(texture)sampled_surface_pixels(*texture);
 ID3D11ShaderResourceView* none=nullptr;context_->VSSetShaderResources(7,1,&none);
 return S_OK;
}

#ifdef EID3D11_EMBEDDED
HRESULT Renderer::draw_embedded_ribbon(const EmbeddedRibbonInput& input,
    const FixedState& state, SurfaceImage* texture) {
    if(input.clip_flags==2 || input.count<2)return S_OK;
    if(!input.points || !std::isfinite(input.width) || !std::isfinite(input.texture_length) ||
       !std::isfinite(input.v_start) || !std::isfinite(input.v_width))return E_INVALIDARG;
    GameRibbonView view{};
    view.points=input.points;view.count=input.count;
    view.object_to_clip=input.projection.object_to_clip;
    view.clip_half_width=input.projection.clip_half_width;
    view.clip_half_height=input.projection.clip_half_height;
    view.half_width_pixels_w=15.0f*input.width;
    game_ribbon_strip(input.texture_length,input.v_start,input.v_width,input.count,view.strip);
    view.interface_pass=input.interface_pass;
    auto additive=state;
    additive.render[D3DRENDERSTATE_SRCBLEND]=D3DBLEND_SRCALPHA;
    additive.render[D3DRENDERSTATE_DESTBLEND]=D3DBLEND_ONE;
    return draw_game_ribbon(view,additive,texture);
}

HRESULT EmbeddedPrintBatch::append(const EmbeddedPrintInput& input, const FixedState& state,
    std::shared_ptr<SurfaceImage> texture) noexcept try {
    if(!input.index_count)return S_OK;
    if(!input.groups || !input.maps || !input.indices || input.index_count%3 ||
       input.group_count>UINT_MAX/4u || vertices.size()>UINT_MAX-input.index_count)return E_INVALIDARG;
    for(std::uint32_t k=0;k<input.index_count;++k)
        if(input.indices[k]>=input.group_count*4u || input.indices[k]>=input.map_count)return E_INVALIDARG;
    const auto first=vertices.size();
    const auto needed=first+input.index_count;
    if(vertices.capacity()<needed)vertices.reserve((std::max)(needed,vertices.capacity()*2));
    if(runs.size()==runs.capacity())runs.reserve((std::max)(runs.size()+1,runs.capacity()*2));
    vertices.resize(first+input.index_count);
    for(std::uint32_t k=0;k<input.index_count;++k) {
        const auto i=input.indices[k];
        game_print_vertex_gpu(input.groups[i/4u].data(),i%4u,input.maps[i][0],input.maps[i][1],
            input.color,vertices[first+k]);
    }
    if(!runs.empty()) {
        auto& last=runs.back();
        if(last.texture==texture && last.projection.object_to_clip==input.projection.object_to_clip &&
           last.projection.clip_half_width==input.projection.clip_half_width &&
           last.projection.clip_half_height==input.projection.clip_half_height &&
           std::memcmp(&last.state,&state,sizeof(FixedState))==0) {
            last.count+=input.index_count;return S_OK;
        }
    }
    Run run{};run.texture=std::move(texture);run.state=state;run.projection=input.projection;
    run.first=static_cast<UINT>(first);run.count=input.index_count;
    runs.push_back(std::move(run));return S_OK;
} catch(...) { return E_OUTOFMEMORY; }

HRESULT Renderer::draw_embedded_prints(EmbeddedPrintBatch& batch) {
    HRESULT result=S_OK;
    for(const auto& run:batch.runs) {
        const auto& p=run.projection;
        const HRESULT hr=draw_game_print_run(batch.vertices.data()+run.first,run.count,p.object_to_clip,
            p.clip_half_width,p.clip_half_height,run.state,run.texture.get());
        if(FAILED(hr) && SUCCEEDED(result))result=hr;
    }
    batch.clear();return result;
}

HRESULT Renderer::draw_embedded_combat_text(const EmbeddedCombatTextInput& input,
    const FixedState& state, SurfaceImage* texture) {
    if(!input.enabled || !input.count)return S_OK;
    if(!input.events || !(input.screen_width>0) || !(input.screen_height>0))return E_INVALIDARG;
    const auto now=embedded_combat_text_clock_ms(input.game_time_segments);
    std::vector<GameParticleGpu> glyphs;
    try {
        for(std::uint32_t i=0;i<input.count;++i) {
            const auto& event=input.events[i];const auto& unit=event.unit;
            const auto age=static_cast<std::int32_t>(now-event.time_ms);
            CombatTextEntry entry{};
            entry.progress=input.addon_layout?static_cast<float>(age)/3000.0f:
                static_cast<float>(age)*kCombatTextProgressPerMs;
            if(entry.progress<0 || entry.progress>1 || !unit.present || !unit.visible ||
               unit.hidden || !unit.unit_visible || !unit.has_figure)continue;
            const auto& rect=unit.figure_visible?unit.figure_rect:unit.unit_rect;
            entry.anchor_x=(rect[0]+rect[2])/2;entry.anchor_y=(rect[1]+rect[3])/2;
            entry.amount=event.amount;entry.flags=event.flags;entry.owned_unit=unit.owned_unit;
            if(input.addon_layout)combat_text_layout_addon(entry,input.screen_width,input.screen_height,
                widescreen_combat_text_scale(),glyphs);
            else combat_text_layout(entry,input.screen_width,input.screen_height,
                widescreen_combat_text_scale(),glyphs);
        }
    } catch(...) { return E_OUTOFMEMORY; }
    if(glyphs.empty())return S_OK;
    GameGlyphView view{};view.glyphs=glyphs.data();view.count=static_cast<std::uint32_t>(glyphs.size());
    return draw_game_glyphs(view,state,texture);
}
#endif

HRESULT Renderer::draw_game_ribbon(const GameRibbonView& view,const FixedState& st,SurfaceImage* texture){
 if(scene_queue()){
  if(!view.points||view.count<2)return E_INVALIDARG;
  scene_cmd::GameRibbon command;
  try{command.points.assign(view.points,view.points+view.count);}catch(...){return E_OUTOFMEMORY;}
  command.view=view;command.view.points=nullptr;command.view.count=0;command.state=st;
  command.texture=scene_surface(texture);
  scene_native_pending_=true;
  scene_post_command(std::move(command));
  return S_OK;
 }
 PerfScope perf(&perf_draw_ticks_);if(perf_enabled())++perf_draw_calls_;
 if(!device_||!context_||!view.points||view.count<2)return E_INVALIDARG;
 if(view.interface_pass){const HRESULT hr=composite_world();if(FAILED(hr))return hr;}
 if(HRESULT created=ensure_game_particle_pipeline();FAILED(created))return created;
 try{game_effect_elements_.resize(view.count);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 for(std::uint32_t j=0;j<view.count;++j)
  game_ribbon_point_gpu(view.points,view.count,j,view.strip,view.half_width_pixels_w,game_effect_elements_[j]);
 GameEffectDraw draw{};draw.object_to_clip=view.object_to_clip.data();
 draw.clip_half_width=view.clip_half_width;draw.clip_half_height=view.clip_half_height;
 draw.element_count=view.count;draw.vertex_count=(view.count-1u)*6u;draw.ribbon_points=view.count;
 // Stock specular alpha 255: bolts are emissive and read through fog unfogged.
 draw.vertex_shader=game_ribbon_vs_.Get();draw.vertex_program=WorldSupportShader::game_ribbon_vs;draw.fog=false;draw.reflection_depth=true;draw.profile_pass="owned_game_lightning";
 return draw_game_effect(draw,st,texture);
}

HRESULT Renderer::draw_game_selection(const GameSelectionView& view,const FixedState& st,SurfaceImage* texture){
 if(scene_queue()){
  scene_cmd::GameSelection command;command.view=view;command.state=st;command.texture=scene_surface(texture);
  scene_native_pending_=true;
  scene_post_command(std::move(command));
  return S_OK;
 }
 return draw_game_selection_held(view,st,scene_surface(texture));
}

HRESULT Renderer::draw_game_selection_held(const GameSelectionView& view,const FixedState& st,std::shared_ptr<SurfaceImage> texture){
 if(!scene_composited_&&!drawing_ground_marks_){
  try{deferred_ground_marks_.push_back([this,view,st,texture]{return draw_game_selection_held(view,st,texture);});}
  catch(...){return E_OUTOFMEMORY;}
  return S_OK;
 }
 PerfScope perf(&perf_draw_ticks_);if(perf_enabled())++perf_draw_calls_;
 if(!device_||!context_)return E_INVALIDARG;
 if(HRESULT created=ensure_game_particle_pipeline();FAILED(created))return created;
 try{game_effect_elements_.resize(1);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 game_selection_gpu(view.marker,game_effect_elements_[0]);
 GameEffectDraw draw{};draw.object_to_clip=view.object_to_clip.data();
 draw.clip_half_width=view.clip_half_width;draw.clip_half_height=view.clip_half_height;
 draw.element_count=1;draw.vertex_count=18;draw.vertex_shader=game_selection_vs_.Get();draw.vertex_program=WorldSupportShader::game_selection_vs;
 // Stock specular alpha 255: the marker is never fogged; a ground indicator,
 // it stays out of water reflections.
 draw.fog=false;draw.reflection_depth=false;draw.profile_pass="owned_game_selection";
 return draw_game_effect(draw,st,texture.get());
}

HRESULT Renderer::draw_game_glyphs(const GameGlyphView& view,const FixedState& st,SurfaceImage* texture){
 if(!view.glyphs||!view.count)return E_INVALIDARG;
 if(scene_queue()){
  scene_cmd::GameGlyphs command;
  try{command.glyphs.assign(view.glyphs,view.glyphs+view.count);}catch(...){return E_OUTOFMEMORY;}
  command.state=st;command.texture=scene_surface(texture);
  scene_native_pending_=true;
  scene_post_command(std::move(command));
  return S_OK;
 }
 PerfScope perf(&perf_draw_ticks_);if(perf_enabled())++perf_draw_calls_;
 if(!device_||!context_)return E_INVALIDARG;
 if(HRESULT created=ensure_game_particle_pipeline();FAILED(created))return created;
 try{game_effect_elements_.assign(view.glyphs,view.glyphs+view.count);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 static const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
 GameEffectDraw draw{};draw.object_to_clip=identity;draw.clip_half_width=draw.clip_half_height=1;
 draw.element_count=view.count;draw.vertex_count=view.count*6u;draw.vertex_shader=game_glyph_vs_.Get();draw.vertex_program=WorldSupportShader::game_glyph_vs;
 // Stock: fog off around the call, specular alpha 255; screen text has no reflection.
 draw.fog=false;draw.reflection_depth=false;draw.profile_pass="owned_combat_text";
 return draw_game_effect(draw,st,texture);
}

HRESULT Renderer::draw_game_print_run(const GameParticleGpu* vertices,UINT count,const std::array<float,16>& object_to_clip,
 float clip_half_width,float clip_half_height,const FixedState& st,SurfaceImage* texture){
 if(!vertices||!count)return E_INVALIDARG;
 std::vector<GameParticleGpu> copy;
 try{copy.assign(vertices,vertices+count);}catch(...){return E_OUTOFMEMORY;}
 if(scene_queue()){
  scene_cmd::GamePrintRun command;command.vertices=std::move(copy);command.object_to_clip=object_to_clip;
  command.clip_half_width=clip_half_width;command.clip_half_height=clip_half_height;command.state=st;
  command.texture=scene_surface(texture);
  scene_native_pending_=true;
  scene_post_command(std::move(command));
  return S_OK;
 }
 return draw_game_print_run_held(std::move(copy),object_to_clip,clip_half_width,clip_half_height,st,scene_surface(texture));
}

HRESULT Renderer::draw_game_print_run_held(std::vector<GameParticleGpu> vertices,const std::array<float,16>& object_to_clip,
 float clip_half_width,float clip_half_height,const FixedState& st,std::shared_ptr<SurfaceImage> texture){
 if(!scene_composited_&&!drawing_ground_marks_){
  try{deferred_ground_marks_.push_back([this,vertices=std::move(vertices),object_to_clip,clip_half_width,clip_half_height,st,texture]{
   return draw_game_print_run_held(vertices,object_to_clip,clip_half_width,clip_half_height,st,texture);});}
  catch(...){return E_OUTOFMEMORY;}
  return S_OK;
 }
 PerfScope perf(&perf_draw_ticks_);if(perf_enabled())++perf_draw_calls_;
 if(!device_||!context_)return E_INVALIDARG;
 if(HRESULT created=ensure_game_particle_pipeline();FAILED(created))return created;
 try{game_effect_elements_=std::move(vertices);}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
 GameEffectDraw draw{};draw.object_to_clip=object_to_clip.data();
 draw.clip_half_width=clip_half_width;draw.clip_half_height=clip_half_height;
 const UINT count=static_cast<UINT>(game_effect_elements_.size());
 draw.element_count=count;draw.vertex_count=count;draw.vertex_shader=game_mesh_vs_.Get();draw.vertex_program=WorldSupportShader::game_mesh_vs;
 // Ground marks fog like the terrain under them; they stay out of reflections.
 draw.fog=true;draw.reflection_depth=false;draw.profile_pass="owned_game_prints";
 return draw_game_effect(draw,st,texture.get());
}

// ---- Typed effect and flush commands (scene_commands_world.h), run on the
// scene thread (or inline in order when a post could not be allocated).
void scene_cmd::GameParticles::run(Renderer& r){
 GameParticleView copy=view;copy.particles=particles.data();copy.count=static_cast<std::uint32_t>(particles.size());
 Renderer::report_scene_failure("draw_game_particles",r.draw_game_particles(copy,state,texture.get()));
}
void scene_cmd::GameRibbon::run(Renderer& r){
 GameRibbonView copy=view;copy.points=points.data();copy.count=static_cast<std::uint32_t>(points.size());
 Renderer::report_scene_failure("draw_game_ribbon",r.draw_game_ribbon(copy,state,texture.get()));
}
void scene_cmd::GameSelection::run(Renderer& r){
 Renderer::report_scene_failure("draw_game_selection",r.draw_game_selection_held(view,state,texture));
}
void scene_cmd::GameGlyphs::run(Renderer& r){
 GameGlyphView copy{};copy.glyphs=glyphs.data();copy.count=static_cast<std::uint32_t>(glyphs.size());
 Renderer::report_scene_failure("draw_game_glyphs",r.draw_game_glyphs(copy,state,texture.get()));
}
void scene_cmd::GamePrintRun::run(Renderer& r){
 Renderer::report_scene_failure("draw_game_print_run",r.draw_game_print_run_held(std::move(vertices),object_to_clip,
  clip_half_width,clip_half_height,state,texture));
}
void scene_cmd::FlushNative::run(Renderer& r){Renderer::report_scene_failure("flush_native",r.flush_native(layout_slot));}

// Constant ring (constant_ring.h, issue #162). context1_ is the immediate
// context as ID3D11DeviceContext1; with render_thread on, the scene thread
// owns it (and the ring) exactly as the game thread does when off.
void Renderer::refresh_constant_ring_context() noexcept{
 context1_.Reset();
 if(context_)(void)context_.As(&context1_);
}

// Every frame start: the next write of each upload ring discards, and
// nothing written before is overwritten.
void Renderer::restart_upload_rings() noexcept{
 dynamic_vertex_offset_=dynamic_index_offset_=0;discard_vertices_=discard_indices_=true;
 constant_ring_.restart();
 native_instance_update_ring_.end_frame(device_.Get(),context_.Get());
 native_pose_input_ring_.end_frame(device_.Get(),context_.Get());
}

// Writes one constant block and binds it at slot for the given stages: a
// ring range with D3D11.1 offsets, or (without them) own_buffer discarded.
HRESULT Renderer::push_constants(UINT stages,UINT slot,const void* data,UINT bytes,ID3D11Buffer* own_buffer) noexcept{
 if(constant_ring_supported_&&context1_){
  owned::ConstantRing::Range range{};
  const HRESULT hr=constant_ring_.write(device_.Get(),context_.Get(),data,bytes,range);if(FAILED(hr))return hr;
  if(stages&constant_vs)context1_->VSSetConstantBuffers1(slot,1,&range.buffer,&range.first,&range.count);
  if(stages&constant_ps)context1_->PSSetConstantBuffers1(slot,1,&range.buffer,&range.first,&range.count);
  return S_OK;
 }
 if(!context_||!own_buffer)return E_FAIL;
 D3D11_MAPPED_SUBRESOURCE mapped{};const HRESULT hr=context_->Map(own_buffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped);if(FAILED(hr))return hr;
 std::memcpy(mapped.pData,data,bytes);context_->Unmap(own_buffer,0);
 if(stages&constant_vs)context_->VSSetConstantBuffers(slot,1,&own_buffer);
 if(stages&constant_ps)context_->PSSetConstantBuffers(slot,1,&own_buffer);
 return S_OK;
}

// The b0 draw constants (Constants) of the fixed-function, native and shadow
// depth passes, bound for VS and PS.
HRESULT Renderer::upload_draw_constants(const void* data,UINT bytes) noexcept{
 if(bytes>draw_constants_copy_.size())return E_INVALIDARG;
 if(data!=draw_constants_copy_.data())std::memcpy(draw_constants_copy_.data(),data,bytes);
 draw_constants_bytes_=bytes;
 if(constant_ring_supported_&&context1_){
  const HRESULT hr=constant_ring_.write(device_.Get(),context_.Get(),data,bytes,draw_constants_range_);if(FAILED(hr))return hr;
  draw_constants_generation_=constant_ring_.generation();
  context1_->VSSetConstantBuffers1(0,1,&draw_constants_range_.buffer,&draw_constants_range_.first,&draw_constants_range_.count);
  context1_->PSSetConstantBuffers1(0,1,&draw_constants_range_.buffer,&draw_constants_range_.first,&draw_constants_range_.count);
  return S_OK;
 }
 return push_constants(constant_vs|constant_ps,0,data,bytes,constants_.Get());
}

// A pass start that binds b0 without writing: the last draw constants, as the
// single constants_ buffer kept them before the ring (written again into the
// ring when a restart, wrap or regrowth made the bound range stale).
void Renderer::bind_draw_constants() noexcept{
 if(constant_ring_supported_&&context1_&&draw_constants_bytes_){
  if(!draw_constants_live()){(void)upload_draw_constants(draw_constants_copy_.data(),draw_constants_bytes_);return;}
  context1_->VSSetConstantBuffers1(0,1,&draw_constants_range_.buffer,&draw_constants_range_.first,&draw_constants_range_.count);
  context1_->PSSetConstantBuffers1(0,1,&draw_constants_range_.buffer,&draw_constants_range_.first,&draw_constants_range_.count);
  return;
 }
 ID3D11Buffer* own=constants_.Get();context_->VSSetConstantBuffers(0,1,&own);context_->PSSetConstantBuffers(0,1,&own);
}

// A native batch's b0 constants and its contact views (t13-t16), for both
// executors (the D3D11 flush and flush_native_d3d12).
void Renderer::native_batch_constants(const NativeBatch& batch,const NativeQueuedDraw& draw,const NativeStateBucket& bucket,
 ID3D11ShaderResourceView* (&contact_views)[4],bool neighbour_terrain_blend,bool neighbour_fog_skip,bool shadow_coverage,void* output){
   const bool active_contact=!scene_composited_&&gc_drawn_frame_==native_frame_index_&&gc_map_generation_==ambient_map_generation_;
   const bool active_art=!scene_composited_&&((contact_maps_.tiles&&contact_maps_.atlas)||(contact_maps_.tiles12&&contact_maps_.atlas12))&&
    contact_map_generation_==ambient_map_generation_&&contact_constants_generation_==contact_map_generation_;
   ID3D11ShaderResourceView* views[4]{active_contact?ambient_ground_srv_.Get():nullptr,
    active_contact?gc_color_srv_.Get():nullptr,active_art?contact_maps_.tiles.Get():nullptr,active_art?contact_maps_.atlas.Get():nullptr};
   std::array<float,4> contact_origin{0,0,0,1};
   std::array<float,2> layout_origin=terrain_layout_origin_;
   std::uint64_t contact_ready=active_art?contact_maps_.ready:0;
   if(bucket.material_key.contact_map){
    std::fill(std::begin(views),std::end(views),nullptr);contact_ready=0;layout_origin={0,0};
    for(const auto& n:gc_neighbours_)if(n.contact_map==bucket.material_key.contact_map){
     if(n.prepared){views[0]=n.ground.Get();views[1]=n.color.Get();}
     views[2]=n.contact.tiles.Get();views[3]=n.contact.atlas.Get();
     contact_origin={n.origin[0],n.origin[1],n.origin[2],0};layout_origin=n.layout_origin;contact_ready=n.contact.ready;break;
    }
    // Temporary cost switch: neighbour terrain without its grid and atlas.
    if(bucket.semantic_kind==SemanticKind::terrain&&!neighbour_terrain_blend){views[2]=views[3]=nullptr;contact_ready=0;}
   }
   Constants constants{};constants.hud_opacity=draw.hud_composition.opacity;identity(constants.world);identity(constants.view);identity(constants.projection);const auto& st=bucket.state;constants.viewport[0]=(float)st.viewport.dwX;constants.viewport[1]=(float)st.viewport.dwY;constants.viewport[2]=(float)std::max<DWORD>(1,st.viewport.dwWidth);constants.viewport[3]=(float)std::max<DWORD>(1,st.viewport.dwHeight);const auto& fog_state=native_static_frame_?*native_static_frame_:st;DWORD fog=fog_state.render[D3DRENDERSTATE_FOGCOLOR],tf=st.render[D3DRENDERSTATE_TEXTUREFACTOR];constants.fog_color[0]=((fog>>16)&255)/255.f;constants.fog_color[1]=((fog>>8)&255)/255.f;constants.fog_color[2]=(fog&255)/255.f;constants.fog_color[3]=1;constants.texture_factor[0]=((tf>>16)&255)/255.f;constants.texture_factor[1]=((tf>>8)&255)/255.f;constants.texture_factor[2]=(tf&255)/255.f;constants.texture_factor[3]=((tf>>24)&255)/255.f;constants.transformed=0;constants.textured=bucket.texture_srv||bucket.texture_d3d12?1u:0u;constants.color_op=st.stage[0][D3DTSS_COLOROP];constants.alpha_op=st.stage[0][D3DTSS_ALPHAOP];constants.color_arg1=st.stage[0][D3DTSS_COLORARG1];constants.color_arg2=st.stage[0][D3DTSS_COLORARG2];constants.alpha_arg1=st.stage[0][D3DTSS_ALPHAARG1];constants.alpha_arg2=st.stage[0][D3DTSS_ALPHAARG2];constants.alpha_test=st.render[D3DRENDERSTATE_ALPHATESTENABLE]!=0;constants.alpha_func=st.render[D3DRENDERSTATE_ALPHAFUNC];constants.alpha_ref=(st.render[D3DRENDERSTATE_ALPHAREF]&255)/256.f;constants.fog_enable=!scene_composited_&&fog_state.render[D3DRENDERSTATE_FOGENABLE]!=0;constants.specular_enable=native_specular_addition_enabled(st.render[D3DRENDERSTATE_SPECULARENABLE]);constants.native_triangle_base=batch.output_triangle_base;constants.padding[0]=shadow_coverage?1u:0u;constants.padding[1]=bucket.opacity!=NativeOpacityClass::ordered?1u:0u;constants.liquid_layer=0;constants.combiner_padding=hit_test_active_?1u:0u;constants.combiner[0]=native_fast_combiner_mode(constants.color_op,constants.color_arg1,constants.color_arg2);constants.combiner[1]=native_fast_combiner_mode(constants.alpha_op,constants.alpha_arg1,constants.alpha_arg2);
   std::copy(contact_origin.begin(),contact_origin.end(),constants.contact_origin);
   constants.layout_origin[0]=layout_origin[0];constants.layout_origin[1]=layout_origin[1];
   // Terrain 2.0: the batch map's table entry + 1 (update_terrain2_table; 0 none).
   if(bucket.semantic_kind==SemanticKind::terrain&&!scene_composited_)
    for(unsigned k=0;k<terrain2_table_count_;++k)if(terrain2_table_[k]==bucket.material_key.contact_map){constants.layout_origin[3]=float(k+1);break;}
   constants.contact_padding=neighbour_fog_skip?0u:1u; // temporary A/B switch (nativeContactTail)
   // Material classes: bit 1 of nativeCombinerTail while the profile is bound at t37.
   if(material_profile_bound(bucket.material_key.texture_backing))constants.combiner_padding|=2u;
   // Bit 2: an interface flush (after the world composite): its records sit in their
   // own space, so the pixel stage takes no world camera, weather or receiver terms.
   if(scene_composited_)constants.combiner_padding|=4u;
   constants.contact_materials[0]=static_cast<UINT>(contact_ready);constants.contact_materials[1]=static_cast<UINT>(contact_ready>>32);
  std::copy(std::begin(views),std::end(views),contact_views);
  *static_cast<Constants*>(output)=constants;
}

HRESULT Renderer::flush_native(unsigned layout_slot){
 PERF06_SCOPE("batch_flush");
#ifdef EID3D11_EMBEDDED
 native_flush_failure_line_=0;
#endif

 if(scene_queue()){scene_native_pending_=false;scene_cmd::FlushNative command;command.layout_slot=layout_slot;scene_post_command(command);return S_OK;}
 // The invariant check: an interface record (owned HUD mesh, preview) drawn
 // in a world flush takes the world's fog, atmosphere and targets (black at
 // night). Named once per session with that flush's count.
 if(!scene_composited_&&!hit_test_active_&&!native_shadow_collecting_){
  std::size_t interface_records=0;
  for(const auto& selected:native_pending_draws_)interface_records+=selected.interface_record?1u:0u;
  if(interface_records){
   static bool reported=false;
   if(!reported){reported=true;char detail[96];std::snprintf(detail,sizeof(detail),"%zu of %zu records",interface_records,native_pending_draws_.size());
    bridge_append_probe_event("interface_record_in_world_flush",detail);}
   if(d3d12_)d3d12_->count("interface records drawn in a world flush (invariant violated)");
  }
 }
 // Record rejected passes too, before the shadow/empty-queue short paths.
 // A cached static bake is real native work even though its queue is empty.
 // No capture allocation or GPU work happens here, and idle requests return first.
 if(!native_pending_draws_.empty()||native_static_reuse_)(void)native_lod_colour_pass();
 if(d3d12_)return flush_native_d3d12(layout_slot); // the native records on the D3D12 executor (interface batches)

 const stall_watch::Bracket stall_bracket("flush_native");
 CampClipScope clip_scope{*this};
 auto* color=active_color_resource_;
 ColorDiagnosticTicks color_flush(color?&color->flush_ticks:nullptr,
  color&&color_mesh_active_?&color->mesh_flush_ticks:
   color&&color_texture_active_?&color->texture_flush_ticks:nullptr,
  nullptr,color&&color_cold_active_?&color->cold_flush_ticks:nullptr);
 auto& layout=native_submission_layouts_[layout_slot];
 auto& native_draw_work_staging_=layout.work;auto& native_batch_staging_=layout.batches;
 auto& native_indirect_args_staging_=layout.arguments;
 if(native_shadow_collecting_)return flush_native_shadows();
 SceneRendererTimingScope scene_timing(
  SceneRendererTimingKind::native_flush);
 // A reused static bake draws with an empty queue (flush_native_static_set).
 // Taken once: any flush nested in this one is an ordinary flush.
 const bool static_reuse=native_static_reuse_,static_bake=native_static_bake_;
 native_static_reuse_=native_static_bake_=false;
 if(native_pending_draws_.empty()&&!static_reuse){discard_native();return S_OK;}
 // Material classes: profiles of newly composed atlases, before any batch binds one.
 prepare_material_profiles();
 const bool materials_on=material_level()>0;
 // Every native batch reads the frame values (t127; material level, view and
 // switches). Only the terrain base uploads and binds them: a frame
 // without terrain (the equipment and trade screens' previews) relied on
 // an earlier binding. The flush binds the latest values itself.
 if(!d3d12_&&native_terrain_frame_srv_){
  ID3D11ShaderResourceView* frame=native_terrain_frame_srv_.Get();
  context_->VSSetShaderResources(PointShadowTextureLayout::terrain_frame_srv,1,&frame);
  context_->PSSetShaderResources(PointShadowTextureLayout::terrain_frame_srv,1,&frame);
 }
  // Terrain batches bound to the cliff tessellation stages; finish unbinds them.
  bool tessellate_terrain=false,tessellation_bound=false;
  auto finish=[&](HRESULT result){flush_anisotropy_=-1;restore_camp_clip();clear_water_reflection_bindings();if(context_){
   context_->PSSetShader(native_ps_.Get(),nullptr,0); // terrain batches bind a terrain variant
   if(tessellation_bound){
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context_->VSSetShader(native_vs_.Get(),nullptr,0);
    context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);tessellation_bound=false;
   }
ID3D11ShaderResourceView* empty[5]{};context_->PSSetShaderResources(8,1,empty);context_->HSSetShaderResources(8,1,empty);context_->DSSetShaderResources(8,1,empty);context_->VSSetShaderResources(36,1,empty);context_->CSSetShaderResources(10,5,empty);context_->VSSetShaderResources(10,1,empty);context_->VSSetShaderResources(12,2,empty);context_->CSSetShaderResources(15,2,empty);context_->CSSetShaderResources(17,1,empty);context_->PSSetShaderResources(18,2,empty);context_->PSSetShaderResources(13,4,empty);context_->PSSetShaderResources(1,2,empty);context_->PSSetShaderResources(4,2,empty);ID3D11SamplerState* no_point_sampler=nullptr;context_->PSSetSamplers(2,1,&no_point_sampler);context_->PSSetShaderResources(20,2,empty);ID3D11SamplerState* empty_history_sampler=nullptr;context_->PSSetSamplers(3,1,&empty_history_sampler);context_->PSSetSamplers(4,1,&empty_history_sampler);context_->PSSetSamplers(10,1,&empty_history_sampler);ID3D11Buffer* empty_history_constants=nullptr;context_->PSSetConstantBuffers(6,1,&empty_history_constants);context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),dsv_.Get());ID3D11UnorderedAccessView* empty_uav=nullptr;context_->CSSetUnorderedAccessViews(4,1,&empty_uav,nullptr);context_->CSSetUnorderedAccessViews(6,1,&empty_uav,nullptr);ID3D11Buffer* empty_constant=nullptr;context_->CSSetConstantBuffers(4,1,&empty_constant);context_->PSSetConstantBuffers(5,1,&empty_constant);}discard_native();
#ifdef EID3D11_EMBEDDED
 if(SUCCEEDED(result))native_flush_failure_line_=0;
#endif
 return result;};
 if(!context_||native_geometry_pages_.empty()||!native_build_cs_)return native_flush_failure_line_=__LINE__,finish(E_FAIL);
 if(native_shadow_constants_dirty_){const HRESULT shadow_hr=upload_native_shadow_constants(0);if(FAILED(shadow_hr))return native_flush_failure_line_=__LINE__,finish(shadow_hr);native_shadow_constants_dirty_=false;}
#ifdef EID3D11_TESTING
 if(test_native_flush_failure_==1){test_native_flush_failure_=0;return native_flush_failure_line_=__LINE__,finish(E_FAIL);}
#endif
 // An unchanged static set reuses its grouped order; the layout check below
 // still verifies every record against the queue.
 const NativeStaticSet* static_set=native_static_set_for_layout(layout_slot);
 auto& bake=layout.static_bake;
 HRESULT hr=S_OK;
 // A reused bake keeps the layout, work and totals of the frame that built
 // it (flush_native_static_set checked its keys). Any other use of a static
 // layout rebuilds it, so its bake stops being valid first.
 if(!static_reuse){
  if(static_set)bake.set_revision=bake.content_revision=0;
  const bool static_order=static_set&&layout.static_revision==static_set->revision&&
   layout.source_revision==native_layout_revision_&&layout.source_draws.size()==native_pending_draws_.size();
  if(static_order)native_draw_order_=layout.source_draws;
  else try{order_native_groups(false);}catch(const std::bad_alloc&){return native_flush_failure_line_=__LINE__,finish(E_OUTOFMEMORY);}
  hr=prepare_native_submission_layout(false,layout_slot);if(FAILED(hr))return native_flush_failure_line_=__LINE__,finish(hr);
  layout.contact_generation=native_contact_generation_;
  if(static_set)layout.static_revision=static_set->revision;
 }
 if(layout.work.empty())return native_flush_failure_line_=__LINE__,finish(S_OK);
 if(!scene_composited_)scene_updated_=true;
 if(native_pending_upload_.size()>UINT_MAX||layout.work.size()>UINT_MAX||layout.batches.size()>UINT_MAX)return native_flush_failure_line_=__LINE__,finish(E_OUTOFMEMORY);
 if(!static_reuse){
  refresh_native_draw_work(layout,false);
  const bool bake_layout=static_set&&static_bake;
  if(bake_layout&&FAILED(hr=bake_native_static_layout(layout)))return native_flush_failure_line_=__LINE__,finish(hr);
 }
 const UINT draw_count=static_cast<UINT>(layout.work.size()),triangle_count=layout.triangles,
  component_count=layout.components,batch_count=static_cast<UINT>(layout.batches.size()),
  args_slots=static_cast<UINT>(layout.arguments.size()/4u); // the batches' slots, then split batches' steep slots
 hr=ensure_native_stream_buffers(static_cast<UINT>(native_pending_upload_.size()),draw_count,triangle_count,component_count,args_slots);
 if(FAILED(hr))return native_flush_failure_line_=__LINE__,finish(hr);
 ID3D11ShaderResourceView* null_srvs[8]={};context_->VSSetShaderResources(1,8,null_srvs);context_->CSSetShaderResources(1,8,null_srvs);ID3D11UnorderedAccessView* null_uavs[4]={};context_->CSSetUnorderedAccessViews(0,4,null_uavs,nullptr);
 D3D11_MAPPED_SUBRESOURCE mapped{};std::uint64_t upload_bytes=native_pending_upload_.size()+(static_reuse?0u:native_draw_work_staging_.size()*sizeof(NativeDrawWorkGpu))+native_indirect_args_staging_.size()*sizeof(UINT);if(native_world_lighting_dirty_){if(!native_world_lighting_constants_)return native_flush_failure_line_=__LINE__,finish(E_FAIL);if(FAILED(hr=context_->Map(native_world_lighting_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return native_flush_failure_line_=__LINE__,finish(hr);std::memcpy(mapped.pData,&native_world_lighting_,sizeof(native_world_lighting_));context_->Unmap(native_world_lighting_constants_.Get(),0);native_world_lighting_dirty_=false;++native_world_lighting_uploads_;upload_bytes+=sizeof(NativeWorldLighting);}
 if(native_point_lights_dirty_){
  const UINT count=static_cast<UINT>(native_point_lights_.size());
  if(count>native_point_light_capacity_){
   D3D11_BUFFER_DESC description{};description.ByteWidth=count*sizeof(NativePointLightGpu);
   description.Usage=D3D11_USAGE_DYNAMIC;description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
   description.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
   description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
   description.StructureByteStride=sizeof(NativePointLightGpu);
   ComPtr<ID3D11Buffer> buffer;ComPtr<ID3D11ShaderResourceView> srv;
   if(FAILED(hr=device_->CreateBuffer(&description,nullptr,&buffer)))return native_flush_failure_line_=__LINE__,finish(hr);
   D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_UNKNOWN;
   view.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;view.Buffer.NumElements=count;
   if(FAILED(hr=device_->CreateShaderResourceView(buffer.Get(),&view,&srv)))return native_flush_failure_line_=__LINE__,finish(hr);
   native_point_light_buffer_=std::move(buffer);native_point_light_srv_=std::move(srv);
   native_point_light_capacity_=count;
  }
  if(count){
   if(FAILED(hr=context_->Map(native_point_light_buffer_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return native_flush_failure_line_=__LINE__,finish(hr);
   std::memcpy(mapped.pData,native_point_lights_.data(),count*sizeof(NativePointLightGpu));
   context_->Unmap(native_point_light_buffer_.Get(),0);
   upload_bytes+=count*sizeof(NativePointLightGpu);
  }
  // The grid depends only on centres and radii: colour fades, flicker and
  // shadow-slot publication reuse it.
  // Compared in place; the input copy is built only when they changed.
  bool grid_changed=native_point_light_grid_inputs_.size()!=native_point_lights_.size();
  for(std::size_t n=0;!grid_changed&&n<native_point_lights_.size();++n)
   grid_changed=native_point_lights_[n].center_inverse_radius_squared!=native_point_light_grid_inputs_[n];
  if(grid_changed){
   std::vector<std::array<float,4>> grid_inputs;
   try{grid_inputs.reserve(count);for(const auto& light:native_point_lights_)grid_inputs.push_back(light.center_inverse_radius_squared);}
   catch(const std::bad_alloc&){return native_flush_failure_line_=__LINE__,finish(E_OUTOFMEMORY);}
   try{if(!build_point_light_grid(grid_inputs,native_point_light_grid_))return native_flush_failure_line_=__LINE__,finish(E_INVALIDARG);}
   catch(const std::bad_alloc&){return native_flush_failure_line_=__LINE__,finish(E_OUTOFMEMORY);}
   native_point_light_grid_inputs_=std::move(grid_inputs);
   const UINT entries=static_cast<UINT>(native_point_light_grid_.data.size());
   if(entries>native_point_light_grid_capacity_){
    // Sized by demand; grows to the largest grid seen.
    D3D11_BUFFER_DESC description{};description.ByteWidth=entries*sizeof(UINT);
    description.Usage=D3D11_USAGE_DYNAMIC;description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    description.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;description.StructureByteStride=sizeof(UINT);
    ComPtr<ID3D11Buffer> buffer;ComPtr<ID3D11ShaderResourceView> srv;
    if(FAILED(hr=device_->CreateBuffer(&description,nullptr,&buffer)))return native_flush_failure_line_=__LINE__,finish(hr);
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_UNKNOWN;
    view.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;view.Buffer.NumElements=entries;
    if(FAILED(hr=device_->CreateShaderResourceView(buffer.Get(),&view,&srv)))return native_flush_failure_line_=__LINE__,finish(hr);
    native_point_light_grid_buffer_=std::move(buffer);native_point_light_grid_srv_=std::move(srv);
    native_point_light_grid_capacity_=entries;
   }
   if(entries){
    if(FAILED(hr=context_->Map(native_point_light_grid_buffer_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return native_flush_failure_line_=__LINE__,finish(hr);
    std::memcpy(mapped.pData,native_point_light_grid_.data.data(),entries*sizeof(UINT));
    context_->Unmap(native_point_light_grid_buffer_.Get(),0);
    upload_bytes+=entries*sizeof(UINT);
   }
  }
  native_point_lights_dirty_=false;native_point_constants_dirty_=true;
 }
 // Caves: the point lights' first bounce (cave_irradiance.h), from the
 // lights just uploaded; re-injected only when its inputs changed. Its
 // sampling constants are the tail of the point-light constants.
 update_cave_irradiance();
 {
  const CaveSampleConstants cave=cave_sample_constants();
  if(native_point_constants_dirty_||std::memcmp(&cave,&native_cave_constants_written_,sizeof(cave))!=0){
   if(!native_point_light_constants_)return native_flush_failure_line_=__LINE__,finish(E_FAIL);
   if(FAILED(hr=context_->Map(native_point_light_constants_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return native_flush_failure_line_=__LINE__,finish(hr);
   const auto& grid=native_point_light_grid_;
   // An empty grid (no lights) selects no cell, so no pixel visits a light.
   // glow: the emissive-surface lights (nativePointLightGlowCount); packed
   // colour receivers walk the grid only for them.
   struct{UINT count,width,height,glow;float origin[2],inverse_cell,tail;CaveSampleConstants cave;}values{
    static_cast<UINT>(native_point_lights_.size()),grid.width,grid.height,static_cast<UINT>(emissive_published_),
    {grid.origin[0],grid.origin[1]},grid.inverse_cell,0,cave};
   static_assert(sizeof(values)==64,"point-light constant layout");
   std::memcpy(mapped.pData,&values,sizeof(values));
   context_->Unmap(native_point_light_constants_.Get(),0);
   upload_bytes+=sizeof(values);native_point_constants_dirty_=false;native_cave_constants_written_=cave;
  }
 }
#ifdef EID3D11_TESTING
 if(test_native_flush_failure_==2){test_native_flush_failure_=0;return native_flush_failure_line_=__LINE__,finish(E_FAIL);}
#endif
 if(!native_pending_upload_.empty()){if(FAILED(hr=context_->Map(native_dynamic_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return native_flush_failure_line_=__LINE__,finish(hr);std::memcpy(mapped.pData,native_pending_upload_.data(),native_pending_upload_.size());context_->Unmap(native_dynamic_.Get(),0);}
 if(FAILED(hr=upload_native_terrain_phases()))return native_flush_failure_line_=__LINE__,finish(hr);
 // Static layouts keep their work in their own buffer, written only when
 // the layout is built; the bake becomes reusable once that write is done.
 if(static_set){
  if(!static_reuse){
   if(FAILED(hr=upload_native_static_work(layout)))return native_flush_failure_line_=__LINE__,finish(hr);
   if(static_bake){bake.set_revision=static_set->revision;bake.content_revision=native_static_content_revision_;}
  }
 }else{
 if(FAILED(hr=context_->Map(native_draw_work_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return native_flush_failure_line_=__LINE__,finish(hr);
 std::memcpy(mapped.pData,native_draw_work_staging_.data(),native_draw_work_staging_.size()*sizeof(NativeDrawWorkGpu));context_->Unmap(native_draw_work_.Get(),0);
 }
#ifdef EID3D11_TESTING
 test_native_work_upload_bytes_+=native_draw_work_staging_.size()*sizeof(NativeDrawWorkGpu);
#endif
 D3D11_BOX args_box{};args_box.right=args_slots*16u;args_box.bottom=args_box.back=1;if(args_slots)context_->UpdateSubresource(native_indirect_args_.Get(),0,&args_box,native_indirect_args_staging_.data(),0,0);profiler_.add_upload(upload_bytes);
 if(FAILED(hr=upload_native_instances())||FAILED(hr=prepare_native_poses())||FAILED(hr=ensure_native_world_states()))return native_flush_failure_line_=__LINE__,finish(hr);
 context_->CSSetUnorderedAccessViews(6,1,native_world_state_uav_.GetAddressOf(),nullptr);
 context_->CSSetShaderResources(10,1,native_animation_srv_.GetAddressOf());
 context_->CSSetShaderResources(12,1,native_pose_node_srv_.GetAddressOf());
 context_->CSSetShaderResources(13,1,native_persistent_instance_srv_.GetAddressOf());
 context_->CSSetShaderResources(19,1,native_terrain_layer_shift_srv_.GetAddressOf());
 context_->CSSetShaderResources(16,1,static_set?bake.work_srv.GetAddressOf():native_draw_work_srv_.GetAddressOf());
 context_->CSSetConstantBuffers(3,1,native_world_clip_constants_.GetAddressOf());
 {ID3D11ShaderResourceView* hiz=native_occlusion_active()?native_hiz_srv_.Get():nullptr;context_->CSSetShaderResources(26,1,&hiz);}
 ID3D11ShaderResourceView* compute_srvs[8]={nullptr,native_dynamic_srv_.Get(),nullptr,nullptr,native_draw_srv_.Get(),nullptr,nullptr,nullptr};context_->CSSetShader(native_build_cs_.Get(),nullptr,0);context_->CSSetConstantBuffers(1,1,native_world_lighting_constants_.GetAddressOf());ID3D11UnorderedAccessView* compute_uavs[4]={native_visible_triangle_uav_.Get(),native_indirect_args_uav_.Get(),native_prepared_vertex_uav_.Get(),native_world_vertex_uav_.Get()};context_->CSSetUnorderedAccessViews(0,4,compute_uavs,nullptr);
 const bool capture_lod=begin_native_lod_capture(layout);
 ID3D11UnorderedAccessView* lod_uav=capture_lod?native_lod_observation_uav_.Get():nullptr;
 context_->CSSetUnorderedAccessViews(4,1,&lod_uav,nullptr);
 for(const auto& range:layout.dispatches){
  compute_srvs[0]=native_geometry_page_srv(range.geometry_page);if(!compute_srvs[0])return native_flush_failure_line_=__LINE__,finish(E_FAIL);
  context_->CSSetShaderResources(1,8,compute_srvs);
  if(FAILED(hr=upload_native_work_range(range,capture_lod)))return native_flush_failure_line_=__LINE__,finish(hr);
  UINT dispatch_width=0,dispatch_height=0;if(!native_dispatch_dimensions(range.count,dispatch_width,dispatch_height))return native_flush_failure_line_=__LINE__,finish(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
  transition_profile_pass("compute_build");context_->Dispatch(dispatch_width,dispatch_height,1);++perf_native_compute_dispatches_;++native_frame_stats_.native_compute_dispatches;
 }
 {ID3D11UnorderedAccessView* empty_lod=nullptr;context_->CSSetUnorderedAccessViews(4,1,&empty_lod,nullptr);}
 context_->CSSetUnorderedAccessViews(0,4,null_uavs,nullptr);context_->CSSetShaderResources(1,8,null_srvs);ID3D11Buffer* null_constant=nullptr;context_->CSSetConstantBuffers(1,1,&null_constant);context_->CSSetConstantBuffers(4,1,&null_constant);context_->CSSetShader(nullptr,nullptr,0);
 const bool history_active=prepare_shadow_history();
 context_->IASetInputLayout(nullptr);context_->IASetVertexBuffers(0,0,nullptr,nullptr,nullptr);context_->IASetIndexBuffer(nullptr,DXGI_FORMAT_UNKNOWN,0);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context_->VSSetShader(native_vs_.Get(),nullptr,0);context_->PSSetShader(native_ps_.Get(),nullptr,0);bind_draw_constants();D3D11_VIEWPORT viewport=target_viewport();context_->RSSetViewports(1,&viewport);context_->OMSetRenderTargets(1,rtv_.GetAddressOf(),dsv_.Get());
 if(static_reuse){
  // The bake's totals; its meshes stay current like every drawn mesh
  // (retire_stale_native_geometry keeps a mesh used this frame).
  native_world_prepared_vertices_+=bake.prepared_vertices;
  for(auto* mesh:bake.meshes)mesh->last_used_frame=native_frame_index_;
 }else for(const auto& work:native_draw_work_staging_)
  if(work.record!=UINT_MAX&&(work.flags&2u))native_world_prepared_vertices_+=native_instance_records_[work.record].command.component_count;
 for(const auto& selected:native_pending_draws_){
  const auto& queued=native_instance_records_[selected.record].command;
  if(queued.world_slot>=native_prepared_mesh_slots_.size())continue;
  auto& slot=native_prepared_mesh_slots_[queued.world_slot];
  if(slot.world_generation==queued.world_generation&&slot.world_base==queued.world_base)
   slot.world_valid=true;
 }
 if(!batch_count)return native_flush_failure_line_=__LINE__,finish(S_OK);
 ID3D11ShaderResourceView* world_srvs[3]={nullptr,native_dynamic_srv_.Get(),nullptr};
 context_->VSSetShaderResources(1,3,world_srvs);
 context_->VSSetShaderResources(8,1,native_world_vertex_srv_.GetAddressOf());
 // Crown impostor atlas (t44/t45, nativeImpostorTexel); null before any bake.
 {ID3D11ShaderResourceView* atlas[2]={impostors_.albedo_srv.Get(),impostors_.normal_srv.Get()};context_->PSSetShaderResources(44,2,atlas);}
 context_->VSSetShaderResources(10,1,native_animation_srv_.GetAddressOf());
 context_->VSSetShaderResources(12,1,native_pose_node_srv_.GetAddressOf());
 context_->VSSetShaderResources(13,1,native_persistent_instance_srv_.GetAddressOf());
 context_->VSSetShaderResources(19,1,native_terrain_layer_shift_srv_.GetAddressOf());
 context_->VSSetConstantBuffers(3,1,native_world_clip_constants_.GetAddressOf());
 // The cliff tessellation's distance cap reads the camera and output size.
 context_->HSSetConstantBuffers(3,1,native_world_clip_constants_.GetAddressOf());
 if(!scene_composited_){
  // The game's hourly fog colour of this world flush: the sky's horizon meets the fog in it.
  const auto& first=native_state_buckets_[native_batch_staging_.front().state_bucket].state;
  const DWORD fog=(native_static_frame_?*native_static_frame_:first).render[D3DRENDERSTATE_FOGCOLOR];
  sky_fog_color_[0]=((fog>>16)&255)/255.f;sky_fog_color_[1]=((fog>>8)&255)/255.f;sky_fog_color_[2]=(fog&255)/255.f;
 }
 if(FAILED(hr=bind_world_fog()))return native_flush_failure_line_=__LINE__,finish(hr);
 // The settings-specialised programs matching this flush's uploaded values (or the general ones).
 update_native_specialisation();
  // Terrain batches use the snow trail pixel variant while a field exists.
  ID3D11PixelShader* const snow_ps=!scene_composited_&&snow_terrain_srv_?native_pixel(&Renderer::native_snow_ps_):nullptr;
  // Low settings: the lean and liquid shaders without the switched-off
  // stages. Low terrain batches draw with the lean one: every terrain stage
  // is off there (the snow trail variant still takes them under a field).
  const bool low_pixels=native_low_ps_&&native_low_liquid_ps_&&native_low_pixel_path(debug_overlay_.config());
  ID3D11PixelShader* const object_ps=low_pixels?native_pixel(&Renderer::native_low_ps_):native_pixel(&Renderer::native_ps_);
  ID3D11PixelShader* const terrain_ps=snow_ps?snow_ps:low_pixels?object_ps:native_pixel(&Renderer::native_terrain_ps_);
  // The flat tile class's variant of the same choice (NativeBatch::flat_pixels).
  const bool flat_variants=native_terrain_flat_ps_&&native_snow_flat_ps_;
  ID3D11PixelShader* const terrain_flat_ps=!flat_variants?terrain_ps:snow_ps?native_pixel(&Renderer::native_snow_flat_ps_):
   low_pixels?object_ps:native_pixel(&Renderer::native_terrain_flat_ps_);
  // A missing terrain variant leaves terrain undrawn: name which are missing, once.
  if((!terrain_ps||!terrain_flat_ps)&&!terrain_variant_missing_reported_){
   terrain_variant_missing_reported_=true;
   const ID3D11PixelShader* const variants[]={native_pixel(&Renderer::native_ps_),native_pixel(&Renderer::native_terrain_ps_),native_pixel(&Renderer::native_snow_ps_),native_pixel(&Renderer::native_low_ps_),native_pixel(&Renderer::native_low_unlit_ps_),
    
    native_pixel(&Renderer::native_terrain_flat_ps_),native_pixel(&Renderer::native_snow_flat_ps_),};
   unsigned present=0;for(unsigned n=0;n<std::size(variants);++n)if(variants[n])present|=1u<<n;
   char text[128];std::snprintf(text,sizeof(text),"present=0x%03x low=%d snow=%d",present,low_pixels?1:0,snow_ps?1:0);
   bridge_report_event("terrain_pixel_variant_missing",text);
  }
 // Terrain 2.0 material cache (terrain2_cache.h): this frame's bake, then its views for the terrain pixels.
 update_terrain2_cache();
 if(sky_draw_pending_||cave_glow_pending_){
  if(sky_draw_pending_)draw_owned_sky();
  draw_cave_exit_glow();
  context_->IASetInputLayout(nullptr);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_->VSSetShader(native_vs_.Get(),nullptr,0);context_->PSSetShader(native_ps_.Get(),nullptr,0);
  bind_draw_constants();
 }
 // The flush's own targets (frame_plan.h): the batch loop below starts from
 // "rtv_ and dsv_ bound" (bound_color_targets 0, bound_depth_view dsv_), so
 // the flush binds them itself instead of relying on the previous pass.
 bind_plan_targets(plan_pass("native_flush",rtv_.Get(),dsv_.Get()));
 context_->VSSetConstantBuffers(1,1,native_world_lighting_constants_.GetAddressOf());
 context_->PSSetConstantBuffers(1,1,native_world_lighting_constants_.GetAddressOf());
 // The interface after composition is not in the world: no sun or cloud
 // shadow, history, lamps or bounce light (unbound b2/b5/b6 read zero), as
 // renderer_native_d3d12.cpp binds zero blocks; its fog is border fog only.
 {ID3D11Buffer* const interface_none=nullptr;
  context_->PSSetConstantBuffers(2,1,scene_composited_?&interface_none:native_shadow_constants_.GetAddressOf());
  context_->PSSetConstantBuffers(5,1,scene_composited_?&interface_none:native_point_light_constants_.GetAddressOf());
  context_->PSSetConstantBuffers(6,1,scene_composited_?&interface_none:shadow_history_constants_.GetAddressOf());}
 ID3D11ShaderResourceView* history_srvs[2]={
  history_active?shadow_history_srv_[shadow_history_previous_index_].Get():nullptr,
  history_active?shadow_history_depth_srv_.Get():nullptr};
 context_->PSSetShaderResources(20,2,history_srvs);
 ID3D11SamplerState* history_sampler=history_active?shadow_history_sampler_.Get():nullptr;
 context_->PSSetSamplers(3,1,&history_sampler);
 context_->PSSetShaderResources(9,1,native_shadow_depth_srv_.GetAddressOf());context_->PSSetShaderResources(64,1,native_shadow_range_srv_.GetAddressOf());
 ID3D11ShaderResourceView* point_lights=native_point_lights_.empty()?nullptr:native_point_light_srv_.Get();
 context_->PSSetShaderResources(18,1,&point_lights);
 ID3D11ShaderResourceView* point_light_grid=native_point_lights_.empty()?nullptr:native_point_light_grid_srv_.Get();
 context_->PSSetShaderResources(17,1,&point_light_grid);
 bind_cave_irradiance();
 if(!scene_composited_)refresh_emissive_lights();
 {
  // Ground contact blend: this frame's terrain height grid and painted colour
  // map, only once the ground cover has refreshed them for the current map.
  const bool contact=!scene_composited_&&gc_drawn_frame_==native_frame_index_&&
   gc_map_generation_==ambient_map_generation_&&ambient_ground_srv_&&gc_color_srv_;
  // Terrain cliffs read the same tile grid and atlas (neighbouring tiles'
  // art), without the ground cover.
  const bool cliffs=!scene_composited_&&configured_terrain_cliffs(debug_overlay_.config());
  // The terrain material blend reads the grid's second half and the atlas.
  const bool tile_blend=!scene_composited_&&configured_integer(debug_overlay_.config(),"terrain_blend",1)>0;
  // The terrain's own art for the band; unbound reads the painted colour only.
  if((contact||cliffs||tile_blend)&&contact_update_frame_!=native_frame_index_){
   contact_update_frame_=native_frame_index_;
   if(!contact){
    // Without the ground cover the shared per-map grids may not exist yet.
    AmbientWorldSource inputs;
    if(bridge_ambient_world_source(inputs)&&inputs.terrain)rebuild_ambient_particle_map(inputs,bridge_world_fog_snapshot().environment);
   }
   if(FAILED(update_contact_blend_maps())){
    release_contact_blend_maps();
    bridge_append_probe_event("contact_blend_art_omitted","contact atlas or tile grid unavailable; contact bands show the painted ground colour on this map");
   }
  }
  ID3D11ShaderResourceView* ground[2]{contact?ambient_ground_srv_.Get():nullptr,contact?gc_color_srv_.Get():nullptr};
  context_->PSSetShaderResources(13,2,ground);
  const bool art=(contact||cliffs||tile_blend)&&contact_maps_.tiles&&contact_maps_.atlas&&
   contact_map_generation_==ambient_map_generation_&&contact_constants_generation_==contact_map_generation_;
  ID3D11ShaderResourceView* terrain_art[2]{art?contact_maps_.tiles.Get():nullptr,art?contact_maps_.atlas.Get():nullptr};
  context_->PSSetShaderResources(15,2,terrain_art);
  ID3D11SamplerState* contact_sampler=art?contact_sampler_.Get():nullptr;
  context_->PSSetSamplers(8,1,&contact_sampler);
  // Tessellated terrain (Phong smoothing; no textures in the domain stage).
  tessellate_terrain=!scene_composited_&&terrain_tessellation_frame_==native_frame_index_&&terrain_tess_vs_&&terrain_hs_&&terrain_ds_&&terrain_hs_plain_&&terrain_ds_plain_;
 }
  {
  std::array<ID3D11ShaderResourceView*,PointShadowTextureLayout::array_bindings> views{};
  for(std::size_t i=0;i<native_point_shadow_pages_.size();++i)views[i]=native_point_shadow_pages_[i].srv.Get();
  context_->PSSetShaderResources(PointShadowTextureLayout::first_srv,static_cast<UINT>(views.size()),views.data());
 }
  context_->PSSetSamplers(1,1,native_shadow_sampler_.GetAddressOf());
  context_->PSSetSamplers(2,1,native_point_shadow_sampler_.GetAddressOf());
  context_->PSSetSamplers(4,1,native_shadow_far_sampler_.GetAddressOf());
  context_->PSSetSamplers(10,1,native_shadow_gather_sampler_.GetAddressOf());
 const bool shadow_coverage=debug_overlay_.mode()==DebugOverlayMode::full&&
  debug_overlay_.config().debug_resource_view()==DebugResourceView::shadow_cascade_coverage;
 Constants previous_constants{};bool have_constants=false;
 // Target set: bit 0 shadow history, bit 1 AO ambient share (world scene only).
 unsigned bound_color_targets=0;
 const bool write_ambient=ao_ambient_rtv_&&!scene_composited_&&rtv_.Get()==scene_composite_.target();
 ao_ambient_written_|=write_ambient;
 ID3D11DepthStencilView* bound_depth_view=dsv_.Get();bool water_bound=false;
 context_->VSSetShaderResources(4,1,native_visible_triangle_srv_.GetAddressOf());context_->VSSetShaderResources(7,1,native_prepared_vertex_srv_.GetAddressOf());
 // Snow trail field (t8; nativeSnowTrailWindow.w is 0 without it). The
 // pixel and cliff tessellation stages of active-map ground read it; the
 // vertex stage reads it at t36 to sink units into troughs (nativeFigureTrailSink).
 {ID3D11ShaderResourceView* snow=scene_composited_?nullptr:snow_terrain_srv_.Get();
  context_->PSSetShaderResources(8,1,&snow);context_->HSSetShaderResources(8,1,&snow);context_->DSSetShaderResources(8,1,&snow);
  context_->VSSetShaderResources(36,1,&snow);}
 const bool neighbour_terrain_blend=configured_boolean(debug_overlay_.config(),"terrain_blend_neighbours",true);
 const bool neighbour_fog_skip=configured_boolean(debug_overlay_.config(),"neighbour_fog_skip",true);
 // A batch's contact views (t13-t16) and constants.
 const auto batch_constants=[&](const NativeBatch& batch,const NativeQueuedDraw& draw,const NativeStateBucket& bucket,
                                ID3D11ShaderResourceView* (&contact_views)[4]){
  Constants constants;native_batch_constants(batch,draw,bucket,contact_views,neighbour_terrain_blend,neighbour_fog_skip,shadow_coverage,&constants);
  return constants;
 };

 // Depth prepass and sun-shadow mask (native_shader.h nativePrepassPS and
 // nativeShadowMaskPS). Batches whose depth is final before shading
 // (opaque, or alpha-tested and overwriting; depth tested LESS/LESSEQUAL and
 // written; no liquid; terrain without an alpha test or the two-pass cache
 // routing) draw depth first and mark stencil 0x80; the mask pass shades the
 // marked pixels' sun and cloud shadow once; their material pass then runs
 // with depth EQUAL and reads the mask (NATIVE_SHADOW_MASK variants).
 ID3D11PixelShader* const masked_object_ps=low_pixels?native_pixel(&Renderer::native_masked_low_ps_):native_pixel(&Renderer::native_masked_ps_);
 ID3D11PixelShader* const masked_snow_ps=snow_ps?native_pixel(&Renderer::native_masked_snow_ps_):nullptr;
 ID3D11PixelShader* const masked_terrain_ps=snow_ps?masked_snow_ps:low_pixels?masked_object_ps:
  native_pixel(&Renderer::native_masked_terrain_ps_);
 ID3D11PixelShader* const masked_terrain_flat_ps=!flat_variants?masked_terrain_ps:
  snow_ps?native_pixel(&Renderer::native_masked_snow_flat_ps_):
  low_pixels?masked_object_ps:native_pixel(&Renderer::native_masked_terrain_flat_ps_);
 ID3D11PixelShader* const prepass_ps=low_pixels?native_low_prepass_ps_.Get():native_prepass_ps_.Get();
 ID3D11PixelShader* const shadow_mask_ps=low_pixels?native_pixel(&Renderer::native_low_shadow_mask_ps_):native_pixel(&Renderer::native_shadow_mask_ps_);
 const bool prepass_ready=configured_boolean(debug_overlay_.config(),"depth_prepass",true)&&!scene_composited_&&fog_camera_valid_&&
  rtv_.Get()==scene_composite_.target()&&dsv_.Get()==world_dsv_.Get()&&prepass_rest_rtv_&&prepass_normal_rtv_&&shadow_mask_rtv_&&
  world_depth_read_only_dsv_&&masked_depth_state_&&depth_srv_&&native_shadow_mask_vs_&&native_prepass_vs_&&prepass_ps&&shadow_mask_ps&&
  masked_object_ps&&masked_terrain_ps&&masked_terrain_flat_ps;
 // Settings read once per flush (the batch loops below ran these lookups per batch).
 ++native_flush_serial_;
 const bool water_simple=configured_boolean(debug_overlay_.config(),"water_simple",false);
 const bool water_physical=configured_boolean(debug_overlay_.config(),"water_physical",true);
 flush_anisotropy_=configured_integer(debug_overlay_.config(),"anisotropy",3); // apply_state; finish clears it
 const bool prepass_tessellation=terrain_prepass_tess_vs_&&terrain_prepass_hs_&&terrain_prepass_snow_hs_&&terrain_prepass_ds_&&terrain_prepass_snow_ds_;
 bool any_prepass=false;
 try{native_prepass_batches_.assign(native_batch_staging_.size(),0);}catch(const std::bad_alloc&){return native_flush_failure_line_=__LINE__,finish(E_OUTOFMEMORY);}
 for(std::size_t batch_index=0;prepass_ready&&batch_index<native_batch_staging_.size();++batch_index){
  const NativeBatch& batch=native_batch_staging_[batch_index];
  const auto* baked=static_reuse?&bake.batches[batch_index]:nullptr;
  const NativeQueuedDraw& draw=baked?native_instance_records_[baked->first_record].command:native_queued_draw(native_draw_order_[batch.first_draw]);
  const NativeStateBucket& bucket=native_state_buckets_[batch.state_bucket];const auto& st=bucket.state;
  if(bucket.opacity==NativeOpacityClass::ordered||draw.hud_composition.opacity<1.f)continue;
  // Posed draws (characters, animated objects): their skinning runs per
  // vertex in both passes for few pixels; they keep the full lookup.
  if(batch.posed)continue;
  const bool blend=st.render[D3DRENDERSTATE_ALPHABLENDENABLE]!=FALSE;
  const bool overwrite=!blend||(st.render[D3DRENDERSTATE_SRCBLEND]==D3DBLEND_ONE&&st.render[D3DRENDERSTATE_DESTBLEND]==D3DBLEND_ZERO);
  // Alpha-tested and blended: its soft edge blends over what lies behind.
  if(bucket.opacity==NativeOpacityClass::cutout&&!overwrite)continue;
  if(st.render[D3DRENDERSTATE_ZENABLE]==D3DZB_FALSE||!st.render[D3DRENDERSTATE_ZWRITEENABLE])continue;
  const DWORD depth_func=st.render[D3DRENDERSTATE_ZFUNC];
  if(depth_func!=D3DCMP_LESS&&depth_func!=D3DCMP_LESSEQUAL)continue;
  const bool terrain=bucket.semantic_kind==SemanticKind::terrain;
  // Terrain's alpha follows its material stages, which the prepass lacks.
  if(terrain&&(st.render[D3DRENDERSTATE_ALPHATESTENABLE]!=FALSE||(tessellate_terrain&&!prepass_tessellation)))continue;
  bool liquid=baked&&baked->liquid;
  for(std::size_t n=batch.first_draw;!baked&&terrain&&!liquid&&n<batch.first_draw+batch.draw_count;++n)
   liquid=native_queued_draw(native_draw_order_[n]).mesh->has_liquid;
  if(liquid)continue;
  native_prepass_batches_[batch_index]=1;any_prepass=true;
 }
 if(any_prepass){
  ID3D11ShaderResourceView* no_views[2]{};context_->PSSetShaderResources(4,2,no_views);
  // The prepass's own vertex stages (nativePrepassVS, nativePrepassTessVS).
  if(tessellation_bound){context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);tessellation_bound=false;}
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context_->VSSetShader(native_prepass_vs_.Get(),nullptr,0);
  bool prepass_tessellated=false;
  ID3D11RenderTargetView* prepass_targets[2]={prepass_rest_rtv_.Get(),prepass_normal_rtv_.Get()};
  context_->OMSetRenderTargets(2,prepass_targets,dsv_.Get());
  context_->PSSetShader(prepass_ps,nullptr,0);
  transition_profile_pass("depth_prepass");
  for(std::size_t batch_index=0;batch_index<native_batch_staging_.size();++batch_index){
   if(!native_prepass_batches_[batch_index])continue;
   const NativeBatch& batch=native_batch_staging_[batch_index];
   const auto* baked=static_reuse?&bake.batches[batch_index]:nullptr;
   const NativeQueuedDraw& draw=baked?native_instance_records_[baked->first_record].command:native_queued_draw(native_draw_order_[batch.first_draw]);
   const NativeStateBucket& bucket=native_state_buckets_[batch.state_bucket];const auto& st=bucket.state;
   ID3D11ShaderResourceView* geometry=native_geometry_page_srv(batch.geometry_page);if(!geometry)return native_flush_failure_line_=__LINE__,finish(E_FAIL);
   context_->VSSetShaderResources(1,1,&geometry);
   ID3D11ShaderResourceView* contact_views[4];const Constants constants=batch_constants(batch,draw,bucket,contact_views);
   if(!have_constants||!draw_constants_live()||std::memcmp(&previous_constants,&constants,sizeof(constants))!=0){
    if(FAILED(hr=upload_draw_constants(&constants,sizeof(constants))))return native_flush_failure_line_=__LINE__,finish(hr);
    previous_constants=constants;have_constants=true;
   }
   const bool textured=bucket.texture_srv!=nullptr;apply_state(st,textured,draw.hud_composition);
   context_->OMSetDepthStencilState(prepass_depth_states_[st.render[D3DRENDERSTATE_ZFUNC]==D3DCMP_LESS?0:1].Get(),0x80);
   context_->OMSetBlendState(nullptr,nullptr,0xffffffffu);
   ID3D11ShaderResourceView* texture=bucket.texture_array?nullptr:bucket.texture_srv.Get();
   ID3D11ShaderResourceView* texture_array=bucket.texture_array?bucket.texture_srv.Get():nullptr;
   context_->PSSetShaderResources(0,1,&texture);context_->PSSetShaderResources(6,1,&texture_array);
   const bool terrain=bucket.semantic_kind==SemanticKind::terrain;
   // The material pass's regions: the flat one untessellated (with a snow
   // trail field on the active map: in two parts, trail_split below;
   // liquid batches never take the prepass).
   const auto prepass_stages=[&](bool tessellate){
    if(tessellate==prepass_tessellated)return;
    prepass_tessellated=tessellate;
    context_->IASetPrimitiveTopology(tessellate?D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST:D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context_->VSSetShader(tessellate?terrain_prepass_tess_vs_.Get():native_prepass_vs_.Get(),nullptr,0);
    const bool snow_stages=snow_terrain_srv_!=nullptr;
    context_->HSSetShader(tessellate?(snow_stages?terrain_prepass_snow_hs_.Get():terrain_prepass_hs_.Get()):nullptr,nullptr,0);
    context_->DSSetShader(tessellate?(snow_stages?terrain_prepass_snow_ds_.Get():terrain_prepass_ds_.Get()):nullptr,nullptr,0);
   };
   // The same region choice as the material pass (depth EQUAL there).
   const bool prepass_neighbour=bucket.material_key.contact_map!=0;
   // Neighbour-map ground draws untessellated (see the material pass).
   const bool tessellated_batch=tessellate_terrain&&terrain&&!prepass_neighbour;
   const bool prepass_trail_split=tessellated_batch&&batch.flat_pixels&&snow_terrain_srv_&&!prepass_neighbour;
   prepass_stages(tessellated_batch&&!(batch.flat_pixels&&(!snow_terrain_srv_||prepass_neighbour))&&!prepass_trail_split);
   std::uint64_t logical_triangles=baked?baked->triangles:0;
   for(std::size_t item=batch.first_draw;!baked&&item<batch.first_draw+batch.draw_count;++item)logical_triangles+=native_queued_draw(native_draw_order_[item]).index_count/3u;
   profiler_.add_draw(logical_triangles,static_cast<UINT>(batch.draw_count));profiler_.add_batch();
   const bool split=terrain&&batch.steep_args_slot!=UINT_MAX;
   for(UINT part=0;part<(split?2u:1u);++part){
    if(part){
     Constants steep=constants;steep.native_triangle_base=batch.steep_triangle_base;
     if(FAILED(hr=upload_draw_constants(&steep,sizeof(steep))))return native_flush_failure_line_=__LINE__,finish(hr);
     previous_constants=steep;have_constants=true;
     prepass_stages(tessellated_batch); // the steep region
    }
    if(prepass_trail_split&&!part){
     // The flat region: untessellated with the triangles near an active
     // trail tile collapsed (layer bit 16), then tessellated with only those (bit 32).
     for(const UINT layer_bits:{16u,32u}){
      Constants layer=constants;layer.liquid_layer|=layer_bits;
      if(FAILED(hr=upload_draw_constants(&layer,sizeof(layer))))return native_flush_failure_line_=__LINE__,finish(hr);
      previous_constants=layer;have_constants=true;
      prepass_stages(layer_bits==32u);
      context_->DrawInstancedIndirect(native_indirect_args_.Get(),static_cast<UINT>(batch_index)*16u);
     }
     continue;
    }
    context_->DrawInstancedIndirect(native_indirect_args_.Get(),part?batch.steep_args_slot*16u:static_cast<UINT>(batch_index)*16u);
   }
  }
  if(prepass_tessellated){context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);}
  if(FAILED(hr=resolve_world_shadow_mask()))return native_flush_failure_line_=__LINE__,finish(hr);
  context_->VSSetShader(native_vs_.Get(),nullptr,0);context_->RSSetViewports(1,&viewport);
  bound_color_targets=0;bound_depth_view=dsv_.Get();
 }
 // Only the wading units that can reach a liquid batch's water (exact: the
 // others add nothing to any of its pixels). A neighbour map's water sways
 // by its own wave multipliers: it keeps the frame's full set.
 const auto bind_batch_units=[&](const NativeBatch& batch,const auto* baked){
  bool neighbour=baked&&baked->neighbour;
  float lo[2]{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()},hi[2]{-std::numeric_limits<float>::max(),-std::numeric_limits<float>::max()};
  if(baked){lo[0]=baked->xy_min[0];lo[1]=baked->xy_min[1];hi[0]=baked->xy_max[0];hi[1]=baked->xy_max[1];}
  else for(std::size_t n=batch.first_draw;n<batch.first_draw+batch.draw_count;++n){
   const auto& queued=native_queued_draw(native_draw_order_[n]);
   neighbour|=(queued.instance.flags&native_neighbour_flag)!=0;
   for(int axis=0;axis<2;++axis){lo[axis]=std::min(lo[axis],queued.bounds_min[axis]);hi[axis]=std::max(hi[axis],queued.bounds_max[axis]);}
  }
  if(!neighbour)bind_liquid_ripple_units(lo,hi);
 };
 // Half-resolution water reflection: once per flush, at its first liquid
 // batch with this frame's reflection snapshot, every liquid batch of the
 // flush writes its rays and water points at half resolution (the liquid
 // input program, its own depth: the nearest water), then nativeLiquidSsrPS
 // marches them; the water bodies resolve the result (t29-t31) at full
 // resolution. Unavailable: no screen reflection on the water, named once.
 bool water_ssr_done=false;
 const auto trace_water=[&]()->HRESULT{
  water_ssr_done=true;
  ID3D11ShaderResourceView* none[4]{};context_->PSSetShaderResources(29,4,none);
  if(low_pixels)return S_OK; // the Low programs never read it
  if(!native_liquid_ssr_input_ps_||!native_liquid_ssr_march_ps_||!native_shadow_mask_vs_||!shadow_mask_raster_||
     FAILED(ensure_liquid_normal_map())||FAILED(create_water_ssr_targets())){
   if(!water_ssr_failure_reported_){
    water_ssr_failure_reported_=true;
    bridge_report_event("water_ssr","unavailable: half-resolution reflection programs or targets; water shows no screen reflections");
   }
   return S_OK;
  }
  transition_profile_pass("water_ssr");
  const float zero[4]{};
  for(const auto& target:water_ssr_rtv_)context_->ClearRenderTargetView(target.Get(),zero);
  context_->ClearDepthStencilView(water_ssr_dsv_.Get(),D3D11_CLEAR_DEPTH,1.f,0);
  D3D11_VIEWPORT half=viewport;half.TopLeftX*=.5f;half.TopLeftY*=.5f;half.Width*=.5f;half.Height*=.5f;
  context_->RSSetViewports(1,&half);
  ID3D11RenderTargetView* inputs[2]={water_ssr_rtv_[0].Get(),water_ssr_rtv_[1].Get()};
  context_->OMSetRenderTargets(2,inputs,water_ssr_dsv_.Get());
  {ID3D11ShaderResourceView* textures[2]={liquid_normal_srv_.Get(),liquid_detail_srv_.Get()};ID3D11SamplerState* normal_sampler=liquid_normal_sampler_.Get();
   context_->PSSetShaderResources(22,2,textures);context_->PSSetSamplers(6,1,&normal_sampler);}
  context_->PSSetConstantBuffers(7,1,water_reflection_constants_.GetAddressOf());
  context_->PSSetShader(native_liquid_ssr_input_ps_.Get(),nullptr,0);
  // Liquid batches are terrain: the stages their water body draws with
  // (untessellated on neighbour maps, as in the batch loop below).
  const bool snow_stages=snow_terrain_srv_!=nullptr;
  int trace_stages=-1; // the stages bound in this loop: 1 tessellated, 0 not
  const auto bind_trace_stages=[&](bool tessellate){
   if(int(tessellate)==trace_stages)return;
   trace_stages=int(tessellate);
   context_->IASetPrimitiveTopology(tessellate?D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST:D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
   context_->VSSetShader(tessellate?terrain_tess_vs_.Get():native_vs_.Get(),nullptr,0);
   context_->HSSetShader(tessellate?(snow_stages?terrain_hs_.Get():terrain_hs_plain_.Get()):nullptr,nullptr,0);
   context_->DSSetShader(tessellate?(snow_stages?terrain_ds_.Get():terrain_ds_plain_.Get()):nullptr,nullptr,0);
  };
  HRESULT result=S_OK;
  for(std::size_t index=0;index<native_batch_staging_.size();++index){
   const NativeBatch& item=native_batch_staging_[index];
   const auto* item_baked=static_reuse?&bake.batches[index]:nullptr;
   const NativeQueuedDraw& item_draw=item_baked?native_instance_records_[item_baked->first_record].command:native_queued_draw(native_draw_order_[item.first_draw]);
   const NativeStateBucket& item_bucket=native_state_buckets_[item.state_bucket];
   if(item_bucket.semantic_kind!=SemanticKind::terrain)continue;
   bool liquid=item_baked&&item_baked->liquid;
   for(std::size_t n=item.first_draw;!item_baked&&!liquid&&n<item.first_draw+item.draw_count;++n)
    liquid=native_queued_draw(native_draw_order_[n]).mesh->has_liquid;
   if(!liquid)continue;
   bind_trace_stages(tessellate_terrain&&item_bucket.material_key.contact_map==0);
   ID3D11ShaderResourceView* geometry=native_geometry_page_srv(item.geometry_page);if(!geometry){result=E_FAIL;break;}
   context_->VSSetShaderResources(1,1,&geometry);
   ID3D11ShaderResourceView* contact_views[4];Constants layer=batch_constants(item,item_draw,item_bucket,contact_views);
   layer.liquid_layer=2u; // the liquid triangles only
   if(FAILED(result=upload_draw_constants(&layer,sizeof(layer))))break;
   previous_constants=layer;have_constants=true;
   apply_state(item_bucket.state,item_bucket.texture_srv!=nullptr,item_draw.hud_composition);
   context_->OMSetDepthStencilState(water_ssr_depth_state_.Get(),0);context_->OMSetBlendState(nullptr,nullptr,0xffffffffu);
   bind_batch_units(item,item_baked);
   context_->DrawInstancedIndirect(native_indirect_args_.Get(),static_cast<UINT>(index)*16u);
   profiler_.add_draw(item_baked?item_baked->triangles:0,static_cast<UINT>(item.draw_count));profiler_.add_batch();
  }
  {ID3D11Buffer* full=liquid_ripple_ready_?liquid_ripple_constants_.Get():nullptr;context_->PSSetConstantBuffers(8,1,&full);}
  if(SUCCEEDED(result)){
   // The march: one full-screen triangle over the half-resolution targets.
   ID3D11RenderTargetView* traced[2]={water_ssr_rtv_[2].Get(),water_ssr_rtv_[3].Get()};
   context_->OMSetRenderTargets(2,traced,nullptr);
   ID3D11ShaderResourceView* rays[2]={water_ssr_srv_[1].Get(),water_ssr_srv_[0].Get()}; // t31 water points, t32 rays
   context_->PSSetShaderResources(31,2,rays);
   ID3D11ShaderResourceView* scene[2]={water_scene_srv_.Get(),water_scene_depth_srv_.Get()};
   context_->PSSetShaderResources(3,1,&scene[0]);context_->PSSetShaderResources(19,1,&scene[1]);
   ID3D11ShaderResourceView* particle_depth=particle_depth_srv_[particle_depth_current_^1u].Get();
   context_->PSSetShaderResources(28,1,&particle_depth);
   ID3D11SamplerState* sampler=water_reflection_sampler_.Get();context_->PSSetSamplers(5,1,&sampler);
   context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
   context_->VSSetShader(native_shadow_mask_vs_.Get(),nullptr,0);
   context_->HSSetShader(nullptr,nullptr,0);context_->DSSetShader(nullptr,nullptr,0);
   context_->RSSetState(shadow_mask_raster_.Get());context_->OMSetBlendState(nullptr,nullptr,0xffffffffu);
   context_->PSSetShader(native_liquid_ssr_march_ps_.Get(),nullptr,0);
   context_->Draw(3,0);profiler_.add_draw(1,1);profiler_.add_batch();
   // The filter (nativeLiquidSsrFilterPS): the traced colour and key in,
   // the filtered pair (targets 4, 5) out.
   bool filtered=false;
   if(native_liquid_ssr_filter_ps_){
    ID3D11RenderTargetView* smoothed[2]={water_ssr_rtv_[4].Get(),water_ssr_rtv_[5].Get()};
    context_->OMSetRenderTargets(2,smoothed,nullptr);
    ID3D11ShaderResourceView* traced_views[3]={water_ssr_srv_[2].Get(),water_ssr_srv_[3].Get(),water_ssr_srv_[1].Get()};
    context_->PSSetShaderResources(29,3,traced_views);
    context_->PSSetShader(native_liquid_ssr_filter_ps_.Get(),nullptr,0);
    context_->Draw(3,0);profiler_.add_draw(1,1);profiler_.add_batch();
    filtered=true;
   }
   // The water bodies read the result: targets off first (a bound target's
   // views read as null).
   context_->OMSetRenderTargets(0,nullptr,nullptr);
   ID3D11ShaderResourceView* resolved[4]={water_ssr_srv_[filtered?4:2].Get(),water_ssr_srv_[filtered?5:3].Get(),water_ssr_srv_[1].Get(),nullptr};
   context_->PSSetShaderResources(29,4,resolved);
  }else context_->OMSetRenderTargets(0,nullptr,nullptr);
  // The current batch's stages and targets come back.
  context_->RSSetViewports(1,&viewport);
  context_->IASetPrimitiveTopology(tessellation_bound?D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST:D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context_->VSSetShader(tessellation_bound?terrain_tess_vs_.Get():native_vs_.Get(),nullptr,0);
  context_->HSSetShader(tessellation_bound?(snow_stages?terrain_hs_.Get():terrain_hs_plain_.Get()):nullptr,nullptr,0);
  context_->DSSetShader(tessellation_bound?(snow_stages?terrain_ds_.Get():terrain_ds_plain_.Get()):nullptr,nullptr,0);
  bound_color_targets=~0u;
  return result;
 };
 // Bindings that are the same for every batch: nothing in the loop rebinds
 // t22/t23, s6, t33 or s8 (trace_water binds t22/t23 and s6 to the same
 // views). A batch's contact views (t13-t16) are bound when they change.
 if(SUCCEEDED(ensure_liquid_normal_map())){
  ID3D11ShaderResourceView* textures[2]={liquid_normal_srv_.Get(),liquid_detail_srv_.Get()};ID3D11SamplerState* normal_sampler=liquid_normal_sampler_.Get();
  context_->PSSetShaderResources(22,2,textures);context_->PSSetSamplers(6,1,&normal_sampler);
 }
 bind_liquid_swamp_frame();
 {ID3D11SamplerState* contact_sampler=contact_sampler_?contact_sampler_.Get():gc_clamp_.Get();context_->PSSetSamplers(8,1,&contact_sampler);}
 ID3D11ShaderResourceView* bound_contact_views[4]{};bool contact_views_bound=false;
 for(std::size_t batch_index=0;batch_index<native_batch_staging_.size();++batch_index){const NativeBatch& batch=native_batch_staging_[batch_index];
  // A reused static bake has no queue: its per-batch totals stand in for the draw walks below.
  const auto* baked=static_reuse?&bake.batches[batch_index]:nullptr;
  const NativeQueuedDraw& draw=baked?native_instance_records_[baked->first_record].command:native_queued_draw(native_draw_order_[batch.first_draw]);const NativeStateBucket& bucket=native_state_buckets_[batch.state_bucket];const UINT group_draw_count=static_cast<UINT>(batch.draw_count);
  ID3D11ShaderResourceView* geometry=native_geometry_page_srv(batch.geometry_page);if(!geometry)return native_flush_failure_line_=__LINE__,finish(E_FAIL);context_->VSSetShaderResources(1,1,&geometry);
  ID3D11ShaderResourceView* contact_views[4];Constants constants=batch_constants(batch,draw,bucket,contact_views);const auto& st=bucket.state;
  // Drawn in this flush's prepass: depth EQUAL and the mask-reading variant.
  const bool masked=any_prepass&&native_prepass_batches_[batch_index]!=0;
  // Without a draw that can reach the ground (terrain never does), the
  // variants with the contact blend compiled out (layout contact bits from
  // the current contact grid and reach only).
  const bool contact=bucket.semantic_kind!=SemanticKind::terrain&&(batch.contact||layout.contact_generation!=native_contact_generation_);
  // Both texture stages in a fast mode: the variants without the general
  // combiner. Any other configuration draws through the general programs;
  // each is named once.
  const bool fast=constants.combiner[0]!=0&&constants.combiner[1]!=0;
  if(!fast){
   const std::uint64_t config=std::uint64_t(constants.color_op&255)|std::uint64_t(constants.color_arg1&255)<<8|std::uint64_t(constants.color_arg2&255)<<16|
    std::uint64_t(constants.alpha_op&255)<<24|std::uint64_t(constants.alpha_arg1&255)<<32|std::uint64_t(constants.alpha_arg2&255)<<40;
   if(native_general_combiners_.size()<64&&native_general_combiners_.insert(config).second){
    char text[128];std::snprintf(text,sizeof(text),"color=%u/%u/%u alpha=%u/%u/%u (general combiner programs)",
     constants.color_op,constants.color_arg1,constants.color_arg2,constants.alpha_op,constants.alpha_arg1,constants.alpha_arg2);
    bridge_report_event("native_general_combiner",text);
   }
  }
  const bool terrain_kind=bucket.semantic_kind==SemanticKind::terrain;
  // Only the batch kind's own program set is looked up.
  ID3D11PixelShader* const batch_object_ps=terrain_kind?nullptr:contact?(masked?(low_pixels?native_pixel(&Renderer::native_masked_low_ps_,true,fast):native_pixel(&Renderer::native_masked_ps_,true,fast)):
   low_pixels?native_pixel(&Renderer::native_low_ps_,true,fast):native_pixel(&Renderer::native_ps_,true,fast)):
   masked?(low_pixels?native_pixel(&Renderer::native_masked_low_ps_,false,fast):native_pixel(&Renderer::native_masked_ps_,false,fast)):
   low_pixels?native_pixel(&Renderer::native_low_ps_,false,fast):native_pixel(&Renderer::native_ps_,false,fast);
  // Terrain: the same choice as above, from the contact-free set.
  const auto terrain_variant=[&](bool flat){
   if(masked){
    if(snow_ps)return flat&&flat_variants?native_pixel(&Renderer::native_masked_snow_flat_ps_,false,fast):native_pixel(&Renderer::native_masked_snow_ps_,false,fast);
    if(low_pixels)return native_pixel(&Renderer::native_masked_low_ps_,false,fast);
    return flat&&flat_variants?native_pixel(&Renderer::native_masked_terrain_flat_ps_,false,fast):native_pixel(&Renderer::native_masked_terrain_ps_,false,fast);
   }
   if(snow_ps)return flat&&flat_variants?native_pixel(&Renderer::native_snow_flat_ps_,false,fast):native_pixel(&Renderer::native_snow_ps_,false,fast);
   if(low_pixels)return native_pixel(&Renderer::native_low_ps_,false,fast);
   return flat&&flat_variants?native_pixel(&Renderer::native_terrain_flat_ps_,false,fast):native_pixel(&Renderer::native_terrain_ps_,false,fast);
  };
  ID3D11PixelShader* const batch_terrain_ps=terrain_kind?terrain_variant(false):nullptr;
  ID3D11PixelShader* const batch_terrain_flat_ps=terrain_kind&&batch.flat_pixels?terrain_variant(true):batch_terrain_ps;
  if(!contact_views_bound||!std::equal(std::begin(contact_views),std::end(contact_views),bound_contact_views)){
   context_->PSSetShaderResources(13,4,contact_views);
   std::copy(std::begin(contact_views),std::end(contact_views),bound_contact_views);contact_views_bound=true;
  }
  if(!have_constants||!draw_constants_live()||std::memcmp(&previous_constants,&constants,sizeof(constants))!=0){if(FAILED(hr=upload_draw_constants(&constants,sizeof(constants))))return native_flush_failure_line_=__LINE__,finish(hr);previous_constants=constants;have_constants=true;}
  bool liquid_batch=baked&&baked->liquid,ground_part=baked&&baked->ground;
  std::uint8_t liquid_types=baked?baked->liquid_types:0;
  if(!baked&&bucket.semantic_kind==SemanticKind::terrain)
   for(std::size_t n=batch.first_draw;n<batch.first_draw+batch.draw_count;++n){
    const NativeMesh& mesh=*native_queued_draw(native_draw_order_[n]).mesh;
    liquid_batch|=mesh.has_liquid;ground_part|=mesh.has_ground;liquid_types|=mesh.liquid_types;
   }
  // Opaque world work after this frame's water scene copy is missing from
  // refraction (legs under water): report it once, with the batch kind.
  if(!liquid_batch&&!scene_composited_&&!water_late_draw_reported_&&water_capture_ready_&&
     water_capture_frame_==native_frame_index_&&bucket.opacity!=NativeOpacityClass::ordered){
   water_late_draw_reported_=true;
   bridge_append_probe_event("water_copy_before_opaque_draw",bucket.semantic_kind==SemanticKind::terrain?"terrain":
    draw.instance.lighting_enabled?"figure":"object");
  }
  // Stock-look water (water_simple prototype): liquid batches draw whole
  // through the ground variant (nativeLiquidLayer bit 3), no reflections.
  const bool stock_water=liquid_batch&&water_simple;
  const bool water_reflections=liquid_batch&&!stock_water&&prepare_water_reflections(st);
  if(liquid_batch&&!stock_water)bound_color_targets=~0u; // its scene copy may leave the targets unbound
  if(liquid_batch){++water_liquid_batches_;if(water_reflections)++water_reflection_batches_;}
  prepare_liquid_ripples(); // frame time also drives caustics on terrain under water
  if(water_reflections&&!water_ssr_done){
   if(FAILED(hr=trace_water()))return native_flush_failure_line_=__LINE__,finish(hr);
   // This batch's geometry page and constants again.
   context_->VSSetShaderResources(1,1,&geometry);
   if(FAILED(hr=upload_draw_constants(&constants,sizeof(constants))))return native_flush_failure_line_=__LINE__,finish(hr);
   previous_constants=constants;have_constants=true;
  }
  ID3D11DepthStencilView* depth_view=dsv_.Get();
  if(water_bound){ID3D11ShaderResourceView* empty=nullptr;context_->PSSetShaderResources(19,1,&empty);}
  const bool write_history=history_active&&!scene_composited_&&!hit_test_active_&&bucket.opacity!=NativeOpacityClass::ordered;
  const unsigned wanted_targets=(write_history?1u:0u)|(write_ambient?2u:0u);
  if(wanted_targets!=bound_color_targets||depth_view!=bound_depth_view){
   ID3D11RenderTargetView* targets[3]={rtv_.Get(),
    write_history?shadow_history_rtv_[shadow_history_current_index_].Get():nullptr,
    write_ambient?ao_ambient_rtv_.Get():nullptr};
   context_->OMSetRenderTargets(write_ambient?3u:write_history?2u:1u,targets,depth_view);
   bound_color_targets=wanted_targets;bound_depth_view=depth_view;
  }
  if(liquid_batch||water_bound) {
   ID3D11ShaderResourceView* water_color=water_reflections?water_scene_srv_.Get():nullptr;
   ID3D11ShaderResourceView* depth=water_reflections?water_scene_depth_srv_.Get():nullptr;
   ID3D11Buffer* values=water_reflections?water_reflection_constants_.Get():nullptr;
   ID3D11SamplerState* sampler=water_reflections?water_reflection_sampler_.Get():nullptr;
   // The previous frame's particle depth (the slot this frame does not write).
   ID3D11ShaderResourceView* particle_depth=water_reflections?particle_depth_srv_[particle_depth_current_^1u].Get():nullptr;
   context_->PSSetShaderResources(3,1,&water_color);context_->PSSetShaderResources(19,1,&depth);
   context_->PSSetShaderResources(28,1,&particle_depth);
   context_->PSSetConstantBuffers(7,1,&values);context_->PSSetSamplers(5,1,&sampler);
  }
  water_bound=water_reflections;
  const bool textured=bucket.texture_srv!=nullptr;if(FAILED(hr=apply_state(st,textured,draw.hud_composition,draw.camp_clip)))return native_flush_failure_line_=__LINE__,finish(hr);ID3D11ShaderResourceView* texture=bucket.texture_array?nullptr:bucket.texture_srv.Get();ID3D11ShaderResourceView* texture_array=bucket.texture_array?bucket.texture_srv.Get():nullptr;context_->PSSetShaderResources(0,1,&texture);context_->PSSetShaderResources(6,1,&texture_array);{ID3D11ShaderResourceView* material_profile[2]={materials_on?material_profile_srv(bucket.material_key.texture_backing):nullptr,materials_on?material_relief_srv(bucket.material_key.texture_backing):nullptr};context_->PSSetShaderResources(37,2,material_profile);}const char* native_pass=bucket.semantic_kind==SemanticKind::terrain?"gpu_driven_terrain":(draw.instance.lighting_enabled?"gpu_driven_figures":"gpu_driven_object_meshes");transition_profile_pass(liquid_batch?(water_reflections?"liquid_reflections":"liquid_surface"):native_pass);std::uint64_t logical_triangles=baked?baked->triangles:0;if(baked)perf_native_gpu_lit_draws_+=baked->lit_draws;const std::size_t next=batch.first_draw+batch.draw_count;for(std::size_t item=batch.first_draw;!baked&&item<next;++item){auto& queued=native_queued_draw(native_draw_order_[item]);logical_triangles+=queued.index_count/3u;queued.mesh->last_used_frame=native_frame_index_;if(queued.instance.lighting_enabled)++perf_native_gpu_lit_draws_;}profiler_.add_draw(logical_triangles,group_draw_count);profiler_.add_batch();
  // Terrain batches draw with a terrain pixel variant (with the snow trail
  // code while a trail field exists; the lean one on the Low path),
  // everything else with the lean shader; set per batch: a reflection pass
  // between batches may rebind it. Liquid triangles take nativeLiquidPS below.
  // Low: a batch whose every draw is unlit and ordered (the stock sky)
  // takes the unlit variant: no point lights, sun shadow or glow walk.
  bool unlit_batch=low_pixels&&native_low_unlit_ps_&&bucket.semantic_kind!=SemanticKind::terrain&&
   bucket.opacity==NativeOpacityClass::ordered;
  if(baked)unlit_batch=unlit_batch&&baked->unlit;
  for(std::size_t item=batch.first_draw;!baked&&unlit_batch&&item<next;++item)
   unlit_batch=!native_queued_draw(native_draw_order_[item]).instance.lighting_enabled;
  context_->PSSetShader(bucket.semantic_kind==SemanticKind::terrain?batch_terrain_ps:unlit_batch?native_pixel(&Renderer::native_low_unlit_ps_,contact,fast):batch_object_ps,nullptr,0);
  if(masked)context_->OMSetDepthStencilState(masked_depth_state_.Get(),0);
  // Cliff tessellation: terrain batches as 3-point patches (same vertex count).
  // Without a snow trail field the flat tile class (the first region of a
  // batch with flat_pixels) draws untessellated: its edges stay whole on
  // steep neighbours (nativeTessFactorsOf), so only the steep region takes
  // the stages. Liquid batches keep them.
  const auto bind_tessellation=[&](bool tessellate){
   if(tessellate==tessellation_bound)return;
   tessellation_bound=tessellate;
   context_->IASetPrimitiveTopology(tessellate?D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST:D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
   context_->VSSetShader(tessellate?terrain_tess_vs_.Get():native_vs_.Get(),nullptr,0);
   // No snow trail field: the stages without the trail code (exact: the
   // trail terms need nativeSnowTrailWindow.w > 0, set only with a field).
   const bool snow_stages=snow_terrain_srv_!=nullptr;
   context_->HSSetShader(tessellate?(snow_stages?terrain_hs_.Get():terrain_hs_plain_.Get()):nullptr,nullptr,0);
   context_->DSSetShader(tessellate?(snow_stages?terrain_ds_.Get():terrain_ds_plain_.Get()):nullptr,nullptr,0);
  };
  // Neighbour work is the material key's contact map (0 on the active map).
  const bool neighbour_batch=bucket.material_key.contact_map!=0;
  // Neighbour-map terrain, its liquids included, draws untessellated:
  // nativeTessVS returns a neighbour control point before the liquid and
  // ground setup (no slope normal, offset limit, flow weight or snow lift),
  // so every factor is 1, the domain adds no displacement and the stages
  // returned the input triangles. Zone3 c70: neighbour water paid 0.57 ms
  // for that pass-through.
  const bool tessellated_batch=tessellate_terrain&&bucket.semantic_kind==SemanticKind::terrain&&!neighbour_batch;
  // With a snow trail field the active map's flat region draws in two parts:
  // untessellated, then tessellated for only its triangles near an active
  // trail tile (nativeTriangleNearTrail; the trail displacement needs the
  // stages there). Neighbour maps take no trails: their flat region stays
  // untessellated.
  const bool trail_split=tessellated_batch&&batch.flat_pixels&&!liquid_batch&&snow_terrain_srv_&&!neighbour_batch;
  const bool flat_untessellated=tessellated_batch&&batch.flat_pixels&&!liquid_batch&&(!snow_terrain_srv_||neighbour_batch);
  bind_tessellation(tessellated_batch&&!flat_untessellated&&!trail_split);
  // Profiling only: GPU sub-scopes split this pass between draw groups
  // (consecutive same-named groups share one timestamp pair).
  const auto terrain_scope=[&](TerrainProfilePart part){
   if(!profiler_.pass_active())return;
   profiler_.sub_scope(terrain_profile_scope(neighbour_batch,tessellation_bound,part));
   profiler_.add_sub_draw(group_draw_count,logical_triangles);
  };
  if(bucket.semantic_kind!=SemanticKind::terrain&&profiler_.pass_active()){
   // Static: a retained static set flush (native_static_frame_); dynamic:
   // the per-frame queue (characters, items, effects meshes).
   static const char* const object_scopes[3][3]={{"static_opaque","static_cutout","static_ordered"},
    {"dynamic_opaque","dynamic_cutout","dynamic_ordered"},{"neighbour_opaque","neighbour_cutout","neighbour_ordered"}};
   profiler_.sub_scope(object_scopes[neighbour_batch?2u:native_static_frame_?0u:1u][static_cast<unsigned>(bucket.opacity)]);
   profiler_.add_sub_draw(group_draw_count,logical_triangles);
  }
  const auto layer_constants=[&](UINT liquid_layer){
   Constants layer=constants;layer.liquid_layer=liquid_layer;
   HRESULT result=upload_draw_constants(&layer,sizeof(layer));if(FAILED(result))return result;
   previous_constants=layer;have_constants=true;
   return S_OK;
  };
  if(!liquid_batch){
   // Terrain tile classes: a split batch draws its first region (flat-class
   // triangles) with the flat pixel variant, then its steep region from its
   // own arguments slot and triangle base with the full one. An unsplit
   // terrain batch whose tiles are all classified flat takes the flat
   // variant too.
   const bool terrain_batch=bucket.semantic_kind==SemanticKind::terrain;
   const bool split=terrain_batch&&batch.steep_args_slot!=UINT_MAX;
   ID3D11PixelShader* const first_ps=terrain_batch&&batch.flat_pixels?batch_terrain_flat_ps:batch_terrain_ps;
   for(UINT part=0;part<(split?2u:1u);++part){
    const UINT args_offset=part?batch.steep_args_slot*16u:static_cast<UINT>(batch_index)*16u;
    ID3D11PixelShader* const live_ps=part?batch_terrain_ps:first_ps;
    if(part){
     // The flat part's bake frame may have left its own targets bound.
     if(wanted_targets!=bound_color_targets||depth_view!=bound_depth_view){
      ID3D11RenderTargetView* targets[3]={rtv_.Get(),
       write_history?shadow_history_rtv_[shadow_history_current_index_].Get():nullptr,
       write_ambient?ao_ambient_rtv_.Get():nullptr};
      context_->OMSetRenderTargets(write_ambient?3u:write_history?2u:1u,targets,depth_view);
      bound_color_targets=wanted_targets;bound_depth_view=depth_view;
     }
     Constants steep=constants;steep.native_triangle_base=batch.steep_triangle_base;
     if(FAILED(hr=upload_draw_constants(&steep,sizeof(steep))))return native_flush_failure_line_=__LINE__,finish(hr);
     previous_constants=steep;have_constants=true;
     bind_tessellation(tessellated_batch); // the steep region
    }
    if(terrain_batch)context_->PSSetShader(live_ps,nullptr,0);
#ifdef EID3D11_EMBEDDED
    // Inspect the actual pixel-stage inputs once per world, at a settled
    // active-ground draw. Empty optional pools are expected; no state changes.
    if(terrain_batch&&!part&&!scene_composited_&&!bucket.material_key.contact_map&&tile_blend_ready_&&!contact_maps_.pending){
     static std::uint64_t reported_epoch=UINT64_MAX;
     const auto epoch=bridge_current_world_epoch();
     if(reported_epoch!=epoch){
      reported_epoch=epoch;
      char text[768];
      std::snprintf(text,sizeof(text),"epoch=%llu table_entry=%.0f maps=%u rows=%u styles=%llu ready=%016llx",
       static_cast<unsigned long long>(epoch),constants.layout_origin[3],terrain2_table_count_,tile_blend_rows_?1u:0u,
       static_cast<unsigned long long>(terrain2_map_.style_serial),static_cast<unsigned long long>(contact_maps_.ready));
      bool missing=constants.layout_origin[3]<=0||!terrain2_table_count_||!tile_blend_rows_||!terrain2_map_.style_serial;
      for(const UINT slot:{85u,86u,87u,88u,89u,90u,91u,92u,93u,119u,121u,127u}){
       ComPtr<ID3D11ShaderResourceView> view;context_->PSGetShaderResources(slot,1,&view);
       const bool required=slot==85u||slot==127u||(contact_maps_.pool>=0&&slot==86u+UINT(contact_maps_.pool));
       missing=missing||(required&&!view);
       char resource_text[96];
       if(!view)std::snprintf(resource_text,sizeof(resource_text)," t%u=null",slot);
       else{
        ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
        ComPtr<ID3D11Texture2D> texture;
        if(SUCCEEDED(resource.As(&texture))){
         D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
         std::snprintf(resource_text,sizeof(resource_text)," t%u=%ux%u/m%u/a%u/f%u",slot,d.Width,d.Height,d.MipLevels,d.ArraySize,unsigned(d.Format));
        }else{
         D3D11_SHADER_RESOURCE_VIEW_DESC d{};view->GetDesc(&d);
         std::snprintf(resource_text,sizeof(resource_text)," t%u=buffer/f%u",slot,unsigned(d.Format));
        }
       }
       const auto used=std::strlen(text);std::snprintf(text+used,sizeof(text)-used,"%s",resource_text);
      }
      bridge_report_event("terrain2_bindings",text);
      if(missing)embedded_report_render_failure("terrain2_bindings_missing",text);
     }
    }
#endif
    // The flat region of a split batch has its own scope (live_flat).
    const TerrainProfilePart live_part=split&&!part?TerrainProfilePart::live_flat:TerrainProfilePart::live;
    if(trail_split&&!part){
     // Untessellated with the triangles near an active trail tile collapsed
     // (layer bit 16), then tessellated with only those (bit 32).
     for(const UINT layer_bits:{16u,32u}){
      if(FAILED(hr=layer_constants(constants.liquid_layer|layer_bits)))return native_flush_failure_line_=__LINE__,finish(hr);
      bind_tessellation(layer_bits==32u);
      terrain_scope(live_part);
      context_->DrawInstancedIndirect(native_indirect_args_.Get(),args_offset);
     }
    }else{
     if(terrain_batch)terrain_scope(live_part);
     context_->DrawInstancedIndirect(native_indirect_args_.Get(),args_offset);
     // DLSS: posed records (units, animated figures) also write their own
     // motion; the next batch rebinds its targets.
     if(batch.posed&&bucket.opacity!=NativeOpacityClass::ordered&&draw_dlss_object_motion(args_offset))
      bound_color_targets=~0u;
    }
   }
  }
  else if(stock_water){
   // Stock-look water: one draw through the ground variant bound above;
   // the vertex stage gives liquid vertices the game's terrain outputs.
   if(FAILED(hr=layer_constants(8u)))return native_flush_failure_line_=__LINE__,finish(hr);
   terrain_scope(TerrainProfilePart::stock_water);
   context_->DrawInstancedIndirect(native_indirect_args_.Get(),static_cast<UINT>(batch_index)*16u);
  }
  else{
   // A liquid batch draws its visible triangles twice: the ground part
   // (other layer tiles) with the terrain pixel variant bound above, then
   // the liquid surfaces with nativeLiquidPS; the vertex stage collapses
   // the other part each time (nativeLiquidLayer bits 2 and 1).
   if(ground_part){
    if(FAILED(hr=layer_constants(4u)))return native_flush_failure_line_=__LINE__,finish(hr);
    terrain_scope(TerrainProfilePart::ground);
    context_->DrawInstancedIndirect(native_indirect_args_.Get(),static_cast<UINT>(batch_index)*16u);
   }
   // The water body's program: Low, or the batch has no reflection frame
   // bound (reflections and physical water read as off: the same program);
   // without physical water (the frame's w, water_physical); else full.
   // Batches whose liquid vertices are all water, or all lava, take that
   // type's program; mixed batches (swamp, lava with water) the combined one.
   const bool physical_water=water_physical;
   // Each mix of liquid types drawn with the combined program is named once
   // (swamp, or lava with water in one batch).
   if(liquid_types!=1u&&liquid_types!=2u&&!(native_liquid_types_reported_&(1u<<(liquid_types&7u)))){
    native_liquid_types_reported_|=1u<<(liquid_types&7u);
    char text[96];std::snprintf(text,sizeof(text),"types=%s%s%s (combined liquid program)",(liquid_types&1u)?"water ":"",(liquid_types&2u)?"lava ":"",(liquid_types&4u)?"swamp":"");
    bridge_report_event("liquid_batch_types",text);
   }
   ID3D11PixelShader* const liquid_ps=liquid_types==2u&&native_lava_ps_?native_lava_ps_.Get():
    liquid_types==1u&&native_water_ps_?(low_pixels||!water_reflections?native_low_water_ps_.Get():!physical_water?native_water_ssr_ps_.Get():native_water_ps_.Get()):
    low_pixels||!water_reflections?native_low_liquid_ps_.Get():!physical_water?native_liquid_ssr_ps_.Get():native_liquid_ps_.Get();
   context_->PSSetShader(liquid_ps,nullptr,0);
   bind_batch_units(batch,baked);
   if(FAILED(hr=layer_constants(2u)))return native_flush_failure_line_=__LINE__,finish(hr);
   terrain_scope(TerrainProfilePart::water);
   context_->DrawInstancedIndirect(native_indirect_args_.Get(),static_cast<UINT>(batch_index)*16u);
   // Foam layer (liquid_surface.hlsl liquidFoamLayer): the liquid triangles
   // again with nativeLiquidLayer bit 0 set, over the water body just
   // drawn. The vertex stage collapses every triangle without foam.
   // It tests depth but never writes it: the kept triangles stand off the
   // water (liquidFoamLayerLift) and also carry transparent foam, so their
   // depth hid a strip of the next liquid batch's water body along every
   // shared sector border of running water (the bed showed through).
   FixedState foam_state=st;foam_state.render[D3DRENDERSTATE_ZWRITEENABLE]=FALSE;apply_state(foam_state,textured);
   context_->PSSetShader(native_liquid_foam_ps_.Get(),nullptr,0);
   if(FAILED(hr=layer_constants(3u)))return native_flush_failure_line_=__LINE__,finish(hr);
   terrain_scope(TerrainProfilePart::foam);
   context_->DrawInstancedIndirect(native_indirect_args_.Get(),static_cast<UINT>(batch_index)*16u);
   {ID3D11Buffer* full=liquid_ripple_ready_?liquid_ripple_constants_.Get():nullptr;context_->PSSetConstantBuffers(8,1,&full);}
  }
  if(write_history)shadow_history_written_=true;perf_native_draws_+=group_draw_count;++perf_native_batches_;perf_native_vertices_+=logical_triangles*3u;++native_frame_stats_.native_physical_batches;if(group_draw_count==1)++native_frame_stats_.native_one_draw_batches;native_frame_stats_.native_max_batch_draws=std::max<std::uint64_t>(native_frame_stats_.native_max_batch_draws,group_draw_count);}
 // Fur prototype (renderer_fur.cpp): this flush's unit records with fur,
 // while their world vertices and the flush's lighting are still bound.
 draw_fur_records();
 ID3D11ShaderResourceView* null_texture=nullptr;context_->PSSetShaderResources(0,1,&null_texture);context_->PSSetShaderResources(6,1,&null_texture);context_->PSSetShaderResources(37,1,&null_texture);context_->PSSetShaderResources(38,1,&null_texture);context_->VSSetShaderResources(1,8,null_srvs);trim_material_profiles();
 if(water_ssr_done){ID3D11ShaderResourceView* none[4]{};context_->PSSetShaderResources(29,4,none);}context_->VSSetConstantBuffers(1,1,&null_constant);end_profile_pass();return native_flush_failure_line_=__LINE__,finish(S_OK);
}

} // namespace owned
