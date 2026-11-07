#include "StdAfx.h"
#include "ModCurseEncounter.h"
#include "ModCurseState.h"
#include "WorldClient.h"
#include "Object3DClientSpecific.h"
#include "Camera.h"
#include "Renderer.h"
#include "dx11/RendererDX11.h"
#include "OwnedRender/E1Scene.h"
#include "OwnedRender/E1World.h"
#include "embedded_curse.h"

namespace ModCurseEncounter {
namespace {
bool armed=false;
float Ground(float x,float y) {return theWorldServer.GetZValue(x,y);}
bool Recent(float born,int lifetime) {return born>0&&Now()>=born&&Now()-born<lifetime;}
owned::EmbeddedCurseActor ActorInput(unsigned id) {
    owned::EmbeddedCurseActor result;auto* unit=theWorld.GetUnit(int(id));
    if(!unit||unit->IsInvalid()||!unit->FinalDataLoading())return result;
    const auto p=unit->GetCP();result.present=true;result.incarnation=unit->GetRTID();
    // Client NewVerticalPos has already added terrain Z. Publish world space
    // once; native particles and the actor skeleton use this same position.
    result.base={p.x,p.y,p.z};return result;
}
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> FlowTexture() {
    auto* object=theWorld.GetMapObject(720101);
    if(!object||object->IsInvalid()||!object->FinalDataLoading())return {};
    auto* specific=dynamic_cast<CObject3DClientSpecific*>(object->GetObjectSpecific());
    auto* texture=specific?specific->GetPrimaryTexture():nullptr;
    auto* surface=texture?texture->GetSurface():nullptr;
    return surface?surface->GetShaderResource():Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>{};
}
}
void ResetVisual() noexcept {armed=false;}
void ArmVisual() noexcept {
    armed=Map()&&Read("ZC.Enabled")==1&&(Read("ZC.Active")==1||Read("ZC.Threat")!=0||
        Recent(Read("ZC.ImpactAt"),18)||Recent(Read("ZC.WardAt"),14));
}
void Draw() {
    if(!armed)return;
    if(!Map()||Read("ZC.Enabled")!=1){armed=false;return;}
    const bool active=Read("ZC.Active")==1;
    if(!active&&Read("ZC.Threat")==0&&!Recent(Read("ZC.ImpactAt"),18)&&!Recent(Read("ZC.WardAt"),14)){armed=false;return;}
    auto* dx=theRenderer.GetD3D();if(!dx||!dx->IsOwnedWorldActive())return;
    OwnedRender::E1WorldFrame frame;if(!OwnedRender::E1ReadWorldFrame(frame)||!frame.camera.valid)return;
    owned::EmbeddedCurseInput input;
    input.world_epoch=OwnedRender::E1Scene().Epoch();input.boss_id=666666;input.tick=Now();input.active=active;
    input.boss=ActorInput(666666);input.mage=ActorInput(640706);input.incarnation=input.boss.incarnation;
    input.threat=int(Read("ZC.Threat"));input.threat_born=Read("ZC.ThreatBorn");input.mage_mode=int(Read("ZC.MageMode"));
    input.x=Read("ZC.X");input.y=Read("ZC.Y");input.pad_x=Read("ZC.PadX");input.pad_y=Read("ZC.PadY");
    input.lance_ax=Read("ZC.LanceAX");input.lance_ay=Read("ZC.LanceAY");input.lance_bx=Read("ZC.LanceBX");input.lance_by=Read("ZC.LanceBY");
    input.impact_at=Read("ZC.ImpactAt");input.impact_x=Read("ZC.ImpactX");input.impact_y=Read("ZC.ImpactY");input.ward_at=Read("ZC.WardAt");
    if(Recent(input.ward_at,14))input.ward=ActorInput(unsigned(Slot("Ward",0,"Lo"))|(unsigned(Slot("Ward",0,"Hi"))<<16));
    if(active)for(int i=0;i<Pool;++i)if(Slot("Enemy",i,"On")==1&&Slot("Enemy",i,"Channel")==1){
        input.enemies[i]=ActorInput(710001+i);input.enemies[i].channel=true;
    }
    const auto camera=theCamera.GetCP();input.camera={camera.x,camera.y,camera.z};input.world_to_clip=frame.camera.world_to_clip;input.ground=Ground;
    const auto texture=FlowTexture();dx->SubmitCurseVisual(input,texture.Get());
}
}
