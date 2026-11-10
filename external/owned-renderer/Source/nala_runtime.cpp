#include "nala_runtime.h"
#include "retained_static_scene.h"
#include "retained_static_submission.h"
#include "world_scene_algorithms.h"
#include "figure_service_nodes.h"
// Fixed accepted source configuration.
#define NALA_WAIST_WRAPS_PREVIEW 1
#define NALA_PANTS_MATERIAL_LINE 1
#define NALA_EXTREMITY_MATERIAL_LINE 1
#define NALA_SEPARATE_EYE_UV 1
#include "nala/UnityCompleteData.h"
#include "nala/UnityBodyAppearance.h"
#include "nala/UnityBodySize.h"
#include "nala/head105_v1/NalaFaceSourceData.h"
#include "nala/head105_v1/NalaUpperArcIdentity.h"
#include "nala/morphs_head105_v1/NalaBodyShape.h"
#include "nala/head105_v1/UnityHandsRuntime.h"
#include "nala/head105_v1/NalaHipPose.h"
#include "nala/head105_v1/NalaUnderwear.h"
#include "nala/head105_v1/NalaStarterShirt.h"
#include "nala/head105_v1/NalaStarterPants.h"
#include "nala/head105_v1/NalaExtremityClothes.h"
#include "nala/head105_v1/WoundComposite.h"
#include "nala/head105_v1/WoundMaskRle.h"
#include <atomic>
#include <mutex>
#include <stdexcept>

namespace owned::nala {
namespace {
using namespace UnityBody;
void require(bool ok,const char* reason) {if(!ok)throw std::runtime_error(reason);}
M matrix(const VisualNodeSample& n) {
 M m=Rotation({n.rotation[0],n.rotation[1],n.rotation[2],n.rotation[3]});
 m.t={n.translation[0],n.translation[1],n.translation[2]};return m;
}
struct Resources {
 ResourceStore store;
 std::uint64_t generation=0;
 std::map<unsigned,ResourceBlob> blobs;
 std::map<unsigned,skn::Dds> textures;
 std::vector<unsigned char> masks;
 std::array<unsigned char,6> lastWounds{};
 skn::Dds woundTexture;
 void open() {
  ResourceStoreConfig config;std::uint64_t next=0;ResourceFailure error;
  require(owned_resource_runtime_mounts(config,&next),"Nala mod Maps namespace unavailable");
  if(generation==next)return;
  store.close();blobs.clear();textures.clear();masks.clear();woundTexture={};
  require(store.open(config,error),error.message.c_str());generation=next;
 }
 const ResourceBlob& read(unsigned id) {
  auto found=blobs.find(id);if(found!=blobs.end())return found->second;
  ResourceBlob blob;ResourceFailure error;
  const auto name="nala/"+std::to_string(id)+(id==202?".rle":".rgb");
  require(store.resolve(name,blob,error),error.message.c_str());
  const unsigned side=id==201||id==211?2048:id<=221?1024:512;
  require(blob.bytes&&(id==202||blob.bytes->size()==std::size_t(side)*side*3),"Nala RGB resource size");
  return blobs.emplace(id,std::move(blob)).first->second;
 }
 skn::Dds texture(unsigned id,const Input& input) {
  const bool wounded=id==201&&std::any_of(input.wounds.begin(),input.wounds.end(),[](auto v){return v!=0;});
  if(wounded&&woundTexture.blob.bytes&&lastWounds==input.wounds)return woundTexture;
  if(!wounded){auto i=textures.find(id);if(i!=textures.end())return i->second;}
  auto original=read(id);
  const unsigned side=id==201||id==211?2048:id<=221?1024:512;
  const std::size_t pixels=std::size_t(side)*side;
  if(wounded&&masks.empty()){
   const auto& mask=read(202);
   require(NalaWoundMaskRle::Decode(mask.bytes->data(),mask.bytes->size(),pixels*19,masks),"Nala wound RLE invalid");
  }
  auto bytes=std::make_shared<std::vector<std::uint8_t>>(pixels*4);
  for(std::size_t p=0;p<pixels;++p){
   unsigned char color[3];const auto* rgb=original.bytes->data()+3*p;
   if(wounded){unsigned char layers[6]{};for(unsigned k=0;k<6;++k)if(input.wounds[k])layers[k]=masks[(1+k*3+input.wounds[k]-1)*pixels+p];
    NalaWoundComposite::Pixel(rgb,masks[p],layers,input.wounds,color);rgb=color;}
   (*bytes)[4*p]=rgb[2];(*bytes)[4*p+1]=rgb[1];(*bytes)[4*p+2]=rgb[0];(*bytes)[4*p+3]=255;
  }
  skn::Dds result;result.blob=original;result.blob.bytes=bytes;
  result.blob.identity.bytes=bytes->size();
  result.blob.identity.canonical_name+="|rgba";
  if(wounded)for(auto v:input.wounds)result.blob.identity.canonical_name+=char('0'+v);
  result.format=NativeOwnedTextureFormat::argb8888;
  result.mips.push_back({bytes->data(),side,side,unsigned(bytes->size())});
  if(!wounded)textures.emplace(id,result);
  else {lastWounds=input.wounds;woundTexture=result;}
  return result;
 }
};
Mesh mesh(RenderPacket packet,unsigned resource) {
 Mesh m;m.resource=resource;
 m.positions.resize(packet.plots.size());m.normals.resize(packet.normals.size());
 static_assert(sizeof(NativePackedGroup)==sizeof(PackedXYZW));
 std::memcpy(m.positions.data(),packet.plots.data(),m.positions.size()*sizeof(NativePackedGroup));
 std::memcpy(m.normals.data(),packet.normals.data(),m.normals.size()*sizeof(NativePackedGroup));
 for(unsigned i=0;i<packet.vertices;++i){m.uv.push_back({packet.uv[2*i],packet.uv[2*i+1]});m.components.push_back({std::uint16_t(i),std::uint16_t(i),std::uint16_t(i)});}
 m.indices=std::move(packet.indices);return m;
}
std::shared_ptr<const Frame> build(const RetainedVisualSample& sample) {
 const auto& input=sample.values.nala;
 require(input.valid,"Nala CUnit morph or wound state unavailable");
 const auto& assembly=*sample.assembly;
 const auto& currentShape=retained_visual_shape(sample);
 std::vector<VisualNodeSample> pose;
 require(sample_visual_skeleton(*assembly.skeleton,sample.selection,currentShape,assembly.center_shift,sample.pose.phase,pose,false),"Nala ANM/ADB pose unavailable");
 UnityGame::Actor actor;actor.complection=currentShape;
 if(!sample.selection.neutral&&sample.selection.database&&sample.selection.ordinal<sample.selection.database->entries.size())
  actor.animation=sample.selection.database->entries[sample.selection.ordinal].name;
 actor.animationFrame=sample.pose.phase;
 std::array<unsigned,15> nodes{};nodes.fill(UINT_MAX);
 for(unsigned i=0;i<assembly.skeleton->nodes.size();++i)for(unsigned k=0;k<15;++k)
  if(assembly.skeleton->nodes[i].name==UnityGame::Names[k]){nodes[k]=i;actor.poses[k]=matrix(pose.at(i));}
 require(std::find(nodes.begin(),nodes.end(),UINT_MAX)==nodes.end(),"Nala skeleton lacks required joint");
 UnityGame::Geometry geometry;
 for(const auto& part:assembly.parts){
  if(!part.source_available||!part.geometry||part.skeleton_node>=pose.size()||unit_service_node(part.name.c_str()))continue;
  if(std::find(nodes.begin(),nodes.end(),part.skeleton_node)!=nodes.end())continue;
  UnityGame::Attachment attachment;attachment.name=part.name;attachment.pose=matrix(pose.at(part.skeleton_node));
  attachment.oldEye=part.name=="eyepoint1"||part.name=="eyepoint2";attachment.flags=part.explicitly_hidden?0x80u:0u;
  unsigned ancestor=part.skeleton_node;
  for(unsigned depth=0;depth<assembly.skeleton->links.size();++depth){
   auto n=std::find(nodes.begin(),nodes.end(),ancestor);
   if(n!=nodes.end()){attachment.driver=int(n-nodes.begin());break;}
   if(ancestor>=assembly.skeleton->links.size())break;ancestor=assembly.skeleton->links[ancestor].parent_index;
  }
  actor.attachments.push_back(attachment);
  if(part.geometry){const auto& fig=*part.geometry;auto& vertices=geometry[part.name];
   // The FIG evaluator uses these same trilinear shape coefficients.
   const auto& s=currentShape;const float weights[8]={(1-s[0])*(1-s[1])*(1-s[2]),s[0]*(1-s[1])*(1-s[2]),(1-s[0])*s[1]*(1-s[2]),s[0]*s[1]*(1-s[2]),(1-s[0])*(1-s[1])*s[2],s[0]*(1-s[1])*s[2],(1-s[0])*s[1]*s[2],s[0]*s[1]*s[2]};
   vertices.resize(fig.position_groups*4);
   for(unsigned shape=0;shape<fig.shape_count;++shape)for(unsigned p=0;p<vertices.size();++p){const auto& g=fig.positions[shape*fig.position_groups+p/4];const unsigned l=p%4;const double w=fig.shape_count==1?1:weights[shape];vertices[p]=Add(vertices[p],Scale({g.x[l],g.y[l],g.z[l]},w));}
  }
 }
 actor.geometry=&geometry;
 static const Data source=CompleteData();
 thread_local Data shaped;thread_local NalaMorphs::Values cached{};thread_local bool initialized=false;
 auto profile=NalaBodyShape::Profile(input.controls);
 if(!initialized||cached!=input.controls){
  require(NalaBodyShape::Build(source,input.controls,std::vector<double>(EditorSchema::BoneCount*9),shaped,profile,NalaUpperArcIdentity::Apply),"Nala shape computation rejected");cached=input.controls;initialized=true;
 }
 UnityHands::Input hands;std::string weapon;UnityHands::Resolve(actor,hands,weapon);
 std::vector<V> v,n;std::vector<M> world;M root;
 require(UnityHands::Compute(shaped,profile,actor.poses,hands,v,n,world),"Nala hand pose computation rejected");
 require(NalaHipPose::Apply(shaped,profile,world,v,n,hands.sitting),"Nala hip pose rejected");
 require(UnityBodySize::Apply(actor.poses,v,world,root,profile.scale),"Nala sole placement rejected");
 std::vector<V> render,renderNormal;
 require(NalaLidContact::Render(v,n,render,renderNormal),"Nala lid topology rejected");
 v.resize(NalaLidContact::NativeCount);n.resize(NalaLidContact::NativeCount);
 auto frame=std::make_shared<Frame>();frame->root_node=nodes[0];
 std::array<unsigned,15> drivers{};drivers.fill(UINT_MAX);
 for(unsigned i=0;i<source.driver.size();++i)if(source.driver[i]>=0)drivers[unsigned(source.driver[i])]=i;
 for(const auto& a:actor.attachments){
  if(a.oldEye||a.driver<0)continue;
  const unsigned driver=unsigned(a.driver),bone=drivers[driver];
  require(bone<world.size(),"Nala attachment driver missing");
  M desired=Product(root,Proper(world[bone]));desired.t=Point(root,world[bone].t);
  M target;
  if(UnityHands::AttachmentPose(actor,hands,root,a,target))desired=target;
  else {const auto restTarget=Proper(shaped.rest[bone]);const auto restSource=Proper(Product(Inverse(source.gameBind[0]),source.gameBind[driver]));
   desired=Product(Product(desired,Product(Inverse(restTarget),restSource)),Product(Inverse(actor.poses[driver]),a.pose));}
  const auto map=Product(Inverse(a.pose),desired);std::array<double,12> values{};
  for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)values[r*4+c]=map.r[r][c];
  values[3]=map.t.x;values[7]=map.t.y;values[11]=map.t.z;frame->attachments.emplace(a.name,values);
 }
 static std::atomic<std::uint64_t> serial{0};frame->serial=++serial;
 RenderPacket body;unsigned faces=unsigned(std::size(NalaLidContact::Indices)/3);
 require(InitializePacket(unsigned(render.size()),NalaLidContact::UV(),NalaLidContact::Indices,unsigned(std::size(NalaLidContact::Indices)),&faces,1,body)&&PreparePacket(render,renderNormal,root,actor.poses[0],body),"Nala body packet rejected");
 frame->meshes.push_back(mesh(std::move(body),201));
 std::vector<V> underwear,underwearNormal;RenderPacket underwearPacket;
 require(NalaUnderwear::Compute(v,underwear,underwearNormal)&&NalaUnderwear::Packet(underwear,underwearNormal,root,actor.poses[0],underwearPacket),"Nala underwear rejected");
 const auto* shirt=NalaShirtMaterials::ForSubtype(input.clothing[0]);
 const auto* pants=NalaPantsMaterials::ForSubtype(input.clothing[1]);
 std::vector<V> shirtV,shirtN,pantsV,pantsN,wrapV,wrapN;
 bool wrap=false;
 if(shirt)require(NalaStarterShirt::Compute(v,n,shirtV,shirtN),"Nala shirt rejected");
 if(pants){
  require(NalaStarterPants::Compute(v,n,pantsV,pantsN),"Nala pants rejected");
  if(shirt&&NalaWaistLayers::SupportsShirtCut(input.clothing[0])){std::vector<V> fitted,fittedN;require(NalaWaistLayers::ShirtProfile(v,n,shirtV,fitted,fittedN),"Nala waist shirt profile rejected");shirtV=std::move(fitted);shirtN=std::move(fittedN);}
  require(NalaWaistLayers::Compute(v,n,shirt!=nullptr,wrapV,wrapN),"Nala waist wrap rejected");wrap=true;
  RenderPacket packet;require(NalaStarterPants::Packet(pantsV,pantsN,root,actor.poses[0],packet,wrap?&wrapV:nullptr,wrap?&wrapN:nullptr),"Nala pants packet rejected");
  frame->meshes.push_back(mesh(std::move(packet),pants->resource));NalaStarterPants::HidePanties(underwearPacket);
 }
 if(shirt){RenderPacket packet;require(NalaStarterShirt::Packet(shirtV,shirtN,root,actor.poses[0],packet,shirt->ordinal),"Nala shirt packet rejected");frame->meshes.push_back(mesh(std::move(packet),shirt->resource));NalaStarterShirt::HideBandeau(underwearPacket);}
 frame->meshes.push_back(mesh(std::move(underwearPacket),211));
 for(unsigned kind=0;kind<2;++kind){unsigned resource=NalaExtremityClothes::Match(kind==0?6:5,input.clothing[2+kind]);if(!resource)continue;
  std::vector<V> p,normal;RenderPacket packet;require(NalaExtremityClothes::Compute(kind,v,n,p,normal)&&NalaExtremityClothes::Packet(kind,p,normal,root,actor.poses[0],packet),"Nala extremity packet rejected");frame->meshes.push_back(mesh(std::move(packet),resource));}
 // ResourceStore and its wound decode cache are shared by world and preview.
 static std::mutex resourceMutex;std::lock_guard<std::mutex> guard(resourceMutex);
 static Resources resources;resources.open();
 auto materials=std::make_shared<skn::RuntimeAsset>();materials->nala_generated=true;
 materials->identity.canonical_name="nala-frame/"+std::to_string(frame->serial);
 for(const auto& m:frame->meshes){
  skn::Material material{};std::fill_n(material.base,4,1.f);material.roughness=m.resource==201?.6f:.9f;material.ao=1;
  // Shared authoring palette, config/equipment-materials.json. Albedo stays
  // unchanged; GGX supplies lighting instead of the old vertex-light pass.
  constexpr float roughness[]={.9f,.8f,.52f,.5f,.48f};
  if(m.resource>=212&&m.resource<=221)material.roughness=roughness[(m.resource-212)%5];
  if(m.resource>=224&&m.resource<=227)material.roughness=roughness[m.resource-223];
  if(m.resource>=228&&m.resource<=231)material.roughness=roughness[m.resource-227];
  materials->asset.materials.push_back(material);materials->textures.emplace_back();materials->textures.back()[0]=resources.texture(m.resource,input);
  for(const auto& p:m.positions)for(unsigned l=0;l<4;++l)frame->radius=(std::max)(frame->radius,float(std::sqrt(p.x[l]*p.x[l]+p.y[l]*p.y[l]+p.z[l]*p.z[l])));
 }
 for(const auto& a:actor.attachments){
  const auto map=frame->attachments.find(a.name);const auto vertices=geometry.find(a.name);
  if(map==frame->attachments.end()||vertices==geometry.end())continue;
  const auto& values=map->second;M correction;
  for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)correction.r[r][c]=values[r*4+c];
  correction.t={values[3],values[7],values[11]};
  const auto toRoot=Product(Product(Inverse(actor.poses[0]),a.pose),correction);
  for(const auto& p:vertices->second){const auto local=Point(toRoot,p);frame->radius=(std::max)(frame->radius,float(std::sqrt(Dot(local,local))));}
 }
 frame->materials=materials;return frame;
}
}
std::shared_ptr<const Frame> prepare(const RetainedVisualSample& sample) {
 auto& prepared=*sample.prepared;
 if(prepared.nala_frame&&prepared.nala_input==sample.values.nala&&owned_visual_pose_key_matches(prepared.nala_key,sample))return prepared.nala_frame;
 auto frame=build(sample);prepared.nala_frame=frame;prepared.nala_input=sample.values.nala;prepared.nala_key=owned_visual_pose_key(sample);return frame;
}
}
