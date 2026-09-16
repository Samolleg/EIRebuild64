#pragma once
#include "ModTerrainHorizon.h"
#include <vector>
#include <memory>
#include <future>
#include <chrono>
#include <cstdio>

// Per-map ownership. Workers see copied heights, never engine pointers.
class ModTerrainShadow {
    struct Slice {
        TerrainShadowPort::TerrainSelfShadowTopologyField field={};
        std::vector<TerrainShadowPort::TerrainSelfShadowTopologyNode> nodes;
        ~Slice(){if(field.surface_coverage)VirtualFree(field.surface_coverage,0,MEM_RELEASE);if(field.surface_valid)VirtualFree(field.surface_valid,0,MEM_RELEASE);if(field.surface_clearance)VirtualFree(field.surface_clearance,0,MEM_RELEASE);}
    };
    std::shared_ptr<const std::vector<float>> heights;
    std::future<std::shared_ptr<Slice>> pending;
    std::shared_ptr<Slice> previous,current;
    int mapWidth=0,mapHeight=0,key=-1,requested=-1,pendingKey=-1;
    int addX=0,addY=0;float riseX=0,riseY=0;
    DWORD published=0,lastUpload=0;
    bool day=false,lastDay=false;
public:
    std::vector<unsigned char> pixels;
    unsigned width=0,height=0;
    bool ready=false;
    void Initialize(int w,int h,std::vector<float> source) {
        mapWidth=w;mapHeight=h;heights=std::make_shared<const std::vector<float>>(std::move(source));
    }
    void Request(int x,int y,float rx,float ry,bool sunlight) {
        day=sunlight;if(!day)return;
        addX=x;addY=y;riseX=rx;riseY=ry;
        float a=atan2f(float(y),float(x));
        int azimuth=int(floorf((a+3.141592654f)*64/(2*3.141592654f)))&63;
        float horizontal=sqrtf(float(x)*x+float(y)*y);
        float vertical=abs(x)>=abs(y)?rx*x:ry*y;
        int elevation=int(atan2f(vertical,horizontal)*360/3.141592654f);
        requested=azimuth+64*elevation;
    }
    bool Tick(DWORD now) {
        bool changed=false;
        if(pending.valid() && pending.wait_for(std::chrono::seconds(0))==std::future_status::ready){
            auto result=pending.get();
            if(result && day && pendingKey==requested){
                previous=current;current=std::move(result);key=pendingKey;published=now;
                width=current->field.surface_width;height=current->field.surface_height;pixels.resize(size_t(width)*height);ready=true;changed=true;
                FILE *f=fopen("eirebuild-terrain-shadow.log","a");
                if(f){fprintf(f,"R7 field=%ux%u key=%d build_ms=%lu complete=1\n",width,height,key,current->field.horizon_build_ms);fclose(f);}
            }
        }
        if(heights && day && requested!=key && !pending.valid() && (!current || DWORD(now-published)>=1200)){
            auto source=heights;int w=mapWidth,h=mapHeight,x=addX,y=addY;float rx=riseX,ry=riseY;pendingKey=requested;
            pending=std::async(std::launch::async,[source,w,h,x,y,rx,ry](){
                auto result=std::make_shared<Slice>();result->nodes.resize(source->size());
                for(size_t i=0;i<source->size();i++){auto &n=result->nodes[i];memcpy(&n.z_bits,&(*source)[i],sizeof(float));n.valid=1;}
                auto &f=result->field;f.nodes=result->nodes.data();f.width=w;f.height=h;f.node_count=f.valid_nodes=DWORD(source->size());
                if(!TerrainShadowPort::Build(&f,x,y,rx,ry))return std::shared_ptr<Slice>();
                return result;
            });
        }
        if(!ready)return false;
        bool blending=bool(previous);
        if(!changed && day==lastDay && (!blending || DWORD(now-lastUpload)<100))return false;
        float amount=DWORD(now-published)/1200.0f;if(amount>1)amount=1;
        for(size_t i=0;i<pixels.size();i++){
            float distance=TerrainShadowPort::TerrainClearanceDecode(current->field.surface_clearance[i]);
            if(previous && previous->field.surface_width==int(width) && previous->field.surface_height==int(height)){
                float from=TerrainShadowPort::TerrainClearanceDecode(previous->field.surface_clearance[i]);
                float a=TerrainShadowPort::TerrainClearanceCoverage(from),b=TerrainShadowPort::TerrainClearanceCoverage(distance);
                pixels[i]=day && current->field.surface_valid[i]?static_cast<unsigned char>(a+(b-a)*amount+0.5f):0;
                continue;
            }
            pixels[i]=day && current->field.surface_valid[i]?TerrainShadowPort::TerrainClearanceCoverage(distance):0;
        }
        if(amount>=1)previous.reset();lastUpload=now;lastDay=day;return true;
    }
};
