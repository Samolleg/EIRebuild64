#pragma once
#include "GamePoseReader.h"
#include "UnityWeaponCatalog.h"
#include "UnityWeaponGripData.h"
namespace UnityGame {
struct WeaponGeometry {
 const Geometry* data=nullptr;
 std::vector<std::pair<std::string,std::pair<unsigned,unsigned>>> ranges;
 bool Load(const Geometry* geometry){data=geometry;ranges.clear();if(!data)return false;for(const auto& item:*data)ranges.push_back({item.first,{0,unsigned(item.second.size()/4)}});return true;}
 bool Point(const std::string& name,unsigned index,UnityBody::V& out)const{if(!data)return false;auto i=data->find(name);if(i==data->end()||index>=i->second.size())return false;out=i->second[index];return true;}
 bool Centre(const UnityWeaponCatalog::Spec&s,UnityBody::V&point)const{
  if(std::string(s.name)==UnityHandsData::VerifiedWeapon)return false;std::vector<double>x,z;for(unsigned i=0;i<s.count;++i){UnityBody::V p;if(!Point(s.name,UnityWeaponCatalog::SampleIndices[s.samples+i],p))return false;x.push_back(p.x);z.push_back(p.z);}if(x.empty())return false;
  auto median=[](std::vector<double>&v){std::sort(v.begin(),v.end());return(v[(v.size()-1)/2]+v[v.size()/2])*.5;};UnityBody::V candidate={median(x),0,median(z)};if(std::sqrt(UnityBody::Dot(UnityBody::Sub(candidate,s.point),UnityBody::Sub(candidate,s.point)))>=.15)return false;point=candidate;return true;
 }
 bool BoundsCentre(const std::string& name,UnityBody::V& out)const{if(!data)return false;auto i=data->find(name);if(i==data->end()||i->second.empty())return false;UnityBody::V lo{1e9,1e9,1e9},hi{-1e9,-1e9,-1e9};for(auto p:i->second){lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}out=UnityBody::Scale(UnityBody::Add(lo,hi),.5);return true;}
};
}