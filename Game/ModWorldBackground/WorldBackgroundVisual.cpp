#include "WorldBackgroundVisual.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace WorldBackground {
namespace {
void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
}
TileUV NativeTileUV(uint16_t tile,uint32_t size){
    Need(size>=256&&size<=4096&&size%8==0,"Unsupported logical atlas UV dimensions");
    const float margin=8.0f/size,extent=0.125f;TileUV uv;
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col){
        unsigned r=row,c=col;
        for(unsigned rotation=0;rotation<uint32_t(tile>>14);++rotation){unsigned old=r;r=c;c=2-old;}
        const unsigned index=row*3+col;
        uv.u[index]=float(tile&7)*extent+(c==0?margin:c==1?extent*.5f:extent-margin);
        uv.v[index]=1.0f-(float((tile>>3)&7)*extent+(r==0?margin:r==1?extent*.5f:extent-margin));
    }
    return uv;
}
uint64_t VisualPatch::Bytes()const{return uint64_t(vertices.size())*sizeof(ET_Vertex)+(pixels?uint64_t(pixels->argb.size())*4:0);}
SectorVisual BuildSectorVisual(const Snapshot& input,SectorKey key,const VisualVertex& shade,uint64_t budget,PixelCache* cache){
    Need(key.neighbor<input.neighbors.size()&&bool(shade),"Missing sector or visual provider");
    const auto& p=input.neighbors[key.neighbor];const auto cost=MeasureSector(p,key.sector);
    Need(cost.bytes<=budget&&budget<=MaxPayloadBudget&&cost.meshes<=256,"Sector visual exceeds renderer limits");
    const auto& s=p.terrain.sectors[key.sector];SectorVisual result;result.key=key;
    std::map<std::pair<uint32_t,uint32_t>,size_t> patches;
    PixelCache localPixels;auto& pixels=cache?*cache:localPixels;
    // Full-resolution original LOD0 topology; no neighbor-edge stitching.
    static const unsigned triangles[]={0,1,4,0,4,3,1,2,5,1,5,4,3,4,7,3,7,6,4,5,8,4,8,7};
    for(unsigned layer=0;layer<(s.overlay.empty()?1u:2u);++layer)for(unsigned tile=0;tile<256;++tile){
        if(layer&&s.overlayMaterials[tile]==65535)continue;
        const uint16_t packed=layer?s.overlayTiles[tile]:s.landTiles[tile];
        const uint32_t atlas=(packed>>6)&255,tag=layer?1u+s.overlayMaterials[tile]:0;
        const Material* material=layer?&p.terrain.materials[s.overlayMaterials[tile]]:nullptr;
        // Type2 is untextured water; its shader cannot be replaced by a tile.
        Need(!material||material->type==1||material->type==3||material->type==4,"Unsupported visual material (untextured water or unknown)");
        const auto id=std::make_pair(tag,atlas);auto found=patches.find(id);size_t patchIndex;
        if(found==patches.end()){
            auto decoded=pixels.find(atlas);
            if(decoded==pixels.end())decoded=pixels.emplace(atlas,std::make_shared<const Pixels>(DecodePixels(p.textures[atlas].mmp,size_t(budget)))).first;
            VisualPatch patch;patch.atlas=atlas;patch.materialTag=tag;patch.pixels=decoded->second;
            patch.material.pass=layer?ET_ALPHA:ET_OPAQUE;
            patch.material.flags=ET_MATERIAL_ALPHA_TEST;patch.material.alphaRef=0;
            patchIndex=result.patches.size();result.patches.push_back(std::move(patch));patches.emplace(id,patchIndex);
        }else patchIndex=found->second;
        auto& output=result.patches[patchIndex].vertices;const auto uv=NativeTileUV(packed,p.terrain.textureSize);
        ET_Vertex local[9];
        for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col){
            const size_t index=(tile/16*2+row)*33+tile%16*2+col;const auto& v=layer?s.overlay[index]:s.land[index];
            Point active;active.x=double(v.x)+p.placement.offset.x;active.y=double(v.y)+p.placement.offset.y;
            auto value=shade(v,active,material,layer?s.overlayAlpha[index]:uint8_t(255),uv.u[row*3+col],uv.v[row*3+col]);
            Need(std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z)&&std::isfinite(value.u)&&std::isfinite(value.v),"Nonfinite prepared visual vertex");
            local[row*3+col]=value;
        }
        for(unsigned i:triangles){output.push_back(local[i]);
            result.patches[patchIndex].lightRefs.push_back({s.x,s.y,(tile/16*2+i/3)*33+tile%16*2+i%3,layer});}
    }
    for(const auto& patch:result.patches){Need(patch.vertices.size()<=65535,"Too many visual vertices");result.bytes+=patch.Bytes();}
    Need(result.bytes==cost.bytes&&result.patches.size()==cost.meshes,"Visual producer/residency cost mismatch");
    return result;
}
std::shared_ptr<RenderSnapshot> PrepareRenderSnapshot(const Snapshot& data,const SectorSelection& plan,
    Point origin,const VisualVertex& shade,const std::function<bool()>& cancelled,uint64_t budget,const RenderSnapshot* previous,uint64_t visualRevision){
    Need(budget>0&&budget<=MaxPayloadBudget,"Invalid negotiated payload budget");
    Need(plan.epoch>0&&plan.epoch<=0xffffffffu,"Invalid renderer epoch");
    Need(std::isfinite(float(origin.x))&&std::isfinite(float(origin.y)),"Invalid saved world origin");
    auto result=std::make_shared<RenderSnapshot>();result->view.mapEpoch=uint32_t(plan.epoch);
    result->source=&data;result->layoutRevision=plan.layoutRevision;
    result->view.contentRevision=visualRevision;
    result->view.topologyRevision=plan.request;result->view.activeWorldX=float(origin.x);result->view.activeWorldY=float(origin.y);
    BatchCost cost;std::set<SectorKey> seen;
    std::map<size_t,PixelCache> pixels;
    if(previous&&(previous->view.mapEpoch!=plan.epoch||previous->source!=&data||previous->layoutRevision!=plan.layoutRevision))previous=nullptr;
    // Caller must keep the same immutable Prepared data for this cache domain.
    if(previous)for(const auto& batch:previous->batches)pixels[batch.neighbor][batch.patch.atlas]=batch.patch.pixels;
    std::map<std::pair<size_t,uint32_t>,size_t> landBatch;
    for(const auto& key:plan.sectors){
        if(cancelled&&cancelled())throw std::runtime_error("Visual preparation cancelled");
        Need(seen.insert(key).second,"Duplicate planned sector");
        SectorVisual sector;BatchCost proposed=cost;
        try{
            Need(key.neighbor<data.neighbors.size(),"Invalid neighbor");
            proposed.Add(key.neighbor,MeasureSector(data.neighbors[key.neighbor],key.sector));
            Need(proposed.bytes<=budget&&proposed.meshes<=256,"Planned snapshot exceeds renderer limits");
            sector=BuildSectorVisual(data,key,shade,budget,&pixels[key.neighbor]);
        }catch(const std::exception& e){pixels.erase(key.neighbor);result->diagnostics.push_back(std::to_string(key.neighbor)+"/"+std::to_string(key.sector)+": "+e.what());continue;}
        cost=std::move(proposed);
        for(auto& patch:sector.patches){
            // Preserve alpha patch bounds/order; only opaque land is batched.
            if(patch.materialTag!=0){MeshBatch batch;batch.neighbor=key.neighbor;batch.part=uint32_t(key.sector);batch.patch=std::move(patch);result->batches.push_back(std::move(batch));continue;}
            const auto id=std::make_pair(key.neighbor,patch.atlas);size_t offset=0;
            while(offset<patch.vertices.size()){
                auto found=landBatch.find(id);
                if(found==landBatch.end()||result->batches[found->second].patch.vertices.size()==BatchVertexLimit){
                    MeshBatch batch;batch.neighbor=key.neighbor;batch.patch.atlas=patch.atlas;
                    batch.part=found==landBatch.end()?0:result->batches[found->second].part+1;
                    batch.patch.material=patch.material;batch.patch.pixels=patch.pixels;
                    const size_t index=result->batches.size();result->batches.push_back(std::move(batch));
                    landBatch[id]=index;found=landBatch.find(id);
                }
                auto& target=result->batches[found->second].patch.vertices;
                const size_t count=(std::min)(size_t(BatchVertexLimit)-target.size(),patch.vertices.size()-offset);
                target.insert(target.end(),patch.vertices.begin()+offset,patch.vertices.begin()+offset+count);
                auto& refs=result->batches[found->second].patch.lightRefs;
                refs.insert(refs.end(),patch.lightRefs.begin()+offset,patch.lightRefs.begin()+offset+count);offset+=count;
            }
        }
        // Keep sector identity for diagnostics, not duplicate vertex/pixel ownership.
        sector.patches.clear();sector.bytes=0;result->sectors.push_back(std::move(sector));
    }
    for(const auto& batch:result->batches){const auto& p=data.neighbors[batch.neighbor];
        const float x=float(origin.x+p.placement.offset.x),y=float(origin.y+p.placement.offset.y);
        Need(std::isfinite(x)&&std::isfinite(y),"Invalid saved neighbor origin");
        const auto& patch=batch.patch;result->bytes+=patch.Bytes();
        {NeighborVisual visual={};visual.mesh.cbSize=sizeof(visual.mesh);
            visual.mesh.vertexCount=uint32_t(patch.vertices.size());visual.mesh.vertices=patch.vertices.data();
            visual.mesh.textureWidth=patch.pixels->width;visual.mesh.textureHeight=patch.pixels->height;visual.mesh.argb=patch.pixels->argb.data();visual.mesh.material=patch.material;
            visual.mesh.worldX=x;visual.mesh.worldY=y;visual.geometryRevision=visualRevision?visualRevision:plan.request;visual.textureRevision=plan.layoutRevision;
            visual.key={{uint32_t(batch.neighbor),patch.materialTag,patch.atlas,batch.part}};
            visual.lightMapKey=uint64_t(batch.neighbor)+1;visual.lightRefs=patch.lightRefs.data();
            if(previous)for(const auto& old:previous->meshes)if(old.key==visual.key){
                if(old.mesh.vertexCount==visual.mesh.vertexCount&&std::memcmp(old.mesh.vertices,visual.mesh.vertices,patch.vertices.size()*sizeof(ET_Vertex))==0)
                    visual.geometryRevision=old.geometryRevision;
                break;
            }
            result->meshes.push_back(visual);
        }
    }
    Need(result->bytes==cost.bytes&&result->meshes.size()==cost.meshes,"Batched visual/residency cost mismatch");
    result->view.count=uint32_t(result->meshes.size());result->view.meshes=result->meshes.data();return result;
}
}
