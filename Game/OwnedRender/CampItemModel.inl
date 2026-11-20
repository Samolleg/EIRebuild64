#include "gltf_asset.h"
#include "skn_material_view.h"
#include "retained_static_submission.h"
#include "ModelPreviewLighting.h"
#include "../WideScreen.h"
#include "small_json.h"
#include "CampClothingFit.h"
#include "CampPreviewCache.h"
#include "CampPreviewStorage.h"
#include "CampSleevePose.h"

namespace {
struct CampItemMesh {
    unsigned material;
    std::uint64_t fingerprint=0;
    std::vector<owned::NativePackedGroup> positions, normals;
    std::vector<owned::NativeTextureCoordinate> uv;
    std::vector<owned::NativeVertexComponent> components;
    std::vector<std::uint16_t> indices;

    void append(const owned::skn::Vertex& v) {
        const auto i=static_cast<std::uint16_t>(components.size());
        const unsigned lane=i%4;
        if (!lane) { positions.emplace_back(); normals.emplace_back(); }
        auto& p=positions.back();
        p.x[lane]=v.position[0]; p.y[lane]=v.position[1]; p.z[lane]=v.position[2]; p.w[lane]=1;
        auto& n=normals.back();
        n.x[lane]=v.normal[0]/32767.f; n.y[lane]=v.normal[1]/32767.f; n.z[lane]=v.normal[2]/32767.f;
        uv.push_back({owned::skn::half_float(v.uv[0]),owned::skn::half_float(v.uv[1])});
        components.push_back({i,i,i});
    }

    CampPreviewStorage::View backing;
    owned::NativeGeometryView savedStreams{};
    void seal(CampPreviewStorage& storage) {
        const CampPreviewStorage::Span spans[]={
            {positions.data(),positions.size()*sizeof(positions[0])},
            {normals.data(),normals.size()*sizeof(normals[0])},
            {uv.data(),uv.size()*sizeof(uv[0])},
            {components.data(),components.size()*sizeof(components[0])},
            {indices.data(),indices.size()*sizeof(indices[0])}
        };
        owned::NativeGeometryView saved{};
        streams(saved);
        backing=storage.back(spans,5);
        if(!backing) return;
        const auto* cursor=backing.get();
        const auto moveStream=[&](auto& values,auto& target,const CampPreviewStorage::Span& span) {
            using Value=typename std::decay_t<decltype(values)>::value_type;
            target=reinterpret_cast<const Value*>(cursor);
            cursor+=span.bytes;
            std::decay_t<decltype(values)>{}.swap(values);
        };
        moveStream(positions,saved.positions,spans[0]);moveStream(normals,saved.normals,spans[1]);
        moveStream(uv,saved.texture_coordinates,spans[2]);moveStream(components,saved.components,spans[3]);
        moveStream(indices,saved.indices,spans[4]);savedStreams=saved;
    }
    void streams(owned::NativeGeometryView& view) const {
        if(backing) {view=savedStreams;return;}
        view.positions=positions.data(); view.position_groups=static_cast<UINT>(positions.size());
        view.normals=normals.data(); view.normal_groups=static_cast<UINT>(normals.size());
        view.texture_coordinates=uv.data(); view.texture_coordinate_count=static_cast<UINT>(uv.size());
        view.components=components.data(); view.component_count=static_cast<UINT>(components.size());
        view.indices=indices.data(); view.index_count=static_cast<UINT>(indices.size());
    }
};

struct CampItemSurface {
    owned::skn::Material material;
    std::array<owned::skn::Dds,4> maps;
    std::array<std::shared_ptr<const CampPreviewStorage::Image>,4> backing;
};
struct CampItemPart {
    std::string resource;
    std::vector<CampItemSurface> surfaces;
    std::vector<CampItemMesh> meshes;
};

struct CampFigureReference {
    CampItemFit::Cloud cloud;
    owned::FigureBounds bounds;
    CampItemFit::ClothingLayout clothing;
    bool valid=false;
};

struct CampItemModel {
    std::vector<CampItemPart> parts;
    std::array<float,3> center{};
    float radius=0;
    CampItemFit::Placement placement;
    std::array<float,2> cardMax{1,1},cardCenter{};
    std::size_t bytes=sizeof(CampItemModel);
    std::string name;
    mutable bool reported=false;
    bool clothing=false;

    CampItemModel(std::vector<owned::gltf::EquipmentAsset> bindings, const std::string& slot,bool blueprint,const CampFigureReference& reference,const CampItemFit::AuthoredClothingPose* authored,CampPreviewStorage& storage):clothing(slot.rfind("armor_",0)==0) {
        std::vector<CampItemFit::Triangle> triangles;
        std::array<float,3> low{FLT_MAX,FLT_MAX,FLT_MAX},high{-FLT_MAX,-FLT_MAX,-FLT_MAX};
        for (auto& binding:bindings) {
            auto& part=parts.emplace_back();
            const auto& asset=*binding.asset;
            part.resource=asset.resource_key;
            const auto& geometry=asset.geometry;
            const auto sleeved=authored?camp_sleeves(asset,authored->sleeveRoll):std::vector<owned::skn::Vertex>{};
            const auto& vertices=sleeved.empty()?asset.rest_vertices:sleeved;
            for(unsigned i=0;i<geometry.materials.size();++i) {
                auto& surface=part.surfaces.emplace_back();
                auto& mat=surface.material;
                mat=geometry.materials[i];
                if(blueprint) {
                    const float gray=.2126f*mat.base[0]+.7152f*mat.base[1]+.0722f*mat.base[2];
                    std::fill_n(mat.base,3,gray);
                    std::fill_n(mat.emission,3,0.f);
                }
                surface.maps=asset.textures.at(i);
                const auto color=binding.base_color_overrides.find(i);
                const auto orm=binding.metallic_roughness_overrides.find(i);
                if(color!=binding.base_color_overrides.end()) surface.maps[0]=*color->second;
                if(orm!=binding.metallic_roughness_overrides.end()) surface.maps[2]=*orm->second;
                for(unsigned k=0;k<surface.maps.size();++k) {
                    const auto& pixels=surface.maps[k].blob.bytes;
                    if(pixels) bytes+=pixels->size();
                    storage.retain(surface.maps[k],surface.backing[k]);
                }
            }
            const auto& lod=geometry.lods.at(0);
            for (unsigned s=lod.first_submesh;s<lod.first_submesh+lod.submesh_count;++s) {
                const auto& sub=geometry.submeshes.at(s);
                std::unordered_map<unsigned,std::uint16_t> local;
                CampItemMesh* mesh=nullptr;
                for (unsigned i=0;i<sub.count;i+=3) {
                    if (!mesh || mesh->components.size()+3>65535) {
                        mesh=&part.meshes.emplace_back();
                        mesh->material=sub.material;
                        local.clear();
                    }
                    CampItemFit::Triangle triangle;
                    for (unsigned k=0;k<3;++k) {
                        const auto index=geometry.indices.at(sub.first+i+k);
                        const auto& vertex=vertices.at(index);
                        if(reference.valid || authored) {
                            auto& point=k==0?triangle.a:k==1?triangle.b:triangle.c;
                            std::copy_n(vertex.position,3,point.begin());
                        }
                        const auto insert=local.emplace(index,static_cast<std::uint16_t>(mesh->components.size()));
                        if (insert.second) {
                            mesh->append(vertex);
                            for (unsigned j=0;j<3;++j) {
                                low[j]=(std::min)(low[j],vertex.position[j]);
                                high[j]=(std::max)(high[j],vertex.position[j]);
                            }
                        }
                        mesh->indices.push_back(insert.first->second);
                    }
                    if(reference.valid || authored) triangles.push_back(triangle);
                }
            }
        }
        if(authored || (clothing && reference.valid)) {
            const CampItemFit::ClothingPose pose(triangles,reference.clothing,slot=="armor_6",slot=="armor_5",authored);
            for(auto& part:parts) for(auto& mesh:part.meshes)
                for(unsigned i=0;i<mesh.components.size();++i) {
                    const unsigned lane=i%4;
                    auto& p=mesh.positions[i/4];auto& n=mesh.normals[i/4];
                    CampItemFit::Point point{p.x[lane],p.y[lane],p.z[lane]},normal{n.x[lane],n.y[lane],n.z[lane]};
                    pose.apply(point,normal);
                    p.x[lane]=point[0];p.y[lane]=point[1];p.z[lane]=point[2];
                    n.x[lane]=normal[0];n.y[lane]=normal[1];n.z[lane]=normal[2];
                }
        }
        for (auto& part:parts) for (auto& mesh:part.meshes) {
            owned::NativeGeometryView geometry{};
            mesh.streams(geometry);
            mesh.fingerprint=OwnedRender::E4::ItemMeshFingerprint(geometry);
            bytes+=(mesh.positions.capacity()+mesh.normals.capacity())*sizeof(owned::NativePackedGroup)
                 +mesh.uv.capacity()*sizeof(owned::NativeTextureCoordinate)
                 +mesh.components.capacity()*sizeof(owned::NativeVertexComponent)+mesh.indices.capacity()*sizeof(std::uint16_t);
        }
        if(authored) {
            cardMax=authored->cardMax;cardCenter=authored->cardCenter;
            return;
        }
        CampItemFit::Cloud source;
        if(reference.valid && (clothing || source.prepare(triangles))) {
            if(!clothing) placement=CampItemFit::fit(source,reference.cloud,true);
            cardMax={reference.bounds.maximum[0],reference.bounds.maximum[1]};
            cardCenter={reference.bounds.center[0],reference.bounds.center[1]};
            if ((std::max)(cardMax[0],cardMax[1])<=1.e-6f) cardMax={1,1};
            return;
        }
        // A GLB can exist before a legacy FIG is supplied. Preserve the authored upright model.
        for (unsigned j=0;j<3;++j) center[j]=(low[j]+high[j])*.5f;
        for (const auto& part:parts) for (const auto& mesh:part.meshes)
            for (unsigned i=0;i<mesh.components.size();++i) {
                const auto& p=mesh.positions[i/4];
                const float x=p.x[i%4]-center[0], y=p.y[i%4]-center[1], z=p.z[i%4]-center[2];
                radius=(std::max)(radius,x*x+y*y+z*z);
            }
        radius=std::sqrt(radius);
        if (!std::isfinite(radius) || radius<=0) throw std::runtime_error("empty inventory item bounds");
        placement.sourceCenter=center;
        placement.scale=1.f/radius;
    }

    void draw(CRenderer* renderer,const OwnedRender::SinglePreviewPlacement& p) const {
        auto* gpu=renderer->GetGPU();
        if (!gpu->IsHaveMatrix(MAT_STACK_PROJSPACE)) return;
        const auto& projection=gpu->GetMatrix(MAT_STACK_PROJSPACE);
        const auto at=owned::widescreen::native_camera_point(WideScreen::PlaceX(p.x),WideScreen::PlaceY(p.y),p.z);
        CQuaternion rotation=QNULL;
        for (unsigned i=0;i<p.rotation_count;++i) {
            const auto& r=p.rotations[i];
            rotation=rotation*CQuaternion(CPlot3D(r.axis[0],r.axis[1],r.axis[2]),r.angle);
        }
        SHMatrix transform; Identity(transform); rotation.DecompEulerMatrix(transform);
        SHMatrix fit; Identity(fit);
        const auto& basis=placement.basis;
        const auto offset=CampItemFit::rotate(basis,placement.sourceCenter);
        const float scale=p.scale*placement.scale;
        fit._11=scale*basis[0][0];fit._12=scale*basis[0][1];fit._13=scale*basis[0][2];
        fit._21=scale*basis[1][0];fit._22=scale*basis[1][1];fit._23=scale*basis[1][2];
        fit._31=scale*basis[2][0];fit._32=scale*basis[2][1];fit._33=scale*basis[2][2];
        fit._14=p.scale*placement.targetCenter[0]-scale*offset[0];
        fit._24=p.scale*placement.targetCenter[1]-scale*offset[1];
        fit._34=p.scale*placement.targetCenter[2]-scale*offset[2];
        transform._14=at.x; transform._24=at.y; transform._34=at.z;
        transform=transform*fit;
        const auto combined=projection*transform;
        std::array<float,16> clip;
        std::memcpy(clip.data(),&projection,sizeof(projection));
        OwnedRender::ModelPreviewLighting light;
        if (!OwnedRender::produce_model_preview_lighting(clip,{at.x,at.y,at.z},light,owned::PreviewStudioStyle::soft_figure)) return;
        if(clothing) {
            light.world.ambient={.55f,.55f,.55f,1.f};
            light.world.sunlight_diffuse={1.f,.98f,.96f,1.f};
        } else light.world.ambient={.78f,.78f,.78f,1.f};
        for (const auto& part:parts) for (const auto& mesh:part.meshes) {
            const auto& surface=part.surfaces.at(mesh.material);
            owned::NativeGeometryView view{};
            mesh.streams(view);
            view.content_fingerprint=mesh.fingerprint;
            view.content_fingerprint_valid=true;
            view.semantic_kind=owned::SemanticKind::figure;
            view.ordering=owned::NativeOrdering::preserve;
            view.default_diffuse=0xffffffffu; view.default_specular=p.specular;
            if (p.specular&0xffffffu) view.flags|=8u;
            view.fog=255; view.lighting.enabled=1;
            std::memcpy(view.lighting.object_to_world.data(),&transform,sizeof(transform));
            std::memcpy(view.object_to_clip.data(),&combined,sizeof(combined));
            owned::retained_visual_part_fixed_state({},view.owned_fixed_state);
            owned::skn::apply_material_view(view,surface.material,surface.maps,surface.maps[0],nullptr,nullptr);
            // A head or torso normally occludes the inside of a worn shell.
            // In the inventory keep the outward faces, including the open front of a cap.
            if(clothing) view.owned_fixed_state.cull_mode=D3DCULL_CW;
            if (!renderer->GetD3D()->SubmitC74Figure(view,&light.world,light.points)) {
                if (!reported) GameJournal::Event("RENDER","camp_item_glb_failed","item=%s",name.c_str());
                reported=true;
                return;
            }
        }
        if (!reported) GameJournal::Event("RENDER","camp_item_glb_draw","item=%s model=%s parts=%zu",
            name.c_str(),parts.front().resource.c_str(),parts.size());
        reported=true;
    }
};

struct CampItemRequest {
    owned::VisualEquipmentItem item;
    std::string figure;
    bool blueprint=false;
};
struct CampItemLoader {
    CampPreviewStorage storage;
    // Neighboring variants often share the same GLB. Keep a small import window
    // of sources while preparing stock; the published models own only draw data.
    std::deque<std::shared_ptr<const owned::gltf::Asset>> recentSources;
    std::deque<std::shared_ptr<const owned::skn::Dds>> recentTextures;
    std::size_t recentPixels=0;
    void remember(const std::shared_ptr<const owned::skn::Dds>& texture) {
        if(std::find(recentTextures.begin(),recentTextures.end(),texture)!=recentTextures.end()) return;
        recentTextures.push_back(texture);recentPixels+=texture->blob.bytes->size();
        while(recentPixels>(96ull<<20) && !recentTextures.empty()) {
            recentPixels-=recentTextures.front()->blob.bytes->size();recentTextures.pop_front();
        }
    }
    owned::ResourceStore store;
    void idle() {recentSources.clear();recentTextures.clear();recentPixels=0;storage.prune();}
    bool opened=false;
    std::map<std::string,CampFigureReference> references;

    bool posesRead=false;
    std::map<std::string,CampItemFit::AuthoredClothingPose> clothingPoses;

    const CampItemFit::AuthoredClothingPose* inventoryPose(const CampItemRequest& request,const std::string& resource) {
        if(!posesRead && open()) {
            posesRead=true;
            owned::ResourceBlob blob; owned::ResourceFailure why;
            if(store.resolve("models/inventory-poses.v1.json",blob,why)) {
                try {
                    const std::string text(blob.bytes->begin(),blob.bytes->end());
                    const auto poses=owned::small_json::Reader(text).root();
                    const auto loadVector=[](auto& target,const owned::small_json::Value& value) {
                        const auto& values=value.list();
                        owned::small_json::require(values.size()==target.size(),"invalid inventory pose vector");
                        for(unsigned i=0;i<target.size();++i) target[i]=float(values[i].num());
                    };
                    for(const auto& entry:poses.members) {
                        CampItemFit::AuthoredClothingPose pose;
                        const auto sleeves=entry.second.members.find("sleeveRoll");
                        if(sleeves!=entry.second.members.end()) pose.sleeveRoll=float(sleeves->second.num());
                        loadVector(pose.cardMax,entry.second.field("cardMax"));
                        loadVector(pose.cardCenter,entry.second.field("cardCenter"));
                        const auto& parts=entry.second.field("pieces").list();
                        owned::small_json::require(!parts.empty() && parts.size()<=2,"invalid inventory pose pieces");
                        for(const auto& part:parts) {
                            auto& spec=pose.pieces.emplace_back();
                            loadVector(spec.center,part.field("center"));
                            spec.height=float(part.field("height").num());
                            spec.yawDegrees=float(part.field("yaw").num());
                            spec.rollDegrees=float(part.field("roll").num());
                            const auto pitch=part.members.find("pitch");
                            if(pitch!=part.members.end()) spec.pitchDegrees=float(pitch->second.num());
                            owned::small_json::require(spec.height>0 && spec.height<100,"invalid inventory pose height");
                        }
                        clothingPoses.emplace(entry.first,std::move(pose));
                    }
                } catch(const std::exception& e) {
                    GameJournal::Event("RENDER","camp_inventory_poses_invalid","reason=%s",e.what());
                }
            }
        }
        // Some meshes serve different shapes or material cutouts. Use their
        // measured inventory pose before the resource-wide presentation.
        const auto variant=clothingPoses.find(request.item.shape+"|"+request.item.material+"|"+resource);
        if(variant!=clothingPoses.end()) return &variant->second;
        const auto it=clothingPoses.find(resource);
        return it==clothingPoses.end()?nullptr:&it->second;
    }

    bool open() {
        if(!opened) {
            owned::ResourceStoreConfig mounts;
            owned::ResourceFailure failure;
            opened=owned::owned_resource_runtime_mounts(mounts) && store.open(mounts,failure);
        }
        return opened;
    }

    // Same UVs and geometry as the item. The optional authored atlas sits
    // alongside its material maps; otherwise keep alpha and desaturate all mips.
    void blueprintTextures(owned::gltf::EquipmentAsset& binding) {
        if(!open()) throw std::runtime_error("inventory resource mounts unavailable");
        const auto& asset=*binding.asset;
        const std::string prefix="models/items/";
        const auto stem=asset.resource_key.substr(prefix.size(),asset.resource_key.size()-prefix.size()-4);
        const auto publish=[&](const std::string& name,const void* data,std::uint32_t size) {
            owned::ResourceBlob result; owned::ResourceFailure failure;
            if(!store.copy_selected_memory(name,data,size,result,failure)) throw std::runtime_error(failure.message);
            return result;
        };
        for(unsigned i=0;i<asset.textures.size();++i) {
            const auto path=prefix+"textures/"+stem+"-blueprint"+(i?"-"+std::to_string(i):std::string())+".png";
            owned::ResourceBlob authored; owned::ResourceFailure failure;
            owned::skn::Dds map;
            if(store.resolve(path,authored,failure)) {
                map=owned::gltf::decode_base_color(authored,publish,{1,1,1,1});
            } else {
                if(failure.code!=owned::ResourceFailureCode::not_found) throw std::runtime_error(failure.message);
                const auto found=binding.base_color_overrides.find(i);
                const auto& color=found!=binding.base_color_overrides.end()?*found->second:asset.textures[i][0];
                if(color.mips.empty()) continue;
                if(color.format!=owned::NativeOwnedTextureFormat::argb8888 || !color.blob.bytes)
                    throw std::runtime_error("unexpected decoded inventory color format");
                auto pixels=*color.blob.bytes;
                for(size_t k=0;k+3<pixels.size();k+=4) {
                    const auto gray=static_cast<std::uint8_t>((19*pixels[k]+183*pixels[k+1]+54*pixels[k+2]+128)>>8);
                    pixels[k]=pixels[k+1]=pixels[k+2]=gray;
                }
                map=color;
                map.blob=publish(path+"#gray",pixels.data(),static_cast<UINT>(pixels.size()));
                for(auto& mip:map.mips) mip.data=map.blob.bytes->data()+(mip.data-color.blob.bytes->data());
            }
            binding.base_color_overrides[i]=std::make_shared<const owned::skn::Dds>(std::move(map));
        }
    }

    const CampFigureReference& reference(const std::string& name,const std::string& slot) {
        auto inserted=references.try_emplace(name);
        auto& result=inserted.first->second;
        if(!inserted.second || name.empty()) return result;
        owned::ResourceFailure failure;
        owned::ResourceBlob blob;
        owned::FigureCpuAsset figure;
        if(!open() || !store.resolve(name+".fig",blob,failure) ||
           !owned::parse_figure_asset(blob,figure,failure) || figure.bounds.empty()) return result;
        std::vector<CampItemFit::Triangle> triangles;
        const auto point=[&](unsigned i) -> CampItemFit::Point {
            const auto index=figure.components.at(figure.indices.at(i)).position;
            const auto& p=figure.positions.at(index/4);
            return {p.x[index%4],p.y[index%4],p.z[index%4]};
        };
        for(unsigned i=0;i+2<figure.indices.size();i+=3)
            triangles.push_back({point(i),point(i+1),point(i+2)});
        if(slot.rfind("armor_",0)==0) {
            result.clothing=CampItemFit::ClothingLayout::measure(triangles,slot=="armor_5" || slot=="armor_6",false);
            result.valid=result.clothing.piece[0].box.valid();
        } else result.valid=result.cloud.prepare(triangles);
        result.bounds=figure.bounds.front();
        return result;
    }

    std::shared_ptr<CampItemModel> operator()(const CampItemRequest& request) {
        if(request.item.shape.empty()) return {};
        try {
            auto bindings=owned::gltf::select_inventory_item(request.item);
            if(bindings.empty()) return {};
            for(const auto& binding:bindings) {
                const auto known=std::find(recentSources.begin(),recentSources.end(),binding.asset);
                if(known!=recentSources.end()) recentSources.erase(known);
                recentSources.push_back(binding.asset);
                if(recentSources.size()>4) recentSources.pop_front();
                for(const auto& color:binding.base_color_overrides) remember(color.second);
                for(const auto& orm:binding.metallic_roughness_overrides) remember(orm.second);
            }
            if(request.blueprint) for(auto& part:bindings) blueprintTextures(part);
            const auto* pose=inventoryPose(request,bindings.front().asset->resource_key);
            const CampFigureReference noFigure{};
            const auto& ref=pose?noFigure:reference(request.figure,request.item.slot);
            auto model=std::make_shared<CampItemModel>(std::move(bindings),
                request.item.slot,request.blueprint,ref,pose,storage);
            for(auto& part:model->parts) for(auto& mesh:part.meshes) mesh.seal(storage);
            model->name=request.item.shape+"/"+(request.blueprint?"blueprint":request.item.material);
            GameJournal::Event("RENDER","camp_item_prepared","item=%s fig=%s aligned=%d authored_pose=%d bytes=%zu",
                model->name.c_str(),request.figure.c_str(),int(ref.valid || pose),int(pose!=nullptr),model->bytes);
            return model;
        } catch(const std::exception& e) {
            GameJournal::Event("RENDER","camp_item_glb_unavailable","item=%s reason=%s",request.item.shape.c_str(),e.what());
            return {};
        }
    }
};
}

struct CampItemModelCache {
    using Key=std::tuple<DWORD,DWORD,DWORD>;
    CampPreviewCache<Key,CampItemRequest,CampItemModel,CampItemLoader> cache;
    static const CItemLoot* blueprint(const CItem& item) {
        if(item.GetTypeID()!=ITID_ITEM_LOOT) return nullptr;
        const auto& loot=static_cast<const CItemLoot&>(item);
        if(loot.GetLootType()!=IDBLIT_INSTRUCTION) return nullptr;
        const auto type=loot.GetInstructionType();
        return type==IDBINS_ARMOR || type==IDBINS_WEAPON ? &loot : nullptr;
    }
    CampPreviewSelection<CampItemModel> find(const CItem& item,bool preload=false) {
        const auto* instruction=blueprint(item);
        if(!instruction && item.GetTypeID()!=ITID_ITEM_ARMOR && item.GetTypeID()!=ITID_ITEM_WEAPON) return {nullptr,true};
        const Key key=instruction?Key{ITID_ITEM_LOOT,instruction->GetInstructionType(),instruction->GetInstructionItemIndex()}:
                                  Key{item.GetTypeID(),item.GetSubtypeID(),0};
        return cache.get(key,preload,[&] {
            CampItemRequest request;
            const auto* shape=instruction?instruction->GetInstructionShape():item.GetShapeEntry();
            if(!shape) return request;
            request.blueprint=instruction!=nullptr;
            if(!instruction) {
                const auto* material=theItemDatabase.materials[item.GetMaterial()];
                if(!material) return request;
                request.item.material=material->GetCode();
            }
            request.figure=LPCSTR(item.GetI3DFigureName());
            request.item.shape=shape->GetCode();
            if(shape->GetTypeID()==ITID_ITEM_ARMOR) {
                request.item.kind=owned::VisualEquipmentItem::Kind::armor;
                request.item.slot="armor_"+std::to_string(static_cast<const SIDBArmorShape*>(shape)->dwTypeID);
            } else request.item.slot="hand_r";
            return request;
        });
    }
};
