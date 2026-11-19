// G02/G03/G04: saved authored meshes, not reconstructed generators.
struct GolemVertex {float x,y,z,u,v,nx,ny,nz;unsigned kind;};
static_assert(sizeof(GolemVertex)==36,"GAV1 vertex layout");
struct GolemPart {
 unsigned kind=0,stage=0,texture=0,hand=0,actor=0,core=0,count=0;
 char bone[32]{};Vec offset{};std::vector<GolemVertex> vertices;
};
using GolemSpellPart=GolemPart;
struct GolemResource {
 std::vector<GolemPart> parts;bool valid=false;
 explicit GolemResource(const wchar_t* name,unsigned expected) {
  auto module=owned::render_resource_module();
  if(!module)return;
  auto r=FindResourceW(module,name,MAKEINTRESOURCEW(10));if(!r)return;
  const auto handle=LoadResource(module,r);const auto* bytes=static_cast<const unsigned char*>(LockResource(handle));size_t left=SizeofResource(module,r);
  auto take=[&](void* out,size_t size){if(!bytes||size>left)return false;std::memcpy(out,bytes,size);bytes+=size;left-=size;return true;};
  char magic[4];unsigned version=0,count=0;if(!take(magic,4)||std::memcmp(magic,"GAV1",4)||!take(&version,4)||version!=1||!take(&count,4)||count!=expected)return;
  parts.resize(count);
  for(auto& p:parts){
   if(!take(&p.kind,4)||!take(&p.stage,4)||!take(&p.texture,4)||!take(&p.hand,4)||!take(&p.actor,4)||!take(&p.core,4)||!take(&p.count,4)||!take(p.bone,32)||!take(&p.offset,12))return;
   if(p.kind>4||p.stage>5||p.texture>2||p.hand>1||p.actor>1||p.core>1||p.bone[31]||!p.count||p.count>60000||p.count%3||!std::isfinite(p.offset.x)||!std::isfinite(p.offset.y)||!std::isfinite(p.offset.z))return;
   p.vertices.resize(p.count);if(!take(p.vertices.data(),p.count*sizeof(GolemVertex)))return;
   for(const auto& v:p.vertices)if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)||!std::isfinite(v.u)||!std::isfinite(v.v)||!std::isfinite(v.nx)||!std::isfinite(v.ny)||!std::isfinite(v.nz)||v.kind>4)return;
  }valid=left==0;
 }
};
const GolemResource& RubyResources(){static const GolemResource value(L"EID3D11_RUBY_V1",26);return value;}
const GolemResource& GolemResources(){static const GolemResource value(L"EID3D11_GOLEM_V1",115);return value;}
