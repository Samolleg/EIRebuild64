#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Figure.h"
#include "figFormat.h"

#include "Polygon.h"
#include "Quaternion.h"
#include "Object.h"

#include "DXGeometryProcessor.h"
#include "GraphicsProcessor.h"
#include "World.h"
#include "Renderer.h"
/////////////////////////////////////////////////////////////////////////////////////
// class CFigure functions and methods
//////////////////////////////////////////////////////////////////////////////////////
CFigure::~CFigure()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
void CFigure::operator=(const CFigure &A)
{
    CBase3D::operator=(A);

    name = A.name;
    /*
            plots4.Copy( A.plots4 );
            norms4.Copy( A.norms4 );
            colors.Copy( A.colors );
    */
    components = A.components;
    nComponents = A.nComponents;
    lightcomponents = A.lightcomponents;
    nLightComponents = A.nLightComponents;
    indices = A.indices;
    nIndices = A.nIndices;
    maps = A.maps;
    nMaps = A.nMaps;

    fpUShift = 0;
    fpVShift = 0;

    fRedPower = A.fRedPower;

    bbCenter = A.bbCenter;
    bbMin = A.bbMin;
    bbMax = A.bbMax;
    radius = A.radius;

    tidMaterialTypeID = A.tidMaterialTypeID;
    tidTextureTypeID = A.tidTextureTypeID;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CFigure::Create()
{
    radius = 0;
    lpFigureSpecific = 0;
    ResetAllFlags();

    tidMaterialTypeID = TID_EMPTY;
    tidTextureTypeID = TID_EMPTY;

    // plots and relative data
    plots4 = 0;
    nNumPlots = 0;
    norms4 = 0;
    nNumNorms = 0;
    // ready-to-use information
    maps = 0;
    nMaps = 0;
    components = 0;
    nComponents = 0;
    lightcomponents = 0;
    nLightComponents = 0;
    indices = 0;
    nIndices = 0;
    //
    fpUShift = 0;
    fpVShift = 0;
    //
    fRedPower = 0;
    //
    dwDefaultColor = RGBA_MAKE(255, 255, 255, 255);
    dwDefaultSpecular = RGBA_MAKE(255, 255, 255, 255);
    //
    memset(&bv, 0, sizeof(bv));

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CFigure::Destroy()
{
    name.Empty();
    szMask.Empty();
    //
    morphs.RemoveAll();

    plots4 = 0;
    nNumPlots = 0;
    norms4 = 0;
    nNumNorms = 0;

    colors.RemoveAll();
    speculars.RemoveAll();

    components = NULL;
    nComponents = 0;
    lightcomponents = NULL;
    nLightComponents = 0;
    indices = NULL;
    nIndices = 0;
    maps = NULL;
    nMaps = 0;
    //
    tidMaterialTypeID = TID_EMPTY;
    tidTextureTypeID = TID_EMPTY;
    //
    SetFigureSpecific(NULL);
    //
    ResetAllFlags();
}
/////////////////////////////////////////////////////////////////////////////////////
// void CFigure::SetFigureSpecific( LPGAMEBASE lpNewFigureSpecific )
// sets figure specific class and registering it
void CFigure::SetFigureSpecific(LPGAMEBASE lpNewFigureSpecific)
{
    if (lpFigureSpecific)
    {
        lpFigureSpecific->RegisterFigure(NULL);
        lpFigureSpecific->Release();
    }

    lpFigureSpecific = (CFigureSpecific *)lpNewFigureSpecific;

    if (lpFigureSpecific)
    {
        lpFigureSpecific->RegisterFigure(this);
        lpFigureSpecific->AddRef();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CFigure::SetObject3D(LPGAMEBASE lpNewObject3D)
{
    lpObject = lpNewObject3D;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CFigure::RotateForDraw( const CQuaternion& A )
// prepare 'afters' from 'plots' via precalculated rotation matrix
void CFigure::RotateForDraw(const CQuaternion &A)
{
    CPlot3D n;
    // rotate this figure
    PrepareQuaternion(A);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CFigure::PrepareD3DRotateMatrix( const CQuaternion& q )
// preparing Direct3D matrix via fillin this from quaternions's matrix and cp
void CFigure::PrepareD3DRotateMatrix(const CQuaternion &q)
{
    Identity(*reinterpret_cast<SHMatrix *>(&d3dmatrix));
    q.DecompEulerMatrix(d3dmatrix);
    d3dmatrix._41 = respectCP.GetX();
    d3dmatrix._42 = respectCP.GetY();
    d3dmatrix._43 = respectCP.GetZ();
}
/////////////////////////////////////////////////////////////////////////////////////
// void CFigure::PrepareForDraw( const CQuaternion& q, const CPlot3D& sp )
// preparation for drawing process : rotation matrix, shift, etc.
void CFigure::PrepareForDraw(const CQuaternion &q, const CPlot3D &sp)
{
    //	PrepareQuaternion( q );
    //	respectCP = q.Rotate( cp ) + sp;
    CBase3D::PrepareForDraw(q, sp);
    PrepareD3DRotateMatrix(rotation1);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CFigure::Save( CFile& file ) const
// just save
void CFigure::Save(CFile &file) const
{
    DWORD num;
    // save name
    num = name.GetLength();
    file.Write(&num, sizeof(num));
    file.Write(name, num);
    // save plots
    num = nNumPlots;
    file.Write(&num, sizeof(num));
    file.Write(plots4, sizeof(SXYZW4) * num);
    // save normales
    num = nNumNorms;
    file.Write(&num, sizeof(num));
    file.Write(norms4, sizeof(SXYZW4) * num);
    // save mapping coords
    num = nMaps;
    file.Write(&num, sizeof(num));
    file.Write(maps, sizeof(CPlot2D) * num);
    // save faces information
    num = 0; // faces1.GetSize();
    file.Write(&num, sizeof(num));
    //	file.Write( faces1.GetData(), sizeof(CFace)*num );
    // save components index array
    num = nComponents;
    file.Write(&num, sizeof(num));
    file.Write(components, sizeof(SVertexComponents) * num);
    // save light conponents index array
    num = nLightComponents;
    file.Write(&num, sizeof(num));
    file.Write(lightcomponents, sizeof(SGvNvComponents) * num);
    // save indices of polygon vertex in tlvertex array, created with 'components' information
    num = nIndices;
    file.Write(&num, sizeof(num));
    file.Write(indices, sizeof(WORD) * num);
    // save bounding volumes information
    file.Write(&bbCenter, sizeof(CPlot3D));
    file.Write(&bbMin, sizeof(CPlot3D));
    file.Write(&bbMax, sizeof(CPlot3D));
    file.Write(&radius, sizeof(float));
    // save ID information
    file.Write(&tidMaterialTypeID, sizeof(tidMaterialTypeID));
    file.Write(&tidTextureTypeID, sizeof(tidTextureTypeID));
}
/////////////////////////////////////////////////////////////////////////////////////
// void CFigure::Load( CFile& file )
// just load
void CFigureLoaded::Load(CFile &file)
{
    DWORD num;
    // load name
    file.Read(&num, sizeof(num));
    file.Read(name.GetBufferSetLength(num), num);
    // load plots
    file.Read(&num, sizeof(num));
    plots4.SetSize(num);
    file.Read(plots4.GetData(), sizeof(SXYZW4) * num);
    // load normales
    file.Read(&num, sizeof(num));
    norms4.SetSize(num);
    file.Read(norms4.GetData(), sizeof(SXYZW4) * num);
    // load mapping coords
    file.Read(&num, sizeof(num));
    maps.SetSize(num);
    file.Read(maps.GetData(), sizeof(CPlot2D) * num);
    // load faces information
    file.Read(&num, sizeof(num));
    //	faces1.SetSize( num );
    //	file.Read( faces1.GetData(), sizeof(CFace)*num );
    // load components index array
    file.Read(&num, sizeof(num));
    components.SetSize(num);
    file.Read(components.GetData(), sizeof(SVertexComponents) * num);
    // load light conponents index array
    file.Read(&num, sizeof(num));
    lightcomponents.SetSize(num);
    file.Read(lightcomponents.GetData(), sizeof(SGvNvComponents) * num);
    // load indices of polygon vertex in tlvertex array, created with 'components' information
    file.Read(&num, sizeof(num));
    indices.SetSize(num);
    file.Read(indices.GetData(), sizeof(WORD) * num);
    // load bounding volumes information
    file.Read(&bbCenter, sizeof(CPlot3D));
    file.Read(&bbMin, sizeof(CPlot3D));
    file.Read(&bbMax, sizeof(CPlot3D));
    file.Read(&radius, sizeof(float));
    // load ID information
    file.Read(&tidMaterialTypeID, sizeof(tidMaterialTypeID));
    file.Read(&tidTextureTypeID, sizeof(tidTextureTypeID));
}
/////////////////////////////////////////////////////////////////////////////////////
// void CFigure::RecalcBoundingVolumeWithRotation( float& maxx, float& maxy, float& maxz, float&
// minx, float& miny, float& minz ) recalculate bounding box with current rotation
void CFigure::RecalcBoundingBoxWithRotation(float &maxx, float &maxy, float &maxz, float &minx,
                                            float &miny, float &minz)
{
    if (fabs2(cp) > 1e8f)
        return;
    D3DMATRIX matrix;
    memset(&matrix, 0, sizeof(matrix));
    CBase3D::PrepareD3DRotateMatrix(matrix);
    const D3DVALUE _11 = matrix._11, _12 = matrix._12, _13 = matrix._13, _14 = matrix._14;
    const D3DVALUE _21 = matrix._21, _22 = matrix._22, _23 = matrix._23, _24 = matrix._24;
    const D3DVALUE _31 = matrix._31, _32 = matrix._32, _33 = matrix._33, _34 = matrix._34;
    const D3DVALUE _41 = matrix._41, _42 = matrix._42, _43 = matrix._43, _44 = matrix._44;
    float x, y, z, xp, yp, zp, wp;
    // AAA
    x = bv.ptCenter.x + bv.ptMax.x;
    y = bv.ptCenter.y + bv.ptMax.y;
    z = bv.ptCenter.z + bv.ptMax.z;
    xp = _11 * x + _21 * y + _31 * z + _41;
    yp = _12 * x + _22 * y + _32 * z + _42;
    zp = _13 * x + _23 * y + _33 * z + _43;
    wp = _14 * x + _24 * y + _34 * z + _44;
    maxx = Max(maxx, xp);
    minx = Min(minx, xp);
    maxy = Max(maxy, yp);
    miny = Min(miny, yp);
    maxz = Max(maxz, zp);
    minz = Min(minz, zp);
    // AAI
    x = bv.ptCenter.x + bv.ptMax.x;
    y = bv.ptCenter.y + bv.ptMax.y;
    z = bv.ptCenter.z + bv.ptMin.z;
    xp = _11 * x + _21 * y + _31 * z + _41;
    yp = _12 * x + _22 * y + _32 * z + _42;
    zp = _13 * x + _23 * y + _33 * z + _43;
    wp = _14 * x + _24 * y + _34 * z + _44;
    maxx = Max(maxx, xp);
    minx = Min(minx, xp);
    maxy = Max(maxy, yp);
    miny = Min(miny, yp);
    maxz = Max(maxz, zp);
    minz = Min(minz, zp);
    // AIA
    x = bv.ptCenter.x + bv.ptMax.x;
    y = bv.ptCenter.y + bv.ptMin.y;
    z = bv.ptCenter.z + bv.ptMax.z;
    xp = _11 * x + _21 * y + _31 * z + _41;
    yp = _12 * x + _22 * y + _32 * z + _42;
    zp = _13 * x + _23 * y + _33 * z + _43;
    wp = _14 * x + _24 * y + _34 * z + _44;
    maxx = Max(maxx, xp);
    minx = Min(minx, xp);
    maxy = Max(maxy, yp);
    miny = Min(miny, yp);
    maxz = Max(maxz, zp);
    minz = Min(minz, zp);
    // AII
    x = bv.ptCenter.x + bv.ptMax.x;
    y = bv.ptCenter.y + bv.ptMin.y;
    z = bv.ptCenter.z + bv.ptMin.z;
    xp = _11 * x + _21 * y + _31 * z + _41;
    yp = _12 * x + _22 * y + _32 * z + _42;
    zp = _13 * x + _23 * y + _33 * z + _43;
    wp = _14 * x + _24 * y + _34 * z + _44;
    maxx = Max(maxx, xp);
    minx = Min(minx, xp);
    maxy = Max(maxy, yp);
    miny = Min(miny, yp);
    maxz = Max(maxz, zp);
    minz = Min(minz, zp);
    // IAA
    x = bv.ptCenter.x + bv.ptMin.x;
    y = bv.ptCenter.y + bv.ptMax.y;
    z = bv.ptCenter.z + bv.ptMax.z;
    xp = _11 * x + _21 * y + _31 * z + _41;
    yp = _12 * x + _22 * y + _32 * z + _42;
    zp = _13 * x + _23 * y + _33 * z + _43;
    wp = _14 * x + _24 * y + _34 * z + _44;
    maxx = Max(maxx, xp);
    minx = Min(minx, xp);
    maxy = Max(maxy, yp);
    miny = Min(miny, yp);
    maxz = Max(maxz, zp);
    minz = Min(minz, zp);
    // IAI
    x = bv.ptCenter.x + bv.ptMin.x;
    y = bv.ptCenter.y + bv.ptMax.y;
    z = bv.ptCenter.z + bv.ptMin.z;
    xp = _11 * x + _21 * y + _31 * z + _41;
    yp = _12 * x + _22 * y + _32 * z + _42;
    zp = _13 * x + _23 * y + _33 * z + _43;
    wp = _14 * x + _24 * y + _34 * z + _44;
    maxx = Max(maxx, xp);
    minx = Min(minx, xp);
    maxy = Max(maxy, yp);
    miny = Min(miny, yp);
    maxz = Max(maxz, zp);
    minz = Min(minz, zp);
    // IIA
    x = bv.ptCenter.x + bv.ptMin.x;
    y = bv.ptCenter.y + bv.ptMin.y;
    z = bv.ptCenter.z + bv.ptMax.z;
    xp = _11 * x + _21 * y + _31 * z + _41;
    yp = _12 * x + _22 * y + _32 * z + _42;
    zp = _13 * x + _23 * y + _33 * z + _43;
    wp = _14 * x + _24 * y + _34 * z + _44;
    maxx = Max(maxx, xp);
    minx = Min(minx, xp);
    maxy = Max(maxy, yp);
    miny = Min(miny, yp);
    maxz = Max(maxz, zp);
    minz = Min(minz, zp);
    // III
    x = bv.ptCenter.x + bv.ptMin.x;
    y = bv.ptCenter.y + bv.ptMin.y;
    z = bv.ptCenter.z + bv.ptMin.z;
    xp = _11 * x + _21 * y + _31 * z + _41;
    yp = _12 * x + _22 * y + _32 * z + _42;
    zp = _13 * x + _23 * y + _33 * z + _43;
    wp = _14 * x + _24 * y + _34 * z + _44;
    maxx = Max(maxx, xp);
    minx = Min(minx, xp);
    maxy = Max(maxy, yp);
    miny = Min(miny, yp);
    maxz = Max(maxz, zp);
    minz = Min(minz, zp);
}
/////////////////////////////////////////////////////////////////////////////////////
void CFigure::SetMorphs(CPlot3DArray &morphings)
{
    morphs.Copy(morphings);
}
/////////////////////////////////////////////////////////////////////////////////////
static CXYZW4Array plots41;
DWORD CFigure::ProjectShadow(CDXGeometryProcessor *pGPU, const SHMatrix &matrix, CPlot2D *buffer,
                             CTRect<float> *pRect)
{
    SXYZW4 *plots = plots4;
    SXYZW4 *renderPlots = plots;
    // take morphs in account
    if (morphs.GetSize() != 0)
    {
        int hindex, lindex;

        plots41.SetSize(nNumPlots); // ARRAY BOUNDS CHECK
        for (int i = 0; i < plots41.GetSize() * 4; ++i)
        {
            hindex = i >> 2;
            lindex = i & 0x3;
            // CRAP - wrong work with morphs - 'morphs.GetSize()' are not multiple to 4
            if (i >= morphs.GetSize())
            {
                plots41[hindex].x[lindex] = plots[hindex].x[lindex];
                plots41[hindex].y[lindex] = plots[hindex].y[lindex];
                plots41[hindex].z[lindex] = plots[hindex].z[lindex];
            }
            else
            {
                plots41[hindex].x[lindex] = plots[hindex].x[lindex] + morphs[i].x;
                plots41[hindex].y[lindex] = plots[hindex].y[lindex] + morphs[i].y;
                plots41[hindex].z[lindex] = plots[hindex].z[lindex] + morphs[i].z;
            }
        }
        renderPlots = plots41.GetData();
    }
    // transform resulting shadow
    pGPU->GetGeometryProcessor()->TransformShadow(matrix, renderPlots, buffer, nNumPlots, pRect);

    return nNumPlots * 4;
}
/////////////////////////////////////////////////////////////////////////////////////
DWORD CFigure::RenderShadow(CGraphicsProcessor *pGrPU, CPoint *shadowpoints,
                            SShadowTextureRenderParam &rp)
{
    static CSimplyArray<CPoint, CPoint &> maps255;
    int i;

    maps255.SetSize(nMaps); // ARRAY BOUNDS CHECK
    // convert maps to range [0...255]
    for (i = 0; i < nMaps; ++i)
    {
        maps255[i].x = MINT(maps[i].x * 255);
        maps255[i].y = MINT(maps[i].y * 255);
    }
    // rasterize
    for (i = 0; i < nIndices; i += 3)
    {
        Copy8Bytes(&(rp.x1), &(shadowpoints[components[indices[i + 0]].gv].x));
        Copy8Bytes(&(rp.tu1), &(maps255[components[indices[i + 0]].tv].x));
        Copy8Bytes(&(rp.x2), &(shadowpoints[components[indices[i + 1]].gv].x));
        Copy8Bytes(&(rp.tu2), &(maps255[components[indices[i + 1]].tv].x));
        Copy8Bytes(&(rp.x3), &(shadowpoints[components[indices[i + 2]].gv].x));
        Copy8Bytes(&(rp.tu3), &(maps255[components[indices[i + 2]].tv].x));
        if (rp.lpSrcTexture)
            pGrPU->RasterizeTriangleWithAlpha(rp);
        else
            pGrPU->RasterizeTriangle(rp);
    }

    return nNumPlots * 4;
}
/////////////////////////////////////////////////////////////////////////////////////
void CFigure::LoadOldBoundingVolume(LPBYTE &lpData, float fpStrength, float fpGrace, float fpTall)
{
    CPlot3D bbCenter, bbMin, bbMax;
    CPlot3D ptSmall, ptTall;
    CPlot3D *ptSmalls;
    CPlot3D *ptTalls;
    float valTall;
    float *smalls;
    float *talls;
    float radius;
    // process bounding volumes ( small: weak, strong, fat; tall: weak, strong, fat )
    // bbCenter
    ptSmalls = (CPlot3D *)lpData;
    ptTalls = ((CPlot3D *)lpData) + 3;
    bbCenter = ptSmalls[0] + (ptSmalls[1] - ptSmalls[0]) * fpStrength;
    bbCenter += (ptSmalls[2] - bbCenter) * fpGrace;
    ptTall = ptTalls[0] + (ptTalls[1] - ptTalls[0]) * fpStrength;
    ptTall += (ptTalls[2] - ptTall) * fpGrace;
    bbCenter += (ptTall - bbCenter) * fpTall;
    lpData += 6 * sizeof(CPlot3D);
    // bbMin
    ptSmalls = (CPlot3D *)lpData;
    ptTalls = ((CPlot3D *)lpData) + 3;
    bbMin = ptSmalls[0] + (ptSmalls[1] - ptSmalls[0]) * fpStrength;
    bbMin += (ptSmalls[2] - bbMin) * fpGrace;
    ptTall = ptTalls[0] + (ptTalls[1] - ptTalls[0]) * fpStrength;
    ptTall += (ptTalls[2] - ptTall) * fpGrace;
    bbMin += (ptTall - bbMin) * fpTall;
    lpData += 6 * sizeof(CPlot3D);
    // bbMax
    ptSmalls = (CPlot3D *)lpData;
    ptTalls = ((CPlot3D *)lpData) + 3;
    bbMax = ptSmalls[0] + (ptSmalls[1] - ptSmalls[0]) * fpStrength;
    bbMax += (ptSmalls[2] - bbMax) * fpGrace;
    ptTall = ptTalls[0] + (ptTalls[1] - ptTalls[0]) * fpStrength;
    ptTall += (ptTalls[2] - ptTall) * fpGrace;
    bbMax += (ptTall - bbMax) * fpTall;
    lpData += 6 * sizeof(CPlot3D);
    // radius
    smalls = (float *)lpData;
    talls = ((float *)lpData) + 3;
    radius = smalls[0] + (smalls[1] - smalls[0]) * fpStrength;
    radius += (smalls[2] - radius) * fpGrace;
    valTall = talls[0] + (talls[1] - talls[0]) * fpStrength;
    valTall += (talls[2] - valTall) * fpGrace;
    radius += (valTall - radius) * fpTall;
    lpData += 6 * sizeof(float);
    // set this data to bounding volume
    bv.SetCenter(bbCenter);
    bv.SetRadius(radius);
    bv.SetBox(bbMin, bbMax);
}
/////////////////////////////////////////////////////////////////////////////////////
void CFigure::JustLoadBoundingVolume(LPBYTE &lpData, float fpStrength, float fpGrace, float fpTall,
                                     DWORD dwSignature)
{
    if (dwSignature == FIG_SIGNATURE_FIG8)
    {
        // process bounding volume
        CPlot3D ptTemp[2];
        // center
        CPlot3D *plots = reinterpret_cast<CPlot3D *>(lpData);
        ptTemp[0] = plots[0] + fpStrength * (plots[1] - plots[0]);
        ptTemp[1] = plots[2] + fpStrength * (plots[3] - plots[2]);
        bbCenter = ptTemp[0] + fpGrace * (ptTemp[1] - ptTemp[0]);
        plots = reinterpret_cast<CPlot3D *>(lpData + 4 * sizeof(CPlot3D));
        ptTemp[0] = plots[0] + fpStrength * (plots[1] - plots[0]);
        ptTemp[1] = plots[2] + fpStrength * (plots[3] - plots[2]);
        ptTemp[0] += fpGrace * (ptTemp[1] - ptTemp[0]);
        bbCenter += fpTall * (ptTemp[0] - bbCenter);
        //
        lpData += 8 * sizeof(CPlot3D);
        // min
        plots = reinterpret_cast<CPlot3D *>(lpData);
        ptTemp[0] = plots[0] + fpStrength * (plots[1] - plots[0]);
        ptTemp[1] = plots[2] + fpStrength * (plots[3] - plots[2]);
        bbMin = ptTemp[0] + fpGrace * (ptTemp[1] - ptTemp[0]);
        plots = reinterpret_cast<CPlot3D *>(lpData + 4 * sizeof(CPlot3D));
        ptTemp[0] = plots[0] + fpStrength * (plots[1] - plots[0]);
        ptTemp[1] = plots[2] + fpStrength * (plots[3] - plots[2]);
        ptTemp[0] += fpGrace * (ptTemp[1] - ptTemp[0]);
        bbMin += fpTall * (ptTemp[0] - bbMin);
        //
        lpData += 8 * sizeof(CPlot3D);
        // max
        plots = reinterpret_cast<CPlot3D *>(lpData);
        ptTemp[0] = plots[0] + fpStrength * (plots[1] - plots[0]);
        ptTemp[1] = plots[2] + fpStrength * (plots[3] - plots[2]);
        bbMax = ptTemp[0] + fpGrace * (ptTemp[1] - ptTemp[0]);
        plots = reinterpret_cast<CPlot3D *>(lpData + 4 * sizeof(CPlot3D));
        ptTemp[0] = plots[0] + fpStrength * (plots[1] - plots[0]);
        ptTemp[1] = plots[2] + fpStrength * (plots[3] - plots[2]);
        ptTemp[0] += fpGrace * (ptTemp[1] - ptTemp[0]);
        bbMax += fpTall * (ptTemp[0] - bbMax);
        //
        lpData += 8 * sizeof(CPlot3D);
        // radius
        float fpTemp[2];
        float *rds = reinterpret_cast<float *>(lpData);
        fpTemp[0] = rds[0] + fpStrength * (rds[1] - rds[0]);
        fpTemp[1] = rds[2] + fpStrength * (rds[3] - rds[2]);
        radius = fpTemp[0] + fpGrace * (fpTemp[1] - fpTemp[0]);
        rds = reinterpret_cast<float *>(lpData + 4 * sizeof(float));
        fpTemp[0] = rds[0] + fpStrength * (rds[1] - rds[0]);
        fpTemp[1] = rds[2] + fpStrength * (rds[3] - rds[2]);
        fpTemp[0] += fpGrace * (fpTemp[1] - fpTemp[0]);
        radius += fpTall * (fpTemp[0] - radius);
        //
        lpData += 8 * sizeof(float);
        // set data
        bv.SetCenter(bbCenter);
        bv.SetRadius(radius);
        bv.SetBox(bbMin, bbMax);
    }
    else
    {
        bbCenter = *reinterpret_cast<CPlot3D *>(lpData);
        lpData += sizeof(CPlot3D);
        bbMin = *reinterpret_cast<CPlot3D *>(lpData);
        lpData += sizeof(CPlot3D);
        bbMax = *reinterpret_cast<CPlot3D *>(lpData);
        lpData += sizeof(CPlot3D);
        radius = *reinterpret_cast<float *>(lpData);
        lpData += sizeof(float);
        // set data
        bv.SetCenter(bbCenter);
        bv.SetRadius(radius);
        bv.SetBox(bbMin, bbMax);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// data format:
// NOTE: all arrays have DWORD before data <- counter (number of elements)
// tidMaterialTypeID                                 <= ETypeIDs
// tidTextureTypeID                                  <= ETypeIDs
// bbCenter, bbMin, bbMax                            <= each CPlot3D ( [weak, strong, fat] for
// small, [weak, strong, fat] for tall ) radius                                            <= float
// ( [weak, strong, fat] for small, [weak, strong, fat] for tall ) plots4 (array of )
//   4x (6 times), 4y (6 times), 4z (6 times)        <= floats ( [weak, strong, fat] for small,
//   [weak, strong, fat] for tall )
// norms4 (array of )
//   4x, 4y, 4z, 4w                                  <= floats
// maps (array of )                                  <= CPlot2D
// faces1 (array of )                                <= CFace
// components (array of )                            <= SVertexComponents
// lightcomponents (array of )                       <= SGvNvComponents
// indices (array of )                               <= WORD
void CFigure::LoadD3DComplex(LPBYTE &lpData, float fpStrength, float fpGrace, float fpTall)
{
    ASSERT(0);
    // set flag, that this figure loaded
    SetFlag(FIG_LOADED);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CFigureLoaded::LoadD3DComplex( LPBYTE& lpData, float fpStrength, float fpGrace, float fpTall
// ) load all data to the internal structures
void CFigureLoaded::LoadD3DComplex(LPBYTE &lpData, float fpStrength, float fpGrace, float fpTall)
{
    if (CheckFlag(FIG_LOADED))
        return;
    //
    SFigureFormatHeader hdr;
    memcpy(&hdr, lpData, sizeof(hdr));
    if ((hdr.dwSignature & FIG_SIGNATURE_MASK) == FIG_SIGNATURE_FIGN)
        LoadNewFigure(lpData, fpStrength, fpGrace, fpTall);
    else
        LoadOldFigure(lpData, fpStrength, fpGrace, fpTall);
    //
    SetFlag(FIG_LOADED);
}
/////////////////////////////////////////////////////////////////////////////////////
// load all data to the internal structures
void CFigureLoaded::LoadNewFigure(LPBYTE &lpData, float fpStrength, float fpGrace, float fpTall)
{
    if (CheckFlag(FIG_LOADED))
        return;
    //
    SFigureFormatHeader hdr;
    memcpy(&hdr, lpData, sizeof(hdr));
    lpData += sizeof(hdr);
    // process material and texture type IDs
    SetMaterialTypeID(*((ETypeIDs *)lpData));
    lpData += sizeof(ETypeIDs);
    SetTextureTypeID(*((ETypeIDs *)lpData));
    lpData += sizeof(ETypeIDs);
    // process bounding volume
    JustLoadBoundingVolume(lpData, fpStrength, fpGrace, fpTall, hdr.dwSignature);
    // process plots4
    plots4.SetSize(hdr.dwNumVertices);
    if (hdr.dwSignature == FIG_SIGNATURE_FIG8)
    {
        for (int i = 0; i < int(hdr.dwNumVertices); ++i)
        {
            float temp[2][4];
            // assemble 'x' components
            // strength
            float *weaks = reinterpret_cast<float *>(lpData);
            float *strongs = reinterpret_cast<float *>(lpData + 4 * sizeof(float));
            temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            weaks = reinterpret_cast<float *>(lpData + 8 * sizeof(float));
            strongs = reinterpret_cast<float *>(lpData + 12 * sizeof(float));
            temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            // grace
            plots4[i].x[0] = temp[0][0] + fpGrace * (temp[1][0] - temp[0][0]);
            plots4[i].x[1] = temp[0][1] + fpGrace * (temp[1][1] - temp[0][1]);
            plots4[i].x[2] = temp[0][2] + fpGrace * (temp[1][2] - temp[0][2]);
            plots4[i].x[3] = temp[0][3] + fpGrace * (temp[1][3] - temp[0][3]);
            // strength
            weaks = reinterpret_cast<float *>(lpData + 16 * sizeof(float));
            strongs = reinterpret_cast<float *>(lpData + 20 * sizeof(float));
            temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            weaks = reinterpret_cast<float *>(lpData + 24 * sizeof(float));
            strongs = reinterpret_cast<float *>(lpData + 28 * sizeof(float));
            temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            // grace
            temp[0][0] += fpGrace * (temp[1][0] - temp[0][0]);
            temp[0][1] += fpGrace * (temp[1][1] - temp[0][1]);
            temp[0][2] += fpGrace * (temp[1][2] - temp[0][2]);
            temp[0][3] += fpGrace * (temp[1][3] - temp[0][3]);
            // tall => all 'x' components are complete
            plots4[i].x[0] += fpTall * (temp[0][0] - plots4[i].x[0]);
            plots4[i].x[1] += fpTall * (temp[0][1] - plots4[i].x[1]);
            plots4[i].x[2] += fpTall * (temp[0][2] - plots4[i].x[2]);
            plots4[i].x[3] += fpTall * (temp[0][3] - plots4[i].x[3]);
            //
            lpData += 8 * 4 * sizeof(float);
            //
            // assemble 'y' components
            // strength
            weaks = reinterpret_cast<float *>(lpData);
            strongs = reinterpret_cast<float *>(lpData + 4 * sizeof(float));
            temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            weaks = reinterpret_cast<float *>(lpData + 8 * sizeof(float));
            strongs = reinterpret_cast<float *>(lpData + 12 * sizeof(float));
            temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            // grace
            plots4[i].y[0] = temp[0][0] + fpGrace * (temp[1][0] - temp[0][0]);
            plots4[i].y[1] = temp[0][1] + fpGrace * (temp[1][1] - temp[0][1]);
            plots4[i].y[2] = temp[0][2] + fpGrace * (temp[1][2] - temp[0][2]);
            plots4[i].y[3] = temp[0][3] + fpGrace * (temp[1][3] - temp[0][3]);
            // strength
            weaks = reinterpret_cast<float *>(lpData + 16 * sizeof(float));
            strongs = reinterpret_cast<float *>(lpData + 20 * sizeof(float));
            temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            weaks = reinterpret_cast<float *>(lpData + 24 * sizeof(float));
            strongs = reinterpret_cast<float *>(lpData + 28 * sizeof(float));
            temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            // grace
            temp[0][0] += fpGrace * (temp[1][0] - temp[0][0]);
            temp[0][1] += fpGrace * (temp[1][1] - temp[0][1]);
            temp[0][2] += fpGrace * (temp[1][2] - temp[0][2]);
            temp[0][3] += fpGrace * (temp[1][3] - temp[0][3]);
            // tall => all 'y' components are complete
            plots4[i].y[0] += fpTall * (temp[0][0] - plots4[i].y[0]);
            plots4[i].y[1] += fpTall * (temp[0][1] - plots4[i].y[1]);
            plots4[i].y[2] += fpTall * (temp[0][2] - plots4[i].y[2]);
            plots4[i].y[3] += fpTall * (temp[0][3] - plots4[i].y[3]);
            //
            lpData += 8 * 4 * sizeof(float);
            //
            // assemble 'z' components
            // strength
            weaks = reinterpret_cast<float *>(lpData);
            strongs = reinterpret_cast<float *>(lpData + 4 * sizeof(float));
            temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            weaks = reinterpret_cast<float *>(lpData + 8 * sizeof(float));
            strongs = reinterpret_cast<float *>(lpData + 12 * sizeof(float));
            temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            // grace
            plots4[i].z[0] = temp[0][0] + fpGrace * (temp[1][0] - temp[0][0]);
            plots4[i].z[1] = temp[0][1] + fpGrace * (temp[1][1] - temp[0][1]);
            plots4[i].z[2] = temp[0][2] + fpGrace * (temp[1][2] - temp[0][2]);
            plots4[i].z[3] = temp[0][3] + fpGrace * (temp[1][3] - temp[0][3]);
            // strength
            weaks = reinterpret_cast<float *>(lpData + 16 * sizeof(float));
            strongs = reinterpret_cast<float *>(lpData + 20 * sizeof(float));
            temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            weaks = reinterpret_cast<float *>(lpData + 24 * sizeof(float));
            strongs = reinterpret_cast<float *>(lpData + 28 * sizeof(float));
            temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            // grace
            temp[0][0] += fpGrace * (temp[1][0] - temp[0][0]);
            temp[0][1] += fpGrace * (temp[1][1] - temp[0][1]);
            temp[0][2] += fpGrace * (temp[1][2] - temp[0][2]);
            temp[0][3] += fpGrace * (temp[1][3] - temp[0][3]);
            // tall => all 'z' components are complete
            plots4[i].z[0] += fpTall * (temp[0][0] - plots4[i].z[0]);
            plots4[i].z[1] += fpTall * (temp[0][1] - plots4[i].z[1]);
            plots4[i].z[2] += fpTall * (temp[0][2] - plots4[i].z[2]);
            plots4[i].z[3] += fpTall * (temp[0][3] - plots4[i].z[3]);
            //
            lpData += 8 * 4 * sizeof(float);
            //
            // assemble 'w' components - just set it to the '1'
            plots4[i].w[0] = plots4[i].w[1] = plots4[i].w[2] = plots4[i].w[3] = 1;
        }
    }
    else
    {
        for (int i = 0; i < int(hdr.dwNumVertices); ++i)
        {
            memcpy(plots4[i].x, lpData, sizeof(plots4[i].x));
            lpData += sizeof(plots4[i].x);

            memcpy(plots4[i].y, lpData, sizeof(plots4[i].y));
            lpData += sizeof(plots4[i].y);

            memcpy(plots4[i].z, lpData, sizeof(plots4[i].z));
            lpData += sizeof(plots4[i].z);

            plots4[i].w[0] = plots4[i].w[1] = plots4[i].w[2] = plots4[i].w[3] = 1;
        }
    }
    // calculate bounding volume
    if (plots4.GetSize())
        GetRenderer()->GetTransformPipeline()->CalculateBoundingVolume(&bv, plots4.GetData(),
                                                                       plots4.GetSize());
    // process norms4
    norms4.SetSize(hdr.dwNumNormales);
    colors.SetSize(hdr.dwNumNormales * 4);
    speculars.SetSize(hdr.dwNumNormales * 4);
    if (hdr.dwNumNormales != 0)
    {
        memcpy(norms4.GetData(), lpData, hdr.dwNumNormales * sizeof(SXYZW4));
        lpData += hdr.dwNumNormales * sizeof(SXYZW4);
    }
    // process maps
    maps.SetSize(hdr.dwNumMaps);
    if (hdr.dwNumMaps != 0)
    {
        memcpy(maps.GetData(), lpData, hdr.dwNumMaps * sizeof(CPlot2D));
        lpData += hdr.dwNumMaps * sizeof(CPlot2D);
    }
    // process indices
    indices.SetSize(hdr.dwNumIndices);
    if (hdr.dwNumIndices != 0)
    {
        memcpy(indices.GetData(), lpData, hdr.dwNumIndices * sizeof(WORD));
        lpData += hdr.dwNumIndices * sizeof(WORD);
    }
    // process components
    components.SetSize(hdr.dwNumComponents);
    if (hdr.dwNumComponents != 0)
    {
        memcpy(components.GetData(), lpData, hdr.dwNumComponents * sizeof(SVertexComponents));
        lpData += hdr.dwNumComponents * sizeof(SVertexComponents);
    }
    // process lightcomponents
    lightcomponents.SetSize(hdr.dwNumLightComponents);
    if (hdr.dwNumLightComponents != 0)
    {
        memcpy(lightcomponents.GetData(), lpData,
               hdr.dwNumLightComponents * sizeof(SGvNvComponents));
        lpData += hdr.dwNumLightComponents * sizeof(SGvNvComponents);
    }
    // map data
    MapPlots(plots4.GetData(), plots4.GetSize());
    MapNorms(norms4.GetData(), norms4.GetSize());
    MapMaps(maps.GetData(), maps.GetSize());
    MapIndices(indices.GetData(), indices.GetSize());
    MapComponents(components.GetData(), components.GetSize());
    MapLightComponents(lightcomponents.GetData(), lightcomponents.GetSize());
    // set flag, that this figure loaded
    SetFlag(FIG_LOADED);
}
/////////////////////////////////////////////////////////////////////////////////////
// recalculate bounding volume
void CFigureLoaded::RecalcBV()
{
    // calculate bounding volume
    if (plots4.GetSize())
        GetRenderer()->GetTransformPipeline()->CalculateBoundingVolume(&bv, plots4.GetData(),
                                                                       plots4.GetSize());
}
/////////////////////////////////////////////////////////////////////////////////////
// load all data to the internal structures
void CFigureLoaded::LoadOldFigure(LPBYTE &lpData, float fpStrength, float fpGrace, float fpTall)
{
    if (CheckFlag(FIG_LOADED))
        return;

    CPlot3D bbCenter, bbMin, bbMax;
    CPlot3D ptSmall, ptTall;
    CPlot3D *ptSmalls;
    CPlot3D *ptTalls;
    float valSmall;
    float valTall;
    float *smalls;
    float *talls;
    float radius;
    DWORD num;
    int i;
    //
    // process material and texture type IDs
    SetMaterialTypeID(*((ETypeIDs *)lpData));
    lpData += sizeof(ETypeIDs);
    SetTextureTypeID(*((ETypeIDs *)lpData));
    lpData += sizeof(ETypeIDs);
    // process bounding volumes ( small: weak, strong, fat; tall: weak, strong, fat )
    // bbCenter
    ptSmalls = (CPlot3D *)lpData;
    ptTalls = ((CPlot3D *)lpData) + 3;
    bbCenter = ptSmalls[0] + (ptSmalls[1] - ptSmalls[0]) * fpStrength;
    bbCenter += (ptSmalls[2] - bbCenter) * fpGrace;
    ptTall = ptTalls[0] + (ptTalls[1] - ptTalls[0]) * fpStrength;
    ptTall += (ptTalls[2] - ptTall) * fpGrace;
    bbCenter += (ptTall - bbCenter) * fpTall;
    lpData += 6 * sizeof(CPlot3D);
    // bbMin
    ptSmalls = (CPlot3D *)lpData;
    ptTalls = ((CPlot3D *)lpData) + 3;
    bbMin = ptSmalls[0] + (ptSmalls[1] - ptSmalls[0]) * fpStrength;
    bbMin += (ptSmalls[2] - bbMin) * fpGrace;
    ptTall = ptTalls[0] + (ptTalls[1] - ptTalls[0]) * fpStrength;
    ptTall += (ptTalls[2] - ptTall) * fpGrace;
    bbMin += (ptTall - bbMin) * fpTall;
    lpData += 6 * sizeof(CPlot3D);
    // bbMax
    ptSmalls = (CPlot3D *)lpData;
    ptTalls = ((CPlot3D *)lpData) + 3;
    bbMax = ptSmalls[0] + (ptSmalls[1] - ptSmalls[0]) * fpStrength;
    bbMax += (ptSmalls[2] - bbMax) * fpGrace;
    ptTall = ptTalls[0] + (ptTalls[1] - ptTalls[0]) * fpStrength;
    ptTall += (ptTalls[2] - ptTall) * fpGrace;
    bbMax += (ptTall - bbMax) * fpTall;
    lpData += 6 * sizeof(CPlot3D);
    // radius
    smalls = (float *)lpData;
    talls = ((float *)lpData) + 3;
    radius = smalls[0] + (smalls[1] - smalls[0]) * fpStrength;
    radius += (smalls[2] - radius) * fpGrace;
    valTall = talls[0] + (talls[1] - talls[0]) * fpStrength;
    valTall += (talls[2] - valTall) * fpGrace;
    radius += (valTall - radius) * fpTall;
    lpData += 6 * sizeof(float);
    // set this data to bounding volume
    bv.SetCenter(bbCenter);
    bv.SetRadius(radius);
    bv.SetBox(bbMin, bbMax);
    // process plots4
    num = *((DWORD *)lpData);
    lpData += sizeof(DWORD);
    plots4.SetSize(num);
    for (i = 0; i < int(num); i++)
    {
        // +0 <- weak
        // +4 <- strong
        // +8 <- fat
        // first - small components, then - tall components
        // processing x component (4)
        smalls = (float *)lpData;
        talls = ((float *)lpData) + 12;
        lpData += 24 * sizeof(float);
        // 0th x value
        valSmall = smalls[0 + 0] + (smalls[4 + 0] - smalls[0 + 0]) * fpStrength;
        valSmall += (smalls[8 + 0] - valSmall) * fpGrace;
        valTall = talls[0 + 0] + (talls[4 + 0] - talls[0 + 0]) * fpStrength;
        valTall += (talls[8 + 0] - valTall) * fpGrace;
        plots4[i].x[0] = valSmall + (valTall - valSmall) * fpTall;
        // 1st x value
        valSmall = smalls[0 + 1] + (smalls[4 + 1] - smalls[0 + 1]) * fpStrength;
        valSmall += (smalls[8 + 1] - valSmall) * fpGrace;
        valTall = talls[0 + 1] + (talls[4 + 1] - talls[0 + 1]) * fpStrength;
        valTall += (talls[8 + 1] - valTall) * fpGrace;
        plots4[i].x[1] = valSmall + (valTall - valSmall) * fpTall;
        // 2nd x value
        valSmall = smalls[0 + 2] + (smalls[4 + 2] - smalls[0 + 2]) * fpStrength;
        valSmall += (smalls[8 + 2] - valSmall) * fpGrace;
        valTall = talls[0 + 2] + (talls[4 + 2] - talls[0 + 2]) * fpStrength;
        valTall += (talls[8 + 2] - valTall) * fpGrace;
        plots4[i].x[2] = valSmall + (valTall - valSmall) * fpTall;
        // 3rd x value
        valSmall = smalls[0 + 3] + (smalls[4 + 3] - smalls[0 + 3]) * fpStrength;
        valSmall += (smalls[8 + 3] - valSmall) * fpGrace;
        valTall = talls[0 + 3] + (talls[4 + 3] - talls[0 + 3]) * fpStrength;
        valTall += (talls[8 + 3] - valTall) * fpGrace;
        plots4[i].x[3] = valSmall + (valTall - valSmall) * fpTall;
        // processing y component (4)
        smalls = (float *)lpData;
        talls = ((float *)lpData) + 12;
        lpData += 24 * sizeof(float);
        // 0th y value
        valSmall = smalls[0 + 0] + (smalls[4 + 0] - smalls[0 + 0]) * fpStrength;
        valSmall += (smalls[8 + 0] - valSmall) * fpGrace;
        valTall = talls[0 + 0] + (talls[4 + 0] - talls[0 + 0]) * fpStrength;
        valTall += (talls[8 + 0] - valTall) * fpGrace;
        plots4[i].y[0] = valSmall + (valTall - valSmall) * fpTall;
        // 1st y value
        valSmall = smalls[0 + 1] + (smalls[4 + 1] - smalls[0 + 1]) * fpStrength;
        valSmall += (smalls[8 + 1] - valSmall) * fpGrace;
        valTall = talls[0 + 1] + (talls[4 + 1] - talls[0 + 1]) * fpStrength;
        valTall += (talls[8 + 1] - valTall) * fpGrace;
        plots4[i].y[1] = valSmall + (valTall - valSmall) * fpTall;
        // 2nd y value
        valSmall = smalls[0 + 2] + (smalls[4 + 2] - smalls[0 + 2]) * fpStrength;
        valSmall += (smalls[8 + 2] - valSmall) * fpGrace;
        valTall = talls[0 + 2] + (talls[4 + 2] - talls[0 + 2]) * fpStrength;
        valTall += (talls[8 + 2] - valTall) * fpGrace;
        plots4[i].y[2] = valSmall + (valTall - valSmall) * fpTall;
        // 3rd y value
        valSmall = smalls[0 + 3] + (smalls[4 + 3] - smalls[0 + 3]) * fpStrength;
        valSmall += (smalls[8 + 3] - valSmall) * fpGrace;
        valTall = talls[0 + 3] + (talls[4 + 3] - talls[0 + 3]) * fpStrength;
        valTall += (talls[8 + 3] - valTall) * fpGrace;
        plots4[i].y[3] = valSmall + (valTall - valSmall) * fpTall;
        // processing z component (4)
        smalls = (float *)lpData;
        talls = ((float *)lpData) + 12;
        lpData += 24 * sizeof(float);
        // 0th z value
        valSmall = smalls[0 + 0] + (smalls[4 + 0] - smalls[0 + 0]) * fpStrength;
        valSmall += (smalls[8 + 0] - valSmall) * fpGrace;
        valTall = talls[0 + 0] + (talls[4 + 0] - talls[0 + 0]) * fpStrength;
        valTall += (talls[8 + 0] - valTall) * fpGrace;
        plots4[i].z[0] = valSmall + (valTall - valSmall) * fpTall;
        // 1st z value
        valSmall = smalls[0 + 1] + (smalls[4 + 1] - smalls[0 + 1]) * fpStrength;
        valSmall += (smalls[8 + 1] - valSmall) * fpGrace;
        valTall = talls[0 + 1] + (talls[4 + 1] - talls[0 + 1]) * fpStrength;
        valTall += (talls[8 + 1] - valTall) * fpGrace;
        plots4[i].z[1] = valSmall + (valTall - valSmall) * fpTall;
        // 2nd z value
        valSmall = smalls[0 + 2] + (smalls[4 + 2] - smalls[0 + 2]) * fpStrength;
        valSmall += (smalls[8 + 2] - valSmall) * fpGrace;
        valTall = talls[0 + 2] + (talls[4 + 2] - talls[0 + 2]) * fpStrength;
        valTall += (talls[8 + 2] - valTall) * fpGrace;
        plots4[i].z[2] = valSmall + (valTall - valSmall) * fpTall;
        // 3rd z value
        valSmall = smalls[0 + 3] + (smalls[4 + 3] - smalls[0 + 3]) * fpStrength;
        valSmall += (smalls[8 + 3] - valSmall) * fpGrace;
        valTall = talls[0 + 3] + (talls[4 + 3] - talls[0 + 3]) * fpStrength;
        valTall += (talls[8 + 3] - valTall) * fpGrace;
        plots4[i].z[3] = valSmall + (valTall - valSmall) * fpTall;
        // processing w component (4)
        plots4[i].w[0] = plots4[i].w[1] = plots4[i].w[2] = plots4[i].w[3] = 1;
    }
    // calculate bounding volume
    if (plots4.GetSize())
        GetRenderer()->GetTransformPipeline()->CalculateBoundingVolume(&bv, plots4.GetData(),
                                                                       plots4.GetSize());
    // process norms4
    num = *((DWORD *)lpData);
    lpData += sizeof(DWORD);
    norms4.SetSize(num);
    colors.SetSize(num * 4);
    speculars.SetSize(num * 4);
    if (num != 0)
    {
        memcpy(norms4.GetData(), lpData, num * sizeof(SXYZW4));
        lpData += num * sizeof(SXYZW4);
        // set default specular value
        MemSetDWord(speculars.GetData(), RGBA_MAKE(0, 0, 0, 255), speculars.GetSize());
    }
    // process maps
    num = *((DWORD *)lpData);
    lpData += sizeof(DWORD);
    maps.SetSize(num);
    if (num != 0)
    {
        memcpy(maps.GetData(), lpData, num * sizeof(CPlot2D));
        lpData += num * sizeof(CPlot2D);
    }
    // process faces <= just skip...
    num = *((DWORD *)lpData);
    lpData += sizeof(DWORD);
    lpData += num * sizeof(CFace);
    // process components
    num = *((DWORD *)lpData);
    lpData += sizeof(DWORD);
    components.SetSize(num);
    if (num != 0)
    {
        memcpy(components.GetData(), lpData, num * sizeof(SVertexComponents));
        lpData += num * sizeof(SVertexComponents);
    }
    // process lightcomponents
    num = *((DWORD *)lpData);
    lpData += sizeof(DWORD);
    lightcomponents.SetSize(num);
    if (num != 0)
    {
        memcpy(lightcomponents.GetData(), lpData, num * sizeof(SGvNvComponents));
        lpData += num * sizeof(SGvNvComponents);
    }
    // process indices
    num = *((DWORD *)lpData);
    lpData += sizeof(DWORD);
    indices.SetSize(num);
    if (num != 0)
    {
        memcpy(indices.GetData(), lpData, num * sizeof(WORD));
        lpData += num * sizeof(WORD);
    }
    // map data
    MapPlots(plots4.GetData(), plots4.GetSize());
    MapNorms(norms4.GetData(), norms4.GetSize());
    MapMaps(maps.GetData(), maps.GetSize());
    MapComponents(components.GetData(), components.GetSize());
    MapLightComponents(lightcomponents.GetData(), lightcomponents.GetSize());
    MapIndices(indices.GetData(), indices.GetSize());
    // set flag, that this figure loaded
    SetFlag(FIG_LOADED);
}
/////////////////////////////////////////////////////////////////////////////////////
void CFigureLoaded::MirrorLeftRight()
{
    // Flip geometry vertexes
    for (int n = 0; n < plots4.GetSize(); n++)
    {
        float *pXs = plots4[n].x;
        pXs[0] = -pXs[0];
        pXs[1] = -pXs[1];
        pXs[2] = -pXs[2];
        pXs[3] = -pXs[3];
    }
    // Flip normals
    for (int n = 0; n < norms4.GetSize(); n++)
    {
        float *pXs = norms4[n].x;
        pXs[0] = -pXs[0];
        pXs[1] = -pXs[1];
        pXs[2] = -pXs[2];
        pXs[3] = -pXs[3];
    }
    FlipFaces();
}

void CFigureLoaded::MirrorUpDown()
{
    // Flip geometry vertexes
    for (int n = 0; n < plots4.GetSize(); n++)
    {
        float *pYs = plots4[n].y;
        pYs[0] = -pYs[0];
        pYs[1] = -pYs[1];
        pYs[2] = -pYs[2];
        pYs[3] = -pYs[3];
    }
    // Flip normals
    for (int n = 0; n < norms4.GetSize(); n++)
    {
        float *pYs = norms4[n].y;
        pYs[0] = -pYs[0];
        pYs[1] = -pYs[1];
        pYs[2] = -pYs[2];
        pYs[3] = -pYs[3];
    }
    FlipFaces();
}

void CFigureLoaded::FlipFaces()
{
    WORD *pInd = indices.GetData();
    for (int n = 0; n < indices.GetSize(); n += 3, pInd += 3)
    {
        WORD wTemp = pInd[0];
        pInd[0] = pInd[1];
        pInd[1] = wTemp;
    }
}

/////////////////////////////////////////////////////////////////////////////////////
void CFigureMapLoaded::LoadD3DComplex(LPBYTE &lpData, float fpStrength, float fpGrace, float fpTall)
{
    if (CheckFlag(FIG_LOADED))
        return;
    //
    SFigureFormatHeader hdr;
    memcpy(&hdr, lpData, sizeof(hdr));
    if ((hdr.dwSignature & FIG_SIGNATURE_MASK) == FIG_SIGNATURE_FIGN)
        LoadNewFigure(lpData, fpStrength, fpGrace, fpTall);
    else
        LoadOldFigure(lpData, fpStrength, fpGrace, fpTall);
    //
    SetFlag(FIG_LOADED);
}
/////////////////////////////////////////////////////////////////////////////////////
void CFigureMapLoaded::LoadOldFigure(LPBYTE &lpData, float fpStrength, float fpGrace, float fpTall)
{
    CPlot3D bbCenter, bbMin, bbMax;
    CPlot3D ptSmall, ptTall;
    CPlot3D *ptSmalls;
    CPlot3D *ptTalls;
    float valSmall;
    float valTall;
    float *smalls;
    float *talls;
    float radius;
    DWORD num;
    int i;
    // process material and texture type IDs
    tidMaterialTypeID = *((ETypeIDs *)lpData);
    lpData += sizeof(ETypeIDs);
    tidTextureTypeID = *((ETypeIDs *)lpData);
    lpData += sizeof(ETypeIDs);
    // process bounding volumes ( small: weak, strong, fat; tall: weak, strong, fat )
    // bbCenter
    ptSmalls = (CPlot3D *)lpData;
    ptTalls = ((CPlot3D *)lpData) + 3;
    bbCenter = ptSmalls[0] + (ptSmalls[1] - ptSmalls[0]) * fpStrength;
    bbCenter += (ptSmalls[2] - bbCenter) * fpGrace;
    ptTall = ptTalls[0] + (ptTalls[1] - ptTalls[0]) * fpStrength;
    ptTall += (ptTalls[2] - ptTall) * fpGrace;
    bbCenter += (ptTall - bbCenter) * fpTall;
    lpData += 6 * sizeof(CPlot3D);
    // bbMin
    ptSmalls = (CPlot3D *)lpData;
    ptTalls = ((CPlot3D *)lpData) + 3;
    bbMin = ptSmalls[0] + (ptSmalls[1] - ptSmalls[0]) * fpStrength;
    bbMin += (ptSmalls[2] - bbMin) * fpGrace;
    ptTall = ptTalls[0] + (ptTalls[1] - ptTalls[0]) * fpStrength;
    ptTall += (ptTalls[2] - ptTall) * fpGrace;
    bbMin += (ptTall - bbMin) * fpTall;
    lpData += 6 * sizeof(CPlot3D);
    // bbMax
    ptSmalls = (CPlot3D *)lpData;
    ptTalls = ((CPlot3D *)lpData) + 3;
    bbMax = ptSmalls[0] + (ptSmalls[1] - ptSmalls[0]) * fpStrength;
    bbMax += (ptSmalls[2] - bbMax) * fpGrace;
    ptTall = ptTalls[0] + (ptTalls[1] - ptTalls[0]) * fpStrength;
    ptTall += (ptTalls[2] - ptTall) * fpGrace;
    bbMax += (ptTall - bbMax) * fpTall;
    lpData += 6 * sizeof(CPlot3D);
    // radius
    smalls = (float *)lpData;
    talls = ((float *)lpData) + 3;
    radius = smalls[0] + (smalls[1] - smalls[0]) * fpStrength;
    radius += (smalls[2] - radius) * fpGrace;
    valTall = talls[0] + (talls[1] - talls[0]) * fpStrength;
    valTall += (talls[2] - valTall) * fpGrace;
    radius += (valTall - radius) * fpTall;
    lpData += 6 * sizeof(float);
    // set this data to bounding volume
    bv.SetCenter(bbCenter);
    bv.SetRadius(radius);
    bv.SetBox(bbMin, bbMax);
    // process plots4
    num = *((DWORD *)lpData);
    lpData += sizeof(DWORD);
    plots4.SetSize(num);
    for (i = 0; i < int(num); i++)
    {
        // +0 <- weak
        // +4 <- strong
        // +8 <- fat
        // first - small components, then - tall components
        // processing x component (4)
        smalls = (float *)lpData;
        talls = ((float *)lpData) + 12;
        lpData += 24 * sizeof(float);
        // 0th x value
        valSmall = smalls[0 + 0] + (smalls[4 + 0] - smalls[0 + 0]) * fpStrength;
        valSmall += (smalls[8 + 0] - valSmall) * fpGrace;
        valTall = talls[0 + 0] + (talls[4 + 0] - talls[0 + 0]) * fpStrength;
        valTall += (talls[8 + 0] - valTall) * fpGrace;
        plots4[i].x[0] = valSmall + (valTall - valSmall) * fpTall;
        // 1st x value
        valSmall = smalls[0 + 1] + (smalls[4 + 1] - smalls[0 + 1]) * fpStrength;
        valSmall += (smalls[8 + 1] - valSmall) * fpGrace;
        valTall = talls[0 + 1] + (talls[4 + 1] - talls[0 + 1]) * fpStrength;
        valTall += (talls[8 + 1] - valTall) * fpGrace;
        plots4[i].x[1] = valSmall + (valTall - valSmall) * fpTall;
        // 2nd x value
        valSmall = smalls[0 + 2] + (smalls[4 + 2] - smalls[0 + 2]) * fpStrength;
        valSmall += (smalls[8 + 2] - valSmall) * fpGrace;
        valTall = talls[0 + 2] + (talls[4 + 2] - talls[0 + 2]) * fpStrength;
        valTall += (talls[8 + 2] - valTall) * fpGrace;
        plots4[i].x[2] = valSmall + (valTall - valSmall) * fpTall;
        // 3rd x value
        valSmall = smalls[0 + 3] + (smalls[4 + 3] - smalls[0 + 3]) * fpStrength;
        valSmall += (smalls[8 + 3] - valSmall) * fpGrace;
        valTall = talls[0 + 3] + (talls[4 + 3] - talls[0 + 3]) * fpStrength;
        valTall += (talls[8 + 3] - valTall) * fpGrace;
        plots4[i].x[3] = valSmall + (valTall - valSmall) * fpTall;
        // processing y component (4)
        smalls = (float *)lpData;
        talls = ((float *)lpData) + 12;
        lpData += 24 * sizeof(float);
        // 0th y value
        valSmall = smalls[0 + 0] + (smalls[4 + 0] - smalls[0 + 0]) * fpStrength;
        valSmall += (smalls[8 + 0] - valSmall) * fpGrace;
        valTall = talls[0 + 0] + (talls[4 + 0] - talls[0 + 0]) * fpStrength;
        valTall += (talls[8 + 0] - valTall) * fpGrace;
        plots4[i].y[0] = valSmall + (valTall - valSmall) * fpTall;
        // 1st y value
        valSmall = smalls[0 + 1] + (smalls[4 + 1] - smalls[0 + 1]) * fpStrength;
        valSmall += (smalls[8 + 1] - valSmall) * fpGrace;
        valTall = talls[0 + 1] + (talls[4 + 1] - talls[0 + 1]) * fpStrength;
        valTall += (talls[8 + 1] - valTall) * fpGrace;
        plots4[i].y[1] = valSmall + (valTall - valSmall) * fpTall;
        // 2nd y value
        valSmall = smalls[0 + 2] + (smalls[4 + 2] - smalls[0 + 2]) * fpStrength;
        valSmall += (smalls[8 + 2] - valSmall) * fpGrace;
        valTall = talls[0 + 2] + (talls[4 + 2] - talls[0 + 2]) * fpStrength;
        valTall += (talls[8 + 2] - valTall) * fpGrace;
        plots4[i].y[2] = valSmall + (valTall - valSmall) * fpTall;
        // 3rd y value
        valSmall = smalls[0 + 3] + (smalls[4 + 3] - smalls[0 + 3]) * fpStrength;
        valSmall += (smalls[8 + 3] - valSmall) * fpGrace;
        valTall = talls[0 + 3] + (talls[4 + 3] - talls[0 + 3]) * fpStrength;
        valTall += (talls[8 + 3] - valTall) * fpGrace;
        plots4[i].y[3] = valSmall + (valTall - valSmall) * fpTall;
        // processing z component (4)
        smalls = (float *)lpData;
        talls = ((float *)lpData) + 12;
        lpData += 24 * sizeof(float);
        // 0th z value
        valSmall = smalls[0 + 0] + (smalls[4 + 0] - smalls[0 + 0]) * fpStrength;
        valSmall += (smalls[8 + 0] - valSmall) * fpGrace;
        valTall = talls[0 + 0] + (talls[4 + 0] - talls[0 + 0]) * fpStrength;
        valTall += (talls[8 + 0] - valTall) * fpGrace;
        plots4[i].z[0] = valSmall + (valTall - valSmall) * fpTall;
        // 1st z value
        valSmall = smalls[0 + 1] + (smalls[4 + 1] - smalls[0 + 1]) * fpStrength;
        valSmall += (smalls[8 + 1] - valSmall) * fpGrace;
        valTall = talls[0 + 1] + (talls[4 + 1] - talls[0 + 1]) * fpStrength;
        valTall += (talls[8 + 1] - valTall) * fpGrace;
        plots4[i].z[1] = valSmall + (valTall - valSmall) * fpTall;
        // 2nd z value
        valSmall = smalls[0 + 2] + (smalls[4 + 2] - smalls[0 + 2]) * fpStrength;
        valSmall += (smalls[8 + 2] - valSmall) * fpGrace;
        valTall = talls[0 + 2] + (talls[4 + 2] - talls[0 + 2]) * fpStrength;
        valTall += (talls[8 + 2] - valTall) * fpGrace;
        plots4[i].z[2] = valSmall + (valTall - valSmall) * fpTall;
        // 3rd z value
        valSmall = smalls[0 + 3] + (smalls[4 + 3] - smalls[0 + 3]) * fpStrength;
        valSmall += (smalls[8 + 3] - valSmall) * fpGrace;
        valTall = talls[0 + 3] + (talls[4 + 3] - talls[0 + 3]) * fpStrength;
        valTall += (talls[8 + 3] - valTall) * fpGrace;
        plots4[i].z[3] = valSmall + (valTall - valSmall) * fpTall;
        // processing w component (4)
        plots4[i].w[0] = plots4[i].w[1] = plots4[i].w[2] = plots4[i].w[3] = 1;
    }
    // calculate bounding volume
    if (plots4.GetSize())
        GetRenderer()->GetTransformPipeline()->CalculateBoundingVolume(&bv, plots4.GetData(),
                                                                       plots4.GetSize());
    // process norms4
    num = *((DWORD *)lpData);
    lpData += sizeof(DWORD);
    norms4.SetSize(num);
    colors.SetSize(num * 4);
    speculars.SetSize(num * 4);
    if (num != 0)
    {
        memcpy(norms4.GetData(), lpData, num * sizeof(SXYZW4));
        lpData += num * sizeof(SXYZW4);
        // set default specular value
        MemSetDWord(speculars.GetData(), RGBA_MAKE(0, 0, 0, 255), speculars.GetSize());
    }
    // process maps
    num = *((DWORD *)lpData);
    lpData += sizeof(DWORD);
    MapMaps(reinterpret_cast<CPlot2D *>(lpData), num);
    lpData += num * sizeof(CPlot2D);
    // process faces
    num = *((DWORD *)lpData);
    lpData += sizeof(DWORD);
    //	faces1 = (CFace*)lpData;
    lpData += num * sizeof(CFace);
    // process components
    num = *((DWORD *)lpData);
    lpData += sizeof(DWORD);
    MapComponents(reinterpret_cast<SVertexComponents *>(lpData), num);
    lpData += num * sizeof(SVertexComponents);
    // process lightcomponents
    num = *((DWORD *)lpData);
    lpData += sizeof(DWORD);
    MapLightComponents(reinterpret_cast<SGvNvComponents *>(lpData), num);
    lpData += num * sizeof(SGvNvComponents);
    // process indices
    num = *((DWORD *)lpData);
    lpData += sizeof(DWORD);
    MapIndices(reinterpret_cast<WORD *>(lpData), num);
    lpData += num * sizeof(WORD);
    // map data
    MapPlots(plots4.GetData(), plots4.GetSize());
    MapNorms(norms4.GetData(), norms4.GetSize());
    // set flag, that this figure loaded
    SetFlag(FIG_LOADED);
}
/////////////////////////////////////////////////////////////////////////////////////
void CFigureMapLoaded::LoadNewFigure(LPBYTE &lpData, float fpStrength, float fpGrace, float fpTall)
{
    if (CheckFlag(FIG_LOADED))
        return;
    //
    SFigureFormatHeader hdr;
    memcpy(&hdr, lpData, sizeof(hdr));
    lpData += sizeof(hdr);
    // process material and texture type IDs
    SetMaterialTypeID(*((ETypeIDs *)lpData));
    lpData += sizeof(ETypeIDs);
    SetTextureTypeID(*((ETypeIDs *)lpData));
    lpData += sizeof(ETypeIDs);
    // process bounding volume
    JustLoadBoundingVolume(lpData, fpStrength, fpGrace, fpTall, hdr.dwSignature);
    // process plots4
    plots4.SetSize(hdr.dwNumVertices);
    if (hdr.dwSignature == FIG_SIGNATURE_FIG8)
    {
        for (int i = 0; i < int(hdr.dwNumVertices); ++i)
        {
            float temp[2][4];
            // assemble 'x' components
            // strength
            float *weaks = reinterpret_cast<float *>(lpData);
            float *strongs = reinterpret_cast<float *>(lpData + 4 * sizeof(float));
            temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            weaks = reinterpret_cast<float *>(lpData + 8 * sizeof(float));
            strongs = reinterpret_cast<float *>(lpData + 12 * sizeof(float));
            temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            // grace
            plots4[i].x[0] = temp[0][0] + fpGrace * (temp[1][0] - temp[0][0]);
            plots4[i].x[1] = temp[0][1] + fpGrace * (temp[1][1] - temp[0][1]);
            plots4[i].x[2] = temp[0][2] + fpGrace * (temp[1][2] - temp[0][2]);
            plots4[i].x[3] = temp[0][3] + fpGrace * (temp[1][3] - temp[0][3]);
            // strength
            weaks = reinterpret_cast<float *>(lpData + 16 * sizeof(float));
            strongs = reinterpret_cast<float *>(lpData + 20 * sizeof(float));
            temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            weaks = reinterpret_cast<float *>(lpData + 24 * sizeof(float));
            strongs = reinterpret_cast<float *>(lpData + 28 * sizeof(float));
            temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            // grace
            temp[0][0] += fpGrace * (temp[1][0] - temp[0][0]);
            temp[0][1] += fpGrace * (temp[1][1] - temp[0][1]);
            temp[0][2] += fpGrace * (temp[1][2] - temp[0][2]);
            temp[0][3] += fpGrace * (temp[1][3] - temp[0][3]);
            // tall => all 'x' components are complete
            plots4[i].x[0] += fpTall * (temp[0][0] - plots4[i].x[0]);
            plots4[i].x[1] += fpTall * (temp[0][1] - plots4[i].x[1]);
            plots4[i].x[2] += fpTall * (temp[0][2] - plots4[i].x[2]);
            plots4[i].x[3] += fpTall * (temp[0][3] - plots4[i].x[3]);
            //
            lpData += 8 * 4 * sizeof(float);
            //
            // assemble 'y' components
            // strength
            weaks = reinterpret_cast<float *>(lpData);
            strongs = reinterpret_cast<float *>(lpData + 4 * sizeof(float));
            temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            weaks = reinterpret_cast<float *>(lpData + 8 * sizeof(float));
            strongs = reinterpret_cast<float *>(lpData + 12 * sizeof(float));
            temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            // grace
            plots4[i].y[0] = temp[0][0] + fpGrace * (temp[1][0] - temp[0][0]);
            plots4[i].y[1] = temp[0][1] + fpGrace * (temp[1][1] - temp[0][1]);
            plots4[i].y[2] = temp[0][2] + fpGrace * (temp[1][2] - temp[0][2]);
            plots4[i].y[3] = temp[0][3] + fpGrace * (temp[1][3] - temp[0][3]);
            // strength
            weaks = reinterpret_cast<float *>(lpData + 16 * sizeof(float));
            strongs = reinterpret_cast<float *>(lpData + 20 * sizeof(float));
            temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            weaks = reinterpret_cast<float *>(lpData + 24 * sizeof(float));
            strongs = reinterpret_cast<float *>(lpData + 28 * sizeof(float));
            temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            // grace
            temp[0][0] += fpGrace * (temp[1][0] - temp[0][0]);
            temp[0][1] += fpGrace * (temp[1][1] - temp[0][1]);
            temp[0][2] += fpGrace * (temp[1][2] - temp[0][2]);
            temp[0][3] += fpGrace * (temp[1][3] - temp[0][3]);
            // tall => all 'y' components are complete
            plots4[i].y[0] += fpTall * (temp[0][0] - plots4[i].y[0]);
            plots4[i].y[1] += fpTall * (temp[0][1] - plots4[i].y[1]);
            plots4[i].y[2] += fpTall * (temp[0][2] - plots4[i].y[2]);
            plots4[i].y[3] += fpTall * (temp[0][3] - plots4[i].y[3]);
            //
            lpData += 8 * 4 * sizeof(float);
            //
            // assemble 'z' components
            // strength
            weaks = reinterpret_cast<float *>(lpData);
            strongs = reinterpret_cast<float *>(lpData + 4 * sizeof(float));
            temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            weaks = reinterpret_cast<float *>(lpData + 8 * sizeof(float));
            strongs = reinterpret_cast<float *>(lpData + 12 * sizeof(float));
            temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            // grace
            plots4[i].z[0] = temp[0][0] + fpGrace * (temp[1][0] - temp[0][0]);
            plots4[i].z[1] = temp[0][1] + fpGrace * (temp[1][1] - temp[0][1]);
            plots4[i].z[2] = temp[0][2] + fpGrace * (temp[1][2] - temp[0][2]);
            plots4[i].z[3] = temp[0][3] + fpGrace * (temp[1][3] - temp[0][3]);
            // strength
            weaks = reinterpret_cast<float *>(lpData + 16 * sizeof(float));
            strongs = reinterpret_cast<float *>(lpData + 20 * sizeof(float));
            temp[0][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[0][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[0][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[0][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            weaks = reinterpret_cast<float *>(lpData + 24 * sizeof(float));
            strongs = reinterpret_cast<float *>(lpData + 28 * sizeof(float));
            temp[1][0] = weaks[0] + fpStrength * (strongs[0] - weaks[0]);
            temp[1][1] = weaks[1] + fpStrength * (strongs[1] - weaks[1]);
            temp[1][2] = weaks[2] + fpStrength * (strongs[2] - weaks[2]);
            temp[1][3] = weaks[3] + fpStrength * (strongs[3] - weaks[3]);
            // grace
            temp[0][0] += fpGrace * (temp[1][0] - temp[0][0]);
            temp[0][1] += fpGrace * (temp[1][1] - temp[0][1]);
            temp[0][2] += fpGrace * (temp[1][2] - temp[0][2]);
            temp[0][3] += fpGrace * (temp[1][3] - temp[0][3]);
            // tall => all 'z' components are complete
            plots4[i].z[0] += fpTall * (temp[0][0] - plots4[i].z[0]);
            plots4[i].z[1] += fpTall * (temp[0][1] - plots4[i].z[1]);
            plots4[i].z[2] += fpTall * (temp[0][2] - plots4[i].z[2]);
            plots4[i].z[3] += fpTall * (temp[0][3] - plots4[i].z[3]);
            //
            lpData += 8 * 4 * sizeof(float);
            //
            // assemble 'w' components - just set it to the '1'
            plots4[i].w[0] = plots4[i].w[1] = plots4[i].w[2] = plots4[i].w[3] = 1;
        }
    }
    else
    {
        for (int i = 0; i < int(hdr.dwNumVertices); ++i)
        {
            memcpy(plots4[i].x, lpData, sizeof(plots4[i].x));
            lpData += sizeof(plots4[i].x);

            memcpy(plots4[i].y, lpData, sizeof(plots4[i].y));
            lpData += sizeof(plots4[i].y);

            memcpy(plots4[i].z, lpData, sizeof(plots4[i].z));
            lpData += sizeof(plots4[i].z);

            plots4[i].w[0] = plots4[i].w[1] = plots4[i].w[2] = plots4[i].w[3] = 1;
        }
    }
    // calculate bounding volume
    if (plots4.GetSize())
        GetRenderer()->GetTransformPipeline()->CalculateBoundingVolume(&bv, plots4.GetData(),
                                                                       plots4.GetSize());
    // process norms4
    norms4.SetSize(hdr.dwNumNormales);
    colors.SetSize(hdr.dwNumNormales * 4);
    speculars.SetSize(hdr.dwNumNormales * 4);
    if (hdr.dwNumNormales != 0)
    {
        memcpy(norms4.GetData(), lpData, hdr.dwNumNormales * sizeof(SXYZW4));
        lpData += hdr.dwNumNormales * sizeof(SXYZW4);
    }
    // process maps
    MapMaps(reinterpret_cast<CPlot2D *>(lpData), hdr.dwNumMaps);
    lpData += hdr.dwNumMaps * sizeof(CPlot2D);
    // process indices
    MapIndices(reinterpret_cast<WORD *>(lpData), hdr.dwNumIndices);
    lpData += hdr.dwNumIndices * sizeof(WORD);
    // process components
    MapComponents(reinterpret_cast<SVertexComponents *>(lpData), hdr.dwNumComponents);
    lpData += hdr.dwNumComponents * sizeof(SVertexComponents);
    // process lightcomponents
    MapLightComponents(reinterpret_cast<SGvNvComponents *>(lpData), hdr.dwNumLightComponents);
    lpData += hdr.dwNumLightComponents * sizeof(SGvNvComponents);
    // map data
    MapPlots(plots4.GetData(), plots4.GetSize());
    MapNorms(norms4.GetData(), norms4.GetSize());
    // set flag, that this figure loaded
    SetFlag(FIG_LOADED);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CFigureLoaded::Create()
{
    return CFigure::Create();
}
void CFigureLoaded::Destroy()
{
    plots4.RemoveAll();
    norms4.RemoveAll();
    components.RemoveAll();
    lightcomponents.RemoveAll();
    indices.RemoveAll();
    maps.RemoveAll();

    CFigure::Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
// map all data pointers to 'CFigure'
void CFigureLoaded::SetupDataPointers()
{
    MapPlots(plots4.GetData(), plots4.GetSize());
    MapNorms(norms4.GetData(), norms4.GetSize());
    MapMaps(maps.GetData(), maps.GetSize());
    MapComponents(components.GetData(), components.GetSize());
    MapLightComponents(lightcomponents.GetData(), lightcomponents.GetSize());
    MapIndices(indices.GetData(), indices.GetSize());
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CFigureMapLoaded::Create()
{
    return CFigure::Create();
}
void CFigureMapLoaded::Destroy()
{
    plots4.RemoveAll();
    norms4.RemoveAll();

    CFigure::Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
