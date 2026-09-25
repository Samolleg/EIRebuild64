#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "lightning.h"

#include "RandomGen.h"
#include "texture.h"
#include "World.h"
#include "WorldClient.h"
#include "TextureManager.h"
#include "Camera.h"
#include "Particle.h"

/////////////////////////////////////////////////////////////////////////////////////
// lightning function generation system
const int N_L_FUNCS_NUMBER = 16;
const float FP_RECREATE_TIME = 4;
const float FP_LIGHTNING_TEXTURE_PER_M = 0.6f;
const int N_LINES_PER_TEXTURE = 4;

// создавать файл с базисными функциями для молнии, если эта константа не определена, то
// данные считываются из ресурса
// #define _CREATE_LIGHTNING_DAT_

/////////////////////////////////////////////////////////////////////////////////////
// SLightningFunc
/////////////////////////////////////////////////////////////////////////////////////
void SLightningFunc::CreateRandom(int nStartHarm, int nFinishHarm, float fpStartPower,
                                  float fpFinishPower)
{
    int i, k;
    fpStartPower /= nFinishHarm - nStartHarm + 1;
    fpFinishPower /= nFinishHarm - nStartHarm + 1;
    for (i = nStartHarm; i < nFinishHarm; i++)
    {
        float fpFraction = ((float)(i - nStartHarm)) / (nFinishHarm - nStartHarm + 1);
        float fpPower = fpFraction * (fpFinishPower - fpStartPower) + fpStartPower;
        float fpSin = random.GetFloat(-fpPower, fpPower);
        float fpCos = random.GetFloat(-fpPower, fpPower);
        for (k = 0; k < N_MAX_LIGHTNING_SEGMENTS; k++)
        {
            float fpArg = float(i * 3.14159 * k / N_MAX_LIGHTNING_SEGMENTS);
            data[k] += float(sin(fpArg) * fpSin + cos(fpArg) * fpCos);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void SLightningFunc::Load(CSave &file)
{
    file.Read(data, sizeof(data));
}
/////////////////////////////////////////////////////////////////////////////////////
void SLightningFunc::Store(CSave &file)
{
    file.Write(data, sizeof(data));
}
/////////////////////////////////////////////////////////////////////////////////////
void SLightningFunc::CreateLightning(BOOL bHighFreq)
{
    int i;
    for (i = 0; i < N_MAX_LIGHTNING_SEGMENTS; i++)
    {
        data[i] = 0;
    }
    if (bHighFreq)
    {
        CreateRandom(10, 25, 0.02f, 0.02f);
        CreateRandom(25, 100, 0.02f, 0.02f);
        CreateRandom(100, 200, 0.02f, 0.007f);
        CreateRandom(200, 400, 0.007f, 0.005f);
    }
    else
    {
        CreateRandom(1, 5, 0.1f, 0.1f);
        CreateRandom(10, 25, 0.1f, 0.08f);
        CreateRandom(25, 100, 0.08f, 0.006f);
        CreateRandom(100, 200, 0.006f, 0.003f);
        CreateRandom(200, 400, 0.003f, 0.001f);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// predefined array of random functions
/////////////////////////////////////////////////////////////////////////////////////
static SLightningFunc lFunctions[N_L_FUNCS_NUMBER * 2];

static BOOL InitializeLightningGenerator()
{
    int i;
#ifdef _CREATE_LIGHTNING_DAT_
    for (i = 0; i < N_L_FUNCS_NUMBER; i++)
        lFunctions[i].CreateLightning(FALSE);
    for (i = 0; i < N_L_FUNCS_NUMBER; i++)
        lFunctions[i + N_L_FUNCS_NUMBER].CreateLightning(TRUE);
    CFileSave file;
    file.StartSaving("lightning.dat");
    for (i = 0; i < N_L_FUNCS_NUMBER * 2; i++)
        lFunctions[i].Store(file);
    file.CloseFile();
#else
    CMappedResource mapRes;
    CMemorySave file;
    if (!mapRes.OpenResource(CString(CWorld::GetResourcePath()) + "gfx.res"))
        return FALSE;
    DWORD dwSize;
    LPBYTE pData = mapRes.GetFile("lightning.dat", &dwSize);
    if (!pData)
        return ReportError("Unable to open file gfx.res\\lightning.dat");
    file.StartSaving();
    file.Write(pData, dwSize);
    file.StartLoading();
    file.Seek(0);
    for (i = 0; i < N_L_FUNCS_NUMBER * 2; i++)
        lFunctions[i].Load(file);
    mapRes.CloseFile(pData);
#endif
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// SLightningLeafFunc
/////////////////////////////////////////////////////////////////////////////////////
void SLightningLeafFunc::CreateRandom(float fpAmpl)
{
    int i;
    float fpSum = 0;
    for (i = 0; i < 2; i++)
    {
        nIdx[i] = random.Get(N_L_FUNCS_NUMBER - 1);
        fpKoef[i] = random.GetFloat(-1, 1);
        fpSum += sqr(fpKoef[i]);
    }
    fpSum = float(1 / sqrt(fpSum));
    for (i = 0; i < 2; i++)
        fpKoef[i] *= fpSum * fpAmpl;
}
/////////////////////////////////////////////////////////////////////////////////////
// CLightningLeaf
/////////////////////////////////////////////////////////////////////////////////////
CPlot3D SLightningLeaf::GetSrcCoord(float fpFrac)
{
    int nIdx = Float2Int((points.GetSize()) * fpFrac);
    nIdx = Min(nIdx, points.GetSize() - 1);
    SXYZW &ptSrc = points[nIdx];
    return CPlot3D(ptSrc.x, ptSrc.y, ptSrc.z);
}
/////////////////////////////////////////////////////////////////////////////////////
// вспомогательная функция, рассчитывающая линейную комбинацию функций
static void CalcFunction(SLightningFunc &res, int *nIdx, float *fpKoef, int nNum, int nLeng)
{
    for (int i = 0; i < nLeng; i++)
    {
        float fpR = 0;
        for (int k = 0; k < nNum; k++)
        {
            fpR += lFunctions[nIdx[k]][i] * fpKoef[k];
        }
        res[i] = fpR;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void SLightningLeaf::CalcXYFunctions(float fpTime, SLightningFunc &funcX, SLightningFunc &funcY,
                                     float fpAmpl)
{
    float fpKoef[8], fpKoefUse[8];
    int nIdx[8], i;
    float fpTime2;
    SLightningLeafFunc randHF;

    fpTime = fpTime - fpCreateTime;
    if (fpTime > 100)
        fpTime = 100;
    while (fpTime > 1)
    {
        randX[2] = randX[1];
        randX[1] = randX[0];
        randX[0].CreateRandom(fpAmpl);
        randY[2] = randY[1];
        randY[1] = randY[0];
        randY[0].CreateRandom(fpAmpl);
        fpCreateTime += 1;
        fpTime -= 1;
    }
    fpTime2 = sqr(fpTime);
    fpKoef[0] = fpTime2 / 2;
    fpKoef[1] = fpKoef[0];
    fpKoef[2] = 0.5f + fpTime - fpTime2;
    fpKoef[3] = fpKoef[2];
    fpKoef[4] = 0.5f - fpTime + fpTime2 / 2;
    fpKoef[5] = fpKoef[4];
    fpKoef[6] = 1;
    fpKoef[7] = 1;

    randHF.CreateRandom(fpAmpl);
    for (i = 0; i < 3; i++)
    {
        nIdx[i * 2] = randX[i].nIdx[0];
        nIdx[i * 2 + 1] = randX[i].nIdx[1];
        fpKoefUse[i * 2] = randX[i].fpKoef[0];
        fpKoefUse[i * 2 + 1] = randX[i].fpKoef[1];
    }
    //	if ( fpTime > 0 )
    //	{
    fpKoefUse[6] = randHF.fpKoef[0];
    fpKoefUse[7] = randHF.fpKoef[1];
    //	}
    //	else
    //	{
    //		fpKoefUse[6] = 0;
    //		fpKoefUse[7] = 0;
    //	}
    nIdx[6] = randHF.nIdx[0] + N_L_FUNCS_NUMBER;
    nIdx[7] = randHF.nIdx[1] + N_L_FUNCS_NUMBER;
    for (i = 0; i < 8; i++)
        fpKoefUse[i] *= fpKoef[i];
    CalcFunction(funcX, nIdx, fpKoefUse, 8, points.GetSize());

    randHF.CreateRandom(fpAmpl);
    for (i = 0; i < 3; i++)
    {
        nIdx[i * 2] = randY[i].nIdx[0];
        nIdx[i * 2 + 1] = randY[i].nIdx[1];
        fpKoefUse[i * 2] = randY[i].fpKoef[0];
        fpKoefUse[i * 2 + 1] = randY[i].fpKoef[1];
    }
    //	if ( fpTime > 0 )
    //	{
    fpKoefUse[6] = randHF.fpKoef[0];
    fpKoefUse[7] = randHF.fpKoef[1];
    //	}
    //	else
    //	{
    //		fpKoefUse[6] = 0;
    //		fpKoefUse[7] = 0;
    //	}
    nIdx[6] = randHF.nIdx[0] + N_L_FUNCS_NUMBER;
    nIdx[7] = randHF.nIdx[1] + N_L_FUNCS_NUMBER;
    for (i = 0; i < 8; i++)
        fpKoefUse[i] *= fpKoef[i];
    CalcFunction(funcY, nIdx, fpKoefUse, 8, points.GetSize());
}
/////////////////////////////////////////////////////////////////////////////////////
// функция для рассчета корректной длины функции ( кратной четырем и лежащей в пределах )
inline int GetFuncLeng(float fpDist)
{
    int nRes = Float2Int(fpDist) & ~3;
    nRes = Max(4, Min(N_MAX_LIGHTNING_SEGMENTS, nRes));
    return nRes;
}
/////////////////////////////////////////////////////////////////////////////////////
// перерасчет параметров ветви
void SLightningLeaf::SetNewSrcDst(const CPlot3D &ptSrc, const CPlot3D &ptDst)
{
    float fpMainDist, fpLeng;
    src = ptSrc;
    mainDir = ptDst - ptSrc;
    fpMainDist = fabs(mainDir);
    int nLeng;
    if (fpMainDist < 10)
        nLeng = GetFuncLeng(50); // fpMainDist * 5 );
    else if (fpMainDist > 15)
    {
        fpMainDist = 15;
        nLeng = GetFuncLeng(75);
    }
    else
        nLeng = GetFuncLeng(fpMainDist * 5);
    CPlot3D ZDir(0.013f, -0.0666f, 1);
    XDir = mainDir ^ ZDir;
    YDir = XDir ^ mainDir;
    fpLeng = fabs(XDir);
    if (fpLeng > 0)
        XDir *= fpMainDist / fpLeng;
    else
        XDir = CPlot3D(0, 0, 0);
    fpLeng = fabs(YDir);
    if (fpLeng > 0)
        YDir *= fpMainDist / fpLeng;
    else
        YDir = CPlot3D(0, 0, 0);
    mainDir /= float(nLeng);
    points.SetSize(nLeng);
}
/////////////////////////////////////////////////////////////////////////////////////
void SLightningLeaf::CreateLeaf(const CPlot3D &ptSrc, const CPlot3D &ptDst, float fpTime,
                                float fpAmpl)
{
    int i;
    SetNewSrcDst(ptSrc, ptDst);
    fpCreateTime = fpTime;
    for (i = 0; i < 3; i++)
    {
        randX[i].CreateRandom(fpAmpl);
        randY[i].CreateRandom(fpAmpl);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void SLightningLeaf::CalcLeafPoints(float fpTime, float fpAmpl)
{
    int nLeng = points.GetSize();
    static SLightningFunc funcX, funcY;
    CalcXYFunctions(fpTime, funcX, funcY, fpAmpl);
    float fpStartX = funcX[0];
    float fpDeltaX = (funcX[nLeng - 1] - funcX[0]) / nLeng;
    float fpStartY = funcY[0];
    float fpDeltaY = (funcY[nLeng - 1] - funcY[0]) / nLeng;

    for (int i = 0; i < points.GetSize(); i++)
    {
        float fpX = funcX[i] - fpStartX;
        float fpY = funcY[i] - fpStartY;
        CPlot3D temp = src + mainDir * float(i) + XDir * fpX + YDir * fpY;
        points[i].x = temp.x;
        points[i].y = temp.y;
        points[i].z = temp.z;
        fpStartX += fpDeltaX;
        fpStartY += fpDeltaY;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// CLightning
/////////////////////////////////////////////////////////////////////////////////////
void CLightning::Clear()
{
    fpLWidth = 1;
    fpFinalWidth = 0.1f;
    pTexture.Clear();
    pTextureBall.Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CLightning::Create()
{
    Clear();
    ((CLightningManager *)GetManager())->lightnings.AddTail(this);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
float CLightning::GetAmpl()
{
    if (fpLWidth > 0)
        return 1.5f;
    return fpLWidth == 0 ? 2 : 1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CLightning::AddLeafToBV(SLightningLeaf &lf)
{
    for (int i = 0; i < lf.points.GetSize(); i++)
    {
        bv.ptMin.x = Min(bv.ptMin.x, lf.points[i].x);
        bv.ptMin.y = Min(bv.ptMin.y, lf.points[i].y);
        bv.ptMin.z = Min(bv.ptMin.z, lf.points[i].z);
        bv.ptMax.x = Max(bv.ptMax.x, lf.points[i].x);
        bv.ptMax.y = Max(bv.ptMax.y, lf.points[i].y);
        bv.ptMax.z = Max(bv.ptMax.z, lf.points[i].z);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CLightning::BuildBoundingVolume()
{
    bv.ptMin = CPlot3D(1e30f, 1e30f, 1e30f);
    bv.ptMax = CPlot3D(-1e30f, -1e30f, -1e30f);
    AddLeafToBV(leaf);
    for (int i = 0; i < N_LIGHTNING_SMALL_THREADS; i++)
        AddLeafToBV(leafSmall[i]);

    bv.ptCenter = (bv.ptMin + bv.ptMax) / 2;
    bv.ptMin -= bv.ptCenter;
    bv.ptMax -= bv.ptCenter;
    bv.fpRadius = fabs(bv.ptMax);
    bv.ptBone = CPlot3D(0, 0, 0);
    bv.dwFlags = BV_BOX | BV_SPHERE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CLightning::CreateNewLightningPoints()
{
    int i;
    float fpTime = theWorld.GetPreciseTime() / FP_RECREATE_TIME;
    float fpDestCircle = fpFinalWidth; // fpLWidth > 0 ? 0.05f : 0.3f;

    leaf.CalcLeafPoints(fpTime, GetAmpl());

    if (fpLWidth != 0)
    {
        CPlot3D ptRes = leaf.GetDst();
        static float fpHarm[N_LIGHTNING_SMALL_THREADS][2] = { { 2, 3 },       { 2.5f, 1.8f },
                                                              { 0.3f, 0.8f }, { 1.2f, 1.3f },
                                                              { 3, 5.1f },    { 2.1f, 1.76f } };
        for (i = 0; i < N_LIGHTNING_SMALL_THREADS; i++)
        {
            float fpRadius = fpDestCircle * fpSmallPlaces[i]; //( 1 - fpSmallPlaces[i] );
            float fpXK = float(sin(fpHarm[i][0] * fpTime / 4) * fpRadius);
            float fpYK = float(sin(fpHarm[i][1] * fpTime / 4) * fpRadius);
            CPlot3D ptDst = ptRes + leaf.XDir * fpXK + leaf.YDir * fpYK;
            leafSmall[i].src = leaf.GetSrcCoord(1 - fpSmallPlaces[i]);
            leafSmall[i].mainDir = (ptDst - leafSmall[i].src) * (1.0f / leafSmall[i].GetLeng());
        }
    }

    for (i = 0; i < N_LIGHTNING_SMALL_THREADS; i++)
        leafSmall[i].CalcLeafPoints(fpTime, GetAmpl());
}
/////////////////////////////////////////////////////////////////////////////////////
void CLightning::DrawBall(CRenderer *pTarget, const CPlot3D &ptWhere, int nPhase, float fpSize)
{
    int i;
    // draw sphere on the end
    // Define temporary arrays
    static CParticleToDrawArray resParts;
    static CXYZWArray inCoords;
    SParticleUV uv[15];
    //
    if (!pTarget->BindTexture(0, pTextureBall))
        return;
    /*uv.fpU0 = 0.75f;//0.25f * ( nPhase & 3 );
    uv.fpV0 = 0;//0.75f - 0.25f * ( ( nPhase >> 2 ) & 3 );
    uv.fpU1 = uv.fpU0 + 0.25f;
    uv.fpV1 = uv.fpV0 + 0.25f;*/
    for (i = 0; i < 15; i++)
    {
        uv[i].fpU0 = 0.25f * (i & 3);
        uv[i].fpV0 = 0.75f - 0.25f * ((i >> 2) & 3);
        uv[i].fpU1 = uv[i].fpU0 + 0.25f;
        uv[i].fpV1 = uv[i].fpV0 + 0.25f;
    }
    resParts.SetSize(8);
    inCoords.SetSize(8);

    CPlot3D ptLook = theCamera.GetCP() - ptWhere; // GetZAxis();
    float fpLeng = fabs(ptLook);
    if (fpLeng)
        ptLook /= fpLeng;
#ifndef _A4
    int nStartPhase = (Float2Int(ptWhere.x * 15) + GetWorldBase()->GetSegmentTimeCounter()) % 15;
#else
    int nStartPhase = (Float2Int(ptWhere.x * 15) + GetWorld()->GetSegmentTimeCounter()) % 15;
#endif //_A4
    for (i = 0; i < 8; i++)
    {
        float fpKoef = i * (1 / 8.0f);
        SXYZW &c = inCoords[i];
        SParticleToDraw &p = resParts[i];
        c = ptWhere + ptLook * fpKoef * fpSize;
        c.w = sqrt(1 - sqr(fpKoef)) * fpSize;      // * 1.6f;
        p.dwColor = RGBA_MAKE(255, 255, 255, 160); // 255 );
        p.pUV = &uv[(2 * i + nStartPhase) % 15];
    }
    pTarget->DrawParticles(false, inCoords, resParts);
}
/////////////////////////////////////////////////////////////////////////////////////
void CLightning::DrawLeaf(CRenderer *pTarget, SLightningLeaf &leaf, int nClipFlag)
{
    float fpVStart = float(random.Get(0, N_LINES_PER_TEXTURE - 1)) / N_LINES_PER_TEXTURE;
    pTarget->Draw3DLine(leaf.points, leaf.fpWidth, nClipFlag,
                        leaf.GetLineLeng() * FP_LIGHTNING_TEXTURE_PER_M, fpVStart,
                        1.0f / N_LINES_PER_TEXTURE);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CLightning::Draw(CRenderer *pTarget)
{
    int nClipFlag, i;
    // Set texture
    if (!pTarget->BindTexture(0, pTexture))
        return FALSE;

    // Checking for bounding volume
    nClipFlag = pTarget->CheckVisibility(bv);
    if (nClipFlag == TRV_INVISIBLE)
        return TRUE;

    // Create lightning`s set of point
    if (fpLWidth >= 0)
        CreateNewLightningPoints();

    // draw lightning itself
    if (fpLWidth < 0)
    {
        pTarget->Draw3DLine(leaf.points, leaf.fpWidth, nClipFlag);
        for (i = 0; i < N_LIGHTNING_SMALL_THREADS; i++)
            pTarget->Draw3DLine(leafSmall[i].points, leafSmall[i].fpWidth, nClipFlag);
    }
    else
    {
        DrawLeaf(pTarget, leaf, nClipFlag);
        for (i = 0; i < N_LIGHTNING_SMALL_THREADS; i++)
            DrawLeaf(pTarget, leafSmall[i], nClipFlag);
    }

    // draw sphere(s) on the end
    CPlot3D ptSrc = leaf.src;
    CPlot3D ptDst = leaf.GetDst();
    // CPlot3D ptDst = leaf.GetSrcCoord( 1 );

#ifndef _A4
    int nPhase = (GetWorldBase()->GetSegmentTimeCounter() / 2) % 15;
#else
    int nPhase = (GetWorld()->GetSegmentTimeCounter() / 2) % 15;
#endif //_A4

    float fpSize = fabs(ptSrc - ptDst) * 0.11f;
    if (fpLWidth > 0)
    {
        fpSize = Max(0.5f, fpSize);
        fpSize = Min(1.2f, fpSize);
    }
    if (fpLWidth >= 0)
        DrawBall(pTarget, ptDst, nPhase, fpSize);
    if (fpLWidth == 0)
        DrawBall(pTarget, ptSrc, nPhase, fpSize);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CLightning::MoveLightning(const CPlot3D &ptNSrc, const CPlot3D &ptNDst)
{
    leaf.SetNewSrcDst(ptNSrc, ptNDst);
    if (fpLWidth != 0)
    {
        for (int i = 0; i < N_LIGHTNING_SMALL_THREADS; i++)
        {
            float fpFrac = fpSmallPlaces[i];
            leafSmall[i].SetNewSrcDst(ptNSrc * fpFrac + ptNDst * (1 - fpFrac), ptNDst);
        }
    }
    else
    {
        for (int i = 0; i < N_LIGHTNING_SMALL_THREADS; i++)
        {
            CPlot3D ptDSrc, ptDDst;
            float fpRnd = fabs(ptNSrc - ptNDst) * 0.05f;
            ptDSrc = CPlot3D(random.GetFloat(-fpRnd, fpRnd), random.GetFloat(-fpRnd, fpRnd),
                             random.GetFloat(-fpRnd, fpRnd));
            ptDDst = ptDSrc; // CPlot3D( random.GetFloat( -fpRnd, fpRnd ), random.GetFloat( -fpRnd,
                             // fpRnd ), random.GetFloat( -fpRnd, fpRnd ) ) );
            leafSmall[i].SetNewSrcDst(ptNSrc + ptDSrc, ptNDst + ptDDst);
        }
    }
    if (fpLWidth < 0)
        CreateNewLightningPoints();
    BuildBoundingVolume();
}
/////////////////////////////////////////////////////////////////////////////////////
void CLightning::CreateLightning(const CPlot3D &ptNSrc, const CPlot3D &ptNDst, float fpNWidth,
                                 BYTE bColor)
{
    int i;
    float fpTime = theWorld.GetPreciseTime() / FP_RECREATE_TIME;
    fpLWidth = fpNWidth;
    fpNWidth = fabs(fpNWidth);
    fpFinalWidth = fpLWidth > 0 ? 0.05f : 0.3f;
    // CRAP for invoke lightning spell
    if (fabs(fabs(ptNDst.z - ptNSrc.z) - 30) < 0.01f)
        fpFinalWidth = 0.05f;
    // end of CRAP
    leaf.CreateLeaf(ptNSrc, ptNDst, fpTime, GetAmpl());
    leaf.fpWidth = fpNWidth;
    if (fpLWidth < 0)
        leaf.fpWidth *= 1.5f;
    if (fpNWidth != 0)
    {
        fpSmallPlaces[0] = 0.2f;
        fpSmallPlaces[1] = 0.3f;
        fpSmallPlaces[2] = 0.5f;
        fpSmallPlaces[3] = 0.8f;
        fpSmallPlaces[4] = 0.9f;
        fpSmallPlaces[5] = 0.9f;
        for (i = 0; i < N_LIGHTNING_SMALL_THREADS; i++)
        {
            float fpFrac = fpSmallPlaces[i];
            leafSmall[i].CreateLeaf(ptNSrc * fpFrac + ptNDst * (1 - fpFrac), ptNDst, fpTime,
                                    GetAmpl());
            leafSmall[i].fpWidth = fpNWidth / 2 * random.GetFloat(0.7f, 1.5f);
        }
    }
    else
    {
        leaf.fpWidth = 4;
        for (i = 0; i < N_LIGHTNING_SMALL_THREADS; i++)
        {
            CPlot3D ptDSrc, ptDDst;
            float fpRnd = fabs(ptNSrc - ptNDst) * 0.05f;
            ptDSrc = CPlot3D(random.GetFloat(-fpRnd, fpRnd), random.GetFloat(-fpRnd, fpRnd),
                             random.GetFloat(-fpRnd, fpRnd));
            ptDDst = ptDSrc; // CPlot3D( random.GetFloat( -fpRnd, fpRnd ), random.GetFloat( -fpRnd,
                             // fpRnd ), random.GetFloat( -fpRnd, fpRnd ) ) );
            leafSmall[i].CreateLeaf(ptNSrc + ptDSrc, ptNDst + ptDDst, fpTime, GetAmpl());
            leafSmall[i].fpWidth = 7 * 0.5f * random.GetFloat(0.7f, 1.5f);
        }
    }
    CreateNewLightningPoints();
    BuildBoundingVolume();
    bv.ptMin -= CPlot3D(1, 1, 1);
    bv.ptMax += CPlot3D(1, 1, 1);
    bv.fpRadius = fabs(bv.ptMax);
    switch (bColor)
    {
    case 0:
        pTextureBall.SetDesc(TTID_SPELL_MAIN, "SmtgLight"); // FireSmoke" );
        pTexture.SetDesc(TTID_SPELL_MAIN, "Lightning");     // FireSmoke" );
        break;
    case 2:
        pTextureBall.SetDesc(TTID_SPELL_MAIN,"SmtgLight2");
        pTexture.SetDesc(TTID_SPELL_MAIN,"Lightning2");
        break;
    case 3:
        pTextureBall.SetDesc(TTID_SPELL_MAIN,"SmtgLight3");
        pTexture.SetDesc(TTID_SPELL_MAIN,"Lightning3");
        break;
    default:
        pTextureBall.SetDesc(TTID_SPELL_MAIN, "DemonLight"); // FireSmoke" );
        pTexture.SetDesc(TTID_SPELL_MAIN, "DemonLightning"); // FireSmoke" );
        break;
    }
}

/////////////////////////////////////////////////////////////////////////////////////
// CLightning Manager
/////////////////////////////////////////////////////////////////////////////////////
LPGAMEBASE CLightningManager::CreateInstant(ETypeIDs tidTypeID)
{
    CLightning *pLightning = dbgnew CLightning;
    if (pLightning == NULL)
        return NULL;
    // pLightning->SetTypeID( tidTypeID );
    return pLightning;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CLightningManager::Draw(CRenderer *lpTarget)
{
    /*	static int nCounter = 0;
            if ( nCounter )
            {
                    nCounter--;
                    return TRUE;
            }
            nCounter = random.Get(1,3 );*/
    POSITION pos = list.GetHeadPosition();
    while (pos)
    {
        CLightning *pLightning = (CLightning *)list.GetNext(pos);
        pLightning->Draw(lpTarget);
    }
    lightnings.RemoveAll();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CLightningManager::Create()
{
    static int bOneTimeInitialize = FALSE;
    if (!bOneTimeInitialize)
    {
        bOneTimeInitialize = TRUE;
        InitializeLightningGenerator();
    }
    return CBaseManager::Create();
}
/////////////////////////////////////////////////////////////////////////////////////
