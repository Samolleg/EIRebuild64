#pragma once

#include "Figure.h"
#include "GeometryInterfaces.h"
#include "Object.h"
#include <vector>
/////////////////////////////////////////////////////////////////////////////////////
static DWORD FCS_ANIMATED_TEXTURE = MAKE_FLAG(FIG_LAST_BIT, 0);

static DWORD FCS_LAST_BIT = MAKE_LAST_BIT(FIG_LAST_BIT, 0);
// static DWORD FCS_ANIMATED_TEXTURE = 0x00000001;
/////////////////////////////////////////////////////////////////////////////////////
class CRenderer;
class CLight;
/////////////////////////////////////////////////////////////////////////////////////
class CFigureClientSpecific : public CFigureSpecific
{
private:
    mutable CRect rcClip; // 2D screen clip rectangle
    CRect rcPickBounds;
    SHMatrix pickingMatrix;
    int pickingFrame=-1;
    mutable bool pickingExact=false;
    mutable std::vector<SXYZW> pickingVertices;
    mutable std::vector<unsigned char> pickingVisited;
    CPlot3D scale;

private:
    bool SubmitNativeFigure(CRenderer* renderer, CMaterial* material, BYTE fog,
        CSimplyArray<CLight*, CLight*>* lights);
    void RecalcXYAdd(float fpTimeStep);
    void DrawBBox(CRenderer *pRend);
    void CalculateScreenBounds() const;

    IGeometry *GetGeometry() const
    {
        if (!pFigure->lpObject)
            return 0;
        return reinterpret_cast<CObject3D *>(pFigure->lpObject)->GetGeometry();
    }

public:
    CFigureClientSpecific();
    virtual ~CFigureClientSpecific();
    // create/destroy functions
    virtual BOOL Create();
    virtual void Destroy();
#if defined(_A4) || defined(__OBJECT_CONVERTOR_A4__)
    virtual void SetScale(const CPlot3D &pt) { scale = pt; }
    const CPlot3D &GetScale() const { return scale; }
    float GetScaleX() const { return scale.x; }
    float GetScaleY() const { return scale.y; }
    float GetScaleZ() const { return scale.z; }
#endif // defined(_A4) || defined(__OBJECT_CONVERTOR_A4__)
       // for animated texture stepping
    void CalcAnimatedTextureFrame(float fpTime);
    // clipping rect functions
    void Calc2DClipRect(CXYZW4Array &homoscreen);
    void Calc2DClipRect(CXYZWArray &homoscreen);
    void Calc2DClipRect(CTLVertexArray &tlscreen);
    const CRect &GetPickingBounds() const { return rcPickBounds; }
    const CRect &Get2DClipRect() const { if(!pickingExact)CalculateScreenBounds();return rcClip; }
    void PrepareOwnedPicking(CRenderer *pRend, CRect *pRect);
    // drawing
    BOOL Draw(CRenderer *pRend, const CQuaternion &qObject, const CPlot3D &cpObject,
              LPGAMEBASE lpMaterial, BYTE cFog, CRect *pRect);
    BOOL Draw(CRenderer *pRend, CSimplyArray<CLight *, CLight *> &lights, LPGAMEBASE lpMaterial,
              BYTE cFog, CRect *pRect, CTexture *pTexture0 = 0, CTexture *pTexture1 = 0);
    BOOL DrawRealisticShadow2(SHMatrix &matShadow, SHMatrix &matScale, LPVOID lpRenderParam);
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CFigureClientSpecific"; };
    virtual EClassIDs WhoAmI() const { return HPC_FIGURECLIENTSPECIFIC; };
};
/////////////////////////////////////////////////////////////////////////////////////
void ComposeD3DTLVertices(D3DTLVERTEX *vertices, SXYZW *geoms, DWORD *colors, DWORD *speculars,
                          CPlot2D *maps, SVertexComponents *components, int nComponents);
