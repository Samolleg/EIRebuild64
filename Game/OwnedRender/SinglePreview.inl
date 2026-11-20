#include "model_draw.h"
// Source resource adapter for SinglePreview.h presentation and pass ordering.
struct SourceSinglePreview {
    CI3DItemScreen* screen;
    CRenderer* renderer;
    CI3DFigure* figure;
    CTexture* background;
    CTexture* modifiers;
    SDrawItem* item = nullptr;
    CRotateItem* state = nullptr;
    bool campPlaced = false;
    std::shared_ptr<const owned::PreparedCampIcon> icon;
    CI3DFigure* runeFigure = nullptr;
    const CampItemModel* model = nullptr;
    OwnedRender::SinglePreviewPlacement placement;
    ~SourceSinglePreview() { end_runes(); }
    void place(const OwnedRender::SinglePreviewPlacement& p) {
        if (!figure) { placement=p; return; }
        figure->SetDefaultSpecular(p.specular);
        figure->SetRelativeToScreen(p.x,p.y,p.z);
        figure->SetScale(p.scale);
        figure->SetAmbient(p.ambient,p.ambient,p.ambient);
        const auto quaternion=[](const OwnedRender::PreviewRotation& r) {
            return CQuaternion(CPlot3D(r.axis[0],r.axis[1],r.axis[2]),r.angle);
        };
        CQuaternion q=quaternion(p.rotations[0]);
        for (unsigned i=1;i<p.rotation_count;++i) q=q*quaternion(p.rotations[i]);
        figure->SetOrientation(q);
    }
    void rotation(const OwnedRender::SinglePreviewRotationState& s) {
        state->fAngle=s.angle;state->fSpeed=s.speed;state->fFlashVal=s.phase;
    }
    void bar(const OwnedRender::PreviewBar& b) {
        struct Figure {
            CI3DItemScreen* screen;CI3DFigure* value;
            ~Figure() { screen->RemoveFigure(value); }
        } temporary{screen,screen->AddFigure()};
        auto* p=temporary.value;
        if (b.gray) {
            p->MakeBar(b.z,100,b.left,b.top,b.right,b.bottom);
            p->SetTexture(nullptr);p->UseDefaultColors(TRUE);
            p->SetDefaultColor(RGBA_MAKE(128,64,64,130));
        } else {
            p->MakeBar(b.z,100,b.left,b.top,b.right,b.bottom,157,157,227,227);
            p->SetTexture(background);
        }
        p->Draw(renderer);
    }
    void draw_item() {
        if (model) { model->draw(renderer,placement,item->eFrom!=ISP_UNKNOWN); return; }
        if (!figure) return;
        const bool preview=item->eFrom==ISP_UNKNOWN&&campPlaced;
        const auto rgb=preview?E4CampAccess::PreviewRGB(screen,item->pItem):0u;
        OwnedRender::E4::Preview scope(rgb,preview);
        OwnedRender::E4::PreviewMaterial material(figure,preview,rgb);
        const auto& ambient=figure->GetAmbient();
        figure->SetDefaultColor(D3DRGBA(ambient.x,ambient.y,ambient.z,1));
        figure->UseDefaultColors(TRUE);
        DWORD specular=FALSE;
        renderer->GetRenderState(D3DRENDERSTATE_SPECULARENABLE,&specular);
        renderer->SetRenderState(D3DRENDERSTATE_SPECULARENABLE,item->pItem->GetSpellsCount()!=0);
        const bool colorSum=renderer->GetD3D()->SetItemColorSum(true);
        struct Restore {
            CRenderer* renderer;DWORD specular;bool colorSum;
            ~Restore() {
                renderer->GetD3D()->SetItemColorSum(colorSum);
                renderer->SetRenderState(D3DRENDERSTATE_SPECULARENABLE,specular);
            }
        } restore{renderer,specular,colorSum};
        OwnedRender::E4::Icon binding(figure,icon);
        figure->Draw(renderer);
    }
    void draw_head() {
        owned::ModelDrawScope preview(owned::ModelDrawDomain::preview);
        OwnedRender::E4::Icon binding(figure,icon);
        figure->Draw(renderer);
    }
    void position(float& x,float& y,float& z,float& scale) {
        figure->GetRelativeToScreen(x,y,z);scale=figure->GetXScale();
    }
    int rune_index(unsigned index) { return item->pItem->GetSpell(0)->GetRunes()[index]; }
    void rune(const OwnedRender::PreviewRune& r) {
        if (!runeFigure) {
            runeFigure=screen->AddFigure();
            runeFigure->MakeBar(r.z,100,0,0,35,35,0,0,32,32);
            runeFigure->SetTexture(modifiers);
            runeFigure->SetScale(r.scale);
            runeFigure->SetAmbient(r.ambient,r.ambient,r.ambient);
        }
        runeFigure->SetUShift(r.u);runeFigure->SetVShift(r.v);
        runeFigure->SetRelativeToScreen(r.x,r.y,r.z);
        runeFigure->Draw(renderer);
    }
    void end_runes() {
        if (runeFigure) { screen->RemoveFigure(runeFigure);runeFigure=nullptr; }
    }
    bool grayed() { return !screen->CanClickOnItem(item->pItem,item->eFrom); }
};
