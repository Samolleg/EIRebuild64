#include "retained_static_scene.h"
// The game host still exposes Windows' max macro, unlike the renderer PCH.
#define max(a,b) WINDOWS_MAX_MUST_NOT_EXPAND(a,b)
#include "astral_rift.h"
#undef max
#include <cstdio>
#include <stdexcept>
#include <cstring>
#include "check-astral-alpha-gpu.h"
static unsigned checks=0;
static void check(bool b){++checks;if(!b)throw std::runtime_error("check "+std::to_string(checks));}
int main() try {
 using namespace owned;
 VisualAssemblyAsset a;auto model=std::make_shared<ModelCpuAsset>();a.skeleton=model;
 model->visual_reference="samastrift01";
 const char* names[]={"empty_rift","empty_slabs","empty_fragments","empty_corefar","empty_corenear","empty_veillow","empty_veilcrown","empty_veilcross","empty_veilwake"};
 auto f=std::make_shared<FigureCpuAsset>();f->shape_count=1;f->position_groups=1;f->normal_groups=1;
 f->positions.resize(1);f->normals.resize(1);f->indices={0,1,2};
 for(unsigned k=0;k<4;++k){f->positions[0].x[k]=float(k);f->positions[0].y[k]=2;f->positions[0].z[k]=10;f->positions[0].w[k]=1;f->components.push_back({(uint16_t)k,(uint16_t)k,(uint16_t)k});f->texture_coordinates.push_back({.0166015625f+float(k)/3*.466796875f,.5166015625f});}
 for(auto name:names){VisualPartAsset p;p.name=name;p.geometry=f;p.skeleton_node=2;a.parts.push_back(p);}
 for(unsigned k=0;k<9;++k)check(astral_part(a,a.parts[k])==(k<5?0:k-4));
 auto other=a;auto otherModel=std::make_shared<ModelCpuAsset>(*model);otherModel->visual_reference="samastrift05";other.skeleton=otherModel;add_astral_clumps(other);check(other.parts.size()==9);
 add_astral_clumps(a);check(a.parts.size()==13);
 for(unsigned k=0;k<9;++k){check(a.parts[k].geometry==f);check(a.parts[k].skeleton_node==2);if(k<5)check(a.parts[k].casts_shadow);}
 for(unsigned k=9;k<13;++k){const auto& p=a.parts[k];check(!p.casts_shadow&&astral_clumps(a,p));check(p.skeleton_node==2);const auto& g=*p.geometry;check(g.components.size()==64);check(g.indices.size()==192);check(g.positions.size()==16);check(g.identity.canonical_name!=f->identity.canonical_name);
  for(auto i:g.indices)check(i<g.components.size());
  for(const auto& c:g.components)check(c.position<g.position_groups*4&&c.texture<g.texture_coordinates.size());
  for(const auto& v:g.positions)for(unsigned i=0;i<4;++i)check(std::isfinite(v.x[i])&&std::isfinite(v.y[i])&&std::isfinite(v.z[i]));
 }
 add_astral_clumps(a);check(a.parts.size()==13);
 auto hidden=other;hidden.skeleton=model;for(auto& p:hidden.parts)p.explicitly_hidden=true;add_astral_clumps(hidden);check(hidden.parts.size()==13);for(unsigned i=9;i<13;++i)check(hidden.parts[i].explicitly_hidden);
 model->visual_reference="SAMASTRIFT01.MOD";check(astral_model(a));
 model->visual_reference="samastrift01_extra";check(!astral_model(a));
 model->visual_reference="samastrborder02";check(!astral_model(a));
 model->visual_reference="samastrborder01_extra";check(!astral_model(a));
 // Installed 02/04 have four disconnected ribbons; 03 has two. Identical
 // atlas UV on every island must not concentrate all births on the first.
 for(unsigned modelNumber=2;modelNumber<=5;++modelNumber) {
  const bool border=modelNumber==5;
  auto originalAtlas=std::make_shared<TextureCpuAsset>();originalAtlas->source_name="samastrtex01";
  VisualAssemblyAsset family;auto skeleton=std::make_shared<ModelCpuAsset>();family.skeleton=skeleton;
  skeleton->visual_reference=border?"samastrborder01":"samastrift0"+std::to_string(modelNumber);
  const unsigned islands=modelNumber==3?2:4;
  auto ribbons=std::make_shared<FigureCpuAsset>();ribbons->shape_count=1;ribbons->position_groups=ribbons->normal_groups=islands;
  ribbons->positions.resize(islands);ribbons->normals.resize(islands);
  for(unsigned b=0;b<islands;++b){
   for(unsigned v=0;v<4;++v){auto& p=ribbons->positions[b];p.x[v]=float(b*10+v);p.y[v]=2;p.z[v]=10;p.w[v]=1;const auto ix=static_cast<uint16_t>(b*4+v);ribbons->components.push_back({ix,ix,ix});ribbons->texture_coordinates.push_back({v%2?.482421875f:.017578125f,v/2?.982421875f:.517578125f});}
   for(unsigned v:{0u,1u,2u,1u,3u,2u})ribbons->indices.push_back(static_cast<uint16_t>(b*4+v));
  }
  const std::vector<const char*> partNames=border?
   std::vector<const char*>{"EMPTY_Bank","EMPTY_Threshold","EMPTY_AstralSlates","EMPTY_AstralDepth","EMPTY_Veils","EMPTY_BoundaryVeils"}:
   std::vector<const char*>{"EMPTY_Rift","EMPTY_MajorSlabs","EMPTY_Debris","EMPTY_AstralDepth","EMPTY_AstralFilaments","EMPTY_Veils"};
  for(const auto* name:partNames){VisualPartAsset part;part.name=name;part.geometry=ribbons;part.texture=originalAtlas;part.skeleton_node=2;family.parts.push_back(part);}
  for(unsigned i=0;i<6;++i){check(astral_part(family,family.parts[i])==(i==5?1:0));check(astral_border_veil(family,family.parts[i])==(border&&i==5));}
  check(astral_ribbons(family.parts[5]).size()==islands);
  add_astral_clumps(family);check(family.parts.size()==7);const auto& generated=*family.parts.back().geometry;
  check(generated.components.size()==islands*64);check(generated.indices.size()==islands*192);
  for(unsigned b=0;b<islands;++b)for(unsigned k=0;k<8;++k){
   const auto& p=generated.positions[b*16+k*2];float center=0;for(unsigned v=0;v<4;++v)center+=p.x[v]*.25f;
   check(center>=b*10-.001f&&center<=b*10+3.001f);
   check(generated.texture_coordinates[b*64+k*8].u==float((b*8+k)*2));
  }
  for(unsigned i=0;i<6;++i){check(family.parts[i].geometry==ribbons);if(i<5)check(family.parts[i].casts_shadow);}
  for(unsigned i=0;i<6;++i)check(family.parts[i].texture==(border&&i==5?astral_border_plain_texture():originalAtlas));
  check(family.parts.back().texture==originalAtlas); // Clump atlas must not become a flat opaque square.
  if(border){const auto tex=family.parts[5].texture;check(tex->width==1&&tex->height==1&&tex->mips.size()==1);check(*tex->source==std::vector<std::uint8_t>({233,171,189,48}));check(tex->opaque_mips==0);}
  add_astral_clumps(family);check(family.parts.size()==7);
  skeleton->visual_reference=border?"SAMASTRBORDER01.MOD":"SAMASTRIFT0"+std::to_string(modelNumber)+".MOD";check(astral_model(family));
  check(astral_part(family,family.parts[5])==1);
  if(!border){VisualPartAsset wrongBorder;wrongBorder.name="EMPTY_BoundaryVeils";check(astral_part(family,wrongBorder)==0);}
  auto hiddenFamily=family;hiddenFamily.parts.resize(6);for(auto& part:hiddenFamily.parts)part.explicitly_hidden=true;
  add_astral_clumps(hiddenFamily);check(hiddenFamily.parts.size()==7&&hiddenFamily.parts.back().explicitly_hidden);
  VisualPartAsset wrong;wrong.name="EMPTY_VeilLow";check(astral_part(family,wrong)==0);
  skeleton->visual_reference+=".bak";check(!astral_model(family));
 }
 // Atlas wrapping and fades: no sample may escape the original rectangle;
 const double textureAlpha=(*astral_border_plain_texture()->source)[3]/255.0;
 for(unsigned frame=0;frame<=600;++frame){
  const double time=frame/100.0/.055;NativePreparedPartValues lit,unlit,hidden,next;
  lit.alpha=1;lit.lighting_enabled=1;unlit.alpha=1;unlit.default_diffuse=0x80443322;hidden.alpha=0;hidden.default_diffuse=0x00443322;next=lit;
  astral_border_opacity(time,lit);astral_border_opacity(time,unlit);astral_border_opacity(time,hidden);astral_border_opacity(time+6/.055,next);
  check(lit.alpha>=.6999f&&lit.alpha<=1.0001f);check(std::abs(lit.alpha-next.alpha)<1e-6f);
  check(textureAlpha*lit.alpha<.19);check(std::pow(1-textureAlpha*lit.alpha,4)>.43);
  check((unlit.default_diffuse&0xffffff)==0x443322);check((unlit.default_diffuse>>24)<=128);
  check(hidden.alpha==0&&(hidden.default_diffuse>>24)==0);
 }
 // both sides of a seam converge to the same weighted sample.
 for(int step=0;step<=2000;++step){double x=step/2000.;double aUv=.0166015625+std::fmod(x,1.)*.466796875;double bUv=.0166015625+std::fmod(x+.5,1.)*.466796875;check(aUv>=.0166015625&&aUv<=.4833984375&&bUv>=.0166015625&&bUv<=.4833984375);}
 check_astral_alpha_gpu();
 std::printf("Astral rift: %u checks PASS\n",checks);return 0;
}catch(const std::exception& e){std::printf("FAIL %s\n",e.what());return 1;}
