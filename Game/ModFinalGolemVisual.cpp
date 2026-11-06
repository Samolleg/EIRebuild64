#include "StdAfx.h"
#include "ModFinalGolem.h"
#include "ModFinalGolemState.h"
#include "Object3DClientSpecific.h"
#include "AnimationSystem2.h"
#include "WorldClient.h"
#include "Camera.h"
#include "Renderer.h"
#include "Console.h"
#include "dx11/RendererDX11.h"
#include "OwnedRender/E1Scene.h"
#include "OwnedRender/E1World.h"
#include "embedded_golem.h"

namespace ModFinalGolem {
namespace {
bool armed=false;
float Ground(float x,float y) {return theWorldServer.GetZValue(x,y);}
CPlot3D WorldPosition(CMapObject& object) {return object.GetCP();}
std::array<float,3> Point(CPlot3D p) {return {p.x,p.y,p.z};}
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Texture(CMapObject* object,bool secondary=false) {
 if(!object||object->IsInvalid()||!object->FinalDataLoading())return {};
 auto* specific=dynamic_cast<CObject3DClientSpecific*>(object->GetObjectSpecific());
 auto* texture=specific?(secondary?specific->GetSecondaryTexture():specific->GetPrimaryTexture()):nullptr;
 auto* surface=texture?texture->GetSurface():nullptr;
 return surface?surface->GetShaderResource():Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>{};
}
}
void SavedPose(const char*,std::array<owned::EmbeddedGolemNode,30>&);
void ArmVisual() noexcept {armed=true;}
void ResetVisual() noexcept {armed=false;}
void Draw() {
 if(!armed)return;
 const bool active=Active(),walls=ModEncounter::Arena(theWorldServer)&&Read("EFC.Walls")==1;
 const float impact=Read("HG.ImpactBorn");const bool dust=ModEncounter::Hard(theWorldServer)&&impact>0&&Now()>=impact&&Now()-impact<=54;
 if(!active&&!walls&&Read("FGL.DeathBorn")<=0&&!dust){armed=false;return;}
 auto* boss=theWorld.GetUnit(990400);if(!boss||boss->IsInvalid())return;
 auto* dx=theRenderer.GetD3D();if(!dx||!dx->IsOwnedWorldActive())return;
 OwnedRender::E1WorldFrame world;if(!OwnedRender::E1ReadWorldFrame(world)||!world.camera.valid||!world.lightingValid)return;
 owned::EmbeddedGolemInput input;
 input.world_epoch=OwnedRender::E1Scene().Epoch();input.incarnation=boss->GetRTID();input.boss_id=990400;input.tick=Now();
 input.hard=ModEncounter::Hard(theWorldServer);input.death_born=Read("FGL.DeathBorn");input.detach_born=Read("FGL.DetachBorn");input.detach_from=int(Read("FGL.DetachFrom"));input.detach_to=int(Read("FGL.DetachTo"));
 input.death_base={Read("FGL.DeathX"),Read("FGL.DeathY"),Read("FGL.DeathZ")};
 struct PoseCache {std::uint64_t epoch=0;float born=-1;std::array<owned::EmbeddedGolemNode,30> nodes{};};
 static PoseCache deathCache,detachCache;
 auto saved=[&](const char* name,float born,PoseCache& cache){if(cache.epoch!=input.world_epoch||cache.born!=born){SavedPose(name,cache.nodes);cache.epoch=input.world_epoch;cache.born=born;}return cache.nodes;};
 if(input.death_born>0)input.death_pose=saved("Death",input.death_born,deathCache);
 if(input.detach_born>0&&Now()-input.detach_born<=126)input.detached_pose=saved("Detach",input.detach_born,detachCache);
 input.stage=int(Read("DJW.Stage"));input.phase=int(Read("FGL.Phase"));input.active=active;input.walls=walls;input.dead=boss->IsDead()!=FALSE;
 input.base=Point(WorldPosition(*boss));input.camera=Point(theCamera.GetCP());input.world_to_clip=world.camera.world_to_clip;input.ground=Ground;
 input.slam_born=Read("HG.SlamBorn");input.slam_x=Read("HG.SlamX");input.slam_y=Read("HG.SlamY");input.slam_angle=Read("HG.SlamAngle");input.grow_born=Read("HG.GrowBorn");input.obelisk_x=Read("HG.ObeliskX");input.obelisk_y=Read("HG.ObeliskY");input.pulse_born=Read("HG.PulseBorn");input.role=int(Read("HG.ObeliskRole"));input.impact_born=Read("HG.ImpactBorn");
 if(input.hard)for(int actor=0;actor<2;++actor){auto* unit=theWorld.GetUnit(int(Read(actor?"HG.ObeliskID":"HG.GuardID")));if(!unit||unit->IsInvalid()||!unit->FinalDataLoading())continue;auto* system=unit->GetAnimationSystem2();if(!system)continue;system->RecalcSkeletonWithCurrentTime();auto& a=input.ruby[actor];a.present=true;a.dead=unit->IsDead()!=FALSE;a.base=Point(unit->GetCP());
  for(int i=0;i<24;++i)if(auto* node=system->FindNode(owned::EmbeddedRubyBones[i])){auto& n=a.nodes[i];CPlot3D p;unit->GetOrientation().Rotate(node->GetPosition(),p);p+=unit->GetCP();n.position=Point(p);auto q=unit->GetOrientation();q*=node->GetOrientation();q.GetComponents(n.rotation[0],n.rotation[1],n.rotation[2],n.rotation[3]);n.valid=true;}
 }
 input.timer=Read("FGL.Timer");input.target_x=Read("FGL.TargetX");input.target_y=Read("FGL.TargetY");
 input.beam_ex=Read("FGL.BeamEX");input.beam_ey=Read("FGL.BeamEY");input.aim_x=Read("FGL.LaserAimX");input.aim_y=Read("FGL.LaserAimY");input.ring_radius=Read("FGL.LaserRingRadius");
 input.throw_count=std::clamp(int(Read("FGL.ThrowCount")),0,2);input.throw_launched=Read("FGL.ThrowLaunched")==1;
 for(int i=0;i<std::clamp(int(Read("FGL.FieldUsed")),0,Fields);++i) {
  auto& f=input.fields[i];f.end=Slot("Field",i,"End");if(f.end<=Now())continue;
  f.x=Slot("Field",i,"X");f.y=Slot("Field",i,"Y");f.z=Slot("Field",i,"Z");f.r=Slot("Field",i,"R");f.born=Slot("Field",i,"Born");f.style=Slot("Field",i,"Style");
 }
 for(int i=0;i<Shots;++i) {
  auto& s=input.shots[i];s.end=Slot("Shot",i,"End");if(s.end<=Now())continue;
  s.x=Slot("Shot",i,"X");s.y=Slot("Shot",i,"Y");s.z=Slot("Shot",i,"Z");s.ex=Slot("Shot",i,"EX");s.ey=Slot("Shot",i,"EY");s.ez=Slot("Shot",i,"EZ");
  s.born=Slot("Shot",i,"Born");s.hand=Slot("Shot",i,"Hand");s.stage=Slot("Shot",i,"Stage");for(int k=0;k<4;++k){char key[8];std::snprintf(key,sizeof(key),"Q%d",k);s.q[k]=Slot("Shot",i,key);}
 }
 for(int i=0;i<2;++i){input.casts[i]={Slot("Cast",i,"X"),Slot("Cast",i,"Y")};input.release[i]=Read(i?"FGL.BladeBRelease":"FGL.BladeARelease");input.ready[i]=Read(i?"FGL.BladeBReady":"FGL.BladeAReady");}
 if(auto* system=boss->GetAnimationSystem2())for(int i=0;i<3;++i)if(auto* node=system->FindNode(i==0?"n03":i==1?"n09":"n15")) {
  auto& n=input.nodes[i];CPlot3D p;boss->GetOrientation().Rotate(node->GetPosition(),p);p+=WorldPosition(*boss);n.position=Point(p);
  auto rotation=boss->GetOrientation();rotation*=node->GetOrientation();rotation.GetComponents(n.rotation[0],n.rotation[1],n.rotation[2],n.rotation[3]);n.valid=true;
 }
 input.reflection_used=std::clamp(int(Read("FGL.ReflectUsed")),0,64);
 for(int i=0;i<input.reflection_used;++i) {
  auto& slot=input.reflections[i];slot.born=Slot("Reflect",i,"Born");slot.end=Slot("Reflect",i,"End");slot.kind=int(Slot("Reflect",i,"Kind"));if(slot.end<=Now())continue;
  const float x=Slot("Reflect",i,"X"),y=Slot("Reflect",i,"Y"),ex=Slot("Reflect",i,"EX"),ey=Slot("Reflect",i,"EY");
  slot.start={x,y,Ground(x,y)+Slot("Reflect",i,"Z")};slot.target={ex,ey,Ground(ex,ey)+Slot("Reflect",i,"EZ")};
  const unsigned id=unsigned(Slot("Reflect",i,"Lo"))|(unsigned(Slot("Reflect",i,"Hi"))<<16);
  if(auto* recipient=theWorld.GetUnit(int(id));recipient&&!recipient->IsInvalid()){auto p=WorldPosition(*recipient);p.z+=.9f;slot.target=Point(p);}
 }
 std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,6> retained={Texture(boss),Texture(boss,true),Texture(theWorld.GetMapObject(990205)),Texture(theWorld.GetMapObject(994520)),Texture(theWorld.GetMapObject(994520),true),Texture(theWorld.GetMapObject(994521))};
 std::array<ID3D11ShaderResourceView*,6> textures={retained[0].Get(),retained[1].Get(),retained[2].Get(),retained[3].Get(),retained[4].Get(),retained[5].Get()};
 dx->SubmitGolemVisual(input,textures);
}
}

#include "ModGolemAppearance.inl"
