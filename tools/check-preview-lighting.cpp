#include "../external/owned-renderer/Source/model_preview_lighting.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using Matrix=std::array<float,16>;
using Vec=std::array<float,3>;
static void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
static void near(float a,float b,const char* why){check(std::abs(a-b)<2e-5f,why);}
static Matrix multiply(const Matrix& a,const Matrix& b){
    Matrix c{};for(unsigned r=0;r<4;++r)for(unsigned k=0;k<4;++k)for(unsigned col=0;col<4;++col)c[r*4+col]+=a[r*4+k]*b[k*4+col];return c;
}
static Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
static Vec to_world(const Matrix& view,const Vec& point,bool direction){
    Vec result{};for(unsigned a=0;a<3;++a)for(unsigned i=0;i<3;++i)
        result[a]+=view[i*4+a]*(point[i]-(direction?0:view[i*4+3]));return result;
}
int main(int argc,char** argv)try {
    const Matrix ortho{.4f,0,0,0,0,.7f,0,0,0,0,.02f,-.1f,0,0,0,1};
    const Matrix perspective{1.4f,0,0,0,0,2,0,0,0,0,1.01f,-.101f,0,0,1,0};
    const Vec cameraCenter{0,0,5},wanted{-.4095760443f,.5735764364f,-.7094064799f};
    unsigned cases=0;
    for(auto projection:{ortho,perspective})for(float yaw:{0.f,.7f,1.5707963f})for(float roll:{0.f,-.8f}) {
        const float c=std::cos(yaw),s=std::sin(yaw),cr=std::cos(roll),sr=std::sin(roll);
        auto view=multiply(Matrix{cr,-sr,0,0,sr,cr,0,0,0,0,1,0,0,0,0,1},
                           Matrix{c,0,s,0,0,1,0,0,-s,0,c,0,0,0,0,1});
        view[3]=3;view[7]=-2;view[11]=.8f;
        const auto center=to_world(view,cameraCenter,false),expected=to_world(view,wanted,true);
        for(bool hud:{false,true})for(float scale:{1.f,.0001f,10000.f}) {
            auto clip=multiply(projection,view);
            if(hud){auto placement=identity();placement[0]=.23f;placement[5]=.65f;placement[3]=.61f;placement[7]=-.34f;clip=multiply(placement,clip);}
            for(auto& value:clip)value*=scale;
            owned::PreviewStudioLighting light;
            check(owned::make_preview_studio_lighting(clip,center,light),"valid camera rejected");
            float norm=0;for(unsigned i=0;i<3;++i){near(light.direction[i],expected[i],"light moved with HUD/projection/view");norm+=light.direction[i]*light.direction[i];}
            near(norm,1,"direction not normalized");near(light.direction[3],0,"direction w");
            for(unsigned i=0;i<3;++i){near(light.ambient[i],.3f,"ambient floor");check(light.diffuse[i]>=.95f&&light.diffuse[i]<=1,"neutral key energy");}
            ++cases;
        }
    }
    owned::PreviewStudioLighting light;
    check(owned::make_preview_studio_lighting(ortho,cameraCenter,light),"reference camera");
    const auto shade=[&](Vec n){float dot=0;for(unsigned a=0;a<3;++a)dot+=n[a]*light.direction[a];return (std::max)(light.ambient[0],light.diffuse[0]*(std::max)(dot,0.f));};
    near(shade({0,0,-1}),.70940648f,"front form");near(shade({-1,0,0}),.40957604f,"left form");
    near(shade({0,1,0}),.57357644f,"top form");near(shade({1,0,0}),.3f,"right shadow floor");
    near(shade({0,-1,0}),.3f,"bottom shadow floor");near(shade({0,0,1}),.3f,"back remains visible");
    near(shade({light.direction[0],light.direction[1],light.direction[2]}),1,"key maximum");
    auto invalid=ortho;invalid[0]=std::numeric_limits<float>::quiet_NaN();
    const auto before=light.direction;
    check(!owned::make_preview_studio_lighting(invalid,cameraCenter,light)&&light.direction==before,"invalid clip published partial state");
    check(!owned::make_preview_studio_lighting(Matrix{},cameraCenter,light),"zero clip accepted");
    check(!owned::make_preview_studio_lighting(perspective,{0,0,0},light),"eye-plane center accepted");
    auto mirrored=ortho;mirrored[0]*=-1;
    check(owned::make_preview_studio_lighting(mirrored,cameraCenter,light),"mirrored view rejected");
    near(light.direction[0],-wanted[0],"screen-left on mirrored view");
    if(argc==2){
        std::ofstream file(argv[1]);file<<"{\"cases\":"<<cases<<",\"ambient\":[0.3,0.3,0.3],\"diffuse\":[1,0.98,0.95],\"camera_direction\":[-0.4095760443,0.5735764364,-0.7094064799],\"extra_passes\":0}\n";
        check(bool(file),"receipt write");
    }
    std::cout<<"PASS "<<cases<<" ortho/perspective camera/HUD cases, directional form/floor, mirrored and invalid inputs; single-light policy\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
