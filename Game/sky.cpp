#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Sky.h"
#include "WorldInterface.h"

/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                      class CSkyFigure functions                            ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
CSkyFigure::CSkyFigure()
{
    Create();
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
void CSkyFigure::RecalcVertexLighting(const CPlot3D &ptDir, D3DCOLORVALUE ambient,
                                      D3DCOLORVALUE sunlight, D3DCOLORVALUE fogColor)
{
    int i;
    DWORD dwAmbient = D3DRGBA(ambient.r, ambient.g, ambient.b, 1);
    DWORD dwFogColor = D3DRGBA(fogColor.r, fogColor.g, fogColor.b, 1);
    if (normZSqrts.GetSize() != colors.GetSize())
    {
        normZSqrts.SetSize(colors.GetSize());
        for (i = 0; i < normZSqrts.GetSize(); i++)
        {
            if (plots4[i / 4].z[i & 3] < 1)
                normZSqrts[i] = 0;
            else
                normZSqrts[i] = (float)sqrt(-norms4[i / 4].z[i & 3]);
        }
    }
    for (i = 0; i < colors.GetSize(); i++)
    {
        float fpSun;
        float fpX = norms4[i / 4].x[i & 3];
        float fpY = norms4[i / 4].y[i & 3];
        float fpZ = norms4[i / 4].z[i & 3];
        if (normZSqrts[i] == 0)
        {
            speculars[i] = RGBA_MAKE(0, 0, 0, 0); // dwFogColor; //RGBA_MAKE( 0,0,0,0 );
            colors[i] = D3DRGBA(ambient.r, ambient.g, ambient.b,
                                0); // RGBA_MAKE( 255, 255, 255, 255 ); //0,0,0,255 );
        }
        else
        {
            fpSun = fpX * ptDir.x + fpY * ptDir.y + fpZ * ptDir.z;
            if (fpSun < 0)
                fpSun = 0;
            fpSun = sqr(fpSun);
            ASSERT(fpZ < 0);
            speculars[i] = RGBA_MAKE(int(sunlight.r * fpSun * 150), int(sunlight.g * fpSun * 150),
                                     int(sunlight.b * fpSun * 150), int(normZSqrts[i] * 255));
            colors[i] = dwAmbient;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// CSkyObject
/////////////////////////////////////////////////////////////////////////////////////
CSkyObject::CSkyObject()
{
    SetSkyRotate(TRUE);
    SetTexture("sky00"); // default sky - daylight
    //	texture.SetDesc( TCTID_STATIC_TEXTURE, "sky00" );
}
/////////////////////////////////////////////////////////////////////////////////////
void CSkyObject::Draw(CRenderer *pRend, const CPlot3D &ptSkyPos)
{
    CPlot3D ptSky = ptSkyPos;
    ptSky.z -= 16;
    if (pSky)
    {
        if (IsRotateSky())
            pSky->SetOrientation(
                CQuaternion(CPlot3D(0, 0, 1), IWorldClient.GetPreciseTime() / 3000.0f));
        pSky->SetCP(ptSky);
        pSky->CreateCurrentPlacement();
        pRend->GetGPU()->PushMainMatrix(pSky->GetPlacement());
        pRend->SetProjectionMatrix();
        pRend->BindTexture(0, texture);

        pRend->DrawFigure(pSky, QNULL, VNULL, NULL, 255, FALSE);
        pRend->GetGPU()->PopMainMatrix();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CSkyObject::RecalcVertexLighting(const CPlot3D &ptDir, D3DCOLORVALUE ambient,
                                      D3DCOLORVALUE sunlight, D3DCOLORVALUE fogColor)
{
    if (pSky)
        pSky->RecalcVertexLighting(ptDir, ambient, sunlight, fogColor);
}
