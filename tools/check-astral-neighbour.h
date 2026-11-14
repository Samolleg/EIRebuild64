#pragma once
#include "astral_neighbour.h"

static void check_astral_neighbour() {
 using namespace owned;
 for(unsigned model=1;model<=6;++model) {
  auto assembly=std::make_shared<VisualAssemblyAsset>();
  auto skeleton=std::make_shared<ModelCpuAsset>();assembly->skeleton=skeleton;
  skeleton->visual_reference=model==5?"samastrborder01":model==6?"unrelated":"samastrift0"+std::to_string(model);
  auto texture=std::make_shared<TextureCpuAsset>();
  auto geometry=std::make_shared<FigureCpuAsset>();geometry->shape_count=1;
  geometry->position_groups=geometry->normal_groups=1;
  geometry->positions.resize(1);geometry->normals.resize(1);geometry->bounds.resize(1);
  geometry->indices={0,1,2};geometry->components={{0,0,0},{1,1,1},{2,2,2}};
  geometry->texture_coordinates={{.02f,.52f},{.48f,.52f},{.48f,.98f}};
  const std::vector<std::string> names=model==1?
   std::vector<std::string>{"stone","empty_veillow","empty_veilcrown","empty_veilcross","empty_veilwake"}:
   std::vector<std::string>{"stone",model==5?"empty_boundaryveils":"empty_veils"};
  RetainedStaticSourceCandidate candidate;
  for(unsigned i=0;i<names.size();++i) {
   VisualPartAsset part;part.name=names[i];part.geometry=geometry;part.texture=texture;part.skeleton_node=i;
   assembly->parts.push_back(part);
   RetainedPreparedNode node;node.figure=geometry.get();node.texture=texture.get();node.texture_identity=123;
   node.object_to_world={1,0,0,float(i*10),0,1,0,20,0,0,1,30,0,0,0,1};
   node.object_local=node.object_to_world;node.bounds.minimum={-1,-2,-3};node.bounds.maximum={1,2,3};node.bounds.radius=4;
   candidate.nodes.push_back(node);
  }
  const auto originals=candidate.nodes;
  add_astral_clumps(*assembly);candidate.assembly=assembly;
  check(prepare_neighbour_astral_nodes(candidate));
  check(candidate.nodes.size()==assembly->parts.size());
  check(candidate.nodes.size()==names.size()+(model==6?0:model==1?4:1));
  for(unsigned i=0;i<candidate.nodes.size();++i) {
   const auto& part=assembly->parts[i];const auto& node=candidate.nodes[i];
   const auto source=part.skeleton_node;
   check(node.object_to_world==originals[source].object_to_world);
   check(node.object_local==originals[source].object_local);
   check(node.figure==part.geometry.get());
   if(astral_dynamic(*assembly,part)) {
    const float margin=astral_clumps(*assembly,part)?7.f:.5f;
    check(node.bounds.minimum[0]==-1-margin&&node.bounds.maximum[2]==3+margin);
   } else check(node.bounds.minimum==originals[source].bounds.minimum);
   if(astral_border_veil(*assembly,part)) {
    check(node.texture==astral_border_plain_texture().get());
    check(node.texture_identity==node.texture->value_serial);
    check(node.texture_format==NativeOwnedTextureFormat::argb8888);
    check(node.texture_mips.size()==1&&node.texture_mips[0].data[3]==48);
   } else {check(node.texture==texture.get());check(node.texture_identity==123);}
   if(astral_clumps(*assembly,part))check(node.positions.size()==part.geometry->positions.size());
  }
 }
 NativePreparedPartValues base; base.alpha=.8f;base.default_diffuse=0x80443322;base.astral={0,1,1,0};
 NativePreparedPartValues first,paused,later,period;
 check(neighbour_astral_values(0,base,true,first));
 check(neighbour_astral_values(0,base,true,paused));
 check(first.astral==paused.astral&&first.alpha==paused.alpha&&first.default_diffuse==paused.default_diffuse);
 check(neighbour_astral_values(1.5/.055,base,true,later));
 check(later.astral[0]>.2499f&&later.astral[0]<.2501f&&later.alpha>first.alpha);
 check(neighbour_astral_values(6/.055,base,true,period));
 check(std::abs(first.alpha-period.alpha)<1e-6f&&period.astral[0]==0);
 NativePreparedPartValues active=base;astral_border_opacity(1.5/.055,active);
 check(active.alpha==later.alpha&&active.default_diffuse==later.default_diffuse);
 check(!neighbour_astral_values(-1,base,true,later));
 check(!neighbour_astral_values(NAN,base,true,later));
 check(neighbour_astral_values(1.5/.055,base,false,later)&&later.alpha==base.alpha);
}
