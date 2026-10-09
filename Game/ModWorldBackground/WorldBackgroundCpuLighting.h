#pragma once
#include "WorldBackgroundVisual.h"
#include "WorldBackgroundEnvironment.h"
#include <tuple>
#include <set>
#include <cstring>
#include <float.h>

#pragma pack(push,8)
namespace WorldBackground {
class CpuLightingRounding {
    unsigned saved=0;
public:
    CpuLightingRounding(){_controlfp_s(&saved,0,0);unsigned ignored=0;_controlfp_s(&ignored,_RC_CHOP,_MCW_RC);}
    ~CpuLightingRounding(){unsigned ignored=0;_controlfp_s(&ignored,saved,_MCW_RC);}
};
inline bool CpuTraceSun(const ShadowHeights& heights,int64_t sx,int64_t sy,int x,int y,float z,const VisualEnvironment& e){
    const int ax=e.terrainSteps[0],ay=e.terrainSteps[1];
    const int dx=(ax<0||(ax==0&&std::signbit(e.terrainSun[0])))?-1:1;
    const int dy=(ay<0||(ay==0&&std::signbit(e.terrainSun[1])))?-1:1;
    const float dzx=e.terrainSun[0]*dx,dzy=e.terrainSun[1]*dy;
    const int a=std::abs(ax),b=std::abs(ay),critical=2*a-b;int error=0;
    if(dzx<0||dzy<0)return e.light[5]<0;
    for(int i=0;i<(std::max)(a,b);++i){
        float height=0;if(!heights.Read(sx,sy,x,y,height))return true;
        if(height>z)return false;
        bool crossed=false;
        if(error<=critical){x+=dx;z=float(z+dzx);error+=2*b;
            if(x==-1){x=31;--sx;crossed=true;}else if(x==33){x=1;++sx;crossed=true;}}
        if(!crossed&&(error>=a||a<=b)){y+=dy;z=float(z+dzy);error-=2*a;
            if(y==-1){y=31;--sy;crossed=true;}else if(y==33){y=1;++sy;crossed=true;}}
        if(crossed&&!heights.Read(sx,sy,x,y,height))return true;
    }
    return true;
}
inline std::array<float,3> CpuSunRGB(const LightingInputs& v,const VisualEnvironment& e,bool visible){
    std::array<float,3> result={};float ambientFactor=1,directFactor=1;
    if(!v.layer&&v.ownerMaterial!=65535&&!std::signbit(v.depth)){
        const float absorption=float(double(e.terrainSun[2])*v.transparencyInverse*v.depth*v.depth);
        const float af=float(1.0-double(absorption));ambientFactor=af>0?af:0;
        const float df=float(1.0-double(e.sunPosition[2])*e.sunPosition[2]*absorption);directFactor=df>0?df:0;
    }
    const double dot=-(double(e.light[3])*v.normal[0]+double(e.light[4])*v.normal[1]+double(e.light[5])*v.normal[2]);
    const float cosine=float(dot*directFactor);
    for(size_t c=0;c<3;++c){
        const float ambient=float(e.light[c+6]*ambientFactor);
        if(!v.layer&&!visible)result[c]=(std::min)(float(ambient+v.emission[c]),1.0f);
        else {const float direct=dot<=0?v.emission[c]:(std::min)(float(double(e.light[c])*cosine+v.emission[c]),1.0f);
            result[c]=(std::max)(direct,ambient);}
    }
    return result;
}
inline uint32_t CpuPackRGB(const std::array<float,3>& rgb){
    uint32_t result=0;for(float c:rgb){const float bounded=(std::max)(0.0f,(std::min)(c,1.0f));
        result=(result<<8)|uint32_t(bounded*255.0f);}return result;
}
// Worker-only cache: shade each source vertex once, not each expanded triangle.
// No native light/terrain functions, global state, or D3D calls.
class CpuLighting {
    using KeyType=std::tuple<size_t,uint32_t,uint32_t,uint32_t>;
    struct Colors {std::array<uint32_t,1089> rgb;};
    std::map<KeyType,Colors> colors;
    std::map<KeyType,std::array<bool,1089>> shadows;
    VisualInvalidation shadowChanges;uint64_t shadowRevision=0;
    VisualInvalidation changes;uint64_t revision=0;
public:
    uint64_t shadedVertices=0,tracedVertices=0;
    bool Apply(RenderSnapshot& target,const std::vector<std::shared_ptr<const LightingPacket>>& packets,
        const VisualEnvironment& env,const RenderSnapshot* previous,const std::function<bool()>& cancelled={}){
        const uint32_t required=VisualLight|VisualSunPosition|VisualTerrainSun;
        if((env.valid&required)!=required)return false;
        const uint64_t next=changes.Update(target.view.mapEpoch,0,env,required);
        if(next!=revision){colors.clear();revision=next;}
        CpuLightingRounding rounding;
        auto shadowEnv=env;shadowEnv.light={};shadowEnv.light[5]=env.light[5]<0?-1.0f:1.0f;
        const auto sr=shadowChanges.Update(target.view.mapEpoch,0,shadowEnv,VisualTerrainSun|VisualLight);
        if(sr!=shadowRevision){shadows.clear();shadowRevision=sr;}
        std::set<KeyType> used;bool complete=true;
        for(size_t bi=0;bi<target.batches.size();++bi){
            if(cancelled&&cancelled())throw std::runtime_error("CPU lighting cancelled");
            auto& batch=target.batches[bi];auto& patch=batch.patch;
            const auto& identity=target.source->neighbors[batch.neighbor].placement.entry.identity;
            const LightingPacket* packet=nullptr;
            for(const auto& p:packets)if(p->Epoch()==target.view.mapEpoch&&p->MapIdentity().profile==identity.profile&&
                p->MapIdentity().allod==identity.allod&&p->MapIdentity().zone==identity.zone&&p->MapIdentity().mpr==identity.mpr){packet=p.get();break;}
            if(!packet){target.diagnostics.push_back("CPU lighting packet unavailable: "+identity.mpr);complete=false;continue;}
            if(patch.lightRefs.size()!=patch.vertices.size())throw std::runtime_error("CPU lighting reference mismatch");
            KeyType last(size_t(-1),0,0,0);const Colors* cached=nullptr;
            for(size_t i=0;i<patch.vertices.size();++i){
                const auto& ref=patch.lightRefs[i];const KeyType key(batch.neighbor,ref.sectorX,ref.sectorY,ref.layer);
                if(key!=last){
                    auto found=colors.find(key);
                    if(found==colors.end()){
                        const SectorLightingInputs* inputs=nullptr;
                        for(const auto& s:packet->Sectors())if(s.x==ref.sectorX&&s.y==ref.sectorY){inputs=&s;break;}
                        if(!inputs||ref.layer>1)throw std::runtime_error("CPU lighting sector unavailable");
                        const auto& vertices=ref.layer?inputs->overlay:inputs->land;
                        if(vertices.size()!=1089)throw std::runtime_error("CPU lighting layer unavailable");
                        auto shadow=shadows.find(key);
                        if(shadow==shadows.end()){
                            std::array<bool,1089> visibility;
                            for(size_t j=0;j<1089;++j){
                                if((j&63)==0&&cancelled&&cancelled())throw std::runtime_error("CPU shadow cancelled");
                                float z=0;packet->Heights().Read(ref.sectorX,ref.sectorY,int(j%33),int(j/33),z);
                                visibility[j]=ref.layer||!env.terrainSteps[2]||CpuTraceSun(packet->Heights(),ref.sectorX,ref.sectorY,int(j%33),int(j/33),z,env);
                                if(!ref.layer&&env.terrainSteps[2])++tracedVertices;
                            }
                            shadow=shadows.emplace(key,std::move(visibility)).first;
                        }
                        Colors computed;
                        for(size_t j=0;j<1089;++j){
                            if((j&63)==0&&cancelled&&cancelled())throw std::runtime_error("CPU lighting cancelled");
                            computed.rgb[j]=CpuPackRGB(CpuSunRGB(vertices[j],env,shadow->second[j]));++shadedVertices;
                        }
                        found=colors.emplace(key,std::move(computed)).first;
                    }
                    used.insert(key);cached=&found->second;last=key;
                }
                if(ref.vertex>=1089)throw std::runtime_error("CPU lighting vertex unavailable");
                patch.vertices[i].diffuse=(patch.vertices[i].diffuse&0xff000000u)|cached->rgb[ref.vertex];
            }
            // Recompare the FINAL lit bytes. White intermediate data must never
            // decide a KEEP operation or force unchanged geometry to be uploaded.
            auto& visual=target.meshes[bi];visual.geometryRevision=target.view.contentRevision;
            if(previous&&previous->view.mapEpoch==target.view.mapEpoch&&previous->source==target.source)
                for(const auto& old:previous->meshes)if(old.key==visual.key){
                    if(old.mesh.vertexCount==visual.mesh.vertexCount&&!std::memcmp(old.mesh.vertices,visual.mesh.vertices,patch.vertices.size()*sizeof(ET_Vertex)))visual.geometryRevision=old.geometryRevision;
                    break;
                }
        }
        // Residency bounds the cache, including after camera moves.
        for(auto it=colors.begin();it!=colors.end();)if(!used.count(it->first))it=colors.erase(it);else ++it;
        for(auto it=shadows.begin();it!=shadows.end();)if(!used.count(it->first))it=shadows.erase(it);else ++it;
        return complete;
    }
};
}
#pragma pack(pop)
