// Read the staged archive through the same category routing and DDS decoder as the game.
#include "../external/owned-renderer/Source/skn_runtime.h"
#include <filesystem>
#include <iostream>
namespace owned {void bridge_append_probe_event(const char*,const char*) noexcept {}}
int main(int argc,char**argv)try{
 if(argc!=2)throw std::runtime_error("nlc_resource_probe staged.pak");
 owned::ResourceStoreConfig config;config.archives={std::filesystem::path(argv[1]).wstring()};
 config.archive_kinds={owned::kArchiveFigures};owned::ResourceFailure failure;owned::ResourceStore figures;
 if(!figures.open(config,failure))throw std::runtime_error(failure.message);
 owned::ResourceBlob absent;if(figures.resolve("dds/nala-body.dds",absent,failure)||failure.code!=owned::ResourceFailureCode::not_found)throw std::runtime_error("texture routing negative case");
 config.archive_kinds={owned::kArchiveFigures|owned::kArchiveTextures};owned::ResourceStore store;
 if(!store.open(config,failure))throw std::runtime_error(failure.message);
 const auto blob=[&](const std::string&name){owned::ResourceBlob b;if(!store.resolve(name,b,failure)||!b.bytes)throw std::runtime_error("missing "+name+": "+failure.message);return b;};
 auto descriptor=blob("unhufe.hero.nlm"),mesh=blob("unhufe.hero.skn");
 auto asset=owned::skm::decode_layered(*descriptor.bytes,*mesh.bytes,[&](const std::string&name){return *blob(name).bytes;});
 owned::skn::Asset skn;std::string error;if(!owned::skn::decode(*mesh.bytes,skn,error))throw std::runtime_error(error);
 std::set<std::string> textures;for(const auto&material:skn.materials)for(unsigned k=0;k<4;++k)if(!material.maps[k].empty()){
  auto texture=owned::skn::dds(blob(material.maps[k]),k);if(texture.mips.empty())throw std::runtime_error("empty DDS");textures.insert(material.maps[k]);}
 for(const char*name:{"unhufe.skn","unhufe.nlm","unhufe.hero.skm"})if(store.resolve(name,absent,failure)||failure.code!=owned::ResourceFailureCode::not_found)throw std::runtime_error("unexpected global/old override");
 if(textures.size()!=4||asset.contexts.size()!=1||asset.contexts[0].equipment!=std::set<std::string>{"lh2.shield01","rh3.sword03"})throw std::runtime_error("hero context");
 std::cout<<"{\"ok\":true,\"pak_resource_route\":true,\"hero_only\":true,\"dds\":4,\"clips\":"<<asset.contexts[0].clips.size()<<",\"figures_only_dds_rejected\":true}\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}
