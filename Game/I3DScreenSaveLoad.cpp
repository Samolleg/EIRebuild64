#include "StdAfx.h"
#include "SaveSlot.h"
#include "CorePerf.h"
#include "OwnedRender/E2Gdi.h"
#include "GameJournal.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DScreenSaveLoad.h"
#include "ModRuntime.h"
#include "I3DScreenMessageBox.h"
#include "I3DScrollBar.h"
#include "WorldInterface.h"
#include "TextManager.h"
#include "SaveLoader.h"
#include "GlobalMap.h"
#include "I3DMapObjTranslation.h"
#include "A3Cursor.h"
#include "GameMain.h"
#include "Time32.h"
#include <algorithm>
#include <functional>
#include <io.h>
#include "dx11/Surface.h"
/////////////////////////////////////////////////////////////////////////////////////
const unsigned char VERSION_CHAR_1 = 36;
const unsigned char VERSION_CHAR_4 = 49;
const unsigned char VERSION_CHAR_7 = 223;
const unsigned char VERSION_CHAR_10 = 124;

const unsigned char VERSION_CODE_2 = 227;
const unsigned char VERSION_CODE_5 = 93;
const unsigned char VERSION_CODE_8 = 33;
const unsigned char VERSION_CODE_11 = 155;

////////////////////////////////////////////////////////////////////////////////
const int SAVELOAD_NUM_SLOTS_VISIBLE = 16;

const float SAVELOAD_FLYTIME = .5f;
const float SAVELOAD_FLY_BEGIN_SAVE = 700.f;
const float SAVELOAD_FLY_END_SAVE = 620.f;
const float SAVELOAD_FLY_BEGIN_LOAD = 700.f;
const float SAVELOAD_FLY_END_LOAD = 550.f;

const COLORREF SAVELOAD_COLOR_TEXT = RGB(228, 215, 167);

const int N_CURRENT_SAVE_SLOT_VERSION = 3;

const int NUM_BUTTONS = 3;

////////////////////////////////////////////////////////////////////////////////
SSaveSlotDesc &SSaveSlotDesc::operator=(const SSaveSlotDesc &a)
{
    szName = a.szName;
    szPath = a.szPath;
    time = a.time;

    fGameTime = a.fGameTime;

    szAllodName = a.szAllodName;
    szZoneName = a.szZoneName;
    szPersonages.Copy(a.szPersonages);
    szQuests.Copy(a.szQuests);

    bNoInfo = a.bNoInfo;

    return *this;
}
////////////////////////////////////////////////////////////////////////////////
void SSaveSlotDesc::Load(CSave &file)
{
    if (!file.LoadSaveHeader(0x111, N_CURRENT_SAVE_SLOT_VERSION, 1))
        return;

    file.Read(fGameTime);
    file.Read(szAllodName);
    file.Read(szZoneName);

    if (file.GetVersion() > 1)
    {
        // WARNING: this will work only unitl 2038 :)
        __time32_t t{};
        file.Read(t);
        time = t;
    }
    if (file.GetVersion() > 2)
    {
        file.Read(szName);
    }

    // LoadCStringArray( szPersonages, file );
    // LoadCStringArray( szQuests, file );
}
////////////////////////////////////////////////////////////////////////////////
void SSaveSlotDesc::Store(CSave &file)
{
    file.StoreSaveHeader(0x111, N_CURRENT_SAVE_SLOT_VERSION);

    file.Write(fGameTime);
    file.Write(szAllodName);
    file.Write(szZoneName);

    // WARNING: this will work only unitl 2038 :)
    // we write date converted to long int
    file.Write(time);

    file.Write(szName);
    // StoreCStringArray( szPersonages, file );
    // StoreCStringArray( szQuests, file );
}
////////////////////////////////////////////////////////////////////////////////
void SSaveSlotDesc::FormName(bool bDate)
{
    CString szQSaveStr, szASaveStr, szBSaveStr;
    IWorld.GetTextManager()->GetSystemString("save_quick", szQSaveStr);
    IWorld.GetTextManager()->GetSystemString("save_auto_enter", szASaveStr);
    IWorld.GetTextManager()->GetSystemString("save_auto_exit", szBSaveStr);
    if (szPath.CompareNoCase("saveq") == 0)
        szName.Format("%s", szQSaveStr);
    else if (szPath.CompareNoCase("savea") == 0)
        szName.Format("%s", szASaveStr);
    else if (szPath.CompareNoCase("saveb") == 0)
        szName.Format("%s", szBSaveStr);
    else
        szName.Format("%s", GetZoneLocalName());
    /*
    if ( bDate )
    {
            szName += " - ";
            szName += time.Format( "%d/%m, %H:%M" );
    }
    */
}
////////////////////////////////////////////////////////////////////////////////
CString SSaveSlotDesc::GetZoneLocalName()
{
    CString szRes;
    CTextManager *pTxtMng = IWorld.GetTextManager();
    if (pTxtMng->GetText(CString("zone ") + szZoneName))
        pTxtMng->GetLine(szRes);
    if (szRes.IsEmpty())
        pTxtMng->GetSystemString("save_camp", szRes);
    return szRes;
}
////////////////////////////////////////////////////////////////////////////////
CString SSaveSlotDesc::GetAllodLocalName()
{
    CString szRes;
    CTextManager *pTxtMng = IWorld.GetTextManager();
    if (pTxtMng->GetText(CString("allod ") + szAllodName))
        pTxtMng->GetLine(szRes);
    if (szRes.IsEmpty())
        szRes = "Unknown";
    return szRes;
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenSaveLoad::OnActivate()
{
    SetRect(0, 0, 800, 600);

    AddTexture("saveload");
    AddTexture(256, 256, TFTID_565);

    CI3DFigure *pFig;

    pFig = AddFigure(); // ok
    pFig->MakeBar(3, 100, 163, 526, 237, 574, 81, 2, 155, 50);
    pFig = AddFigure(); // cancel
    pFig->MakeBar(3, 100, 563, 526, 637, 574, 160, 2, 234, 50);
    pFig = AddFigure(); // delete
    pFig->MakeBar(3, 100, 363, 526, 437, 574, 2, 2, 76, 50);

    CI3DPolyArea *pArea;
    pArea = AddArea();
    pArea->MakeRect(163, 526, 237, 574); // ok
    if (bIsSaveScreen)
        pArea->SetToolTip(60100);
    else
        pArea->SetToolTip(60101);

    pArea = AddArea();
    pArea->MakeRect(563, 526, 637, 574); // cancel
    pArea->SetToolTip(60103);

    pArea = AddArea();
    pArea->MakeRect(363, 526, 437, 574); // delete
    pArea->SetToolTip(60102);

    pFig = AddFigure(); // screenshot
    pFig->MakeBar(3, 100, 410, 100, 700, 320, 0, 256, 256, 64);
    pFig->SetTexture(0);

    pFig = AddFigure(); // selection
    pFig->MakeBar(3, 100, 108, 108, 370, 132);
    pFig->SetTexture(0);
    pFig->SetDefaultColor(RGBA_MAKE(160, 104, 0, 200));
    pFig->UseDefaultColors(TRUE);

    pFig = AddFigure();
    pFig->MakeBarFrame(2, 100, 95, 95, 395, 505, 5, 2, 254, 180, 249);
    pFig = AddFigure();
    pFig->MakeBarFrame(2, 100, 405, 95, 705, 325, 5, 2, 254, 180, 249);
    pFig = AddFigure();
    pFig->MakeBarFrame(2, 100, 405, 335, 705, 505, 5, 2, 254, 180, 249);

    pFig = AddFigure();
    pFig->MakeBar(5, 100, 100, 100, 390, 500);
    pFig->SetTexture(0);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));
    pFig = AddFigure();
    pFig->MakeBar(5, 100, 410, 100, 700, 320);
    pFig->SetTexture(0);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));
    pFig = AddFigure();
    pFig->MakeBar(5, 100, 410, 340, 700, 500);
    pFig->SetTexture(0);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));

    // slot areas
    for (int i = 0; i < SAVELOAD_NUM_SLOTS_VISIBLE; i++)
        AddArea()->MakeRect(108, 108 + i * 24, 370, 132 + i * 24);

    AddSurface(dbgnew CI3DSurface(pI3D, 100, 100, 700, 500));

    AddObject(pScroll = dbgnew CI3DScrollBar(pI3D, CRect(108, 108, 384, 492), 3));

    CI3DMapObjTranslation *pObj = AddMapObjTranslation();
    pObj->Init("unmoco1", "escMenu00", "escMenu00labels");
    pObj->SetOrientation(CQuaternion(CPlot3D(1, 0, 0), FP_PI2 * 1.00f));
    pObj->SetRelativeToScreen(400, 700, 4.8f);

    if (bIsSaveScreen)
    {
        pObj->ShowAllBodyParts(false);
        pObj->ShowBodyPart("save", true);
        pObj->ShowBodyPart("savelabel", true);
    }
    else
    {
        pObj->ShowAllBodyParts(false);
        pObj->ShowBodyPart("load", true);
        pObj->ShowBodyPart("loadlabel", true);
    }

    pCurrentShot = 0;
    if (bIntro)
    {
        fIntroTime = 0;
    }

    FormSlotsList();
    if (bIsSaveScreen)
    {
        // addding new slot in the beginning
        nCurrentSlot = 0;
        ResetEdit();
    }
    else
    {
        if (slots.size() > 0)
            nCurrentSlot = 0;
        else
            nCurrentSlot = -1;
    }
    RedrawSlots();
    ReloadPicture();

    if (!bIntro)
        pI3D->CheckTutorial();
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenSaveLoad::OnDeactivate()
{
    if (pCurrentShot)
        pCurrentShot->Release();
    pCurrentShot = 0;
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenSaveLoad::OnExitFromModal(DWORD dwRetValue)
{
    switch (dwRetValue)
    {
    case 0x00010001:
        Delete();
        break;
    case 0x00020001:
        Save();
        break;
    case 0x00030001:
        pI3D->ExitFromModal(0x00100002);
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
LPCTSTR CI3DScreenSaveLoad::GetTutorialScreenName()
{
    /*
    if ( bIsSaveScreen )
            return "save";
    else
            return "load";
    */
    return 0;
}
////////////////////////////////////////////////////////////////////////////////
BOOL CI3DScreenSaveLoad::Draw(CRenderer *pRend)
{
    CorePerf::Scope coreTiming(CorePerf::ScreenSaveLoad);
    float fPart;
    CI3DMapObjTranslation *pObj = 0;
    if (bIntro)
    {
        surfaces[0]->Show(FALSE);
        if (fIntroTime < .5f)
            fPart = 2.f * fIntroTime * fIntroTime;
        else
            fPart = 1.f - 2.f * (1.f - fIntroTime) * (1.f - fIntroTime);

        if (pObj = GetMapObjTranslation(0))
        {
            if (bIsSaveScreen)
                pObj->SetRelativeToScreen(400,
                                          SAVELOAD_FLY_BEGIN_SAVE +
                                              fPart *
                                                  (SAVELOAD_FLY_END_SAVE - SAVELOAD_FLY_BEGIN_SAVE),
                                          4.8f);
            else
                pObj->SetRelativeToScreen(400,
                                          SAVELOAD_FLY_BEGIN_LOAD +
                                              fPart *
                                                  (SAVELOAD_FLY_END_LOAD - SAVELOAD_FLY_BEGIN_LOAD),
                                          4.8f);
            pObj->Draw(pRend);
        }
    }
    else
    {
        surfaces[0]->Show(TRUE);
        if (bIsSaveScreen)
            GetMapObjTranslation(0)->SetRelativeToScreen(400, SAVELOAD_FLY_END_SAVE, 4.8f);
        else
            GetMapObjTranslation(0)->SetRelativeToScreen(400, SAVELOAD_FLY_END_LOAD, 4.8f);
        CI3DScreen::Draw(pRend);
    }
    return TRUE;
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenSaveLoad::OnIdle()
{
    CI3DScreen::OnIdle();
    if (bIntro)
    {
        fIntroTime += GetFrameTime() / SAVELOAD_FLYTIME;
        if (fIntroTime > 1)
        {
            bIntro = false;
            pI3D->CheckTutorial();
        }
    }
    int n;
    if (pScroll->GetPos(n))
    {
        nPos = n;
        RedrawSlots();
    }
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenSaveLoad::OnLButtonDown(UINT nFlags, CPoint point)
{
    if (bIntro)
        return;
    int ht = HitTest(point);
    JournalHit(ht);
    switch (ht)
    {
    case 0: // OK
        ConfirmOK();
        break;
    case 1: // cancel
        pI3D->BeginFX("buttons\\messbox\\cancel.wav");
        pI3D->ExitFromModal(0x00100001);
        break;
    case 2: // delete
        ConfirmDel();
        break;
    }
    if (ht >= NUM_BUTTONS) // another slot
    {
        int n = nPos + ht - NUM_BUTTONS;
        if (n < slots.size())
        {
            pI3D->BeginFX("buttons\\save\\select.wav");
            if (n != nCurrentSlot)
            {
                nCurrentSlot = n;
                ResetEdit();
            }
            RedrawSlots();
            ReloadPicture();
        }
    }
    CI3DScreen::OnLButtonDown(nFlags, point);
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenSaveLoad::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    if (bIntro)
        return;
    int ht = HitTest(point);
    JournalHit(ht);
    if (ht >= NUM_BUTTONS) // another slot
    {
        int n = nPos + ht - NUM_BUTTONS;
        if (n < slots.size())
        {
            if (n != nCurrentSlot)
            {
                nCurrentSlot = n;
                ResetEdit();
            }
            ConfirmOK();
        }
    }
    CI3DScreen::OnLButtonDblClk(nFlags, point);
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenSaveLoad::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    if (bIntro)
        return;
    CI3DScreen::OnKeyDown(nChar, nRepCnt, nFlags);
    switch (nChar)
    {
    case 13:
        ConfirmOK();
        SetProcessed();
        break;
    /*
    case VK_DELETE:
            ConfirmDel();
            break;
    */
    case VK_ESCAPE:
        pI3D->BeginFX("buttons\\messbox\\cancel.wav");
        pI3D->ExitFromModal(0x00100001);
        SetProcessed();
        break;
    case VK_UP:
        pI3D->BeginFX("buttons\\save\\select.wav");
        --nCurrentSlot;
        MoveScrollToSelection();
        SetProcessed();
        break;
    case VK_DOWN:
        pI3D->BeginFX("buttons\\save\\select.wav");
        ++nCurrentSlot;
        MoveScrollToSelection();
        SetProcessed();
        break;
    case VK_PRIOR:
        pI3D->BeginFX("buttons\\save\\select.wav");
        nCurrentSlot -= SAVELOAD_NUM_SLOTS_VISIBLE;
        MoveScrollToSelection();
        SetProcessed();
        break;
    case VK_NEXT:
        pI3D->BeginFX("buttons\\save\\select.wav");
        nCurrentSlot += SAVELOAD_NUM_SLOTS_VISIBLE;
        MoveScrollToSelection();
        SetProcessed();
        break;
    }

    if (GetProcessed())
        return;

    if (bIsSaveScreen)
    {
        switch (nChar)
        {
        case VK_BACK:
            BeginEdit();
            if (nCursorPos > 0)
            {
                szEditName = szEditName.Left(nCursorPos - 1) + szEditName.Mid(nCursorPos);
                --nCursorPos;
            }
            RedrawSlots();
            SetProcessed();
            break;
        case VK_DELETE:
            BeginEdit();
            if (nCursorPos < szEditName.GetLength())
                szEditName = szEditName.Left(nCursorPos) + szEditName.Mid(nCursorPos + 1);
            RedrawSlots();
            SetProcessed();
            break;
        case VK_LEFT:
            BeginEdit(false);
            --nCursorPos;
            RedrawSlots();
            SetProcessed();
            break;
        case VK_RIGHT:
            BeginEdit(false);
            ++nCursorPos;
            RedrawSlots();
            SetProcessed();
            break;
        case VK_HOME:
            BeginEdit(false);
            nCursorPos = 0;
            RedrawSlots();
            SetProcessed();
            break;
        case VK_END:
            BeginEdit(false);
            nCursorPos = szEditName.GetLength();
            RedrawSlots();
            SetProcessed();
            break;
        }
    }
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenSaveLoad::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    if (!bIsSaveScreen)
        return;
    switch (nChar)
    {
    case VK_BACK:
    case VK_TAB:
    case VK_ESCAPE:
    case 13:
        break;

    default:
        BeginEdit();
        if (!pI3D->IsCtrlDown() && szEditName.GetLength() < 60)
        {
            CString szNewName = szEditName.Left(nCursorPos) + (char)nChar +
                                szEditName.Mid(nCursorPos);
            if (pI3D->TextSize(szNewName, 1).cx < 178)
            {
                szEditName = szNewName;
                nCursorPos++;
            }
        }
        RedrawSlots();
        break;
    }
    CI3DScreen::OnChar(nChar, nRepCnt, nFlags);
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenSaveLoad::ConfirmOK()
{
    if (nCurrentSlot < 0)
        return;
    if (bIsSaveScreen)
    {
        pI3D->BeginFX("buttons\\messbox\\ok.wav");
        if (nCurrentSlot == 0)
            Save();
        else
        {
            CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(pI3D, "overwrite",
                                                                     "overwrite_msg", 0x00020000);
            pI3D->DoModal(pScr);
        }
    }
    else
        Load();
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenSaveLoad::ConfirmDel()
{
    if (nCurrentSlot < 0)
        return;
    if (bIsSaveScreen && nCurrentSlot == 0)
        return;
    else
    {
        pI3D->BeginFX("buttons\\save\\delete.wav");
        CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(pI3D, "delete", "delete_msg",
                                                                 0x00010000);
        pI3D->DoModal(pScr);
    }
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenSaveLoad::MoveScrollToSelection()
{
    if (nCurrentSlot < 0)
        nCurrentSlot = 0;
    if (nCurrentSlot >= slots.size())
        nCurrentSlot = slots.size() - 1;
    if (nPos + SAVELOAD_NUM_SLOTS_VISIBLE <= nCurrentSlot)
        nPos = nCurrentSlot - SAVELOAD_NUM_SLOTS_VISIBLE + 1;
    if (nPos > nCurrentSlot)
        nPos = nCurrentSlot;
    pScroll->SetPos(nPos);
    pScroll->GetPos(nPos);
    ResetEdit();
    RedrawSlots();
    ReloadPicture();
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenSaveLoad::Save()
{
    if (nCurrentSlot < 0 || nCurrentSlot >= slots.size() || !IsEnoughSpace()) return;
    SSaveSlotDesc desc=slots[nCurrentSlot];
    if(bEdited) desc.szName=szEditName;
    desc.time=CTime32::GetCurrentTime();
    try {
        SaveSchema::Slot::SetPresentation(desc,pCurrentShot?pCurrentShot->GetSurface():nullptr);
        GameJournal::Source source("manual");
        if(!SaveSchema::Slot::Save(desc.szPath))return;
        pI3D->ExitFromModal(0x00100002);
    } catch(const std::exception &e){ReportError("Save64: %s",e.what());}
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenSaveLoad::Load()
{
    if (nCurrentSlot < 0 || nCurrentSlot >= slots.size())
        return;
    if (slots[nCurrentSlot].bNoInfo)
        return;
    CString szMsg;
    IWorld.GetTextManager()->GetSystemString("notify_loading", szMsg);
    theInterface.DrawTextOverFrame(szMsg);
    pI3D->BeginFX("buttons\\messbox\\ok.wav");
    IWorldConnector.ReqLoadGame(slots[nCurrentSlot].szPath);
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenSaveLoad::Delete()
{
    if (nCurrentSlot < 0 || nCurrentSlot >= slots.size())
        return;

    CString szDelPath;
    szDelPath.Format("%sSaves64\\%s", ModRuntime::SaveRoot(IWorld.GetWorkPath()).c_str(), slots[nCurrentSlot].szPath);

    CFileFind find;
    BOOL bFind = find.FindFile(szDelPath + "\\*.*");
    while (bFind)
    {
        bFind = find.FindNextFile();
        DeleteFile(find.GetFilePath());
    }
    find.Close();
    BOOL bRet = RemoveDirectory(szDelPath);
    FormSlotsList();
    if (nCurrentSlot >= slots.size())
        nCurrentSlot = slots.size() - 1;
    ResetEdit();
    RedrawSlots();
    ReloadPicture();
}
/////////////////////////////////////////////////////////////////////////////////////
class CSaveSlotTimeLessThenFunctional
{
public:
    bool operator()(const SSaveSlotDesc &pSlot1, const SSaveSlotDesc &pSlot2) const
    {
        if (pSlot1.time < pSlot2.time)
            return false;
        else
            return true;
    }
};
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenSaveLoad::FormSlotsList()
{
    CString szWildcard;
    szWildcard.Format("%sSaves64\\save*", ModRuntime::SaveRoot(IWorld.GetWorkPath()).c_str());
    CFileFind find;

    slots.clear();
    int nMax = 0;
    BOOL bFound = find.FindFile(szWildcard);
    while (bFound)
    {
        bFound = find.FindNextFile();
        if (find.IsDots())
            continue;
        if (find.IsDirectory())
        {
            SSaveSlotDesc slot;
            slots.push_back(slot);
            SSaveSlotDesc *pSlot = &slots.back();
            pSlot->szPath = find.GetFileName();

            pSlot->bNoInfo=!SaveSchema::Slot::Presentation(pSlot->szPath,*pSlot);
            if(pSlot->bNoInfo)pSlot->szName="Несовместимое сохранение";
            else if(pSlot->szName.IsEmpty())pSlot->FormName();

            if (pSlot->szPath.GetLength() > 4)
            {
                nMax = Max(nMax, atoi(pSlot->szPath.Mid(4)));
            }
        }
    }
    find.Close();
    // sort slots by time
    CSaveSlotTimeLessThenFunctional less_func;
    std::sort(slots.begin(), slots.end(), less_func);
    // add new slot for save screen
    if (bIsSaveScreen)
    {
        SSaveSlotDesc slot;
        slots.insert(slots.begin(), slot);
        SSaveSlotDesc *pSlot = &slots.front();
        pSlot->szPath.Format("save%d", nMax + 1);
        pSlot->fGameTime = IWorldClient.GSGetVar(szPartyGlobalTimeVarName);
        pSlot->szZoneName = IWorldClient.GetCurrentZoneName();
        SScenarioZone *pZone = theMap.GetZoneByName(pSlot->szZoneName);
        if (pZone)
            pSlot->szAllodName = pZone->szAllodName;
        pSlot->FormName();
        // IWorld.GetTextManager()->GetSystemString( "save_new", pSlot->szName );
        pSlot->bNoInfo = false;

        if (pCurrentShot)
            pCurrentShot->Release();
        pCurrentShot = 0;

        if (pI3D->GetSmallPicture())
        {
            pCurrentShot = AddTexture(256, 256, TFTID_565);
            pCurrentShot->AddRef();
            RemoveTexture(textures.GetSize() - 1);

            CDXGISurface *pSurfFrom = pI3D->GetSmallPicture();
            CDXGISurface *pSurfTo = pCurrentShot->GetSurface();
            pSurfTo->CopyResource(pSurfFrom);
        }
    }

    if (slots.size() > SAVELOAD_NUM_SLOTS_VISIBLE)
    {
        int n;
        pScroll->GetPos(n);
        pScroll->SetRange(slots.size() - SAVELOAD_NUM_SLOTS_VISIBLE);
        pScroll->SetPos(n);
    }
    else
        pScroll->SetRange(0);
    nPos = 0;
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenSaveLoad::RedrawSlots()
{
    if (nCursorPos > szEditName.GetLength())
        nCursorPos = szEditName.GetLength();
    if (nCursorPos < 0)
        nCursorPos = 0;

#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    CDC *pDC = pSurf->GetDC();
    pSurf->Clear();
#endif // NOGETDC
    int i, n;
    figures[4]->Show(FALSE);
    CRect rc(12, 11, 190, 32);
    CRect rc2(190, 11, 265, 32);
    CPoint shift(0, 24);
    for (i = 0, n = nPos; i < SAVELOAD_NUM_SLOTS_VISIBLE && n < slots.size(); i++, n++)
    {
        if (n == nCurrentSlot)
        {
#ifndef NOGETDC
            if (bIsSaveScreen && bEdited)
                pSurf->DrawText(szEditName, rc, 1, SAVELOAD_COLOR_TEXT, DT_LEFT | DT_NOPREFIX);
            else
                pSurf->DrawText(slots[n].szName, rc, 1, SAVELOAD_COLOR_TEXT, DT_LEFT | DT_NOPREFIX);

            if (bIsSaveScreen && !IsSystemSlot(n))
            {
                CSize sizeCursor = pI3D->TextSize(szEditName.Left(nCursorPos), 1);
                if (sizeCursor.cx < rc.Width())
                {
                    CPen pen(PS_SOLID, 1, RGB(255, 179, 49));
                    CPen *pOldPen = pDC->SelectObject(&pen);
                    CPoint p1(rc.left + sizeCursor.cx, rc.top);
                    CPoint p2(rc.left + sizeCursor.cx, rc.bottom - 3);
                    pI3D->VirtualToScreenLocal(p1);
                    pI3D->VirtualToScreenLocal(p2);
                    pDC->MoveTo(p1.x, p1.y);
                    OwnedRender::E2LineTo(pDC,p2.x, p2.y);
                    pDC->SelectObject(pOldPen);
                }
            }
#endif // NOGETDC
            figures[4]->Show(TRUE);
            figures[4]->SetRelativeToScreen(239, 120 + i * 24, 3);
        }
#ifndef NOGETDC
        else
            pSurf->DrawText(slots[n].szName, rc, 1, SAVELOAD_COLOR_TEXT, DT_LEFT | DT_NOPREFIX);

        if (!bIsSaveScreen || n != 0)
        {
            CString szTime = CTime32::Format(slots[n].time);

            pSurf->DrawText(szTime, rc2, 1, SAVELOAD_COLOR_TEXT, DT_RIGHT);
        }
#endif // NOGETDC
        rc += shift;
        rc2 += shift;
    }

    if (nCurrentSlot < 0 || nCurrentSlot >= slots.size())
    {
#ifndef NOGETDC
        pSurf->ReleaseDC();
#endif // NOGETDC
        return;
    }

    SSaveSlotDesc *pSlot = &slots[nCurrentSlot];
    if (pSlot->bNoInfo)
    {
#ifndef NOGETDC
        pSurf->ReleaseDC();
#endif // NOGETDC
        return;
    }
#ifndef NOGETDC
    rc = CRect(340, 260, 590, 284);
    CString szTmp;

    szTmp.Format("%s", pSlot->GetAllodLocalName());
    pSurf->DrawText(szTmp, rc, 1, SAVELOAD_COLOR_TEXT, DT_LEFT);
    rc += shift;
    szTmp.Format("%s", pSlot->GetZoneLocalName());
    pSurf->DrawText(szTmp, rc, 1, SAVELOAD_COLOR_TEXT, DT_LEFT);
    rc += shift;

    int nMinutes = static_cast<int>(pSlot->fGameTime * 60);

    int nDay = nMinutes / 1440;
    nMinutes -= nDay * 1440;
    int nHour = nMinutes / 60;
    nMinutes -= nHour * 60;
    int nMinute = nMinutes;

    CString szDayString;
    IWorld.GetTextManager()->GetSystemString("save_day", szDayString);

    szTmp.Format("%s  %d,  %d:%02d", szDayString, nDay + 1, nHour, nMinute);
    pSurf->DrawText(szTmp, rc, 1, SAVELOAD_COLOR_TEXT, DT_LEFT);

    pSurf->DrawText(pSlot->szPath, CRect(340, 360, 580, 385), 1, SAVELOAD_COLOR_TEXT, DT_RIGHT);

    pSurf->ReleaseDC();
#endif // NOGETDC
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenSaveLoad::ReloadPicture()
{
    figures[3]->Show(FALSE);

    if (nCurrentSlot < 0 || nCurrentSlot >= slots.size())
        return;
    SSaveSlotDesc *pSlot = &slots[nCurrentSlot];
    if (pSlot->bNoInfo)
        return;

    if (bIsSaveScreen && nCurrentSlot == 0)
    {
        if (!pCurrentShot)
            return;
        figures[3]->Show(TRUE);
        figures[3]->SetTexture(pCurrentShot);
        return;
    }

    SaveSchema::Bytes shot;
    SSaveSlotDesc info;
    if(!SaveSchema::Slot::Presentation(pSlot->szPath,info,&shot)||shot.empty())return;
    try{
        CMemorySave f;f.Write(shot.data(),static_cast<UINT>(shot.size()));f.Seek(0);
        SaveSchema::Registry reg;
        auto graph=SaveSchema::Load(f,reg,[&](SaveSchema::Reader &r){
            auto width=r.UInt32(),height=r.UInt32(),count=r.Count();
            auto *surface=textures[1]->GetSurface();
            if(width!=surface->GetWidth()||height!=surface->GetHeight()||std::uint64_t(width)*height!=count)
                throw SaveSchema::Error(SaveSchema::ErrorCode::Invalid,"thumbnail dimensions");
            std::vector<DWORD> pixels(count);for(auto &p:pixels)p=r.UInt32();
            D3D11_SUBRESOURCE_DATA data{};data.pSysMem=pixels.data();data.SysMemPitch=width*sizeof(DWORD);
            D3D11_BOX region{0,0,0,width,height,1};surface->UpdateTexture(0,&region,&data,MMP_FORMAT_8888);
        });
        figures[3]->Show(TRUE);figures[3]->SetTexture(textures[1]);
    }catch(const std::exception &e){GameJournal::Event("SAVE","thumbnail_invalid","reason=%s",e.what());}
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenSaveLoad::QuickSave(LPCTSTR pszSlotName, bool bIsAuto)
{
    GameJournal::Source source(bIsAuto?"auto":"manual_quick");
    if(!IsEnoughSpace())return;
    if(bIsAuto)NGame::DrawFrame(false);
    auto screen=std::make_unique<CI3DSurface>(&theInterface,0,0,800,600,BS_VISIBLE);
    screen->GetFromScreen(true);
    SSaveSlotDesc desc;desc.szPath=pszSlotName;desc.szName="";desc.time=CTime32::GetCurrentTime();
    desc.fGameTime=IWorldClient.GSGetVar(szPartyGlobalTimeVarName);desc.szZoneName=IWorldClient.GetCurrentZoneName();
    if(auto *zone=theMap.GetZoneByName(desc.szZoneName))desc.szAllodName=zone->szAllodName;
    try{
        auto thumbnail=SmallScreenFromScreen(screen.get());
        SaveSchema::Slot::SetPresentation(desc,thumbnail.get());
        IWorldConnector.ReqSaveGame(pszSlotName);
    }catch(const std::exception &e){ReportError("Save64: %s",e.what());}
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenSaveLoad::QuickLoad(LPCTSTR pszSlotName)
{

    CString szFullPath;
    szFullPath.Format("%sSaves64\\%s\\", ModRuntime::SaveRoot(IWorld.GetWorkPath()).c_str(), pszSlotName);

    if (_access(szFullPath, 0) != -1)
    {
        CString szMsg;
        IWorld.GetTextManager()->GetSystemString("notify_loading", szMsg);
        theInterface.DrawTextOverFrame(szMsg);
        IWorldConnector.ReqLoadGame(pszSlotName);
    }
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenSaveLoad::BeginEdit(bool bClear)
{
    if (nCurrentSlot < 0 || nCurrentSlot >= slots.size())
        return;
    if (IsSystemSlot(nCurrentSlot))
        return;
    if (!bEdited)
    {
        bEdited = true;
        if (bClear)
        {
            szEditName.Empty();
            nCursorPos = 0;
        }
        else
        {
            szEditName = slots[nCurrentSlot].szName;
            nCursorPos = szEditName.GetLength();
        }
    }
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenSaveLoad::ResetEdit()
{
    if (nCurrentSlot < 0 || nCurrentSlot >= slots.size())
        return;
    bEdited = false;
    szEditName = slots[nCurrentSlot].szName;
    nCursorPos = szEditName.GetLength();
}
////////////////////////////////////////////////////////////////////////////////
bool CI3DScreenSaveLoad::IsSystemSlot(int nSlot)
{
    if (slots[nSlot].szPath.CompareNoCase("saveq") == 0)
        return true;
    if (slots[nSlot].szPath.CompareNoCase("savea") == 0)
        return true;
    if (slots[nSlot].szPath.CompareNoCase("saveb") == 0)
        return true;
    return false;
}
////////////////////////////////////////////////////////////////////////////////
bool CI3DScreenSaveLoad::IsEnoughSpace()
{
    // check disk free space
    double fFreeSpace = NGame::GetFreeDiskSpace(ModRuntime::SaveRoot(IWorld.GetWorkPath()).c_str());
    if (fFreeSpace != 0)
    {
        if (fFreeSpace < double(IWorldConnector.GetSaveSize()))
            return false;
    }
    return true;
}
////////////////////////////////////////////////////////////////////////////////
