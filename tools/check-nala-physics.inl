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
    PhysicsFx::InstallCharacterPhysics();BreastState state;require(state.bind(body),"Nala existing physics branches");
    Affine root{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    unsigned moved=0;
    float peak=0;
    for(const char* action:{"idle","walk","run"}) {
        const unsigned clip=named_clip(*body->animation,action);
        require(clip!=UINT32_MAX,"Nala locomotion clip");
        float low=1,high=-1;
        const float duration=body->animation->clips[clip].duration;
        for(unsigned frame=0;frame<=90;++frame) {
            const double clock=duration*2.*frame/90.;
            Playback pose{clip,UINT32_MAX,float(std::fmod(clock,duration)),0,1};
            state.update(clock,pose,root,frame==0,0);
            require(state.physics&&state.physics->error().empty()&&state.overrides,"live GPU pose");
            const auto original=evaluate(*body->animation,pose);
            auto corrected=original.joints;
            for(const auto& o:*state.overrides) {
                require(o.node!=state.attachments[0]&&o.node!=state.attachments[1],"attachment bone moved");
                for(unsigned r=0;r<3;++r)
                    std::copy_n(o.value.rows[r].begin(),4,corrected[o.node].begin()+r*4);
            }
            const auto tip=state.targets[0];
            const float dz=corrected[tip][11]-original.joints[tip][11];
            if(frame==0)require(dz<-.015f&&dz>-.06f,"front must droop in standing pose on first draw");
            if(frame>45){low=(std::min)(low,dz);high=(std::max)(high,dz);}
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
                require(std::isfinite(d)&&d<.16f,"unstable surface deformation");
                if(d>1e-5f)++moved;
                peak=(std::max)(peak,d);
            }
            require(moved>100&&moved<body->rest_vertices.size()/2,"localized weighted deformation");
        }
        std::cout<<"Nala "<<action<<" tip_dz_min_m="<<low<<" max_m="<<high<<" skin_peak_so_far_m="<<peak<<std::endl;
        if(std::string(action)!="idle")require(high-low>.005f,"relative swing during steady locomotion");
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
    const auto before=state.physics_clock;
    state.update(state.clock+1,state.playback,state.placement,false,0,true);
    require(state.physics_clock==before,"Nala host combat pause freezes physics time");
    state.update(state.clock+1,state.playback,state.placement,false,0,false);
    require(state.physics_clock==before,"Nala resume excludes paused interval");
    std::cout<<"PASS Nala installed GLB: affected_nodes="<<state.affected.size()<<" moved_vertices="<<moved
        <<" total_vertices="<<body->rest_vertices.size()<<" peak_m="<<peak<<" pause/resume/reuse\n";
}
