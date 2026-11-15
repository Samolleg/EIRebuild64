// Exercise the active engine publication, not the independent resource loader.
#include "../Game/OwnedRender/E1Terrain.h"
#include "../Game/OwnedRender/E1World.h"
#include "retained_static_scene.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <cstring>
namespace owned {
void bridge_append_probe_event(const char* tag,const char* detail) noexcept {std::cout<<tag<<": "<<detail<<'\n';}
SceneWorldToClipSnapshot bridge_scene_world_to_clip_snapshot() noexcept {SceneWorldToClipSnapshot s;s.valid=true;s.scene_frame_serial=1;return s;}
}
namespace owned {
const BridgeFrameValues* bridge_embedded_installed_frame() noexcept {std::terminate();}
RetainedTerrainSourceSnapshot RetainedStaticScene::terrain_source_snapshot() const noexcept {std::terminate();}
RetainedStaticScene& retained_visual_scene() {std::terminate();}
}
namespace OwnedRender {
std::uint64_t E1WorldGeneration() noexcept{return 1;}
bool E1ReadWorldLighting(owned::NativeWorldLighting&) noexcept {std::terminate();}
std::shared_ptr<const owned::OwnedVisualSurface> E1SurfaceValue(std::uint64_t) noexcept {std::terminate();}
}
namespace GameJournal {void Event(const char*,const char*,const char*,...) noexcept{}}
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Reader {
 owned::MemoryResourceArchive map;owned::ResourceStore textures;
 bool profileMissing=false;
 static HRESULT read(Reader& reader,bool texture,const char* name,void* destination,std::uint32_t capacity,std::uint32_t* size){
  owned::ResourceBlob blob;owned::ResourceFailure failure;
  if(texture&&reader.profileMissing&&std::strstr(name,"ei_atlas_profile_"))return E_FAIL;
  if(!(texture?reader.textures.resolve(name,blob,failure):reader.map.resolve(name,blob,failure))||!blob.bytes)return E_FAIL;
  *size=std::uint32_t(blob.bytes->size());if(destination){if(capacity<*size)return E_FAIL;std::memcpy(destination,blob.bytes->data(),*size);}return S_OK;
 }
 static HRESULT __cdecl mapRead(void* p,const char* n,void* d,std::uint32_t c,std::uint32_t* s){return read(*static_cast<Reader*>(p),false,n,d,c,s);}
 static HRESULT __cdecl textureRead(void* p,const char* n,void* d,std::uint32_t c,std::uint32_t* s){return read(*static_cast<Reader*>(p),true,n,d,c,s);}
};
int wmain(int argc,wchar_t** argv)try{
 using namespace owned;using namespace OwnedRender;check(argc==4,"map, texture archive, expected slot count");
 Reader reader;ResourceStoreConfig config;config.archives.push_back(argv[2]);ResourceFailure failure;
 check(reader.textures.open(config,failure),"open texture resources");ResourceBlob blob;std::filesystem::path path(argv[1]);
 check(reader.textures.read_loose_file(path.wstring(),path.filename().string(),blob,failure),"read MPR");check(reader.map.open(blob,failure),"open MPR");
 TerrainCpuAsset original;check(parse_terrain_asset(blob,path.filename().string(),original,failure),"parse MPR");
 const unsigned expected=unsigned(std::wcstoul(argv[3],nullptr,10));
 for(unsigned pass=0;pass<2;++pass){
  reader.profileMissing=pass==1;std::string why;int owner;
  check(E1PublishTerrainSource(&owner,1,pass+1,path.filename().string().c_str(),original.map_header_resource.c_str(),
      Reader::mapRead,&reader,why,Reader::textureRead,&reader),"active map publication");
  auto geometry=frozen_owned_terrain_geometry(original);geometry.revision=1;
  std::array<float,16> matrix{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  check(E1PublishTerrainFrame(&owner,geometry,matrix),"active frame publication");
  RetainedTerrainSourceSnapshot snapshot;check(E1CurrentTerrainSource(snapshot),"actual active source");
  unsigned count=0;for(auto mask:snapshot.terrain->astral_slots)count+=mask!=0;
  check(count==(pass?0:expected),"active source retained Astral mask (missing profiles must not invent one)");
  unsigned marked=0;
  for(const auto& sector:snapshot.terrain->sectors)for(const auto& tile:sector.tiles)
   if(!tile.transparent&&tile.animation_ordinal==UINT_MAX&&tile.tile_index<snapshot.terrain->astral_slots.size()&&snapshot.terrain->astral_slots[tile.tile_index])++marked;
  check((marked!=0)==(!pass&&expected!=0),"actual placed Astral tiles");
  unsigned gpuMarked=0,totalVertices=0;
  const auto& terrain=*snapshot.terrain;
  for(unsigned si=0;si<terrain.sectors.size();++si){const auto& sector=terrain.sectors[si];
   for(unsigned mi=0;mi<terrain.texture_count+2&&mi<sector.materials.size();++mi){
    OwnedTerrainSourceDecodeContext decode;decode.source=&terrain;decode.sector=&sector;decode.material=&sector.materials[mi];
    decode.geometry=snapshot.geometry.get();OwnedTerrainDynamicSector dynamic;dynamic.animation_phases.assign(sector.animation_count,0);decode.dynamic=&dynamic;
    auto& command=decode.command;command.source_sector=si;command.material=std::uint16_t(mi);command.subpart_mask=15;command.lod=0;
    if(!select_owned_terrain_descriptor_counts(*decode.material,0,false,false,15,command.tile_count,command.index_count)||!command.tile_count)continue;
    command.vertex_count=command.tile_count*9;const unsigned n=command.vertex_count,g=(n+3)/4;
    std::vector<NativePackedGroup> positions(g),normals(g);std::vector<NativeTextureCoordinate> uv(n);
    std::vector<NativeVertexComponent> components(n);std::vector<std::uint16_t> indices(command.index_count);std::vector<NativeTerrainVertex> vertices(n);
    NativeGeometryDecodeTarget target;target.positions=positions.data();target.position_groups=g;target.normals=normals.data();target.normal_groups=g;
    target.texture_coordinates=uv.data();target.texture_coordinate_count=n;target.components=components.data();target.component_count=n;
    target.indices=indices.data();target.index_count=command.index_count;target.terrain_vertices=vertices.data();target.terrain_vertex_count=n;
    check(decode_owned_terrain_tiles(decode,target),"active terrain GPU stream decode");
    for(const auto& vertex:vertices)if(vertex.flags&native_terrain_astral){++gpuMarked;check(((vertex.flags>>16)&15)!=0,"GPU corner mask");}
    totalVertices+=n;
   }
  }
  check(gpuMarked==marked*9&&totalVertices>gpuMarked,"GPU Astral flags and ordinary ground exclusion");
  std::cout<<"PASS actual GPU vertex stream: astral="<<gpuMarked<<" total="<<totalVertices<<'\n';
  std::cout<<"PASS active publication: slots="<<count<<" placed="<<marked<<" missing_profile="<<pass<<'\n';
  E1RetireTerrain(&owner);
 }
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
