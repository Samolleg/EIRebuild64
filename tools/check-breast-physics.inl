void check_breast_physics(bool missing=false) {
    using namespace owned::gltf;
    InstallCharacterPhysics();check(secondary_factory!=nullptr,"breast PhysX factory registered");
    auto sim=CreateCharacterPhysics();SecondaryPoints targets{{{-.1f,0,1.4f},{.1f,0,1.4f}}};
    auto offsets=sim->advance(0,targets,false);
    if(missing) {check(!sim->error().empty()&&offsets==SecondaryPoints{},"breast missing GPU reports error without CPU fallback");return;}
    if(!sim->error().empty())throw std::runtime_error(sim->error());
    const auto rest=offsets;
    check(rest[0]==rest[1]&&rest[0][2]<-.035f&&rest[0][2]>-.05f,"start at gravity-loaded equilibrium");
    const auto magnitude=[](const auto& p){return std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);};
    float peak=0;double clock=0;
    for(unsigned frame=1;frame<=120;++frame) {
        clock=frame/120.;
        // Only the left attachment moves: detects cross-side coupling.
        targets[0][2]=1.4f+.04f*std::sin(float(clock*6.283185*3));
        offsets=sim->advance(clock,targets,false);
        if(!sim->error().empty())throw std::runtime_error(sim->error());
        peak=(std::max)(peak,magnitude(offsets[0]));
        if(magnitude(offsets[0])>secondary_limit+1e-6f || std::abs(offsets[1][2]-rest[1][2])>.009f || std::abs(offsets[1][0])+std::abs(offsets[1][1])>.001f)
            {std::cerr<<"frame "<<frame<<" rest "<<rest[1][2]<<" measured "<<offsets[1][2]<<" peak "<<peak<<std::endl;throw std::runtime_error("breast bounded independent sides");}
        const auto paused=sim->advance(clock,targets,false);
        if(paused!=offsets)throw std::runtime_error("breast duplicate/pause stepped simulation");
    }
    check(peak>std::abs(rest[0][2])+.012f&&peak<=secondary_limit+1e-6f,"breast GPU inertia with bounded independent sides");
    check(sim->advance(clock,targets,false)==offsets,"breast pause and repeated passes exact");
    for(unsigned frame=0;frame<360;++frame)offsets=sim->advance(clock+=(1./120.),targets,false);
    check(std::abs(offsets[0][2]-rest[0][2])<.009f,"breast spring settles without drift");
    targets[0][0]+=20;targets[1][0]+=20;
    check(sim->advance(clock+.01,targets,false)==rest,"breast teleport resets momentum");
    check(sim->advance(0,targets,false)==rest,"breast rewind resets momentum");
    targets[0][2]+=.05f;
    check(sim->advance(.1,targets,true)==rest,"breast explicit seek resets momentum");
    check(sim->advance(4,targets,false)==rest,"breast visibility time gap resets momentum");
    sim.reset();sim=CreateCharacterPhysics();
    check(sim->advance(0,targets,false)==rest&&sim->error().empty(),"breast actor lifetime recreates GPU scene");
    auto worker=secondary_factory();
    check(worker->advance(0,targets,true)==rest,"async first pose preserves gravity-loaded equilibrium");
    float asyncPeak=0;double lastSample=0;
    for(unsigned frame=0;frame<360;++frame) {
        targets[0][2]=1.4f+.04f*std::sin(float(frame)*.16f);
        lastSample=frame/120.;
        const auto visible=worker->advance(lastSample,targets,false);
        check(visible==worker->advance(frame/120.,targets,false),"async duplicate sample is stable");
        asyncPeak=(std::max)(asyncPeak,magnitude(visible[0]));
        if(!worker->error().empty())throw std::runtime_error(worker->error());
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if(worker->ready()&&asyncPeak>std::abs(rest[0][2])+.003f)break;
    }
    check(worker->ready()&&asyncPeak>std::abs(rest[0][2])+.003f&&asyncPeak<=secondary_limit+1e-6f,"asynchronous GPU pose reaches renderer");
    targets[0][0]+=2;targets[1][0]+=2;
    check(worker->advance(lastSample,targets,false)==rest,"paused teleport drops previous async result");
    check(worker->advance(0,targets,true)==rest,"async stale result suppressed after reset");
    std::cout<<"BREAST peak_m="<<peak<<" GPU spring and reset checks passed\n";
}
