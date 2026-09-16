#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "WorldClient.h"

#include "WorldInterface.h"
#include "ScenarioTracker.h"

#include "WinRegistry.h"
#include "IniFile.h"

#include "Object.h"
#include "Object3DClientSpecific.h"
#include "Figure.h"

#include "ParticleManager.h"
#include "MaterialManager.h"
#include "LightManager.h"
#include "FigureManager.h"
#include "ClientSpecificManager.h"
#include "SoundManager.h"
#include "TextManager.h"
#include "TextureManager.h"

#include "FlyingHitPoints.h"
#include "Sky.h"

#include "2DScreenWait.h"

#include "Terrain.h"

#include "Camera.h"

#include "PlayerClient.h"
#include "PlayerManager.h"
#include "ParticleManager.h"
#include "Effect.h"
#include "EffectManager.h"
#include "MapObjectManager.h"
#include "ServerConsts.h"
#include "SaveLoader.h"
#include "Lightning.h"
#include "spellManager.h"
#include "net.h"
#include "StuffMapObjects.h"
#include "StuffMOClientSpec.h"
#include "UnitClientSpecific.h"
#include "MapObjectClientSpecific.h"
#include "PolyLine.h"
#include "ServerConsts.h"
#include "party.h"
#include "VisFog.h"
#include "ItemManager.h"
#include "Item.h"
#include "MusicSystem.h"
#include "StringProcessing.h"
#include "IDebugInfoSystem.h"
#include "Renderer.h"
#include "ModShadowSchedule.h"
#include "dx11/RendererDX11.h"
#include "GeometryManager.h"

#include "I3DMain.h"
#include "Shadow.h"
#include "GeometryInterfaces.h"
#include "TerrainPrintsDatabase.h"
#include "Console.h"

#include "NetChat.h"
#include "ChatServer.h"
#include "GameMain.h"

#include <DirectXHelpers.h>
#include <SimpleMath.h>
#include <Effects.h>
#include "VertexTypes.h"
#include <PrimitiveBatch.h>
extern void LMPTimePassed();
/////////////////////////////////////////////////////////////////////////////////////
#include <functional>
#include <algorithm>
#include <list>
#include <vector>
#include <iterator>
using std::copy_if;
using std::find_if;
using std::for_each;
using std::list;
using std::sort;
/////////////////////////////////////////////////////////////////////////////////////
// половина стороны квадрата точки выхода из зоны
const int N_EXIT_PLACE_SIZE = 20;
const int N_EXIT_EDGE_SIZE = 10;
/////////////////////////////////////////////////////////////////////////////////////
CWorldClient theWorld;
DWORD DW_SEGMENT_TIME = DW_DEFAULT_SEGMENT_TIME;
const float CAMERA_STEP = 0.5f;
/////////////////////////////////////////////////////////////////////////////////////
const int NUM_START_SEGMENTS_DELAY = 10;
const int NUM_START_SEGMENTS_DELAY_MUSIC = 18 * 5;
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE>
class CReleasePointerFunctional
{
public:
    void operator()(TYPE *pObject)
    {
        if (pObject)
            pObject->Release();
    }
};
template <class TYPE>
class CReleasePositiveFunctional
{
public:
    bool operator()(TYPE *pObject)
    {
        if (pObject->GetID() > 0)
        {
            pObject->Release();
            return true;
        }
        return false;
    }
};
template <class TYPE>
class CSegmentFunctional
{
public:
    bool operator()(TYPE &object) { return object.Segment() == 0; }
};
/////////////////////////////////////////////////////////////////////////////////////
// CBridgeManager
/////////////////////////////////////////////////////////////////////////////////////
BOOL CBridgeManager::AddSurface(const CPlot3D &p1, const CPlot3D &p2, const CPlot3D &p3,
                                const CPlot3D &normal, SBridge **pRes)
{
    pRes[0] = NULL;
    if (normal.z < 0.8f)
        return FALSE;
    SBridge &res = bridges.GetAt(bridges.AddHead(SBridge()));
    pRes[0] = &res;
    CPlot3D ptDif1 = p2 - p1, ptDif2 = p3 - p1;
    res.fpLeng1 = fabs(ptDif1);
    res.fpLeng2 = fabs(ptDif2);
    res.ptNormal = normal;
    res.ptCorner = p1;
    res.vec1 = (p2 - p1) / res.fpLeng1;
    res.vec2 = (p3 - p1) / res.fpLeng2;
    return TRUE;
}

/////////////////////////////////////////////////////////////////////////////////////
void CBridgeManager::AddBridge(int nID, SBoundingVolume &bv, const CPlot3D &pos,
                               const CQuaternion &rot)
{
    SHMatrix matrix;
    int j;
    SBridge *pRes = NULL;
    // RemoveBridge( nID );
    CPlot3D ptFLD(bv.ptMax.x, bv.ptMin.y, bv.ptMin.z), ptFRD(bv.ptMax.x, bv.ptMax.y, bv.ptMin.z),
        ptBLD = bv.ptMin, ptBRD(bv.ptMin.x, bv.ptMax.y, bv.ptMin.z),
        ptFLU(bv.ptMax.x, bv.ptMin.y, bv.ptMax.z), ptFRU = bv.ptMax,
        ptBLU(bv.ptMin.x, bv.ptMin.y, bv.ptMax.z), ptBRU(bv.ptMin.x, bv.ptMax.y, bv.ptMax.z);
    CPlot3D normals[6];
    CPlot3D faces[8] = { ptFLD,
                         ptBLD, // 	 Center of the DOWN face
                         ptBRD, // BACK
                         ptBRU, // RIGHT
                         ptFRU, // UP
                         ptFLU, // FRONT
                         ptFLD, // LEFT
                         ptBLD };
    normals[0] = CPlot3D(0, 0, -1);
    normals[1] = CPlot3D(-1, 0, 0);
    normals[2] = CPlot3D(0, 1, 0);
    normals[3] = CPlot3D(0, 0, 1);
    normals[4] = CPlot3D(1, 0, 0);
    normals[5] = CPlot3D(0, -1, 0);

    rot.DecompEulerMatrix(matrix);
    for (j = 0; j < 8; j++)
    {
        faces[j] = faces[j] + bv.ptBone + bv.ptCenter;
        //		faces[j] = rot.RotateVector( faces[j] );
        faces[j] = matrix.RotateVector(faces[j]);
        faces[j] += pos;
    }
    for (j = 0; j < 6; j++)
        normals[j] = matrix.RotateVector(normals[j]);
    //		normals[j] = rot.RotateVector( normals[j] );

    for (j = 1; j < 7; j++)
    {
        SBridge *pRes;
        if (AddSurface(faces[j], faces[j - 1], faces[j + 1], normals[j - 1], &pRes))
        {
            ASSERT(pRes);
            pRes->nID = nID;
            pRes->ptCenter = matrix.RotateVector(bv.ptBone + bv.ptCenter) + pos;
            //			pRes->ptCenter = rot.RotateVector( bv.ptBone + bv.ptCenter ) + pos;
            pRes->fpRadius = bv.fpRadius;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////
void CBridgeManager::RemoveBridge(int nID)
{
    POSITION pos, posCheck;
    pos = bridges.GetHeadPosition();
    while (pos)
    {
        posCheck = pos;
        SBridge &b = bridges.GetNext(pos);
        if (b.nID == nID)
            bridges.RemoveAt(posCheck);
    }
}

/////////////////////////////////////////////////////////////////////////////////////
float CBridgeManager::CalcZ(const CPlot3D &p, float fpZTerrain) const
{
    POSITION pos = bridges.GetHeadPosition();
    while (pos)
    {
        auto &bridge = bridges.GetNext(pos);
        float fpDist = sqr(p.x - bridge.ptCenter.x) + sqr(p.y - bridge.ptCenter.y);
        if (fpDist > sqr(bridge.fpRadius))
            continue;
        CPlot3D ptSrc = p - bridge.ptCorner;
        float fpDz = (ptSrc * bridge.ptNormal) / bridge.ptNormal.z;
        ptSrc.z -= fpDz;
        float fp1, fp2;
        fp1 = ptSrc * bridge.vec1;
        fp2 = ptSrc * bridge.vec2;
        if (fp1 < -0.5f || fp2 < -0.5f || fp1 > bridge.fpLeng1 + 0.5f ||
            fp2 > bridge.fpLeng2 + 0.5f)
            continue;

        fpZTerrain = Max(fpZTerrain, bridge.ptCorner.z + fp1 * bridge.vec1.z + fp2 * bridge.vec2.z);
    }
    return fpZTerrain;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CBridgeManager::IsBridge(float x, float y)
{
    POSITION pos = bridges.GetHeadPosition();
    CPlot3D p(x, y, 0);
    while (pos)
    {
        SBridge &bridge = bridges.GetNext(pos);
        float fpDist = sqr(p.x - bridge.ptCenter.x) + sqr(p.y - bridge.ptCenter.y);
        if (fpDist > sqr(bridge.fpRadius))
            continue;
        CPlot3D ptSrc = p - bridge.ptCorner;
        float fpDz = (ptSrc * bridge.ptNormal) / bridge.ptNormal.z;
        ptSrc.z -= fpDz;
        float fp1, fp2;
        fp1 = ptSrc * bridge.vec1;
        fp2 = ptSrc * bridge.vec2;
        if (fp1 < -0.1f || fp2 < -0.1f || fp1 > bridge.fpLeng1 + 0.1f ||
            fp2 > bridge.fpLeng2 + 0.1f)
            continue;
        return true;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CBridgeManager::GetBridgeSurface(float x, float y, CPlot3D &ptNormal, float &fpH)
{
    POSITION pos = bridges.GetHeadPosition();
    CPlot3D p(x, y, 0);
    fpH = 0;
    ptNormal = CPlot3D(0, 0, 1);
    while (pos)
    {
        SBridge &bridge = bridges.GetNext(pos);
        float fpDist = sqr(p.x - bridge.ptCenter.x) + sqr(p.y - bridge.ptCenter.y);
        if (fpDist > sqr(bridge.fpRadius))
            continue;
        CPlot3D ptSrc = p - bridge.ptCorner;
        float fpDz = (ptSrc * bridge.ptNormal) / bridge.ptNormal.z;
        ptSrc.z -= fpDz;
        float fp1, fp2;
        fp1 = ptSrc * bridge.vec1;
        fp2 = ptSrc * bridge.vec2;
        if (fp1 < -0.5f || fp2 < -0.5f || fp1 > bridge.fpLeng1 + 0.5f ||
            fp2 > bridge.fpLeng2 + 0.5f)
            continue;

        float fpNH = bridge.ptCorner.z + fp1 * bridge.vec1.z + fp2 * bridge.vec2.z;
        if (fpNH > fpH)
        {
            fpH = fpNH;
            ptNormal = bridge.ptNormal;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// CWorldClient implementation
/////////////////////////////////////////////////////////////////////////////////////
float CWorldClient::GetFPSCoeff() const
{
    float fpNewFPSCoeff = 25;
    /*
            if ( CheckFlag(WC_MAKE_MOVIE) )
            {
                    if ( lpD3DScene )
                            fpNewFPSCoeff = 25.0f / lpD3DScene->GetFPSCounter();
                    else
                            fpNewFPSCoeff = 25;

                    if ( fpNewFPSCoeff > 25 )
                            fpNewFPSCoeff = 25;
            }
            else
            {
                    if ( lpD3DScene )
                            fpNewFPSCoeff = 15.0f / lpD3DScene->GetFPSCounter();
                    else
                            fpNewFPSCoeff = 15;

                    if ( fpNewFPSCoeff > 15 )
                            fpNewFPSCoeff = 15;
            }
    */
    return fpNewFPSCoeff;
}
float CWorldClient::GetTimeStep() const
{
    if (CheckFlag(WC_MAKE_MOVIE))
    {
        float fpNewFPSCoeff = GetFPSCoeff();
        return fpNewFPSCoeff / 25.0f * 1000.0f;
    }
    else
    {
        float fpNewFPSCoeff = GetFPSCoeff();
        return fpNewFPSCoeff / 15.0f * 1000.0f;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
CWorldClient::CWorldClient()
{
    dwShowStatisticsFlag = WCSS_SHOW_FPS | WCSS_SHOW_RENDER_INFO | WCSS_SHOW_WORLD_DATA;
    //
    nLastReceivedEvent = -1;
    //
    pCamera = 0;
    //
    SetFlag(WC_RENDER_OBJECTS | WC_RENDER_TERRAIN | WC_RENDER_INTERFACE | WC_RENDER_EFFECTS |
            WC_RENDER_UNITS | WC_RENDER_SKY | WC_RENDER_FLYINGHP);

    //	nCurrentSegmentCount = 0;
    //
    nMouseScrollSensitiveArea = 10;
    // data managers
    pMaterialManager = NULL;
    pParticleManager = NULL;
    pTextureManager = NULL;
    pLightManager = NULL;

    pWindowManager = NULL;
    pBitmapManager = NULL;
    pLightningManager = NULL;
    pVisFogManager = NULL;

    // non-managers
    pSoundManager = NULL;
    pTextManager = NULL;

    pTerrain = NULL;
    //
    pPlayer = NULL;
    //
    pSky = NULL;
    pMoshka = NULL;
    //
    hWnd = NULL;
    hInstance = NULL;
    hAccel = NULL;
    hCursor = NULL;
    // names
    szMainName = "EvilIslands";
    szMainClass = "EvilIslandsClass";
    szMainTitle = "EvilIslands";
    szPaused = "EvilIslands Paused";
    // sun direction
    nFrameCounter = 0;

#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CWorldClient::~CWorldClient()
{
    // destroy all manager's data
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
// create all in-game internal data structures
BOOL CWorldClient::Create()
{
    CWorldBase::Create();
    //
    szTexturesPath = GetWorkPath() + "textures\\";
    //
    LoadLights("LightsGipat.ini");
    //
    nShadowAlpha = 0;
    // create all before-game internal data structures
    // sound manager (not from CBaseManager)
    pSoundManager = dbgnew CSoundManager;
    pSoundManager->AddRef();
    // text manager
    pTextManager = dbgnew CTextManager;
    pTextManager->AddRef();
    // create main terrain object
    pTerrain = dbgnew CTerrain();
    pTerrain->AddRef();
    szCurrentAllodName = "gipath";
    // create data managers
    // specific manager
    pSpecificManager = dbgnew CClientSpecificManager();
    pSpecificManager->AddRef();
    pSpecificManager->Create(this);
    // texture manager
    pTextureManager = dbgnew CTextureManager;
    pTextureManager->AddRef();
    pTextureManager->CBaseManager::Create(this);
    // material manager
    pMaterialManager = dbgnew CMaterialManager;
    pMaterialManager->AddRef();
    pMaterialManager->Create(this);
    // light manager
    pLightManager = dbgnew CLightManager;
    pLightManager->AddRef();
    pLightManager->Create(this);
    // particle manager
    pParticleManager = dbgnew CParticleManager;
    pParticleManager->AddRef();
    pParticleManager->Create(this);

    // create player for connecting to server
    pPlayer = ((CPlayerManager *)pPlayerManager)->CreatePlayer();

    // non-managers
    pTerrain->Create();
    pSoundManager->Create();

    pTextManager->Create();
    pTextManager->SelectResource(GetResourcePath() + "texts.res");
#if !defined(__SINGLEONLY__)
    pTextManager->SelectResource(GetResourcePath() + "textsLmp.res");
#endif // !defined( __SINGLEONLY__ )
       //
    pLightningManager = dbgnew CLightningManager;
    pLightningManager->Create(this);
    //
    pVisFogManager = dbgnew CVisFogManager;
    pVisFogManager->Create(this);
    //
    pMusicSystem = dbgnew CMusicSystem;
    pMusicSystem->Create();

    weather.Create(this);
    //
    bridgesManager.Create();
    // setup some old variables from general theSettings
    if (theSettings.nShadowQuality > 0)
        SetFlag(WC_RENDER_SHADOWS);
    else
        ResetFlag(WC_RENDER_SHADOWS);
    // load windows cursors
    LoadCursors();
    // CRAP
    if (GetSpellManager())
    {
        {
            CManagerIterator *spellIter = dbgnew CManagerIterator;
            CGameBase *pThing = spellIter->GetFirst(GetSpellManager());
            while (pThing)
            {
                pThing->AddRef();
                pThing = spellIter->GetNext();
            }
            delete spellIter;
        }
    }
    // picked objects vector
    vcPicked.reserve(400); // this will be far enough for complex scene
    // temporary map objects IDs
    nLastTemporaryMapObjectID = -100000;
    //
    eZoneSky = GMS_NORMAL;
    dangerState = WDS_NORMAL;
    // initialize areas with small zone size
    areaLights.SetDimentions(64, 64);
    areaObjects.SetDimentions(64, 64);
    //
    nFirstSegmentsCounter = 0;
    nPrintsRelightCounter = 0;
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// create Sky object :)
void CWorldClient::CreateSky()
{
    if (pSky)
        return;
    pSky = dbgnew CSkyObject;
    CFigureManager *pFigMan = (CFigureManager *)GetFigureManager();
    CMaterial *pMaterial;
    CSkyFigure *pFig = (CSkyFigure *)pFigMan->CreateData(FTID_SKY, -666);
    if (pFig)
    {
        // set name and mask for figure
        pFig->SetName("nask0");
        pFig->SetMask("");
        // here we'll load figure's data
        LPBYTE lpData = pFigMan->GetFile("nask0sky.fig");
        if (lpData == NULL)
        {
            pFig->AddRef();
            pFig->Release();
            pFig = NULL;
            return;
        }
        //
        pFig->LoadD3DComplex(lpData, 0, 0, 0);
        pFig->SetFlag(FIG_LIGHTED); // we'll lit this figure with own procedure
        pSky->SetSkyFigure(pFig);

        pMaterial = ((CMaterialManager *)GetMaterialManager())->CreateMaterial(MTID_WOOD);
        ASSERT(pMaterial);
        pSky->SetMaterial(pMaterial);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::AddQuickReferences()
{
    // release every item currently in item manager
    if (GetItemManager())
    {
        {
            CItemIterator *itemIter = dbgnew CItemIterator;
            CItem *pItem = itemIter->GetFirst(GetItemManager());
            while (pItem)
            {
                pItem->AddRef();
                pItem = itemIter->GetNext();
            }
            delete itemIter;
        }
    }
    // release every spellobject currently in spell manager
    if (GetItemManager())
    {
        {
            CManagerIterator *spellIter = dbgnew CManagerIterator;
            CGameBase *pThing = spellIter->GetFirst(GetSpellManager());
            while (pThing)
            {
                pThing->AddRef();
                pThing = spellIter->GetNext();
            }
            delete spellIter;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::RemoveQuickItemsAndSpells()
{
    // release every item currently in item manager
    if (GetItemManager())
    {
        {
            CItemIterator *itemIter = dbgnew CItemIterator;
            CItem *pItem = itemIter->GetFirst(GetItemManager());
            while (pItem)
            {
                pItem->Release();
                pItem = itemIter->GetNext();
            }
            delete itemIter;
            GetItemManager()->RemoveAll();
        }
    }
    // release every spellobject currently in spell manager
    if (GetSpellManager())
    {
        {
            CManagerIterator *spellIter = dbgnew CManagerIterator;
            CGameBase *pThing = spellIter->GetFirst(GetSpellManager());
            while (pThing)
            {
                pThing->Release();
                pThing = spellIter->GetNext();
            }
            delete spellIter;
            GetSpellManager()->RemoveAll();
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// destroy all internal data structures
void CWorldClient::Destroy()
{
    theCamera.SetCarrier((CObject3D *)0);
    if (pFlyingHP)
    {
        delete pFlyingHP;
        pFlyingHP = 0;
    }
    //
    if (pSky)
    {
        delete pSky;
        pSky = NULL;
    }
    if (pMoshka)
    {
        delete pMoshka;
        pMoshka = NULL;
    }
    RemoveQuickItemsAndSpells();

    // destroy all internal data structures
    // delete all terrain prints
    exitParticles.RemoveAll();
    bloodprints.clear();
    footprints.clear();
    fireprints.clear();
    // delete weather manager
    weather.Destroy();
    // destroy bridges mechanism
    bridgesManager.Destroy();
    // remove information about bridges
    bridgesManager.RemoveAllBridges();
    // remove all objects
    areaObjects.RemoveAll();
    selectedObjects.clear();
    heroes.clear();
    std::for_each(objects.begin(), objects.end(), CReleasePointerFunctional<CMapObject>());
    objects.clear();
    // delete player
    if (pPlayer)
    {
        pPlayer->Release();
        pPlayer = NULL;
    }
    // call inherited
    CWorldBase::Destroy();
    // delete lightnings
    if (pLightningManager)
    {
        delete pLightningManager;
        pLightningManager = NULL;
    }
    if (pVisFogManager)
    {
        delete pVisFogManager;
        pVisFogManager = 0;
    }
    // delete particle manager
    if (pParticleManager)
    {
        pParticleManager->DecRefCount();
        delete pParticleManager;
        pParticleManager = NULL;
    }
    // delete terrain
    if (pTerrain)
    {
        pTerrain->DecRefCount();
        delete pTerrain;
        pTerrain = NULL;
    }
    // delete light manager
    if (pLightManager)
    {
        pLightManager->DecRefCount();
        delete pLightManager;
        pLightManager = NULL;
    }
    // delete material manager
    if (pMaterialManager)
    {
        pMaterialManager->DecRefCount();
        delete pMaterialManager;
        pMaterialManager = NULL;
    }
    // delete texture manager
    if (pTextureManager)
    {
        pTextureManager->DecRefCount();
        delete pTextureManager;
        pTextureManager = NULL;
    }
    // delete music system
    if (pMusicSystem)
    {
        delete pMusicSystem;
        pMusicSystem = NULL;
    }
    // delete sound manager
    if (pSoundManager)
    {
        pSoundManager->DecRefCount();
        delete pSoundManager;
        pSoundManager = NULL;
    }
    // delete text manager
    if (pTextManager)
    {
        pTextManager->DecRefCount();
        delete pTextManager;
        pTextManager = NULL;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::Init(CRenderer *pRend)
{
    GetTextureManager()->SetInterface(pRend);
    //
    CreateSky();
    // flying hit points init
    pFlyingHP = dbgnew CFlyingHitPoints;
    CTexture *pTex = GetTextureManager()->CreateTexture(TTID_SPELL_MAIN, "numbers");
    if (pTex)
    {
        pTex->AddRef();
        GetTextureManager()->LoadTexture(pTex);
        pFlyingHP->SetTexture(pTex);
        pTex->SetFlag(TEX_CANT_UNLOAD);
    }
    // geometry manager initialization
    pGeometryManager = dbgnew CGeometryManager(&theRenderer, LPCSTR(theSettings.szResourcePath));
    // init num sounds for races and prototypes in MapObjectManager
    theUnitDatabase.SetupWithSounds(pSoundManager);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::LoadCursors()
{
    // default
    hCursor = LoadCursorFromFile(GetWorkPath() + "wincurs\\Df0000.cur");
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CWorldClient::LoadLights()
// read lighting info (sunlight, sky, ambient) for day/night cycles
BOOL CWorldClient::LoadLights(LPCSTR pszLightsName)
{
    D3DCOLORVALUE dcvColor;
    CStringArray szEntries;
    int previndex, index;
    CString szString;
    CIntArray vals;
    CIniFile reg;
    int i, j;
    //
    if (!reg.Open(GetWorkPath() + "config\\" + pszLightsName))
        ReportWarning("Unable to open %s for reading", pszLightsName);

    // sunlight
    previndex = index = 0;
    szEntries.RemoveAll();
    reg.GetEntries("sunlight", szEntries);
    for (i = 0; i < szEntries.GetSize(); i++)
    {
        szString = szEntries[i].Mid(4, 2);
        index = atoi(szString);
        vals.RemoveAll();
        reg.GetIntArray("sunlight", szEntries[i], vals);
        dcvSunlights[index].r = float(vals[0]) / 255.0f;
        dcvSunlights[index].g = float(vals[1]) / 255.0f;
        dcvSunlights[index].b = float(vals[2]) / 255.0f;
        dcvSunlights[index].a = 1;

        dcvColor.r = dcvSunlights[previndex].r;
        dcvColor.g = dcvSunlights[previndex].g;
        dcvColor.b = dcvSunlights[previndex].b;
        for (j = previndex + 1; j < index; j++)
        {
            dcvColor.r += (dcvSunlights[index].r - dcvSunlights[previndex].r) /
                          float(index - previndex);
            dcvColor.g += (dcvSunlights[index].g - dcvSunlights[previndex].g) /
                          float(index - previndex);
            dcvColor.b += (dcvSunlights[index].b - dcvSunlights[previndex].b) /
                          float(index - previndex);
            dcvSunlights[j].r = dcvColor.r;
            dcvSunlights[j].g = dcvColor.g;
            dcvSunlights[j].b = dcvColor.b;
            dcvSunlights[j].a = 1;
        }
        previndex = index;
    }
    // ambient
    previndex = index = 0;
    szEntries.RemoveAll();
    reg.GetEntries("ambient", szEntries);
    for (i = 0; i < szEntries.GetSize(); i++)
    {
        szString = szEntries[i].Mid(4, 2);
        index = atoi(szString);
        vals.RemoveAll();
        reg.GetIntArray("ambient", szEntries[i], vals);
        dcvAmbients[index].r = float(vals[0]) / 255.0f;
        dcvAmbients[index].g = float(vals[1]) / 255.0f;
        dcvAmbients[index].b = float(vals[2]) / 255.0f;
        dcvAmbients[index].a = 1;

        dcvColor.r = dcvAmbients[previndex].r;
        dcvColor.g = dcvAmbients[previndex].g;
        dcvColor.b = dcvAmbients[previndex].b;
        for (j = previndex + 1; j < index; j++)
        {
            dcvColor.r += (dcvAmbients[index].r - dcvAmbients[previndex].r) /
                          float(index - previndex);
            dcvColor.g += (dcvAmbients[index].g - dcvAmbients[previndex].g) /
                          float(index - previndex);
            dcvColor.b += (dcvAmbients[index].b - dcvAmbients[previndex].b) /
                          float(index - previndex);
            dcvAmbients[j].r = dcvColor.r;
            dcvAmbients[j].g = dcvColor.g;
            dcvAmbients[j].b = dcvColor.b;
            dcvAmbients[j].a = 1;
        }
        previndex = index;
    }
    // sky
    previndex = index = 0;
    szEntries.RemoveAll();
    reg.GetEntries("sky", szEntries);
    for (i = 0; i < szEntries.GetSize(); i++)
    {
        szString = szEntries[i].Mid(4, 2);
        index = atoi(szString);
        vals.RemoveAll();
        reg.GetIntArray("sky", szEntries[i], vals);
        dcvSkys[index].r = float(vals[0]) / 255.0f;
        dcvSkys[index].g = float(vals[1]) / 255.0f;
        dcvSkys[index].b = float(vals[2]) / 255.0f;
        dcvSkys[index].a = 1;

        dcvColor.r = dcvSkys[previndex].r;
        dcvColor.g = dcvSkys[previndex].g;
        dcvColor.b = dcvSkys[previndex].b;
        for (j = previndex + 1; j < index; j++)
        {
            dcvColor.r += (dcvSkys[index].r - dcvSkys[previndex].r) / float(index - previndex);
            dcvColor.g += (dcvSkys[index].g - dcvSkys[previndex].g) / float(index - previndex);
            dcvColor.b += (dcvSkys[index].b - dcvSkys[previndex].b) / float(index - previndex);
            dcvSkys[j].r = dcvColor.r;
            dcvSkys[j].g = dcvColor.g;
            dcvSkys[j].b = dcvColor.b;
            dcvSkys[j].a = 1;
        }
        previndex = index;
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
float CWorldClient::GetZValue(float x, float y) const
{
    x = Min(Max(0.0f, x), pTerrain->GetMaxX() - 1);
    y = Min(Max(0.0f, y), pTerrain->GetMaxY() - 1);
    return pTerrain->GetZValue(x, y);
}
/////////////////////////////////////////////////////////////////////////////////////
float CWorldClient::GetSurfaceValue(float x, float y) const
{
    float fpTerra, fpDepth;
    pTerrain->GetZValue(x, y, &fpTerra, &fpDepth);
    return fpTerra + fpDepth;
}
/////////////////////////////////////////////////////////////////////////////////////
CUnit *CWorldClient::GetNearestEnemy(CUnit *pHero)
{
    CPlayerClient *pPC = (CPlayerClient *)pPlayer->GetPlayerSpecific();
    if (!pPC || pHero == 0)
        return 0;

    float fpBestDist = 1e38f, fpDist;
    CUnit *pRes = 0;
    for (int i = 0; i < pPC->visibleUnitIDs.GetSize(); i++)
    {
        CUnit *pT = GetUnit(pPC->visibleUnitIDs[i]);
        if (!pT || pT->WhoAmI() != HPC_UNIT)
            continue;
        if (!pT->IsDead() && pT->IsEnemy(pHero->GetPlayerNum()))
        {
            fpDist = distanceXY2(pT->GetCP(), pHero->GetCP());
            if (fpDist < fpBestDist)
            {
                fpBestDist = fpDist;
                pRes = pT;
            }
        }
    }
    return pRes;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::SetClientWind(const CPlot3D &newWindVector, float fpNewWindForce)
{
    CWorldBase::SetWind(newWindVector, fpNewWindForce);
    if (pTerrain)
    {
        CPlot3D terrWind;
        terrWind.x = newWindVector.x;
        terrWind.y = newWindVector.y;
        terrWind.z = newWindVector.z;
        pTerrain->SetWind(&terrWind, fpNewWindForce);
    }
    weather.SetWind(newWindVector, fpNewWindForce);
}
/////////////////////////////////////////////////////////////////////////////////////
int CWorldClient::Segment()
{
    CMapObjectIterator mapobjIter;
    CEffectIterator effIter;
    //
    nFirstSegmentsCounter++;
    if (nFirstSegmentsCounter == NUM_START_SEGMENTS_DELAY)
    {
        theInterface.StringEvent("autosave");
        pSoundManager->SetFXMasterVolume(theSettings.nAudioSFX);
        pSoundManager->SetSpeechMasterVolume(theSettings.nAudioSpeech);
        pSoundManager->SetMusicMasterVolume(theSettings.nAudioStreaming);
    }
    else if (((nFirstSegmentsCounter < NUM_START_SEGMENTS_DELAY_MUSIC) &&
              (theWorld.GetDangerState() == WDS_COMBAT)) ||
             (nFirstSegmentsCounter == NUM_START_SEGMENTS_DELAY_MUSIC))
        pMusicSystem->UnpauseZoneMode();
    //
    // Terrain segment
    if (pTerrain)
        pTerrain->Segment();
    // Sound manager segment
    if (pMusicSystem)
        pMusicSystem->Segment();
    if (pSoundManager)
        pSoundManager->SegmentAmbient();
    // call segment() for fogs
    if (!IsCombatPause())
    {
        if (pVisFogManager)
            pVisFogManager->Segment();
    }
    // call segment for network drivers
    NetSegment();
    // process data from server and sending new information to server about camera placement
    if (pPlayer)
    {
        CPlayerClient *pPC = (CPlayerClient *)pPlayer->GetPlayerSpecific();
        if (!IsCombatPause())
            pPlayer->PrepareNewSegment();
        if (pPC->ParseIncomingPackets() == FALSE)
        {
            pPlayer->Disconnect();
            ReportError(
                "CWorldClient::Segment().\nDisconnecting client due to packet parsing error.\n");
            return FALSE;
        }
        pPC->Segment(); // == FALSE && !pPlayer->GetConnection()->HasPacket() )
    }
    if (!IsCombatPause())
        SetSegmentTimeCounter(GetSegmentTimeCounter() + 1);

    if (!IsCombatPause())
    {
        // Call Segment() for all mapobjects
        POSITION objpos = objList.GetHeadPosition();
        while (objpos)
        {
            CMapObject *pMO = objList.GetNext(objpos);
            if (pMO->Segment() == 0)
                RemoveDynamicObject(pMO->GetID());
        }
        //		for ( auto pos =  objects.begin(); pos != objects.end(); ++pos )
        //			(*pos)->Segment();
        // Effects segment
        CEffect *pEff = effIter.GetFirst((CEffectManager *)GetEffectManager());
        while (pEff)
        {
            pEff->Segment();
            pEff = effIter.GetNext();
        }
        if (pMoshka)
            pMoshka->Segment();
    }
    if (IWorldConnector.IsLMP() && theInterface.GetScreensNum() > 1)
    {
        // Call SetExactPosition() for all mapobjects
        float fpTime = GetPreciseTime();
        POSITION objpos = objList.GetHeadPosition();
        while (objpos)
            objList.GetNext(objpos)->SetExactPosition(fpTime);
    }
    // Particles segment
    if (pParticleManager)
        ((CParticleManager *)pParticleManager)->Segment(IsCombatPause());
    // call segment for all terrain prints
    if (!IsCombatPause())
    {
        bloodprints.remove_if(CSegmentFunctional<CBloodPrints>());
        footprints.remove_if(CSegmentFunctional<CFootPrints>());
        fireprints.remove_if(CSegmentFunctional<CFirePrints>());
    }
    // relight bloodprints
    if (!IsCombatPause() && theSettings.nTerrainEnableBloodprintsRelight)
    {
        nPrintsRelightCounter -= DW_SEGMENT_TIME;
        if (nPrintsRelightCounter <= 0)
        {
            nPrintsRelightCounter = theSettings.nLightingFreq;
            CLight *pSunLight = theRenderer.GetSunlight();
            for (auto pos = bloodprints.begin(); pos != bloodprints.end(); ++pos)
            {
                float fLuminance = pSunLight->GetLuminanceInPoint(VNULL);
                // iterate through all light sources
                CManagerIterator mi;
                CLight *pLight = static_cast<CLight *>(mi.GetFirst(GetLightManager()));
                while (pLight)
                {
                    if ((pSunLight != pLight) && !pLight->CheckFlag(LIT_INACTIVE))
                    {
                        float fDist2 = fabs2(pLight->GetCPX() - pos->GetCenterX(),
                                             pLight->GetCPY() - pos->GetCenterY());
                        if (fDist2 <= pLight->GetRange2())
                        {
                            CPlot3D ptPos(pos->GetCenterX(), pos->GetCenterY(),
                                          pTerrain->GetZValue(pos->GetCenterX(),
                                                              pos->GetCenterY()));
                            fLuminance = Max(fLuminance, pLight->GetLuminanceInPoint(ptPos));
                        }
                    }
                    pLight = static_cast<CLight *>(mi.GetNext());
                }
                // set color from the luminance
                DWORD dwColor = MINT(Max(0.0f, Min(fLuminance, 1.0f)) * 255.0f);
                pos->SetColor(0xFF000000 | (dwColor << 16) | (dwColor << 8) | dwColor);
            }
        }
    }
    // remove some LRU textures
    if ((GetSegmentTimeCounter() % 32) == 0)
        GetTextureManager()->ShrinkLRU();

    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// add 3D object to the world.
// check for ID
BOOL CWorldClient::AddObject(CMapObject *lpObject)
{
    // iterate objects in order to find such object
    for (auto pos = objects.begin(); pos != objects.end(); ++pos)
    {
        CMapObject *pObject = *pos;
        // check for such object existance
        if (pObject->GetID() == lpObject->GetID())
        {
            ASSERT(0);
            //			RemoveObject( pObject->GetID() );
            lpObject->AddRef();
            lpObject->Release();
            return TRUE;
        }
    }
    //
    POSITION objpos = 0;
    if ((objMap.Lookup(lpObject->GetID(), objpos) == FALSE) && lpObject->IsDynamic())
    {
        objList.AddTail(lpObject);
        objMap[lpObject->GetID()] = objList.GetTailPosition();
    }
    // if object happens to be a bridge add information about it
    AddBridgeInformation(lpObject);
    // fade out units
    if ((lpObject->WhoAmI() == HPC_UNIT) && (pPlayer->curInterface == IFACE_MISSION) &&
        (((CUnit *)lpObject)->GetOwner() == 0) && (nFirstSegmentsCounter >= 1))
        static_cast<CUnit *>(lpObject)->GetUnitSpecific()->StartFadeOut();
    // add object
    lpObject->AddRef();
    objects.push_back(lpObject);
    // add object to the area map, but first remove any copy...
    areaObjects.Remove(lpObject);
    areaObjects.Add(lpObject);
    //
    if ((lpObject->GetTypeID() == MOTID_HUMAN) && IsIDofPersonage(lpObject->GetID()))
        heroes.push_back(lpObject);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::AddBridgeInformation(CMapObject *pObj)
{
    int i;
    if (pObj->WhoAmI() == HPC_BUILDING || pObj->WhoAmI() == HPC_LEVER)
    {
        for (i = 0; i < pObj->GetNumFigures(); i++)
        {
            if (!pObj->GetBoundingVolumeFromFigure(i)->IsBridge())
                continue;
            bridgesManager.AddBridge(pObj->GetID(), *pObj->GetBoundingVolumeFromFigure(i),
                                     pObj->GetCP(), pObj->GetOrientation());
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// remove 3D object with desired ID from the world
void CWorldClient::RemoveObject(int nID)
{
    bridgesManager.RemoveBridge(nID);
    //
    POSITION objpos = 0;
    if (objMap.Lookup(nID, objpos) != FALSE)
    {
        objList.RemoveAt(objpos);
        objMap.RemoveAt(nID);
    }
    //
    for (auto pos = objects.begin(); pos != objects.end(); ++pos)
    {
        CMapObject *pObject = *pos;
        if (pObject->GetID() == nID)
        {
            if ((pObject->GetTypeID() == MOTID_HUMAN) && IsIDofPersonage(nID))
            {
                heroes.remove(pObject);
                if (pPlayer->curInterface == IFACE_BASE)
                    static_cast<CUnitClientSpecific *>(
                        static_cast<CUnit *>(pObject)->GetUnitSpecific())
                        ->ReleaseMouthMimicTexture();
            }
            areaObjects.Remove(pObject);
            objects.erase(pos);
            pObject->Release();
            return;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldClient::HasObject(int nID)
{
    for (auto pos = objects.begin(); pos != objects.end(); ++pos)
    {
        CMapObject *pObject = *pos;
        if (pObject->GetID() == nID)
            return TRUE;
    }
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// удаляет все объекты с положительными идентификаторами
void CWorldClient::RemoveAllObjects()
{
    areaLights.RemoveAll();  // remove all lights from area map
    areaObjects.RemoveAll(); // remove all objects from area map
    bridgesManager.RemoveAllBridges();
    //
    objList.RemoveAll();
    objMap.RemoveAll();
    selectedObjects.clear();
    heroes.clear();
    //
    objects.remove_if(CReleasePositiveFunctional<CMapObject>());

    pPlayer->RemoveAllObjects();

    if (pGeometryManager)
        pGeometryManager->ReleaseAllGeometries();
    pFigureManager->ClearTrash();
    pMapObjectManager->ClearTrash();
    pSpecificManager->ClearTrash();
}
/////////////////////////////////////////////////////////////////////////////////////
// удаляет все эффекты с клиента
void CWorldClient::RemoveAllEffects()
{
    exitParticles.RemoveAll();
    areaLights.RemoveAll(); // remove all lights from area map
    //
    CEffectIterator effIter;
    CEffect *pEff = effIter.GetFirst((CEffectManager *)GetEffectManager());
    while (pEff)
    {
        pEff->Release();
        pEff = effIter.GetNext();
    }
    //
    if (pMoshka)
        pMoshka->Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
// map object nID was updated and it is likely to be bridge
void CWorldClient::UpdateBridgeInformation(CMapObject *lpObject)
{
    int nID = lpObject->GetID();
    if (HasObject(nID))
    {
        bridgesManager.RemoveBridge(nID);
        AddBridgeInformation(lpObject);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// work with quest states
void CWorldClient::RecheckAllQuestStates()
{
    for (auto pos = objects.begin(); pos != objects.end(); ++pos)
    {
        CMapObject *pMO = *pos;
        const char *pszQuest = pMO->GetAttachedQuest();
        if (pszQuest && pszQuest[0])
            pMO->SetQuestVarValue(pPlayer->party.vars.GetVar(GetFullQuestName(pszQuest)));
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// на вход принимается полное имя квестовой переменной (q.gz1g.xuz)
void CWorldClient::UpdateQuestState(const char *pszQuestName, float fpVal)
{
    bool bOn = fpVal == 1;
    CString szName(pszQuestName);
    szName = szName.Right(szName.GetLength() - szName.Find(".", szName.Find(".") + 1) - 1);
    for (auto pos = objects.begin(); pos != objects.end(); ++pos)
    {
        CMapObject *pMO = *pos;
        if (szName == pMO->GetAttachedQuest())
            pMO->SetQuestVarValue(fpVal);
    }
    if (fpVal == 2 && szName.Find(".") < 0)
        CreateMoshka();
}
/////////////////////////////////////////////////////////////////////////////////////
CString CWorldClient::GetFullQuestName(const char *pszQuestName)
{
    return CString("q.") + IWorldClient.GetCurrentZoneName() + "." + pszQuestName;
}
/////////////////////////////////////////////////////////////////////////////////////
// create texture via texture manager
LPTEXTURE CWorldClient::CreateTexture(int nTypeID)
{
    if (pTextureManager)
        return ((CTextureManager *)pTextureManager)->CreateTexture(ETypeIDs(nTypeID));
    else
        return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPMATERIAL CWorldClient::CreateMaterial( int nTypeID )
// create material via material manager
LPMATERIAL CWorldClient::CreateMaterial(int nTypeID)
{
    if (pMaterialManager)
        return ((CMaterialManager *)pMaterialManager)->CreateMaterial(ETypeIDs(nTypeID));
    else
        return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPTEXTURE CWorldClient::FindTexture( int nID )
// find texture by ID via texture manager
LPTEXTURE CWorldClient::FindTexture(int nID)
{
    if (pTextureManager)
        return (LPTEXTURE)((CTextureManager *)pTextureManager)->Find(nID);
    else
        return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPMATERIAL CWorldClient::FindMaterial( int nID )
// find material by ID via material manager
LPMATERIAL CWorldClient::FindMaterial(int nID)
{
    if (pMaterialManager)
        return (LPMATERIAL)((CMaterialManager *)pMaterialManager)->Find(nID);
    else
        return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// load textures via texture manager
void CWorldClient::LoadTextures()
{
    if (pTextureManager)
        ((CTextureManager *)pTextureManager)->LoadTextures();
}
/////////////////////////////////////////////////////////////////////////////////////
// void CWorldClient::ReleaseTextures()
// release all textures
void CWorldClient::ReleaseTextures()
{
    if (pTextureManager)
        ((CTextureManager *)pTextureManager)->ReleaseTextures();
}
/////////////////////////////////////////////////////////////////////////////////////
// void CWorldClient::SetCamera( CCamera* pNewCamera )
// set main camera
void CWorldClient::SetCamera(CCamera *pNewCamera)
{
    if (pCamera)
        pCamera->DecRefCount();

    pCamera = pNewCamera;

    if (pCamera)
        pCamera->AddRef();
}
/////////////////////////////////////////////////////////////////////////////////////
class CMapObjectSelectionMatchFunctional
{
private:
    CPoint point;
    CBaseManager *pManager;

public:
    explicit CMapObjectSelectionMatchFunctional(CBaseManager *pMgr, const CPoint &pnt)
        : point(pnt)
        , pManager(pMgr)
    {
    }
    BOOL operator()(const SObjectScreenRect &osr)
    {
        if (osr.rcRect.PtInRect(point))
        {
            CMapObject *pMO = static_cast<CMapObject *>(pManager->Find(osr.nID));
            if (pMO)
                return pMO->IsCanSelect(point);
            else
                return FALSE;
        }
        else
            return FALSE;
    }
};
class CMapObjectRoughlySelectionMatchFunctional
{
private:
    CPoint point;
    CBaseManager *pManager;

public:
    explicit CMapObjectRoughlySelectionMatchFunctional(CBaseManager *pMgr, const CPoint &pnt)
        : point(pnt)
        , pManager(pMgr)
    {
    }
    BOOL operator()(const SObjectScreenRect &osr)
    {
        if (osr.rcRect.PtInRect(point))
        {
            CMapObject *pMO = static_cast<CMapObject *>(pManager->Find(osr.nID));
            if (pMO)
                return pMO->CanSelectRoughly(point);
            else
                return FALSE;
        }
        else
            return FALSE;
    }
};
/////////////////////////////////////////////////////////////////////////////////////
// sort by 'z' value and by the 'dead' state
// 'dead' map objects will be at the end
class CMapObjectZCompareFunctional
{
private:
    const SHMatrix &matrix;
    CBaseManager *pManager;

public:
    explicit CMapObjectZCompareFunctional(const SHMatrix &m, CBaseManager *pMgr)
        : matrix(m)
        , pManager(pMgr)
    {
    }
    bool operator()(const SObjectScreenRect &osr1, const SObjectScreenRect &osr2)
    {
        CMapObject *pMO1 = static_cast<CMapObject *>(pManager->Find(osr1.nID));
        CMapObject *pMO2 = static_cast<CMapObject *>(pManager->Find(osr2.nID));
        if (pMO1 && pMO2)
        {
            float z1 = matrix._31 * pMO1->GetCPX() + matrix._32 * pMO1->GetCPY() +
                       matrix._33 * pMO1->GetCPZ() + matrix._34;
            float z2 = matrix._31 * pMO2->GetCPX() + matrix._32 * pMO2->GetCPY() +
                       matrix._33 * pMO2->GetCPZ() + matrix._34;
            if ((!pMO1->IsDead() && !pMO2->IsDead()) || (pMO1->IsDead() && pMO2->IsDead()))
                return (z1 < z2);
            else if (pMO1->IsDead())
                return false;
            else
                return true;
        }
        else
            return false;
    }
};
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::PickMapObjects(const CRect &rcRect, std::list<CMapObject *> &mapobjects)
{
    for (auto it = lstScreenObjects.begin(); it != lstScreenObjects.end(); ++it)
    {
        CMapObject *pMO = static_cast<CMapObject *>(pMapObjectManager->Find(it->nID));
        if (pMO &&
            static_cast<CObject3DClientSpecific *>(pMO->GetObjectSpecific())->IsCanSelect(rcRect))
            mapobjects.push_back(pMO);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// pick first found map object (NOT A NEAREST), which maps to this point on the screen
CMapObject *CWorldClient::PickMapObject(const CPoint &point)
{
    CMapObjectManager *pMOManager = static_cast<CMapObjectManager *>(GetMapObjectManager());
    auto li = find_if(lstScreenObjects.begin(), lstScreenObjects.end(),
                      CMapObjectSelectionMatchFunctional(pMOManager, point));
    if (li == lstScreenObjects.end())
        return 0;
    else
    {
        while (li != lstScreenObjects.end())
        {
            CMapObject *pMO = static_cast<CMapObject *>(pMOManager->Find(li->nID));
            if (pMO)
                return pMO;
            ++li;
        }

        return 0;
    }
}

/////////////////////////////////////////////////////////////////////////////////////
// pick all map objects, which maps to this screen point, sort it and select first by respectCP.z
CMapObject *CWorldClient::RayPickFirstMapObject(const CPoint &point)
{
    // first, remove all the old picked elements
    vcPicked.clear();
    auto cameraPos = theCamera.GetCP();
    SRayPicker picker(point, cameraPos);
    CMapObjectManager *pMOManager = static_cast<CMapObjectManager *>(GetMapObjectManager());
    for (auto const &it : lstScreenObjects)
    {
        CMapObject *pMO = static_cast<CMapObject *>(pMOManager->Find(it.nID));
        const CPlot3D pos = pMO->GetCP();
        const SBoundingVolume &bv = pMO->GetBoundingVolume();
        if (picker.CheckBV(pos, bv))
        {
            vcPicked.push_back(it);
        }
    }
    if (vcPicked.empty())
        return 0;
    // sort it
    sort(vcPicked.begin(), vcPicked.end(),
         CMapObjectZCompareFunctional(theRenderer.GetGPU()->GetMatrix(MAT_STACK_CAMERA),
                                      GetMapObjectManager()));
    // return first of them
    vciLastPicked = vcPicked.begin();
    while (vciLastPicked != vcPicked.end())
    {
        CMapObject *pMO = static_cast<CMapObject *>(pMOManager->Find(vciLastPicked->nID));
        if (pMO)
            return pMO;
        ++vciLastPicked;
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// pick all map objects, which maps to this screen point, sort it and select first by respectCP.z
CMapObject *CWorldClient::RoughlyPickFirstMapObject(const CPoint &point)
{
    // first, remove all the old picked elements
    vcPicked.clear();
    //
    CMapObjectManager *pMOManager = static_cast<CMapObjectManager *>(GetMapObjectManager());
    std::back_insert_iterator<CObjectScreenRectVector> result(vcPicked);
    // copy all elements, which can be picked
    copy_if(lstScreenObjects.begin(), lstScreenObjects.end(), result,
            CMapObjectRoughlySelectionMatchFunctional(pMOManager, point));
    if (vcPicked.empty())
        return 0;
    // sort it
    sort(vcPicked.begin(), vcPicked.end(),
         CMapObjectZCompareFunctional(theRenderer.GetGPU()->GetMatrix(MAT_STACK_CAMERA),
                                      GetMapObjectManager()));
    // return first of them
    vciLastPicked = vcPicked.begin();
    while (vciLastPicked != vcPicked.end())
    {
        CMapObject *pMO = static_cast<CMapObject *>(pMOManager->Find(vciLastPicked->nID));
        if (pMO)
            return pMO;
        ++vciLastPicked;
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// pick all map objects, which maps to this screen point, sort it and select first by respectCP.z
CMapObject *CWorldClient::PickFirstMapObject(const CPoint &point)
{
    return RayPickFirstMapObject(point);
    // first, remove all the old picked elements
    vcPicked.clear();
    //
    CMapObjectManager *pMOManager = static_cast<CMapObjectManager *>(GetMapObjectManager());
    std::back_insert_iterator<CObjectScreenRectVector> result(vcPicked);
    // copy all elements, which can be picked
    copy_if(lstScreenObjects.begin(), lstScreenObjects.end(), result,
            CMapObjectSelectionMatchFunctional(pMOManager, point));
    if (vcPicked.empty())
        return 0;
    // sort it
    sort(vcPicked.begin(), vcPicked.end(),
         CMapObjectZCompareFunctional(theRenderer.GetGPU()->GetMatrix(MAT_STACK_CAMERA),
                                      GetMapObjectManager()));
    // return first of them
    vciLastPicked = vcPicked.begin();
    while (vciLastPicked != vcPicked.end())
    {
        CMapObject *pMO = static_cast<CMapObject *>(pMOManager->Find(vciLastPicked->nID));
        if (pMO)
            return pMO;
        ++vciLastPicked;
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
CMapObject *CWorldClient::CombinedPickFirstMapObject(const CPoint &point)
{
    CMapObject *pMO = PickFirstMapObject(point);
    if (pMO == 0)
        pMO = RoughlyPickFirstMapObject(point);
    return pMO;
}
/////////////////////////////////////////////////////////////////////////////////////
// return next element from 'picked' array
CMapObject *CWorldClient::PickNextMapObject()
{
    ++vciLastPicked;
    while (vciLastPicked != vcPicked.end())
    {
        CMapObject *pMO = static_cast<CMapObject *>(pMapObjectManager->Find(vciLastPicked->nID));
        if (pMO)
            return pMO;
        ++vciLastPicked;
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::PickUnits(const CRect &rcRect, std::list<CUnit *> &units)
{
    for (auto it = lstScreenObjects.begin(); it != lstScreenObjects.end(); ++it)
    {
        CMapObject *pMO = static_cast<CMapObject *>(pMapObjectManager->Find(it->nID));
        if (pMO && (pMO->WhoAmI() == HPC_UNIT) &&
            static_cast<CObject3DClientSpecific *>(pMO->GetObjectSpecific())->IsCanSelect(rcRect))
            units.push_back(static_cast<CUnit *>(pMO));
    }
}
/////////////////////////////////////////////////////////////////////////////////////
CUnit *CWorldClient::PickFirstUnit(const CPoint &point)
{
    CMapObject *pMO = PickFirstMapObject(point);
    if (pMO)
    {
        if (pMO->WhoAmI() == HPC_UNIT)
            return static_cast<CUnit *>(pMO);

        while (pMO && pMO->WhoAmI() != HPC_UNIT)
            pMO = PickNextMapObject();

        return static_cast<CUnit *>(pMO);
    }
    else
        return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
CUnit *CWorldClient::RoughlyPickFirstUnit(const CPoint &point)
{
    CMapObject *pMO = RoughlyPickFirstMapObject(point);
    if (pMO)
    {
        if (pMO->WhoAmI() == HPC_UNIT)
            return static_cast<CUnit *>(pMO);

        while (pMO && pMO->WhoAmI() != HPC_UNIT)
            pMO = PickNextMapObject();

        return static_cast<CUnit *>(pMO);
    }
    else
        return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
CUnit *CWorldClient::CombinedPickFirstUnit(const CPoint &point)
{
    CUnit *pUnit = PickFirstUnit(point);
    if (pUnit == 0)
        pUnit = RoughlyPickFirstUnit(point);
    return pUnit;
}
/////////////////////////////////////////////////////////////////////////////////////
CUnit *CWorldClient::PickNextUnit()
{
    CMapObject *pMO = PickNextMapObject();
    if (pMO)
    {
        if (pMO->WhoAmI() == HPC_UNIT)
            return static_cast<CUnit *>(pMO);

        while (pMO && pMO->WhoAmI() != HPC_UNIT)
            pMO = PickNextMapObject();

        return static_cast<CUnit *>(pMO);
    }
    else
        return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// select object and place it in array of selected objects
void CWorldClient::SelectObject(LPGAMEBASE lpNewObject)
{
    if (!lpNewObject)
        return;

    CMapObject *lpObject = (CMapObject *)lpNewObject;
#ifdef __MAP_EDITOR__
    lpObject->RecalcBoundingVolumeFast();
#else
    lpObject->RecalcBoundingVolume();
#endif // __MAP_EDITOR__
    lpObject->Select(TRUE);

    if (std::find(selectedObjects.begin(), selectedObjects.end(), lpObject->GetID()) ==
        selectedObjects.end())
        selectedObjects.push_back(lpObject->GetID());

    return;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::SelectObject(int nID)
{
    SelectObject(GetMapObjectManager()->Find(nID));
}
/////////////////////////////////////////////////////////////////////////////////////
// reset selection for one object or (if NULL) for all
void CWorldClient::ResetSelection(LPGAMEBASE lpNewObject)
{
    if (!lpNewObject)
    {
        // reset all selections
        for (auto pos = selectedObjects.begin(); pos != selectedObjects.end(); ++pos)
        {
            CMapObject *pObject = (CMapObject *)pMapObjectManager->Find(*pos);
            if (pObject)
                pObject->Select(FALSE);
        }
        selectedObjects.clear();
    }
    else
    {
        // reset selection for specified object
        CMapObject *pObject = (CMapObject *)lpNewObject;
        auto pos = std::find(selectedObjects.begin(), selectedObjects.end(), pObject->GetID());
        if (pos != selectedObjects.end())
        {
            selectedObjects.erase(pos);
            pObject->Select(FALSE);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::ResetSelection(int nID)
{
    ResetSelection(GetMapObjectManager()->Find(nID));
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldClient::CenterOnMapObject(const LPBASE3D lpNewObject)
{
    if (lpNewObject)
        return CenterOnLocation(lpNewObject->GetCP());

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldClient::CenterOnLocation(const CPlot3D &plot)
{
    CPlot3D axis = pCamera->GetZAxis() * theSettings.fpCameraDefaultDistanceToCarrier;
    CPlot3D ptCenter = plot;

    ptCenter.z = pCamera->GetAnchorPoint().z;
    pCamera->SetAnchorPoint(ptCenter);
    pCamera->ReconstructCameraPlacement();

    pCamera->CorrectHeight();

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::BeginWaitCursor()
{
    //	AfxGetMainWnd()->BeginWaitCursor();
}
void CWorldClient::EndWaitCursor()
{
    //	AfxGetMainWnd()->EndWaitCursor();
}
/////////////////////////////////////////////////////////////////////////////////////
// set combat pause state
void CWorldClient::CombatPause(bool bPause)
{
    ((CPlayerClient *)pPlayer->GetPlayerSpecific())->CommandCombatPause(bPause);
}
/////////////////////////////////////////////////////////////////////////////////////
// set/reset combat pause
void CWorldClient::SetCombatPause(bool bPause)
{
    bCombatPause = bPause;
}
/////////////////////////////////////////////////////////////////////////////////////
static float fpOldAdjustTime = -1; // CRAP
BOOL CWorldClient::AdjustLightingWithTime()
{
    CRenderer *pRend = GetRenderer();
    // check for pause mode
    if (!pRend->IsActive())
        return TRUE; // can't change lighting in paused mode
    //
    ColorExitZoneEffects();
    // special processing for cave zones
    if (IsCaveZone())
    {
        float fpTime = GetWorldTime(), fpHourFraction = 0;
        D3DCOLORVALUE dcvColor;
        int nIndex = 0;
        // check for midnight time swap (from 23.99 to 0.00)
        if (fpOldAdjustTime > fpTime)
            fpOldAdjustTime = -1;
        //
        GetRenderer()->GetTransformPipeline()->SetFogRange(theSettings.fpFogDayStartDistance,
                                                           theSettings.fpFarClipDistance);

        dcvColor.r = dcvSunlights[nIndex].r +
                     fpHourFraction * (dcvSunlights[(nIndex + 1) % 24].r - dcvSunlights[nIndex].r);
        dcvColor.g = dcvSunlights[nIndex].g +
                     fpHourFraction * (dcvSunlights[(nIndex + 1) % 24].g - dcvSunlights[nIndex].g);
        dcvColor.b = dcvSunlights[nIndex].b +
                     fpHourFraction * (dcvSunlights[(nIndex + 1) % 24].b - dcvSunlights[nIndex].b);
        dcvSkySun = dcvColor;
        // set ambient as 0.8 from color
        if ((dcvAmbients[nIndex].r == 0) && (dcvAmbients[nIndex].g == 0) &&
            (dcvAmbients[nIndex].b == 0))
        {
            dcvAmbient.r = 0.8f * dcvColor.r;
            dcvAmbient.g = 0.8f * dcvColor.g;
            dcvAmbient.b = 0.8f * dcvColor.b;
        }
        else
        {
            dcvAmbient.r = dcvAmbients[nIndex].r +
                           fpHourFraction *
                               (dcvAmbients[(nIndex + 1) % 24].r - dcvAmbients[nIndex].r);
            dcvAmbient.g = dcvAmbients[nIndex].g +
                           fpHourFraction *
                               (dcvAmbients[(nIndex + 1) % 24].g - dcvAmbients[nIndex].g);
            dcvAmbient.b = dcvAmbients[nIndex].b +
                           fpHourFraction *
                               (dcvAmbients[(nIndex + 1) % 24].b - dcvAmbients[nIndex].b);
        }
        dcvSkyAmbient = dcvAmbient;
        // set directional light (moon/sun) as color exactly
        LPLIGHT lpSunlight = pRend->GetSunlight();
        lpSunlight->SetDiffuse(D3DVECTOR(dcvColor.r, dcvColor.g, dcvColor.b));
        // set direction of sunlight/moonlight
        CPlot3D ptVector(0.5f, 0.5f, -0.707106f);
        lpSunlight->SetDirection(D3DVECTOR(ptVector.x, ptVector.y, ptVector.z));
        // setup ambient and directional light
        pRend->SetLighting(lpSunlight, dcvAmbient.r, dcvAmbient.g, dcvAmbient.b);
        // set sky color for current time value
        dcvColor.r = dcvSkys[nIndex].r +
                     fpHourFraction * (dcvSkys[(nIndex + 1) % 24].r - dcvSkys[nIndex].r);
        dcvColor.g = dcvSkys[nIndex].g +
                     fpHourFraction * (dcvSkys[(nIndex + 1) % 24].g - dcvSkys[nIndex].g);
        dcvColor.b = dcvSkys[nIndex].b +
                     fpHourFraction * (dcvSkys[(nIndex + 1) % 24].b - dcvSkys[nIndex].b);
        GetRenderer()->SetFogBGColor(dcvColor.r, dcvColor.g, dcvColor.b);

        if (pSky)
        {
            D3DCOLORVALUE black;
            black.r = 0;
            black.g = 0;
            black.b = 0;
            black.a = 1;
            pSky->RecalcVertexLighting(ptVector, black, black,
                                       black); // dcvSkyAmbient, dcvSkySun, dcvColor );
        }
        // check for minimal time quant to recalculate sun
        if (fpTime - fpOldAdjustTime < 1)
            return FALSE;
        fpOldAdjustTime = (float)floor(fpTime);
        //
        if (pTerrain)
            pTerrain->SetSun(lpSunlight, dcvAmbient);

        return TRUE;
    }
    //
    float fpTime = GetWorldTime();
    int nIndex = int(fpTime);
    float fpFogStartDistance;
    D3DCOLORVALUE dcvColor;
    float alpha, phi;
    CPlot3D ptVector;
    // set fog range for current time
    if ((fpTime >= TIME_DAY_START) && (fpTime <= TIME_DAY_END)) // check for daytime
        fpFogStartDistance = theSettings.fpFogDayStartDistance;
    else if ((fpTime >= TIME_NIGHT_START) || (fpTime <= TIME_NIGHT_END)) // check for nighttime
        fpFogStartDistance = theSettings.fpFogNightStartDistance;
    else if (fpTime < TIME_DAY_START) // morning
        fpFogStartDistance = theSettings.fpFogNightStartDistance +
                             ((fpTime - TIME_NIGHT_END) / TIME_MORNING_LENGTH) *
                                 (theSettings.fpFogDayStartDistance -
                                  theSettings.fpFogNightStartDistance);
    else // evening
        fpFogStartDistance = theSettings.fpFogDayStartDistance -
                             ((fpTime - TIME_DAY_END) / TIME_EVENING_LENGTH) *
                                 (theSettings.fpFogDayStartDistance -
                                  theSettings.fpFogNightStartDistance);
    GetRenderer()->GetTransformPipeline()->SetFogRange(fpFogStartDistance,
                                                       theSettings.fpFarClipDistance);
    // calculate shadow alpha
    CalculateShadowAlpha();
    // check for midnight time swap (from 23.99 to 0.00)
    if (fpOldAdjustTime > fpTime)
        fpOldAdjustTime = -1;
    if ((fpTime >= TIME_MORNING) && (fpTime <= TIME_EVENING)) // day sun track
        alpha = 30.0f + 30.0f * (1.0f - (fabs(fpTime - (TIME_EVENING + TIME_MORNING) * 0.5f) /
                                         (TIME_EVENING - TIME_MORNING) * 2.0f));
    else if (fpTime > TIME_EVENING) // night sun track
        alpha = 30.0f + 45.0f * ((fpTime - TIME_EVENING) / (24.0f - TIME_EVENING));
    else
        alpha = 30.0f + 45.0f * ((TIME_MORNING - fpTime) / TIME_MORNING);
    alpha = ToRadian(alpha);
    //
    phi = float((fpTime / 24.0f) * 2.0f * pi);
    // sun direction
    ptVector.x = float(cos(alpha) * cos(phi));
    ptVector.y = float(cos(alpha) * sin(phi));
    ptVector.z = float(-sin(alpha)); // sun shines DOWN to earth :)
    //
    double fpTimeInteger;
    float fpHourFraction = static_cast<float>(modf(fpTime, &fpTimeInteger));
    // set light color for current time value
    dcvColor.r = dcvSunlights[nIndex].r +
                 fpHourFraction * (dcvSunlights[(nIndex + 1) % 24].r - dcvSunlights[nIndex].r);
    dcvColor.g = dcvSunlights[nIndex].g +
                 fpHourFraction * (dcvSunlights[(nIndex + 1) % 24].g - dcvSunlights[nIndex].g);
    dcvColor.b = dcvSunlights[nIndex].b +
                 fpHourFraction * (dcvSunlights[(nIndex + 1) % 24].b - dcvSunlights[nIndex].b);
    dcvSkySun = dcvColor;
    // set ambient as 0.8 from color
    if ((dcvAmbients[nIndex].r == 0) && (dcvAmbients[nIndex].g == 0) &&
        (dcvAmbients[nIndex].b == 0))
    {
        dcvAmbient.r = 0.8f * dcvColor.r;
        dcvAmbient.g = 0.8f * dcvColor.g;
        dcvAmbient.b = 0.8f * dcvColor.b;
    }
    else
    {
        dcvAmbient.r = dcvAmbients[nIndex].r +
                       fpHourFraction * (dcvAmbients[(nIndex + 1) % 24].r - dcvAmbients[nIndex].r);
        dcvAmbient.g = dcvAmbients[nIndex].g +
                       fpHourFraction * (dcvAmbients[(nIndex + 1) % 24].g - dcvAmbients[nIndex].g);
        dcvAmbient.b = dcvAmbients[nIndex].b +
                       fpHourFraction * (dcvAmbients[(nIndex + 1) % 24].b - dcvAmbients[nIndex].b);
    }
    dcvSkyAmbient = dcvAmbient;
    // set directional light (moon/sun) as color exactly
    LPLIGHT lpSunlight = pRend->GetSunlight();
    lpSunlight->SetDiffuse(D3DVECTOR(dcvColor.r, dcvColor.g, dcvColor.b));
    // set direction of sunlight/moonlight
    lpSunlight->SetDirection(D3DVECTOR(ptVector.x, ptVector.y, ptVector.z));
    // setup ambient and directional light
    pRend->SetLighting(lpSunlight, dcvAmbient.r, dcvAmbient.g, dcvAmbient.b);
    // set sky color for current time value
    dcvColor.r = dcvSkys[nIndex].r +
                 fpHourFraction * (dcvSkys[(nIndex + 1) % 24].r - dcvSkys[nIndex].r);
    dcvColor.g = dcvSkys[nIndex].g +
                 fpHourFraction * (dcvSkys[(nIndex + 1) % 24].g - dcvSkys[nIndex].g);
    dcvColor.b = dcvSkys[nIndex].b +
                 fpHourFraction * (dcvSkys[(nIndex + 1) % 24].b - dcvSkys[nIndex].b);
    GetRenderer()->SetFogBGColor(dcvColor.r, dcvColor.g, dcvColor.b);

    if (pSky)
        pSky->RecalcVertexLighting(ptVector, dcvSkyAmbient, dcvSkySun, dcvColor);
    // check for minimal time quant to recalculate sun
    if (fpTime - fpOldAdjustTime < 1)
        return FALSE;
    fpOldAdjustTime = (float)floor(fpTime);
    //
    if (pTerrain)
        pTerrain->SetSun(lpSunlight, dcvAmbient);
    // reset flag 'FIG_LIGHTED' from all figures in scene to relight it
    /*
    POSITION objpos = objects.GetHeadPosition();
    while ( objpos )
    {
            LPGAMEBASE lpObject = objects.GetNext( objpos );
            ((CObject3D*)lpObject)->ResetFlagsFromAllFigures( FIG_LIGHTED );
            lpObject->ResetFlag( OBJ_LIGHTED );
    }
    */

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// void CWorldClient::CalculateShadowAlpha()
// calculate shadow alpha value, based on current world time
void CWorldClient::CalculateShadowAlpha()
{
    nShadowAlpha = 128;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::ReceiveEvent(int nEventID)
{
#ifdef __MAP_EDITOR__
    m_aReciveEvent.Add(nEventID);
#endif // __MAP_EDITOR__
    nLastReceivedEvent = nEventID;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::CreateMoshka()
{
    if (!pMoshka)
        pMoshka = dbgnew CEffectMoshka;
    pMoshka->Clear();
    pMoshka->Create(pPlayer->party.people[0].nID);
}
/////////////////////////////////////////////////////////////////////////////////////
#ifdef __MAP_EDITOR__
void CWorldClient::GetEventsAfter(CIntArray &aEvents, int nNumber)
{
    if (nNumber < m_aReciveEvent.GetSize())
        for (int i = nNumber; i < m_aReciveEvent.GetSize(); i++)
            aEvents.Add(m_aReciveEvent[i]);
}
#endif // __MAP_EDITOR__
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::SetNewConnection(CConnection *pConnection)
{
    RemoveAllObjects();
    RemoveAllEffects();
    pPlayer->SetConnection(0);
    pPlayer->SetConnection(pConnection);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::ConnectLocal(CLocalNetDriver *pServerDriver)
{
    // CRAP
    StopNetDrivers();
    ASSERT(pNetDriver.IsValid());
    ASSERT(pPlayer);
    SetNewConnection(pNetDriver->Connect(pServerDriver));
}
/////////////////////////////////////////////////////////////////////////////////////
bool CWorldClient::ConnectTcp(const char *pszAddress)
{
    ASSERT(pChatClient->IsValid());
    if (!pChatClient->IsValid())
        return false;
    const CCSBase::CClientList &clients = pChatClient->GetClients();
    if (clients.empty())
        return false;
    StartNetDrivers();
    ASSERT(pChatDriver.IsValid());
    SetNewConnection(pChatDriver->Connect(clients.front()));
    return true;
    /*	CTcpConnection *pCon;
            StartNetDrivers();
            ASSERT( pTcpDriver.IsValid() );
            ASSERT( pPlayer );
            pPlayer->SetConnection( 0 );
            if ( !pTcpDriver->StartConnect( pszAddress ) )
                    return false;
            for(;;)
            {
                    bool bInProgress;
                    pCon = pTcpDriver->GetConnection( bInProgress );
                    if ( pCon )
                            break;
                    if ( !bInProgress )
                            return false;
                    Sleep( 50 );
            }
            SetNewConnection( pCon );*/
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::Disconnect()
{
    GetPlayer()->GetPlayer()->Disconnect();
}
/////////////////////////////////////////////////////////////////////////////////////
bool CWorldClient::HasPacket()
{
    NetSegment();
    CConnection *pCon = pPlayer->GetConnection();
    if (!pCon || !pCon->IsValid())
        return false;
    return pCon->HasPacket();
}
/////////////////////////////////////////////////////////////////////////////////////
int CWorldClient::GetInputPacketsNum()
{
    NetSegment();
    CConnection *pCon = pPlayer->GetConnection();
    if (!pCon || !pCon->IsValid())
        return 0;
    return pCon->GetInputPacketsNum();
}
/////////////////////////////////////////////////////////////////////////////////////
bool CWorldClient::IsConnected()
{
    CPlayerClient *pC = GetPlayer();
    if (pC)
        return pC->GetStatus() == CPlayerClient::CPS_ACTIVE;
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CWorldClient::IsFailedConnection()
{
    CPlayerClient *pC = GetPlayer();
    if (pC)
    {
        CConnection *pConnection = pC->GetPlayer()->GetConnection();
        if (!pConnection || !pConnection->IsValid() || !pConnection->CanRecv())
            return true;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::NetSegment()
{
    if (pNetDriver)
        pNetDriver->Segment();
    if (pChatDriver)
        pChatDriver->Segment(pChatClient);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::DeployClientPersonages(CPersonages &people, bool bShowHelm)
{
    CString szTemp;
    int i, k;
    const char *pszBPs[] = { "hd",  "bd",  "hp",  "hr",  "lh1", "lh2", "lh3", "rh1", "rh2",
                             "rh3", "ll1", "ll2", "ll3", "rl1", "rl2", "rl3", NULL };
    const char **ppBPs;
    ppBPs = pszBPs;

    for (i = 0; i < people.GetSize(); i++)
    {
        CPersonage &pers = *people[i];

        CStringArray szNames;
        ppBPs = pszBPs;
        while (ppBPs[0])
        {
            if (strcmp(*ppBPs, "hr") == 0)
            {
                DWORD dwHair = theUnitDatabase.prototypes[pers.szPrototype]->dwHair;
                if (int(dwHair) != -1)
                {
                    szTemp.Format("hr.%.2d", dwHair);
                    szNames.Add(szTemp);
                }
                ppBPs++;
            }
            else
                szNames.Add(*ppBPs++);
        }

        // Create new map object
        CUnit *pUnit = (CUnit *)((CMapObjectManager *)GetMapObjectManager())
                           ->CreateData(MOTID_HUMAN, pers.nID);
        ASSERT(pUnit);
        ASSERT(pUnit->WhoAmI() == HPC_UNIT);
        if (!pUnit)
            return;
        // set subtype ID
        pUnit->SetSubtypeID(theUnitDatabase.prototypes.GetEntryIndex(pers.szPrototype));
        pUnit->SetVoicePrototype(pers.szVoicePrototype);

        // forbid unit to move by route
        pUnit->SetFlag(UNIT_DONTMOVE);
        // get mask name and load figures
        if (!pUnit->LoadByTemplate(pers.szTemplate, szNames, TRUE))
            return;
        CObject3DClientSpecific *lpObject3DSpecific = ((CObject3DClientSpecific *)
                                                           pUnit->GetObjectSpecific());
        lpObject3DSpecific->SetMaterials();
        // get start animation name and load it
        pUnit->LoadAnimation(pers.szTemplate, NULL);
        pUnit->InitAnimation();
        //
        if (strcmp(pers.szSecondTexture, "none"))
            lpObject3DSpecific->LoadTextures("none", pers.szSecondTexture);
        else
            lpObject3DSpecific->LoadTextures("none", NULL);
        pUnit->UpdateInit();

        pUnit->SetRPGStats(&pers.stats);
        pUnit->SetComplection(pers.complection);
        pUnit->SetBaseComplection(pers.complection);
        pUnit->RestoreComplection();
        // give unit some items etc
        pUnit->RemoveAllItems();
        for (k = 0; k < pers.weapons.GetSize(); k++)
            pUnit->AddWeapon(pers.weapons[k]);
        if (pers.weapons.GetSize())
            pUnit->ChangeWeapon(0);
        for (k = 0; k < pers.armors.GetSize(); k++)
        {
            if (bShowHelm || pers.armors[k]->GetArmorType() != RPGAST_HELM)
                pUnit->Wear(pers.armors[k]);
        }
        for (k = 0; k < pers.items.GetSize(); k++)
            pUnit->AddQItem(pers.items[k]);
        pUnit->GetUnitSpecific()->NotifyItemsUpdated();
        pUnit->GetUnitSpecific()->NotifyWeaponUpdated();

        pUnit->SetName(pers.szName);

        AddObject(pUnit);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::RemoveClientPersonages(CPersonages &people)
{
    for (int i = 0; i < people.GetSize(); i++)
    {
        CPersonage &pers = *people[i];
        RemoveObject(pers.nID);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldClient::IsExitPoint(const CPlot3D &ptCheck, int *nExitPoint)
{
    for (int i = 0; i < exits.GetSize(); i++)
    {
        if (exits[i].IsIn(ptCheck.x, ptCheck.y) && exits[i].szZone != "none")
        {
            if (nExitPoint)
                nExitPoint[0] = i;
            return TRUE;
        }
    }
    return FALSE; // UNREACHABLE
    if (nExitPoint)
        nExitPoint[0] = -1;
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
CPlot2D CWorldClient::GetExitCenter(int nExit)
{
    return CPlot2D((exits[nExit].ex1 + exits[nExit].ex2) * 0.5f,
                   (exits[nExit].ey1 + exits[nExit].ey2) * 0.5f);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::SpawnExitZoneEffects()
{
    exitParticles.SetSize(0);
    exitParticles.SetSize(exits.GetSize());
    for (int i = 0; i < exits.GetSize(); i++)
    {
        if (exits[i].szZone.CompareNoCase("none") == 0)
            continue;
        CParticle *pParticle = ((CParticleManager *)theWorld.GetParticleManager())
                                   ->CreateParticleSystem(PTID_ZONEEXIT);
        exitParticles[i] = pParticle;
        pParticle->SetCP(
            CPlot3D((exits[i].ex1 + exits[i].ex2) * 0.5f, (exits[i].ey1 + exits[i].ey2) * 0.5f, 0));
        pParticle->SetPower((exits[i].ex2 - exits[i].ex1) * 0.5f);
        pParticle->SetPower2((exits[i].ey2 - exits[i].ey1) * 0.5f);
        pParticle->SetParam1(cos(exits[i].eangle));
        pParticle->SetParam2(sin(exits[i].eangle));
        for (int k = 0; k < 20; k++)
            pParticle->Segment();
    }
    ColorExitZoneEffects();
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::ColorExitZoneEffects()
{
    if (exits.GetSize() != exitParticles.GetSize())
        return;
    for (int i = 0; i < exits.GetSize(); i++)
    {
        if (exitParticles[i] == 0)
            continue;
        if (pPlayer->party.vars.GetVar(CString("z.") + exits[i].szZone) == 1)
            exitParticles[i]->SetWallVector(CPlot3D(1, 0, 0));
        else
            exitParticles[i]->SetWallVector(CPlot3D(0, 0, 0));
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldClient::ArePersonagesAtExit(CPersonages &people, int nExitPoint,
                                       CPersonages &exitingPeople)
{
    BOOL bResult = TRUE;
    int nUnitExitPoint;
    int nUnitCount = 0;
    exitingPeople.SetSize(0);
    for (int i = 0; i < people.GetSize(); i++)
    {
        CPersonage &pers = *people[i];
        CUnit *pUnit = GetUnit(pers.nID);
        if (!pUnit || pUnit->IsDead())
            continue;
        nUnitCount++;
        if (!IsExitPoint(pUnit->GetCP(), &nUnitExitPoint))
        {
            bResult = FALSE;
            continue;
        }
        if (nUnitExitPoint != nExitPoint)
        {
            bResult = FALSE;
            continue;
        }
        exitingPeople.Add(&pers);
    }
    if (nUnitCount == 0)
        return FALSE;
    return bResult;
}
/////////////////////////////////////////////////////////////////////////////////////
static BOOL LoadList(CBaseManager *pMan, CMemorySave &packet)
{
    char cCont;
    CGameBase *pObj;
    int nID, nTypeID, nSubTypeID;
    for (;;)
    {
        packet.Read(cCont);
        if (!cCont)
            break;
        if (!packet.IsOk())
            return FALSE;
        packet.Read(nID);
        packet.Read(nTypeID);
        packet.Read(nSubTypeID);
        // remove old item with this ID
        pObj = pMan->Find(nID);
        if (pObj)
        {
            ASSERT(pObj->GetRefCount() == 1);
            if (pObj->GetRefCount() == 0)
                pObj->AddRef();
            pObj->Release();
        }
        // create new one
        pObj = pMan->CreateData(ETypeIDs(nTypeID), nID);
        pObj->SetSubtypeID((DWORD)nSubTypeID);
        pObj->Load(packet);
        pObj->AddRef();
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldClient::AddItems(CMemorySave &packet)
{
    packet.Seek(0);
    packet.SetOk();
    // load
    if (!LoadList(GetSpellManager(), packet))
        return FALSE;
    if (!LoadList(GetItemManager(), packet))
        return FALSE;
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldClient::NewItems(CMemorySave &packet)
{
#ifdef _DEBUG
    // make sure no objects exist on server at this moment
    for (auto pos = objects.begin(); pos != objects.end(); ++pos)
    {
        CMapObject *pObject = *pos;
        ASSERT((pObject->GetID() <= 0));
    }
#endif
    // erasing all objects
    RemoveQuickItemsAndSpells();
    // transmitting all items and spells from server to client, make it happen dude :)
    if (packet.GetSize())
    {
        pSpellManager->Load(packet);
        pItemManager->Load(packet);
        pSpellManager->LoadData(packet);
        pItemManager->LoadData(packet);
        AddQuickReferences();
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldClient::NewZone(CMemorySave &packet)
{
    IWorldConnector.LMPClientRecvZone();
    CString szZone, szMob;
    EGMZoneSky sky;

    // turn all sound volumes off
    pSoundManager->SetFXMasterVolume(0);
    //	pSoundManager->SetSpeechMasterVolume( 0 );
    //	pSoundManager->SetMusicMasterVolume( 0 );
    pSoundManager->StopAllSounds(true);
    //
    dangerState = WDS_NORMAL;
    packet.Read(nSegmentTimeCounter);
    szZone = packet.ReadString();
    szMob = packet.ReadString();
    LoadCArray(exits, packet);
    packet.Read(&sky, sizeof(sky));
    // loading statistics
    std::vector<SUnitStatistics> statistics;
    std::vector<SUnitWearsStatistics> wears;
    CUnitStatisticsDriver::Load(packet, statistics, wears);
    //
    eZoneSky = sky;
    // load on server appropriate mob if its not loaded yet
    IWorldServer.CheckZone(szZone, szMob);
    //
    C2DScreenWait::Step(8);

    // erasing all objects
    bloodprints.clear();
    footprints.clear();
    fireprints.clear();
    RemoveAllEffects();
    RemoveAllObjects();
    RemoveQuickItemsAndSpells();
    // erasing all particle effects
    ((CParticleManager *)GetParticleManager())->EraseNotReferencedParticleEffects();
    //
    pPlayer->curInterface = IFACE_NONE;
    //
    C2DScreenWait::Step(9);

    CTerrain *lpTerrain = GetTerrain();
    if (szZone.GetLength() > 1 && szZone[1] != ':')
        szZone = GetMobPath() + szZone;
    // free old terrain data
    lpTerrain->Free();
    //	remove all the old textures from LRU (and from video memory)
    ((CTextureManager *)GetTextureManager())->DeleteUnitLayerTextures();
    ((CTextureManager *)GetTextureManager())->RemoveAllLRUTextures();

    C2DScreenWait::Step(10);
    // get allod name. possible variants:
    // "gipat"
    // "ingos"
    // "suslanger"
    SScenarioZone *pZone = theMap.GetZoneByName(IWorldClient.GetCurrentZoneName());
    szCurrentAllodName = "gipat";
    if (pZone)
        szCurrentAllodName = pZone->szAllodName;
    szCurrentAllodName.MakeLower();

    // load new terrain
    if (lpTerrain->Load(szZone, szCurrentAllodName, sky == GMS_CAVE) == FALSE)
        return ReportError("Can't load terrain or map file \"%s\".\n", szZone);
    // setup area maps
    areaObjects.SetDimentions(MINT(lpTerrain->GetMaxX()), MINT(lpTerrain->GetMaxY()));
    areaLights.SetDimentions(MINT(lpTerrain->GetMaxX()), MINT(lpTerrain->GetMaxY()));
    // setup terrain flags
    if (!theSettings.nTerrainEnableWaterWaves)
        lpTerrain->RemoveRenderState(CTerrain::ETRS_WAVES);
    if (sky == GMS_CAVE) // we MUST have a water waves in the cave to have an animated lave
        lpTerrain->SetRenderState(CTerrain::ETRS_WAVES);
    if (!theSettings.nTerrainEnableSelfShadowing)
        lpTerrain->RemoveRenderState(CTerrain::ETRS_TRACE_SHADOWS);
    lpTerrain->SetAutoRelightDelay(theSettings.nTerrainLightingFreq / 66);
    //
    if (pSky)
    {
        if (sky == GMS_CAVE) // cave zone
        {
            pSky->SetTexture("Sky01");
            pSky->SetSkyRotate(FALSE);
        }
        else // normal zone
        {
            pSky->SetTexture("Sky00");
            pSky->SetSkyRotate(TRUE);
        }
    }
    // compose name of lights.ini from allod name
    CString szLightsName("Lights");
    // load lights for each zone
    if (IsCaveZone()) // cave zone
    {
        szLightsName += "Cave";
        if (theSettings.nAudio && theSettings.nAudioSFX)
            pSoundManager->SetReverb(0.3f, 1.0f, 3.0f);
    }
    else // normal zone
    {
        if (theSettings.nAudio && theSettings.nAudioSFX)
            pSoundManager->ClearReverb();
    }
    LoadLights(szLightsName + szCurrentAllodName + ".ini");
    //
    // spawn exit effects
    SpawnExitZoneEffects();
    //
    fpOldAdjustTime = -1;
    AdjustLightingWithTime();
    //
    if (theSettings.nAudio && theSettings.nAudioStreaming)
    {
        // CRAP - set special melody section for the final combat zone
        if (IWorldClient.GetCurrentZoneName() == "gz20g")
            pMusicSystem->SetZoneMode("final", IsCaveZone());
        else
            // CRAP ends
            pMusicSystem->SetZoneMode(szCurrentAllodName, IsCaveZone());
    }
    //
    GetPlayer()->AppendPacket(IWorldServer.GetInitialUpdate());
    //
    nFirstSegmentsCounter = 0;
    // process unit statistics in order to create layer textures, pre-caching geometry, etc.
    PrecacheData(statistics, wears);
    //
    if (theSettings.fpDefaultWindForce)
        theWorld.GetTerrain()->SetWindForce(theSettings.fpDefaultWindForce);
    //
    C2DScreenWait::Step(11);
    GetPlayer()->FinishZoneLoading();
    C2DScreenWait::Clear();
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldClient::NewWaterLevels(CMemorySave &packet)
{
    waterLevels.UpdateFrom(packet, pTerrain);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void FormItemMMPData(bool bPrimary, const SUnitWearsStatistics &stat,
                     std::vector<SItemMMPData> &userdata)
{
    const SUDBUnitPrototype *pPrototype = theUnitDatabase.prototypes[stat.wPrototype];
    const SUDBRaceModel *pRace = theUnitDatabase.races[pPrototype->dwRaceIndex];
    int i;
    //
    userdata.clear();
    // form layer textures
    if (stat.armors.size() != 0)
    {
        if (bPrimary)
        {
            // form layer user data
            userdata.resize(RPG_NUM_ARMOR_TYPES + 1);
            // skin
            userdata[0].dwSubtypeID = stat.wPrototype;
            userdata[0].tidTypeID = MOTID_HUMAN;
            userdata[0].dwType = -1;
            // armors
            for (i = 1; i < RPG_NUM_ARMOR_TYPES; ++i)
            {
                if (stat.armors[i] == -1)
                    continue; // empty armor
                //
                userdata[i + 1].dwSubtypeID = stat.armors[i];
                userdata[i + 1].tidTypeID = ITID_ITEM_ARMOR;
                userdata[i + 1].dwType = theItemDatabase.armors[(stat.armors[i] >> 16) & 0xFFFF]
                                             ->dwTypeID;
            }
        }
        else
        {
            // form layer user data
            userdata.resize(2);
            // helm
            if (stat.armors[0] != -1)
            {
                userdata[0].dwSubtypeID = stat.armors[0];
                userdata[0].tidTypeID = ITID_ITEM_ARMOR;
                userdata[0].dwType = theItemDatabase.armors[(stat.armors[0] >> 16) & 0xFFFF]
                                         ->dwTypeID;
            }
            // weapon
            if ((stat.weapons.size() > stat.cCurrentWeaponIndex) &&
                (stat.weapons[stat.cCurrentWeaponIndex] != -1))
            {
                userdata[1].dwSubtypeID = stat.weapons[stat.cCurrentWeaponIndex];
                userdata[1].tidTypeID = ITID_ITEM_WEAPON;
                userdata[1]
                    .dwType = theItemDatabase
                                  .weapons[(stat.weapons[stat.cCurrentWeaponIndex] >> 16) & 0xFFFF]
                                  ->dwTypeID;
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void FormArmorTextureName(DWORD dwItem, const CString &szMaskName, char *buff)
{
    CString szFileName;
    CItemArmor::FormTextureNameMask(szFileName, CItem::GetShape(dwItem),
                                    CItem::GetMaterial(dwItem));
    strcpy(buff, szMaskName);
    strcat(buff, szFileName);
    strcat(buff, ".mmp");
}
/////////////////////////////////////////////////////////////////////////////////////
void FormWeaponTextureName(DWORD dwItem, const CString &szMaskName, char *buff)
{
    const SIDBItemGeneralShape *pShape = theItemDatabase.weapons[(dwItem >> 16) & 0xFFFF];
    CString szFileName;
    // form texture name
    CItemWeapon::FormTextureNameMask(szFileName, pShape,
                                     theItemDatabase.materials[dwItem & 0xFFFF]);
    strcpy(buff, szMaskName);
    strcat(buff, szFileName);
    strcat(buff, ".mmp");
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::PrecacheData(std::vector<SUnitStatistics> &statistics,
                                std::vector<SUnitWearsStatistics> &wears)
{
    // **************************** pre-assemble layered textures ************************* //
    for (auto it = wears.begin(); it != wears.end(); ++it)
    {
        const SUDBUnitPrototype *pPrototype = theUnitDatabase.prototypes[it->wPrototype];
        const SUDBRaceModel *pRace = theUnitDatabase.races[pPrototype->dwRaceIndex];
        std::vector<SItemMMPData> userdata;
        STextureSetDescription set;
        int i;
        if (it->armors.size() != 0)
        {
            CString szTextureName;
            // ****************************** primary texture layers *****************************
            // //
            FormItemMMPData(true, *it, userdata);
            set.data.SetSize(userdata.size() * sizeof(SItemMMPData));
            memcpy(set.data.GetData(), &*userdata.begin(), set.data.GetSize());
            // form texture names
            CString szFileName;
            set.szTextures.RemoveAll();
            if (pPrototype)
                szFileName.Format("%sskin_%.2d.mmp", pRace->szMaskName, pPrototype->dwSkinIndex);
            else
                szFileName.Format("%sskin_00.mmp", pRace->szMaskName);
            set.szTextures.Add(szFileName);
            // 2nd armor layer (non-shaped armors)
            for (i = RPGAST_SHIRT; i <= RPGAST_GLOVES; ++i)
            {
                if (it->armors[i] == -1)
                    continue; // empty armor
                // form texture name
                char buff[256];
                FormArmorTextureName(it->armors[i], pRace->szMaskName, buff);
                set.szTextures.Add(buff);
            }
            // 3rd armor layer (shaped armors w/o helm)
            {
                // wear plate and leggings with the 'wear order' taken in account
                if ((it->armors[RPGAST_PLATE] != -1) && (it->armors[RPGAST_LEGGINGS] != -1))
                {
                    DWORD dwPlateShape = (it->armors[RPGAST_PLATE] >> 16) & 0xFFFF;
                    DWORD dwLeggingsShape = (it->armors[RPGAST_LEGGINGS] >> 16) & 0xFFFF;
                    if (theItemDatabase.armors[dwLeggingsShape]->dwWearOrder <
                        theItemDatabase.armors[dwPlateShape]->dwWearOrder)
                    {
                        char buff[256];
                        FormArmorTextureName(it->armors[RPGAST_LEGGINGS], pRace->szMaskName, buff);
                        set.szTextures.Add(buff);
                        FormArmorTextureName(it->armors[RPGAST_PLATE], pRace->szMaskName, buff);
                        set.szTextures.Add(buff);
                    }
                    else
                    {
                        char buff[256];
                        FormArmorTextureName(it->armors[RPGAST_PLATE], pRace->szMaskName, buff);
                        set.szTextures.Add(buff);
                        FormArmorTextureName(it->armors[RPGAST_LEGGINGS], pRace->szMaskName, buff);
                        set.szTextures.Add(buff);
                    }
                }
                else
                {
                    DWORD dwItem = it->armors[RPGAST_PLATE] != -1 ? it->armors[RPGAST_PLATE]
                                                                  : it->armors[RPGAST_LEGGINGS];
                    if (dwItem != -1)
                    {
                        char buff[256];
                        FormArmorTextureName(dwItem, pRace->szMaskName, buff);
                        set.szTextures.Add(buff);
                    }
                }
            }
            //
            /*
                                    for ( i=RPGAST_PLATE; i<=RPGAST_LEGGINGS; ++i )
                                    {
                                            if ( it->armors[i] == -1 )
                                                    continue;                       // empty armor
                                            // form texture name
                                            char buff[256];
                                            FormArmorTextureName( it->armors[i], pRace->szMaskName,
               buff ); set.szTextures.Add( buff );
                                    }
            */
            // make primary texture
            CString szResultFileName;
            if (it->nID != -1)
                szResultFileName.Format("mo%dp", it->nID);
            else
                szResultFileName.Format("prot%dp", it->wPrototype);

            set.dwTextureDimention = 256;
            pTextureManager->ComposeTexture(szTextureName, set, szResultFileName, szResultFileName,
                                            3, 0, MMP_FORMAT_5551);
            pTextureManager->GetAndLoadTexture(TTID_UNIT_MAIN, szResultFileName);

            // ****************************** secondary texture layers *****************************
            // //
            FormItemMMPData(false, *it, userdata);
            set.data.SetSize(userdata.size() * sizeof(SItemMMPData));
            memcpy(set.data.GetData(), &*userdata.begin(), set.data.GetSize());
            // form texture names
            set.szTextures.RemoveAll();
            // helm
            if (it->armors[0] != -1)
            {
                char buff[256];
                FormArmorTextureName(it->armors[0], pRace->szMaskName, buff);
                set.szTextures.Add(buff);
            }
            // weapon
            if ((it->weapons.size() > it->cCurrentWeaponIndex) &&
                (it->weapons[it->cCurrentWeaponIndex] != -1))
            {
                char buff[256];
                FormWeaponTextureName(it->weapons[it->cCurrentWeaponIndex], pRace->szMaskName,
                                      buff);
                set.szTextures.Add(buff);
            }
            // make secondary texture
            if (it->nID != -1)
                szResultFileName.Format("mo%ds", it->nID);
            else
                szResultFileName.Format("prot%ds", it->wPrototype);

            set.dwTextureDimention = 128;
            pTextureManager->ComposeTexture(szTextureName, set, szResultFileName, szResultFileName,
                                            3, 0, MMP_FORMAT_5551);
            pTextureManager->GetAndLoadTexture(TTID_UNIT_WEAPON, szResultFileName);
        }
    }
    // ************************************ pre-cache geometry ******************************** //
    for (auto pos = statistics.begin(); pos != statistics.end(); ++pos)
    {
        const SUDBUnitPrototype *pPrototype = theUnitDatabase.prototypes[pos->wPrototype];
        const SUDBRaceModel *pRace = theUnitDatabase.races[pPrototype->dwRaceIndex];
        std::vector<std::string> szNames, szBPNames;
        //
        if (pos->szBPs.size() == 0)
            FormBodypartNames(pPrototype, pos->armors, pos->weapons, szNames, szBPNames);
        else
        {
            szNames.reserve(szNames.size() + pos->szBPs.size());
            for (auto pos1 = pos->szBPs.begin(); pos1 != pos->szBPs.end(); ++pos1)
                szNames.push_back(LPCSTR(*pos1));
        }
        szNames.insert(szNames.end(), szBPNames.begin(), szBPNames.end());
        pGeometryManager->GetGeometry(LPCSTR(pRace->szMaskName), szNames, pos->fStrength,
                                      pos->fGrace, pos->fTall);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::ToggleWireframe()
{
    if (!CheckFlag(WC_RENDER_WIREFRAME))
    {
        HRESULT dxrval = theRenderer.SetRenderState(D3DRENDERSTATE_FILLMODE, D3DFILL_WIREFRAME);
        if (dxrval != D3D_OK)
        {
            ReportError(dxrval, "CWorldClient::ToggleWireframe.\n lpD3DDevice->SetRenderState: "
                                "D3DFILL_WIREFRAME.\n");
            return;
        }
        SetFlag(WC_RENDER_WIREFRAME);
    }
    else
    {
        HRESULT dxrval = theRenderer.SetRenderState(D3DRENDERSTATE_FILLMODE, D3DFILL_SOLID);
        if (dxrval != D3D_OK)
        {
            ReportError(
                dxrval,
                "CWorldClient::ToggleWireframe.\n lpD3DDevice->SetRenderState: D3DFILL_SOLID.\n");
            return;
        }
        ResetFlag(WC_RENDER_WIREFRAME);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::SetCameraScroll(CPoint point, int nFlags)
{
    /*
          if ( nFlags & MK_RBUTTON )
          {
                  point.x -= lpD3DWindow->x_disp;
                  point.y -= lpD3DWindow->y_disp;
                  float x = float( point.x - int( lpD3DWindow->d3dViewport.dwWidth/2 ) ) / float(
       lpD3DWindow->d3dViewport.dwWidth/2 ); float y = float( point.y - int(
       lpD3DWindow->d3dViewport.dwHeight/2 ) ) / float( lpD3DWindow->d3dViewport.dwHeight/2 ); if (
       CheckFlag(WC_HOLD_CTRL) )
                  {
                          pCamera->SetRotateValue( theSettings.fCameraXAxisDir * x *
       theSettings.cctCameraRotate.GetMouseValue() ); pCamera->SetPitchValue(
       theSettings.fCameraYAxisDir * y * theSettings.cctCameraPitch.GetMouseValue() );
                  }
                  else
                          pCamera->SetScrollValue( CPlot2D(x, y) *
       theSettings.cctCameraScroll.GetMouseValue() );
          }
          else
          {
                  if ( CheckFlag(WC_HOLD_CTRL) )
                  {
                          pCamera->SetRotateValue( 0 );
                          pCamera->SetPitchValue( 0 );
                  }
                  else
                          pCamera->SetScrollValue( CPlot2D(0, 0) );
          }
    */
}
/////////////////////////////////////////////////////////////////////////////////////
CPlayerClient *CWorldClient::GetPlayer()
{
    if (pPlayer)
        return (CPlayerClient *)pPlayer->GetPlayerSpecific();
    else
        return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::ShowFPSOn()
{
    dwShowStatisticsFlag |= WCSS_SHOW_FPS;
}
void CWorldClient::ShowFPSOff()
{
    dwShowStatisticsFlag &= ~WCSS_SHOW_FPS;
    CDebugInfoSystemInterface *lpDIS = GetDebugInfoSystem();
    lpDIS->Remove("FPS");
}
void CWorldClient::ShowRenderInfoOn()
{
    dwShowStatisticsFlag |= WCSS_SHOW_RENDER_INFO;
}
void CWorldClient::ShowRenderInfoOff()
{
    dwShowStatisticsFlag &= ~WCSS_SHOW_RENDER_INFO;
    CDebugInfoSystemInterface *lpDIS = GetDebugInfoSystem();
    lpDIS->Remove("triangles submitted");
    lpDIS->Remove("vertices submitted");
}
void CWorldClient::ShowManagersOn()
{
    dwShowStatisticsFlag |= WCSS_SHOW_MANAGERS;
}
void CWorldClient::ShowManagersOff()
{
    dwShowStatisticsFlag &= ~WCSS_SHOW_MANAGERS;
    CDebugInfoSystemInterface *lpDIS = GetDebugInfoSystem();
    lpDIS->Remove(pFigureManager->GetGameClassName());
    lpDIS->Remove(pMapObjectManager->GetGameClassName());
    lpDIS->Remove(pTextureManager->GetGameClassName());
    lpDIS->Remove(pParticleManager->GetGameClassName());
    lpDIS->Remove(pLightManager->GetGameClassName());
}
void CWorldClient::ShowWorldDataOn()
{
    dwShowStatisticsFlag |= WCSS_SHOW_WORLD_DATA;
}
void CWorldClient::ShowWorldDataOff()
{
    dwShowStatisticsFlag &= ~WCSS_SHOW_WORLD_DATA;
    CDebugInfoSystemInterface *lpDIS = GetDebugInfoSystem();
    lpDIS->Remove("Last Event");
    lpDIS->Remove("World Time");
}
/////////////////////////////////////////////////////////////////////////////////////
int CWorldClient::AddPolyLine(CPolyLine *lpPolyLine)
{
    int nKey = polylines.FindFirstFreeKey();
    polylines[nKey] = lpPolyLine;
    lpPolyLine->SetID(nKey);
    return nKey;
}
/////////////////////////////////////////////////////////////////////////////////////
CPolyLine *CWorldClient::RemovePolyLine(int nID)
{
    return polylines.RemoveAt(nID);
}
/////////////////////////////////////////////////////////////////////////////////////
CPolyLine *CWorldClient::FindPolyLine(int nID)
{
    return polylines[nID];
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldClient::DrawPolyLines()
{
    CHashMapIterator<int, CPolyLine *> hmi;
    CPolyLine *lpPolyLine = hmi.GetFirst(&polylines);
    //
    while (hmi.IsReachEnd() != TRUE)
    {
        lpPolyLine->Draw(theRenderer.GetGPU(), &theRenderer);
        lpPolyLine = hmi.GetNext();
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
class CObjectTextureLessThenFunctional
{
public:
    bool operator()(CObject3D *pObject1, CObject3D *pObject2) const
    {
#if defined(__GAME__) && !defined(_A4)
        if (pObject1 && pObject2)
        {
            CObject3DClientSpecific *pOCS1 = static_cast<CObject3DClientSpecific *>(
                pObject1->GetObjectSpecific());
            CObject3DClientSpecific *pOCS2 = static_cast<CObject3DClientSpecific *>(
                pObject2->GetObjectSpecific());
            if (pOCS1 && pOCS2)
            {
                CTexture *pTexture1 = pOCS1->GetPrimaryTexture();
                CTexture *pTexture2 = pOCS2->GetPrimaryTexture();
                if (pTexture1 < pTexture2)
                    return true;
                else if (pTexture1 == pTexture2)
                {
                    if (pObject1->GetGeometry() < pObject2->GetGeometry())
                        return true;
                    else if (pObject1->GetGeometry() == pObject2->GetGeometry())
                        return (pObject1->GetRespectCP().z < pObject2->GetRespectCP().z);
                    else
                        return false;
                }
                else
                    return false;
            }
        }
        return false;
#else
        return (static_cast<CObject3DClientSpecific *>(pObject1->GetObjectSpecific())
                    ->GetPrimaryTexture() <
                static_cast<CObject3DClientSpecific *>(pObject2->GetObjectSpecific())
                    ->GetPrimaryTexture());
#endif // defined(__GAME__) && !defined(_A4)
    }
};
/////////////////////////////////////////////////////////////////////////////////////
class CObjectLightTimeLessThenFunctional
{
public:
    bool operator()(CObject3D *pObject1, CObject3D *pObject2) const
    {
        DWORD dwTime1 = static_cast<CObject3DClientSpecific *>(pObject1->GetObjectSpecific())
                            ->GetLastLightTime();
        DWORD dwTime2 = static_cast<CObject3DClientSpecific *>(pObject2->GetObjectSpecific())
                            ->GetLastLightTime();
        return dwTime2 < dwTime1;
    }
};
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldClient::Draw()
{
    CDXGeometryProcessor *pGPU = theRenderer.GetGPU();
    CRenderer *pRend = &theRenderer;
    //
    AdjustLightingWithTime();
    // flush all previos renders
    pRend->FlushBuffers();
    // create camera matrix
    SHMatrix matrix;
    pGPU->CreateCameraMatrix(matrix, &theCamera);
    pGPU->PopMatrix(MAT_STACK_CAMERA);          // remove old camera matrix
    pGPU->PushMatrix(matrix, MAT_STACK_CAMERA); // add new one
    pRend->SetProjectionMatrixOne();
    pGPU->PushMainMatrix(pGPU->GetMatrix(MAT_STACK_CAMERA));
    pGPU->PushMatrix(pGPU->GetMatrix(MAT_STACK_CAMERA), MAT_STACK_VIEW2CLIP);
    // render sky object
#ifndef _A4
    if (CheckFlag(WC_RENDER_SKY) && pSky)
    {
        //			pRend->SetRenderState( D3DRENDERSTATE_ALPHAREF, 10 );
        pRend->SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, FALSE);
        pRend->SetTexturingAlphaMode(AM_VERTEX);
        //			pRend->SetRenderState( D3DRENDERSTATE_ZENABLE, D3DZB_TRUE );
        pRend->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, FALSE);
        pRend->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
        pRend->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, TRUE);

        pRend->SetAddFogOff();
        pSky->Draw(&theRenderer, pCamera->GetCP());
        pRend->FlushBuffers(); // flush terrain render data
        pRend->SetAddFogOn();

        pRend->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_CW);
        pRend->SetRenderState(D3DRENDERSTATE_SPECULARENABLE, FALSE);
        pRend->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, TRUE);
    }
    LMPTimePassed();
#endif // _A4
       // render terrain's main part
    if (CheckFlag(WC_RENDER_TERRAIN) && pTerrain)
    {
        CPlot3D centerPoint = pCamera->GetIntersectionWithZero();
        pTerrain->SetClip(pCamera->GetCPX(), pCamera->GetCPY(), 100.0f);
        pRend->SetTexturingAlphaMode(AM_DEFAULT);
        pTerrain->DrawBase(&theRenderer);
        pRend->FlushBuffers();
    }
    // render main 3D scene: objects and their shadows
    Draw3DScene();
    LMPTimePassed();
    // render terrain's transparent part (water)
    if (CheckFlag(WC_RENDER_TERRAIN) && pTerrain)
    {
        CPlot3D centerPoint = pCamera->GetIntersectionWithZero();
        pTerrain->SetClip(pCamera->GetCPX(), pCamera->GetCPY(), 100.0f);
        // flush previous renders
        pRend->FlushBuffers();
        pRend->SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, FALSE);
        pRend->SetTexturingAlphaMode(AM_VERTEX);
        pTerrain->DrawTransparent(&theRenderer);
        pRend->FlushBuffers();
    }
    LMPTimePassed();
    // render effects
    if (CheckFlag(WC_RENDER_EFFECTS))
    {
        // flush previous renders
        pRend->FlushBuffers();
        pRend->SetTexturingAlphaMode(AM_MULTIPLY);
        // particles
        theRenderer.SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
        theRenderer.SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, FALSE);
        theRenderer.SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
        theRenderer.SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
        pRend->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, FALSE);
        GetVisFogManager()->Draw(&theRenderer);
        pRend->FlushBuffers();
        //        ;
        theRenderer.SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
        theRenderer.SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
        pRend->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, FALSE);
        static_cast<CParticleManager *>(GetParticleManager())->Draw(&theRenderer);
        pRend->FlushBuffers();
        // lightning
        theRenderer.SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
        theRenderer.SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
        GetLightningManager()->Draw(&theRenderer);
        pRend->FlushBuffers();
        // weather
        theRenderer.SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
        theRenderer.SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
        weather.Draw(&theRenderer);
        pRend->FlushBuffers();
        pRend->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, TRUE);
    }
    pRend->SetTexturingAlphaMode(AM_VERTEX);
    LMPTimePassed();
    //
    DrawPolyLines();
    // pop main and camera matrix
    pGPU->PopMatrix(MAT_STACK_VIEW2CLIP);
    pGPU->PopMainMatrix();
    theRenderer.SetProjectionMatrixOne();
    // check for flying hp rendering
    if (CheckFlag(WC_RENDER_FLYINGHP))
    {
        pRend->SetRenderState(D3DRENDERSTATE_ZENABLE, D3DZB_FALSE);
        pRend->SetTexturingAlphaMode(AM_MULTIPLY);
        pRend->SetAddFogOff();
        // for (int i=0; i<10; ++i)
        pFlyingHP->Draw(&theRenderer);
        pRend->SetAddFogOn();
        pRend->FlushBuffers();
        pRend->SetRenderState(D3DRENDERSTATE_ZENABLE, TRUE);
    }

    if (false)
    {
        using namespace DirectX;
        using namespace SimpleMath;

        /*CDX11* pDX11 = theInterface.GetD3D();

        auto device = pDX11->GetDevice().Get();
        auto dc = pDX11->GetDeviceContext().Get();

        std::unique_ptr<PrimitiveBatch<VertexPositionColor>> batch;
        batch = std::make_unique<PrimitiveBatch<VertexPositionColor>>(dc);

        auto cam = GetCamera();
        auto cp = cam->GetCP();

        std::unique_ptr<BasicEffect> basicEffect;

        basicEffect = std::make_unique<BasicEffect>(device);

        DWORD screenWidth, screenHeight;
        pDX11->GetCurrentMode(screenWidth, screenHeight);
        basicEffect->SetProjection(
            XMMatrixOrthographicOffCenterRH(0, screenWidth, screenHeight, 0, -1, 1));
        basicEffect->SetVertexColorEnabled(true);

        Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout;
        IfFailedThrowHR(
            CreateInputLayoutFromEffect<VertexPositionColor>(device, basicEffect.get(),
                                                    inputLayout.ReleaseAndGetAddressOf()));

        auto states = pDX11->GetCommonStates();
        dc->OMSetBlendState(states->Opaque(), Colors::White, 0xFFFFFFFF);
        dc->OMSetDepthStencilState(states->DepthNone(), 0);
        dc->RSSetState(states->CullNone());

        basicEffect->Apply(dc);
        dc->IASetInputLayout(inputLayout.Get());

        batch->Begin();

        VertexPositionColor v1(Vector3(0., screenHeight, 0.0f ), Colors::Yellow);
        VertexPositionColor v2(Vector3(screenWidth, screenHeight, 0.0f), Colors::Yellow);
        VertexPositionColor v3(Vector3(screenWidth/2, 0.0f, 0.0f), Colors::Yellow);

        batch->DrawTriangle(v1, v2, v3);
        batch->End();*/

        // batch->Begin();
        // VertexPositionColor v1(cp, { 0.5, 0, 0.0, 1.0 });
        // VertexPositionColor v2(cp + CPlot3D(0.7, 0.7, -1.0), { 0., 0.5, 0.0, 1.0 });
        // batch->End();
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL AddScreenRects(const CMapObject *pObject, std::list<SObjectScreenRect> *result)
{
    SObjectScreenRect osr;
    if (pObject->CheckFlag(OBJ_VISIBLE) && !pObject->CheckFlag(OBJ_HIDDEN))
    {
        osr.clsid = pObject->WhoAmI();
        osr.nID = pObject->GetID();
        osr.rcRect = pObject->GetScreenRect();
        result->push_back(osr);

        return TRUE;
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
class CDrawSelectionFunctional
{
private:
    CBaseManager *pManager;

public:
    explicit CDrawSelectionFunctional(CBaseManager *pNewManager)
        : pManager(pNewManager)
    {
    }
    bool operator()(int nID)
    {
        CMapObject *pObject = (CMapObject *)pManager->Find(nID);
        if (pObject)
        {
            if (pObject->IsDead())
            {
                pObject->Select(FALSE);
                return true;
            }
            else
            {
                CPlot3D ptPoint = pObject->GetCP();
                CFigure *pFigure = pObject->MatchFigure("hp");
                if (pFigure)
                {
                    ptPoint = pObject->GetFigureWorldPosition(pFigure);
                    ptPoint.z = pObject->GetCPZ();
                }
                theRenderer.DrawSelection(ptPoint, 0.5f, pObject->GetXAxis(), pObject->GetYAxis());
            }
        }
        return false;
    }
};
/////////////////////////////////////////////////////////////////////////////////////
namespace NWorldClient {
// visibility lists
std::list<CSmartPtr<CMapObject>> dynamicObjects;
std::vector<CMapObject *> visibleBuildings;
std::vector<CMapObject *> visibleObjects;
std::vector<CMapObject *> visibleLevers;
std::vector<CMapObject *> visibleUnits;
std::vector<CMapObject *> visibleTrees;
// shadow lists
std::vector<CMapObject *> shadowBuildings;
std::vector<CMapObject *> shadowObjects;
std::vector<CMapObject *> shadowLevers;
std::vector<CMapObject *> shadowUnits;
std::vector<CMapObject *> shadowTrees;
//
std::vector<CMapObject *> relights;
//
void ClearLists()
{
    dynamicObjects.clear();
    visibleBuildings.clear();
    visibleObjects.clear();
    visibleLevers.clear();
    visibleUnits.clear();
    visibleTrees.clear();
    shadowBuildings.clear();
    shadowObjects.clear();
    shadowLevers.clear();
    shadowUnits.clear();
    shadowTrees.clear();
    relights.clear();
}
}; // namespace NWorldClient
using namespace NWorldClient;
/////////////////////////////////////////////////////////////////////////////////////
// iterate 'pObjects' list in order to determin objects PVS.
// if bPickDynamic == false, then put all dynamic objects (e.g. units) in the 'dynamicObjects' list
// else, process it
// return nRelightCounter for this objects list.
int CWorldClient::FormVisibilityLists(std::list<CSmartPtr<CMapObject>> *pObjects,
                                      const SHMatrix &matCamera, float fpTime,
                                      DWORD dwCurrentLightTime, bool bPickDynamic, bool bPickHeroes)
{
    CDXGeometryProcessor *pGPU = theRenderer.GetGPU();
    int nRelightCounter = 0;
    bool bDrawInvisibleUnits;
    //
    bDrawInvisibleUnits = !theSettings.nDrawVisibleOnly || pPlayer->curInterface != IFACE_MISSION;
    for (auto pos = pObjects->begin(); pos != pObjects->end(); ++pos)
    {
        CMapObject *pObject = *pos;
        //
        if ((pObject == 0) || (pObject->GetObjectSpecific() == 0))
            continue;
        // set exact position for units
        if (pObject->WhoAmI() == HPC_UNIT)
        {
            if ((pObject->GetTypeID() == MOTID_HUMAN) && IsIDofPersonage(pObject->GetID()) &&
                !bPickHeroes)
                continue;
            // обработать юнитов надо, ...
            if (bPickDynamic)
                pObject->SetExactPosition(fpTime);
            else
            {
                dynamicObjects.push_back(pObject);
                continue;
            }
            // ... а вот показывать только тех, кого наши персы видят
            if (!(bDrawInvisibleUnits || pObject->CheckFlag(UNIT_VISIBLE)))
                continue;
        }
        // first, check all the objects for visibility
        pObject->CreateCurrentPlacement();
        pGPU->PushMatrix(pObject->GetPlacement(), MAT_STACK_PROJSPACE);
        int nRetCode = theRenderer.CheckVisibility(pObject->GetBoundingVolume());
        pGPU->PopMatrix(MAT_STACK_PROJSPACE);
        if (nRetCode != TRV_INVISIBLE)
        {
            pObject->SetFlag(OBJ_VISIBLE);
            // respectCP для z-sorting в пространстве камеры
            pObject->SetRespectCP(matCamera.RotateVector(pObject->GetCP()));
            switch (pObject->WhoAmI()) // add to list by type: unit/building/tree/object
            {
            case HPC_UNIT:
                visibleUnits.push_back(pObject);
                shadowUnits.push_back(pObject);
                break;
            case HPC_BUILDING:
                visibleBuildings.push_back(pObject);
                shadowBuildings.push_back(pObject);
                break;
            case HPC_TREE:
                visibleTrees.push_back(pObject);
                shadowTrees.push_back(pObject);
                break;
            case HPC_LEVER:
                visibleLevers.push_back(pObject);
                shadowLevers.push_back(pObject);
                break;
            default:
                visibleObjects.push_back(pObject);
                shadowObjects.push_back(pObject);
            }
            if (pObject->CheckFlag(OBJ_STATIC) &&
                (dwCurrentLightTime -
                     static_cast<CObject3DClientSpecific *>(pObject->GetObjectSpecific())
                         ->GetLastLightTime() >
                 theSettings.nLightingFreq))
            {
                relights.push_back(pObject);
                ++nRelightCounter;
            }
        }
        else
        {
            pObject->ResetFlag(OBJ_VISIBLE);
            // добавить в списки теней по детальной проверке тени
            if (static_cast<CObject3DClientSpecific *>(pObject->GetObjectSpecific())
                    ->IsShadowVisible())
            {
                if (pObject->GetAnimationSystem2())
                    pObject->GetAnimationSystem2()->RecalcSkeletonWithCurrentTime();
                switch (pObject->WhoAmI()) // add to list by type: unit/building/tree/object
                {
                case HPC_UNIT:
                    shadowUnits.push_back(pObject);
                    break;
                case HPC_BUILDING:
                    shadowBuildings.push_back(pObject);
                    break;
                case HPC_TREE:
                    shadowTrees.push_back(pObject);
                    break;
                case HPC_LEVER:
                    shadowLevers.push_back(pObject);
                    break;
                default:
                    shadowObjects.push_back(pObject);
                }
            }
        }
    }

    return nRelightCounter;
}
/////////////////////////////////////////////////////////////////////////////////////
// perform all operation steps to draw current d3d scene
BOOL CWorldClient::Draw3DScene()
{
    if (!pTerrain || !pTerrain->IsLoaded())
        return FALSE;
    CRenderer *pRend = &theRenderer;
    CDXGeometryProcessor *pGPU = theRenderer.GetGPU();
    DWORD dwCurrentLightTime = timeGetTime();
    CMapObject *pObject = 0;
    const SHMatrix &matCamera = pGPU->GetMatrix(MAT_STACK_CAMERA);
    pRend->SetCameraPosition(theCamera.GetCP());
    // now we can test only this objects for visibility in the camera (against the current matrix
    // stack)
    NWorldClient::ClearLists();
    float fpTime = theWorld.GetPreciseTime();
    int nRelightCounter = 0;
    // iterate all object and store dynamic in the separate list
    const CPlot3D &ptObservationPosition = pCamera->GetObservationPosition();
    // set time for 'quest notifier MOSHKA' :)
    if (pMoshka)
        pMoshka->SetTime(fpTime);
    // puck objects from the area map
    const float fPickRadius = 100;
    std::list<CSmartPtr<CMapObject>> *pObjects = areaObjects.GetFirstList(
        Max(0.0f, Min(pTerrain->GetMaxX() - 1, ptObservationPosition.x - fPickRadius)),
        Max(0.0f, Min(pTerrain->GetMaxY() - 1, ptObservationPosition.y - fPickRadius)),
        Min(pTerrain->GetMaxX() - 1, ptObservationPosition.x + fPickRadius),
        Min(pTerrain->GetMaxY() - 1, ptObservationPosition.y + fPickRadius));
    while (pObjects)
    {
        nRelightCounter += FormVisibilityLists(pObjects, matCamera, fpTime, dwCurrentLightTime,
                                               false, false);
        // get next list with objects
        pObjects = areaObjects.GetNextList();
    }
    // iterate dynamic list
    nRelightCounter += FormVisibilityLists(&dynamicObjects, matCamera, fpTime, dwCurrentLightTime,
                                           true, false);
    dynamicObjects.clear();
    // pick heroes
    nRelightCounter += FormVisibilityLists(&heroes, matCamera, fpTime, dwCurrentLightTime, true,
                                           true);
    // sort relight list by light time
    if (!relights.empty())
    {
        std::sort(relights.begin(), relights.end(), CObjectLightTimeLessThenFunctional());
        nRelightCounter >>= 3;
        if (nRelightCounter < 10)
            nRelightCounter = 10;
        for (auto iter = relights.begin(); iter != relights.end() && nRelightCounter != 0;
             ++iter, --nRelightCounter)
        {
            (*iter)->ResetFlag(OBJ_LIGHTED);
        }
    }
    // sort this lists by primary textures
    CObjectTextureLessThenFunctional cmpfunct;
    if (CheckFlag(WC_RENDER_OBJECTS | WC_RENDER_UNITS))
    {
        pRend->SetTexturingAlphaMode(AM_MULTIPLY);
        // remove all object screen rect structs (from the last render)
        lstScreenObjects.clear();
        // flush previous renders
        pRend->FlushBuffers();
        // set alpha blend mode and alpha ref value
        pRend->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
        pRend->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
        pRend->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
        pRend->SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, TRUE);
        pRend->FlushBuffers();
    }
    auto lbObjDrawWithoutBBox = [pRend](CObject3D *obj) { obj->DrawWithoutBBox(pRend); };
    auto lbObjDraw = [pRend](CObject3D *obj) { obj->Draw(pRend); };
    auto lbAddObjScreenRects = [this](const CMapObject *obj) {
        AddScreenRects(obj, &lstScreenObjects);
    };
    // check for objects rendering
    if (CheckFlag(WC_RENDER_OBJECTS))
    {
        std::sort(visibleBuildings.begin(), visibleBuildings.end(), cmpfunct);
        std::sort(visibleObjects.begin(), visibleObjects.end(), cmpfunct);
        std::sort(visibleLevers.begin(), visibleLevers.end(), cmpfunct);
        std::sort(visibleTrees.begin(), visibleTrees.end(), cmpfunct);
        // enable alpha blending and alpha ref testing
        pRend->SetRenderState(D3DRENDERSTATE_ALPHAREF, 200);
        // draw objects
        for_each(visibleObjects.begin(), visibleObjects.end(), lbObjDrawWithoutBBox);
        // draw levers (and add screen rects)
        for_each(visibleLevers.begin(), visibleLevers.end(), lbObjDraw);
        for_each(visibleLevers.begin(), visibleLevers.end(), lbAddObjScreenRects);

        pRend->FlushBuffers();
        // draw buildings
        for_each(visibleBuildings.begin(), visibleBuildings.end(), lbObjDrawWithoutBBox);
        // draw trees
        for_each(visibleTrees.begin(), visibleTrees.end(), lbObjDrawWithoutBBox);
        pRend->FlushBuffers();
    }
    // check for Units rendering
    if (CheckFlag(WC_RENDER_UNITS) && !visibleUnits.empty())
    {
        std::sort(visibleUnits.begin(), visibleUnits.end(), cmpfunct);
        // enable alpha blending and alpha ref testing
        pRend->SetRenderState(D3DRENDERSTATE_ALPHAREF, 10);
        // draw units
        // pRend->SetTexturingAlphaMode(AM_MULTIPLY);
        for_each(visibleUnits.begin(), visibleUnits.end(), lbObjDraw);
        for_each(visibleUnits.begin(), visibleUnits.end(), lbAddObjScreenRects);
        pRend->FlushBuffers();
    }
    // draw terrain prints (footprints, blood and fireprints)
    if (!bloodprints.empty() || !footprints.empty() || !fireprints.empty())
    {
        pRend->SetTexturingAlphaMode(AM_MULTIPLY);
        pRend->SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, FALSE);
        pRend->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, FALSE);
        if (theSettings.nTerrainEnableBloodprints)
            for_each(bloodprints.begin(), bloodprints.end(), [](CBloodPrints &obj) { obj.Draw(); });
        if (theSettings.nTerrainEnableFootprints)
            for_each(footprints.begin(), footprints.end(), [](CFootPrints &obj) { obj.Draw(); });
        if (theSettings.nTerrainEnableFireprints)
            for_each(fireprints.begin(), fireprints.end(), [](CFirePrints &obj) { obj.Draw(); });
        pRend->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, TRUE);
    }
    // draw shadows
    if (CheckFlag(WC_RENDER_SHADOWS))
    {
        pRend->FlushBuffers();
        ModShadowSchedule::BeginFrame();
        pRend->GetD3D()->BeginModShadows();
        std::sort(shadowBuildings.begin(), shadowBuildings.end(), cmpfunct);
        std::sort(shadowObjects.begin(), shadowObjects.end(), cmpfunct);
        std::sort(shadowLevers.begin(), shadowLevers.end(), cmpfunct);
        std::sort(shadowTrees.begin(), shadowTrees.end(), cmpfunct);
        // shift shadow in camera direction
        SHMatrix matrix;
        Identity(matrix);
        CPlot3D ptZAxis = theCamera.GetZAxis();
        matrix._14 = -ptZAxis.x * 0.02f;
        matrix._24 = -ptZAxis.y * 0.02f;
        matrix._34 = -ptZAxis.z * 0.02f;
        pGPU->PushMainMatrix(matrix);
        //
        pRend->SetTexturingAlphaMode(AM_MULTIPLY);
        pRend->SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, FALSE);
        pRend->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, FALSE);
        pTerrain->DrawModTerrainShadow(pRend);
        auto lbObjDrawShadow = [pRend](CObject3D *obj) { obj->DrawRealisticShadow2(pRend); };
        for_each(shadowLevers.begin(), shadowLevers.end(), lbObjDrawShadow);
        for_each(shadowObjects.begin(), shadowObjects.end(), lbObjDrawShadow);
        for_each(shadowBuildings.begin(), shadowBuildings.end(), lbObjDrawShadow);
        for_each(shadowTrees.begin(), shadowTrees.end(), lbObjDrawShadow);
        for_each(shadowUnits.begin(), shadowUnits.end(), lbObjDrawShadow);
        pRend->FlushBuffers();
        pRend->GetD3D()->EndModShadows();
        ModShadowSchedule::EndFrame();
        pRend->SetRenderState(D3DRENDERSTATE_ZWRITEENABLE, TRUE);
        //
        pGPU->PopMainMatrix();

        //
    }
    // draw selection for objects
    if (!selectedObjects.empty() && (theSettings.nSelectionType == 1) && CheckFlag(WC_RENDER_UNITS))
    {
        pRend->SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, FALSE);
        theRenderer.BindTexture(0, (CDXGISurface *)0);
        // draw selection and remove dead units from selection
        selectedObjects.remove_if(CDrawSelectionFunctional(pMapObjectManager));
    }
    // remove this render frame data
    NWorldClient::ClearLists();
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// determine terrain's 3D coords from screen's 2D as iteration numeric solution
CPlot3D CWorldClient::GetTerrainXYZFromScreenXY(int sx, int sy)
{
    // create correct matrix in screen space 2
    CDXGeometryProcessor *pGPU = theRenderer.GetGPU();
    if (!pGPU->IsHaveMatrix(MAT_STACK_CAMERA))
        return VNULL;
    pGPU->PushMatrix(pGPU->GetMatrix(MAT_STACK_CAMERA), MAT_STACK_SCRNSPACE2);
    //
    const SHMatrix &m = theRenderer.GetGPU()->GetMatrix(MAT_STACK_SCRNSPACE2);
    float denominator = (m._42 * m._21 - m._22 * m._41) * sx +
                        (m._12 * m._41 - m._11 * m._42) * sy + (m._11 * m._22 - m._12 * m._21);
    float x = 0, y = 0, z = 0, z1 = 0;

    if (denominator)
    {
        float xz = ((m._22 * m._43 - m._23 * m._42) * sx + (m._42 * m._13 - m._43 * m._12) * sy +
                    (m._23 * m._12 - m._22 * m._13)) /
                   denominator;
        float xadd = ((m._44 * m._22 - m._42 * m._24) * sx + (m._42 * m._14 - m._44 * m._12) * sy +
                      (m._24 * m._12 - m._22 * m._14)) /
                     denominator;
        float yz = -((m._43 * m._21 - m._41 * m._23) * sx + (m._13 * m._41 - m._11 * m._43) * sy +
                     (m._11 * m._23 - m._13 * m._21)) /
                   denominator;
        float yadd = -((m._44 * m._21 - m._41 * m._24) * sx + (m._14 * m._41 - m._11 * m._44) * sy +
                       (m._11 * m._24 - m._14 * m._21)) /
                     denominator;
        // begin iterations
        z1 = z = pTerrain->GetAverageZ();
        // select first z value as a average between terrain's average and first result
        x = xz * z + xadd;
        y = yz * z + yadd;
        x = Max(x, 0.0f);
        y = Max(y, 0.0f);
        z1 = pTerrain->GetZValue(x, y);
        z1 = z = (z + z1) / 2.0f;
        //
        int nIterationCount = 0;
        do
        {
            z = z1;
            x = xz * z + xadd;
            y = yz * z + yadd;
            x = Max(x, 0.0f);
            y = Max(y, 0.0f);
            z1 = pTerrain->GetZValue(x, y);
            nIterationCount++;
        } while ((fabs(z1 - z) > 0.01) && (nIterationCount < 100));
        // теперь проведём итерации с усреднением
        if (nIterationCount >= 100)
        {
            nIterationCount = 0;
            do
            {
                z = (z + z1) / 2.0f;
                x = xz * z + xadd;
                y = yz * z + yadd;
                x = Max(x, 0.0f);
                y = Max(y, 0.0f);
                z1 = pTerrain->GetZValue(x, y);
                ++nIterationCount;
            } while ((fabs(z1 - z) > 0.01) && (nIterationCount < 100));
        }
    }
    // remove camera matrix from screen space2
    pGPU->PopMatrix(MAT_STACK_SCRNSPACE2);
    // return result...
    return CPlot3D(x, y, z1);
}
/////////////////////////////////////////////////////////////////////////////////////
CPlot3D CWorldClient::GetTerrainXYZFromScreenXYWithBridge(int sx, int sy)
{
    // create correct matrix in screen space 2
    CDXGeometryProcessor *pGPU = theRenderer.GetGPU();
    if (!pGPU->IsHaveMatrix(MAT_STACK_CAMERA))
        return VNULL;
    pGPU->PushMatrix(pGPU->GetMatrix(MAT_STACK_CAMERA), MAT_STACK_SCRNSPACE2);
    //
    const SHMatrix &m = theRenderer.GetGPU()->GetMatrix(MAT_STACK_SCRNSPACE2);
    float denominator = (m._42 * m._21 - m._22 * m._41) * sx +
                        (m._12 * m._41 - m._11 * m._42) * sy + (m._11 * m._22 - m._12 * m._21);
    float x = 0, y = 0, z = 0, z1 = 0;

    if (denominator)
    {
        float xz = ((m._22 * m._43 - m._23 * m._42) * sx + (m._42 * m._13 - m._43 * m._12) * sy +
                    (m._23 * m._12 - m._22 * m._13)) /
                   denominator;
        float xadd = ((m._44 * m._22 - m._42 * m._24) * sx + (m._42 * m._14 - m._44 * m._12) * sy +
                      (m._24 * m._12 - m._22 * m._14)) /
                     denominator;
        float yz = -((m._43 * m._21 - m._41 * m._23) * sx + (m._13 * m._41 - m._11 * m._43) * sy +
                     (m._11 * m._23 - m._13 * m._21)) /
                   denominator;
        float yadd = -((m._44 * m._21 - m._41 * m._24) * sx + (m._14 * m._41 - m._11 * m._44) * sy +
                       (m._11 * m._24 - m._14 * m._21)) /
                     denominator;
        // begin iterations
        z1 = z = pTerrain->GetAverageZ();

        int nIterationCount = 0;
        // select first z value as a average between terrain's average and first result
        z = z1;
        x = xz * z + xadd;
        y = yz * z + yadd;
        x = Max(x, 0.0f);
        y = Max(y, 0.0f);
        z1 = GetBridgeZValue(x, y);
        z1 = z = (z + z1) / 2.0f;
        //
        do
        {
            z = z1;
            x = xz * z + xadd;
            y = yz * z + yadd;
            x = Max(x, 0.0f);
            y = Max(y, 0.0f);
            z1 = GetBridgeZValue(x, y);
            nIterationCount++;
        } while ((fabs(z1 - z) > 0.01) && (nIterationCount < 100));
        // теперь проведём итерации с усреднением
        if (nIterationCount >= 100)
        {
            nIterationCount = 0;
            do
            {
                z = (z + z1) / 2.0f;
                x = xz * z + xadd;
                y = yz * z + yadd;
                x = Max(x, 0.0f);
                y = Max(y, 0.0f);
                z1 = GetBridgeZValue(x, y);
                ++nIterationCount;
            } while ((fabs(z1 - z) > 0.01) && (nIterationCount < 100));
        }
    }
    // remove camera matrix from screen space2
    pGPU->PopMatrix(MAT_STACK_SCRNSPACE2);
    // return result...
    return CPlot3D(x, y, z1);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::CastShadowToTerrain(const CPlot3D &ptSunDir, const CPlot3D &ptCenter,
                                       float fpRadius, CXYZW4Array &srcRect, CXYZW4Array &dstGeoms,
                                       CPlot2DArray &dstMaps, CWORDArray &dstIndices,
                                       bool bSmallPrint, bool bCastOnBridges)
{
    // void AddTriangle( const CPlot3D &ptA, const CPlot3D &ptB, const CPlot3D &ptC );
    // void AllocForTriangles( int nTris );
    static CShadowBuilder builder;
    static CXYZWArray result;
    builder.Setup(dstGeoms, dstMaps, dstIndices, srcRect, ptSunDir, ptCenter, fpRadius);
    if (!bSmallPrint || !IsBridge(ptCenter.x, ptCenter.y))
        pTerrain->BuildShadowMesh(builder, srcRect);
    //
    CDXGeometryProcessor *pGPU = theRenderer.GetGPU();

    if (bCastOnBridges)
    {
        for (POSITION pos = bridgesManager.bridges.GetHeadPosition(); pos;)
        {
            SBridge &b = bridgesManager.bridges.GetNext(pos);
            CMapObject *pMO = GetMapObject(b.nID);
            if (!pMO)
                continue;
            const CPlot3D &bvCenter = pMO->bv.ptCenter;
            if (!builder.CheckSphere(pMO->GetCP(), pMO->bv.fpRadius + fabs(bvCenter)))
                continue;
            pMO->CreateCurrentPlacement();
            const SHMatrix &objMatrix = pMO->GetPlacement();
            if (!builder.CheckSphere(objMatrix.RotateVector(bvCenter) + pMO->GetCP(),
                                     pMO->bv.fpRadius))
                continue;
            pGPU->PushMatrix(objMatrix, MAT_STACK_TEMPORARY);
            IGeometry *pGeom = pMO->GetGeometry();
            if (pGeom != 0)
            {
                for (int i = 0; i < pMO->GetNumFigures(); i++)
                {
                    CFigure *pFig = pMO->GetFigure(i);
                    pFig->CreateCurrentPlacement();
                    pGPU->PushMatrix(pFig->GetPlacement(), MAT_STACK_TEMPORARY);
                    const SHMatrix &matrix = pGPU->GetMatrix(MAT_STACK_TEMPORARY);

                    const SXYZW4 *plots;
                    const SVertexComponents *comps;
                    const WORD *inds;
                    DWORD dwNumIndices, dwNumPlots;
                    pGeom->GetRangeGeometryData(pFig->GetName(), plots, comps, inds, dwNumPlots,
                                                dwNumIndices);
                    result.SetSize(dwNumPlots * 4); // ARRAY BOUNDS CHECK
                    //
                    SHMatrix matTransposed(matrix);
                    Transpose(matTransposed);
                    pGPU->GetGeometryProcessor()->SimplyTransformNC(
                        reinterpret_cast<const D3DMATRIX &>(matTransposed),
                        const_cast<SXYZW4 *>(plots), result.GetData(), dwNumPlots);
                    builder.AllocForTriangles(dwNumIndices * 6);
                    for (int k = 0; k < dwNumIndices; k += 3)
                    {
                        SXYZW &a = result[comps[inds[k]].gv];
                        SXYZW &b = result[comps[inds[k + 1]].gv];
                        SXYZW &c = result[comps[inds[k + 2]].gv];
                        builder.AddTriangle(a, b, c);
                    }
                    pGPU->PopMatrix(MAT_STACK_TEMPORARY);
                }
            }
            pGPU->PopMatrix(MAT_STACK_TEMPORARY);
        }
    }

    builder.Finish();
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::ReceiveDamage(int nMapObjectID, int nAmount, DWORD dwDamageFlags)
{
    CMapObject *pMO = static_cast<CMapObject *>(GetMapObjectManager()->Find(nMapObjectID));
    if (pMO && pMO->WhoAmI() == HPC_UNIT && pFlyingHP)
        pFlyingHP->ReceiveDamage(nMapObjectID, nAmount, dwDamageFlags);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::AddBloodPrints(float fX, float fY, float fSize, float fFinalSize, int nType,
                                  bool bSmall)
{
    if (theSettings.nTerrainEnableBloodprints)
    {
        ETERRTYPE type = pTerrain->GetTerrainType(fX, fY);
        const STPDesc *pDesc = theTPDatabase.blood[type];
        if (pDesc)
        {
            float fPower = pDesc->clear.fPower;
            int nLifetime = pDesc->clear.nLifetime;
            int nFadeoutTime = pDesc->clear.nFadeoutTime;
            if (weather.GetCurrentWeather() != EW_CLEAR)
            {
                fPower = pDesc->weather.fPower;
                nLifetime = pDesc->weather.nLifetime;
                nFadeoutTime = pDesc->weather.nFadeoutTime;
            }
            if ((fPower > 0) && (nLifetime > 0) && (nFadeoutTime > 0))
            {
                DWORD dwColor = RGBA_MAKE(MINT(dcvSkySun.r * 255.0f), MINT(dcvSkySun.g * 255.0f),
                                          MINT(dcvSkySun.b * 255.0f), 1);
                bloodprints.push_back(CBloodPrints(fX, fY, fSize, fFinalSize, fPower, nLifetime,
                                                   nFadeoutTime, nType, dwColor, bSmall));
            }
        }
    }
}
// вернуть тип крови, в которую вступили
int CWorldClient::AddFootPrints(float fX, float fY, float fSizeX, float fSizeY, float fRotate,
                                int nType, float fDetection, bool bVisible, int nBloodType)
{
    bool bInBlood = false;
    if (theSettings.nTerrainEnableFootprints)
    {
        // first, check for blood
        float fEffectiveRadius = (fSizeX + fSizeY) * 0.5f;
        for (auto pos = bloodprints.begin(); pos != bloodprints.end(); ++pos)
        {
            if (fabs2(pos->GetCenterX() - fX, pos->GetCenterY() - fY) <=
                fabs2(fEffectiveRadius + pos->GetSizeX()))
            {
                if (!pos->IsSmall()) // не оставлять следы после вступления в маленькие капли
                {
                    bInBlood = true;
                    nBloodType = pos->GetBloodType();
                    break;
                }
            }
        }
        // and then add this print
        ETERRTYPE type = pTerrain->GetTerrainType(fX, fY);
        const STPDesc *pDesc = (nBloodType != -1) ? theTPDatabase.blood[type]
                                                  : theTPDatabase.foot[type];
        //
        if (pDesc)
        {
            float fPower = pDesc->clear.fPower;
            int nLifetime = pDesc->clear.nLifetime;
            int nFadeoutTime = pDesc->clear.nFadeoutTime;
            if (weather.GetCurrentWeather() != EW_CLEAR)
            {
                fPower = pDesc->weather.fPower;
                nLifetime = pDesc->weather.nLifetime;
                nFadeoutTime = pDesc->weather.nFadeoutTime;
            }
            if ((fPower > 0) && (nLifetime > 0) && (nFadeoutTime > 0))
                footprints.push_back(CFootPrints(fX, fY, fSizeX, fSizeY, fRotate, fPower, nLifetime,
                                                 nFadeoutTime, nType, fDetection, bVisible,
                                                 nBloodType));
        }
    }
    return bInBlood ? nBloodType : -1;
}
void CWorldClient::AddFirePrints(float fX, float fY, float fSizeX, float fSizeY, float fRotate,
                                 float fPower)
{
    if (theSettings.nTerrainEnableFireprints)
    {
        ETERRTYPE type = pTerrain->GetTerrainType(fX, fY);
        const STPDesc *pDesc = theTPDatabase.fire[type];
        if (pDesc)
        {
            int nLifetime = pDesc->clear.nLifetime;
            int nFadeoutTime = pDesc->clear.nFadeoutTime;
            if (weather.GetCurrentWeather() != EW_CLEAR)
            {
                nLifetime = pDesc->weather.nLifetime;
                nFadeoutTime = pDesc->weather.nFadeoutTime;
            }
            if ((fPower > 0) && (nLifetime > 0) && (nFadeoutTime > 0))
                fireprints.push_back(
                    CFirePrints(fX, fY, fSizeX, fSizeY, fRotate, fPower, nLifetime, nFadeoutTime));
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
class CFootPrintsVisualizeFunctional
{
private:
    float fX, fY;
    float fVision;

public:
    CFootPrintsVisualizeFunctional(const CPlot3D &ptCenter, float _fRadius)
        : fX(ptCenter.x)
        , fY(ptCenter.y)
        , fVision(_fRadius)
    {
    }
    //
    void operator()(CFootPrints &tp)
    {
        float fDist = hypot(tp.GetCenterX() - fX, tp.GetCenterY() - fY);
        if (fDist <= fVision * tp.GetDetection())
            tp.SetVisible(true);
    }
};
void CWorldClient::VisualizePrints(const CPlot3D &ptCenter, float fVision)
{
    CFootPrintsVisualizeFunctional funct(ptCenter, fVision);
    std::for_each(footprints.begin(), footprints.end(), funct);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::RemoveShadowInformationFromObjects()
{
    for (auto pos = objects.begin(); pos != objects.end(); ++pos)
    {
        CObject3DClientSpecific *pSpec = static_cast<CObject3DClientSpecific *>(
            (*pos)->GetObjectSpecific());
        if (!pSpec->IsNeedShadow())
            pSpec->RemoveShadowInformation();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
bool CWorldClient::AddDynamicObject(CMapObject *pMO)
{
    POSITION objpos = 0;
    if (objMap.Lookup(pMO->GetID(), objpos) == FALSE)
    {
        objList.AddTail(pMO);
        objMap[pMO->GetID()] = objList.GetTailPosition();
        return true;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CWorldClient::RemoveDynamicObject(int nID)
{
    POSITION objpos = 0;
    if (objMap.Lookup(nID, objpos) != FALSE)
    {
        objList.RemoveAt(objpos);
        objMap.RemoveAt(nID);
        return true;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
DWORD CWorldClient::FormBoredAckFlagsForUnit(CUnit *pUnit)
{
    DWORD dwAckFlags = 0;
    // form current state flags
    switch (pUnit->GetShowPose())
    {
    case UP_LAY:
        dwAckFlags = ACK_CRAWLING;
        break;
    case UP_CRAWL:
        dwAckFlags = ACK_SNEAKING;
        break;
    case UP_STAND:
        dwAckFlags = ACK_WALKING;
        break;
    case UP_RUN:
        dwAckFlags = ACK_RUNING;
        break;
    }
    // аллод
    if (szCurrentAllodName == "gipat")
        dwAckFlags |= ACK_GIPATH;
    else if (szCurrentAllodName == "ingos")
        dwAckFlags |= ACK_INGOS;
    else if (szCurrentAllodName == "suslanger")
        dwAckFlags |= ACK_SUSLANGER;
    // время суток
    if (IsNightTime())
        dwAckFlags |= ACK_NIGHT;
    else
        dwAckFlags |= ACK_DAY;
    // в пещере или на свежем воздухе ?
    if (IsCaveZone())
        dwAckFlags |= ACK_CAVE;
    else
        dwAckFlags |= ACK_OUTDOORS;
    // погода
    switch (weather.GetCurrentWeather())
    {
    case EW_RAIN:
        dwAckFlags |= ACK_SKYRAIN;
        break;
    case EW_SNOW:
        dwAckFlags |= ACK_SKYSNOW;
        break;
    default:
        dwAckFlags |= ACK_SKYCLEAR;
    }

    return dwAckFlags;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::CollectObjectsInArea(const CPlot3D &ptPos, float fRadius,
                                        std::list<CMapObject *> &result)
{
    std::list<CSmartPtr<CMapObject>> *pObjects = areaObjects.GetFirstList(
        Max(0.0f, Min(pTerrain->GetMaxX() - 1, Max(0.0f, ptPos.x - fRadius))),
        Max(0.0f, Min(pTerrain->GetMaxY() - 1, Max(0.0f, ptPos.y - fRadius))),
        Min(pTerrain->GetMaxX() - 1, Max(0.0f, ptPos.x + fRadius)),
        Min(pTerrain->GetMaxY() - 1, Max(0.0f, ptPos.y + fRadius)));
    while (pObjects)
    {
        for (auto pos = pObjects->begin(); pos != pObjects->end(); ++pos)
        {
            CPlot3D ptPos1 = (*pos)->GetCP() + (*pos)->GetBoundingVolume().ptCenter;
            ptPos1.z = ptPos.z;
            float fXYRadius = hypot((*pos)->GetBoundingVolume().ptMax.x,
                                    (*pos)->GetBoundingVolume().ptMax.y);
            if (fabs2(ptPos1, ptPos) <= square(fRadius + fXYRadius))
                result.push_back(*pos);
        }
        // get next list with objects
        pObjects = areaObjects.GetNextList();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldClient::LoadHeroesMimic()
{
    for (auto pos = objects.begin(); pos != objects.end(); ++pos)
    {
        if ((*pos)->WhoAmI() == HPC_UNIT)
        {
            CUnit *pUnit = static_cast<CUnit *>(*pos);
            if (pUnit)
                static_cast<CUnitClientSpecific *>(pUnit->GetUnitSpecific())
                    ->LoadMouthMimicTexture();
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
