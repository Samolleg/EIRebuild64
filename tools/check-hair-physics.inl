void check_hair_physics() {
    using namespace owned::gltf;
    InstallCharacterPhysics();check(secondary_factory!=nullptr,"Nala worker factory");
    SecondaryTargets rest;
    for(unsigned i=0;i<rest.hair.size();++i)rest.hair[i]={0,-.15f,1.7f-.08f*(i%hair_points)};
    auto sim=CreateCharacterPhysics();
    sim->advance(0,rest,true);
    if(!sim->error().empty())throw std::runtime_error(sim->error());
    float hair=0;
    for(unsigned frame=1;frame<=120;++frame){
        auto targets=rest;
        for(auto& p:targets.hair)p[0]+=.04f*std::sin(float(frame)*.08f);
        const auto sample=sim->advance(frame*secondary_step,targets,false);
        check(sample.hair[0]==SecondaryPoint{},"Scalp pinned");
        hair=(std::max)(hair,std::abs(sample.hair[4][0]));
    }
    check(hair>.001f&&hair<=secondary_limit,"Hair lags the moving head");
    auto reset=sim->advance(0,rest,false);
    check(reset.hair[4]==SecondaryPoint{},"Rewind clears dynamics");
    auto worker=secondary_factory();
    worker->advance(0,rest,true);
    for(unsigned i=1;i<300&&!worker->ready();++i){
        worker->advance(i/120.,rest,false);std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    check(worker->ready()&&worker->error().empty(),"Asynchronous solver publishes poses");
    std::cout<<"NALA hair_m="<<hair<<std::endl;
}
