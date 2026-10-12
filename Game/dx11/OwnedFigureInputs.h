#pragma once
#include "OwnedRendererClient.h"
#include "../Figure.h"
#include "../MapObject.h"
#include "../Material.h"
#include "../Texture.h"

namespace OwnedFigureInputs {
// This scope contains copied material/identity values in the EXE. Figure and
// object pointers are inspected only here and never sent to the renderer DLL.
class Scope {
    OwnedRendererClient& renderer;
public:
    Scope(OwnedRendererClient& r,CFigure* figure,CMaterial* material,bool world,
          CTexture* texture0,CTexture* texture1,
          ID3D11ShaderResourceView* boundPrimary,
          ID3D11ShaderResourceView* boundSecondary):renderer(r) {
        renderer.EndFigure();
        auto* object=dynamic_cast<CMapObject*>(figure->GetObject3D());
        if(!material)return;
        EIRenderFigureV4 p={};p.size=sizeof(p);p.version=4;
        p.object_id=world&&object?uint32_t(object->GetID()):0;
        p.category=object&&object->WhoAmI()==HPC_UNIT?
            (figure->GetTextureTypeID()==TTID_UNIT_WEAPON?EI_RENDERER_FIGURE_EQUIPMENT:EI_RENDERER_FIGURE_CHARACTER):EI_RENDERER_FIGURE_STATIC;
        p.texture_role=figure->GetTextureTypeID();
        p.visible=1;p.no_shadow=!object||object->CheckFlag(OBJ_NOSHADOW)?1u:0u;
        p.highlighted=object&&object->CheckFlag(OBJ_SELECTED|OBJ_PRESELECTED)?1u:0u;
        if(object){const auto& shape=object->GetComplection();
        p.shape[0]=shape.strength;p.shape[1]=shape.grace;p.shape[2]=shape.tall;}
        const auto& m=material->GetMaterial();
        p.material_diffuse[0]=m.diffuse.r;p.material_diffuse[1]=m.diffuse.g;p.material_diffuse[2]=m.diffuse.b;
        p.material_alpha=m.diffuse.a;
        memcpy(p.emissive,&m.emissive,sizeof(p.emissive));
        memcpy(p.material_specular,&m.specular,sizeof(p.material_specular));
        auto primary=texture0&&texture0->GetSurface()?texture0->GetSurface()->GetShaderResource():
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>{};
        auto secondary=texture1&&texture1->GetSurface()?texture1->GetSurface()->GetShaderResource():
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>{};
        if(!primary&&boundPrimary)primary=boundPrimary;
        if(!secondary&&boundSecondary)secondary=boundSecondary;
        renderer.BeginFigure(p,figure->GetName(),object?object->GetMaskName():nullptr,primary.Get(),secondary.Get());
    }
    ~Scope(){renderer.EndFigure();}
};
static_assert(sizeof(SXYZW4)==sizeof(EIRenderPackedV4),"source packed ABI");
static_assert(sizeof(SVertexComponents)==sizeof(EIRenderComponentV4),"source component ABI");
}
