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
    const unsigned clip=named_clip(*body->animation,"run");require(clip!=UINT32_MAX,"Nala run clip");
    Affine root{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    float peak=0;unsigned moved=0;
    std::ofstream trajectory(out/L"nala-physics.csv");trajectory<<"frame,node,x,y,z\n";
    for(unsigned frame=0;frame<90;++frame) {
        const float clock=frame/60.f;root[3]=clock*1.5f;
        Playback pose{clip,UINT32_MAX,std::fmod(clock,body->animation->clips[clip].duration),0,1};
        state.update(clock,pose,root,false,0);
        require(state.physics&&state.physics->error().empty()&&state.overrides,"Nala live PhysX pose");
        const auto original=evaluate(*body->animation,pose,multiply(root,Affine{1,0,0,0,0,0,-1,0,0,1,0,0,0,0,0,1}));
        auto corrected=original.joints;
        for(const auto& o:*state.overrides) {
            float squared=0;
            for(unsigned r=0;r<3;++r) {
                const float delta=o.value.rows[r][3]-original.joints[o.node][r*4+3];squared+=delta*delta;
                for(unsigned c=0;c<3;++c)require(std::abs(o.value.rows[r][c]-original.joints[o.node][r*4+c])<5e-5f,"Nala original linear transform retained");
                std::copy_n(o.value.rows[r].begin(),4,corrected[o.node].begin()+r*4);
            }
            const float distance=std::sqrt(squared);peak=(std::max)(peak,distance);
            require(distance<=secondary_limit+1e-4f,"Nala bounded pose displacement");
            if(frame%5==0)trajectory<<frame<<','<<o.node<<','<<o.value.rows[0][3]<<','<<o.value.rows[1][3]<<','<<o.value.rows[2][3]<<'\n';
        }
        // Existing weights must deform a localized subset, leaving the rest exact.
        if(frame==30)for(const auto& v:body->rest_vertices) {
            double delta[3]{};
            for(unsigned k=0;k<4;++k)if(v.weights[k]) {
                const auto j=v.joints[k];const double w=v.weights[k]/65535.;
                for(unsigned r=0;r<3;++r)delta[r]+=(corrected[j][r*4+3]-original.joints[j][r*4+3])*w;
            }
            const double distance=std::sqrt(delta[0]*delta[0]+delta[1]*delta[1]+delta[2]*delta[2]);
            require(distance<=secondary_limit+1e-4f,"Nala bounded skinned surface");if(distance>1e-5)++moved;
        }
    }
    require(peak>.001f&&moved>100&&moved<body->rest_vertices.size()/2,"Nala existing weights produce localized motion");
    auto previous=state.overrides;state.update(state.clock,state.playback,state.placement,false,0);
    require(state.overrides==previous,"Nala repeated render reuses immutable pose");
    const auto before=state.physics_clock;
    state.update(state.clock+1,state.playback,state.placement,false,0,true);
    require(state.physics_clock==before,"Nala host combat pause freezes physics time");
    state.update(state.clock+1,state.playback,state.placement,false,0,false);
    require(state.physics_clock==before,"Nala resume excludes paused interval");
    std::cout<<"PASS Nala installed GLB: affected_nodes="<<state.affected.size()<<" moved_vertices="<<moved
        <<" total_vertices="<<body->rest_vertices.size()<<" peak_m="<<peak<<" pause/resume/reuse\n";
}
