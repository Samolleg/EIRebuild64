#pragma once
#include "ground_cover_wood.h"
#include <cstring>

namespace owned::ground_cover {

// MOB unit records contain their own model and authored position. They are
// not entries in the loader's static-object slice.
inline bool cave_undead_site(const std::uint8_t* model, std::uint32_t model_bytes,
    const std::uint8_t* position, std::uint32_t position_bytes, std::array<float,2>& site) {
    if (!model || !position || position_bytes != 12) return false;
    std::string name(reinterpret_cast<const char*>(model),model_bytes);
    while(!name.empty() && name.back()=='\0')name.pop_back();
    for(auto& c:name)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
    if(name.find('\0')!=std::string::npos)return false;
    const bool undead=name=="unhusk"||name=="unhuzm"||name=="unhuzf"||
        name.compare(0,6,"unmoba")==0||name.compare(0,6,"unmosh")==0||
        name.compare(0,6,"unmosk")==0||name.compare(0,6,"unmozo")==0||name.compare(0,6,"unmocu")==0;
    if(!undead)return false;
    float p[3]{};std::memcpy(p,position,sizeof(p));
    if(!std::isfinite(p[0])||!std::isfinite(p[1])||!std::isfinite(p[2]))return false;
    site={p[0],p[1]};return true;
}

inline GcMesh make_cave_patch() {
    GcMesh m; Rng r(711);
    const float color[4]{.7f,.76f,.6f,.1f};
    add(m,{0,0,.025f},{0,0,1},color,.5f,.5f,1,0);
    for(unsigned k=0;k<12;++k) {
        const float a=k*6.2831853f/12,rad=r.r(.36f,.5f);
        add(m,{std::cos(a)*rad,std::sin(a)*rad,.008f},{0,0,1},color,.5f+.5f*std::cos(a),.5f+.5f*std::sin(a),.88f,0);
    }
    for(unsigned k=0;k<12;++k)tri(m,0,1+k,1+(k+1)%12);
    return m;
}

inline GcMesh make_cave_fern() {
    GcMesh m;
    const float color[4]{.26f,.48f,.18f,.25f};
    for(unsigned frond=0;frond<7;++frond) {
        const float a=frond*6.2831853f/7,c=std::cos(a),s=std::sin(a);
        for(unsigned leaf=1;leaf<=7;++leaf) {
            const float t=leaf/8.f,x=t*.65f,z=.65f*std::sin(t*2.f),w=.16f*(1-t)+.025f;
            for(float sign:{-1.f,1.f}) {
                const auto b=std::uint32_t(m.v.size());
                add(m,{c*x,s*x,z},{0,0,1},color,0,0,.9f,0);
                add(m,{c*(x+.08f)-s*w*sign,s*(x+.08f)+c*w*sign,z-.03f},{0,0,1},color,1,.5f,1,0);
                add(m,{c*(x+.09f),s*(x+.09f),z+.035f},{0,0,1},color,0,1,1,0);
                tri(m,b,b+1,b+2);
            }
        }
    }
    return m;
}

inline GcMesh make_cave_roots() {
    GcMesh m;Rng r(83);
    for(unsigned k=0;k<6;++k) {
        const float y=(k-2.5f)*.08f,h=r.r(.55f,1.f);
        add_prism(m,{{.3f,y,h},{.18f,y,.75f*h},{.05f,y+.06f,.4f*h},{-.05f,y+.04f,.02f}},
                  .018f,.003f,4,{.31f,.27f,.17f},.9f,false,{});
    }
    return m;
}

inline void cave_ellipsoid(GcMesh& m,V3 center,V3 radius,V3 tone) {
    const float color[4]{tone.x,tone.y,tone.z,1};
    const auto first=std::uint32_t(m.v.size());
    for(unsigned ring=0;ring<=6;++ring)for(unsigned k=0;k<=8;++k) {
        const float p=ring*3.14159265f/6,a=k*6.2831853f/8;
        const V3 n{std::sin(p)*std::cos(a),std::sin(p)*std::sin(a),std::cos(p)};
        add(m,center+V3{n.x*radius.x,n.y*radius.y,n.z*radius.z},n,color,k/8.f,ring/6.f,.85f+.15f*n.z,0);
    }
    for(unsigned ring=0;ring<6;++ring)for(unsigned k=0;k<8;++k) {
        const auto b=first+ring*9+k;tri(m,b,b+1,b+9);tri(m,b+1,b+10,b+9);
    }
}

inline GcMesh make_cave_mushrooms() {
    GcMesh m;
    for(unsigned k=0;k<3;++k) {
        const float x=(k-1.f)*.28f,y=k==1?.1f:-.12f,h=.48f+k*.09f;
        add_prism(m,{{x,y,.01f},{x+.02f,y,h}},.035f,.028f,6,{.38f,.57f,.49f},.8f,false,{});
        cave_ellipsoid(m,{x+.02f,y,h},{.2f,.2f,.09f},{.24f,.65f,.48f});
    }
    return m;
}

inline GcMesh make_cave_slab(bool shard=false) {
    GcMesh m;Rng r(shard?919:817);
    const float color[4]{.65f,.63f,.58f,.1f};
    const unsigned sides=shard?5:6;
    const float height=shard?.35f:.13f;
    for(unsigned layer=0;layer<2;++layer)for(unsigned k=0;k<sides;++k) {
        const float a=k*6.2831853f/sides,rad=.4f+(mesh_hash(k+917)%100)*.001f;
        add(m,{std::cos(a)*rad,std::sin(a)*rad,layer?height*(shard?r.r(.4f,1.f):1):0},
            {std::cos(a)*.2f,std::sin(a)*.2f,.95f},color,k/float(sides),float(layer),layer?1:.7f,0);
    }
    for(unsigned k=1;k+1<sides;++k)tri(m,sides,sides+k,sides+k+1);
    for(unsigned k=0;k<sides;++k){const unsigned n=(k+1)%sides;tri(m,k,n,k+sides);tri(m,n,n+sides,k+sides);}
    return m;
}

inline GcMesh make_cave_bones() {
    GcMesh m;Rng r(919);
    for(unsigned k=0;k<5;++k) {
        const float a=r.r(0,6.28f),x=r.r(-.25f,.25f),y=r.r(-.25f,.25f),len=r.r(.2f,.4f);
        V3 p{x,y,.035f},q{x+std::cos(a)*len,y+std::sin(a)*len,.035f};
        add_prism(m,{p,q},.022f,.019f,6,{.72f,.69f,.56f},1,false,{});
        cave_ellipsoid(m,p,{.035f,.027f,.027f},{.72f,.69f,.56f});
        cave_ellipsoid(m,q,{.035f,.027f,.027f},{.72f,.69f,.56f});
    }
    return m;
}

inline GcMesh make_cave_web() {
    GcMesh m;const float color[4]{.52f,.5f,.44f,1};
    const auto thread=[&](V3 a,V3 b) {
        const auto start=std::uint32_t(m.v.size());
        for(const auto p:{a+V3{0,.0025f,0},a-V3{0,.0025f,0},b+V3{0,.0025f,0},b-V3{0,.0025f,0}})
            add(m,p,{0,0,1},color,0,0,1,0);
        tri(m,start,start+1,start+2);tri(m,start+1,start+3,start+2);
    };
    for(unsigned k=0;k<8;++k) {
        const float a=k*6.2831853f/8,b=(k+1)*6.2831853f/8;
        thread({0,0,.2f},{std::cos(a)*.45f,std::sin(a)*.45f,.025f});
        for(unsigned ring=1;ring<=4;++ring) {
            const float radius=ring*.11f,z=.2f-radius*.38f;
            thread({std::cos(a)*radius,std::sin(a)*radius,z},{std::cos(b)*radius,std::sin(b)*radius,z});
        }
    }
    return m;
}

inline GcMesh make_cave_crystals() {
    GcMesh m;
    const float color[4]{.55f,.6f,.63f,.4f};
    for(unsigned k=0;k<4;++k) {
        const float x=(k-1.5f)*.18f,y=k%2?.15f:-.12f,h=.4f+k*.12f;
        const auto first=std::uint32_t(m.v.size());
        for(unsigned v=0;v<6;++v){const float a=v*1.04719755f;add(m,{x+.1f*std::cos(a),y+.1f*std::sin(a),.03f},{std::cos(a),std::sin(a),.3f},color,0,0,.85f,0);}
        add(m,{x+.02f,y,h},{0,0,1},color,0,0,1,0);
        for(unsigned v=0;v<6;++v)tri(m,first+v,first+(v+1)%6,first+6);
    }
    return m;
}

inline GcMesh make_cave_skull() {
    GcMesh m;cave_ellipsoid(m,{0,0,.35f},{.34f,.4f,.35f},{.72f,.69f,.56f});
    // Dark recessed sockets on the forward face, with a small lower jaw.
    for(float x:{-.13f,.13f})cave_ellipsoid(m,{x,-.37f,.4f},{.09f,.04f,.09f},{.12f,.11f,.09f});
    cave_ellipsoid(m,{0,-.17f,.12f},{.24f,.25f,.1f},{.72f,.69f,.56f});
    return m;
}
} // namespace owned::ground_cover
