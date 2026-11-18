void check_breast_physics(bool missing=false) {
    using namespace owned::gltf;
    InstallCharacterPhysics();check(secondary_factory!=nullptr,"breast PhysX factory registered");
    auto sim=secondary_factory();SecondaryPoints targets{{{-.1f,0,1.4f},{.1f,0,1.4f}}};
    auto offsets=sim->advance(0,targets,false);
    if(missing) {check(!sim->error().empty()&&offsets==SecondaryPoints{},"breast missing GPU reports error without CPU fallback");return;}
    if(!sim->error().empty())throw std::runtime_error(sim->error());
    check(offsets==SecondaryPoints{},"breast starts at authored pose");
    const auto magnitude=[](const auto& p){return std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);};
    float peak=0;double clock=0;
    for(unsigned frame=1;frame<=120;++frame) {
        clock=frame/120.;
        // Only the left attachment moves: detects cross-side coupling.
        targets[0][2]=1.4f+.04f*std::sin(float(clock*6.283185*3));
        offsets=sim->advance(clock,targets,false);
        if(!sim->error().empty())throw std::runtime_error(sim->error());
        peak=(std::max)(peak,magnitude(offsets[0]));
        if(magnitude(offsets[0])>secondary_limit+1e-6f || magnitude(offsets[1])>1e-5f)
            throw std::runtime_error("breast bounded independent sides");
        const auto paused=sim->advance(clock,targets,false);
        if(paused!=offsets)throw std::runtime_error("breast duplicate/pause stepped simulation");
    }
    check(peak>.003f&&peak<=secondary_limit+1e-6f,"breast GPU inertia with bounded independent sides");
    check(sim->advance(clock,targets,false)==offsets,"breast pause and repeated passes exact");
    for(unsigned frame=0;frame<360;++frame)offsets=sim->advance(clock+=(1./120.),targets,false);
    check(magnitude(offsets[0])<.0001f,"breast spring settles without drift");
    targets[0][0]+=20;targets[1][0]+=20;
    check(sim->advance(clock+.01,targets,false)==SecondaryPoints{},"breast teleport resets momentum");
    check(sim->advance(0,targets,false)==SecondaryPoints{},"breast rewind resets momentum");
    targets[0][2]+=.05f;
    check(sim->advance(.1,targets,true)==SecondaryPoints{},"breast explicit seek resets momentum");
    check(sim->advance(4,targets,false)==SecondaryPoints{},"breast visibility time gap resets momentum");
    sim.reset();sim=secondary_factory();
    check(sim->advance(0,targets,false)==SecondaryPoints{}&&sim->error().empty(),"breast actor lifetime recreates GPU scene");
    std::cout<<"BREAST peak_m="<<peak<<" GPU spring and reset checks passed\n";
}
