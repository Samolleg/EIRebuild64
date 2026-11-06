#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace owned {
// Dejavu 421604cc78b4 arena_particles.h. Instance-local values only;
// the engine owns allocation, pause, source lifetime, bounds and billboard draw.
struct ArenaWaveVector { float x=0,y=0,z=0; };
struct ArenaWaveEmitter {
    ArenaWaveVector origin{};
    float radius=-1;
    int kind=0,lease_tick=-100000;
    bool charge=false;
    std::uint32_t sequence=0,random=0x0a690427u;
};
struct ArenaWaveParticle {
    float x=0,y=0,z=0,size=0,px=0,py=0,pz=0,psize=0;
    ArenaWaveVector reserved{},reserved2{},speed{};
    int life=0,special=0,phase=0,phaseTime=0,previousPhase=0;
    std::uint32_t color=0,previousColor=0;
};
inline float arena_wave_random(std::uint32_t& state) noexcept {
    state=state*1664525u+1013904223u;return(state>>8)*(1.f/16777216.f);
}
inline int arena_wave_tries(const ArenaWaveEmitter& p) noexcept {
    return p.radius<0?0:p.charge?24:(std::min)(700,(std::max)(80,int(p.radius*16)));
}
inline int arena_wave_maximum(int kind) noexcept {return kind==2?3072:kind==1?1200:2400;}
inline bool arena_wave_create(ArenaWaveEmitter*p,ArenaWaveParticle*s,int tick){
 if((tick<p->lease_tick||tick-p->lease_tick>=4)||p->kind<1||p->kind>3)return 0;
 const int kind=(int)p->kind;const bool charge=p->charge!=0;
 float a=(p->sequence++*.61803398875f+arena_wave_random(p->random)*.11f)*6.28318530718f;
 // Fill the swept annulus, rather than transporting existing flame images.
 float r=p->radius,inner=(std::fmax)(0.f,r-3.333334f);
 if(charge){r=1.45f+arena_wave_random(p->random)*.35f;inner=r;}
 r=std::sqrt(inner*inner+arena_wave_random(p->random)*(r*r-inner*inner));
 float x=p->origin.x+std::cos(a)*r,y=p->origin.y+std::sin(a)*r;
 if(x<.25f||y<.25f||x>127.75f||y>127.75f)return 0;
 std::memset(s,0,sizeof(*s));s->special=0xa690+kind;s->x=x;s->y=y;
 s->life=kind==1?3+(int)(arena_wave_random(p->random)*5):8+(int)(arena_wave_random(p->random)*6);
 s->reserved2.y=(float)s->life;s->phaseTime=1;
 float side=(arena_wave_random(p->random)-.5f)*.035f;
 s->speed={std::cos(a)*.025f-std::sin(a)*side,std::sin(a)*.025f+std::cos(a)*side,0};
 if(kind==2){
  bool ember=arena_wave_random(p->random)<.14f;s->reserved.z=ember?1.f:0.f;
  s->size=ember?.035f+arena_wave_random(p->random)*.035f:.22f+arena_wave_random(p->random)*.33f;
  s->z=p->origin.z+.10f+s->size*.3f+arena_wave_random(p->random)*.08f;
  s->speed.z=ember?.04f+arena_wave_random(p->random)*.025f:.020f+arena_wave_random(p->random)*.024f;
  s->phase=(int)(arena_wave_random(p->random)*2);s->color=ember?0xffffb04a:0xe8ffffff;
 }else if(kind==1){
  s->size=.035f+arena_wave_random(p->random)*.10f;s->z=p->origin.z+.9f+arena_wave_random(p->random)*.48f;
  s->speed.z=(arena_wave_random(p->random)-.5f)*.06f;s->phase=(int)(arena_wave_random(p->random)*4);s->color=0xff8dcfff;
 }else{
  s->size=.18f+arena_wave_random(p->random)*.35f;s->z=p->origin.z+.12f+arena_wave_random(p->random)*.18f;
  s->speed.z=.006f+arena_wave_random(p->random)*.017f;s->phase=8+(int)(arena_wave_random(p->random)*3);s->color=0x799bceef;
 }
 if(charge){s->speed.x=-std::cos(a)*.075f;s->speed.y=-std::sin(a)*.075f;}
 s->reserved2.x=s->size;s->reserved.x=arena_wave_random(p->random)*6.2831853f;
 s->px=s->x;s->py=s->y;s->pz=s->z;s->psize=s->size;
 s->previousPhase=s->phase;s->previousColor=s->color;return 1;
}
inline bool arena_wave_age(ArenaWaveParticle*s){
 if(s->special<0xa691||s->special>0xa693||--s->life<=0)return 0;
 int kind=s->special-0xa690;float age=s->reserved2.y-s->life,t=age/s->reserved2.y;
 // Independent rising/turbulent particles stay near their birth location.
 s->x+=s->speed.x+std::sin(s->reserved.x+age*.9f)*.012f;
 s->y+=s->speed.y+std::cos(s->reserved.x+age*.7f)*.012f;
 s->z+=s->speed.z;s->speed.x*=.92f;s->speed.y*=.92f;s->speed.z*=.97f;
 if(kind==2){
  if(s->reserved.z!=0){s->phase=0;s->size=s->reserved2.x*(1.f-t*.7f);s->color=((unsigned)(255*(1-t))<<24)|0xffb04a;}
  else{s->phase=(std::min)(7,(int)(t*8));s->size=s->reserved2.x*(1.f+.3f*std::sin(t*3.14159265f)-.35f*t);s->color=((unsigned)(230*(1-t)*(1-t))<<24)|0xffffff;}
 }else if(kind==1){s->phase=(std::min)(14,(int)(t*15));s->size=s->reserved2.x*(1.f-t*.65f);s->color=((unsigned)(255*(1-t))<<24)|0x8dcfff;}
 else{s->phase=(std::min)(14,8+(int)(t*7));s->size=s->reserved2.x*(1+t*.65f);s->color=((unsigned)(110*std::sin(t*3.14159265f))<<24)|0x9bceef;}
 return 1;
}
} // namespace owned
