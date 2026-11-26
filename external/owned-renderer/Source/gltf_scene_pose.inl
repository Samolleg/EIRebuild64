// glTF scene-space pose shared by the game and the editor. Column vectors,
// column-major matrices, glTF axes until publication at the host boundary.
struct SceneForm {
    using Vector=std::array<float,3>;
    struct Weight {unsigned index;int driver;float value;};
    std::vector<Vector> translations,inverseTranslations;
    std::vector<std::vector<Weight>> weights;

    SceneForm(const cgltf_data& d,const Vector& shape):
        translations(d.nodes_count),inverseTranslations(d.nodes_count),weights(d.nodes_count) {
        for(float value:shape)require(std::isfinite(value),"nonfinite body form");
        const auto extras=[&d](const cgltf_extras& data){
            // cgltf extracts targetNames from mesh.extras and retains the JSON offsets.
            if(data.end_offset<=data.start_offset)return Json::object();
            require(data.end_offset<=d.json_size,"form extras outside JSON");
            const auto obj=Json::parse(d.json+data.start_offset,d.json+data.end_offset);
            return obj.is_object()?obj.value("ei",Json::object()):Json::object();
        };
        for(size_t i=0;i<d.nodes_count;++i){
            const auto& node=d.nodes[i];const auto props=extras(node.extras);
            if(props.contains("formJoint")){
                require(!node.has_matrix,"formJoint requires TRS");
                const auto& data=props.at("formJoint");
                for(unsigned a=0;a<3;++a){
                    const auto local=vector3(data.at("local").at(a));
                    const auto inverse=vector3(data.at("inverse").at(a));
                    for(unsigned k=0;k<3;++k){
                        translations[i][k]+=(shape[a]-.5f)*local[k];
                        inverseTranslations[i][k]+=(shape[a]-.5f)*inverse[k];
                    }
                }
            }
            if(!node.mesh)continue;
            const auto mesh=extras(node.mesh->extras);
            if(!mesh.contains("formTargets"))continue;
            require(node.mesh->primitives_count!=0,"missing form primitive");
            const auto count=node.mesh->primitives[0].targets_count;
            for(const auto& entry:mesh.at("formTargets")){
                const auto index=entry.at("target").get<unsigned>();
                const auto axis=entry.at("axis").get<unsigned>();
                const int driver=entry.value("driver",-1);
                require(index<count && axis<3 && driver>=-1 && (driver<0||size_t(driver)<count),"form target binding");
                weights[i].push_back({index,driver,shape[axis]-.5f});
            }
        }
    }
};

struct ScenePose {
    using Matrix=std::array<float,16>;
    static Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
    static Matrix product(const Matrix& a,const Matrix& b){
        Matrix out{};
        for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)
            for(unsigned k=0;k<4;++k)out[c*4+r]+=a[k*4+r]*b[c*4+k];
        return out;
    }
    const std::vector<SceneForm::Vector>& inverseTranslations;
    std::vector<Matrix> world;
    std::vector<std::vector<float>> weights;
    using Clips=std::map<std::string,gltf::Clip>;
    static Clips loadClips(const cgltf_data& d) {
        Clips clips;
        for(size_t index=0;index<d.animations_count;++index){
            const auto& animation=d.animations[index];
            const auto key=animation.name?lower(animation.name):std::to_string(index);
            auto inserted=clips.emplace(key,gltf::Clip{});
            require(inserted.second,"duplicate animation name");
            auto& clip=inserted.first->second;
            for(size_t i=0;i<animation.channels_count;++i){
                const auto& ch=animation.channels[i];
                require(ch.target_node && ch.sampler && ch.sampler->input && ch.sampler->output,"animation channel");
                const auto& n=*ch.target_node;const auto& s=*ch.sampler;
                require(!n.has_matrix,"matrix animation target");
                gltf::Track tr;
                tr.times=values(s.input,1);require(!tr.times.empty(),"empty animation track");
                for(size_t k=0;k<tr.times.size();++k)require(std::isfinite(tr.times[k])&&(!k||tr.times[k]>tr.times[k-1]),"animation key order");
                tr.interpolation=s.interpolation==cgltf_interpolation_type_cubic_spline?gltf::Interpolation::cubic:
                    s.interpolation==cgltf_interpolation_type_step?gltf::Interpolation::step:gltf::Interpolation::linear;
                const auto width=cgltf_num_components(s.output->type);tr.values=values(s.output,unsigned(width));
                const size_t rows=tr.times.size()*(tr.interpolation==gltf::Interpolation::cubic?3:1);
                require(tr.values.size()%rows==0,"animation output count");tr.width=unsigned(tr.values.size()/rows);
                tr.node=unsigned(ch.target_node-d.nodes);
                switch(ch.target_path){
                case cgltf_animation_path_type_translation:tr.path=gltf::Path::translation;break;
                case cgltf_animation_path_type_rotation:tr.path=gltf::Path::rotation;break;
                case cgltf_animation_path_type_scale:tr.path=gltf::Path::scale;break;
                case cgltf_animation_path_type_weights:tr.path=gltf::Path::weights;break;
                default:throw std::runtime_error("unsupported animation channel");
                }
                if(tr.path==gltf::Path::weights)require(n.mesh && n.mesh->primitives_count && tr.width==n.mesh->primitives[0].targets_count,"animation weights");
                else require(tr.width==(tr.path==gltf::Path::rotation?4u:3u),"channel width");
                clip.duration=(std::max)(clip.duration,tr.times.back());
                clip.tracks.push_back(std::move(tr));
            }
        }
        return clips;
    }
    ScenePose(const cgltf_data& d,const Clips& clips,const std::string& clip,float seconds,const SceneForm& form):inverseTranslations(form.inverseTranslations){
        std::vector<cgltf_node> nodes(d.nodes,d.nodes+d.nodes_count);
        weights.resize(nodes.size());world.resize(nodes.size());
        for(size_t i=0;i<nodes.size();++i){
            const auto& n=nodes[i];
            if(n.weights_count)weights[i].assign(n.weights,n.weights+n.weights_count);
            else if(n.mesh && n.mesh->weights_count)weights[i].assign(n.mesh->weights,n.mesh->weights+n.mesh->weights_count);
        }
        if(!clip.empty()){
            const auto it=clips.find(lower(clip));require(it!=clips.end(),"missing animation "+clip);
            for(const auto& tr:it->second.tracks){
                auto& n=nodes[tr.node];
                if(tr.path==gltf::Path::weights){
                    const auto span=gltf::interval(tr,seconds);auto& w=weights[tr.node];w.resize(tr.width);
                    for(unsigned k=0;k<tr.width;++k)w[k]=gltf::component(tr,span,k);
                }else{
                    auto* dst=tr.path==gltf::Path::rotation?n.rotation:tr.path==gltf::Path::scale?n.scale:n.translation;
                    const auto v=gltf::sample(tr,seconds);std::copy_n(v.begin(),tr.width,dst);
                }
            }
        }
        for(size_t i=0;i<nodes.size();++i){
            for(unsigned k=0;k<3;++k)nodes[i].translation[k]+=form.translations[i][k];
            if(form.weights[i].empty())continue;
            auto& w=weights[i];w.resize(nodes[i].mesh->primitives[0].targets_count);
            const auto animated=w;
            for(const auto& binding:form.weights[i])
                w[binding.index]+=binding.value*(binding.driver<0?1.f:animated[binding.driver]);
        }
        std::vector<bool> done(nodes.size());
        std::function<void(size_t)> visit=[&](size_t i){
            if(done[i])return;
            Matrix m;cgltf_node_transform_local(&nodes[i],m.data());
            if(auto* p=nodes[i].parent){size_t j=size_t(p-d.nodes);visit(j);world[i]=product(world[j],m);}
            else world[i]=m;
            done[i]=true;
        };
        for(size_t i=0;i<nodes.size();++i)visit(i);
    }
    std::vector<Matrix> palette(const cgltf_data& d,const cgltf_skin& s) const {
        auto inverse=values(s.inverse_bind_matrices,16);
        require(!s.inverse_bind_matrices || inverse.size()==s.joints_count*16,"inverse bind count");
        std::vector<Matrix> result;
        for(size_t i=0;i<s.joints_count;++i){
            Matrix m=identity();if(!inverse.empty())std::copy_n(inverse.data()+i*16,16,m.begin());
            const auto j=size_t(s.joints[i]-d.nodes);
            for(unsigned k=0;k<3;++k)m[12+k]+=inverseTranslations[j][k];
            result.push_back(product(world.at(j),m));
        }
        return result;
    }
};
