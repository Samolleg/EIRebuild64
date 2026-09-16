#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "TLPipeline.h"
/////////////////////////////////////////////////////////////////////////////////////
class CTLPipelineFPU : public CTLPipeline
{
private:
    D3DCOLORVALUE dcvAmbientColor; // ambient color
    float fpClipHH, fpClipHW;      // clip half-height and half-width
    float fpFogStart;              // fog start distance
    float fpFogEnd;                // fog end distance
    float fpRcprFogDiff255;        // fog end - fog start
    D3DMATRIX projection;

private:
    // interpolate two vertices in projection space to fit on the sides of the clipping volume
    void MakeMinXVertex(D3DTLVERTEX *pVertex0, D3DTLVERTEX *pVertex1, D3DTLVERTEX *pResultVertex);
    void MakeMaxXVertex(D3DTLVERTEX *pVertex0, D3DTLVERTEX *pVertex1, D3DTLVERTEX *pResultVertex);
    void MakeMinYVertex(D3DTLVERTEX *pVertex0, D3DTLVERTEX *pVertex1, D3DTLVERTEX *pResultVertex);
    void MakeMaxYVertex(D3DTLVERTEX *pVertex0, D3DTLVERTEX *pVertex1, D3DTLVERTEX *pResultVertex);
    void MakeMinZVertex(D3DTLVERTEX *pVertex0, D3DTLVERTEX *pVertex1, D3DTLVERTEX *pResultVertex);
    void MakeMaxZVertex(D3DTLVERTEX *pVertex0, D3DTLVERTEX *pVertex1, D3DTLVERTEX *pResultVertex);
    // clipping
    void ClipMinX(D3DTLVERTEX *vertices, BYTE *clips, int &nVertices, WORD *indices, int &nIndices);
    void ClipMaxX(D3DTLVERTEX *vertices, BYTE *clips, int &nVertices, WORD *indices, int &nIndices);
    void ClipMinY(D3DTLVERTEX *vertices, BYTE *clips, int &nVertices, WORD *indices, int &nIndices);
    void ClipMaxY(D3DTLVERTEX *vertices, BYTE *clips, int &nVertices, WORD *indices, int &nIndices);
    void ClipMinZ(D3DTLVERTEX *vertices, BYTE *clips, int &nVertices, WORD *indices, int &nIndices);
    void ClipMaxZ(D3DTLVERTEX *vertices, BYTE *clips, int &nVertices, WORD *indices, int &nIndices);

public:
    CTLPipelineFPU();
    virtual ~CTLPipelineFPU();
    // ************************** HELPER FUNCTIONS *************************************
    // set clip rect for correct TL pipeline work
    virtual void SetClipRect(float fpClipWidth, float fpClipHeight);
    // set sunlight (to avid pass it to each lit function)
    virtual void SetSunlight(CLight *lpLight);
    // set ambient light (to avid pass it to each lit function)
    virtual void SetAmbientLight(float r, float g, float b);
    // set projection matrix
    virtual void SetProjectionMatrix(D3DMATRIX &proj);
    // set fog range
    virtual void SetFogRange(float fpNewFogStart, float fpNewFogEnd);
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // different vertex array transformation functions
    // transform vertices with homogenezation
    // without clip check
    virtual int TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, STLVertex1 *lpDst,
                                  DWORD *speculars, const int nCount);
    virtual int TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, STLVertex1 *lpDst,
                                  const int nCount);
    virtual int TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, SXYZW4 *lpDst,
                                  const int nCount);
    virtual int TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, SXYZW *lpDst,
                                  const int nCount);
    virtual int TransformVertices(const SHMatrix &matrix, SXYZW *lpSrc, SXYZW *lpDst,
                                  const int nCount);
    // with clip check. if not clipped, then homogeneaze, else - leave in projection space (for
    // further clipping)
    virtual int TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, STLVertex1 *lpDst,
                                  BYTE *lpClip, DWORD *speculars, const int nCount);
    virtual int TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, STLVertex1 *lpDst,
                                  BYTE *lpClip, const int nCount);
    virtual int TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, SXYZW4 *lpDst,
                                  BYTE *lpClip, const int nCount);
    virtual int TransformVertices(const SHMatrix &matrix, SXYZW4 *lpSrc, SXYZW *lpDst, BYTE *lpClip,
                                  const int nCount);
    virtual int TransformVertices(const SHMatrix &matrix, SXYZW *lpSrc, SXYZW *lpDst, BYTE *lpClip,
                                  const int nCount);
    // simply transform vertices (just multiply vertices to matrix)
    virtual int SimplyTransform(const SHMatrix &matrix, SXYZW *lpSrc, SXYZW *lpDst,
                                const int nCount);
    virtual int SimplyTransform(const SHMatrix &matrix, SXYZW *lpSrc, SXYZW *lpDst, BYTE *lpClip,
                                const int nCount);
    //	// w/o clip check
    //	virtual int SimplyTransform( const SHMatrix& matrix, SXYZW4* lpSrc, SXYZW4* lpDst, const int
    // nCount ); 	virtual int SimplyTransform( const SHMatrix& matrix, SXYZW4* lpSrc, SXYZW*
    // lpDst, const int nCount ); 	virtual int SimplyTransform( const SHMatrix& matrix, SXYZW*
    // lpSrc, SXYZW* lpDst, const int nCount );
    //	// with clip check
    //	virtual int SimplyTransform( const SHMatrix& matrix, SXYZW4* pSrc, SXYZW4* pDst, BYTE*
    // pClip, int nCount ) = 0; 	virtual int SimplyTransform( const SHMatrix& matrix, SXYZW4*
    // pSrc, SXYZW* pDst, BYTE* pClip, int nCount ) = 0; 	virtual int SimplyTransform( const
    // SHMatrix& matrix, SXYZW* pSrc, SXYZW* pDst, BYTE* pClip, int nCount ) = 0;
    //	// transform vertices from projection space to screen space
    //	// w/o care about 'wp'
    //	virtual void CastProjectionToScreenNC( STLVertex1* vertices, int nVertices ) = 0;
    //	virtual void CastProjectionToScreenNC( STLVertex3* vertices, int nVertices ) = 0;
    //	virtual void CastProjectionToScreenNC( SXYZW* plots, int nPlots ) = 0;
    //	// with care about 'wp'
    //	virtual void CastProjectionToScreenC( STLVertex1* vertices, int nVertices ) = 0;
    //	virtual void CastProjectionToScreenC( STLVertex3* vertices, int nVertices ) = 0;
    //	virtual void CastProjectionToScreenC( SXYZW* plots, int nPlots ) = 0;
    //	// transform 2d particled and store results in STLVertex1 array
    //	virtual int Transform2DParticles( const SHMatrix& matrix, SXYZW* pSrc, STLVertex1* pDst,
    // BYTE* pClip, const int nCount ) = 0;
    //	// add fog to vertices
    //	virtual void AddFog( STLVertex1* vertices, int nSize ) = 0;
    //	virtual void AddFog( STLVertex3* vertices, int nSize ) = 0;
    //	// interpolate two vertices with coeff 'alpha'
    //	virtual void MakeVertex( const float alpha, STLVertex1* pVertex0, STLVertex1* pVertex1,
    // STLVertex1* pResultVertex ) = 0; 	virtual void MakeVertex( const float alpha,
    // STLVertex3* pVertex0, STLVertex3* pVertex1, STLVertex3* pResultVertex ) = 0;
    //	// clip face, which consist of vertices in projection space
    //	virtual int ClipFace( STLVertex1* vertices, BYTE* clips, WORD* indices, int& nVertices, int&
    // nIndices ) = 0; 	virtual int ClipFace( STLVertex3* vertices, BYTE* clips, WORD* indices, int&
    // nVertices, int& nIndices ) = 0;
    // bounding volume recalculation functions
    virtual void CalculateBoundingVolume(SBoundingVolume *lpBV, SXYZW4 *plots, int nNumPlots);
    virtual void CalculateBoundingVolume(SBoundingVolume *lpBV, SXYZW *plots, int nNumPlots);

    // ***************** TRANSFORMATION PIPELINE FUNCTIONS *****************************
    // transform vertices from SXYZW4 to SXYZW4 without clipping chek
    virtual int TransformVertices4NC(const D3DMATRIX &matrix, SXYZW4 *lpSrc, SXYZW4 *lpDst,
                                     const int nCount);
    // transform vertices from SXYZW4 to SXYZW4 with clipping chek
    virtual int TransformVertices4C(const D3DMATRIX &matrix, SXYZW4 *lpSrc, SXYZW4 *lpDst,
                                    BYTE *pClip, const int nCount);
    // transform vertices from SXYZW4 to SXYZW without clipping chek
    virtual int TransformVerticesNC(const D3DMATRIX &matrix, SXYZW4 *lpSrc, SXYZW *lpDst,
                                    const int nCount);
    // transform vertices from SXYZW4 to SXYZW with clipping chek
    virtual int TransformVerticesC(const D3DMATRIX &matrix, SXYZW4 *lpSrc, SXYZW *lpDst,
                                   BYTE *pClip, const int nCount);
    // transform 2d particled and store results in special format
    virtual int Transform2DParticles(const D3DMATRIX &matrix, SXYZW *pSrc, D3DTLVERTEX *pDst,
                                     BYTE *pClip, const int nCount);
    // simply multiply 'src' (SXYZW4) vertices to 'matrix' and store results in 'dst' (SXYZW4)
    // vertices w/o homogenezation and w/o clip check
    virtual int SimplyTransform4NC(const D3DMATRIX &matrix, SXYZW4 *pSrc, SXYZW4 *pDst, int nCount);
    // simply multiply 'src' (SXYZW4) vertices to 'matrix' and store results in 'dst' (SXYZW)
    // vertices w/o homogenezation and w/o clip check
    virtual int SimplyTransformNC(const D3DMATRIX &matrix, SXYZW4 *pSrc, SXYZW *pDst, int nCount);
    // simply multiply 'src' (SXYZW4) vertices to 'matrix' and store results in 'dst' (SXYZW4)
    // vertices w/o homogenezation and with clip check
    virtual int SimplyTransform4C(const D3DMATRIX &matrix, SXYZW4 *pSrc, SXYZW4 *pDst, BYTE *pClip,
                                  int nCount);
    // simply multiply 'src' (SXYZW4) vertices to 'matrix' and store results in 'dst' (SXYZW)
    // vertices w/o homogenezation and with clip check
    virtual int SimplyTransformC(const D3DMATRIX &matrix, SXYZW4 *pSrc, SXYZW *pDst, BYTE *pClip,
                                 int nCount);
    // transform shadow on plane ZERO and convert x and y to int
    virtual int TransformShadow(const SHMatrix &matrix, SXYZW4 *lpSrc, POINT *lpDst, int nCount);
    virtual int TransformShadow(const SHMatrix &matrix, SXYZW4 *lpSrc, CPlot2D *lpDst, int nCount,
                                CTRect<float> *pRect);
    // transform vertices from projection space to screen w/o care about 'wp'
    virtual void CastProjectionToScreenNC(D3DTLVERTEX *vertices, int nVertices);
    // transform vertices from projection space to screen with care about 'wp'
    virtual void CastProjectionToScreenC(D3DTLVERTEX *vertices, int nVertices);
    // transform plots from projection space to screen w/o care about 'wp'
    virtual void CastProjectionToScreenNC(SXYZW *plots, int nPlots);
    // transform plotsfrom projection space to screen with care about 'wp'
    virtual void CastProjectionToScreenC(SXYZW *plots, int nPlots);
    // add fog to vertices
    virtual void AddFog(D3DTLVERTEX *vertices, int nSize);
    // clip face, which consist of vertices in projection space
    virtual int ClipFace(D3DTLVERTEX *vertices, BYTE *clips, WORD *indices, int &nVertices,
                         int &nIndices);
    virtual void ClipFaces(D3DTLVERTEX *vertices, BYTE *cClips, WORD *wInputIndices,
                           WORD *wOutputIndices, int &nNumVertices, int &nNumIndices);
    // interpolate two vertices with coeff 'alpha'
    virtual void MakeVertex(const float alpha, D3DTLVERTEX *pVertex0, D3DTLVERTEX *pVertex1,
                            D3DTLVERTEX *pResultVertex);

    // ********************** LIGHTING PIPELINE FUNCTIONS ******************************
    // general function to lit vertices with point lights (with diffuse and specular)
    virtual void LightVerticesGeneralPt(SXYZW4 *plots, SXYZW4 *norms, LPDWORD colors,
                                        LPDWORD speculars, SGvNvComponents *components,
                                        int nComponents, CLight **lights, int nLights,
                                        float fpMaterialPower, const D3DVECTOR &dvEmissive,
                                        const CPlot3D &dc, const BYTE cFog, DWORD bFlatPointLights);
    // lit vertices with diffuse components only (with point lights)
    virtual void LightVerticesDfPt(SXYZW4 *plots, SXYZW4 *norms, LPDWORD colors,
                                   SGvNvComponents *components, int nComponents, CLight **lights,
                                   int nLights, const D3DVECTOR &dvEmissive);
    // lit vertices with specular components only (with point lights)
    virtual void LightVerticesSpPt(SXYZW4 *plots, SXYZW4 *norms, LPDWORD speculars,
                                   SGvNvComponents *components, int nComponents, CLight **lights,
                                   int nLights, float fpMaterialPower, const D3DVECTOR &dvEmissive,
                                   const CPlot3D &dc, const BYTE cFog = 255);
    // general function to lit vertices without point lights (with diffuse and specular)
    virtual void LightVerticesGeneral(SXYZW4 *plots, SXYZW4 *norms, LPDWORD colors,
                                      LPDWORD speculars, SGvNvComponents *components,
                                      int nComponents, float fpMaterialPower,
                                      const D3DVECTOR &dvEmissive, const CPlot3D &dc,
                                      const BYTE cFog = 255);
    // lit vertices with diffuse components only (without point lights)
    virtual void LightVerticesDf(SXYZW4 *plots, SXYZW4 *norms, LPDWORD colors,
                                 SGvNvComponents *components, int nComponents,
                                 const D3DVECTOR &dvEmissive);
    // lit vertices with specular components only (without point lights)
    virtual void LightVerticesSp(SXYZW4 *plots, SXYZW4 *norms, LPDWORD speculars,
                                 SGvNvComponents *components, int nComponents,
                                 float fpMaterialPower, const D3DVECTOR &dvEmissive,
                                 const CPlot3D &dc, const BYTE cFog = 255);
};
/////////////////////////////////////////////////////////////////////////////////////
