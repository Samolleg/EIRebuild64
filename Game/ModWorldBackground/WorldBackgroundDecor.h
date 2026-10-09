#pragma once
#include <tuple>
#include "WorldBackgroundFiles.h"
#include "WorldBackgroundCpuLighting.h"
#include <sstream>
#include <limits>

#pragma pack(push,8)
namespace WorldBackground { namespace Decor {
inline void Need(bool v,const char* why){if(!v)throw std::runtime_error(why);}
struct V3 {float x=0,y=0,z=0;};
inline float& Axis(V3& v,unsigned axis){return axis==0?v.x:axis==1?v.y:v.z;}
inline V3 Add(V3 a,V3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
struct Reader {
    const Blob& data;size_t pos=0;
    explicit Reader(const Blob& d):data(d){}
    template<class T>T Get(){Need(pos<=data.size()&&sizeof(T)<=data.size()-pos,"Truncated decoration field");T v;std::memcpy(&v,data.data()+pos,sizeof(v));pos+=sizeof(v);return v;}
    void Skip(size_t n){Need(pos<=data.size()&&n<=data.size()-pos,"Truncated decoration array");pos+=n;}
    float Real(){float v=Get<float>();Need(std::isfinite(v)&&std::abs(v)<100000,"Invalid decoration coordinate");return v;}
    V3 Vec(){return {Real(),Real(),Real()};}
    std::string Text(size_t n){Need(n<=1024&&n<=data.size()-pos,"Invalid decoration text");std::string s(reinterpret_cast<const char*>(data.data()+pos),n);pos+=n;while(!s.empty()&&!s.back())s.pop_back();Need(s.find('\0')==std::string::npos,"Embedded null in decoration text");return s;}
};
using Fields=std::map<uint32_t,Blob>;
inline Fields Chunks(const Blob& b){Fields result;Reader r(b);while(r.pos<b.size()){
    const auto id=r.Get<uint32_t>(),n=r.Get<uint32_t>();Need(n>=8&&n-8<=b.size()-r.pos,"Invalid decoration chunk");
    Need(result.emplace(id,Blob(b.begin()+r.pos,b.begin()+r.pos+n-8)).second,"Duplicate decoration field");r.pos+=n-8;}return result;}
inline std::string Text(const Fields& f,uint32_t id){const auto& b=f.at(id);Reader r(b);return r.Text(b.size());}
struct Object {uint32_t id=0,type=0;std::string model,primary,secondary;V3 position,morph;std::array<float,4> rotation;std::vector<std::string> parts;};
inline bool Allowed(uint32_t type){return type==53||type==54||type==55;}
inline std::vector<Object> ReadMob(const Blob& b,std::vector<std::string>& errors){
    Need(b.size()<=64u*1024u*1024u,"Decoration MOB exceeds budget");const auto top=Chunks(b);const auto root=Chunks(top.at(0xa000));
    const auto& objects=root.at(0xb000);Reader r(objects);std::vector<Object> result;std::set<uint32_t> ids;
    while(r.pos<objects.size()){
        const auto tag=r.Get<uint32_t>(),size=r.Get<uint32_t>();Need(size>=8&&size-8<=objects.size()-r.pos,"Invalid MOB object length");
        const size_t offset=r.pos;r.pos+=size-8;if(tag!=0xb001)continue; // Never parse NPC, trap, effect or script bodies.
        try{const auto f=Chunks(Blob(objects.begin()+offset,objects.begin()+offset+size-8));Object o;
            auto integer=[&](uint32_t id){const auto& raw=f.at(id);Need(raw.size()==4,"Bad object integer");return Reader(raw).Get<uint32_t>();};
            o.type=integer(0xb003);if(!Allowed(o.type))continue;o.id=integer(0xb002);Need(ids.insert(o.id).second,"Duplicate decoration ID");
            o.model=Key(Text(f,0xb006));o.primary=Key(Text(f,0xb007));o.secondary=Key(Text(f,0xb008));
            Need(!o.model.empty()&&!o.primary.empty(),"Missing decoration model/texture");
            const auto& pos=f.at(0xb009);Need(pos.size()==12,"Bad decoration position");o.position=Reader(pos).Vec();
            const auto& morph=f.at(0xb00c);Need(morph.size()==12,"Bad decoration complection");o.morph=Reader(morph).Vec();
            const auto& rotation=f.at(0xb00a);Need(rotation.size()==16,"Bad decoration rotation");Reader qr(rotation);for(auto& v:o.rotation)v=qr.Real();
            const auto& parts=f.at(0xb00d);Reader pr(parts);const auto n=pr.Get<uint32_t>();Need(n<=256,"Decoration part count exceeded");
            for(uint32_t i=0;i<n;++i){Need(pr.Get<uint32_t>()==0xb00d,"Bad decoration part tag");const auto len=pr.Get<uint32_t>();Need(len>=8,"Bad decoration part length");o.parts.push_back(Key(pr.Text(len-8)));}
            Need(pr.pos==parts.size(),"Decoration part list tail");Need(result.size()<20000,"Decoration count exceeded");result.push_back(std::move(o));
        }catch(const std::exception& e){if(errors.size()<100)errors.push_back(e.what());}
    }return result;
}
// MOB stores grace,strength,tall. FIG8 inner interpolation axis is strength.
inline float Mix(const float* v,unsigned n,V3 morph){if(n==1)return v[0];auto l=[](float a,float b,float t){return a+(b-a)*t;};
    return l(l(l(v[0],v[1],morph.y),l(v[2],v[3],morph.y),morph.x),l(l(v[4],v[5],morph.y),l(v[6],v[7],morph.y),morph.x),morph.z);}
inline V3 Rotate(V3 p,const std::array<float,4>& q){const float w=q[0],x=q[1],y=q[2],z=q[3];
    return {(1-2*y*y-2*z*z)*p.x+(2*x*y-2*w*z)*p.y+(2*x*z+2*w*y)*p.z,
        (2*x*y+2*w*z)*p.x+(1-2*x*x-2*z*z)*p.y+(2*y*z-2*w*x)*p.z,
        (2*x*z-2*w*y)*p.x+(2*y*z+2*w*x)*p.y+(1-2*x*x-2*y*y)*p.z};}
struct FigVertex {V3 position,normal;float u=0,v=0;};
struct Figure {uint32_t material=0,texture=0;std::vector<FigVertex> vertices;std::vector<uint16_t> indices;};
inline Figure ReadFigure(const Blob& b,V3 morph){
    Need(b.size()<=32u*1024u*1024u,"Figure byte budget");Reader r(b);const auto sig=r.Get<uint32_t>();Need(sig==0x38474946||sig==0x31474946,"Unsupported FIG version");const unsigned n=sig==0x38474946?8:1;
    const auto groups=r.Get<uint32_t>(),normals=r.Get<uint32_t>(),uvCount=r.Get<uint32_t>(),indexCount=r.Get<uint32_t>(),components=r.Get<uint32_t>(),lights=r.Get<uint32_t>(),user=r.Get<uint32_t>();
    const bool empty=groups==0&&normals==0&&uvCount==0&&indexCount==0&&components==0&&lights==0;
    Need((groups||empty)&&groups<=65535&&normals<=65535&&uvCount<=65535&&components<=65535&&indexCount<=600000&&indexCount%3==0&&user==0,"Invalid FIG counts");
    Figure result;result.material=r.Get<uint32_t>();result.texture=r.Get<uint32_t>();
    Need(b.size()==40ull+40*n+uint64_t(groups)*48*n+uint64_t(normals)*64+uint64_t(uvCount)*8+uint64_t(indexCount)*2+uint64_t(components)*6+uint64_t(lights)*4,"FIG layout mismatch");
    r.Skip(40*n);std::vector<V3> positions(size_t(groups)*4),ns(size_t(normals)*4);
    for(uint32_t g=0;g<groups;++g)for(unsigned axis=0;axis<3;++axis){float samples[8][4]={};for(unsigned m=0;m<n;++m)for(unsigned lane=0;lane<4;++lane)samples[m][lane]=r.Get<float>();
        for(unsigned lane=0;lane<4;++lane){float v[8]={};for(unsigned m=0;m<n;++m)v[m]=samples[m][lane];Axis(positions[size_t(g)*4+lane],axis)=Mix(v,n,morph);}}
    for(uint32_t g=0;g<normals;++g)for(unsigned axis=0;axis<4;++axis)for(unsigned lane=0;lane<4;++lane){const float v=r.Get<float>();if(axis<3)Axis(ns[size_t(g)*4+lane],axis)=v;}
    std::vector<std::array<float,2>> uv(uvCount);for(auto& v:uv){v[0]=r.Real();v[1]=r.Real();}
    result.indices.resize(indexCount);for(auto& v:result.indices){v=r.Get<uint16_t>();Need(v<components,"FIG index outside components");}
    result.vertices.reserve(components);for(uint32_t i=0;i<components;++i){const auto p=r.Get<uint16_t>(),normal=r.Get<uint16_t>(),t=r.Get<uint16_t>();Need(p<positions.size()&&normal<ns.size()&&t<uv.size(),"Invalid FIG component");
        auto v=FigVertex{positions[p],ns[normal],uv[t][0],uv[t][1]};for(float f:{v.position.x,v.position.y,v.position.z,v.normal.x,v.normal.y,v.normal.z})Need(std::isfinite(f)&&std::abs(f)<100000,"Invalid referenced FIG data");result.vertices.push_back(v);}
    r.Skip(size_t(lights)*4);Need(r.pos==b.size(),"FIG tail");return result;
}
inline V3 ReadBone(const Blob& b,V3 morph){Need(b.size()==12||b.size()==96,"Invalid BON position");Reader r(b);float v[3][8]={};const unsigned n=unsigned(b.size()/12);
    for(unsigned i=0;i<n;++i)for(unsigned a=0;a<3;++a)v[a][i]=r.Real();return {Mix(v[0],n,morph),Mix(v[1],n,morph),Mix(v[2],n,morph)};}
inline std::map<std::string,std::string> ReadHierarchy(const Blob& b){Reader r(b);auto count=r.Get<uint32_t>();Need(count>0&&count<=2048,"Invalid decoration hierarchy");std::map<std::string,std::string> result;
    while(count--){auto n=r.Get<uint32_t>();auto name=Key(r.Text(n));n=r.Get<uint32_t>();auto parent=Key(r.Text(n));Need(!name.empty()&&result.emplace(name,parent).second,"Duplicate figure part");}Need(r.pos==b.size(),"Hierarchy tail");return result;}

inline float Height(const Terrain& t,float x,float y){
    x=(std::max)(0.0f,(std::min)(x,float(t.sectorsX*32)-1));y=(std::max)(0.0f,(std::min)(y,float(t.sectorsY*32)-1));
    auto vertex=[&](int vx,int vy)->const Vertex*{
        if(vx<0||vy<0||vx>int(t.sectorsX*32)||vy>int(t.sectorsY*32))return nullptr;
        uint32_t sx=uint32_t(vx)/32,sy=uint32_t(vy)/32,lx=uint32_t(vx)%32,ly=uint32_t(vy)%32;
        if(!lx&&sx){--sx;lx=32;}if(!ly&&sy){--sy;ly=32;}
        for(const auto& sec:t.sectors)if(sec.x==sx&&sec.y==sy)return &sec.land[size_t(ly)*33+lx];return nullptr;};
    auto cross=[](const Vertex& a,const Vertex& b,const Vertex& c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);};
    auto plane=[&](const Vertex& a,const Vertex& b,const Vertex& c){const float ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z,vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;
        const float nz=ux*vy-uy*vx;if(std::abs(nz)<.01f)return (a.z+b.z+c.z)/3.0f;
        const float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,d=-(nx*a.x+ny*a.y+nz*a.z);return -(nx*x+ny*y+d)/nz;};
    int cx=int(std::nearbyint(x)),cy=int(std::nearbyint(y));
    for(unsigned tries=1;tries<=10;++tries){const auto a=vertex(cx,cy),b=vertex(cx+1,cy),c=vertex(cx,cy+1),d=vertex(cx+1,cy+1);
        if(!a||!d)return 0;if(b&&!c)return plane(*a,*b,*d);if(c&&!b)return plane(*a,*d,*c);if(!b||!c)return 0;
        if(cross(*b,*d,*a)<=0||cross(*c,*a,*d)<=0)return(c->z+d->z)*.5f;
        if((x-a->x)*(d->y-a->y)-(y-a->y)*(d->x-a->x)>0){
            if(tries<10){if((b->x-x)*(a->y-b->y)-(b->y-y)*(a->x-b->x)>0){--cy;continue;}
                if((b->x-x)*(d->y-b->y)-(b->y-y)*(d->x-b->x)<0){++cx;continue;}}
            return plane(*a,*b,*d);
        }
        if(tries<10){if((c->x-x)*(a->y-c->y)-(c->y-y)*(a->x-c->x)<0){--cx;continue;}
            if((c->x-x)*(d->y-c->y)-(c->y-y)*(d->x-c->x)>0){++cy;continue;}}
        return plane(*a,*d,*c);
    }return 0;
}
struct Surface {
    std::array<float,3> diffuse={{.3f,.3f,.3f}};
    static Surface Read(const Blob& raw){Surface result;if(raw.empty())return result;Reader r(raw);r.Text(r.Get<uint16_t>());
        for(auto& c:result.diffuse){c=r.Real();Need(c>=0&&c<=1,"Material diffuse range");}
        Need(r.Real()==1,"Non-opaque decoration material");r.Skip(12*4);r.Real();Need(r.pos==raw.size(),"Material layout");return result;}
};
struct Part {Surface surface;std::vector<FigVertex> vertices;std::vector<uint16_t> indices;std::shared_ptr<const Pixels> pixels;uint32_t texture=0;bool translucent=false;std::string textureName;};
struct Instance {size_t neighbor=0;uint32_t id=0;V3 low,high;std::vector<Part> parts;};
struct Model {Blob mod,bon;std::map<std::string,std::string> hierarchy;};
inline bool Visible(const Instance& o,Point offset,const SectorView& view){
    const double minx=o.low.x+offset.x,maxx=o.high.x+offset.x,miny=o.low.y+offset.y,maxy=o.high.y+offset.y;
        bool visible=true;
        if(view.useFrustum){bool outside[6]={true,true,true,true,true,true};
            for(unsigned c=0;c<8;++c){const double p[4]={c&1?maxx:minx,c&2?maxy:miny,c&4?o.high.z:o.low.z,1};double q[4]={};
                for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)q[row]+=view.worldToClip[row*4+col]*p[col];
                const double plane[6]={q[0]+q[3],q[3]-q[0],q[1]+q[3],q[3]-q[1],q[2],q[3]-q[2]};
                for(unsigned n=0;n<6;++n)outside[n]=outside[n]&&plane[n]<0;}
            for(bool p:outside)visible=visible&&!p;
        }
    return visible;
}
constexpr uint64_t ModelCacheBudget=128ull*1024*1024;
constexpr uint64_t GeometryBudget=128ull*1024*1024;
constexpr uint64_t TextureCacheBudget=128ull*1024*1024;
class Scene {
    std::vector<Instance> objects;
    std::map<std::string,Model> models;uint64_t rawBytes=0;
    std::string modelProfile;std::vector<std::wstring> modelArchives;
    std::map<std::string,uint32_t> textureKeys;
    std::map<std::string,std::weak_ptr<const Pixels>> pixelCache;
public:
    uint32_t modelLoads=0,modelHits=0,visibleCacheSkipped=0;
    std::vector<std::pair<size_t,uint32_t>> cacheSkipSample;
    uint64_t bytes=0,textureBytes=0;uint32_t skipped=0,budgetSkipped=0,reusedTextures=0,decodedTextures=0;std::vector<std::string> diagnostics;
    void Prepare(const Snapshot& data,const ProfileFiles& files,const std::function<bool()>& cancelled,const SectorView* view=nullptr,uint64_t textureBudget=TextureCacheBudget){
        modelLoads=modelHits=visibleCacheSkipped=0;cacheSkipSample.clear();
        if(modelProfile!=files.profile||modelArchives!=files.figureArchives){models.clear();rawBytes=0;modelProfile=files.profile;modelArchives=files.figureArchives;}
        objects.clear();bytes=textureBytes=0;skipped=budgetSkipped=reusedTextures=decodedTextures=0;diagnostics.clear();
        Need(textureBudget<=TextureCacheBudget,"Decoration texture limit");
        for(auto it=pixelCache.begin();it!=pixelCache.end();)if(it->second.expired())it=pixelCache.erase(it);else ++it;
        std::map<std::string,std::shared_ptr<const Pixels>> textures;
        std::map<uint32_t,Surface> materials;std::map<std::string,bool> translucent;
        auto check=[&]{if(cancelled&&cancelled())throw std::runtime_error("Decoration preparation cancelled");};
        for(size_t ni=0;ni<data.neighbors.size();++ni){const auto& neighbor=data.neighbors[ni];const auto& entry=neighbor.placement.entry;
            check();std::vector<Object> placements;
            try{Need(!entry.mob.empty(),"Neighbor catalog MOB absent");placements=ReadMob((files.layoutRoot.empty()?files.Mob(entry.identity.profile,entry.mob):files.LayoutMap(entry.identity.profile,entry.mob,true)),diagnostics);}
            catch(const std::exception& e){diagnostics.push_back(entry.identity.mpr+": "+e.what());continue;}
            for(const auto& o:placements){check();try{
                auto model=models.find(o.model);if(model==models.end()){
                    Model m;m.mod=files.Figure(entry.identity.profile,o.model+".mod");m.bon=files.Figure(entry.identity.profile,o.model+".bon");
                    Need(m.mod.size()+m.bon.size()<=64u*1024u*1024u,"Decoration model cache budget");
                    if(rawBytes+m.mod.size()+m.bon.size()>ModelCacheBudget){models.clear();rawBytes=0;}Blob hierarchy;
                    Need(ReadMember(m.mod,o.model,hierarchy),"Model hierarchy missing");m.hierarchy=ReadHierarchy(hierarchy);
                    rawBytes+=m.mod.size()+m.bon.size();model=models.emplace(o.model,std::move(m)).first;++modelLoads;
                }
                else ++modelHits;
                const auto& m=model->second;Instance object;object.neighbor=ni;object.id=o.id;
                object.low={1e30f,1e30f,1e30f};object.high={-1e30f,-1e30f,-1e30f};
                std::set<std::string> included(o.parts.begin(),o.parts.end());
                for(const auto& name:included)Need(m.hierarchy.count(name)!=0,"Requested figure part absent");
                V3 position=o.position;position.z+=Height(neighbor.terrain,position.x,position.y);uint64_t objectBytes=0;
                for(const auto& node:m.hierarchy){check();if(!included.empty()&&!included.count(node.first))continue;
                    Blob fig;Need(ReadMember(m.mod,node.first,fig),"Figure part missing");auto figure=ReadFigure(fig,o.morph);
                    // Keep the hierarchy/BON node for descendants, but publish no empty mesh.
                    if(figure.vertices.empty()&&figure.indices.empty())continue;
                    // Solid decoration materials; translucent/special shaders need a separate visual contract.
                    Need((figure.material>=17&&figure.material<=22)||figure.material==25,"Unsupported decoration material");
                    auto surface=materials.find(figure.material);if(surface==materials.end()){
                        const char* names[]={"leather.mat","wood.mat","metal.mat","cloth.mat","skin.mat","stone.mat"};
                        surface=materials.emplace(figure.material,Surface::Read(files.MaterialFile(entry.identity.profile,figure.material==25?"metal.mat":names[figure.material-17]))).first;
                    }
                    const auto& texture=(figure.texture==2||figure.texture==5)?o.secondary:o.primary;
                    Need(figure.texture>=1&&figure.texture<=8,"Unsupported decoration texture role");
                    V3 offset;std::string name=node.first;std::set<std::string> visited;
                    while(!name.empty()){Need(visited.insert(name).second&&visited.size()<=256,"Cyclic decoration hierarchy");const auto parent=m.hierarchy.find(name);Need(parent!=m.hierarchy.end(),"Missing parent figure part");
                        Blob bone;Need(ReadMember(m.bon,name,bone),"Missing bone offset");offset=Add(offset,ReadBone(bone,o.morph));name=parent->second;}
                    Part part;part.surface=surface->second;part.textureName=texture;part.indices=std::move(figure.indices);part.vertices.reserve(figure.vertices.size());
                    for(auto v:figure.vertices){v.position=Add(Rotate(Add(v.position,offset),o.rotation),position);v.normal=Rotate(v.normal,o.rotation);
                        for(float value:{v.position.x,v.position.y,v.position.z})Need(std::isfinite(value)&&std::abs(value)<100000,"Invalid transformed decoration");
                        object.low.x=(std::min)(object.low.x,v.position.x);object.low.y=(std::min)(object.low.y,v.position.y);object.low.z=(std::min)(object.low.z,v.position.z);
                        object.high.x=(std::max)(object.high.x,v.position.x);object.high.y=(std::max)(object.high.y,v.position.y);object.high.z=(std::max)(object.high.z,v.position.z);part.vertices.push_back(v);}
                    objectBytes+=uint64_t(part.vertices.size())*sizeof(FigVertex)+uint64_t(part.indices.size())*2;Need(objectBytes<=16u*1024u*1024u,"Decoration instance geometry budget");object.parts.push_back(std::move(part));
                }
                if(view){const auto delta=neighbor.placement.offset;
                    const double dx=(std::max)(0.0,(std::max)(object.low.x+delta.x-view->camera.x,view->camera.x-object.high.x-delta.x));
                    const double dy=(std::max)(0.0,(std::max)(object.low.y+delta.y-view->camera.y,view->camera.y-object.high.y-delta.y));
                    if(std::hypot(dx,dy)>view->radius+view->prefetch+64){
                        if(!view->anticipate)continue;
                        const double ax=(std::max)(0.0,(std::max)(object.low.x+delta.x-view->anticipated.x,view->anticipated.x-object.high.x-delta.x));
                        const double ay=(std::max)(0.0,(std::max)(object.low.y+delta.y-view->anticipated.y,view->anticipated.y-object.high.y-delta.y));
                        if(std::hypot(ax,ay)>view->radius+view->prefetch+64)continue;
                    }
                }
                Need(bytes+objectBytes<=GeometryBudget,"Decoration geometry budget");
                if(!object.parts.empty()){bytes+=objectBytes;objects.push_back(std::move(object));}
            }catch(const std::exception& e){++skipped;if(diagnostics.size()<100)diagnostics.push_back(entry.identity.mpr+"/"+o.model+": "+e.what());}}
        }
        // Exact bounds are available before allocating pixels. Distance, not MOB order,
        // decides which objects own the bounded texture cache, including across maps.
        auto distance=[&](const Instance& object){if(!view)return 0.0;const auto delta=data.neighbors[object.neighbor].placement.offset;
            const double dx=(std::max)(0.0,(std::max)(object.low.x+delta.x-view->camera.x,view->camera.x-object.high.x-delta.x));
            const double dy=(std::max)(0.0,(std::max)(object.low.y+delta.y-view->camera.y,view->camera.y-object.high.y-delta.y));return std::hypot(dx,dy);};
        std::stable_sort(objects.begin(),objects.end(),[&](const Instance& a,const Instance& b){const bool av=!view||Visible(a,data.neighbors[a.neighbor].placement.offset,*view),bv=!view||Visible(b,data.neighbors[b.neighbor].placement.offset,*view);if(av!=bv)return av>bv;return distance(a)<distance(b);});
        auto candidates=std::move(objects);objects.clear();bytes=0;std::set<std::string> failedTextures;
        for(auto& object:candidates){check();try{
            const auto& profile=data.neighbors[object.neighbor].placement.entry.identity.profile;
            std::map<std::string,std::shared_ptr<const Pixels>> staged;uint64_t additional=0;bool fits=true;
            for(const auto& part:object.parts){check();const auto& name=part.textureName;if(textures.count(name)||staged.count(name))continue;
                Need(!failedTextures.count(name),"Previously failed decoration texture");std::shared_ptr<const Pixels> decoded;
                const auto cached=pixelCache.find(name);if(cached!=pixelCache.end())decoded=cached->second.lock();
                if(decoded)++reusedTextures;
                else try{decoded=std::make_shared<const Pixels>(DecodePixels(files.Texture(profile,name+".mmp"),size_t(textureBudget)));++decodedTextures;}
                    catch(...){failedTextures.insert(name);throw;}
                const uint64_t cost=uint64_t(decoded->argb.size())*4;
                if(cost>textureBudget-textureBytes-additional){fits=false;break;}
                additional+=cost;staged.emplace(name,std::move(decoded));
            }
            if(!fits){++budgetSkipped;if(!view||Visible(object,data.neighbors[object.neighbor].placement.offset,*view)){++visibleCacheSkipped;if(cacheSkipSample.size()<8)cacheSkipSample.push_back({object.neighbor,object.id});}continue;} // no partial-object texture charge
            for(const auto& entry:staged){textures.emplace(entry);pixelCache[entry.first]=entry.second;
                translucent[entry.first]=std::any_of(entry.second->argb.begin(),entry.second->argb.end(),[](uint32_t c){const auto a=c>>24;return a!=0&&a!=255;});}
            textureBytes+=additional;
            for(auto& part:object.parts){const auto& name=part.textureName;
                if(!textureKeys.count(name))textureKeys[name]=uint32_t(textureKeys.size());
                part.texture=textureKeys.at(name);part.pixels=textures.at(name);part.translucent=translucent.at(name);
                bytes+=uint64_t(part.vertices.size())*sizeof(FigVertex)+uint64_t(part.indices.size())*2;
            }
            objects.push_back(std::move(object));
        }catch(const std::exception& e){++skipped;if(diagnostics.size()<100)diagnostics.push_back(e.what());}}
        // Admission above remains visible/near first. Freeze publication order only
        // after admission, so rebuilding the area cannot permute unchanged geometry.
        std::stable_sort(objects.begin(),objects.end(),[](const Instance& a,const Instance& b){
            return a.neighbor<b.neighbor||(a.neighbor==b.neighbor&&a.id<b.id);
        });
        bytes+=textureBytes;
    }
    const std::vector<Instance>& Objects()const{return objects;}
};

using ObjectKey=std::pair<size_t,uint32_t>; // neighbor identity in this snapshot + MOB ID
using SpatialKey=std::tuple<size_t,uint32_t,int,int>;
inline SpatialKey GroupKey(const Instance& o,uint32_t texture,uint32_t cell){
    return std::make_tuple(o.neighbor,texture,cell?int(std::floor((double(o.low.x)+o.high.x)*.5/cell)):0,cell?int(std::floor((double(o.low.y)+o.high.y)*.5/cell)):0);
}
inline uint32_t GroupPart(const SpatialKey& key,uint32_t cell,uint32_t part){
    Need(part<256,"Decoration batch part range");if(!cell)return part;
    const int x=std::get<2>(key),y=std::get<3>(key);Need(x>=-1024&&x<1024&&y>=-1024&&y<1024,"Decoration spatial range");
    const uint32_t mode=cell==128?1:cell==256?2:3;
    return(mode<<30)|(uint32_t(x+1024)<<19)|(uint32_t(y+1024)<<8)|part;
}
struct Selection {uint32_t cellSize=0;std::vector<size_t> objects;uint64_t bytes=0;uint32_t meshes=0,skipped=0;
    uint32_t visible=0,prefetch=0,visibleSkipped=0,prefetchSkipped=0,rangeSkipped=0;
    std::set<ObjectKey> selectedKeys;std::vector<ObjectKey> visibleSkipSample;
};
inline Selection SelectObjects(const Scene& scene,const Snapshot& data,const SectorView& view,uint64_t budget,uint32_t meshLimit,uint32_t partitionLimit=128){
    struct Candidate {size_t index;double distance;bool visible,future;};std::vector<Candidate> candidates;Selection result;
    for(size_t i=0;i<scene.Objects().size();++i){const auto& o=scene.Objects()[i];const auto offset=data.neighbors[o.neighbor].placement.offset;
        const double minx=o.low.x+offset.x,maxx=o.high.x+offset.x,miny=o.low.y+offset.y,maxy=o.high.y+offset.y;
        const double dx=(std::max)(0.0,(std::max)(minx-view.camera.x,view.camera.x-maxx)),dy=(std::max)(0.0,(std::max)(miny-view.camera.y,view.camera.y-maxy));
        const double distance=std::hypot(dx,dy);const bool future=distance>view.radius+view.prefetch;
        if(future){
            const double ax=(std::max)(0.0,(std::max)(minx-view.anticipated.x,view.anticipated.x-maxx)),ay=(std::max)(0.0,(std::max)(miny-view.anticipated.y,view.anticipated.y-maxy));
            if(!view.anticipate||std::hypot(ax,ay)>view.radius+view.prefetch){++result.rangeSkipped;continue;}
        }
        const bool visible=Visible(o,offset,view);
        candidates.push_back({i,distance,visible,future});
    }
    // Keep nearby offscreen geometry resident across camera turns. Visibility
    // controls budget priority; GPU clipping still controls actual pixels.
    std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){if(a.future!=b.future)return a.future<b.future;if(a.visible!=b.visible)return a.visible>b.visible;return a.distance<b.distance||(a.distance==b.distance&&a.index<b.index);});
    using Key=std::pair<size_t,uint32_t>;std::map<Key,uint64_t> counts;
    for(const auto& c:candidates){auto proposed=counts;uint64_t extra=0;uint32_t meshes=0;const auto& o=scene.Objects()[c.index];
        for(const auto& p:o.parts){auto& n=proposed[{o.neighbor,p.texture}];const auto before=(n+65534)/65535;n+=p.indices.size();const auto after=(n+65534)/65535;
            extra+=uint64_t(p.indices.size())*sizeof(ET_Vertex)+(after-before)*p.pixels->argb.size()*4;meshes+=uint32_t(after-before);}
        if(extra>budget-result.bytes||meshes>meshLimit-result.meshes){++result.skipped;
            if(c.visible){++result.visibleSkipped;if(result.visibleSkipSample.size()<8)result.visibleSkipSample.push_back({o.neighbor,o.id});}
            else ++result.prefetchSkipped;continue;}
        result.bytes+=extra;result.meshes+=meshes;counts=std::move(proposed);result.objects.push_back(c.index);result.selectedKeys.insert({o.neighbor,o.id});
        if(c.visible)++result.visible;else ++result.prefetch;
    }
    // Camera-distance order must not churn mesh topology when membership is unchanged.
    std::sort(result.objects.begin(),result.objects.end());
    // Partition only when the exact SAME admitted objects still fit. The renderer
    // charges pixels per mesh, so fine cells cannot steal visible coverage.
    for(uint32_t cell:{128u,256u,512u}){
        std::map<SpatialKey,uint64_t> vertices;uint64_t total=0;uint32_t batches=0;
        for(size_t oi:result.objects){const auto& object=scene.Objects()[oi];for(const auto& part:object.parts){
            auto& n=vertices[GroupKey(object,part.texture,cell)];const auto before=(n+65534)/65535;n+=part.indices.size();const auto after=(n+65534)/65535;
            total+=uint64_t(part.indices.size())*sizeof(ET_Vertex)+(after-before)*part.pixels->argb.size()*4;batches+=uint32_t(after-before);
        }}
        if(total<=budget&&batches<=meshLimit&&batches<=(std::max)(result.meshes,partitionLimit)){result.cellSize=cell;result.bytes=total;result.meshes=batches;break;}
    }
    return result;
}
inline void Append(const Scene& scene,const Snapshot& data,const Selection& selected,Point origin,const VisualEnvironment& env,
    RenderSnapshot& output,const RenderSnapshot* previous,const std::function<bool()>& cancelled){
    struct Group {SpatialKey key;uint32_t part;};std::vector<Group> groups;std::map<SpatialKey,size_t> current;
    CpuLightingRounding rounding;
    for(size_t oi:selected.objects){if(cancelled&&cancelled())throw std::runtime_error("Decoration publication cancelled");const auto& o=scene.Objects().at(oi);
        for(const auto& p:o.parts){auto materialEnv=env;for(size_t channel=0;channel<3;++channel)materialEnv.light[channel]*=p.surface.diffuse[channel];const auto key=GroupKey(o,p.texture,selected.cellSize);size_t offset=0;
            while(offset<p.indices.size()){
                auto it=current.find(key);if(it==current.end()||output.decorationPatches[it->second].vertices.size()==65535){
                    VisualPatch patch;patch.pixels=p.pixels;patch.material.pass=p.translucent?ET_ALPHA:ET_OPAQUE;patch.material.flags=ET_MATERIAL_ALPHA_TEST;patch.material.alphaRef=0;
                    const auto part=it==current.end()?0:groups[it->second].part+1;const size_t index=output.decorationPatches.size();output.decorationPatches.push_back(std::move(patch));groups.push_back({key,part});current[key]=index;it=current.find(key);
                }
                auto& vertices=output.decorationPatches[it->second].vertices;const auto n=(std::min)(size_t(65535)-vertices.size(),p.indices.size()-offset);
                for(size_t j=0;j<n;++j){const auto& v=p.vertices[p.indices[offset+j]];uint32_t rgb=0xffffff;
                    if(env.valid&VisualLight){LightingInputs input;input.normal={{v.normal.x,v.normal.y,v.normal.z}};rgb=CpuPackRGB(CpuSunRGB(input,materialEnv,true));}
                    vertices.push_back({v.position.x,v.position.y,v.position.z,0xff000000u|rgb,0xff000000u,v.u,v.v});}
                offset+=n;
            }
        }
    }
    uint64_t added=0;
    for(size_t i=0;i<groups.size();++i){const auto& g=groups[i];const auto& patch=output.decorationPatches[i];const auto offset=data.neighbors[std::get<0>(g.key)].placement.offset;
        NeighborVisual v={};v.mesh.cbSize=sizeof(v.mesh);v.mesh.vertices=patch.vertices.data();v.mesh.vertexCount=uint32_t(patch.vertices.size());
        v.mesh.textureWidth=patch.pixels->width;v.mesh.textureHeight=patch.pixels->height;v.mesh.argb=patch.pixels->argb.data();v.mesh.material=patch.material;
        v.mesh.worldX=float(origin.x+offset.x);v.mesh.worldY=float(origin.y+offset.y);v.geometryRevision=output.view.contentRevision;v.textureRevision=output.layoutRevision;
        v.key={{uint32_t(std::get<0>(g.key)),0x80000000u,std::get<1>(g.key),GroupPart(g.key,selected.cellSize,g.part)}};
        if(previous&&previous->view.mapEpoch==output.view.mapEpoch)for(const auto& old:previous->meshes)if(old.key==v.key){
            if(old.mesh.vertexCount==v.mesh.vertexCount&&!std::memcmp(old.mesh.vertices,v.mesh.vertices,patch.vertices.size()*sizeof(ET_Vertex)))v.geometryRevision=old.geometryRevision;break;}
        added+=uint64_t(patch.vertices.size())*sizeof(ET_Vertex)+uint64_t(patch.pixels->argb.size())*4;output.meshes.push_back(v);
    }
    Need(added==selected.bytes&&groups.size()==selected.meshes,"Decoration residency cost mismatch");output.bytes+=added;output.view.count=uint32_t(output.meshes.size());output.view.meshes=output.meshes.data();
}
} }
#pragma pack(pop)
