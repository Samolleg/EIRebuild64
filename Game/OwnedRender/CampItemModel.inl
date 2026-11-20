#include "gltf_asset.h"
#include "skn_material_view.h"
#include "retained_static_submission.h"
#include "ModelPreviewLighting.h"
#include "../WideScreen.h"

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
};

struct CampItemModel {
    std::vector<CampItemPart> parts;
    std::array<float,3> center{};
    float radius=0;
    CQuaternion displayRotation=QNULL;
    std::string name;
    mutable bool reported=false;

    CampItemModel(std::vector<owned::gltf::EquipmentAsset> bindings, bool weapon) {
        std::array<float,3> low{FLT_MAX,FLT_MAX,FLT_MAX},high{-FLT_MAX,-FLT_MAX,-FLT_MAX};
        for (auto& binding:bindings) {
            auto& part=parts.emplace_back();
            part.binding=std::move(binding);
            const auto& asset=*part.binding.asset;
            const auto& geometry=asset.geometry;
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
                    for (unsigned k=0;k<3;++k) {
                        const auto index=geometry.indices.at(sub.first+i+k);
                        const auto insert=local.emplace(index,static_cast<std::uint16_t>(mesh->components.size()));
                        if (insert.second) {
                            const auto& vertex=asset.rest_vertices.at(index);
                            mesh->append(vertex);
                            for (unsigned j=0;j<3;++j) {
                                low[j]=(std::min)(low[j],vertex.position[j]);
                                high[j]=(std::max)(high[j],vertex.position[j]);
                            }
                        }
                        mesh->indices.push_back(insert.first->second);
                    }
                }
            }
        }
        for (auto& part:parts) for (auto& mesh:part.meshes) {
            owned::NativeGeometryView geometry{};
            mesh.streams(geometry);
            mesh.fingerprint=OwnedRender::E4::ItemMeshFingerprint(geometry);
        }
        const float width=high[0]-low[0], height=high[1]-low[1], depth=high[2]-low[2];
        // A weapon authored along its grip's depth axis would otherwise point at the camera.
        if (weapon && depth>(std::max)(width,height))
            displayRotation=CQuaternion(CPlot3D(0,0,1),.62f)*CQuaternion(CPlot3D(1,0,0),1.36f);
        else
            displayRotation=CQuaternion(CPlot3D(0,1,0),.35f)*CQuaternion(CPlot3D(1,0,0),.18f);
        for (unsigned j=0;j<3;++j) center[j]=(low[j]+high[j])*.5f;
        for (const auto& part:parts) for (const auto& mesh:part.meshes)
            for (unsigned i=0;i<mesh.components.size();++i) {
                const auto& p=mesh.positions[i/4];
                const float x=p.x[i%4]-center[0], y=p.y[i%4]-center[1], z=p.z[i%4]-center[2];
                radius=(std::max)(radius,x*x+y*y+z*z);
            }
        radius=std::sqrt(radius);
        if (!std::isfinite(radius) || radius<=0) throw std::runtime_error("empty inventory item bounds");
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
        rotation=rotation*displayRotation;
        SHMatrix transform; Identity(transform); rotation.DecompEulerMatrix(transform);
        SHMatrix fit; Identity(fit);
        const float scale=p.scale/radius;
        fit._11=fit._22=fit._33=scale;
        fit._14=-scale*center[0]; fit._24=-scale*center[1]; fit._34=-scale*center[2];
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
            owned::skn::apply_material_view(view,b.asset->geometry.materials.at(mesh.material),maps,
                color==b.base_color_overrides.end()?maps[0]:*color->second,
                orm==b.metallic_roughness_overrides.end()?nullptr:orm->second.get(),
                identity==b.material_identities.end()?nullptr:identity->second.get());
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
}

struct CampItemModelCache {
    struct Entry { std::unique_ptr<CampItemModel> model; std::uint64_t lastFrame=0; };
    std::map<std::pair<DWORD,DWORD>,Entry> entries;
    std::uint64_t frame=0;
    const CampItemModel* find(const CItem& item, std::uint64_t now) {
        if (frame!=now) {
            frame=now;
            for (auto it=entries.begin();it!=entries.end();) {
                if (it->second.model && now-it->second.lastFrame>120) it=entries.erase(it);
                else ++it;
            }
        }
        auto result=entries.try_emplace({item.GetTypeID(),item.GetSubtypeID()});
        auto& entry=result.first->second;
        entry.lastFrame=frame;
        if (result.second) {
            const auto* shape=item.GetShapeEntry();
            const auto* material=theItemDatabase.materials[item.GetMaterial()];
            if (!shape || !material) return nullptr;
            owned::VisualEquipmentItem selected;
            selected.shape=shape->GetCode(); selected.material=material->GetCode();
            if (item.GetTypeID()==ITID_ITEM_ARMOR) {
                selected.kind=owned::VisualEquipmentItem::Kind::armor;
                selected.slot="armor_"+std::to_string(static_cast<const CItemArmor&>(item).GetArmorType());
            } else selected.slot="hand_r";
            try {
                auto bindings=owned::gltf::select_inventory_item(selected);
                if (!bindings.empty()) {
                    entry.model=std::make_unique<CampItemModel>(std::move(bindings),item.GetTypeID()==ITID_ITEM_WEAPON);
                    entry.model->name=selected.shape+"/"+selected.material;
                }
            } catch (const std::exception& error) {
                GameJournal::Event("RENDER","camp_item_glb_unavailable","item=%s material=%s reason=%s",
                    selected.shape.c_str(),selected.material.c_str(),error.what());
            }
        }
        return entry.model.get();
    }
};
