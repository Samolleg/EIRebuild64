#pragma once
#include <fstream>
#include "astral_neighbour.h"

inline void check_astral_border_mesh(const char* fixture,const char* objPath) {
 using namespace owned;
 const auto require=[](bool value){if(!value)throw std::runtime_error("Actual border mesh regression");};
 std::ifstream input(fixture,std::ios::binary);require(bool(input));
 const auto read=[&](void* data,std::size_t size){input.read(static_cast<char*>(data),size);require(bool(input));};
 std::uint32_t signature,header[9];read(&signature,4);read(header,sizeof(header));
 require(signature==0x31474946 && header[0]==272 && header[1]==2646 && header[2]==626 && header[3]==10584);
 auto f=std::make_shared<FigureCpuAsset>();f->signature=signature;f->shape_count=1;f->position_groups=header[0];f->normal_groups=header[1];
 f->material_group=header[7];f->texture_number=header[8];f->bounds.resize(1);read(&f->bounds[0],40);
 f->positions.resize(header[0]);for(auto& p:f->positions){read(p.x,16);read(p.y,16);read(p.z,16);for(auto& w:p.w)w=1;}
 f->normals.resize(header[1]);read(f->normals.data(),f->normals.size()*sizeof(NativePackedGroup));
 f->texture_coordinates.resize(header[2]);read(f->texture_coordinates.data(),header[2]*sizeof(NativeTextureCoordinate));
 f->indices.resize(header[3]);read(f->indices.data(),header[3]*2);
 f->components.resize(header[4]);read(f->components.data(),header[4]*sizeof(NativeVertexComponent));
 f->morph_components.resize(header[5]);read(f->morph_components.data(),header[5]*sizeof(FigureMorphComponent));
 const auto closed=close_astral_border_cuts(f);require(closed!=f);
 require(closed->indices.size()==f->indices.size()+33*12); // two triangles, both windings per missing cell
 require(closed->positions.size()==f->positions.size()&&std::memcmp(closed->positions.data(),f->positions.data(),f->positions.size()*sizeof(NativePackedGroup))==0);
 require(std::memcmp(closed->normals.data(),f->normals.data(),f->normals.size()*sizeof(NativePackedGroup))==0);
 require(closed->texture_coordinates.size()==f->texture_coordinates.size()&&std::memcmp(closed->texture_coordinates.data(),f->texture_coordinates.data(),f->texture_coordinates.size()*sizeof(NativeTextureCoordinate))==0);
 require(std::equal(f->indices.begin(),f->indices.end(),closed->indices.begin()));
 require(std::memcmp(closed->components.data(),f->components.data(),f->components.size()*sizeof(NativeVertexComponent))==0);
 require(std::memcmp(closed->bounds.data(),f->bounds.data(),40)==0);
 std::set<std::array<unsigned,3>> triangles;
 for(unsigned i=0;i<closed->indices.size();i+=3){std::array<unsigned,3> t;
  for(unsigned k=0;k<3;++k){const auto ix=closed->indices[i+k];require(ix<closed->components.size());const auto& c=closed->components[ix];require(c.normal<closed->normals.size()*4&&c.texture<closed->texture_coordinates.size());t[k]=c.position;if(i>=f->indices.size())require(c.position<413);}
  std::sort(t.begin(),t.end());if(t[2]<413)triangles.insert(t);
 }
 require(triangles.size()==58*6*2);require(close_astral_border_cuts(closed)==closed);
 auto malformed=std::make_shared<FigureCpuAsset>(*f);malformed->positions[0].x[0]+=1;require(close_astral_border_cuts(malformed)==malformed);
 auto skeleton=std::make_shared<ModelCpuAsset>();skeleton->visual_reference="samastrborder01";
 auto assembly=std::make_shared<VisualAssemblyAsset>();assembly->skeleton=skeleton;
 VisualPartAsset part;part.name="EMPTY_BoundaryVeils";part.geometry=f;assembly->parts.push_back(part);
 auto originalEmitter=*assembly;auto referenceSkeleton=std::make_shared<ModelCpuAsset>();referenceSkeleton->visual_reference="samastrift02";originalEmitter.skeleton=referenceSkeleton;originalEmitter.parts[0].name="EMPTY_Veils";
 add_astral_clumps(*assembly);add_astral_clumps(originalEmitter);
 const auto& actual=*assembly->parts.back().geometry;const auto& expected=*originalEmitter.parts.back().geometry;
 require(actual.positions.size()==expected.positions.size()&&std::memcmp(actual.positions.data(),expected.positions.data(),actual.positions.size()*sizeof(NativePackedGroup))==0);
 require(actual.indices==expected.indices);require(assembly->parts[0].geometry!=f);
 RetainedStaticSourceCandidate candidate;candidate.assembly=assembly;RetainedPreparedNode node;node.figure=f.get();node.positions=f->positions;node.normals=f->normals;candidate.nodes.push_back(node);
 require(prepare_neighbour_astral_nodes(candidate));require(candidate.nodes[0].figure==assembly->parts[0].geometry.get());require(candidate.nodes[0].normals.size()==closed->normals.size());require(candidate.nodes.size()==2);
 const auto writeObj=[&](const FigureCpuAsset& g,const std::string& name){
  std::ofstream obj(name);require(bool(obj));
  for(const auto& c:g.components){const auto& p=g.positions[c.position/4];auto k=c.position%4;obj<<"v "<<p.x[k]<<' '<<p.y[k]<<' '<<p.z[k]<<'\n';const auto& uv=g.texture_coordinates[c.texture];obj<<"vt "<<uv.u<<' '<<uv.v<<'\n';}
  for(unsigned i=0;i<g.indices.size();i+=3){obj<<"f";for(unsigned k=0;k<3;++k){unsigned j=g.indices[i+k]+1;obj<<' '<<j<<'/'<<j;}obj<<'\n';}
 };
 writeObj(*f,std::string(objPath)+".original.obj");writeObj(*closed,objPath);
 std::printf("Actual FIG: 33 grid cuts closed (132 two-sided triangles); all original positions/UV/triangles and emitter positions unchanged; neighbour geometry PASS\n");
}
