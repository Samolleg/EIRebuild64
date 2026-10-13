#pragma once

#include "../OwnedRender/E1Terrain.h"
#include "../Unit.h"
#include "../UnitClientSpecific.h"
#include "../Terrain.h"
#include "../TextureManager.h"
#include "../Light.h"
#include "../PlayerClient.h"
#include <set>
#include <cfloat>
#include <cmath>

// Borrowed engine inputs end at the API call. No UObject pointer or callback
// escapes into the DLL; only copies of actual resources/rows are retained.
namespace OwnedWorldInputs {
inline void Check(HRESULT hr){if(FAILED(hr))throw hr;}
inline HRESULT __cdecl ReadMap(void* context,const char* name,void* destination,uint32_t capacity,uint32_t* size) noexcept {
    try{return static_cast<CTerrain*>(context)->CopyRendererMember(name,destination,capacity,size);}
    catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(...){return E_FAIL;}
}
inline HRESULT __cdecl ReadTexture(void* context,const char* name,void* destination,uint32_t capacity,uint32_t* size) noexcept {
    if(!context||!size||!name)return E_INVALIDARG;*size=0;
    try{
        auto* owner=static_cast<CTextureManager*>(context);DWORD bytes=0;
        auto* data=owner->GetFile(name,&bytes);if(!data)return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        *size=bytes;const HRESULT hr=!destination?S_OK:capacity<bytes?HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER):S_OK;
        if(destination&&SUCCEEDED(hr))memcpy(destination,data,bytes);owner->CloseFile(data);return hr;
    }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(...){return E_FAIL;}
}
inline std::wstring Wide(const std::string& text){
    const int n=MultiByteToWideChar(CP_ACP,0,text.data(),int(text.size()),nullptr,0);
    if(n<=0)throw E_INVALIDARG;std::wstring out(size_t(n),L'\0');
    MultiByteToWideChar(CP_ACP,0,text.data(),int(text.size()),&out[0],n);return out;
}
inline void Terrain(OwnedRendererClient& renderer,CTerrain& terrain,CTextureManager* textures,uint64_t generation,
                    const CModPackage* package,const CString& workRoot,const CString& resourceRoot){
    std::vector<std::wstring> roots,archives,bases;
    // Completed override groups precede base resources (ResourceManager policy).
    if(package){
        roots.push_back(Wide(package->Directory())+L"/textures");
        roots.push_back(Wide(package->Directory())+L"/figures");
    }
    roots.push_back(Wide((LPCSTR)workRoot)+L"/textures");roots.push_back(Wide((LPCSTR)workRoot)+L"/figures");
    for(const char* category:{"figures.res","menus.res","textures.res","redress.res","materials.res"}){
        std::vector<std::string> paths;bool ignore=false;if(package)package->ResourcePlan(category,paths,ignore);
        for(const auto& path:paths)archives.push_back(Wide(path));
        if(!ignore)bases.push_back(Wide((LPCSTR)resourceRoot)+Wide(category));
    }
    archives.insert(archives.end(),bases.begin(),bases.end());
    std::vector<const wchar_t*> rootPointers,archivePointers;
    for(const auto& path:roots)rootPointers.push_back(path.c_str());for(const auto& path:archives)archivePointers.push_back(path.c_str());
    std::vector<float> shifts;for(int i=0;i<terrain.GetObjectsNumber();++i)shifts.push_back(terrain.GetObject(i)->fGlobalShift);
    const std::string base=(LPCSTR)terrain.GetBaseName(),header=base+".mp",project=base+".mpr";
    EIRenderTerrainSourceV5 source={};source.source_revision=terrain.GetRendererRevision();
    source.read_base_vertices=[](void* owner,uint32_t sector,float* rows,uint32_t count)->HRESULT {
        return static_cast<CTerrain*>(owner)->CopyRendererBaseVertices(sector,rows,count);
    };source.vertex_context=&terrain;
    auto& input=source.base;input.size=sizeof(source);input.world_generation=generation;
    input.map_header=header.c_str();input.map_resource=project.c_str();input.read_member=ReadMap;input.read_context=&terrain;
    input.read_texture=ReadTexture;input.texture_context=textures;
    input.loose_roots=rootPointers.data();input.loose_count=uint32_t(rootPointers.size());
    input.archives=archivePointers.data();input.archive_count=uint32_t(archivePointers.size());
    input.layer_shifts=shifts.data();input.layer_count=uint32_t(shifts.size());Check(renderer.SetTerrain(source));
    std::string failure;
    if (!OwnedRender::E1PublishTerrainSource(&terrain, generation, source.source_revision,
        project.c_str(), header.c_str(), ReadMap, &terrain, failure)) {
        GameJournal::Event("RENDER", "terrain_publication_failed", "%s", failure.c_str());
        throw E_FAIL;
    }
}
inline bool Bounds(const CMapObject& object,float* out){
    const auto& volume=object.GetBoundingVolume();if(!volume.IsBox())return false;
    // Cached placement is prepared by visibility/draw traversal, which can
    // skip undetected units. Build the world transform from current state.
    SHMatrix matrix={};object.GetOrientation().DecompEulerMatrix(matrix);
    const auto& position=object.GetCP();
    matrix._14=position.x;matrix._24=position.y;matrix._34=position.z;matrix._44=1.f;
    float bounds[6];
    for(int i=0;i<3;++i){bounds[i]=FLT_MAX;bounds[i+3]=-FLT_MAX;}
    for(unsigned i=0;i<8;++i){
        const CPlot3D local=volume.ptCenter+CPlot3D(i&1?volume.ptMax.x:volume.ptMin.x,
            i&2?volume.ptMax.y:volume.ptMin.y,i&4?volume.ptMax.z:volume.ptMin.z);
        const auto p=matrix.RotateHVector(local);const float v[]={p.x,p.y,p.z};
        for(int j=0;j<3;++j){
            if(!std::isfinite(v[j]))return false;
            bounds[j]=(std::min)(bounds[j],v[j]);bounds[j+3]=(std::max)(bounds[j+3],v[j]);
        }
    }
    std::copy_n(bounds,6,out);return true;
}
struct Scene {
    std::vector<EIRenderObjectV3> objects;
    std::vector<EIRenderUnitV3> units;
    std::vector<EIRenderPointLightV3> lights;
    std::vector<owned::RetainedWorldPointLight> c56Lights;
    std::set<int> ids;
    void Add(CMapObject* object,bool drawUndetected,CPlayerClient* player,float night){
        if(!object||object->CheckFlag(OBJ_HIDDEN)||!ids.insert(object->GetID()).second)return;
        const auto& p=object->GetCP();
        if(object->WhoAmI()!=HPC_UNIT){
            EIRenderObjectV3 row={};row.id=object->GetID();row.family=object->WhoAmI();row.visual=object->GetMaskName();
            row.position[0]=p.x;row.position[1]=p.y;row.position[2]=p.z;
            row.bounds_valid=object->CheckFlag(OBJ_VISIBLE)&&Bounds(*object,row.bounds);objects.push_back(row);return;
        }
        auto* unit=static_cast<CUnit*>(object);const auto* stats=unit->GetUserUnitStats();
        EIRenderUnitV3 row={};row.id=unit->GetID();row.visual=unit->GetMaskName();
        row.position[0]=p.x;row.position[1]=p.y;row.position[2]=p.z;row.pose=unit->GetCurrentPose();
        row.locomotion=stats->cLocomotion;row.height=unit->GetUnitHeight();row.radius=unit->GetUnitRadius();
        row.fade=1;if(auto* specific=dynamic_cast<CUnitClientSpecific*>(unit->GetUnitSpecific()))row.fade=specific->GetFadeOutAlpha();
        row.visibility=stats->cPercepts[RPGS_SIGHT];row.loudness=stats->cPercepts[RPGS_HEARING];
        row.vision=stats->GetSightRange(night);if(const auto* race=unit->GetRaceModel())row.blood=race->dwBloodType;
        row.flags=(unit->IsDead()?1:0)|(unit->IsMoving()?2:0)|(player&&player->IsUnitUnderControl(unit)?4:0)|
          ((drawUndetected||unit->CheckFlag(UNIT_VISIBLE))&&!unit->IsInvisible()?8:0);
        float box[6];if(Bounds(*unit,box)){
            const float radius=.5f*(std::max)(box[3]-box[0],box[4]-box[1]);
            if(std::isfinite(radius)){row.flags|=16;row.lowest=box[2];row.radius=radius;}
        }
        units.push_back(row);
    }
    void Lights(CBaseManager* manager){
        if(!manager)return;CManagerIterator it;
        for(auto* value=it.GetFirst(manager);value;value=it.GetNext()){
            const auto* light=dynamic_cast<CLight*>(value);if(!light||light->CheckFlag(LIT_INACTIVE))continue;
            const auto& v=light->GetLight();if(v.dltType!=D3DLIGHT_POINT||!(v.dvRange>0))continue;
            EIRenderPointLightV3 row={{v.dvPosition.x,v.dvPosition.y,v.dvPosition.z},
                                     {v.dcvDiffuse.r,v.dcvDiffuse.g,v.dcvDiffuse.b},v.dvRange};lights.push_back(row);
            owned::RetainedWorldPointLight c56{};
            c56.source_id = light->GetID();
            c56.center = {v.dvPosition.x, v.dvPosition.y, v.dvPosition.z};
            c56.color = {v.dcvDiffuse.r, v.dcvDiffuse.g, v.dcvDiffuse.b};
            c56.radius = v.dvRange;
            c56.casts_shadow = !light->CheckFlag(LIT_UNSHADOW);
            c56.attached = light->CheckFlag(LIT_QUEST) != FALSE;
            c56Lights.push_back(c56);
        }
    }
    void Submit(OwnedRendererClient& renderer,uint64_t generation){
        EIRenderSceneV3 scene={};scene.size=sizeof(scene);scene.world_generation=generation;
        scene.objects=objects.data();scene.object_count=uint32_t(objects.size());scene.units=units.data();scene.unit_count=uint32_t(units.size());
        scene.lights=lights.data();scene.light_count=uint32_t(lights.size());Check(renderer.SetScene(scene));
    }
};
}
