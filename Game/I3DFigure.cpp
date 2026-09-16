#include "StdAfx.h"
#include "WideScreen.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DFigure.h"
#include "Renderer.h"
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DFigure::Create()
{
    CFigureLoaded::Create();

    pMaterial = NULL;
    pTexture = NULL;

    Identity(scale);

    ambientLight = D3DVECTOR(1, 1, 1);
    directionalLight = D3DVECTOR(0, 0, 0);

    SetOrientation(QNULL);
    SetRelativeToScreen(400, 300, 10);

    SetDefaultColor(0xff000000);
    SetDefaultSpecular(0xff000000);

    Show(TRUE);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DFigure::Destroy()
{
    pMaterial = NULL;
    pTexture = NULL;
    CFigureLoaded::Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DFigure::Draw(CRenderer *pRend)
{
    if (!IsVisible())
        return TRUE;

    CDXGeometryProcessor *pGPU = pRend->GetGPU();
    BOOL bCheck = FALSE;
    SHMatrix matrix;

    bCheck = pRend->BindTexture(0, pTexture);
    if (bCheck == FALSE)
        return ReportError("CI3DFigure::Draw.\n SetTexture failed.\n");

    // prepare placement matrix
    CreateCurrentPlacement();
    GetPlacement(matrix);
    // push this placement to all necessary stacks
    pGPU->PushMainMatrix(matrix);
    pGPU->PushMainMatrix(scale);

    pRend->SetProjectionMatrix();

    DWORD dwOldAmbient = pRend->GetAmbientColor();
    pRend->SetAmbientColor(ambientLight.x, ambientLight.y, ambientLight.z);
    // draw figure
    bCheck = pRend->DrawFigure(this, QNULL, CPlot3D(0, 0, 0), pMaterial, 255, FALSE);
    pRend->SetAmbientColor(dwOldAmbient);
    if (bCheck == FALSE)
        return ReportError("CI3DFigure::Draw.\n CFigure::Draw failed.\n");
    // pop placement from all stacks above

    pGPU->PopMainMatrix();
    pGPU->PopMainMatrix();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DFigure::SetRelativeToScreen(float xScr, float yScr, float zDist)
{
    xScr += WideScreen::currentShift;
    SetCP(CPlot3D((xScr / 400.f - 1.f) * VIEWING_ANGLE_TAN * zDist,
                  (yScr / 400.f - .75f) * VIEWING_ANGLE_TAN * zDist, zDist));
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DFigure::GetRelativeToScreen(float &xScr, float &yScr, float &zDist)
{
    xScr = GetCP().x / GetCP().z / VIEWING_ANGLE_TAN * 400 + 400;
    yScr = GetCP().y / GetCP().z / VIEWING_ANGLE_TAN * 400 + 300;
    zDist = GetCP().z;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DFigure::SetScale(float fScale)
{
    scale._11 = fScale;
    scale._22 = fScale;
    scale._33 = fScale;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DFigure::SetXScale(float fScale)
{
    scale._11 = fScale;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DFigure::SetYScale(float fScale)
{
    scale._22 = fScale;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DFigure::SetZScale(float fScale)
{
    scale._33 = fScale;
}
/////////////////////////////////////////////////////////////////////////////////////
float CI3DFigure::GetXScale()
{
    return scale._11;
}
/////////////////////////////////////////////////////////////////////////////////////
float CI3DFigure::GetYScale()
{
    return scale._22;
}
/////////////////////////////////////////////////////////////////////////////////////
float CI3DFigure::GetZScale()
{
    return scale._33;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DFigure::MakeBar(float fDepth, float fPixInMeter, float fL, float fT, float fR, float fB,
                         int nMapL, int nMapT, int nMapR, int nMapB)
{
    CI3DFigure *pFig = this;
    int i;
    SetFlatNorms();
    // fill components
    pFig->components.SetSize(4);
    for (i = 0; i < 4; i++)
    {
        pFig->components[i].gv = i;
        pFig->components[i].tv = i;
        pFig->components[i].nv = 0;
    }
    if (nMapL > nMapR)
    {
        nMapL--;
        nMapR--;
    }
    // fill maps
    pFig->maps.SetSize(4);
    pFig->maps[0].x = nMapL / 256.f;
    pFig->maps[0].y = 1 - nMapT / 256.f;
    pFig->maps[1].x = nMapL / 256.f;
    pFig->maps[1].y = 1 - nMapB / 256.f;
    pFig->maps[2].x = nMapR / 256.f;
    pFig->maps[2].y = 1 - nMapT / 256.f;
    pFig->maps[3].x = nMapR / 256.f;
    pFig->maps[3].y = 1 - nMapB / 256.f;
    // fill faces
    pFig->indices.RemoveAll();
    // AlexK: this is CLOCKWISE WINDING.
    pFig->indices.Add(0); // LB
    pFig->indices.Add(1); // LT
    pFig->indices.Add(2); // RB
    pFig->indices.Add(1);
    pFig->indices.Add(3);
    pFig->indices.Add(2);
    // fill vertices
    pFig->plots4.SetSize(1);
    for (i = 0; i < 4; i++)
    {
        pFig->plots4[0].z[i] = 0;
        pFig->plots4[0].w[i] = 1;
    }
    float fWidth2 = (fR - fL) / 2 / fPixInMeter;
    float fHeight2 = (fB - fT) / 2 / fPixInMeter;
    pFig->plots4[0].x[0] = -fWidth2; // LB
    pFig->plots4[0].y[0] = -fHeight2;
    pFig->plots4[0].x[1] = -fWidth2; // LT
    pFig->plots4[0].y[1] = fHeight2;
    pFig->plots4[0].x[2] = fWidth2; // RB
    pFig->plots4[0].y[2] = -fHeight2;
    pFig->plots4[0].x[3] = fWidth2; // RT
    pFig->plots4[0].y[3] = fHeight2;

    pFig->SetRelativeToScreen((fR + fL) / 2, (fT + fB) / 2, fDepth);
    pFig->SetScale(fDepth * fPixInMeter / 400 * VIEWING_ANGLE_TAN);

    for (i = 0; i < maps.GetSize(); i++)
    {
        maps[i].x += 1 / 512.f;
        maps[i].y -= 1 / 512.f;
    }
    MapAfterModify();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DFigure::MakeTilingBar(float fDepth, float fPixInMeter, float fL, float fT, float fR,
                               float fB, int nMapL, int nMapT, int nMapR, int nMapB, int nTilesX,
                               int nTilesY)
{
    CI3DFigure *pFig = this;
    int i, j, k, l, num;
    SetFlatNorms();

    float tilex = (fR - fL) / fPixInMeter / nTilesX;
    float tiley = (fB - fT) / fPixInMeter / nTilesY;

    // plots
    num = (nTilesX + 1) * (nTilesY + 1) / 4;
    if ((nTilesX + 1) * (nTilesY + 1) % 4)
        num++;
    pFig->plots4.SetSize(num);
    i = 0;
    j = 0;
    for (k = 0; k < num; k++)
        for (l = 0; l < 4; l++)
        {
            pFig->plots4[k].x[l] = i * tilex;
            pFig->plots4[k].y[l] = j * tiley;
            pFig->plots4[k].z[l] = 0;
            pFig->plots4[k].w[l] = 1;
            if (++i > nTilesX)
            {
                i = 0;
                if (j < nTilesY)
                    j++;
            }
        }

    // fill maps
    pFig->maps.SetSize(4);
    pFig->maps[0].x = nMapL / 256.f;
    pFig->maps[0].y = 1 - nMapT / 256.f;
    pFig->maps[1].x = nMapL / 256.f;
    pFig->maps[1].y = 1 - nMapB / 256.f;
    pFig->maps[2].x = nMapR / 256.f;
    pFig->maps[2].y = 1 - nMapT / 256.f;
    pFig->maps[3].x = nMapR / 256.f;
    pFig->maps[3].y = 1 - nMapB / 256.f;
    // components and indices
    pFig->components.SetSize(nTilesX * nTilesY * 4);
    pFig->indices.RemoveAll();
    int numc = 0;
    int numv = 0;
    for (j = 0; j < nTilesY; j++)
    {
        for (i = 0; i < nTilesX; i++)
        {
            for (k = 0; k < 4; k++)
            {
                pFig->components[numc + k].tv = k;
                pFig->components[numc + k].nv = 0;
            }
            pFig->components[numc + 0].gv = numv;
            pFig->components[numc + 1].gv = numv + nTilesX + 1;
            pFig->components[numc + 2].gv = numv + 1;
            pFig->components[numc + 3].gv = numv + nTilesX + 2;

            pFig->indices.Add(numc + 0);
            pFig->indices.Add(numc + 1);
            pFig->indices.Add(numc + 2);
            pFig->indices.Add(numc + 1);
            pFig->indices.Add(numc + 3);
            pFig->indices.Add(numc + 2);

            numc += 4;
            numv++;
        }
        numv++;
    }
    pFig->SetRelativeToScreen(fL, fT, fDepth);
    pFig->SetScale(fDepth * fPixInMeter / 400 * VIEWING_ANGLE_TAN);

    for (i = 0; i < maps.GetSize(); i++)
    {
        maps[i].x += 1 / 512.f;
        maps[i].y -= 1 / 512.f;
    }
    MapAfterModify();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DFigure::MakeBarFrame(float fDepth, float fPixInMeter, float fL, float fT, float fR,
                              float fB, float fFrameW, int nMapL, int nMapT, int nMapR, int nMapB)
{
    CI3DFigure *pFig = this;
    int i, j, k;
    SetFlatNorms();
    // fill components
    pFig->components.SetSize(16);
    for (i = 0; i < 4; i++)
    {
        pFig->components[i * 4].gv = i * 2;
        pFig->components[i * 4].tv = 0;
        pFig->components[i * 4].nv = 0;
        pFig->components[i * 4 + 1].gv = i * 2 + 1;
        pFig->components[i * 4 + 1].tv = 1;
        pFig->components[i * 4 + 1].nv = 0;
        pFig->components[i * 4 + 2].gv = i * 2 + 2;
        pFig->components[i * 4 + 2].tv = 2;
        pFig->components[i * 4 + 2].nv = 0;
        pFig->components[i * 4 + 3].gv = i * 2 + 3;
        pFig->components[i * 4 + 3].tv = 3;
        pFig->components[i * 4 + 3].nv = 0;
    }
    pFig->components[14].gv = 0;
    pFig->components[15].gv = 1;
    // fill maps
    pFig->maps.SetSize(4);
    pFig->maps[0].x = nMapL / 256.f;
    pFig->maps[0].y = 1 - nMapT / 256.f;
    pFig->maps[1].x = nMapL / 256.f;
    pFig->maps[1].y = 1 - nMapB / 256.f;
    pFig->maps[2].x = nMapR / 256.f;
    pFig->maps[2].y = 1 - nMapT / 256.f;
    pFig->maps[3].x = nMapR / 256.f;
    pFig->maps[3].y = 1 - nMapB / 256.f;
    // fill faces
    pFig->indices.RemoveAll();
    for (i = 0; i < 4; i++)
    {
        pFig->indices.Add(i * 4);
        pFig->indices.Add(i * 4 + 1);
        pFig->indices.Add(i * 4 + 2);
        pFig->indices.Add(i * 4 + 1);
        pFig->indices.Add(i * 4 + 3);
        pFig->indices.Add(i * 4 + 2);
    }
    // fill vertices
    pFig->plots4.SetSize(2);
    k = 0;
    for (i = 0; i < 2; i++)
        for (j = 0; j < 4; j++)
        {
            pFig->plots4[i].z[j] = 0;
            pFig->plots4[i].w[j] = 1;
        }
    float fWidth2 = (fR - fL) / 2 / fPixInMeter;
    float fHeight2 = (fB - fT) / 2 / fPixInMeter;
    float fWidthIn2 = ((fR - fL) / 2 - fFrameW) / fPixInMeter;
    float fHeightIn2 = ((fB - fT) / 2 - fFrameW) / fPixInMeter;
    pFig->plots4[0].x[0] = -fWidth2;
    pFig->plots4[0].y[0] = -fHeight2;
    pFig->plots4[0].x[1] = -fWidthIn2;
    pFig->plots4[0].y[1] = -fHeightIn2;
    pFig->plots4[0].x[2] = fWidth2;
    pFig->plots4[0].y[2] = -fHeight2;
    pFig->plots4[0].x[3] = fWidthIn2;
    pFig->plots4[0].y[3] = -fHeightIn2;
    pFig->plots4[1].x[0] = fWidth2;
    pFig->plots4[1].y[0] = fHeight2;
    pFig->plots4[1].x[1] = fWidthIn2;
    pFig->plots4[1].y[1] = fHeightIn2;
    pFig->plots4[1].x[2] = -fWidth2;
    pFig->plots4[1].y[2] = fHeight2;
    pFig->plots4[1].x[3] = -fWidthIn2;
    pFig->plots4[1].y[3] = fHeightIn2;

    pFig->SetRelativeToScreen((fR + fL) / 2, (fT + fB) / 2, fDepth);
    pFig->SetScale(fDepth * fPixInMeter / 400 * VIEWING_ANGLE_TAN);

    for (i = 0; i < maps.GetSize(); i++)
    {
        maps[i].x += 1 / 512.f;
        maps[i].y -= 1 / 512.f;
    }
    MapAfterModify();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DFigure::MakeBarFrame2(float fDepth, float fPixInMeter, float fL, float fT, float fR,
                               float fB, float fFrameW, int nMapL, int nMapT, int nMapR, int nMapB)
{
    CI3DFigure *pFig = this;
    int i, j, k;
    SetFlatNorms();
    // maps
    float fMapFrameX = (nMapR - nMapL) * fFrameW / (fR - fL);
    float fMapFrameY = (nMapB - nMapT) * fFrameW / (fB - fT);

    pFig->maps.SetSize(8);
    pFig->maps[0].x = nMapL / 256.f;
    pFig->maps[0].y = 1 - nMapT / 256.f;
    pFig->maps[1].x = nMapL / 256.f;
    pFig->maps[1].y = 1 - nMapB / 256.f;
    pFig->maps[2].x = nMapR / 256.f;
    pFig->maps[2].y = 1 - nMapT / 256.f;
    pFig->maps[3].x = nMapR / 256.f;
    pFig->maps[3].y = 1 - nMapB / 256.f;

    pFig->maps[4].x = (nMapL + fMapFrameX) / 256.f;
    pFig->maps[4].y = 1 - (nMapT + fMapFrameY) / 256.f;
    pFig->maps[5].x = (nMapL + fMapFrameX) / 256.f;
    pFig->maps[5].y = 1 - (nMapB - fMapFrameY) / 256.f;
    pFig->maps[6].x = (nMapR - fMapFrameX) / 256.f;
    pFig->maps[6].y = 1 - (nMapT + fMapFrameY) / 256.f;
    pFig->maps[7].x = (nMapR - fMapFrameX) / 256.f;
    pFig->maps[7].y = 1 - (nMapB - fMapFrameY) / 256.f;

    // indices
    pFig->indices.RemoveAll();
    pFig->indices.Add(0);
    pFig->indices.Add(4);
    pFig->indices.Add(2);
    pFig->indices.Add(4);
    pFig->indices.Add(6);
    pFig->indices.Add(2);
    pFig->indices.Add(2);
    pFig->indices.Add(6);
    pFig->indices.Add(3);
    pFig->indices.Add(6);
    pFig->indices.Add(7);
    pFig->indices.Add(3);
    pFig->indices.Add(3);
    pFig->indices.Add(7);
    pFig->indices.Add(1);
    pFig->indices.Add(7);
    pFig->indices.Add(5);
    pFig->indices.Add(1);
    pFig->indices.Add(1);
    pFig->indices.Add(5);
    pFig->indices.Add(0);
    pFig->indices.Add(5);
    pFig->indices.Add(4);
    pFig->indices.Add(0);

    // fill components
    pFig->components.SetSize(8);
    for (k = 0; k < 8; k++)
    {
        pFig->components[k].gv = k;
        pFig->components[k].tv = k;
        pFig->components[k].nv = 0;
    }

    // fill vertices
    pFig->plots4.SetSize(2);
    k = 0;
    for (i = 0; i < 2; i++)
        for (j = 0; j < 4; j++)
        {
            pFig->plots4[i].z[j] = 0;
            pFig->plots4[i].w[j] = 1;
        }
    float fWidth2 = (fR - fL) / 2 / fPixInMeter;
    float fHeight2 = (fB - fT) / 2 / fPixInMeter;
    float fWidthIn2 = ((fR - fL) / 2 - fFrameW) / fPixInMeter;
    float fHeightIn2 = ((fB - fT) / 2 - fFrameW) / fPixInMeter;
    pFig->plots4[0].x[0] = -fWidth2;
    pFig->plots4[0].y[0] = -fHeight2;
    pFig->plots4[0].x[1] = -fWidth2;
    pFig->plots4[0].y[1] = fHeight2;
    pFig->plots4[0].x[2] = fWidth2;
    pFig->plots4[0].y[2] = -fHeight2;
    pFig->plots4[0].x[3] = fWidth2;
    pFig->plots4[0].y[3] = fHeight2;
    pFig->plots4[1].x[0] = -fWidthIn2;
    pFig->plots4[1].y[0] = -fHeightIn2;
    pFig->plots4[1].x[1] = -fWidthIn2;
    pFig->plots4[1].y[1] = fHeightIn2;
    pFig->plots4[1].x[2] = fWidthIn2;
    pFig->plots4[1].y[2] = -fHeightIn2;
    pFig->plots4[1].x[3] = fWidthIn2;
    pFig->plots4[1].y[3] = fHeightIn2;

    pFig->SetRelativeToScreen((fR + fL) / 2, (fT + fB) / 2, fDepth);
    pFig->SetScale(fDepth * fPixInMeter / 400 * VIEWING_ANGLE_TAN);
    for (i = 0; i < maps.GetSize(); i++)
    {
        maps[i].x += 1 / 512.f;
        maps[i].y -= 1 / 512.f;
    }
    MapAfterModify();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DFigure::MakeCircle(float fDepth, float fPixInMeter, float fX, float fY, float fR,
                            int nEdges, int nMapX, int nMapY, int nMapR)
{
    CI3DFigure *pFig = this;
    int i, j, k;
    SetFlatNorms();
    // fill components
    pFig->components.SetSize(nEdges);
    for (i = 0; i < nEdges; i++)
    {
        pFig->components[i].gv = i;
        pFig->components[i].tv = i;
        pFig->components[i].nv = 0;
    }
    // fill maps
    pFig->maps.SetSize(nEdges);
    for (i = 0; i < nEdges; i++)
    {
        pFig->maps[i].x = float(nMapX + cos(2 * FP_PI * i / nEdges) * nMapR) / 256.f;
        pFig->maps[i].y = 1 - float(nMapY + sin(2 * FP_PI * i / nEdges) * nMapR) / 256.f;
    }
    // fill faces
    pFig->indices.RemoveAll();
    for (i = 1; i < nEdges - 1; i++)
    {
        pFig->indices.Add(0);
        pFig->indices.Add(i + 1);
        pFig->indices.Add(i);
    }
    // fill vertices
    int num = nEdges / 4;
    pFig->plots4.SetSize(num);
    k = 0;
    for (i = 0; i < num; i++)
        for (j = 0; j < 4; j++)
        {
            pFig->plots4[i].x[j] = float(cos(2 * FP_PI * k / nEdges)) * fR / fPixInMeter;
            pFig->plots4[i].y[j] = float(sin(2 * FP_PI * k / nEdges)) * fR / fPixInMeter;
            pFig->plots4[i].z[j] = 0;
            pFig->plots4[i].w[j] = 1;
            k++;
        }
    pFig->SetRelativeToScreen(fX, fY, fDepth);
    pFig->SetScale(fDepth * fPixInMeter / 400 * VIEWING_ANGLE_TAN);

    for (i = 0; i < maps.GetSize(); i++)
    {
        maps[i].x += 1 / 512.f;
        maps[i].y -= 1 / 512.f;
    }
    MapAfterModify();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DFigure::MakeCircleFrame(float fDepth, float fPixInMeter, float fX, float fY, float fR,
                                 int nEdges, float fFrameW, int nMapL, int nMapT, int nMapR,
                                 int nMapB)
{
    CI3DFigure *pFig = this;
    int i, k;
    SetFlatNorms();
    // fill components
    pFig->components.SetSize(nEdges * 4);
    for (i = 0; i < nEdges; i++)
    {
        pFig->components[i * 4].gv = i * 2;
        pFig->components[i * 4].tv = 0;
        pFig->components[i * 4].nv = 0;
        pFig->components[i * 4 + 1].gv = i * 2 + 1;
        pFig->components[i * 4 + 1].tv = 1;
        pFig->components[i * 4 + 1].nv = 0;
        pFig->components[i * 4 + 2].gv = i * 2 + 2;
        pFig->components[i * 4 + 2].tv = 2;
        pFig->components[i * 4 + 2].nv = 0;
        pFig->components[i * 4 + 3].gv = i * 2 + 3;
        pFig->components[i * 4 + 3].tv = 3;
        pFig->components[i * 4 + 3].nv = 0;
    }
    pFig->components[(nEdges - 1) * 4 + 2].gv = 0;
    pFig->components[(nEdges - 1) * 4 + 3].gv = 1;
    // fill maps
    pFig->maps.SetSize(4);
    pFig->maps[0].x = nMapL / 256.f;
    pFig->maps[0].y = 1 - nMapT / 256.f;
    pFig->maps[1].x = nMapL / 256.f;
    pFig->maps[1].y = 1 - nMapB / 256.f;
    pFig->maps[2].x = nMapR / 256.f;
    pFig->maps[2].y = 1 - nMapT / 256.f;
    pFig->maps[3].x = nMapR / 256.f;
    pFig->maps[3].y = 1 - nMapB / 256.f;
    // fill faces
    pFig->indices.RemoveAll();
    for (i = 0; i < nEdges; i++)
    {
        pFig->indices.Add(i * 4);
        pFig->indices.Add(i * 4 + 1);
        pFig->indices.Add(i * 4 + 2);
        pFig->indices.Add(i * 4 + 1);
        pFig->indices.Add(i * 4 + 3);
        pFig->indices.Add(i * 4 + 2);
    }
    // fill vertices
    int num = nEdges / 2;
    pFig->plots4.SetSize(num);
    k = 0;
    for (i = 0; i < num; i++)
    {
        pFig->plots4[i].x[0] = float(cos(2 * FP_PI * k / nEdges)) * fR / fPixInMeter;
        pFig->plots4[i].y[0] = float(sin(2 * FP_PI * k / nEdges)) * fR / fPixInMeter;
        pFig->plots4[i].z[0] = 0;
        pFig->plots4[i].w[0] = 1;
        pFig->plots4[i].x[1] = float(cos(2 * FP_PI * k / nEdges)) * (fR - fFrameW) / fPixInMeter;
        pFig->plots4[i].y[1] = float(sin(2 * FP_PI * k / nEdges)) * (fR - fFrameW) / fPixInMeter;
        pFig->plots4[i].z[1] = 0;
        pFig->plots4[i].w[1] = 1;
        k++;
        pFig->plots4[i].x[2] = float(cos(2 * FP_PI * k / nEdges)) * fR / fPixInMeter;
        pFig->plots4[i].y[2] = float(sin(2 * FP_PI * k / nEdges)) * fR / fPixInMeter;
        pFig->plots4[i].z[2] = 0;
        pFig->plots4[i].w[2] = 1;
        pFig->plots4[i].x[3] = float(cos(2 * FP_PI * k / nEdges)) * (fR - fFrameW) / fPixInMeter;
        pFig->plots4[i].y[3] = float(sin(2 * FP_PI * k / nEdges)) * (fR - fFrameW) / fPixInMeter;
        pFig->plots4[i].z[3] = 0;
        pFig->plots4[i].w[3] = 1;
        k++;
    }
    pFig->SetRelativeToScreen(fX, fY, fDepth);
    pFig->SetScale(fDepth * fPixInMeter / 400 * VIEWING_ANGLE_TAN);

    for (i = 0; i < maps.GetSize(); i++)
    {
        maps[i].x += 1 / 512.f;
        maps[i].y -= 1 / 512.f;
    }
    MapAfterModify();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DFigure::MakeTriangle(float fDepth, float fPixInMeter, float fX1, float fY1, float fX2,
                              float fY2, float fX3, float fY3, float fMapX1, float fMapY1,
                              float fMapX2, float fMapY2, float fMapX3, float fMapY3)
{
    CI3DFigure *pFig = this;
    int i;
    SetFlatNorms();
    // fill components
    pFig->components.SetSize(4);
    for (i = 0; i < 4; i++)
    {
        pFig->components[i].gv = i;
        pFig->components[i].tv = i;
        pFig->components[i].nv = 0;
    }
    // fill maps
    pFig->maps.SetSize(4);
    pFig->maps[0].x = fMapX1 / 256.f;
    pFig->maps[0].y = 1 - fMapY1 / 256.f;
    pFig->maps[1].x = fMapX2 / 256.f;
    pFig->maps[1].y = 1 - fMapY2 / 256.f;
    pFig->maps[2].x = fMapX3 / 256.f;
    pFig->maps[2].y = 1 - fMapY3 / 256.f;
    pFig->maps[3].x = 0;
    pFig->maps[3].y = 0;
    // fill faces
    pFig->indices.RemoveAll();
    pFig->indices.Add(0);
    pFig->indices.Add(1);
    pFig->indices.Add(2);
    // fill vertices
    pFig->plots4.SetSize(1);
    for (i = 0; i < 4; i++)
    {
        pFig->plots4[0].z[i] = 0;
        pFig->plots4[0].w[i] = 1;
    }
    pFig->plots4[0].x[0] = 0;
    pFig->plots4[0].y[0] = 0;
    pFig->plots4[0].x[1] = (fX2 - fX1) / fPixInMeter;
    pFig->plots4[0].y[1] = (fY2 - fY1) / fPixInMeter;
    pFig->plots4[0].x[2] = (fX3 - fX1) / fPixInMeter;
    pFig->plots4[0].y[2] = (fY3 - fY1) / fPixInMeter;
    pFig->plots4[0].x[3] = 0;
    pFig->plots4[0].y[3] = 0;

    pFig->SetRelativeToScreen(fX1, fY1, fDepth);
    pFig->SetScale(fDepth * fPixInMeter / 400 * VIEWING_ANGLE_TAN);

    for (i = 0; i < maps.GetSize(); i++)
    {
        maps[i].x += 1 / 512.f;
        maps[i].y -= 1 / 512.f;
    }
    MapAfterModify();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DFigure::SetFlatNorms()
{
    CI3DFigure *pFig = this;
    int i;
    // fill normals
    pFig->norms4.SetSize(1);
    pFig->colors.SetSize(4);
    pFig->speculars.SetSize(4);
    for (i = 0; i < 4; i++)
    {
        pFig->norms4[0].x[i] = 0;
        pFig->norms4[0].y[i] = 0;
        pFig->norms4[0].z[i] = 1;
        pFig->norms4[0].w[i] = 1;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DFigure::MapAfterModify()
{
    MapPlots(plots4.GetData(), plots4.GetSize());
    MapNorms(norms4.GetData(), norms4.GetSize());
    MapMaps(maps.GetData(), maps.GetSize());
    MapComponents(components.GetData(), components.GetSize());
    MapLightComponents(lightcomponents.GetData(), lightcomponents.GetSize());
    MapIndices(indices.GetData(), indices.GetSize());
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DFigure::SetPlotsSize(int num)
{
    plots4.SetSize(num);
    for (int i = 0; i < num; i++)
        for (int j = 0; j < 4; j++)
        {
            plots4[i].z[j] = 0;
            plots4[i].w[j] = 1;
        }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DFigure::SetMapsSize(int num)
{
    maps.SetSize(num);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DFigure::SetIndicesSize(int num)
{
    indices.SetSize(num);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DFigure::SetComponentsSize(int num)
{
    components.SetSize(num);
    for (int i = 0; i < num; i++)
        components[i].nv = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
