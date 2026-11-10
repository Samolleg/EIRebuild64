#pragma once
// SKN section 13 v1. Bone-only data: no model tables or vertex generation.
#include "nala/UnityBodyKernel.h"
#include <cstring>
#include <stdexcept>

namespace owned::skn::nala_pose {
using UnityBody::Data;
using UnityBody::M;
using Bytes = std::vector<std::uint8_t>;
struct Table { unsigned at=0, count=0, stride=0; };
class Reader {
  const Bytes& bytes;
public:
  explicit Reader(const Bytes& b):bytes(b) {}
  void range(std::uint64_t at, std::uint64_t size) const {
    if(at>bytes.size() || size>bytes.size()-at)
      throw std::runtime_error("SKN section 13 range");
  }
  unsigned u32(std::uint64_t at) const {
    range(at,4); unsigned x=0;
    for(unsigned i=0;i<4;++i)x|=unsigned(bytes[at+i])<<(8*i);
    return x;
  }
  double number(std::uint64_t at) const {
    range(at,8); std::uint64_t bits=0;
    for(unsigned i=0;i<8;++i)bits|=std::uint64_t(bytes[at+i])<<(8*i);
    double value;std::memcpy(&value,&bits,8);
    if(!std::isfinite(value))throw std::runtime_error("SKN section 13 nonfinite value");
    return value;
  }
  M matrix(unsigned at) const {
    M m;
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col) {
      const double v=number(at+8*(row*4+col));
      if(col<3)m.r[row][col]=v;
      else (row==0?m.t.x:row==1?m.t.y:m.t.z)=v;
    }
    const auto inverse=UnityBody::Inverse(m);
    for(const auto& row:inverse.r)for(double v:row)
      if(!std::isfinite(v))throw std::runtime_error("SKN section 13 singular matrix");
    return m;
  }
};
inline Data decode(const Bytes& bytes) {
  Reader r(bytes);
  constexpr unsigned strides[]={0,328,100,96,4,20,12,20,136,24};
  if(r.u32(0)!=16 || r.u32(4)!=9 || r.u32(8)!=16 || r.u32(12))
    throw std::runtime_error("SKN section 13 header");
  std::array<Table,10> tables{};
  std::vector<std::pair<std::uint64_t,std::uint64_t>> spans{{0,160}};
  r.range(0,160);
  for(unsigned i=0;i<9;++i) {
    unsigned at=16+16*i,kind=r.u32(at);
    if(!kind || kind>=tables.size() || tables[kind].stride)
      throw std::runtime_error("SKN section 13 duplicate/unknown table");
    Table t{r.u32(at+4),r.u32(at+8),r.u32(at+12)};
    if(t.stride!=strides[kind] || t.at%16 || t.count>65535)
      throw std::runtime_error("SKN section 13 table stride/alignment/count");
    std::uint64_t end=std::uint64_t(t.at)+std::uint64_t(t.count)*t.stride;
    r.range(t.at,end-t.at);
    if(t.count) {
      for(auto span:spans)if(t.at<span.second && end>span.first)
        throw std::runtime_error("SKN section 13 overlapping tables");
      spans.emplace_back(t.at,end);
    }
    tables[kind]=t;
  }
  const unsigned count=tables[1].count;
  if(!count || !tables[2].count || tables[3].count!=15 ||
      tables[4].count!=count || tables[9].count!=1)
    throw std::runtime_error("SKN section 13 required table count");
  Data d;
  auto node=[&](unsigned v) {
    if(v>=count)throw std::runtime_error("SKN section 13 node reference");
    return v;
  };
  auto each=[&](unsigned kind,auto function) {
    const auto t=tables[kind];
    for(unsigned i=0;i<t.count;++i)function(t.at+i*t.stride);
  };
  d.children.resize(count);
  std::array<bool,15> drivers{};
  each(1,[&](unsigned at) {
    const auto parent=static_cast<std::int32_t>(r.u32(at));
    const auto driver=static_cast<std::int32_t>(r.u32(at+4));
    if(parent < -1 || parent>=int(count) || driver < -1 || driver>=15)
      throw std::runtime_error("SKN section 13 parent/driver");
    if(driver>=0) {
      if(drivers[driver])throw std::runtime_error("SKN section 13 duplicate driver");
      drivers[driver]=true;
    }
    unsigned index=unsigned(d.parent.size());
    if(parent>=0)d.children[parent].push_back(index);
    d.parent.push_back(parent);d.driver.push_back(driver);
    d.rest.push_back(r.matrix(at+8));d.local.push_back(r.matrix(at+104));
    d.stretch.push_back(r.matrix(at+200));
    UnityBody::Q q{};double norm=0;
    for(unsigned k=0;k<4;++k){q[k]=r.number(at+296+8*k);norm+=q[k]*q[k];}
    if(std::abs(norm-1)>1e-10)throw std::runtime_error("SKN section 13 rest quaternion");
    d.restQ.push_back(q);
  });
  if(std::find(drivers.begin(),drivers.end(),false)!=drivers.end())
    throw std::runtime_error("SKN section 13 missing game driver");
  each(2,[&](unsigned at) {
    d.bindNode.push_back(node(r.u32(at)));d.bindInverse.push_back(r.matrix(at+4));
  });
  unsigned game=0;
  each(3,[&](unsigned at){d.gameBind[game++]=r.matrix(at);});
  std::vector<bool> visited(count,false);
  each(4,[&](unsigned at) {
    auto i=node(r.u32(at));
    if(visited[i] || (d.parent[i]>=0 && !visited[d.parent[i]]))
      throw std::runtime_error("SKN section 13 hierarchy order/cycle");
    visited[i]=true;d.order.push_back(i);
  });
  auto side=[&](unsigned at) {
    unsigned v=r.u32(at);
    if(v>1)throw std::runtime_error("SKN section 13 controller side");
    return v;
  };
  auto child=[&](unsigned v) {
    v=node(v);
    if(d.parent[v]<0)throw std::runtime_error("SKN section 13 root controller");
    return v;
  };
  each(5,[&](unsigned at) {
    d.dual[side(at)].push_back({child(r.u32(at+4)),child(r.u32(at+8)),
      node(r.u32(at+12)),node(r.u32(at+16))});
  });
  each(6,[&](unsigned at) {
    d.damped[side(at)].push_back({child(r.u32(at+4)),node(r.u32(at+8))});
  });
  each(7,[&](unsigned at) {
    double w=r.number(at+12);
    if(w<0 || w>1)throw std::runtime_error("SKN section 13 semi weight");
    d.semi[side(at)].push_back({child(r.u32(at+4)),node(r.u32(at+8)),w});
  });
  each(8,[&](unsigned at) {
    UnityBody::JointLimit limit{node(r.u32(at)),node(r.u32(at+4)),
      r.matrix(at+8),r.number(at+104),r.number(at+112),r.number(at+120),r.number(at+128)};
    if(limit.low>limit.high || limit.y<=0 || limit.z<=0 ||
       limit.y>=6.283185307179586 || limit.z>=6.283185307179586)
      throw std::runtime_error("SKN section 13 joint limit");
    d.limits.push_back(limit);
  });
  each(9,[&](unsigned at){d.forward={r.number(at),r.number(at+8),r.number(at+16)};});
  if(std::abs(UnityBody::Dot(d.forward,d.forward)-1)>1e-10)
    throw std::runtime_error("SKN section 13 forward vector");
  return d;
}

// Call with the shape-specific rig. No Compute(), skin loop, vertex arrays,
// body tables, garment generation or GPU readback occurs here.
inline std::vector<M> palette(const Data& rig,const std::array<M,15>& game) {
  auto world=UnityBody::Retarget(rig,game);
  UnityBody::LimitRetarget(rig,world);
  UnityBody::Control(rig,world);
  std::vector<M> result;result.reserve(rig.bindInverse.size());
  for(unsigned i=0;i<rig.bindInverse.size();++i)
    result.push_back(UnityBody::Product(world[rig.bindNode[i]],rig.bindInverse[i]));
  return result;
}
} // namespace owned::skn::nala_pose
