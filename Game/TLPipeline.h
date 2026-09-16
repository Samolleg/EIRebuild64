#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "Plot.h"
#include "Light.h"
#include "dx11/Vertex.h"
/////////////////////////////////////////////////////////////////////////////////////
class CLight;
struct SBoundingVolume;
/////////////////////////////////////////////////////////////////////////////////////
// TL pipeline constants
static const int TRV_FAILED = -1;
static const int TRV_VISIBLE_NOCLIP = 0;
static const int TRV_VISIBLE_CLIP = 1;
static const int TRV_INVISIBLE = 2;
static const BYTE TRV_CLIP_MINX = 0x01;
static const BYTE TRV_CLIP_MAXX = 0x02;
static const BYTE TRV_CLIP_MINY = 0x04;
static const BYTE TRV_CLIP_MAXY = 0x08;
static const BYTE TRV_CLIP_MINZ = 0x10;
static const BYTE TRV_CLIP_MAXZ = 0x20;
static const BYTE TRV_CLIP_ALL = TRV_CLIP_MINX | TRV_CLIP_MAXX | TRV_CLIP_MINY | TRV_CLIP_MAXY |
                                 TRV_CLIP_MINZ | TRV_CLIP_MAXZ;
/////////////////////////////////////////////////////////////////////////////////////
// class CTLPipeline
// transformation and lit pipeline base abstract class
class CTLPipeline
{
#ifdef _A4
public:
    enum ELightModel
    {
        ELM_ADVENTURE,
        ELM_COMBAT
    } curLM;
#endif //_A4
protected:
    CLight *lpSunlight;

    CWORDArray clipindices;
    int nNewIndices;

public:
    CTLPipeline();
    virtual ~CTLPipeline();
    // ************************** HELPER FUNCTIONS *************************************
    // set clip rect for correct TL pipeline work
    virtual void SetClipRect(float fpClipWidth, float fpClipHeight) = 0;
    // set sunlight (to avid pass it to each lit function)
    virtual void SetSunlight(CLight *pLight) = 0;
    // set ambient light (to avid pass it to each lit function)
    virtual void SetAmbientLight(float r, float g, float b) = 0;
    // set projection matrix
    virtual void SetProjectionMatrix(D3DMATRIX &projection) = 0;
    // set fog range
    virtual void SetFogRange(float fpNewFogStart, float fpNewFogEnd) = 0;
    // bounding volume recalculation functions
    virtual void CalculateBoundingVolume(SBoundingVolume *lpBV, SXYZW4 *plots, int nNumPlots) = 0;
    virtual void CalculateBoundingVolume(SBoundingVolume *lpBV, SXYZW *plots, int nNumPlots) = 0;
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // different vertex array transformation functions
    // transform vertices with homogenezation
    // without clip check
    virtual int TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, STLVertex1 *lpDst,
                                  DWORD *speculars, const int nCount) = 0;
    virtual int TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, STLVertex1 *lpDst,
                                  const int nCount) = 0;
    virtual int TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, SXYZW4 *lpDst,
                                  const int nCount) = 0;
    virtual int TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, SXYZW *lpDst,
                                  const int nCount) = 0;
    virtual int TransformVertices(const SHMatrix &matrix, SXYZW *lpSrc, SXYZW *lpDst,
                                  const int nCount) = 0;
    // with clip check. if not clipped, then homogeneaze, else - leave in projection space (for
    // further clipping)
    virtual int TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, STLVertex1 *lpDst,
                                  BYTE *lpClip, DWORD *speculars, const int nCount) = 0;
    virtual int TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, STLVertex1 *lpDst,
                                  BYTE *lpClip, const int nCount) = 0;
    virtual int TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, SXYZW4 *lpDst,
                                  BYTE *lpClip, const int nCount) = 0;
    virtual int TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, SXYZW *lpDst, BYTE *lpClip,
                                  const int nCount) = 0;
    virtual int TransformVertices(const SHMatrix &matrix, SXYZW *lpSrc, SXYZW *lpDst, BYTE *lpClip,
                                  const int nCount) = 0;
    // simply transform vertices (just multiply vertices to matrix)
    virtual int SimplyTransform(const SHMatrix &matrix, SXYZW *lpSrc, SXYZW *lpDst,
                                const int nCount) = 0;
    virtual int SimplyTransform(const SHMatrix &matrix, SXYZW *lpSrc, SXYZW *lpDst, BYTE *lpClip,
                                const int nCount) = 0;
    //	// w/o clip check
    //	virtual int SimplyTransform( const SHMatrix& matrix, SXYZW4* pSrc, SXYZW4* pDst, int nCount
    //) = 0; 	virtual int SimplyTransform( const SHMatrix& matrix, SXYZW4* pSrc, SXYZW* pDst, int
    // nCount ) = 0; 	virtual int SimplyTransform( const SHMatrix& matrix, SXYZW* pSrc, SXYZW*
    // pDst, int nCount ) = 0;

    // ***************** TRANSFORMATION PIPELINE FUNCTIONS *****************************
    // transform vertices from SXYZW4 to SXYZW4 without clipping chek
    virtual int TransformVertices4NC(const D3DMATRIX &matrix, SXYZW4 *lpSrc, SXYZW4 *lpDst,
                                     const int nCount) = 0;
    // transform vertices from SXYZW4 to SXYZW4 with clipping chek
    virtual int TransformVertices4C(const D3DMATRIX &matrix, SXYZW4 *lpSrc, SXYZW4 *lpDst,
                                    BYTE *pClip, const int nCount) = 0;
    // transform vertices from SXYZW4 to SXYZW without clipping chek
    virtual int TransformVerticesNC(const D3DMATRIX &matrix, SXYZW4 *lpSrc, SXYZW *lpDst,
                                    const int nCount) = 0;
    // transform vertices from SXYZW4 to SXYZW with clipping chek
    virtual int TransformVerticesC(const D3DMATRIX &matrix, SXYZW4 *lpSrc, SXYZW *lpDst,
                                   BYTE *pClip, const int nCount) = 0;
    // transform 2d particled and store results in special format
    virtual int Transform2DParticles(const D3DMATRIX &matrix, SXYZW *pSrc, D3DTLVERTEX *pDst,
                                     BYTE *pClip, const int nCount) = 0;
    // simply multiply 'src' (SXYZW4) vertices to 'matrix' and store results in 'dst' (SXYZW4)
    // vertices w/o homogenezation and w/o clip check
    virtual int SimplyTransform4NC(const D3DMATRIX &matrix, SXYZW4 *pSrc, SXYZW4 *pDst,
                                   int nCount) = 0;
    // simply multiply 'src' (SXYZW4) vertices to 'matrix' and store results in 'dst' (SXYZW)
    // vertices w/o homogenezation and w/o clip check
    virtual int SimplyTransformNC(const D3DMATRIX &matrix, SXYZW4 *pSrc, SXYZW *pDst,
                                  int nCount) = 0;
    // simply multiply 'src' (SXYZW4) vertices to 'matrix' and store results in 'dst' (SXYZW4)
    // vertices w/o homogenezation and with clip check
    virtual int SimplyTransform4C(const D3DMATRIX &matrix, SXYZW4 *pSrc, SXYZW4 *pDst, BYTE *pClip,
                                  int nCount) = 0;
    // simply multiply 'src' (SXYZW4) vertices to 'matrix' and store results in 'dst' (SXYZW)
    // vertices w/o homogenezation and with clip check
    virtual int SimplyTransformC(const D3DMATRIX &matrix, SXYZW4 *pSrc, SXYZW *pDst, BYTE *pClip,
                                 int nCount) = 0;
    // transform shadow on plane ZERO and convert x and y to int
    virtual int TransformShadow(const SHMatrix &matrix, SXYZW4 *lpSrc, POINT *lpDst,
                                int nCount) = 0;
    virtual int TransformShadow(const SHMatrix &matrix, SXYZW4 *lpSrc, CPlot2D *lpDst, int nCount,
                                CTRect<float> *pRect) = 0;
    // transform vertices from projection space to screen w/o care about 'wp'
    virtual void CastProjectionToScreenNC(D3DTLVERTEX *vertices, int nVertices) = 0;
    // transform vertices from projection space to screen with care about 'wp'
    virtual void CastProjectionToScreenC(D3DTLVERTEX *vertices, int nVertices) = 0;
    // transform plots from projection space to screen w/o care about 'wp'
    virtual void CastProjectionToScreenNC(SXYZW *plots, int nPlots) = 0;
    // transform plotsfrom projection space to screen with care about 'wp'
    virtual void CastProjectionToScreenC(SXYZW *plots, int nPlots) = 0;
    // add fog to vertices
    virtual void AddFog(D3DTLVERTEX *vertices, int nSize) = 0;
    // clip face, which consist of vertices in projection space
    virtual int ClipFace(D3DTLVERTEX *vertices, BYTE *clips, WORD *indices, int &nVertices,
                         int &nIndices) = 0;
    virtual void ClipFaces(D3DTLVERTEX *vertices, BYTE *cClips, WORD *wInputIndices,
                           WORD *wOutputIndices, int &nNumVertices, int &nNumIndices) = 0;
    // interpolate two vertices with coeff 'alpha'
    virtual void MakeVertex(const float alpha, D3DTLVERTEX *pVertex0, D3DTLVERTEX *pVertex1,
                            D3DTLVERTEX *pResultVertex) = 0;

    // ********************** LIGHTING PIPELINE FUNCTIONS ******************************
#ifdef _A4
    virtual void SetLightModel(ELightModel newLM);
    virtual ELightModel GetLightModel() { return curLM; }
#endif //_A4
       // general function to lit vertices with point lights (with diffuse and specular)
    virtual void LightVerticesGeneralPt(SXYZW4 *plots, SXYZW4 *norms, LPDWORD colors,
                                        LPDWORD speculars, SGvNvComponents *components,
                                        int nComponents, CLight **lights, int nLights,
                                        float fpMaterialPower, const D3DVECTOR &dvEmissive,
                                        const CPlot3D &dc, const BYTE cFog,
                                        DWORD bFlatPointLights) = 0;
    // lit vertices with diffuse components only (with point lights)
    virtual void LightVerticesDfPt(SXYZW4 *plots, SXYZW4 *norms, LPDWORD colors,
                                   SGvNvComponents *components, int nComponents, CLight **lights,
                                   int nLights, const D3DVECTOR &dvEmissive) = 0;
    // lit vertices with specular components only (with point lights)
    virtual void LightVerticesSpPt(SXYZW4 *plots, SXYZW4 *norms, LPDWORD speculars,
                                   SGvNvComponents *components, int nComponents, CLight **lights,
                                   int nLights, float fpMaterialPower, const D3DVECTOR &dvEmissive,
                                   const CPlot3D &dc, const BYTE cFog = 255) = 0;
    // general function to lit vertices without point lights (with diffuse and specular)
    virtual void LightVerticesGeneral(SXYZW4 *plots, SXYZW4 *norms, LPDWORD colors,
                                      LPDWORD speculars, SGvNvComponents *components,
                                      int nComponents, float fpMaterialPower,
                                      const D3DVECTOR &dvEmissive, const CPlot3D &dc,
                                      const BYTE cFog = 255) = 0;
    // lit vertices with diffuse components only (without point lights)
    virtual void LightVerticesDf(SXYZW4 *plots, SXYZW4 *norms, LPDWORD colors,
                                 SGvNvComponents *components, int nComponents,
                                 const D3DVECTOR &dvEmissive) = 0;
    // lit vertices with specular components only (without point lights)
    virtual void LightVerticesSp(SXYZW4 *plots, SXYZW4 *norms, LPDWORD speculars,
                                 SGvNvComponents *components, int nComponents,
                                 float fpMaterialPower, const D3DVECTOR &dvEmissive,
                                 const CPlot3D &dc, const BYTE cFog = 255) = 0;
};
/////////////////////////////////////////////////////////////////////////////////////
inline CTLPipeline::CTLPipeline()
{
    lpSunlight = 0;
    nNewIndices = 0;
    clipindices.SetSize(32);
#ifdef _A4
    curLM = ELM_ADVENTURE;
#endif //_A4
}
inline CTLPipeline::~CTLPipeline()
{
}
/////////////////////////////////////////////////////////////////////////////////////
typedef CTLPipeline *LPTLPIPELINE;
typedef LPTLPIPELINE(FAR WINAPI *GETTLPIPELINE)();
