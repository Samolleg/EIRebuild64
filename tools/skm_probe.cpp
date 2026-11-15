// Offline check of the exact production SKM decoder/sampler. Binary output is
// private validation data, never a game resource or procedural pose source.
#include "../external/owned-renderer/Source/skm_asset.h"
#include <fstream>
#include <iostream>
#include <filesystem>
static std::vector<std::uint8_t> read(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("open "+p.string());return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv)try{
 if(argc<2)throw std::runtime_error("skm_probe directory [clip phase output]");
 const std::filesystem::path path=argv[1];auto asset=owned::skm::decode(read(path/"unhufe.skm"));
 if(argc==5){const std::string name=argv[2];auto reference=asset.contexts[0].clips.at(name);auto bytes=read(path/reference.resource);if(owned::skm::hash(bytes)!=reference.hash)throw std::runtime_error("hash");auto clip=owned::skm::decode_clip(bytes,asset,name);auto pose=owned::skm::evaluate(asset,clip,std::stod(argv[3]));std::ofstream out(argv[4],std::ios::binary);for(auto&m:pose.native)out.write(reinterpret_cast<const char*>(m.data()),128);for(auto&m:pose.mounts)out.write(reinterpret_cast<const char*>(m.data()),128);out.write(reinterpret_cast<const char*>(pose.root.data()),128);return !out;}
 std::size_t count=0,frames=0;double radius=0;
 for(const auto&context:asset.contexts)for(const auto&entry:context.clips){auto bytes=read(path/entry.second.resource);if(owned::skm::hash(bytes)!=entry.second.hash)throw std::runtime_error("hash");auto clip=owned::skm::decode_clip(bytes,asset,entry.first);frames+=clip.phases.size();
  for(double phase:{0.,double(clip.phases.back())*.5,double(clip.phases.back())}){auto p=owned::skm::evaluate(asset,clip,phase);for(const auto&m:p.native){double r=std::sqrt(m[3]*m[3]+m[7]*m[7]+m[11]*m[11]);radius=(std::max)(radius,r);}}
  // The loader rejects truncation and identity mismatches instead of using a
  // different clip or the old ANM in their place.
  bytes.pop_back();bool rejected=false;try{owned::skm::decode_clip(bytes,asset,entry.first);}catch(const std::exception&){rejected=true;}if(!rejected)throw std::runtime_error("truncation accepted");++count;
 }
 std::cout<<"clips="<<count<<" frames="<<frames<<" nodes="<<asset.nodes.size()<<" sampled_joint_radius="<<radius<<" truncation_rejected="<<count<<"\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}
