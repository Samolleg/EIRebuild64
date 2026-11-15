#include "nlc_asset.h"
#include <windows.h>
#include <bcrypt.h>
#include <zstd.h>
#include "../../../SDKs/JSON/json.hpp"
#pragma comment(lib,"bcrypt.lib")
namespace owned::skm {
using Json=nlohmann::json;
namespace {
void require(bool ok,const char*message){if(!ok)throw std::runtime_error(message);}
std::uint64_t number(const std::uint8_t*p,unsigned n){std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=std::uint64_t(p[i])<<(i*8);return v;}
float f32(const std::uint8_t*p){float f;std::memcpy(&f,p,4);require(std::isfinite(f),"NLC nonfinite value");return f;}
unsigned integer(const Json&j,unsigned max){require(j.is_number_integer(),"NLC expected integer");auto n=j.get<std::int64_t>();require(n>=0&&std::uint64_t(n)<=max,"NLC integer range");return unsigned(n);}
double real(const Json&j){require(j.is_number(),"NLC expected number");double x=j.get<double>();require(std::isfinite(x),"NLC nonfinite number");return x;}
std::string name(const Json&j){auto s=j.get<std::string>();require(!s.empty()&&s.size()<=256&&s.find('\0')==std::string::npos,"NLC invalid name");return s;}
std::string resource(const Json&j){auto s=name(j);require(s.find_first_of("/\\:")==std::string::npos&&s!="."&&s!="..","NLC invalid resource name");return s;}
Bytes decompress(const Bytes&b){auto size=ZSTD_getFrameContentSize(b.data(),b.size());require(size>=12&&size<=256ull*1024*1024,"NLC unbounded zstd resource");require(ZSTD_findFrameCompressedSize(b.data(),b.size())==b.size(),"NLC trailing compressed data");Bytes out(std::size_t(size),0);auto got=ZSTD_decompress(out.data(),out.size(),b.data(),b.size());require(!ZSTD_isError(got)&&got==out.size(),"NLC zstd decode failed");return out;}
void validate_times(const std::vector<float>&times){require(!times.empty(),"NLC missing keys");double previous=-1;for(auto t:times){require(std::isfinite(t)&&t>=0&&t>previous,"NLC invalid times");previous=t;}}
std::vector<float> json_times(const Json&j){require(j.is_array()&&!j.empty()&&j.size()<=100000,"NLC phase count");std::vector<float> result;for(const auto&v:j)result.push_back(float(real(v)));validate_times(result);return result;}
struct Hash {
 BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE handle=nullptr;
 Hash(){if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("NLC SHA provider");if(BCryptCreateHash(algorithm,&handle,nullptr,0,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(algorithm,0);throw std::runtime_error("NLC SHA create");}}
 ~Hash(){if(handle)BCryptDestroyHash(handle);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
 void add(const void*p,std::size_t n){require(n<=ULONG_MAX,"NLC hash length");require(BCryptHashData(handle,(PUCHAR)p,ULONG(n),0)>=0,"NLC SHA update");}
 std::string finish(){std::uint8_t b[32];require(BCryptFinishHash(handle,b,32,0)>=0,"NLC SHA finish");const char*hex="0123456789abcdef";std::string out;for(auto v:b){out+=hex[v>>4];out+=hex[v&15];}return out;}
};
struct Bank {Json meta;std::map<std::string,Curve> channels;std::size_t bytes=0;};
Bank decode_bank(const Bytes&compressed){
 auto raw=decompress(compressed);require(raw.size()>=12&&!std::memcmp(raw.data(),"NLC1",4),"NLC bank signature");
 auto length=number(raw.data()+4,8);require(length<=raw.size()-12&&length<=64*1024*1024,"NLC header bounds");
 auto json=Json::parse(raw.begin()+12,raw.begin()+12+std::size_t(length));require(json.at("schema")=="nala_layer_bank_v1","NLC schema");
 const auto&directory=json.at("channels");require(directory.is_object()&&directory.size()<=250000,"NLC channel count");
 Bank out;out.meta=std::move(json["metadata"]);const auto payload=raw.data()+12+length;const auto payload_size=raw.size()-12-length;
 std::vector<std::pair<std::size_t,std::size_t>> ranges;
 for(auto it=directory.begin();it!=directory.end();++it){
  require(it.key().size()==64,"NLC content ID");const auto&m=it.value();auto c=std::make_shared<Channel>();
  c->kind=integer(m.at("kind"),2);c->bits=integer(m.at("bits"),32);require(c->bits==8||c->bits==12||c->bits==16||c->bits==20||c->bits==24||c->bits==32,"NLC bit rate");
  c->width=integer(m.at("width"),9);c->drop=m.at("drop").get<int>();c->symmetric=m.at("symmetric").get<bool>();
  require(c->drop>=-1&&c->drop<=3&&(!c->symmetric||c->kind==1)&&(c->drop<0||c->kind==0),"NLC channel representation");
  const unsigned width=c->kind==0?(c->drop<0?4:3):c->kind==1?(c->symmetric?6:9):3;
  require(c->width==width,"NLC component count");unsigned count=integer(m.at("count"),100000);require(count>0,"NLC empty channel");
  auto offset=integer(m.at("offset"),unsigned(payload_size)),size=integer(m.at("size"),unsigned(payload_size));
  require(std::uint64_t(offset)+size<=payload_size&&size>=std::uint64_t(count)*4,"NLC channel bounds");ranges.emplace_back(offset,offset+size);
  c->times.resize(count);for(unsigned i=0;i<count;++i)c->times[i]=f32(payload+offset+i*4);validate_times(c->times);
  if(c->bits!=32){
   require(m.at("low").size()==width&&m.at("span").size()==width&&m.at("columns").size()<=width,"NLC quantizer dimensions");
   for(unsigned i=0;i<width;++i){c->low[i]=float(real(m["low"][i]));c->span[i]=float(real(m["span"][i]));require(std::isfinite(c->low[i])&&std::isfinite(c->span[i])&&c->span[i]>=0,"NLC quantizer range");}
   std::set<unsigned> columns;for(const auto&v:m["columns"]){auto column=integer(v,width-1);require(columns.insert(column).second&&c->span[column]>0,"NLC duplicate column");c->columns.push_back(column);}
   for(unsigned i=0;i<width;++i)require((c->span[i]!=0)==bool(columns.count(i)),"NLC missing variable component");
  }
  const auto expected=c->bits==32?std::uint64_t(count)*width*4:(std::uint64_t(count)*c->columns.size()*c->bits+7)/8;
  require(size==count*4+expected,"NLC packed size");c->packed.assign(payload+offset+count*4,payload+offset+size);
  // Verify the same content ID as the exporter, then keep quantized values.
  Bytes decoded;const unsigned decoded_width=c->kind==0?4:c->kind==1?9:3;decoded.reserve(std::size_t(count)*decoded_width*4);
  for(unsigned frame=0;frame<count;++frame){auto values=c->key(frame);for(unsigned j=0;j<decoded_width;++j){require(std::isfinite(values[j]),"NLC invalid decoded value");auto p=reinterpret_cast<const std::uint8_t*>(&values[j]);decoded.insert(decoded.end(),p,p+4);}if(c->kind==0){double norm=0;for(unsigned j=0;j<4;++j)norm+=double(values[j])*values[j];require(std::abs(norm-1)<1e-3,"NLC invalid quaternion");}}
  Hash hash;hash.add(&c->kind,4);hash.add(&count,4);hash.add(c->times.data(),count*4);hash.add(decoded.data(),decoded.size());require(hash.finish()==it.key(),"NLC channel checksum");
  out.bytes+=c->times.size()*4+c->packed.size();out.channels.emplace(it.key(),std::move(c));
 }
 std::sort(ranges.begin(),ranges.end());std::size_t end=0;for(auto r:ranges){require(r.first==end,"NLC overlapping or unreferenced channel bytes");end=r.second;}require(end==payload_size,"NLC trailing payload");return out;
}
Curve find(const Bank&base,const Bank*layer,const Json&j){auto key=name(j);auto b=base.channels.find(key);if(b!=base.channels.end())return b->second;if(layer){auto l=layer->channels.find(key);if(l!=layer->channels.end())return l->second;}throw std::runtime_error("NLC unresolved channel");}
void curve_kinds(const std::vector<Curve>&c){for(unsigned i=0;i<c.size();++i)require(c[i]->kind==i%3,"NLC wrong curve kind");}
void read_surface(Asset&a,const Bytes&compressed,const std::string&mesh_sha){
 auto raw=decompress(compressed);require(raw.size()>=16&&!std::memcmp(raw.data(),"NALS",4)&&number(raw.data()+4,4)==1,"NLS header");auto length=number(raw.data()+8,8);require(length<=raw.size()-16,"NLS header bounds");
 auto m=Json::parse(raw.begin()+16,raw.begin()+16+std::size_t(length));require(m.at("schema")=="nala_postskin_surface_v1"&&m.at("mesh_sha256")==mesh_sha,"NLS mesh identity");
 auto frames=integer(m.at("frames"),100000),vertices=integer(m.at("render_vertices"),1000000);a.correction_count=integer(m.at("affected_vertices"),1000000);a.correction_clip=name(m.at("clip"));
 require(frames>0&&vertices>0&&std::uint64_t(frames)*a.correction_count<=10000000,"NLS dimensions");
 auto at=16+std::size_t(length);const auto values=std::size_t(frames)*a.correction_count*6;
 require(raw.size()-at==std::size_t(vertices)*4+frames*4+values*4,"NLS payload size");
 a.correction_map.resize(vertices);for(auto&v:a.correction_map){v=std::int32_t(number(raw.data()+at,4));at+=4;require(v>=-1&&v<int(a.correction_count),"NLS correction index");}
 a.correction_phases.resize(frames);for(auto&v:a.correction_phases){v=f32(raw.data()+at);at+=4;}validate_times(a.correction_phases);
 a.corrections.resize(values);for(unsigned component=0;component<a.correction_count*6;++component){std::uint32_t previous=0;for(unsigned f=0;f<frames;++f){std::uint32_t delta=0;for(unsigned lane=0;lane<4;++lane)delta|=std::uint32_t(raw[at+(std::size_t(component)*4+lane)*frames+f])<<(lane*8);previous+=delta;float v;std::memcpy(&v,&previous,4);require(std::isfinite(v),"NLS invalid correction");a.corrections[std::size_t(f)*a.correction_count*6+component]=v;}}
}
} // namespace
std::string sha256(const Bytes&bytes){Hash h;h.add(bytes.data(),bytes.size());return h.finish();}
std::array<float,9> Channel::key(unsigned frame)const {
 require(frame<times.size(),"NLC key index");std::array<float,9> values=low;
 if(bits==32){for(unsigned i=0;i<width;++i)values[i]=f32(packed.data()+(std::size_t(frame)*width+i)*4);}
 else for(unsigned c=0;c<columns.size();++c){auto bit=(std::uint64_t(c)*times.size()+frame)*bits;auto byte=std::size_t(bit/8);unsigned shift=unsigned(bit%8);std::uint64_t word=0;
  for(unsigned n=0;n<(bits+shift+7)/8;++n)word|=std::uint64_t(packed[byte+n])<<(n*8);
  auto value=(word>>shift)&((std::uint64_t(1)<<bits)-1);unsigned col=columns[c];values[col]=float(double(low[col])+double(value)/double((std::uint64_t(1)<<bits)-1)*double(span[col]));}
 if(symmetric)values={values[0],values[1],values[2],values[1],values[3],values[4],values[2],values[4],values[5]};
 if(drop>=0){std::array<float,9> q{};unsigned col=0;double length=0;for(unsigned i=0;i<4;++i)if(i!=unsigned(drop)){q[i]=values[col++];length+=double(q[i])*q[i];}q[drop]=float(std::sqrt((std::max)(0.,1-length)));values=q;}
 return values;
}
std::array<double,9> Channel::sample(double phase)const{
 auto i=interval(times,phase);auto a=key(i.first),b=key(i.second);std::array<double,9> result{};
 if(kind==0){double aa[4],bb[4];for(unsigned j=0;j<4;++j){aa[j]=a[j];bb[j]=b[j];}mix_quaternion(aa,bb,i.mix,result.data());}
 else for(unsigned j=0;j<(kind==1?9:3);++j)result[j]=a[j]*(1-i.mix)+b[j]*i.mix;return result;
}
Asset decode_layered(const Bytes&descriptor,const Bytes&mesh,const ResourceReader&read){
 require(descriptor.size()<=1024*1024,"NLM descriptor too large");auto d=Json::parse(descriptor);require(d.at("schema")=="nala_runtime_layers_v1","NLM schema");
 auto mesh_sha=sha256(mesh);require(d.at("mesh_sha256")==mesh_sha,"NLM mesh identity");
 auto resource_bytes=[&](const Json&r){auto b=read(resource(r.at("resource")));require(sha256(b)==r.at("sha256"),"NLM resource checksum");return b;};
 auto common_bytes=resource_bytes(d.at("common"));auto base_sha=sha256(common_bytes);auto base=decode_bank(common_bytes);common_bytes.clear();common_bytes.shrink_to_fit();Asset a;
 require(integer(base.meta.at("author_nodes"),4096)==378,"NLC missing author nodes");const auto&names=base.meta.at("names");const auto&parents=base.meta.at("parents");const auto&rebases=base.meta.at("rebase");
 require(names.size()==378&&parents.size()==378&&rebases.size()==378&&base.meta.at("order").size()==378,"NLC rig dimensions");
 std::set<std::string> unique;for(unsigned n=0;n<378;++n){Node node;node.name=name(names[n]);require(unique.insert(node.name).second,"NLC duplicate node");node.parent=parents[n].get<int>();require(node.parent>=-1&&node.parent<378&&node.parent!=int(n),"NLC parent index");require(rebases[n].size()==4,"NLC rebase rows");for(unsigned row=0;row<4;++row){require(rebases[n][row].size()==4,"NLC rebase columns");for(unsigned col=0;col<4;++col)node.rebase[row*4+col]=real(rebases[n][row][col]);}a.nodes.push_back(node);}
 std::set<unsigned> visited;for(const auto&v:base.meta["order"]){auto n=integer(v,377);require(visited.insert(n).second&&(a.nodes[n].parent<0||visited.count(a.nodes[n].parent)),"NLC invalid hierarchy order");a.order.push_back(n);}
 require(integer(base.meta.at("native_hz"),1000)==15,"NLC time base mismatch");
 require(d.at("shape").size()==3,"NLM body shape");for(unsigned i=0;i<3;++i)a.shape[i]=float(real(d["shape"][i]));a.radius=float(real(d.at("radius")));require(a.radius>0&&a.radius<=100,"NLM bounds");a.root_name=name(d.at("root_name"));
 require(d.at("mounts").size()==2,"NLM mounts");for(const auto&m:d["mounts"]){Mount mount;mount.name=name(m.at("name"));mount.anchor=integer(m.at("anchor"),377);require(unique.insert(mount.name).second,"NLM duplicate mount");a.mounts.push_back(mount);}
 read_surface(a,resource_bytes(d.at("surface")),mesh_sha);
 const auto&clips=base.meta.at("clips");require(clips.is_object()&&clips.size()==325,"NLC clip coverage");
 std::map<std::string,std::shared_ptr<Clip>> common_clips;
 for(auto it=clips.begin();it!=clips.end();++it){auto c=std::make_shared<Clip>();c->name=it.key();require(c->name.size()<=256,"NLC clip name");const auto&m=it.value();c->scale=float(real(m.at("scale")));c->first=real(m.at("first_phase"));c->last=real(m.at("last_phase"));require(c->scale>0&&c->scale<100&&c->first==0&&c->last>=c->first,"NLC clip range");require(m.at("channels").size()==379*3,"NLC common tracks");for(const auto&v:m["channels"])c->common.push_back(find(base,nullptr,v));curve_kinds(c->common);common_clips.emplace(c->name,c);}
 a.unique_channels=base.channels.size();a.packed_curve_bytes=base.bytes;
 std::set<std::set<std::string>> signatures;require(d.at("contexts").is_array()&&!d["contexts"].empty()&&d["contexts"].size()<=128,"NLM context count");
 for(const auto&context:d["contexts"]){Context out;out.name=name(context.at("name"));out.neutral=name(context.at("neutral"));for(const auto&e:context.at("equipment"))require(out.equipment.insert(name(e)).second,"NLM duplicate equipment");require(signatures.insert(out.equipment).second,"NLM ambiguous equipment");
  auto layer_bytes=resource_bytes(context.at("layer"));auto bank=decode_bank(layer_bytes);require(bank.meta.at("base_sha256")==base_sha,"NLL wrong common bank");
  for(const auto&c:bank.channels)require(!base.channels.count(c.first),"NLL duplicated common channel");
  std::set<std::string> equipment;for(const auto&e:bank.meta.at("equipment"))equipment.insert(name(e));require(equipment==out.equipment,"NLL equipment mismatch");
  require(bank.meta.at("anchors").size()==a.mounts.size(),"NLL anchor count");for(unsigned i=0;i<a.mounts.size();++i)require(integer(bank.meta["anchors"][i],377)==a.mounts[i].anchor,"NLL anchor mismatch");
  require(bank.meta.at("clips").size()==clips.size()&&bank.meta.at("identity").size()==3,"NLL clip coverage");std::vector<Curve> defaults;for(const auto&v:bank.meta["identity"])defaults.push_back(find(base,&bank,v));curve_kinds(defaults);
  for(unsigned k=0;k<3;++k){auto value=defaults[k]->key(0);require(defaults[k]->times.size()==1,"NLL identity not constant");for(unsigned j=0;j<(k==0?4:k==1?9:3);++j)require(value[j]==(k==0&&j==0?1.f:0.f),"NLL nonidentity default");}
  const auto&poses=bank.meta.at("poses");for(const auto&entry:common_clips){auto c=std::make_shared<Clip>(*entry.second);const auto&m=bank.meta.at("clips").at(entry.first);c->phases=json_times(m.at("phases"));require(c->phases.front()==c->first&&c->phases.back()==c->last,"NLL clip duration changed");for(unsigned n=0;n<379;++n)c->layer.insert(c->layer.end(),defaults.begin(),defaults.end());std::set<unsigned> assigned,mask;
   auto assign=[&](const Json&rows,bool constant){require(rows.is_array()&&rows.size()<=379*3,"NLL override count");for(const auto&row:rows){require(row.size()==2,"NLL override layout");auto index=integer(row[0],379*3-1);require(assigned.insert(index).second,"NLL duplicate override");auto curve=find(base,&bank,row[1]);require(!constant||curve->times.size()==1,"NLL static pose contains curve");c->layer[index]=curve;mask.insert(index/3);}};
   assign(poses.at(name(m.at("pose"))),true);assign(m.at("curves"),false);std::vector<unsigned> stated;for(const auto&v:m.at("mask"))stated.push_back(integer(v,378));c->mask.assign(mask.begin(),mask.end());require(stated==c->mask,"NLL mask mismatch");curve_kinds(c->layer);
   require(m.at("mounts").size()==a.mounts.size()*3,"NLL mount curves");for(const auto&v:m["mounts"])c->mounts.push_back(find(base,&bank,v));curve_kinds(c->mounts);out.clips.emplace(c->name,ClipRef{c});
  }
  require(out.clips.count(out.neutral),"NLM missing neutral clip");a.unique_channels+=bank.channels.size();a.packed_curve_bytes+=bank.bytes;a.contexts.push_back(std::move(out));
 }
 require(common_clips.count(a.correction_clip)&&a.correction_phases.front()==0&&a.correction_phases.back()<=common_clips.at(a.correction_clip)->last,"NLS clip coverage");return a;
}
} // namespace owned::skm
