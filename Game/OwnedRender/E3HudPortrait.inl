// Typed EIRebuild64 inputs for the renderer author's HUD portrait pass.
namespace {
using namespace owned;

bool portraitHair(const std::string& name,const std::string& model,std::int32_t& hair) {
    const std::string prefix = "infa" + model;
    // Native AddFaces (6409F3..640A46) names the face with the selected
    // hair + 1, independent of the equipped parts. Zero is native no-hair.
    if (name.size() < prefix.size()+7 || name.compare(0,prefix.size(),prefix) != 0 ||
        name.compare(name.size()-4,4,"face") != 0) return false;
    const auto grace = name.substr(prefix.size(),2);
    if (grace != "th" && grace != "me" && grace != "fa") return false;
    const std::size_t first = prefix.size()+2, end = name.size()-4;
    if (end-first > 1 && name[first] == '0') return false;
    std::int32_t encoded = 0;
    for (std::size_t i = first; i < end; ++i) {
        if (name[i] < '0' || name[i] > '9') return false;
        const int digit = name[i]-'0';
        if (encoded > ((std::numeric_limits<std::int32_t>::max)()-digit)/10) return false;
        encoded = encoded*10+digit;
    }
    hair = encoded-1;
    return true;
}


bool portraitFile(CResourceManager* manager,const std::string& name,ResourceBlob& blob) {
    if (!manager) return false;
    DWORD size=0;
    BYTE* bytes=manager->GetFile(name.c_str(),&size);
    if (!bytes) return false;
    struct Close { CResourceManager* manager; BYTE* bytes; ~Close(){manager->CloseFile(bytes);} } close{manager,bytes};
    ResourceFailure failure;
    std::uint64_t generation=0;
    return retained_visual_copy_selected_memory(name,bytes,size,blob,generation,failure);
}

bool portraitPictures(const std::string& neutral,const std::string& expression,
                      std::shared_ptr<const TextureCpuAsset>& first,std::shared_ptr<const TextureCpuAsset>& second) {
    for (const char* suffix : {".x4.mmp",".x2.mmp",".mmp"}) {
        ResourceBlob a,b;
        ResourceFailure failure;
        if (portraitFile(state().textures,neutral+suffix,a) && portraitFile(state().textures,expression+suffix,b)) {
            first=retained_visual_texture_asset(a,failure);
            second=retained_visual_texture_asset(b,failure);
            if (first && second) return true;
        }
    }
    return false;
}

bool livePortrait(CI3DFigure* figure,CRenderer* game,unsigned slot,int unitId,const char* neutral,
                  const char* selected,std::array<float,16> clip,
                  std::shared_ptr<const TextureCpuAsset>& expressionMask) {
    struct Cache {
        VisualResourceIncarnation resource{};
        std::shared_ptr<VisualPreparedResources> prepared;
        std::shared_ptr<const VisualAssemblyAsset> source,bare;
        int hair=-2;
        std::string expression;
        std::shared_ptr<OwnedPortraitOverlay> overlay;
        std::string lastResult;
        std::uint64_t previewSerial=0;
        std::shared_ptr<const VisualAssemblyAsset> fitSource,poseSource;
        std::array<float,16> poseMatrix{};
        std::array<float,3> fitLo{},fitHi{},faceLo{},faceHi{};
    };
    static std::array<Cache,32> caches;
    if (slot>=caches.size()) return false;
    auto& cache=caches[slot];
    const auto result=[&](const char* why,bool ok=false){
        if (cache.lastResult!=why) {
            cache.lastResult=why;
            GameJournal::Event("RENDER","hud_live_portrait","slot=%u id=%d result=%s",slot,unitId,why);
        }
        return ok;
    };
    auto& scene=OwnedRender::E1Scene().Scene();
    const auto id=scene.visual_resource(unitId);
    RetainedVisualSample sample;
    std::uint64_t revision=0;
    if (!id.serial || !scene.visual_preview_source(id,sample,revision) || !sample.assembly)
        return result("unit_visual_source");
    if (!cache.prepared || id.serial!=cache.resource.serial || id.signed_id!=cache.resource.signed_id) {
        static std::uint64_t nextPortrait=0;
        cache={}; cache.resource=id;
        cache.previewSerial=(std::uint64_t(3)<<62) | ++nextPortrait; cache.prepared=std::make_shared<VisualPreparedResources>();
    }
    HudAssetFrame frame;
    if (!embedded_hud_layout().portrait_frame(slot,frame)) return result("portrait_frame");
    const auto& layout=widescreen_layout();
    widescreen::compose_hud_clip(clip.data(),widescreen::hud_blit,layout.width,layout.height);
    auto hud=widescreen::hud_blit;
    const float border=std::round(frame.thickness);
    float l=std::round(frame.bounds.left)+border,t=std::round(frame.bounds.top)+border;
    float rr=std::round(frame.bounds.left+frame.bounds.width)-border,bb=std::round(frame.bounds.top+frame.bounds.height)-border;
    if (hud.clip_native_screen && hud.clip_right>=0) {
        l=std::max(l,hud.clip_left);t=std::max(t,hud.clip_top);
        rr=std::min(rr,hud.clip_right);bb=std::min(bb,hud.clip_bottom);
    }
    if(rr<=l || bb<=t) return true;
    hud.clip_native_screen=true;hud.clip_left=l;hud.clip_top=t;hud.clip_right=rr;hud.clip_bottom=bb;
    const auto* bounds=figure->GetBoundingVolume();
    struct {std::array<float,3> minimum,maximum;} box{
        {bounds->ptMin.x,bounds->ptMin.y,bounds->ptMin.z},{bounds->ptMax.x,bounds->ptMax.y,bounds->ptMax.z}};
        const auto select_parts = [](const VisualAssemblyAsset& assembly) {
            std::vector<std::size_t> selected;
            for (std::size_t i = 0; i < assembly.parts.size(); ++i) {
                const auto& part = assembly.parts[i];
                if (!part.source_available || part.explicitly_hidden || !part.geometry || !part.material || part.weapon) continue;
                const auto stem = fur_part_stem(part.name);
                if (stem == "hd" || fur_hair_stem(stem)) selected.push_back(i);
            }
            return selected;
        };

    auto parts=select_parts(*sample.assembly);
    if(parts.empty()) return result("head_parts");
    bool helmeted=std::any_of(parts.begin(),parts.end(),[&](auto i){
        return sample.assembly->parts[i].name.find(".armor")!=std::string::npos;
    });
    std::shared_ptr<const VisualAssemblyAsset> bare_fit;
    bool registrySelected=false;
    const auto registryModel=skn::select_gltf(sample.assembly->skeleton->visual_reference,
        sample.assembly->main_hero_visual,registrySelected);
    if (registryModel) {
        sample.object_pose.shape=sample.assembly->shape;
        if (sample.assembly->main_hero_visual)
            if (auto* unit=theWorld.GetUnit(unitId)) sample.physique=unit->VisualPhysiqueInput();
        helmeted=std::any_of(sample.assembly->equipment.begin(),sample.assembly->equipment.end(),
            [](const auto& item){return item.slot=="armor_0";});
        if (helmeted && !g_portrait_helmet) {
            if(cache.source!=sample.assembly || !cache.bare) {
                cache.source=sample.assembly;
                auto bare=std::make_shared<VisualAssemblyAsset>(*sample.assembly);
                auto& items=bare->equipment;
                items.erase(std::remove_if(items.begin(),items.end(),[](const auto& item){return item.slot=="armor_0";}),items.end());
                cache.bare=std::move(bare);
            }
            sample.assembly=cache.bare;
        }
    } else if(helmeted) {
        const auto& model=sample.assembly->skeleton->visual_reference;
        int hair=-1;
        if (!figure->GetName() || !portraitHair(figure->GetName(),model,hair)) return result("hair_identity");
        if(cache.source!=sample.assembly || cache.hair!=hair || !cache.bare){
            const char* why="bare_model";
            cache.source=sample.assembly;cache.hair=hair;
            cache.bare=build_owned_portrait_bare_assembly(*sample.assembly,
                [&](ResourceBlob& b){return portraitFile(state().figures,model+".mod",b);},
                [](const std::string& name,ResourceBlob& b){return portraitFile(state().textures,name,b);},hair,why);
            if(!cache.bare) return result(why);
        }
        if(g_portrait_helmet) bare_fit=cache.bare;
        else {sample.assembly=cache.bare;parts=select_parts(*sample.assembly);}
    }
        std::array<float,3> lo{}, hi{}, face_lo{}, face_hi{};
        if (registryModel && registryModel->gltf_asset && registryModel->gltf_asset->face_indices) {
            const auto& body=*registryModel->gltf_asset;
            lo=body.portrait_min;hi=body.portrait_max;
            face_lo=body.face_min;face_hi=body.face_max;
        } else {
            // The fit boxes: the drawn head and hair, or for a shown helmet the
            // bare head and hair, so the face keeps the bare head's eye line
            // and scale and the frame clip trims the helmet.
            const auto drawn = sample.assembly;
            std::vector<std::size_t> fit_parts = parts, head_parts;
            if (bare_fit) {
                sample.assembly = bare_fit;
                fit_parts = select_parts(*bare_fit);
            }
            for (const auto index : fit_parts)
                if (fur_part_stem(sample.assembly->parts[index].name) == "hd") head_parts.push_back(index);
            if (cache.fitSource!=sample.assembly) {
                if (!retained_visual_neutral_parts_box(sample,fit_parts,cache.fitLo,cache.fitHi) ||
                    !retained_visual_neutral_parts_box(sample,head_parts,cache.faceLo,cache.faceHi)) return result("head_box");
                cache.fitSource=sample.assembly;
            }
            lo=cache.fitLo;hi=cache.fitHi;face_lo=cache.faceLo;face_hi=cache.faceHi;
            sample.assembly = drawn;
        }
        // Axes (first run, 85d2c383): the unit model is Z-up with its face
        // toward -Y; the face figure is Y-up (its box 1.5 x 2.2 x 1.9 against
        // the head's 0.24 x 0.30 x 0.35) and the identity showed the scalp.
        // R: (x, y, z) -> (x, z, -y) stands the head up, face toward the viewer.
        const float head_height = hi[2] - lo[2], face_height = box.maximum[1] - box.minimum[1];
        if (!(head_height > 0) || !(face_height > 0)) { return result("fit_extent"); }
        // The reference FIG fit is calibrated to the original portrait. The GLB
        // head has different proportions; leave room above the slot's status bars.
        const float scale = (registryModel ? .8f : .97f) * face_height / head_height;
        // The face (the head part without hair) placed where the baked face sits.
        const std::array<float,3> head_centre{.5f*(face_lo[0]+face_hi[0]), .5f*(face_lo[1]+face_hi[1]), .5f*(face_lo[2]+face_hi[2])};
        std::array<float,16> base_world{{scale,0,0,0, 0,0,scale,0, 0,-scale,0,0, 0,0,0,1}};
        const std::array<float,3> rotated_centre{head_centre[0], head_centre[2], -head_centre[1]};
        for (unsigned axis = 0; axis < 3; ++axis)
            base_world[axis*4+3] = .5f*(box.minimum[axis]+box.maximum[axis]) - scale*rotated_centre[axis];
        base_world[1*4+3] -= (registryModel ? .06f : .22f) * face_height;

    OwnedPreviewCommand command;
    command.portrait=true;
    command.portrait_clip={l,t,rr,bb};
    command.base_world=base_world;
    command.world_to_clip=clip;
    if (!registryModel) {
        // Each retail face carrier has its own projection and animation. Frame
        // the live FIG geometry in this slot directly, independent of that carrier.
        const float width=hi[0]-lo[0];
        if (!(width>0) || !(layout.width>0 && layout.height>0))return result("portrait_extent");
        const float pixels=std::min((rr-l)*.92f/width,(bb-t)*.8f/head_height);
        const float cx=(lo[0]+hi[0])*.5f, cy=(lo[1]+hi[1])*.5f, cz=(lo[2]+hi[2])*.5f;
        command.base_world={1,0,0,-cx, 0,0,1,-cz, 0,-1,0,cy, 0,0,0,1};
        command.world_to_clip={2*pixels/layout.width,0,0,(l+rr)/layout.width-1,
            0,2*pixels/layout.height,0,1-(t+bb-(bb-t)*.08f)/layout.height,
            0,0,-.25f/std::max(head_height,hi[1]-lo[1]),.5f, 0,0,0,1};
    }
    command.presentation_serial=game->GetD3D()->ModelPreviewFrame();
    command.texture_time=theWorld.GetPreciseTime();
    command.fog=255;
    sample.resource.serial=cache.previewSerial;
    sample.lifecycle={unitId,cache.previewSerial,cache.previewSerial};
    sample.values.unit=true;sample.selection={};sample.selection.neutral=true;
    sample.pose.phase=0;sample.prepared=cache.prepared;
    OwnedRender::ModelPreviewLighting light;
    const std::array<float,3> center{bounds->ptCenter.x,bounds->ptCenter.y,bounds->ptCenter.z};
    if(!OwnedRender::produce_model_preview_lighting(clip,center,light))return result("lighting");
    command.portrait_lighting=light.world;
    auto* device=game->GetD3D();
    const auto fixed=device->CaptureOwnedTerrainMaterial();
        for (const auto index : parts) {
            const auto& source = sample.assembly->parts[index];
            const bool defaults = (source.figure_flags & FIG_USE_DEFAULT_COLORS) != 0u;
            OwnedPreviewPart part;
            part.ordinal = index; part.set = 0;
            part.material = fixed;
            part.uses_default_colors = defaults;
            part.default_diffuse = defaults ? source.diffuse : 0xffffffffu;
            part.default_specular = defaults ? source.specular : 0xff000000u;
            part.material_flags = source.material_flags;
            part.lighting.enabled = defaults ? 0u : 1u;
            part.lighting.material_alpha = 1;
            std::copy_n(source.material->diffuse.begin(), 3, part.lighting.material_diffuse.begin());
            part.lighting.emissive = source.material->emissive;
            part.lighting.specular = source.material->specular; part.lighting.specular[3] = 1;
            part.lighting.object_to_world = command.base_world;
            command.parts.push_back(std::move(part));
        }

    if (!registryModel && (cache.poseSource!=sample.assembly || cache.poseMatrix!=command.base_world)) {
        // Submit exactly the neutral node frames used to fit the portrait. A newly
        // deployed unit can still have a world animation pending on the GPU.
        std::vector<VisualNodeSample> nodes;
        if (!sample_visual_skeleton(*sample.assembly->skeleton,sample.selection,
            retained_visual_shape(sample),sample.assembly->center_shift,0,nodes,false)) return result("portrait_pose");
        VisualPoseState root;
        root.tilted_facing=root.root_rotation_valid=true;
        root.root_rotation={1,0,0,0};
        auto rows=std::make_shared<std::vector<NativeWorldGeometry::Pose::Override>>();
        for (const auto index:parts) {
            const auto node=sample.assembly->parts[index].skeleton_node;
            if (node>=nodes.size()) return result("portrait_node");
            if (std::any_of(rows->begin(),rows->end(),[&](const auto& entry){return entry.node==node;})) continue;
            std::array<float,16> local;
            if (!visual_node_world_matrix(root,nodes[node],{1,1,1},local)) return result("portrait_matrix");
            const auto placed=retained_multiply_matrix(command.base_world,local);
            NativeWorldGeometry::Pose::Override entry;
            entry.node=node;entry.value.rotation=nodes[node].rotation;
            std::copy_n(nodes[node].translation.begin(),3,entry.value.translation.begin());
            for (unsigned row=0;row<3;++row) std::copy_n(placed.begin()+4*row,4,entry.value.rows[row].begin());
            rows->push_back(entry);
        }
        std::sort(rows->begin(),rows->end(),[](const auto& a,const auto& b){return a.node<b.node;});
        sample.prepared->gpu_pose.overrides=std::move(rows);
        cache.poseSource=sample.assembly;cache.poseMatrix=command.base_world;
    }
    command.sample=std::move(sample);
    if(neutral && selected && std::strcmp(neutral,selected)!=0) {
        const char* why="expression";
        if(cache.expression!=selected || !cache.overlay){
            int expression=1;
            for(;expression<=3;++expression)if(owned_portrait_expression_name(neutral,expression)==selected)break;
            if(expression>3)return result("expression_name");
            cache.overlay=owned_portrait_overlay(neutral,expression,portraitPictures,why);
            if(!cache.overlay)return result(why);
            cache.expression=selected;
            GameJournal::Event("RENDER","hud_portrait_expression","slot=%u id=%d expression=%d",slot,unitId,expression);
        }
        // Native character GLBs have their own facial UVs. Project only the
        // changed texels through the game's expression figure for those heads.
        if (registryModel) expressionMask=cache.overlay->texture;
        else command.portrait_overlay=cache.overlay;
    }
    auto sinks=std::make_shared<SceneSinks>();
    sinks->state=device->CaptureC56MaterialState();
    if (!registryModel) sinks->state.viewport={0,0,DWORD(layout.width),DWORD(layout.height),0.f,1.f};
    command.sets.push_back(sinks);
    command.handoff.install_sinks=&SceneSinks::install;
    struct ClipScope {
        widescreen::HudComposition previous=widescreen::hud_blit;
        ~ClipScope(){widescreen::hud_blit=previous;}
    } restore;
    widescreen::hud_blit=hud;
    const HRESULT hr=device->GetRendererFacade().Visit([&](Renderer& renderer) {
        sinks->renderer=&renderer;
        if (renderer.has_pending_ui() && FAILED(renderer.flush_ui()))return E_FAIL;
        return run_owned_preview_command(command)?S_OK:E_FAIL;
    });
    if (FAILED(hr))return result("submission");
    if (cache.lastResult.empty())
        GameJournal::Event("RENDER","hud_portrait_fit","slot=%u native_model=%d parts=%zu groups=%zu scale=%.3f center=%.3f,%.3f,%.3f face=%.3f,%.3f,%.3f bounds=%.0f,%.0f,%.0f,%.0f",
            slot,registryModel?1:0,parts.size(),cache.prepared->merged_groups.size(),scale,
            head_centre[0],head_centre[1],head_centre[2],box.maximum[0],box.maximum[1],box.maximum[2],l,t,rr,bb);
    return result(helmeted?(g_portrait_helmet?"helmet":"bare_hair"):"head",true);
}
}
