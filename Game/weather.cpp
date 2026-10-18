#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "world.h"
#include "worldClient.h"
#include "Camera.h"
#include "weather.h"
#include "spline.h"
#include "SoundManager.h"
// for some reasons (late introduction of visualizator concept)
// weather is strongly DX dependent piece of code, so it requires this includes
#include "DXGeometryProcessor.h"
#include "Renderer.h"
/////////////////////////////////////////////////////////////////////////////////////
// Maximum number of vertexes for particles
const int N_MAX_PARTICLES = 32768; // 8192;
// time period of one particle falling
const float FP_DROP_RAIN_CYCLE = 200;  // 60;
const float FP_DROP_SNOW_CYCLE = 1000; // 400;
// speed of falling in meters per segment
const float FP_RAIN_DROP_SPEED = 0.5f;
const float FP_SNOW_DROP_SPEED = 0.1f;
// number of drops per meter
const float FP_DROP_DENSITY = 2;
const float FP_DROP_DENSITY_1 = 1 / FP_DROP_DENSITY;
// Z coord dispersion of drops
const float FP_DROP_Z_DISP = 10;
// number of particle squares in one side
const int N_PARTICLES_S_NUMBER = 40;
const int N_PARTICLES_PER_AXIS = N_PARTICLES_S_NUMBER * 2 + 1;
// average Z coord of one drop
const float FP_DROP_RAIN_MIDDLE_Z = FP_DROP_RAIN_CYCLE * FP_RAIN_DROP_SPEED / 2 - FP_DROP_Z_DISP;
const float FP_DROP_SNOW_MIDDLE_Z = FP_DROP_SNOW_CYCLE * FP_SNOW_DROP_SPEED / 2 - FP_DROP_Z_DISP;
// sizes of particles
const float FP_DROP_H = 1.0f;   // 0.4f
const float FP_DROP_W = 0.033f; // 0.033f
const float FP_LIMIT_DISTANCE = N_PARTICLES_S_NUMBER / FP_DROP_DENSITY + FP_DROP_Z_DISP;
const float FP_SNOWFLAKE_SIZE = 0.2f;
const float FP_SNOWFLAKE_H = FP_SNOWFLAKE_SIZE;
const int N_FUNCTION_STEPS = 128;
// параметр, вли€ющий на закон распределени€ случайных функций
const float FP_RANDOM_FUNC_P = 0.3f;
// средний размах случайных фунций дл€ снежинок
const float FP_RANDOM_AMPLITUDE = 0.5f;

/////////////////////////////////////////////////////////////////////////////////////
// CRandomFunction
class CRandomFunction
{
    float fpRandom[N_FUNCTION_STEPS];
    SPolynom3 polynoms[N_FUNCTION_STEPS];

public:
    float operator()(float fpX);
    // create random function with average squared deviation of fpNewSCO
    void Create(float fpNewSCO);
};

/////////////////////////////////////////////////////////////////////////////////////
float CRandomFunction::operator()(float fpX)
{
    int n = int(fpX);
    if (n < 0 || n >= N_FUNCTION_STEPS - 4)
        return 0;
    float fpFrac = fpX - n;
    return polynoms[n].Calc(fpFrac);
}

void CRandomFunction::Create(float fpNewSCO)
{
    int i;
    float fpDisp;
    for (i = 0; i < N_FUNCTION_STEPS; i++)
        fpRandom[i] = float(rand() - RAND_MAX / 2);
    for (i = 1; i < N_FUNCTION_STEPS; i++)
        fpRandom[i] = fpRandom[i] * FP_RANDOM_FUNC_P + fpRandom[i - 1] * (1 - FP_RANDOM_FUNC_P);
    for (i = N_FUNCTION_STEPS - 2; i > 1; i--)
        fpRandom[i] = fpRandom[i] * FP_RANDOM_FUNC_P + fpRandom[i + 1] * (1 - FP_RANDOM_FUNC_P);
    for (i = 0; i < N_FUNCTION_STEPS - 20; i++)
        fpRandom[i] = fpRandom[i + 20];
    fpDisp = 0;
    for (i = 0; i < N_FUNCTION_STEPS; i++)
        fpDisp += sqr(fpRandom[i]);
    fpDisp = float(sqrt(fpDisp / N_FUNCTION_STEPS));
    for (i = 0; i < N_FUNCTION_STEPS; i++)
        fpRandom[i] *= fpNewSCO / fpDisp;
    // lets create kinda spline :)
    for (i = 0; i < N_FUNCTION_STEPS - 1; i++)
    {
        float fpD0, fpD1;
        if (i == 0)
            fpD0 = fpRandom[1] - fpRandom[0];
        else
            fpD0 = (fpRandom[i + 1] - fpRandom[i - 1]) / 2;
        if (i == N_FUNCTION_STEPS - 2)
            fpD1 = fpRandom[i + 1] - fpRandom[i];
        else
            fpD1 = (fpRandom[i + 2] - fpRandom[i]) / 2;
        polynoms[i].Spline22(fpRandom[i], fpD0, fpRandom[i + 1], fpD1, 1);
    }
}

static CRandomFunction rndX, rndY, rndZ, rndX1, rndY1;

struct SWeatherInit
{
    SWeatherInit()
    {
        rndX.Create(FP_RANDOM_AMPLITUDE);
        rndY.Create(FP_RANDOM_AMPLITUDE);
        rndX1.Create(FP_RANDOM_AMPLITUDE);
        rndY1.Create(FP_RANDOM_AMPLITUDE);
        rndZ.Create(FP_RANDOM_AMPLITUDE);
    }
};
static SWeatherInit weatherInit;
/////////////////////////////////////////////////////////////////////////////////////
// CWeather
void CWeather::Clear()
{
    if (hSnd)
    {
        theWorld.GetSoundManager()->CancelFX(hSnd);
        hSnd = NULL;
    }

    pTexture.Clear();
    pWorld = NULL;
    nCurrentVertice = 0;
    ptWind = CPlot3D(0, 0, 0);
    nextWeather = curWeather = EW_CLEAR;
    nPowerCheck = N_WEATHER_POWER_GRADATIONS;
    fpWeatherStartTime = 0;
    fpWeatherTransformTime = 0;
}

/////////////////////////////////////////////////////////////////////////////////////
void CWeather::InitArrays()
{
    int i;

    srcVertexes.SetSize(N_MAX_PARTICLES);
    tlVertexes.SetSize(N_MAX_PARTICLES * 3);
    kinds.SetSize(N_MAX_PARTICLES);
    order.SetSize(N_PARTICLES_PER_AXIS);

    for (i = 0; i < N_PARTICLES_S_NUMBER; i++)
    {
        order[i * 2] = N_PARTICLES_S_NUMBER - i;
        order[i * 2 + 1] = -order[i * 2];
    }
    order[order.GetSize() - 1] = 0;

    float fpUVs[4][2] = { { 0.02f, 0.02f }, { 0.98f, 0.02f }, { 0.5f, 0.98f }, { 0, 0 } };
    for (i = 0; i < tlVertexes.GetSize(); i++)
    {
        D3DTLVERTEX &tl = tlVertexes[i];
        tl.color = RGBA_MAKE(0xFF, 0xFF, 0xFF, 0xFF);
        tl.specular = RGBA_MAKE(0, 0, 0, 0xFF);
        tl.tu = fpUVs[i % 3][0] / 4;
        tl.tv = 1 - (1 - fpUVs[i % 3][1]) / 4;
    }

    tFaces.SetSize(N_MAX_PARTICLES * 3);
    for (i = 0; i < tFaces.GetSize(); i++)
        tFaces[i] = i;
}

/////////////////////////////////////////////////////////////////////////////////////
void CWeather::AddPoint(float fpX, float fpY, float fpZ)
{
    SXYZW4 &s = srcVertexes[nCurrentVertice >> 2];
    int nIdx = nCurrentVertice & 3;
    s.x[nIdx] = fpX;
    s.y[nIdx] = fpY;
    s.z[nIdx] = fpZ;
    nCurrentVertice++;
}

/////////////////////////////////////////////////////////////////////////////////////
void CWeather::SetWeather(EWeather weather, float fpTransformTime)
{
    pTexture.SetDesc(TTID_SPELL_MAIN, "RainSnow");

    if (hSnd && weather != EW_CLEAR)
    {
        theWorld.GetSoundManager()->CancelFX(hSnd);
        hSnd = NULL;
    }
    if (weather == EW_RAIN) //&& fpTransformTime != 0 ) // curWeather == EW_CLEAR &&
    {
        hSnd = theWorld.GetSoundManager()->BeginFX("nature\\rain\\circle.wav", 1000, 0, 0,
                                                   TRUE); // "nature\\rain\\start.wav"
    }
    /*if ( weather == EW_CLEAR && curWeather == EW_RAIN && fpTransformTime != 0 )
    {
            theWorld.GetSoundManager()->BeginFX( "nature\\rain\\end.wav", 0, 100, 0, FALSE );
    }*/
    nextWeather = weather;
    if (curWeather == EW_CLEAR)
        curWeather = nextWeather;
    fpWeatherStartTime = (float)pWorld->GetSegmentTimeCounter();
    fpWeatherTransformTime = fpTransformTime;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWeather::SwitchWeather()
{
    curWeather = nextWeather;
    if (curWeather == EW_RAIN)
    {
        // hSnd = theWorld.GetSoundManager()->BeginFX( "nature\\rain\\circle.wav", 0, 100, 0, TRUE
        // );
        if (hSnd != NULL)
            theWorld.GetSoundManager()->UpdateFX(hSnd, 100, 0);
    }
    if (curWeather == EW_CLEAR)
    {
        if (hSnd != NULL)
        {
            theWorld.GetSoundManager()->CancelFX(hSnd);
            hSnd = NULL;
        }
    }
    nPowerCheck = N_WEATHER_POWER_GRADATIONS;
    fpWeatherTransformTime = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWeather::SetupPower(float fpTime)
{
    if (fpWeatherTransformTime == 0)
    {
        SwitchWeather();
        return;
    }
    float fpFrac = (fpTime - fpWeatherStartTime) / fpWeatherTransformTime;
    if (fpFrac > 1)
    {
        SwitchWeather();
        return;
    }
    if (nextWeather == EW_CLEAR)
        fpFrac = (1 + (float)cos(fpFrac * 3.14159)) / 2;
    else
        fpFrac = (1 - (float)cos(fpFrac * 3.14159)) / 2;
    if (hSnd != NULL)
        theWorld.GetSoundManager()->UpdateFX(hSnd, Float2Int(100 * fpFrac), 0);
    nPowerCheck = Float2Int(fpFrac * N_WEATHER_POWER_GRADATIONS);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWeather::SetWind(const CPlot3D &newWindVector, float fpNewWindForce)
{
    ptWind = newWindVector * fpNewWindForce;
}

/////////////////////////////////////////////////////////////////////////////////////
BOOL CWeather::DrawRain(CRenderer *pTarget)
{
    int x, y, i, nRes, nParticle, ix, iy;
    CPlot3D ptRainWind = ptWind;
    ptRainWind.z -= FP_RAIN_DROP_SPEED;
    float T = pWorld->GetPreciseTime();
    CDXGeometryProcessor *pGPU = pTarget->GetGPU();

    CPlot3D ptCameraPos = pWorld->GetCamera()->GetCP();
    float fpShiftAmount = (ptCameraPos.z - FP_DROP_RAIN_MIDDLE_Z) / ptRainWind.z;
    int nCameraX = Float2Int((ptCameraPos.x - ptRainWind.x * fpShiftAmount) * FP_DROP_DENSITY);
    int nCameraY = Float2Int((ptCameraPos.y - ptRainWind.y * fpShiftAmount) * FP_DROP_DENSITY);
    int nClipFlag;

    nCurrentVertice = 0;
    nParticle = 0;
    srcVertexes.SetSize(N_MAX_PARTICLES);
    static int nRandomTable[32] = { 17,  43,  456, 942, 32,  234, 865, 95,  321, 47,  909,
                                    284, 543, 396, 193, 120, 98,  784, 633, 10,  259, 77,
                                    118, 66,  921, 849, 356, 80,  475, 235, 213, 826 };

    float fpUpX, fpUpY;
    float fpWindLeng = fabs(ptRainWind);
    fpUpX = -ptRainWind.x / fpWindLeng * FP_DROP_H;
    fpUpY = -ptRainWind.y / fpWindLeng * FP_DROP_H;
    float fpMinH, fpMaxH;
    fpMinH = Max(0.0f, ptCameraPos.z - FP_LIMIT_DISTANCE);
    fpMaxH = ptCameraPos.z + FP_LIMIT_DISTANCE;
    for (iy = 0; iy < N_PARTICLES_PER_AXIS; iy++)
    {
        y = order[iy];
        for (ix = 0; ix < N_PARTICLES_PER_AXIS; ix++)
        {
            x = order[ix];
            float fpX, fpY, fpZ;
            int nX = nCameraX + x;
            int nY = nCameraY + y;
            int nStep;
            float fpTime = T + ((nX * 172 + nY * 13 + nRandomTable[(nX * nX + nY) & 31]) & 0xffff);
            nStep = Float2Int(fpTime / FP_DROP_RAIN_CYCLE);
            if ((nStep & (N_WEATHER_POWER_GRADATIONS - 1)) >= nPowerCheck)
                continue;
            float fpTimeStep = (fpTime - nStep * FP_DROP_RAIN_CYCLE);
            fpZ = FP_DROP_SNOW_MIDDLE_Z + fpTimeStep * ptRainWind.z;
            while (fpZ > fpMaxH)
                fpZ -= 2 * FP_LIMIT_DISTANCE;
            while (fpZ < fpMinH)
                fpZ += 2 * FP_LIMIT_DISTANCE;
            /*		if ( fpZ < fpMinH || fpZ > fpMaxH )
                                            continue;*/
            fpX = float(nX) * FP_DROP_DENSITY_1 + fpTimeStep * ptRainWind.x;
            fpY = float(nY) * FP_DROP_DENSITY_1 + fpTimeStep * ptRainWind.y;
            fpX += (((nStep * 197) & 255) - 128) / 128.0f * FP_DROP_DENSITY_1;
            fpY += (((nStep * 67) & 255) - 128) / 128.0f * FP_DROP_DENSITY_1;
            fpZ += (((nStep * 129) & 255) - 128) / 128.0f * FP_DROP_Z_DISP;
            kinds[nParticle] = nStep & 3;
            if (abs(x) > abs(y))
            {
                if (x < 0)
                {
                    AddPoint(fpX, fpY - FP_DROP_W, fpZ);
                    AddPoint(fpX, fpY + FP_DROP_W, fpZ);
                    AddPoint(fpX + fpUpX, fpY + fpUpY, fpZ + FP_DROP_H);
                }
                else
                {
                    AddPoint(fpX, fpY + FP_DROP_W, fpZ);
                    AddPoint(fpX, fpY - FP_DROP_W, fpZ);
                    AddPoint(fpX + fpUpX, fpY + fpUpY, fpZ + FP_DROP_H);
                }
            }
            else
            {
                if (y > 0)
                {
                    AddPoint(fpX - FP_DROP_W, fpY, fpZ);
                    AddPoint(fpX + FP_DROP_W, fpY, fpZ);
                    AddPoint(fpX + fpUpX, fpY + fpUpY, fpZ + FP_DROP_H);
                }
                else
                {
                    AddPoint(fpX + FP_DROP_W, fpY, fpZ);
                    AddPoint(fpX - FP_DROP_W, fpY, fpZ);
                    AddPoint(fpX + fpUpX, fpY + fpUpY, fpZ + FP_DROP_H);
                }
            }
            nParticle++;
        }
    }

    srcVertexes.SetSize((nCurrentVertice + 3) >> 2);
    transCoords.SetSize(srcVertexes.GetSize() * 4); // ARRAY BOUNDS CHECK
    clips.SetSize(srcVertexes.GetSize() * 4);
    nClipFlag = pGPU->GetGeometryProcessor()->TransformVertices(
        pGPU->GetMatrix(MAT_STACK_PROJSPACE), srcVertexes.GetData(), transCoords.GetData(),
        clips.GetData(), srcVertexes.GetSize());

    nRes = 0;
    tlVertexes.SetSize(nCurrentVertice); // ARRAY BOUNDS CHECK
    BYTE *pKinds = kinds.GetData();
    for (i = 0; i < nCurrentVertice; i += 3, pKinds++)
    {
        if (clips[i] || clips[i + 1] || clips[i + 2])
            continue;
        if (transCoords[i].z < 4)
            continue;
        Copy128Bits(&tlVertexes[nRes].sx, &transCoords[i].x);
        Copy128Bits(&tlVertexes[nRes + 1].sx, &transCoords[i + 1].x);
        Copy128Bits(&tlVertexes[nRes + 2].sx, &transCoords[i + 2].x);
        float fpPart = pKinds[0] * 0.25f;
        tlVertexes[nRes].tu = fpPart;
        tlVertexes[nRes + 1].tu = fpPart + 1.0f / 4;
        tlVertexes[nRes + 2].tu = fpPart + 0.5f / 4;
        tlVertexes[nRes].tv = 0.75f;
        tlVertexes[nRes + 1].tv = 0.75f;
        tlVertexes[nRes + 2].tv = 1.f;
        tlVertexes[nRes].dcSpecular = 0xFF000000;
        tlVertexes[nRes + 1].dcSpecular = 0xFF000000;
        tlVertexes[nRes + 2].dcSpecular = 0xFF000000;
        tlVertexes[nRes].dcColor = 0xFFFFFFFF;
        tlVertexes[nRes + 1].dcColor = 0xFFFFFFFF;
        tlVertexes[nRes + 2].dcColor = 0xFFFFFFFF;
        nRes += 3;
    }
    tlVertexes.SetSize(nRes); // ARRAY BOUNDS CHECK
    if (nRes == 0)
        return TRUE;

    pTarget->SetProjectionMatrixOne();

    return pTarget->RenderTriangleSet(reinterpret_cast<STLVertex1 *>(tlVertexes.GetData()),
                                      tlVertexes.GetSize(), tFaces.GetData(), nRes);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWeather::DrawSnow(CRenderer *pTarget)
{
    int x, y, i, nRes, ix, iy;
    CPlot3D ptSnowWind = ptWind;
    ptSnowWind.z -= FP_SNOW_DROP_SPEED;
    float T = pWorld->GetPreciseTime();
    CDXGeometryProcessor *pGPU = pTarget->GetGPU();

    CPlot3D ptCameraPos = pWorld->GetCamera()->GetCP();
    float fpShiftAmount = (ptCameraPos.z - FP_DROP_SNOW_MIDDLE_Z) / ptSnowWind.z;
    int nCameraX = Float2Int((ptCameraPos.x - ptSnowWind.x * fpShiftAmount) * FP_DROP_DENSITY);
    int nCameraY = Float2Int((ptCameraPos.y - ptSnowWind.y * fpShiftAmount) * FP_DROP_DENSITY);
    int nClipFlag, nParticles;

    nCurrentVertice = 0;
    srcVertexes.SetSize(N_MAX_PARTICLES);
    static int nRandomTable[32] = { 5417,  2343,  34456, 12942, 8832,  12234, 45865, 8395,
                                    45321, 8847,  99009, 55284, 76543, 45396, 12193, 98120,
                                    6798,  79784, 11633, 5710,  54259, 3477,  78118, 1266,
                                    97821, 80949, 35786, 8780,  78475, 72235, 34213, 92826 };

    float fpMinH, fpMaxH;
    fpMinH = Max(0.0f, ptCameraPos.z - FP_LIMIT_DISTANCE);
    fpMaxH = ptCameraPos.z + FP_LIMIT_DISTANCE;
    nParticles = 0;
    for (iy = 0; iy < N_PARTICLES_PER_AXIS; iy++)
    {
        y = order[iy];
        for (ix = 0; ix < N_PARTICLES_PER_AXIS; ix++)
        {
            x = order[ix];
            float fpX, fpY, fpZ;
            int nX = nCameraX + x;
            int nY = nCameraY + y;
            int nStep;
            float fpTime = T +
                           ((nX * 35472 + nY * 17843 + nRandomTable[(nX * nX + nY) & 31]) & 0xffff);
            nStep = Float2Int(fpTime / FP_DROP_SNOW_CYCLE);
            if ((nStep & (N_WEATHER_POWER_GRADATIONS - 1)) >= nPowerCheck)
                continue;
            float fpTimeStep = (fpTime - nStep * FP_DROP_SNOW_CYCLE);
            fpZ = FP_DROP_SNOW_MIDDLE_Z + fpTimeStep * ptSnowWind.z;
            if (fpZ < fpMinH || fpZ > fpMaxH)
                continue;
            fpX = float(nX) * FP_DROP_DENSITY_1 + fpTimeStep * ptSnowWind.x;
            fpY = float(nY) * FP_DROP_DENSITY_1 + fpTimeStep * ptSnowWind.y;
            fpX += (((nStep * 197) & 255) - 128) / 128.0f * FP_DROP_DENSITY_1;
            fpY += (((nStep * 67) & 255) - 128) / 128.0f * FP_DROP_DENSITY_1;
            fpZ += (((nStep * 129) & 255) - 128) / 128.0f * FP_DROP_Z_DISP;
            fpX += rndX(fpZ) + rndX1(float(fmod(fpX + 1000, 50)));
            fpY += rndY(fpZ) + rndY1(float(fmod(fpY + 1000, 50)));
            fpZ += rndZ(fpZ);
            AddPoint(fpX, fpY, fpZ);
            kinds[nParticles] = nStep & 3;
            nParticles++;
        }
    }

    srcVertexes.SetSize((nCurrentVertice + 3) >> 2);
    transCoords.SetSize(srcVertexes.GetSize() * 4); // ARRAY BOUNDS CHECK
    clips.SetSize(srcVertexes.GetSize() * 4);
    nClipFlag = pGPU->GetGeometryProcessor()->TransformVertices(
        pGPU->GetMatrix(MAT_STACK_PROJSPACE), srcVertexes.GetData(), transCoords.GetData(),
        clips.GetData(), srcVertexes.GetSize());
    nRes = 0;
    tlVertexes.SetSize(nCurrentVertice * 3); // ARRAY BOUNDS CHECK
    BYTE *pKinds = kinds.GetData();
    for (i = 0; i < nCurrentVertice; i++, pKinds++)
    {
        if (clips[i])
            continue;
        Copy128Bits(&tlVertexes[nRes].sx, &transCoords[i].x);
        Copy128Bits(&tlVertexes[nRes + 1].sx, &transCoords[i].x);
        Copy128Bits(&tlVertexes[nRes + 2].sx, &transCoords[i].x);
        tlVertexes[nRes].sx -= FP_SNOWFLAKE_SIZE / 2;
        tlVertexes[nRes + 1].sx += FP_SNOWFLAKE_SIZE / 2;
        tlVertexes[nRes].sy -= FP_SNOWFLAKE_H * 0.3f;
        tlVertexes[nRes + 1].sy -= FP_SNOWFLAKE_H * 0.3f;
        tlVertexes[nRes + 2].sy += FP_SNOWFLAKE_H * 0.7f;
        float fpPart = pKinds[0] * 0.25f;
        tlVertexes[nRes].tu = fpPart;
        tlVertexes[nRes].tv = 0.5f;
        tlVertexes[nRes + 1].tu = fpPart + 1.0f / 4;
        tlVertexes[nRes + 1].tv = 0.5f;
        tlVertexes[nRes + 2].tu = fpPart + 0.5f / 4;
        tlVertexes[nRes + 2].tv = 0.74f;
        tlVertexes[nRes].dcSpecular = 0xFF000000;
        tlVertexes[nRes + 1].dcSpecular = 0xFF000000;
        tlVertexes[nRes + 2].dcSpecular = 0xFF000000;
        tlVertexes[nRes].dcColor = 0xFFFFFFFF;
        tlVertexes[nRes + 1].dcColor = 0xFFFFFFFF;
        tlVertexes[nRes + 2].dcColor = 0xFFFFFFFF;
        nRes += 3;
    }
    tlVertexes.SetSize(nRes); // ARRAY BOUNDS CHECK
    if (nRes == 0)
        return TRUE;

    pTarget->SetProjectionMatrixOne();

    return pTarget->RenderTriangleSet(reinterpret_cast<STLVertex1 *>(tlVertexes.GetData()),
                                      tlVertexes.GetSize(), tFaces.GetData(), nRes);
}

/////////////////////////////////////////////////////////////////////////////////////
BOOL CWeather::Draw(CRenderer *pTarget)
{
    ASSERT(pWorld);
    if (!pWorld)
        return FALSE;
    if (curWeather == EW_CLEAR)
        return TRUE;

    SetupPower((float)pWorld->GetSegmentTimeCounter());
    if (curWeather == EW_CLEAR)
        return TRUE;

    // Weather transitions and rain audio still advance on the host. c73 is
    // the sole rain/snow producer; a rejected renderer pass stays omitted.
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
