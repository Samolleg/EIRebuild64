void check_breast_physics(bool missing=false) {
    using namespace owned::gltf;
    InstallCharacterPhysics();check(secondary_factory!=nullptr,"Nala worker factory");
    SecondaryTargets rest;
    for(unsigned side=0;side<2;++side){
        const float x=side?-.1f:.1f;
        for(unsigned k=0;k<4;++k){
            float y=k==3?0.f:.05f*k,z=k==3?1.f:1.4f;
            rest.body[side*4+k]={{x,y,z},{x,y+(k==3?-.07f:.05f),z},{1,0,0}};
        }
    }
    rest.has_hair=true;
    for(unsigned i=0;i<rest.hair.size();++i)rest.hair[i]={0,-.15f,1.7f-.08f*(i%hair_points)};
    auto sim=CreateCharacterPhysics();
    sim->advance(0,rest,true);
    if(missing){check(!sim->error().empty(),"Missing GPU reported");return;}
    auto test=[&](bool ok,const char* label){if(!sim->error().empty())throw std::runtime_error(sim->error());check(ok,label);};
// Relative displacement of a marker on the synthetic Y-directed segment.
    // The glute uses rotation about a pinned pivot; measuring shift alone misses it.
    auto excursion=[&](const SecondaryPose& p,unsigned i){
        const auto& d=p.body[i];const auto& q=d.rotation;
        const float arm=rest.body[i].end[1]-rest.body[i].pivot[1];
        float x=d.shift[0]+2*arm*(q[0]*q[1]-q[2]*q[3]);
        float y=d.shift[1]-2*arm*(q[0]*q[0]+q[2]*q[2]);
        float z=d.shift[2]+2*arm*(q[1]*q[2]+q[0]*q[3]);
        return std::sqrt(x*x+y*y+z*z);
    };
    // Begin at rest, breathe for two cycles, then run and stop in the same simulation.
    float peak[3][2]{};
    for(unsigned phase=0;phase<3;++phase){
        const unsigned frames=phase==0?240:60;
        for(unsigned f=0;f<frames;++f){
            auto pose=rest;
            const float t=f/30.f;
            const float dz=phase==0?.004f*std::sin(t*1.5707963f):
                           phase==1?.045f*std::sin(t*15.707963f):0;
            for(auto& s:pose.body){s.pivot[2]+=dz;s.end[2]+=dz;}
            const auto value=sim->advance((phase==0?0.:phase==1?8.:10.)+t,pose,phase==0&&f==0);
            if(!sim->error().empty())throw std::runtime_error(sim->error());
            if(phase==2&&t<.30f)continue;
            for(unsigned part=0;part<2;++part)for(unsigned side=0;side<2;++side)
                peak[phase][part]=(std::max)(peak[phase][part],excursion(value,4*side+2+part));
        }
    }
    for(unsigned phase=0;phase<3;++phase)
        std::cout<<"NALA response "<<(phase==0?"breathing":phase==1?"running":"settled")
            <<" breast_mm="<<1000*peak[phase][0]<<" glute_mm="<<1000*peak[phase][1]<<std::endl;
    test(peak[0][0]<.001f&&peak[0][1]<.0006f,"Breathing follows authored support");
    test(peak[1][0]>.002f&&peak[1][0]<.016f,"Bounded breast inertia on footfalls");
    test(peak[1][1]>.0005f&&peak[1][1]<.0041f,"Small glute response on footfalls");
    test(peak[2][0]<.0005f&&peak[2][1]<.0003f,"Both regions settle after stopping");
    sim->advance(0,rest,true);
    float swing=0,glute=0,hair=0;
    double clock=0;
    SecondaryPose sample;auto input=rest;
    for(unsigned frame=1;frame<=240;++frame){
        clock=frame*secondary_step;input=rest;
        float x=.04f*std::sin(float(clock)*10);
        for(unsigned i=0;i<4;++i){input.body[i].pivot[2]+=x;input.body[i].end[2]+=x;}
        for(auto& p:input.hair)p[0]+=x;
        sample=sim->advance(clock,input,false);
        test(sample.hair[0]==SecondaryPoint{},"Scalp pinned");
        swing=(std::max)(swing,std::abs(sample.body[2].shift[2]-sample.body[6].shift[2]));
        glute=(std::max)(glute,std::abs(sample.body[3].rotation[0]-sample.body[7].rotation[0]));
        hair=(std::max)(hair,std::abs(sample.hair[4][0]));
        const auto frozen=sim->advance(clock,input,false);
        test(frozen.body[2].rotation==sample.body[2].rotation&&frozen.hair==sample.hair,"Duplicate samples freeze body and hair");
    }
    test(swing>.001f&&swing<.20f,"Independent breast chains");
    test(glute>.001f&&glute<1,"Glute rotates under acceleration");
    test(hair>.001f&&hair<=secondary_limit,"Hair lags its anchor");
    auto reset=sim->advance(0,rest,false);
    test(reset.hair[4]==SecondaryPoint{}&&reset.body[2].shift==SecondaryPoint{},"Rewind clears dynamics");
    auto worker=secondary_factory();
    worker->advance(0,rest,true);
    for(unsigned i=1;i<300&&!worker->ready();++i){
        worker->advance(i/120.,rest,false);std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    test(worker->ready()&&worker->error().empty(),"Asynchronous solver publishes poses");
    std::cout<<"NALA differential breast_m="<<swing<<" glute_quat_x="<<glute<<" hair_m="<<hair<<"\n";
}
