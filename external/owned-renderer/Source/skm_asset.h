#pragma once
// Shared affine math for the NLC/NLL baked motion runtime.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace owned::skm {
using Matrix=std::array<double,16>; // row major, column vectors
inline Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
inline Matrix mul(const Matrix&a,const Matrix&b){Matrix c{};for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)for(unsigned k=0;k<4;++k)c[i*4+j]+=a[i*4+k]*b[k*4+j];return c;}
inline std::array<double,3> vector(const Matrix&m,const double*p){return {m[0]*p[0]+m[1]*p[1]+m[2]*p[2],m[4]*p[0]+m[5]*p[1]+m[6]*p[2],m[8]*p[0]+m[9]*p[1]+m[10]*p[2]};}
inline std::array<double,3> normal(const Matrix&m,const double*p){
 const double a=m[0],b=m[1],c=m[2],d=m[4],e=m[5],f=m[6],g=m[8],h=m[9],i=m[10];
 const double det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
 if(std::abs(det)<1e-15)throw std::runtime_error("SKM singular placement");
 std::array<double,3> n{((e*i-f*h)*p[0]+(f*g-d*i)*p[1]+(d*h-e*g)*p[2])/det,
 ((c*h-b*i)*p[0]+(a*i-c*g)*p[1]+(b*g-a*h)*p[2])/det,
 ((b*f-c*e)*p[0]+(c*d-a*f)*p[1]+(a*e-b*d)*p[2])/det};
 double length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);if(length<1e-12)throw std::runtime_error("SKM zero normal");for(auto&v:n)v/=length;return n;
}
inline std::uint64_t hash(const std::vector<std::uint8_t>&b){std::uint64_t h=14695981039346656037ull;for(auto v:b)h=(h^v)*1099511628211ull;return h;}
} // namespace owned::skm
