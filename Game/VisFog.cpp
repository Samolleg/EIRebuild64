#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "VisFog.h"
#include <algorithm>
#include "aiSensesConst.h"
#include "world.h"
#include "aiGraph.inl"
#include "randomGen.h"
#include "Renderer.h" 
#include "Particle.h"
using namespace std;

#define SWITCH_PHASES
const float FP_VISFOG_PARTICLE_SIZE = 0.4f;
/////////////////////////////////////////////////////////////////////////////////////
void CVisibilityFog::BuildBoundingVolume()
{
    bv.ptMin = CPlot3D(1e30f, 1e30f, 1e30f);
    bv.ptMax = CPlot3D(-1e30f, -1e30f, -1e30f);
    for (auto i = particles.begin(); i != particles.end(); ++i)
    {
        CPlot3D &point = i->second.pos;
        bv.ptMin.x = Min(bv.ptMin.x, point.x);
        bv.ptMin.y = Min(bv.ptMin.y, point.y);
        bv.ptMin.z = Min(bv.ptMin.z, point.z);
        bv.ptMax.x = Max(bv.ptMax.x, point.x);
        bv.ptMax.y = Max(bv.ptMax.y, point.y);
        bv.ptMax.z = Max(bv.ptMax.z, point.z);
    }
    bv.ptMax += CPlot3D(0.5f, 0.5f, 0.5f);
    bv.ptMin -= CPlot3D(0.5f, 0.5f, 0.5f);
    bv.ptCenter = (bv.ptMin + bv.ptMax) / 2;
    bv.ptMin -= bv.ptCenter;
    bv.ptMax -= bv.ptCenter;
    bv.fpRadius = fabs(bv.ptMax);
    bv.ptBone = CPlot3D(0, 0, 0);
    bv.dwFlags = BV_BOX | BV_SPHERE;
    bBVIsCorrect = true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CVisibilityFog::Clear()
{
    particles.clear();
    bBVIsCorrect = false;
}
/////////////////////////////////////////////////////////////////////////////////////
int CVisibilityFog::Segment()
{
    static vector<int> toDel;
    toDel.clear();
    int nCurSegment = GetWorldBase()->GetSegmentTimeCounter();
    for (auto i = particles.begin(); i != particles.end(); ++i)
    {
        if (i->second.nState)
            i->second.fpPower = 1 - (1 - i->second.fpPower) * 0.5f;
        else
            i->second.fpPower *= 0.5f;
        if (i->second.fpPower < 0.01f)
            toDel.push_back(i->first);
#ifdef SWITCH_PHASES
        i->second.nPhase = (i->second.nPhase + 1) % 15;
#endif
    }
    for_each(toDel.begin(), toDel.end(), FEraser(particles));
    return GetRefCount() > 1 || particles.size() > 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CVisibilityFog::AddNewVisibilityData(SVisibilityMask &mask)
{
    for (auto it = particles.begin(); it != particles.end(); ++it)
        it->second.nState = 0;
    for (int y = 0; y < mask.data.GetYSize(); y++)
    {
        for (int x = 0; x < mask.data.GetXSize(); x++)
        {
            if (mask.data[y][x])
            {
                int nHash = ((y + mask.nShiftY) << 16) + (x + mask.nShiftX);
                auto pPart = particles.find(nHash);
                if (pPart != particles.end())
                    pPart->second.nState = 1;
                else
                {
                    SFogParticle &part = particles[nHash];
                    part.nState = 1;
                    part.nX = x + mask.nShiftX;
                    part.nY = y + mask.nShiftY;
                    part.pos.x = AITile2Meter(part.nX + random.GetFloat(-0.25f, 0.25f));
                    part.pos.y = AITile2Meter(part.nY + random.GetFloat(-0.25f, 0.25f));
                    part.pos.z = GetWorldBase()->GetBridgeSurfaceZValue(part.pos.x, part.pos.y) +
                                 0.5f;
                    part.fpPower = 0;
#ifdef SWITCH_PHASES
                    part.nPhase = random.Get(0, 14);
#else
                    part.nPhase = 15;
#endif
                    bBVIsCorrect = false;
                }
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// координаты текстуры для партиклов
static SParticleUV uv16[16];
/////////////////////////////////////////////////////////////////////////////////////
BOOL CVisibilityFog::Draw(CRenderer *pTarget)
{
    if (particles.size() == 0)
        return TRUE;
    if (particles.size() > 12000)

        if (!bBVIsCorrect)
            BuildBoundingVolume();

    int nClipFlag = pTarget->CheckVisibility(bv);
    if (nClipFlag == TRV_INVISIBLE)
        return TRUE;

    // Define temporary arrays
    static CParticleToDrawArray resParts;
    static CXYZWArray inCoords;

    resParts.SetSize(4096);
    inCoords.SetSize(4096);
    resParts.SetSize(particles.size());
    inCoords.SetSize(particles.size());

    // Fill inCoords and set colors and texture coordinates
    SXYZW *pXYZW = inCoords.GetData();
    SParticleToDraw *pParticle = resParts.GetData();
    int nSafeCounter = particles.size();
    // Sequently fill and render arrays
    for (auto i = particles.begin(); i != particles.end(); ++i, pXYZW++, pParticle++)
    {
        if (--nSafeCounter < 0)
        {
            ASSERT(0);
            break;
        }
        pXYZW[0] = i->second.pos;
        pXYZW->w = FP_VISFOG_PARTICLE_SIZE;
        DWORD dwPower = Float2Int(0xFF * i->second.fpPower);
        pParticle->dwColor = RGBA_MAKE(0xFF, 0xFF, 0xFF, dwPower);
        pParticle->pUV = uv16 + i->second.nPhase;
    }
    return pTarget->DrawParticles(false, inCoords, resParts);
}
/////////////////////////////////////////////////////////////////////////////////////
// CVisFogManager Manager
/////////////////////////////////////////////////////////////////////////////////////
LPGAMEBASE CVisFogManager::CreateInstant(ETypeIDs tidTypeID)
{
    CVisibilityFog *pFog = dbgnew CVisibilityFog;
    if (pFog == NULL)
        return NULL;
    // pLightning->SetTypeID( tidTypeID );

    return pFog;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CVisFogManager::Draw(CRenderer *pTarget)
{
    if (fogs.IsEmpty())
        return TRUE;
    if (!pTarget->BindTexture(0, pTexture))
        return FALSE;
    for (POSITION pos = fogs.GetHeadPosition(); pos;)
    {
        CVisibilityFog *pFog = fogs.GetNext(pos);
        pFog->Draw(pTarget);
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
int CVisFogManager::Segment()
{
    for (POSITION pos = fogs.GetHeadPosition(); pos;)
    {
        POSITION delPos = pos;
        CVisibilityFog *pFog = fogs.GetNext(pos);
        if (!pFog->Segment())
        {
            fogs.RemoveAt(delPos);
            pFog->Release();
        }
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CVisFogManager::Destroy()
{
    pTexture.Clear();
    for (POSITION pos = fogs.GetHeadPosition(); pos;)
    {
        CVisibilityFog *pFog = fogs.GetNext(pos);
        pFog->Release();
    }
    fogs.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
CVisibilityFog *CVisFogManager::CreateFog()
{
    CVisibilityFog *pRes = (CVisibilityFog *)CreateData(TID_EMPTY);
    pRes->AddRef();
    fogs.AddTail(pRes);
    return pRes;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CVisFogManager::Create()
{
    pTexture.SetDesc(TTID_SPELL_MAIN, "VisionFog"); // PoisonCloud" );
    for (int i = 0; i < 16; i++)
    {
        uv16[i].fpU0 = (i & 3) * 0.25f;
        uv16[i].fpV1 = 1 - (1 + i / 4) * 0.25f;
        uv16[i].fpU1 = uv16[i].fpU0 + 0.25f;
        uv16[i].fpV0 = uv16[i].fpV1 + 0.25f;
    }
    return CBaseManager::Create();
}
/////////////////////////////////////////////////////////////////////////////////////
