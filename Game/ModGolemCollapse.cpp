#include "StdAfx.h"
#include "ModFinalGolem.h"
#include "ModFinalGolemState.h"
#include "ModCliTool.h"
#include "WorldClient.h"
#include "ModGolemCollapse.inl"
#include <stdexcept>
namespace ModFinalGolem {
namespace {
void PublishCollapse(CMapObject& object) {
 object.UpdateFrame();auto* client=theWorld.GetMapObject(object.GetID());if(!client||client==&object||client->IsInvalid())return;
 auto p=object.GetCP();p.z+=theWorldServer.GetZValue(p.x,p.y);client->SetCP(p);client->SetOrientation(object.GetOrientation());client->ResetFlag(OBJ_STATIC|OBJ_LIGHTED);
}
bool SceneReady(){return ModCliTool::CurrentKind()!=ModCliTool::Kind::NavBake&&ModEncounter::Hard(theWorldServer)&&Read("EFC.Enabled")==1;}
}
void CommitCollapse() {
 if(!SceneReady())return;const int requested=int(Read("HG.CollapsePending"));if(!requested)return;
 auto& nav=theWorldServer.aiMap.Navigation();for(int type=0;type<Navigation::AgentCount;++type)if(type!=Navigation::Flying&&nav.ObserverUpdating(type))return;
 Set("HG.CollapseNav",float(requested));Set("HG.CollapsePending",0);
}
void RestoreCollapse() {
 if(!SceneReady()||Read("HG.CollapseSaved")!=1)return;
 // Native saves own the complete pose. Rebuild membership from the saved
 // encounter phase before the first Nav64 initial synchronization or route.
 const int stage=int(Read("HG.Stage"));const bool present=stage>=4&&stage<=6||(stage==3&&Read("HG.Timer")>=60);
 auto& nav=theWorldServer.aiMap.Navigation();
 for(const auto& piece:collapsePieces){auto* object=theWorldServer.GetMapObject(int(piece.id));if(!object||object->IsInvalid())throw std::runtime_error("hard collapse saved object missing");
  std::array<float,4> q;object->GetOrientation().GetComponents(q[0],q[1],q[2],q[3]);const auto p=object->GetCP();
  // World loading registered all saved objects in the native spatial index.
  nav.ForgetObject(int(piece.id));nav.TransformObject(int(piece.id),{p.x,p.y,p.z},q,present);
 }
 Set("HG.CollapsePending",present?2.f:stage>=8?4.f:0.f);
}
void CollapseTick(bool clearing) {
 if(!SceneReady())return;
 auto& nav=theWorldServer.aiMap.Navigation();float frame=Read("HG.Timer");if(!std::isfinite(frame)||frame<0)throw std::runtime_error("invalid hard collapse frame");
 const bool settled=!clearing&&frame>=60;const bool clearingStarted=clearing&&Read("HG.CollapseClearing")!=1;
 const bool settledStarted=settled&&Read("HG.CollapseSettled")!=1;
 if((!clearing&&!settled)||clearing&&frame<=72||settledStarted||clearingStarted){
  int index=0;for(const auto& piece:collapsePieces){
   auto* object=theWorldServer.GetMapObject(int(piece.id));if(!object||object->IsInvalid())throw std::runtime_error("hard collapse authored object missing");
   const auto pose=Collapse::Sample(piece,index++,settled?60.f:frame,clearing);
   // Remove while the old pose is still installed. Re-adding is exclusively
   // the final settled publication; flying/clearing faces never enter ground.
   nav.TransformObject(int(piece.id),{pose.position.x,pose.position.y,pose.position.z},std::array<float,4>{pose.rotation.w,pose.rotation.x,pose.rotation.y,pose.rotation.z},settled);
   PublishCollapse(*object);
  }
 }
 Set("HG.CollapseSaved",1);Set("HG.CollapseFrame",frame);
 if(!clearing){Set("HG.CollapseClearing",0);if(settled){Set("HG.CollapseSettled",1);Set("HG.CollapsePending",2);}else{Set("HG.CollapseSettled",0);Set("HG.CollapseNav",1);}}
 else{Set("HG.CollapseClearing",1);Set("HG.CollapseSettled",0);Set("HG.CollapseNav",3);if(frame>=72)Set("HG.CollapsePending",4);}
 CommitCollapse();
}
}
