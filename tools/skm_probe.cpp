// Native NLC/NLL decoder and production sampler verification. No game writes.
#include "../external/owned-renderer/Source/nlc_asset.h"
#include "../external/owned-renderer/Source/skn_asset.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <chrono>
static owned::skm::Bytes read(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("open "+p.string());return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv)try{
 using namespace owned::skm;
 if(argc!=4)throw std::runtime_error("skm_probe runtime-directory requests.txt poses.bin");
 const std::filesystem::path path=argv[1];auto start=std::chrono::steady_clock::now();
 const auto loader=[&](const std::string&name){return read(path/name);};
 auto descriptor=read(path/"unhufe.hero.nlm"),meshbytes=read(path/"unhufe.hero.skn");
 auto asset=decode_layered(descriptor,meshbytes,loader);
 auto loaded=std::chrono::steady_clock::now();
 owned::skn::Asset mesh;std::string failure;if(!owned::skn::decode(meshbytes,mesh,failure))throw std::runtime_error(failure);
 if(asset.nodes.size()!=378||asset.contexts.size()!=1||asset.contexts[0].clips.size()!=325||asset.correction_map.size()!=mesh.vertices.size())throw std::runtime_error("coverage");
 // Inputs are corrupted one resource at a time; no alternate format is used.
 unsigned rejected=0;
 for(const std::string member:{"nala378.common.nlc","nala378.swsh.nll","nala378.surface.nls"}){
  bool failed=false;try{decode_layered(descriptor,meshbytes,[&](const std::string&name){auto b=loader(name);if(name==member)b.pop_back();return b;});}catch(const std::exception&){failed=true;}
  if(!failed)throw std::runtime_error("truncated bank accepted");++rejected;
 }
 std::ifstream requests(argv[2]);std::ofstream output(argv[3],std::ios::binary);if(!requests||!output)throw std::runtime_error("sample files");
 std::string name;double phase,weight;unsigned count=0;double radius=0;
 auto sampling=std::chrono::steady_clock::now();
 while(requests>>name>>phase>>weight){auto c=asset.contexts[0].clips.at(name).clip;auto p=evaluate(asset,*c,phase,weight);
  auto write=[&](const Matrix&m){for(auto v:m)if(!std::isfinite(v))throw std::runtime_error("nonfinite sample");output.write(reinterpret_cast<const char*>(m.data()),128);};
  for(auto&m:p.native){write(m);radius=(std::max)(radius,std::sqrt(m[3]*m[3]+m[7]*m[7]+m[11]*m[11]));}for(auto&m:p.mounts)write(m);write(p.root);
  unsigned corrections=unsigned(p.corrections.size());output.write(reinterpret_cast<const char*>(&corrections),4);
  for(auto&v:p.corrections)output.write(reinterpret_cast<const char*>(v.data()),48);++count;
 }
 bool bad_phase=false;try{evaluate(asset,*asset.contexts[0].clips.begin()->second.clip,-1);}catch(const std::exception&){bad_phase=true;}if(!bad_phase)throw std::runtime_error("negative phase accepted");
 auto end=std::chrono::steady_clock::now();if(radius>asset.radius)throw std::runtime_error("bone radius exceeded");
 std::cout<<"{\"ok\":true,\"clips\":325,\"nodes\":378,\"samples\":"<<count<<",\"packed_curve_bytes\":"<<asset.packed_curve_bytes<<",\"unique_channels\":"<<asset.unique_channels<<",\"truncations_rejected\":"<<rejected<<",\"load_seconds\":"<<std::chrono::duration<double>(loaded-start).count()<<",\"sample_seconds\":"<<std::chrono::duration<double>(end-sampling).count()<<"}\n";
 return !output;
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}
