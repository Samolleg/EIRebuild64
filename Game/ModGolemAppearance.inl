// Client facts only; geometry and rendering remain owned by the renderer.
#include "Figure.h"
#include "TextureManager.h"
#include "ModGolemParts.inl"
namespace ModFinalGolem {
namespace {
float PoseValue(const char* group,int node,int component) {
 char key[96];std::snprintf(key,sizeof(key),"FGL.Visual.%s.N%d.%d",group,node,component);return Read(key);
}
void PoseValue(const char* group,int node,int component,float value) {
 char key[96];std::snprintf(key,sizeof(key),"FGL.Visual.%s.N%d.%d",group,node,component);Set(key,value);
}
}
void CaptureVisualState(bool death,int from,int to) {
 if(!ModEncounter::Arena(theWorldServer))return;
 if(death&&Read("FGL.DeathBorn")>0)return;
 auto* boss=theWorld.GetUnit(990400);if(!boss||boss->IsInvalid()||!boss->FinalDataLoading())return;
 auto* system=boss->GetAnimationSystem2();if(!system)return;system->RecalcSkeletonWithCurrentTime();
 std::array<owned::EmbeddedGolemNode,30> nodes{};
 for(int i=0;i<30;++i){char name[8];std::snprintf(name,sizeof(name),"n%02d",i);auto* node=system->FindNode(name);if(!node)continue;
  auto& n=nodes[i];CPlot3D p;boss->GetOrientation().Rotate(node->GetPosition(),p);p+=boss->GetCP();n.position=Point(p);
  auto q=boss->GetOrientation();q*=node->GetOrientation();q.GetComponents(n.rotation[0],n.rotation[1],n.rotation[2],n.rotation[3]);n.valid=true;
 }
 for(int i:{0,1,2,3,6,8,9,12,14,15,18,20,21,24,26,27,28,29})if(!nodes[i].valid){NConsole::game<<"golem visual capture: required current bone missing"<<std::endl;return;}
 const char* group=death?"Death":"Detach";
 for(int i=0;i<30;++i){for(int k=0;k<3;++k)PoseValue(group,i,k,nodes[i].position[k]);for(int k=0;k<4;++k)PoseValue(group,i,k+3,nodes[i].rotation[k]);PoseValue(group,i,7,nodes[i].valid?1.f:0.f);}
 if(death){Set("FGL.DeathX",boss->GetCP().x);Set("FGL.DeathY",boss->GetCP().y);Set("FGL.DeathZ",boss->GetCP().z);Set("FGL.DeathBorn",float(Now()));}
 else{Set("FGL.DetachFrom",float(from));Set("FGL.DetachTo",float(to));Set("FGL.DetachBorn",float(Now()));}
 ArmVisual();
}
void Died(CUnit& unit) {
 if(unit.GetID()==990400&&ModEncounter::Arena(theWorldServer)&&Read("DJW.Stage")>0)CaptureVisualState(true,5,5);
}
bool IsRubyActor(const CObject3D& object){return ModEncounter::Hard(theWorldServer)&&Active()&&(object.GetID()==Read("HG.GuardID")||object.GetID()==Read("HG.ObeliskID"));}
bool PrepareAppearance(CObject3D& object) {
 if(IsRubyActor(object)){auto* unit=dynamic_cast<CUnit*>(&object);auto* specific=dynamic_cast<CObject3DClientSpecific*>(object.GetObjectSpecific());if(!unit||!specific)return false;
  struct State {std::uint64_t epoch=0,identity=0;bool dead=false;};static State actors[2];auto& cached=actors[object.GetID()==Read("HG.ObeliskID")?1:0];const bool dead=unit->IsDead()!=FALSE;
  if(cached.epoch!=OwnedRender::E1Scene().Epoch()||cached.identity!=object.GetRTID()||cached.dead!=dead){specific->InvalidateOwnedAppearance();cached={OwnedRender::E1Scene().Epoch(),object.GetRTID(),dead};}return true;
 }
 if(object.GetID()!=990400||!ModEncounter::Arena(theWorldServer)||Read("DJW.Stage")<=0)return true;
 ArmVisual();auto* boss=dynamic_cast<CUnit*>(&object);if(!boss)return false;
 auto* specific=dynamic_cast<CObject3DClientSpecific*>(object.GetObjectSpecific());if(!specific)return false;
 const int stage=boss->IsDead()?5:std::clamp(int(Read("DJW.Stage")),1,5);
 const bool hard=ModEncounter::Hard(theWorldServer),dead=boss->IsDead()!=FALSE;
 unsigned blades=0;for(int hand=0;hand<2;++hand){float released=Read(hand?"FGL.BladeBRelease":"FGL.BladeARelease"),ready=Read(hand?"FGL.BladeBReady":"FGL.BladeAReady");if(Active()&&stage<5&&released>0&&Now()>=released&&Now()<ready)blades|=1u<<hand;}
 static std::uint64_t epoch=0,identity=0;static int signature=-1;const int next=stage|int(dead)<<3|int(hard)<<4|int(blades)<<5;
 if(epoch==OwnedRender::E1Scene().Epoch()&&identity==object.GetRTID()&&signature==next)return true;
 const char* body=hard?(stage>=2?"rghcrack":"rghbody"):(stage>=2?"djwcrack":"djwbody");
 const char* blade=hard?(stage>=2?"rghbreak":"rghblade"):(stage>=2?"djwbreak":"djwblade");
 auto* primary=specific->GetPrimaryTexture();auto* secondary=specific->GetSecondaryTexture();if(!primary||!secondary)return false;
 // Resolve the complete pair before changing either provider. No shared edits.
 auto* manager=static_cast<CTextureManager*>(theWorld.GetTextureManager());
 auto* nextPrimary=manager?manager->FindTexture(primary->GetTypeID(),body):nullptr;
 auto* nextSecondary=manager?manager->FindTexture(secondary->GetTypeID(),blade):nullptr;
 if(!nextPrimary||!nextSecondary||!manager->LoadTexture(nextPrimary)||!manager->LoadTexture(nextSecondary)||!nextPrimary->GetSurface()||!nextSecondary->GetSurface())return false;
 for(const auto& part:golemPartNames)if(part.stage==stage&&!object.FindFigure(part.name))return false;
 bool changed=false;
 if(primary!=nextPrimary){specific->ChangeTexture(nextPrimary);changed=true;}
 if(secondary!=nextSecondary){specific->ChangeTexture(nextSecondary);changed=true;}
 for(const auto& part:golemPartNames){auto* figure=object.FindFigure(part.name);if(!figure)continue;
  bool hidden=part.stage!=stage||dead;
  const int hand=strstr(part.name,"n09")?0:strstr(part.name,"n15")?1:-1;
  if(!dead&&Active()&&stage<5&&hand>=0){float released=Read(hand?"FGL.BladeBRelease":"FGL.BladeARelease"),ready=Read(hand?"FGL.BladeBReady":"FGL.BladeAReady");hidden=hidden||(released>0&&Now()>=released&&Now()<ready);}
  if(bool(figure->CheckFlag(FIG_HIDDEN))!=hidden){if(hidden)figure->SetFlag(FIG_HIDDEN);else figure->ResetFlag(FIG_HIDDEN);changed=true;}
  if(strstr(part.name,"_core")){const DWORD color=hard?0xffff7088u:0xffb7eeffu;if(!figure->CheckFlag(FIG_USE_DEFAULT_COLORS)||figure->GetDefaultColor()!=color){figure->SetFlag(FIG_USE_DEFAULT_COLORS);figure->SetDefaultColor(color);changed=true;}}
 }
 if(changed)specific->InvalidateOwnedAppearance();epoch=OwnedRender::E1Scene().Epoch();identity=object.GetRTID();signature=next;return true;
}
void SavedPose(const char* group,std::array<owned::EmbeddedGolemNode,30>& nodes) {
 for(int i=0;i<30;++i){auto& n=nodes[i];n.valid=PoseValue(group,i,7)==1;for(int k=0;k<3;++k)n.position[k]=PoseValue(group,i,k);for(int k=0;k<4;++k)n.rotation[k]=PoseValue(group,i,k+3);}
}
}
