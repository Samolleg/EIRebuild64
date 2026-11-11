// Offline Nala bind mesh and pose extraction.
// Includes the file-driven CPU shape program and bone/contact payloads.
#include "nala_pose_asset.h"
#include "nala/UnityCompleteData.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
using namespace UnityBody;
using Bytes=owned::skn::nala_pose::Bytes;
void u32(Bytes& out,unsigned value) {
  for(unsigned i=0;i<4;++i)out.push_back(std::uint8_t(value>>(8*i)));
}
void number(Bytes& out,double value) {
  if(!std::isfinite(value))throw std::runtime_error("nonfinite source pose value");
  std::uint64_t bits;std::memcpy(&bits,&value,8);
  for(unsigned i=0;i<8;++i)out.push_back(std::uint8_t(bits>>(8*i)));
}
void matrix(Bytes& out,const M& m) {
  for(unsigned r=0;r<3;++r) {
    for(unsigned c=0;c<3;++c)number(out,m.r[r][c]);
    number(out,r==0?m.t.x:r==1?m.t.y:m.t.z);
  }
}
void set32(Bytes& out,unsigned at,unsigned value) {
  for(unsigned i=0;i<4;++i)out.at(at+i)=std::uint8_t(value>>(8*i));
}
Bytes encode(const Data& d) {
  constexpr unsigned strides[]={0,328,100,96,4,20,12,20,136,24};
  std::array<Bytes,10> tables;
  auto& nodes=tables[1];
  for(unsigned i=0;i<d.rest.size();++i) {
    u32(nodes,unsigned(d.parent.at(i)));u32(nodes,unsigned(d.driver.at(i)));
    matrix(nodes,d.rest[i]);matrix(nodes,d.local.at(i));matrix(nodes,d.stretch.at(i));
    for(double q:d.restQ.at(i))number(nodes,q);
  }
  for(unsigned i=0;i<d.bindInverse.size();++i) {
    u32(tables[2],d.bindNode.at(i));matrix(tables[2],d.bindInverse[i]);
  }
  for(const auto& m:d.gameBind)matrix(tables[3],m);
  for(unsigned i:d.order)u32(tables[4],i);
  for(unsigned side=0;side<2;++side) {
    for(const auto& s:d.dual[side]) {
      for(unsigned i:{side,s.a,s.b,s.i1,s.i2})u32(tables[5],i);
    }
    for(const auto& s:d.damped[side]) {
      for(unsigned i:{side,s.own,s.target})u32(tables[6],i);
    }
    for(const auto& s:d.semi[side]) {
      for(unsigned i:{side,s.own,s.target})u32(tables[7],i);
      number(tables[7],s.weight);
    }
  }
  for(const auto& s:d.limits) {
    u32(tables[8],s.own);u32(tables[8],s.parent);matrix(tables[8],s.frame);
    for(double v:{s.low,s.high,s.y,s.z})number(tables[8],v);
  }
  for(double v:{d.forward.x,d.forward.y,d.forward.z})number(tables[9],v);
  Bytes out;for(unsigned i:{16u,9u,16u,0u})u32(out,i);
  out.resize(160);
  for(unsigned kind=1;kind<tables.size();++kind) {
    while(out.size()%16)out.push_back(0);
    const unsigned at=16+(kind-1)*16;
    set32(out,at,kind);set32(out,at+4,unsigned(out.size()));
    set32(out,at+8,unsigned(tables[kind].size()/strides[kind]));
    set32(out,at+12,strides[kind]);
    out.insert(out.end(),tables[kind].begin(),tables[kind].end());
  }
  return out;
}
void write(const std::filesystem::path& path,const Bytes& bytes) {
  if(std::filesystem::exists(path))throw std::runtime_error("output already exists");
  std::ofstream file(path,std::ios::binary);
  file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
  file.close();if(!file)throw std::runtime_error("cannot write pose section");
}
}
#include "bind_mesh.h"
int main(int argc,char** argv) try {
  if(argc==3 && std::string(argv[1])=="--export-bind-mesh") {
    const Data source=CompleteData();Data shaped;
    NalaMorphs::Values values;NalaMorphs::Status status;
    if(!NalaMorphs::Resolve({},values,status))throw std::runtime_error("default shape resolver failed");
    auto profile=NalaBodyShape::Profile(values);
    if(!NalaBodyShape::Build(source,values,std::vector<double>(EditorSchema::BoneCount*9),shaped,profile,NalaUpperArcIdentity::Apply))
      throw std::runtime_error("default bind shape failed");
    const auto bytes=bind_mesh(shaped);
    write(argv[2],bytes);
    std::cout<<"bind SKN: "<<shaped.vertices.size()<<" sampling vertices, "<<NalaLidContact::RenderCount
      <<" visible vertices, "<<std::size(NalaLidContact::Indices)/3<<" triangles, "<<shaped.bindInverse.size()
      <<" skin slots; sections 8/9/13/14; "<<bytes.size()<<" bytes\n"
      <<"Shape data is file-driven; full engine build and game acceptance belong to the coordinator.\n";
    return 0;
  }
  if(argc!=3 || std::string(argv[1])!="--export-pose-section") {
    std::cerr<<"usage: nala-to-skn --export-pose-section OUTPUT.section13\n"
      "or: nala-to-skn --export-bind-mesh OUTPUT.skn\nRuntime integration remains incomplete.\n";
    return 2;
  }
  const Data source=CompleteData();
  const auto bytes=encode(source);
  // Decode the actual bytes before publishing, so unsupported/invalid source
  // data cannot be mistaken for a usable section. Re-encoding must be exact.
  const auto decoded=owned::skn::nala_pose::decode(bytes);
  if(encode(decoded)!=bytes)throw std::runtime_error("pose section round-trip changed source data");
  write(argv[2],bytes);
  std::cout<<"section13 v1: "<<source.rest.size()<<" nodes, "
    <<source.bindInverse.size()<<" bind slots, "<<source.limits.size()
    <<" limits, "<<bytes.size()<<" bytes; no SKN mesh or DDS emitted\n";
  return 0;
} catch(const std::exception& error) {
  std::cerr<<"nala-to-skn: "<<error.what()<<'\n';return 1;
}
