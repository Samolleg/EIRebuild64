// Offline check of the exact production SKM decoder/sampler. Binary output is
// private validation data, never a game resource or procedural pose source.
#include "../external/owned-renderer/Source/skm_asset.h"
#include "../external/owned-renderer/Source/skn_asset.h"
#include <fstream>
#include <iostream>
#include <filesystem>
static std::vector<std::uint8_t> read(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("open "+p.string());return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv)try{
 if(argc<2)throw std::runtime_error("skm_probe directory [clip phase output]");
 const std::filesystem::path path=argv[1];auto asset=owned::skm::decode(read(path/"unhufe.skm"));
 if(argc==5){const std::string name=argv[2];auto reference=asset.contexts[0].clips.at(name);auto bytes=read(path/reference.resource);if(owned::skm::hash(bytes)!=reference.hash)throw std::runtime_error("hash");auto clip=owned::skm::decode_clip(bytes,asset,name);auto pose=owned::skm::evaluate(asset,clip,std::stod(argv[3]));std::ofstream out(argv[4],std::ios::binary);for(auto&m:pose.native)out.write(reinterpret_cast<const char*>(m.data()),128);for(auto&m:pose.mounts)out.write(reinterpret_cast<const char*>(m.data()),128);out.write(reinterpret_cast<const char*>(pose.root.data()),128);return !out;}
 std::size_t count=0,frames=0;double radius=0;
 owned::skn::Asset mesh;std::string failure;
 if(!owned::skn::decode(read(path.parent_path()/"hero-overlay/unhufe.hero.skn"),mesh,failure))throw std::runtime_error(failure);
 std::vector<double> spheres(mesh.joints.size(),-1);std::vector<int> author(mesh.joints.size(),-1),mount(mesh.joints.size(),-1);
 for(unsigned j=0;j<mesh.joints.size();++j){for(unsigned i=0;i<asset.nodes.size();++i)if(mesh.joints[j].name==asset.nodes[i].name)author[j]=int(i);for(unsigned i=0;i<asset.mounts.size();++i)if(mesh.joints[j].name==asset.mounts[i].name)mount[j]=int(i);}
 for(const auto&v:mesh.vertices)for(unsigned k=0;k<4;++k)if(v.weights[k]){auto p=owned::skn::point(mesh.joints[v.joints[k]].inverse,v.position);double r=std::sqrt(double(p[0])*p[0]+double(p[1])*p[1]+double(p[2])*p[2]);spheres[v.joints[k]]=(std::max)(spheres[v.joints[k]],r);}
 for(const auto&context:asset.contexts)for(const auto&entry:context.clips){auto bytes=read(path/entry.second.resource);if(owned::skm::hash(bytes)!=entry.second.hash)throw std::runtime_error("hash");auto clip=owned::skm::decode_clip(bytes,asset,entry.first);frames+=clip.phases.size();
  for(double phase:clip.phases){auto p=owned::skm::evaluate(asset,clip,phase);for(unsigned j=0;j<spheres.size();++j)if(spheres[j]>=0){if(author[j]<0&&mount[j]<0)throw std::runtime_error("weighted joint missing from bake");const auto&m=author[j]>=0?p.native[author[j]]:p.mounts[mount[j]];double linear=0;for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)linear+=m[row*4+col]*m[row*4+col];double r=std::sqrt(m[3]*m[3]+m[7]*m[7]+m[11]*m[11])+std::sqrt(linear)*spheres[j]+.1;radius=(std::max)(radius,r);}}
  // The loader rejects truncation and identity mismatches instead of using a
  // different clip or the old ANM in their place.
  bytes.pop_back();bool rejected=false;try{owned::skm::decode_clip(bytes,asset,entry.first);}catch(const std::exception&){rejected=true;}if(!rejected)throw std::runtime_error("truncation accepted");++count;
 }
 if(radius>asset.radius)throw std::runtime_error("cooked conservative bound exceeded");
 std::cout<<"clips="<<count<<" frames="<<frames<<" nodes="<<asset.nodes.size()<<" all_key_surface_bound="<<radius<<" truncation_rejected="<<count<<"\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}
