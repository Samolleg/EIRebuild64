// Real installed GLB, production animation sampler and production PhysX adapter.
static void check_nala_physics(const fs::path& game,const fs::path& out) {
    using namespace owned::gltf;
    const auto read=[](const fs::path& path) {
        std::ifstream f(path,std::ios::binary|std::ios::ate);require(bool(f),"Nala input");
        const auto size=f.tellg();auto b=std::make_shared<std::vector<std::uint8_t>>(std::size_t(size));
        f.seekg(0);require(bool(f.read(reinterpret_cast<char*>(b->data()),size)),"Nala bytes");
        owned::ResourceBlob blob;blob.bytes=b;return blob;
    };
    const auto maps=game/L"MODS/Dejavu/Maps";
    auto body=decode(read(maps/L"models/characters/nala.glb"),read(maps/L"profiles/human.v1.json"),
        [](const std::string&,const void* p,std::uint32_t n){owned::ResourceBlob b;
            const auto* first=static_cast<const std::uint8_t*>(p);
            b.bytes=std::make_shared<std::vector<std::uint8_t>>(first,first+n);return b;});
    PhysicsFx::InstallCharacterPhysics();BreastState state;state.physics=PhysicsFx::CreateCharacterPhysics();require(state.bind(body),"Nala existing physics branches");
    Affine root{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    unsigned moved=0;
    float peak=0;
    for(const char* action:{"idle","cidle09","cidle10","cidle11","walk","run"}) {
        const bool locomotion=std::string(action)=="walk"||std::string(action)=="run";
        const unsigned clip=named_clip(*body->animation,action);
        require(clip!=UINT32_MAX,"Nala locomotion clip");
        float low=1,high=-1,breastPeak=0,glutePeak=0;
        const float duration=body->animation->clips[clip].duration;
        const unsigned frames=unsigned(std::ceil(duration*60));
        for(unsigned frame=0;frame<=frames;++frame) {
            const double clock=duration*2.*frame/frames;
            Playback pose{clip,UINT32_MAX,float(std::fmod(clock,duration)),0,1};
            state.update(clock,pose,root,frame==0,0,false,nullptr,nullptr,!locomotion);
            require(state.physics&&state.physics->error().empty()&&state.overrides,"live GPU pose");
            const auto original=evaluate(*body->animation,pose);
            auto corrected=original.joints;
            for(const auto& o:*state.overrides) {
                for(unsigned r=0;r<3;++r)
                    std::copy_n(o.value.rows[r].begin(),4,corrected[o.node].begin()+r*4);
            }
            const auto tip=state.targets[0];
            const float dz=corrected[tip][11]-original.joints[tip][11];
            for(unsigned side=0;side<2;++side) {
                const unsigned tipNode=state.targets[side], glute=4*side+3;
                const auto& marker=original.joints[state.ends[glute]];
                const SecondaryPoint rest{marker[3],marker[7],marker[11]};
                const auto pivot=state.starts[glute];
                const auto displaced=mapped_point(multiply(corrected[pivot],physique_inverse(original.joints[pivot])),rest);
                float breast2=0,glute2=0;
                for(unsigned r=0;r<3;++r){
                    const float delta=corrected[tipNode][4*r+3]-original.joints[tipNode][4*r+3];
                    breast2+=delta*delta;glute2+=(displaced[r]-rest[r])*(displaced[r]-rest[r]);
                }
                breastPeak=(std::max)(breastPeak,std::sqrt(breast2));
                glutePeak=(std::max)(glutePeak,std::sqrt(glute2));
                const auto attachment=state.attachments[side];
                const auto rib=state.ribs[side];
                const auto chest=multiply(corrected[rib],physique_inverse(original.joints[rib]));
                const auto expectedBase=multiply(chest,original.joints[attachment]);
                for(unsigned c=0;c<12;++c)require(std::abs(corrected[attachment][c]-expectedBase[c])<1e-4f,"breast remains attached to breathing ribs");
                auto previous=expectedBase;
                auto correctedPrevious=corrected[attachment];
                for(unsigned k=0;k<3;++k){
                    const unsigned n=state.ends[side*4+k];
                    auto breathingJoint=multiply(chest,original.joints[n]);
                    float before=0,after=0;
                    for(unsigned r=0;r<3;++r){
                        float d=breathingJoint[4*r+3]-previous[4*r+3];
                        float e=corrected[n][4*r+3]-correctedPrevious[4*r+3];
                        before+=d*d;after+=e*e;
                    }
                    require(after>=before*.899f*.899f&&after<=before*1.101f*1.101f,"Nala breast segment surface strain");
                    previous=breathingJoint;correctedPrevious=corrected[n];
                }
            }
            if(frame==0)require(std::abs(dz)<1e-5f,"reset starts at authored pose");
            if(frame>frames/2){low=(std::min)(low,dz);high=(std::max)(high,dz);}
            moved=0;
            for(const auto& v:body->rest_vertices) {
                std::array<double,3> delta{};
                for(unsigned k=0;k<4;++k)if(v.weights[k]) {
                    const auto j=v.joints[k];
                    const auto local=owned::skn::point(body->geometry.joints[j].inverse,v.position);
                    for(unsigned r=0;r<3;++r) {
                        double d=corrected[j][r*4+3]-original.joints[j][r*4+3];
                        for(unsigned c=0;c<3;++c)d+=(corrected[j][r*4+c]-original.joints[j][r*4+c])*local[c];
                        delta[r]+=d*v.weights[k]/65535.;
                    }
                }
                const float d=float(std::sqrt(delta[0]*delta[0]+delta[1]*delta[1]+delta[2]*delta[2]));
                require(std::isfinite(d)&&d<secondary_limit+.01f,"unstable surface deformation");
                if(d>1e-5f)++moved;
                peak=(std::max)(peak,d);
            }
            require(moved<body->rest_vertices.size()/2,"localized weighted deformation");
        }
        std::cout<<"Nala "<<action<<" tip_dz_min_m="<<low<<" max_m="<<high<<" breast_peak_mm="<<1000*breastPeak<<" glute_peak_mm="<<1000*glutePeak<<" skin_peak_so_far_m="<<peak<<std::endl;
        require(breastPeak<.068f&&glutePeak<.067f,"Tissue travel on the actual rig");
        if(locomotion)require(high-low>.0015f&&breastPeak>.024f&&glutePeak>.006f,"Inertia during locomotion");
        else require(breastPeak>.009f&&breastPeak<.048f&&glutePeak<.012f,"Visible rib cage motion and quiet hips at rest");
    }
    auto previous=state.overrides;state.update(state.clock,state.playback,state.placement,false,0);
    require(state.overrides==previous,"Nala repeated render reuses immutable pose");
    const auto oldIndices=state.overrides;
    state.update(state.clock,state.playback,state.placement,false,17);
    require(state.overrides!=oldIndices,"Nala equipment palette base invalidates sparse cache");
    for(unsigned i=0;i<state.overrides->size();++i) {
        require((*state.overrides)[i].node==(*oldIndices)[i].node+17,"Nala equipment remaps sparse node index");
        for(unsigned r=0;r<3;++r)require((*state.overrides)[i].value.rows[r]==(*oldIndices)[i].value.rows[r],"Nala equipment preserves physical pose");
    }
    // Identical sampled pose, two UI arrangements. Late changes to scale,
    // rotation or centring must re-place every override even on the same tick.
    const auto metric=state.overrides;
    for(const auto& view:std::array<owned::RetainedVisualSample::PreviewPhysicsSpace,2>{{
        {root,{.25f,0,0,1,0,0,-.25f,2,0,.25f,0,6,0,0,0,1},"info"},
        {root,{0,-1.2f,0,-2,0,0,-1.2f,1,1.2f,0,0,6,0,0,0,1},"inventory"}}}) {
        state.update(state.clock,state.playback,root,false,17,false,&view);
        require(state.overrides!=metric,"UI placement cache must include this view");
        for(unsigned i=0;i<metric->size();++i) {
            Affine original{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
            for(unsigned row=0;row<3;++row)
                std::copy_n((*metric)[i].value.rows[row].begin(),4,original.begin()+4*row);
            const auto expected=multiply(view.to_display,original);
            for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col)
                require(std::abs((*state.overrides)[i].value.rows[row][col]-expected[row*4+col])<1e-5f,
                    "info/inventory corrected joints must follow their model placement");
        }
    }
    // Stop, pause mid-blend, and settle on the info-panel clock.
    state.update(state.clock+.03,state.playback,root,false,17,false,nullptr,nullptr,true);
    require(state.tissue_gain>BreastState::idle_gain&&state.tissue_gain<BreastState::active_gain,"smooth entry into rest");
    const float partialGain=state.tissue_gain;
    state.update(state.clock+.2,state.playback,root,false,17,true,nullptr,nullptr,true);
    require(state.tissue_gain==partialGain,"paused transition stays frozen");
    state.update(state.clock+.1,state.playback,root,false,17,false,nullptr,nullptr,true);
    for(unsigned f=0;f<20;++f)state.update(state.clock+1./30.,state.playback,root,false,17,false,nullptr,nullptr,true);
    require(std::abs(state.tissue_gain-BreastState::idle_gain)<.001f,"stop settles to idle response");
    const auto before=state.physics_clock;
    state.update(state.clock+1,state.playback,state.placement,false,0,true);
    require(state.physics_clock==before,"Nala host combat pause freezes physics time");
    state.update(state.clock+1,state.playback,state.placement,false,0,false);
    require(state.physics_clock==before,"Nala resume excludes paused interval");
    std::cout<<"PASS Nala installed GLB: affected_nodes="<<state.affected.size()<<" moved_vertices="<<moved
        <<" total_vertices="<<body->rest_vertices.size()<<" peak_m="<<peak<<" pause/resume/reuse\n";
}
