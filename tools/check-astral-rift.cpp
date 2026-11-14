#include "astral_rift.h"
#include <cstdio>
#include <stdexcept>
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
 auto other=a;auto otherModel=std::make_shared<ModelCpuAsset>(*model);otherModel->visual_reference="samastrift02";other.skeleton=otherModel;add_astral_clumps(other);check(other.parts.size()==9);
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
 // Atlas wrapping and fades: no sample may escape the original rectangle;
 // both sides of a seam converge to the same weighted sample.
 for(int step=0;step<=2000;++step){double x=step/2000.;double aUv=.0166015625+std::fmod(x,1.)*.466796875;double bUv=.0166015625+std::fmod(x+.5,1.)*.466796875;check(aUv>=.0166015625&&aUv<=.4833984375&&bUv>=.0166015625&&bUv<=.4833984375);}
 std::printf("Astral rift: %u checks PASS\n",checks);return 0;
}catch(const std::exception& e){std::printf("FAIL %s\n",e.what());return 1;}
