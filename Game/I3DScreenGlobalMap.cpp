#include "StdAfx.h"
#include "GameJournal.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DScreenEcsapeMenu.h"
#include "I3DScreenCamp.h"
#include "I3DScreenObjectives.h"
#include "I3DScreenBase.h"
#include "I3DScreenGlobalMap.h"
#include "I3DScreenMessageBox.h"
#include "I3DScreenSaveLoad.h"
#include "I3DMain.h"
#include "A3Cursor.h"
#include "WorldInterface.h"
#include "ScenarioTracker.h"
#include "StringProcessing.h"
#include "GlobalMap.h"
#include "TextManager.h"
#include "WorldClient.h"
#include "GameMain.h"
#include "MusicSystem.h"
#include "Console.h"
#include "KeyMap.h"
#include "WorldServer.h"
#include "Perlin.h"
#include "PlayerClient.h"
#include "Camera.h"
#include "Renderer.h"
#include "IDebugInfoSystem.h"
#include "dx11/Surface.h"
#include <vector>
/////////////////////////////////////////////////////////////////////////////////////
const int GM_NF = 3;
const int GM_NA = 3;
const float GM_DEFAULT_SCALE = .3f;
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenGlobalMap::StoreSpecific()
{
    interfaceState.Write(0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenGlobalMap::OnActivate()
{
    bNeedAutosave = false;
    if (interfaceState.GetSize())
        interfaceState.SetSize(0);
    else if (theWorld.GetPlayer()->GetPlayer()->prevInterface != IFACE_CAMP)
        bNeedAutosave = true;

    int i, j;
    float x, y, z;
    CI3DSurface *pSurf;
    CI3DFigure *pFig;

    fDist = 10;
    nCenterX = 400;
    nCenterY = 280;

    nRotate = 0;
    fTime = 0;

    SScenarioZone *pZone;
    SScenarioZone *pBase;
    SScenarioAllod *pAllod;

    pZone = theMap.GetZoneByName(IWorldClient.GetCurrentZoneName());
    pAllod = theMap.GetAllodByName(pZone->szAllodName);
    szAllodLocalName = GetAllodLocalName(pAllod->szName);
    cur_loc = GetZonePos(pZone->szName);

    AddTexture(pAllod->szName + "Map");
    AddTexture(pAllod->szName + "Sys");
    AddTexture(pAllod->szName + "MapG");
    AddTexture("saveload");
    AddTexture(CLOUDS_SIZE, CLOUDS_SIZE, TFTID_4444); // clouds texture

    fRotAngle = 0;
    qRot = CQuaternion(CPlot3D(1, 0, 0), FP_PI * .8f);

    // current pos
    pFig = AddFigure(pAllod->szArrowFigure1);
    SetFigurePos(pFig, cur_loc + CPlot3D(0, 0, 2.f));
    pFig->SetTexture(textures[1]);
    pFig->SetScale(1.f);
    pFig->SetOrientation(qRot);

    pFig = AddFigure(pAllod->szArrowFigure2);
    SetFigurePos(pFig, cur_loc);
    pFig->SetTexture(textures[1]);
    pFig->SetScale(.8f);
    pFig->SetOrientation(qRot);

    // bottom of allod
    pFig = AddFigure(pAllod->szBottomFigure);
    pFig->SetTexture(textures[2]);
    pFig->SetRelativeToScreen(nCenterX, nCenterY, fDist);
    pFig->SetOrientation(qRot);

    CI3DPolyArea *pArea;
    pArea = AddArea();
    pArea->MakeRect(737, 537, 785, 585); // rotate left
    pArea->SetToolTip(90102);
    pArea = AddArea();
    pArea->MakeRect(689, 537, 737, 585); // rotate right
    pArea->SetToolTip(90101);
    pArea = AddArea();
    pArea->MakeRect(15, 525, 107, 585); // go camp
    pArea->SetToolTip(90100);

    CString szVarName;
    CStringArray szVarParts;
    CGSVariableSet vars;
    SGlobalMapZone zone;
    SGlobalMapBase base;
    SGlobalMapObj obj;

    nBases = 0;
    nObjectives = 0;
    nZones = 0;

    for (i = 0; i < pAllod->zones.GetSize(); i++)
    {
        pZone = theMap.GetZone(pAllod->zones[i]);
        if (pZone->eType != GM_REALZONE || pZone->szFigureName.IsEmpty())
            continue;
        zones.Add(zone);
        szVarName.Format("z.%s", pZone->szName);
        zones[nZones].iChildBase = -1;
        zones[nZones].state = (int)IWorldClient.GSGetVar(szVarName);
        zones[nZones].szName = pZone->szName;
        zones[nZones].szLocalName = GetZoneLocalName(pZone->szName);
        pFig = AddFigure(pZone->szFigureName);
        pFig->SetRelativeToScreen(nCenterX, nCenterY, fDist);
        pFig->SetOrientation(qRot);
        nZones++;
    }

    for (i = 0; i < nZones; i++)
    {
        szVarName.Format("q.%s", zones[i].szName);
        IWorldClient.GSGetVars(szVarName, vars);
        CGSVariableSet::const_iterator p;
        for (p = vars.begin(); p != vars.end(); ++p)
            if (p->second == SCN_OBJECTIVE_AVAILABLE)
            {
                NStrProc::SplitString(p->first.c_str(), szVarParts, '.');
                if (szVarParts.GetSize() == 3)
                {
                    objs.Add(obj);
                    objs[nObjectives].szName = szVarParts[2];
                    objs[nObjectives].iParentZone = i;
                    objs[nObjectives].szLocalName = GetObjLocalName(szVarParts[2]);
                    objs[nObjectives].loc = GetObjPos(szVarParts[2]);
                    pFig = AddFigure(pAllod->szObjFigure);
                    SetFigurePos(pFig, objs[nObjectives].loc);
                    pFig->SetTexture(textures[1]);
                    pFig->SetScale(GM_DEFAULT_SCALE);
                    pFig->SetOrientation(qRot);
                    pFig->SetAmbient(1, 1, 1);
                    pFig->GetRelativeToScreen(x, y, z);
                    // AddArea()->MakeRect(int(x-10),int(y-40),int(x+10),int(y));

                    nObjectives++;
                }
            }
    }

    for (i = 0; i < nZones; i++)
    {
        pZone = theMap.GetZoneByName(zones[i].szName);
        for (j = 0; j < pZone->exits.GetSize(); j++)
        {
            pBase = theMap.GetZoneByName(pZone->exits[j].szZone);
            if (!pBase)
                continue;
            szVarName.Format("z.%s", pBase->szName);
            if (pBase->eType == GM_BASE && IWorldClient.GSGetVar(szVarName) == SCN_ZONE_EXPLORED &&
                !pBase->szFigureName.IsEmpty())
            {
                if (zones[i].state == SCN_ZONE_EXPLORED)
                    zones[i].iChildBase = nBases;
                bases.Add(base);
                bases[nBases].iParentZone = i;
                bases[nBases].szName = pBase->szName;
                bases[nBases].szLocalName = GetZoneLocalName(pBase->szName);
                bases[nBases].loc = GetZonePos(pBase->szName);
                pFig = AddFigure(pBase->szFigureName);
                SetFigurePos(pFig, bases[nBases].loc);
                pFig->SetTexture(textures[1]);
                pFig->SetScale(GM_DEFAULT_SCALE);
                pFig->SetOrientation(qRot);
                pFig->SetAmbient(1, 1, 1);

                pFig->GetRelativeToScreen(x, y, z);
                AddArea()->MakeRect(int(x - 20), int(y - 25), int(x + 20), int(y + 10));

                nBases++;
            }
        }
    }

    nTotal = nZones + nObjectives + nBases;
    // clouds - layer 1
    pFig = AddFigure();
    pFig->MakeBar(25, 100, 700, 0, 100, 600, 0, 256, 256, 0);
    pFig->SetTexture(textures[4]);
    pFig->SetOrientation(qRot);
    SetFigurePos(pFig, CPlot3D(0, 0, -.5f));
    //	pFig->SetDefaultColor( RGBA_MAKE( 0, 80, 255, 160 ) );
    //	pFig->SetDefaultColor( RGBA_MAKE( 175, 195, 218, 160 ) );
    pFig->SetDefaultColor(RGBA_MAKE(130, 0, 200, 160));
    pFig->UseDefaultColors(TRUE);
    pFig->SetVShift(.3f);

    // clouds - layer 2
    pFig = AddFigure();
    pFig->MakeBar(25, 100, 700, 0, 100, 600);
    pFig->SetTexture(textures[4]);
    pFig->SetOrientation(qRot);
    SetFigurePos(pFig, CPlot3D(0, 0, .7f));
    //	pFig->SetDefaultColor( RGBA_MAKE( 0, 160, 255, 240 ) );
    //	pFig->SetDefaultColor( RGBA_MAKE( 201, 215, 230, 240 ) );
    pFig->SetDefaultColor(RGBA_MAKE(180, 0, 255, 240));
    pFig->UseDefaultColors(TRUE);
    pFig->SetVShift(.6f);

    // clouds - layer 3
    pFig = AddFigure();
    pFig->MakeBar(25, 100, 700, 0, 100, 600);
    pFig->SetTexture(textures[4]);
    pFig->SetOrientation(qRot);
    SetFigurePos(pFig, CPlot3D(0, 0, 2));
    pFig->SetDefaultColor(RGBA_MAKE(255, 255, 255, 75));
    pFig->UseDefaultColors(TRUE);
    pFig->Show(FALSE);

    // set background color
    //	GetRenderer()->SetBGColor( RGBA_MAKE( 0, 0, 90, 255 ) );
    //	GetRenderer()->SetBGColor( RGBA_MAKE( 9, 44, 83, 255 ) );
    GetRenderer()->SetBGColor(RGBA_MAKE(60, 0, 90, 255));

    // hit test dds
    // DX11 copies the whole back buffer into this surface. These dimensions
    // are already physical pixels; do not apply the virtual 800x600 transform.
    pHitTestDDS = std::make_unique<CI3DSurface>(pI3D, 0, 0, pI3D->GetWidth(), pI3D->GetHeight(),
                                                EBlitStyleFlag(BS_VISIBLE | BS_COLOR_KEY), false);
    // pHitTestDDS->SetRect(CRect(0, 0, 800, 600));

    // info about quests
    pFig = AddFigure();
    pFig->SetTexture(0);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));
    pFig->UseDefaultColors(TRUE);
    pFig->Show(FALSE);

    pFig = AddFigure();
    pFig->SetTexture(textures[3]);
    pFig->Show(FALSE);

    AddSurface(pSurf = dbgnew CI3DSurface(pI3D, 400, 20, 780, 300));
    pSurf->Show(FALSE);

    // allod info
    pFig = AddFigure();
    pFig->MakeBar(3, 100, 20, 20, 170, 70);
    pFig->SetTexture(0);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));
    pFig->UseDefaultColors(TRUE);

    pFig = AddFigure();
    pFig->MakeBarFrame(3, 100, 15, 15, 175, 75, 5, 2, 254, 180, 249);
    pFig->SetTexture(textures[3]);

    int nMinutes = static_cast<int>(IWorldClient.GSGetVar(szPartyGlobalTimeVarName) * 60);
    int nDay = nMinutes / 1440;
    nMinutes -= nDay * 1440;
    int nHour = nMinutes / 60;
    nMinutes -= nHour * 60;
    int nMinute = nMinutes;
    CString szTime, szDayString;
    IWorld.GetTextManager()->GetSystemString("save_day", szDayString);
    szTime.Format("%s  %d,  %d:%02d", szDayString, nDay + 1, nHour, nMinute);

    AddSurface(pSurf = dbgnew CI3DSurface(pI3D, 20, 20, 170, 70));
#ifndef NOGETDC
    pSurf->GetDC();
    pSurf->DrawText(szAllodLocalName, CRect(0, 5, 150, 25), 2, RGB(228, 215, 167),
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    pSurf->DrawText(szTime, CRect(0, 25, 150, 45), 2, RGB(228, 215, 167),
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    pSurf->ReleaseDC();
#endif // NOGETDC

    // go camp
    pFig = AddFigure();
    pFig->MakeBar(3, 100, 15, 525, 107, 585, 2, 151, 94, 211);
    pFig->SetTexture(textures[3]);

    nCampState = IWorldClient.GSGetVar("i.nocamp");
    if (nCampState == 1)
        pFig->Show(FALSE);

    // left & right rotate arrows
    pFig = AddFigure();
    pFig->MakeBar(3, 100, 689, 537, 737, 585, 81, 55, 129, 103);
    pFig->SetTexture(textures[3]);

    pFig = AddFigure();
    pFig->MakeBar(3, 100, 737, 537, 785, 585, 134, 55, 182, 103);
    pFig->SetTexture(textures[3]);

    bNeedMergeBaseWithZone = false;

    fZoneR = 0;
    fZoneG = 1;
    fZoneB = 0;

    // CRAP{
    if (pAllod->szName == "ingos")
    {
        fZoneR = 95 / 256.f;
        fZoneG = 130 / 256.f;
        fZoneB = 160 / 256.f;
    }
    else if (pAllod->szName == "suslanger")
    {
        fZoneR = 100 / 256.f;
        fZoneG = 50 / 256.f;
        fZoneB = 22 / 256.f;
    }

    if (pAllod->szName == "gipat")
        bNeedMergeBaseWithZone = true;
    // CRAP}

    bGenerateHitTestDDS = TRUE;
    SetZonesColors();

    bRButDown = false;

    IWorldServer.SetWeather(EW_CLEAR, 0);
    theWorld.GetMusicSystem()->SetMuteMode();

    // debug only
    // AddSurface(pSurf = dbgnew CI3DSurface(pI3D,600,200,800,400) );

    pCloudGen = dbgnew CCloudsGenerator();
    pCloudGen->Init();

    ApplyCloudsParams();
    OnIdle();
    hSnd = pI3D->BeginFX("buttons\\globalmap\\wind.wav", TRUE);

    if (bNeedAutosave)
    {
        if (theSettings.nDoAutosave)
            CI3DScreenSaveLoad::QuickSave("saveb", true);
        bNeedAutosave = false;
    }
    pI3D->CheckTutorial();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenGlobalMap::OnDeactivate()
{
    if (pCloudGen)
        delete pCloudGen;
    pCloudGen = 0;
    pHitTestDDS = 0;
    pI3D->StopFX(hSnd);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenGlobalMap::OnExitFromModal(DWORD dwRetValue)
{
    CPoint point = theCursor.GetPos();

    nRotate = 0;
    bRButDown = false;
    bGenerateHitTestDDS = TRUE;

    OnMouseMove(0, point);

    switch (dwRetValue)
    {
    case 0x00010001:
    case 0x00010002:
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
LPCTSTR CI3DScreenGlobalMap::GetTutorialScreenName()
{
    if (nCampState == 1)
        return "global_map_nocamp";
    else
        return "global_map";
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenGlobalMap::ApplyCloudsParams()
{
    DWORD dwBackgroundColor = RGBA_MAKE(0, 0, 10, 25);
    DWORD dwFigureColor1 = RGBA_MAKE(155, 0, 225, 100);
    DWORD dwFigureColor2 = RGBA_MAKE(155, 0, 125, 110);
    DWORD dwFigureColor3 = RGBA_MAKE(105, 10, 195, 185);
    int cloudDensity = 119;
    int cloudSharpness = 248;

    GetRenderer()->SetBGColor(dwBackgroundColor);
    figures[GM_NF + nTotal]->SetDefaultColor(dwFigureColor1);
    figures[GM_NF + nTotal + 1]->SetDefaultColor(dwFigureColor2);
    figures[GM_NF + nTotal + 2]->SetDefaultColor(dwFigureColor3);
    pCloudGen->SetParams(cloudDensity, cloudSharpness, 2);
}

/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DScreenGlobalMap::Draw(CRenderer *pRend)
{
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, TRUE);
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHATESTENABLE, FALSE);
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_SRCBLEND, D3DBLEND_SRCALPHA);
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_ALPHAREF, 1);
    CI3DScreen::Draw(pRend);
    // pHitTestDDS->Draw();
    // DrawWithCamera(pRend);
    return TRUE;
}

float g_rotan = 0;
BOOL CI3DScreenGlobalMap::DrawWithCamera(CRenderer *pRend)
{
    g_rotan += 0.01;
    g_rotan = static_cast<float>(fmod(g_rotan, FP_2PI));

    CPlot3D islandPos = { 0, 0, 10 };
    const CPlot3D cameraUp = { 0, -1, 0 };

    // Rotate camera around target point while looking at it
    // needs to emulate object rotating FP_PI * 0.8f along X, then fRotAngle along Z

    CQuaternion rotationX(CPlot3D(1, 0, 0), FP_PI * 0.2f);
    CQuaternion rotationY(CPlot3D(0, 0, 1), g_rotan);
    CQuaternion rot = rotationY * rotationX;
    CPlot3D rotatedPos = rot.Rotate(
        islandPos); // rotate point along Y axis, getting +-i.z, 0, +-i.z
    CPlot3D newPos = rotatedPos + islandPos;
    // newPos = { 0, 0, 0 };

    auto pGPU = theRenderer.GetGeometryProcessor();
    SHMatrix camMatrix; // look at island from newPos
    pGPU->CreateCameraMatrix(camMatrix, newPos, islandPos, cameraUp);

    // Create camera matrix from camera, using it's pos and orientation
    CPlot3D posZero;
    CCamera cam;
    cam.SetCP(posZero);
    CQuaternion lookZ(CPlot3D(0, 0, 1), 0);
    cam.SetOrientation(lookZ);
    SHMatrix camMatrix2;
    pGPU->CreateCameraMatrix(camMatrix2, &cam);

    // Apply one of cam matrices
    pGPU->PushMatrix(camMatrix, MAT_STACK_PROJSPACE);
    CI3DScreen::Draw(pRend);
    pGPU->PopMatrix(MAT_STACK_PROJSPACE);

    // auto pDBI = GetDebugInfoSystem();

    // CString szPos;
    // cameraPos = newPos;
    // szPos.Format("%.1f %.1f %.1f", cameraPos.x, cameraPos.y, cameraPos.z);
    // pDBI->Set("cameraPos", szPos);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenGlobalMap::OnIdle()
{
    CI3DScreen::OnIdle();
    int i;
    float x, y, z;
    //
    if (bRButDown && !nRotate)
    {
        CPoint ptCursorPos;
        GetCursorPos(&ptCursorPos);
        OnMouseMove(0, ptCursorPos);
    }
    if (nRotate || bRButDown)
    {
        fRotAngle += nRotate * FP_PI * GetFrameTime() / 4.f;

        fRotAngle = static_cast<float>(fmod(fRotAngle, FP_2PI));

        qRot = CQuaternion(CPlot3D(1, 0, 0), FP_PI * .8f) *
               CQuaternion(fRotAngle, CPlot3D(0, 0, 1));
        qRot.MinimizeRotationAngle();

        SetFigurePos(figures[1], cur_loc);
        figures[1]->SetOrientation(qRot);

        figures[2]->SetOrientation(qRot);

        figures[GM_NF + nTotal]->SetOrientation(qRot);
        figures[GM_NF + nTotal + 1]->SetOrientation(qRot);
        figures[GM_NF + nTotal + 2]->SetOrientation(qRot);

        for (i = 0; i < nZones; i++)
            figures[GM_NF + i]->SetOrientation(qRot);

        for (i = 0; i < nObjectives; i++)
        {
            SetFigurePos(figures[GM_NF + i + nZones], objs[i].loc);
            figures[GM_NF + i + nZones]->SetOrientation(qRot);
            // figures[GM_NF + i + nZones]->GetRelativeToScreen(x, y, z);
        }

        for (i = 0; i < nBases; i++)
        {
            SetFigurePos(figures[GM_NF + i + nZones + nObjectives], bases[i].loc);
            figures[GM_NF + i + nZones + nObjectives]->SetOrientation(qRot);
            figures[GM_NF + i + nZones + nObjectives]->GetRelativeToScreen(x, y, z);
            areas[GM_NA + i]->MakeRect(int(x - 20), int(y - 25), int(x + 20), int(y + 10));
        }
    }

    fTime += GetFrameTime();
    float fArrowHgt = .5f - (float)cos(fTime * 5) * .2f;
    SetFigurePos(figures[0], cur_loc + CPlot3D(0, 0, fArrowHgt));

    float time;
    time = fTime / 100;
    figures[GM_NF + nTotal]->SetUShift(time - (int)time);

    time = fTime / 60;
    figures[GM_NF + nTotal + 1]->SetUShift(time - (int)time);

    time = fTime / 30;
    figures[GM_NF + nTotal + 2]->SetUShift(time - (int)time);

    if (bGenerateHitTestDDS)
        GenerateHitTestDDS();
    bGenerateHitTestDDS = FALSE;

    if (textures.GetSize() > 4)
    {
        CDXGISurface *pSurface = textures[4]->GetSurface();

        DWORD pitch = pSurface->GetWidth() * 2;
        std::unique_ptr<WORD[]> buffer = std::make_unique<WORD[]>(pSurface->GetWidth() *
                                                                  pSurface->GetHeight());

        pCloudGen->GeneratePerlin(buffer.get(), GetFrameTime());

        D3D11_SUBRESOURCE_DATA srd;
        srd.pSysMem = buffer.get();
        srd.SysMemPitch = pitch;

        pSurface->UpdateTexture(0, nullptr, &srd);
    }
    if (bRButDown)
        theCursor.SetMode(CT_CAMERA);
    else
        theCursor.SetMode(CT_DEFAULT);
}
int CI3DScreenGlobalMap::HitTestFiguresForCurrentZone(const CPoint point)
{
    // works, but doesn't account for screen rotation...
    const CPlot3D cameraPos = { 0, 0, 0 };
    const CPlot3D cameraDir = { 0, 0, 1 };
    const CPlot3D cameraUp = { 0, 1, 0 };
    SHMatrix camMatrix;

    auto pGPU = theRenderer.GetGeometryProcessor();
    pGPU->CreateCameraMatrix(camMatrix, cameraPos, cameraDir, cameraUp);
    pGPU->PopMatrix(MAT_STACK_CAMERA);
    pGPU->PushMatrix(camMatrix, MAT_STACK_CAMERA);

    SRayPicker picker(point, cameraPos);

    for (int zone_idx = 0; zone_idx < nZones; ++zone_idx)
    {
        const int zone_figure_idx = GM_NA + zone_idx;
        const SBoundingVolume &bv = figures[zone_figure_idx]->GetBV();
        const CPlot3D &pos = figures[zone_figure_idx]->GetCP() + bv.ptCenter;
        SBoundingVolume bv2 = bv;
        // bv2.dwFlags |= BV_SPHERE;
        bv2.dwFlags |= BV_BOX;
        if (picker.CheckBV(pos, bv2))
            return zone_idx;
    }
    return -1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenGlobalMap::OnLButtonDown(UINT nFlags, CPoint point)
{
    GameJournal::Source journalSource("global_map");
    int ht;
    int nCurZone;
    DWORD color;

    if (bRButDown)
        return;

    CString szTmp;
    ht = HitTest(point);
    JournalHit(ht);
    switch (ht)
    {
    case 0:
        nRotate = -1;
        pI3D->BeginFX("buttons\\globalmap\\move.wav");
        return;
    case 1:
        nRotate = 1;
        pI3D->BeginFX("buttons\\globalmap\\move.wav");
        return;
    case 2:
        if (nCampState != 1)
        {
            pI3D->BeginFX("buttons\\globalmap\\transit.wav");
            IWorldServer.SetConstructorNumber(0);
            IWorldClient.GoCamp();
        }
        return;
    }
    if (ht >= GM_NA)
    {
        ht -= GM_NA;
        pI3D->BeginFX("buttons\\globalmap\\ok.wav");
        szDestZoneName = bases[ht].szName;
        int nE = FindPathes();
        if (nE < 0)
            return;
        if (!CheckZonePresence(szDestZoneName))
            return;
        ASSERT(entrances.find(0) != entrances.end());
        // add time
        IWorldServer.AddPlayersGlobalTime(entrances[0]);
        IWorldConnector.ClientGoZone(bases[ht].szName, 0);
        return;
    }
    // after all checks we try to detect, which zone is under cursor
    color = pHitTestDDS->GetPixel(point.x, point.y);
    nCurZone = int((color >> 4) & 0x0000000f) - 1;

    if (nCurZone >= 0 && nCurZone < nZones)
    {
        GameJournal::Event("ZONE", "map_choice", "index=%d zone=%s state=%d", nCurZone, GameJournal::Quote(zones[nCurZone].szName).c_str(), zones[nCurZone].state);
        // go to zone
        if (zones[nCurZone].state != SCN_ZONE_UNKNOWN)
        {
            pI3D->BeginFX("buttons\\globalmap\\ok.wav");
            int nCurBase = zones[nCurZone].iChildBase;
            if (bNeedMergeBaseWithZone && nCurBase >= 0)
            {
                szDestZoneName = bases[nCurBase].szName;
                int nE = FindPathes();
                if (nE < 0)
                    return;
                if (!CheckZonePresence(szDestZoneName))
                    return;
                ASSERT(entrances.find(0) != entrances.end());
                // add time
                IWorldServer.AddPlayersGlobalTime(entrances[0]);
                IWorldConnector.ClientGoZone(bases[nCurBase].szName, 0);
            }
            else
            {
                szDestZoneName = zones[nCurZone].szName;
                int nE = FindPathes();
                if (!CheckZonePresence(szDestZoneName))
                    return;
                CI3DScreenObjectives *pScr = dbgnew CI3DScreenObjectives(pI3D);
                pScr->SetParams(szDestZoneName, &entrances, TRUE);
                pI3D->DoModal(pScr);
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenGlobalMap::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (nRotate != 0)
        bGenerateHitTestDDS = TRUE;
    nRotate = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenGlobalMap::ResetInputState()
{
    bRButDown = false;
    bGenerateHitTestDDS = TRUE;
    theCursor.ResetIncrement();
    CPoint pt = theCursor.GetPos();
    theCursor.LockPos(false);
    SetCursorPos(pt.x, pt.y);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenGlobalMap::OnRButtonDown(UINT nFlags, CPoint point)
{
    bRButDown = true;
    nRotate = 0;
    ptBegin = point;
    pI3D->ScreenToVirtual(ptBegin);
    fRotBegin = fRotAngle;
    theCursor.ResetIncrement();
    theCursor.LockPos(true);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenGlobalMap::OnRButtonUp(UINT nFlags, CPoint point)
{
    bRButDown = false;
    bGenerateHitTestDDS = TRUE;
    theCursor.ResetIncrement();
    CPoint pt = theCursor.GetPos();
    theCursor.LockPos(false);
    SetCursorPos(pt.x, pt.y);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenGlobalMap::OnMouseMove(UINT nFlags, CPoint point)
{
    int ht, nCurZone, nCurObj;
    CI3DFigure *pFig;

    if (bRButDown)
    {
        CPoint ptIncrement = theCursor.GetIncrement();
        NConsole::graphics << ptIncrement.x << ':' << ptIncrement.y << endl;
        pI3D->ScreenToVirtualLocal(ptIncrement);
        fRotAngle += static_cast<float>(ptIncrement.x) / 300.f;

        fRotAngle = static_cast<float>(fmod(fRotAngle, FP_2PI));
        qRot = CQuaternion(CPlot3D(1, 0, 0), FP_PI * .8f) *
               CQuaternion(fRotAngle, CPlot3D(0, 0, 1));
        //		qRot.MinimizeRotationAngle();
        theCursor.ResetIncrement();

        return;
    }

    bool bIsBase = false;
    nCurZone = -1;
    nCurObj = -1;

    ht = HitTest(point);
    // CLOUDS:
    if (ht >= GM_NA + nBases)
        return;

    if (nRotate == 0)
    {
        if (ht >= GM_NA)
        {
            nCurZone = ht - GM_NA;
            bIsBase = true;
        }
        if (ht < 0)
        {
            // hittest for zone

            DWORD color = pHitTestDDS->GetPixel(point.x, point.y);
            nCurZone = int((color >> 4) & 0x0000000f) - 1;
            if (nCurZone >= nZones)
                nCurZone = -1;
            if (bNeedMergeBaseWithZone && nCurZone >= 0 && zones[nCurZone].iChildBase >= 0)
            {
                bIsBase = true;
                nCurZone = zones[nCurZone].iChildBase;
            }
        }
    }

    SetZonesColors(nCurZone, bIsBase);

    if (nCurZone == -1)
    {
        surfaces[0]->Show(FALSE);
        figures[GM_NF + nTotal + 3]->Show(FALSE);
        figures[GM_NF + nTotal + 4]->Show(FALSE);
        szCurZoneName.Empty();
    }
    else
    {
        if (!bIsBase && zones[nCurZone].state == SCN_ZONE_UNKNOWN)
        {
            surfaces[0]->Show(FALSE);
            figures[GM_NF + nTotal + 3]->Show(FALSE);
            figures[GM_NF + nTotal + 4]->Show(FALSE);
            szCurZoneName.Empty();
        }
        else
        {
            CString szZoneName;
            // non-local
            if (bIsBase)
                szZoneName = bases[nCurZone].szName;
            else
                szZoneName = zones[nCurZone].szName;
            if (szCurZoneName != szZoneName)
                pI3D->BeginFX("buttons\\globalmap\\zone.wav");
            szCurZoneName = szZoneName;

            if (bIsBase)
                szZoneName = bases[nCurZone].szLocalName;
            else
                szZoneName = zones[nCurZone].szLocalName;
            // COLORREF cr;
            int j = 0;
            surfaces[0]->Show(TRUE);
#ifndef NOGETDC
            surfaces[0]->GetDC();
            surfaces[0]->Clear();
            surfaces[0]->DrawText(szZoneName, CRect(10, 10, 300, 50), 2, RGB(228, 215, 167),
                                  DT_LEFT | DT_VCENTER);
            if (!bIsBase)
                for (int i = 0; i < objs.GetSize(); i++)
                {
                    if (objs[i].iParentZone != nCurZone)
                        continue;
                    surfaces[0]->DrawText(objs[i].szLocalName,
                                          CRect(20, 40 + j * 20, 350, 60 + j * 20), 2,
                                          RGB(228, 215, 167), DT_LEFT | DT_VCENTER);
                    j++;
                }
            surfaces[0]->ReleaseDC();
#endif // NOGETDC

            pFig = figures[GM_NF + nTotal + 3];
            pFig->MakeBar(3, 100, 400, 20, 780, float(75 + j * 20));
            pFig->Show(TRUE);
            pFig = figures[GM_NF + nTotal + 4];
            pFig->MakeBarFrame(3, 100, 395, 15, 785, float(80 + j * 20), 5, 2, 254, 180, 249);
            pFig->Show(TRUE);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenGlobalMap::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    CI3DScreen::OnKeyDown(nChar, nRepCnt, nFlags);
    switch (nChar)
    {
    case VK_ESCAPE:
        pI3D->DoModal(dbgnew CI3DScreenEscapeMenu(pI3D));
        SetProcessed();
        break;
#ifndef NODEBUGINFO
    case '1':
        IWorldServer.SetConstructorNumber(1);
        IWorldClient.GoCamp();
        SetProcessed();
        break;
    case '2':
        IWorldServer.SetConstructorNumber(2);
        IWorldClient.GoCamp();
        SetProcessed();
        break;
    case '3':
        IWorldServer.SetConstructorNumber(3);
        IWorldClient.GoCamp();
        SetProcessed();
        break;
    case '4':
        IWorldServer.SetConstructorNumber(4);
        IWorldClient.GoCamp();
        SetProcessed();
        break;
    case '5':
        IWorldServer.SetConstructorNumber(5);
        IWorldClient.GoCamp();
        SetProcessed();
        break;
#endif // NODEBUGINFO
    }

    if (GetProcessed())
        return;

    switch (NKeyMap::keyMap[nFlags & 0x1ff])
    {
    case KA_QSAVE:
        CI3DScreenSaveLoad::QuickSave("saveq");
        break;
    case KA_QLOAD:
        CI3DScreenSaveLoad::QuickLoad("saveq");
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenGlobalMap::GenerateHitTestDDS()
{
    int i, j;
    if (!pHitTestDDS)
        return;
    CI3DFigure *pFig;
    // set ambients
    for (i = 0; i < nZones; i++)
    {
        j = (i + 1) * 16 + 8;
        pFig = figures[GM_NF + i];
        pFig->SetTexture(NULL);
        pFig->SetDefaultColor(RGBA_MAKE(j, j, j, 255));
        pFig->UseDefaultColors(TRUE);
    }
    // render to back buffer
    surfaces[1]->Show(FALSE);
    surfaces[0]->Show(FALSE);

    for (i = 0; i < GM_NF; i++)
        figures[i]->Show(FALSE);
    for (i = GM_NF + nZones; i < figures.GetSize(); i++)
        figures[i]->Show(FALSE);

    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_SHADEMODE, D3DSHADE_FLAT);
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_DITHERENABLE, FALSE);

    DWORD dwBG = GetRenderer()->GetBGColor();
    GetRenderer()->SetBGColor(0);

    NGame::DrawFrame(false, false);

    GetRenderer()->SetBGColor(dwBG);

    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_SHADEMODE, D3DSHADE_GOURAUD);
    pI3D->GetRenderer()->SetRenderState(D3DRENDERSTATE_DITHERENABLE, TRUE);

    surfaces[1]->Show(TRUE);

    for (i = 0; i < GM_NF; i++)
        figures[i]->Show(TRUE);
    for (i = GM_NF + nZones; i < figures.GetSize(); i++)
        figures[i]->Show(TRUE);
    figures[GM_NF + nTotal + 3]->Show(FALSE);
    figures[GM_NF + nTotal + 4]->Show(FALSE);

    // blit to surface
    pHitTestDDS->GetFromScreen();
    const HRESULT captured = pHitTestDDS->GetPixelReadbackStatus();
    if (FAILED(captured))
        GameJournal::Event("RENDER", "global_map_mask_failed", "result=%08X", captured);

    SetZonesColors();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenGlobalMap::SetZonesColors(int nCurZone, bool bIsBase)
{
    int nLightZone = -1;
    int nLightBase = -1;
    if (bIsBase)
    {
        nLightBase = nCurZone;
        if (bNeedMergeBaseWithZone && nCurZone >= 0 && bases[nCurZone].iParentZone >= 0)
            nLightZone = bases[nCurZone].iParentZone;
    }
    else
    {
        nLightZone = nCurZone;
        if (bNeedMergeBaseWithZone && nCurZone >= 0 && zones[nCurZone].iChildBase >= 0)
            nLightBase = zones[nCurZone].iChildBase;
    }
    for (int i = 0; i < nZones; i++)
    {
        CI3DFigure *pFig = figures[GM_NF + i];
        switch (zones[i].state)
        {
        case SCN_ZONE_UNKNOWN:
            pFig->SetTexture(textures[2]);
            pFig->UseDefaultColors(FALSE);
            break;
        case SCN_ZONE_UNEXPLORED:
            pFig->SetTexture(textures[0]);
            if (i == nLightZone)
                pFig->SetAmbient(fZoneR, fZoneG, fZoneB);
            else
                pFig->SetAmbient(1, 1, 1);
            pFig->UseDefaultColors(FALSE);
            break;
        case SCN_ZONE_EXPLORED:
            pFig->SetTexture(textures[0]);
            if (i == nLightZone)
                pFig->SetAmbient(.5f * fZoneR, .5f * fZoneG, .5f * fZoneB);
            else
                pFig->SetAmbient(.5f, .5f, .5f);
            pFig->UseDefaultColors(FALSE);
            break;
        }
    }
    for (int i = 0; i < nBases; i++)
    {
        CI3DFigure *pFig = figures[GM_NF + i + nZones + nObjectives];
        if (i == nLightBase)
            pFig->SetAmbient(fZoneR, fZoneG, fZoneB);
        else
            pFig->SetAmbient(1, 1, 1);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenGlobalMap::SetFigurePos(CI3DFigure *pFig, CPlot3D pos)
{
    pFig->SetCP(CPlot3D((nCenterX / 400.f - 1.f) * VIEWING_ANGLE_TAN * fDist,
                        (nCenterY / 400.f - .75f) * VIEWING_ANGLE_TAN * fDist, fDist) +
                qRot.Rotate(pos));
}
/////////////////////////////////////////////////////////////////////////////////////
CPlot3D CI3DScreenGlobalMap::GetObjPos(LPCTSTR pszName)
{
    CString szTmp;
    CStringArray szTmpArray;
    CPlot3D res;
    CTextManager *pTxtMng = theWorld.GetTextManager();
    pTxtMng->GetText("map.txt");
    szTmp.Format("#quest %s", pszName);
    if (pTxtMng->FindNextCommand(szTmp))
    {
        pTxtMng->SkipLine();
        if (pTxtMng->GetStringArray(szTmpArray))
        {
            while (szTmpArray.GetSize() < 3)
                szTmpArray.Add("0");
            res.x = (float)atof(szTmpArray[0]);
            res.y = (float)atof(szTmpArray[1]);
            res.z = (float)atof(szTmpArray[2]);
        }
    }
    pTxtMng->CloseText();
    return res;
}
/////////////////////////////////////////////////////////////////////////////////////
CPlot3D CI3DScreenGlobalMap::GetZonePos(LPCTSTR pszName)
{
    CString szTmp;
    CStringArray szTmpArray;
    CPlot3D res;
    CTextManager *pTxtMng = theWorld.GetTextManager();
    pTxtMng->GetText("map.txt");
    szTmp.Format("#zone %s", pszName);
    if (pTxtMng->FindNextCommand(szTmp) && pTxtMng->FindNextCommand("#position"))
    {
        pTxtMng->SkipLine();
        if (pTxtMng->GetStringArray(szTmpArray))
        {
            while (szTmpArray.GetSize() < 3)
                szTmpArray.Add("0");
            res.x = (float)atof(szTmpArray[0]);
            res.y = (float)atof(szTmpArray[1]);
            res.z = (float)atof(szTmpArray[2]);
        }
    }
    pTxtMng->CloseText();
    return res;
}
/////////////////////////////////////////////////////////////////////////////////////
CString CI3DScreenGlobalMap::GetObjLocalName(LPCTSTR pszName)
{
    CString szRes;
    CTextManager *pTxtMng = IWorld.GetTextManager();
    if (pTxtMng->GetText(CString("quest ") + pszName))
        pTxtMng->GetLine(szRes);
    return szRes;
}
/////////////////////////////////////////////////////////////////////////////////////
CString CI3DScreenGlobalMap::GetZoneLocalName(LPCTSTR pszName)
{
    CString szRes;
    CTextManager *pTxtMng = IWorld.GetTextManager();
    if (pTxtMng->GetText(CString("zone ") + pszName))
        pTxtMng->GetLine(szRes);
    return szRes;
}
/////////////////////////////////////////////////////////////////////////////////////
CString CI3DScreenGlobalMap::GetAllodLocalName(LPCTSTR pszName)
{
    CString szRes;
    CTextManager *pTxtMng = IWorld.GetTextManager();
    if (pTxtMng->GetText(CString("allod ") + pszName))
        pTxtMng->GetLine(szRes);
    return szRes;
}
/////////////////////////////////////////////////////////////////////////////////////
int CI3DScreenGlobalMap::FindPathes()
{
    fPathLen = 0;
    entrances.clear();
    bPathFound = false;
    bIsSecondaryPath = false;
    RecurseFindPath(theMap.GetZoneByName(IWorldClient.GetCurrentZoneName()));
    if (bPathFound)
    {
        ASSERT(entrances.size());
        return 0;
    }
    else
        return -1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenGlobalMap::RecurseFindPath(SScenarioZone *pEndZone)
{
    CString szVarName;
    for (int i = 0; i < pEndZone->exits.GetSize(); i++)
    {
        bool bNeedRestore = false;
        SScenarioZone *pZ = theMap.GetZoneByName(pEndZone->exits[i].szZone);
        if (!pZ)
            continue;
        float passtime = pEndZone->exits[i].fPassTime;
        if (pZ->szName == szDestZoneName)
        {
            // next path found
            bPathFound = true;
            int nE = pEndZone->exits[i].nZoneEntrance; // number of entrance
            float fCheatLen = path.size() > 0 ? fPathLen : -1.f;
            if (entrances.find(nE) != entrances.end()) // found prev way
            {
                if (fPathLen < entrances[nE])
                    entrances[nE] = fCheatLen;
            }
            else
                entrances[nE] = fCheatLen; // first way
            if (bIsSecondaryPath)          // second time we have this zone reached
                continue;
            else
            {
                bNeedRestore = true;
                bIsSecondaryPath = true;
            }
        }
        // find in chain
        bool bIsInChain = false;
        for (CHAIN::reverse_iterator it = path.rbegin(); it != path.rend(); ++it)
            if (it->first == pZ)
            {
                bIsInChain = true;
                break;
            }
        if (bIsInChain)
            continue;
        // recurse
        bool bNeedRecurse = false;
        switch (pZ->eType)
        {
        case GM_REALZONE:
            szVarName.Format("z.%s", pZ->szName);
            if ((int)IWorldClient.GSGetVar(szVarName) == SCN_ZONE_EXPLORED)
                bNeedRecurse = true;
            break;
        case GM_BETWEENZONES:
            szVarName.Format("z.%s", pZ->szName);
            if ((int)IWorldClient.GSGetVar(szVarName) != SCN_ZONE_UNEXPLORED)
                bNeedRecurse = true;
            break;
        case GM_BASE:
            szVarName.Format("z.%s", pZ->szName);
            if ((int)IWorldClient.GSGetVar(szVarName) != SCN_ZONE_UNKNOWN)
                bNeedRecurse = true;
            break;
        }
        if (bNeedRecurse)
        {
            path.push_back(std::make_pair(pEndZone, i));
            float oldlen = fPathLen;
            fPathLen += passtime;
            RecurseFindPath(pZ);
            fPathLen = oldlen;
            path.pop_back();
        }
        if (bNeedRestore)
            bIsSecondaryPath = false;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
bool CI3DScreenGlobalMap::CheckZonePresence(LPCTSTR pszZoneName)
{
    return true;
    SScenarioZone *pZ = theMap.GetZoneByName(pszZoneName);
    if (!pZ)
        return false;
    CString szMobName;
    szMobName.Format("%s%s.mob", IWorld.GetMobPath(), pZ->szMobName);
    CFileFind find;
    if (!find.FindFile(szMobName))
    {
        CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(pI3D, "no_zone", "no_zone_msg",
                                                                 0x00010000, MBF_OK);
        pI3D->DoModal(pScr);
        return false;
    }
    else
        return true;
}
/////////////////////////////////////////////////////////////////////////////////////
