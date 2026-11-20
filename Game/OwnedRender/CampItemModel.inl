#include "gltf_asset.h"
#include "skn_material_view.h"
#include "retained_static_submission.h"
#include "ModelPreviewLighting.h"
#include "../WideScreen.h"
#include "CampItemFit.h"
#include "CampPreviewCache.h"

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

    void streams(owned::NativeGeometryView& view) const {
        view.positions=positions.data(); view.position_groups=static_cast<UINT>(positions.size());
        view.normals=normals.data(); view.normal_groups=static_cast<UINT>(normals.size());
        view.texture_coordinates=uv.data(); view.texture_coordinate_count=static_cast<UINT>(uv.size());
        view.components=components.data(); view.component_count=static_cast<UINT>(components.size());
        view.indices=indices.data(); view.index_count=static_cast<UINT>(indices.size());
    }
};

struct CampItemPart {
    owned::gltf::EquipmentAsset binding;
    std::vector<CampItemMesh> meshes;
    std::vector<owned::skn::Material> blueprintMaterials;
};

struct CampFigureReference {
    CampItemFit::Cloud cloud;
    owned::FigureBounds bounds;
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

    CampItemModel(std::vector<owned::gltf::EquipmentAsset> bindings, bool weapon,bool blueprint,const CampFigureReference& reference) {
        std::vector<CampItemFit::Triangle> triangles;
        std::array<float,3> low{FLT_MAX,FLT_MAX,FLT_MAX},high{-FLT_MAX,-FLT_MAX,-FLT_MAX};
        for (auto& binding:bindings) {
            auto& part=parts.emplace_back();
            part.binding=std::move(binding);
            const auto& asset=*part.binding.asset;
            const auto& geometry=asset.geometry;
            if(blueprint) {
                part.blueprintMaterials=geometry.materials;
                for(auto& mat:part.blueprintMaterials) {
                    const float gray=.2126f*mat.base[0]+.7152f*mat.base[1]+.0722f*mat.base[2];
                    std::fill_n(mat.base,3,gray);
                    std::fill_n(mat.emission,3,0.f);
                }
            }
            // A conservative charge counts shared textures in each variant. Visible rows stay pinned.
            bytes+=asset.document.bytes->size()+asset.rest_vertices.capacity()*sizeof(owned::skn::Vertex)
                 +geometry.vertices.capacity()*sizeof(owned::skn::Vertex)+geometry.indices.capacity()*sizeof(unsigned);
            const auto textureBytes=[](const owned::skn::Dds& d) {return d.blob.bytes?d.blob.bytes->size():0;};
            for(const auto& material:asset.textures) for(const auto& map:material) bytes+=textureBytes(map);
            for(const auto& map:part.binding.base_color_overrides) bytes+=textureBytes(*map.second);
            for(const auto& map:part.binding.metallic_roughness_overrides) bytes+=textureBytes(*map.second);
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
                        const auto& vertex=asset.rest_vertices.at(index);
                        if(reference.valid) {
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
                    if(reference.valid) triangles.push_back(triangle);
                }
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
        CampItemFit::Cloud source;
        if(reference.valid && source.prepare(triangles)) {
            placement=CampItemFit::fit(source,reference.cloud,weapon);
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
        light.world.ambient={.78f,.78f,.78f,1.f};
        for (const auto& part:parts) for (const auto& mesh:part.meshes) {
            const auto& b=part.binding;
            const auto& maps=b.asset->textures.at(mesh.material);
            const auto color=b.base_color_overrides.find(mesh.material);
            const auto orm=b.metallic_roughness_overrides.find(mesh.material);
            const auto identity=b.material_identities.find(mesh.material);
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
            const bool blueprint=!part.blueprintMaterials.empty();
            const auto& material=blueprint?part.blueprintMaterials.at(mesh.material):b.asset->geometry.materials.at(mesh.material);
            owned::skn::apply_material_view(view,material,maps,
                color==b.base_color_overrides.end()?maps[0]:*color->second,
                orm==b.metallic_roughness_overrides.end()?nullptr:orm->second.get(),
                blueprint || identity==b.material_identities.end()?nullptr:identity->second.get());
            if (!renderer->GetD3D()->SubmitC74Figure(view,&light.world,light.points)) {
                if (!reported) GameJournal::Event("RENDER","camp_item_glb_failed","item=%s",name.c_str());
                reported=true;
                return;
            }
        }
        if (!reported) GameJournal::Event("RENDER","camp_item_glb_draw","item=%s model=%s parts=%zu",
            name.c_str(),parts.front().binding.asset->resource_key.c_str(),parts.size());
        reported=true;
    }
};

struct CampItemRequest {
    owned::VisualEquipmentItem item;
    std::string figure;
    bool blueprint=false;
};
struct CampItemLoader {
    owned::ResourceStore store;
    bool opened=false;
    std::map<std::string,CampFigureReference> references;

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

    const CampFigureReference& reference(const std::string& name) {
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
        result.valid=result.cloud.prepare(triangles);
        result.bounds=figure.bounds.front();
        return result;
    }

    std::shared_ptr<CampItemModel> operator()(const CampItemRequest& request) {
        if(request.item.shape.empty()) return {};
        try {
            auto bindings=owned::gltf::select_inventory_item(request.item);
            if(bindings.empty()) return {};
            if(request.blueprint) for(auto& part:bindings) blueprintTextures(part);
            const auto& ref=reference(request.figure);
            auto model=std::make_shared<CampItemModel>(std::move(bindings),
                request.item.kind==owned::VisualEquipmentItem::Kind::weapon,request.blueprint,ref);
            model->name=request.item.shape+"/"+(request.blueprint?"blueprint":request.item.material);
            GameJournal::Event("RENDER","camp_item_prepared","item=%s fig=%s aligned=%d bytes=%zu",
                model->name.c_str(),request.figure.c_str(),int(ref.valid),model->bytes);
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
    CampPreviewCache<Key,CampItemRequest,CampItemModel,CampItemLoader> cache{384ull<<20};
    static const CItemLoot* blueprint(const CItem& item) {
        if(item.GetTypeID()!=ITID_ITEM_LOOT) return nullptr;
        const auto& loot=static_cast<const CItemLoot&>(item);
        if(loot.GetLootType()!=IDBLIT_INSTRUCTION) return nullptr;
        const auto type=loot.GetInstructionType();
        return type==IDBINS_ARMOR || type==IDBINS_WEAPON ? &loot : nullptr;
    }
    std::shared_ptr<CampItemModel> find(const CItem& item,std::uint64_t now) {
        const auto* instruction=blueprint(item);
        if(!instruction && item.GetTypeID()!=ITID_ITEM_ARMOR && item.GetTypeID()!=ITID_ITEM_WEAPON) return {};
        const Key key=instruction?Key{ITID_ITEM_LOOT,instruction->GetInstructionType(),instruction->GetInstructionItemIndex()}:
                                  Key{item.GetTypeID(),item.GetSubtypeID(),0};
        return cache.find(key,now,[&] {
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
