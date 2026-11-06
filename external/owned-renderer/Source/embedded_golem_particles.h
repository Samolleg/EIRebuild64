#pragma once
#include "embedded_arena_particles.h"
namespace owned {
// Plugin 421604cc78b4 final_golem_particles.h, through native particle storage.
struct GolemWallEmitter {float ground=0;int side=0,lease_tick=-100000;std::uint32_t random=0xf6101357u;};
inline bool golem_wall_create(GolemWallEmitter& p,ArenaWaveParticle& s,int tick) {
 if(tick<p.lease_tick||tick-p.lease_tick>3||p.side<0||p.side>3)return false;
 s={};auto random=[&](){return arena_wave_random(p.random);};const int side=p.side;
 const float t=random(),across=(random()-.5f)*.28f;
 s.x=side==0||side==2?28+72*t:side==1?100+across:28+across;
 s.y=side==1||side==3?26+36*t:side==0?26+across:62+across;
 const bool ember=random()<.28f;s.special=0xf610;s.reserved.z=ember?1.f:0.f;
 s.size=ember?.035f+random()*.045f:.18f+random()*.28f;s.z=p.ground+random()*(ember?3.7f:.65f);
 s.life=ember?16+int(random()*18):12+int(random()*11);s.reserved2={s.size,float(s.life),0};
 s.reserved.x=random()*6.2831853f;s.speed={(random()-.5f)*.018f,(random()-.5f)*.018f,ember?.065f:.045f};
 s.phase=0;s.phaseTime=1;s.color=ember?0xffffb354:0xe8ffffff;
 s.px=s.x;s.py=s.y;s.pz=s.z;s.psize=s.size;s.previousPhase=s.phase;s.previousColor=s.color;return true;
}
inline bool golem_wall_age(ArenaWaveParticle& s) {
 if(s.special!=0xf610||--s.life<=0)return false;
 const float age=s.reserved2.y-s.life,t=age/s.reserved2.y;
 s.x+=s.speed.x+std::sin(s.reserved.x+age*.45f)*.009f;s.y+=s.speed.y;s.z+=s.speed.z;
 const bool ember=s.reserved.z!=0;s.phase=ember?0:(std::min)(7,int(t*8));
 s.size=s.reserved2.x*(1-t*.65f);s.color=(unsigned(230*(1-t))<<24)|(ember?0xffa52e:0xffffff);return true;
}
}
