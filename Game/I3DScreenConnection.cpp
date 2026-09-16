#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DScreenConnection.h"
#include "I3DScreenMessageBox.h"
#include "I3DMapObjTranslation.h"
#include "I3DScrollBar.h"
#include "I3DMain.h"
#include "WorldInterface.h"
#include "Console.h"
#include "TextManager.h"
#include "RandomGen.h"

#ifndef MASTER_SERVER

////////////////////////////////////////////////////////////////////////////////
const int CON_NUM_SLOTS_VISIBLE = 16;
COLORREF CON_COLOR_TEXT = RGB(228, 215, 167);
const int NUM_BUTTONS = 4;
const int NUM_ALLODS = 4;
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::OnActivate()
{
    SetRect(0, 0, 800, 600);

    AddTexture("saveload");

    CI3DMapObjTranslation *pObj = AddMapObjTranslation();
    pObj->Init("unmoco2", "MainMenu00", "MainMenu00labels");
    pObj->SetRelativeToScreen(400, 350, 8.f);
    pObj->SetOrientation(
        CQuaternion(CPlot3D(1, 0, 0),
                    FP_PI2 * 1.2f) /* CQuaternion( CPlot3D(0,0,1), g_PI_DIV_2 * 2.00f ) */);

    pObj->ShowAllBodyParts(false);
    pObj->ShowBodyPart("but03", true);
    pObj->ShowBodyPart("button03_multiplayer", true);

    CI3DFigure *pFig;

    pFig = AddFigure(); // ok
    pFig->MakeBar(3, 100, 163, 526, 237, 574, 81, 2, 155, 50);
    pFig = AddFigure(); // cancel
    pFig->MakeBar(3, 100, 563, 526, 637, 574, 160, 2, 234, 50);
    pFig = AddFigure(); // address book add
    pFig->MakeBar(10, 100, 332, 532, 380, 568, 206, 147, 254, 183);
    pFig = AddFigure(); // address book delete
    pFig->MakeBar(10, 100, 420, 532, 468, 568, 206, 184, 254, 220);

    CI3DPolyArea *pArea;
    pArea = AddArea();
    pArea->MakeRect(163, 526, 237, 574); // ok
    pArea->SetToolTip(40600);
    pArea = AddArea();
    pArea->MakeRect(563, 526, 637, 574); // cancel
    pArea->SetToolTip(40601);
    pArea = AddArea();
    pArea->MakeRect(332, 532, 380, 568); // address book add
    pArea->SetToolTip(40602);
    pArea = AddArea();
    pArea->MakeRect(420, 532, 468, 568); // address book delete
    pArea->SetToolTip(40603);

    pFig = AddFigure(); // selection
    pFig->MakeBar(3, 100, 108, 108, 370, 132);
    pFig->SetTexture(0);
    pFig->SetDefaultColor(RGBA_MAKE(160, 104, 0, 200));
    pFig->UseDefaultColors(TRUE);

    pFig = AddFigure();
    pFig->MakeBarFrame(2, 100, 275, 533, 525, 567, 5, 2, 254, 180, 249);
    pFig->Show(FALSE);
    pFig = AddFigure();
    pFig->MakeBar(5, 100, 280, 538, 520, 563);
    pFig->SetTexture(0);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 200));
    pFig->Show(FALSE);

    CTextManager *pTxtMng = IWorld.GetTextManager();
    CString szTmp;
    for (int i = 0; i < NUM_ALLODS; i++)
    {
        szTmp.Format("lmp_allod_name_%d", i);
        pTxtMng->GetSystemString(szTmp, szTmp);
        szAllodNames.Add(szTmp);
        pFig = AddFigure();
        pFig->MakeBar(4, 100, 450, 240 + i * 24, 471, 261 + i * 24, 209, 55, 230, 76);
        AddArea()->MakeRect(450, 240 + i * 24, 471, 261 + i * 24);
    }
    figures[7]->SetUShift(-23 / 256.f);
    nAllod = 0;

    pFig = AddFigure();
    pFig->MakeBarFrame(2, 100, 95, 95, 395, 505, 5, 2, 254, 180, 249);
    pFig = AddFigure();
    pFig->MakeBarFrame(2, 100, 405, 95, 705, 505, 5, 2, 254, 180, 249);

    pFig = AddFigure();
    pFig->MakeBar(5, 100, 100, 100, 390, 500);
    pFig->SetTexture(0);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));
    pFig = AddFigure();
    pFig->MakeBar(5, 100, 410, 100, 700, 500);
    pFig->SetTexture(0);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));

    // slot areas
    for (int i = 0; i < CON_NUM_SLOTS_VISIBLE; i++)
        AddArea()->MakeRect(108, 108 + i * 24, 370, 132 + i * 24);

    AddSurface(dbgnew CI3DSurface(pI3D, 100, 100, 700, 500));

    AddObject(pScroll = dbgnew CI3DScrollBar(pI3D, CRect(108, 108, 384, 492), 3));

    AddObject(pPlayersSlider = dbgnew CI3DHScrollBar(pI3D, CRect(450, 196, 650, 206), 3, 5));
    pPlayersSlider->SetRect(CRect(450, 196, 650, 206));
    pPlayersSlider->SetPos(5);
    nNewMaxPlayers = 6;

    AddObject(pServerName = dbgnew CI3DEditBox(pI3D, CRect(450, 144, 650, 160)));
    pServerName->SetParams(EBF_DIGITS | EBF_LETTERS, FALSE, 1, 10, CON_COLOR_TEXT,
                           RGB(255, 179, 49));
    CString szServerName(IWorldClient.GetParty().people[0].szName);
    int nCh = szServerName.Find("|");
    if (nCh >= 0)
        szServerName = szServerName.Left(nCh);
    if (szServerName.GetLength() > 10)
        szServerName = szServerName.Left(10);
    pServerName->SetString(szServerName);
    pServerName->Show(FALSE);
    pServerName->Activate(false);

    for (int i = 0; i < TOTAL; i++)
    {
        szTmp.Format("lmp_connection_%d", i);
        pTxtMng->GetSystemString(szTmp, szTmp);
        szStrings.Add(szTmp);
    }

    AddObject(pIPAddress = dbgnew CI3DEditBox(pI3D, CRect(285, 542, 515, 558)));
    pIPAddress->SetParams(EBF_ALL, FALSE, 1, 30, CON_COLOR_TEXT, RGB(255, 179, 49));
    pIPAddress->SetString("");
    pIPAddress->Show(FALSE);
    pIPAddress->Activate(false);

    IWorldConnector.StartLMPClient();

    CCSAddressBook book;
    CStructureSaver saver;
    CFileSave file;
    if (file.OpenRead(IWorld.GetWorkPath() + "config\\addrbook.ini"))
    {
        saver.Start(file, true);
        saver.Add(1, book);
        saver.Finish(file);
        file.CloseFile();
        if (book.nUniqueClientID == 0)
        {
            // generate unique client ID (hope this will be sufficient to separate clients
            book.nUniqueClientID = random.Get() ^ GetTickCount();
        }
        pChatClient->SetAddressBook(book);
    }

    bOldConnectState = false;
    bEnterIP = false;
    nCurrentSlot = 0;
    curAddress.Clear();

    FormSlotsList();

    RedrawSlots();

    pI3D->CheckTutorial();
    fTime = 0;

    CI3DSurface *pSurf = AddSurface(dbgnew CI3DSurface(pI3D, 0, 0, 300, 20));
#ifndef NOGETDC
    pSurf->GetDC();
    pSurf->Clear();
    pSurf->DrawText(szStrings[CONNECTING], CRect(0, 0, 300, 20), 2, CON_COLOR_TEXT, DT_LEFT);
    pSurf->ReleaseDC();
#endif // NOGETDC
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::OnDeactivate()
{
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::OnExitFromModal(DWORD dwRetValue)
{
}
/////////////////////////////////////////////////////////////////////////////////////
LPCTSTR CI3DScreenConnection::GetTutorialScreenName()
{
    return 0;
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::OnIdle()
{
    if (bEnterIP)
        return;
    bool bNewConnectState;
    if (bNewConnectState = pChatClient->IsConnecting())
        surfaces[1]->Show(TRUE);
    else
        surfaces[1]->Show(FALSE);
    if (!bNewConnectState && bOldConnectState) // server rejected
    {
        bOldConnectState = bNewConnectState;
        CString szReason;
        switch (pChatClient->GetRejectReason())
        {
        case CSR_SERVER_FULL:
            szReason = "lmp_server_full";
            break;
        case CSR_IP_BANNED:
            szReason = "lmp_you_are_banned";
            break;
        default:
            szReason = "lmp_cannot_connect";
            break;
        }
        CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(
            pI3D, szReason, (LPTSTR)(LPCTSTR)(szReason + "_msg"), 0x00010000, MBF_OK);
        pI3D->DoModal(pScr);
        return;
    }
    bOldConnectState = bNewConnectState;

    CI3DScreen::OnIdle();
    fTime += GetFrameTime();
    if (fTime > 0.5f)
    {
        while (fTime > 0.5f)
            fTime -= 0.5f;
        pChatClient->SendServerReqs();
        FormSlotsList();
        RedrawSlots();
    }
    int n;
    if (pScroll->GetPos(n))
    {
        nPos = n;
        RedrawSlots();
    }
    if (pPlayersSlider->GetPos(n))
    {
        nNewMaxPlayers = n + 1;
        RedrawSlots();
    }
    if (pServerName->IsUpdated())
        RedrawSlots();
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::OnLButtonDown(UINT nFlags, CPoint point)
{
    if (bEnterIP)
        return;
    int ht = HitTest(point);
    switch (ht)
    {
    case 0: // OK
        EnterGame();
        break;
    case 1: // cancel
        pI3D->BeginFX("buttons\\messbox\\cancel.wav");
        SaveAddressBook();
        IWorldConnector.StopLMP();
        pI3D->ExitFromModal(0x01000001);
        break;
    case 2: // address book
        AddressBookAdd();
        break;
    case 3:
        AddressBookDelete();
        break;
    }
    if (ht >= NUM_BUTTONS && ht < NUM_BUTTONS + NUM_ALLODS && nCurrentSlot > 0)
    {
        nAllod = ht - NUM_BUTTONS;
        for (int i = 0; i < NUM_ALLODS; i++)
            figures[7 + i]->SetUShift(0);
        figures[7 + nAllod]->SetUShift(-23 / 256.f);
    }
    if (ht >= NUM_BUTTONS + NUM_ALLODS) // another slot
    {
        int n = nPos + ht - NUM_BUTTONS - NUM_ALLODS;
        if (n < slots.size())
        {
            pI3D->BeginFX("buttons\\save\\select.wav");
            nCurrentSlot = n;
            curAddress = slots[nCurrentSlot].addr;
            RedrawSlots();
        }
    }
    CI3DScreen::OnLButtonDown(nFlags, point);
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    if (bEnterIP)
        return;
    int ht = HitTest(point);
    if (ht >= NUM_BUTTONS + NUM_ALLODS) // another slot
    {
        int n = nPos + ht - NUM_BUTTONS - NUM_ALLODS;
        if (n < slots.size())
        {
            nCurrentSlot = n;
            curAddress = slots[nCurrentSlot].addr;
            EnterGame();
        }
    }
    CI3DScreen::OnLButtonDblClk(nFlags, point);
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    CI3DScreen::OnKeyDown(nChar, nRepCnt, nFlags);
    switch (nChar)
    {
    case 13:
        if (bEnterIP)
        {
            CCSNodeAddress addr;
            if (addr.SetInetName(pIPAddress->GetString(), NCSParams::nCSServerPort))
                pChatClient->AddAddressToBook(addr);
            SaveAddressBook();
            FinishEnterIP();
        }
        else
            EnterGame();
        SetProcessed();
        break;
    case VK_ESCAPE:
        if (bEnterIP)
            FinishEnterIP();
        else
        {
            pI3D->BeginFX("buttons\\messbox\\cancel.wav");
            IWorldConnector.StopLMP();
            pI3D->ExitFromModal(0x01000001);
        }
        SetProcessed();
        break;
    case VK_UP:
        if (bEnterIP)
            break;
        pI3D->BeginFX("buttons\\save\\select.wav");
        --nCurrentSlot;
        MoveScrollToSelection();
        SetProcessed();
        break;
    case VK_DOWN:
        if (bEnterIP)
            break;
        pI3D->BeginFX("buttons\\save\\select.wav");
        ++nCurrentSlot;
        MoveScrollToSelection();
        SetProcessed();
        break;
    case VK_PRIOR:
        if (bEnterIP)
            break;
        pI3D->BeginFX("buttons\\save\\select.wav");
        nCurrentSlot -= CON_NUM_SLOTS_VISIBLE;
        MoveScrollToSelection();
        SetProcessed();
        break;
    case VK_NEXT:
        if (bEnterIP)
            break;
        pI3D->BeginFX("buttons\\save\\select.wav");
        nCurrentSlot += CON_NUM_SLOTS_VISIBLE;
        MoveScrollToSelection();
        SetProcessed();
        break;
    }
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::MoveScrollToSelection()
{
    if (nCurrentSlot < 0)
        nCurrentSlot = 0;
    if (nCurrentSlot >= slots.size())
        nCurrentSlot = slots.size() - 1;
    curAddress = slots[nCurrentSlot].addr;
    if (nPos + CON_NUM_SLOTS_VISIBLE <= nCurrentSlot)
        nPos = nCurrentSlot - CON_NUM_SLOTS_VISIBLE + 1;
    if (nPos > nCurrentSlot)
        nPos = nCurrentSlot;
    pScroll->SetPos(nPos);
    pScroll->GetPos(nPos);
    RedrawSlots();
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::EnterGame()
{
    pI3D->BeginFX("buttons\\messbox\\ok.wav");
    // kakoy CRAP!
    if (nCurrentSlot == 0)
    {
        int nMaxPlayers;
        pPlayersSlider->GetPos(nMaxPlayers);
        nMaxPlayers++;
        CString szServerName(pServerName->GetString());
        if (szServerName.IsEmpty())
            szServerName = "?";
        SaveAddressBook();
        IWorldConnector.StartLMPServer(szServerName, nMaxPlayers, (ELMPAllod)nAllod, "", true, 0);
    }
    else
    {
        if (nCurrentSlot < 0 || nCurrentSlot >= slots.size())
            return;
        if (slots[nCurrentSlot].bWrong)
        {
            CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(pI3D, "lmp_wrong_protocol",
                                                                     "lmp_wrong_protocol_msg",
                                                                     0x00010000, MBF_OK);
            pI3D->DoModal(pScr);
            return;
        }
        SaveAddressBook();
        CMemoryStream pswd;
        pChatClient->ConnectServer(slots[nCurrentSlot].addr, pswd);
        pChatClient->allod = slots[nCurrentSlot].allod;
    }
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::AddressBookAdd()
{
    bEnterIP = true;
    for (int i = 0; i < 4; i++)
        figures[i]->SetAmbient(0.5f, 0.5f, 0.5f);
    for (int i = 0; i < NUM_BUTTONS; i++)
        areas[i]->SetToolTip(0);
    figures[5]->Show(TRUE);
    figures[6]->Show(TRUE);
    pServerName->Show(FALSE);
    pServerName->Activate(false);
    pIPAddress->Show(TRUE);
    pIPAddress->Activate(true);
    pIPAddress->SetString("");
    RedrawSlots();
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::AddressBookDelete()
{
    if (nCurrentSlot < 1 || nCurrentSlot >= slots.size())
        return;
    if (!slots[nCurrentSlot].bIsInBook)
        return;
    pChatClient->RemoveAddressFromBook(slots[nCurrentSlot].addr);
    SaveAddressBook();
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::SaveAddressBook()
{
    CStructureSaver saver;
    CFileSave file;
    if (file.OpenWrite(IWorld.GetWorkPath() + "config\\addrbook.ini"))
    {
        saver.Start(file, false);
        saver.Add(1, pChatClient->GetAddressBook());
        saver.Finish(file);
        file.CloseFile();
    }
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::FinishEnterIP()
{
    bEnterIP = false;
    for (int i = 0; i < 4; i++)
        figures[i]->SetAmbient(1, 1, 1);
    for (int i = 0; i < NUM_BUTTONS; i++)
        areas[i]->SetToolTip(40600 + i);
    figures[5]->Show(FALSE);
    figures[6]->Show(FALSE);
    pIPAddress->Show(FALSE);
    pIPAddress->Activate(false);
    RedrawSlots();
}
/////////////////////////////////////////////////////////////////////////////////////
class CServerDescLessThenFunctional
{
public:
    bool operator()(const SServerDesc &pSlot1, const SServerDesc &pSlot2) const
    {
        if (!pSlot1.bIsInBook && pSlot2.bIsInBook)
            return true;
        return false;
    }
};
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::FormSlotsList()
{
    slots.clear();

    CCSAddressBook &book = pChatClient->GetAddressBook();
    // CCSClient::CServerInfoList::iterator i;
    SServerDesc *pSlot;
    for (int k = 0;; k++)
    {
        CChatServerInfo *pInfo = (CChatServerInfo *)pChatClient->GetServerInfo(k);
        if (!pInfo)
            break;
        slots.emplace_back();
        SServerDesc *pSlot = &slots.back();
        pSlot->addr = pInfo->addr;
        pSlot->allod = pInfo->allod;
        pSlot->szName = pInfo->szName.c_str();
        pSlot->szZone = pInfo->szQuestName.c_str();
        pSlot->nPlayers = pInfo->nPlayers;
        pSlot->nMaxPlayers = pInfo->nMaxPlayers;
        pSlot->nPing = Float2Int(pInfo->fpPing * 1000);
        pSlot->bWrong = pInfo->bWrongVersion;
        pSlot->bIsInBook = book.HasAddr(pInfo->addr);
    }
    CServerDescLessThenFunctional less_func;
    std::sort(slots.begin(), slots.end(), less_func);

    slots.emplace(slots.begin());
    pSlot = &slots.front();
    pSlot->addr.Clear();
    pSlot->allod = LMPA_NONE;
    pSlot->szName = "??";
    pSlot->szZone = "??";
    pSlot->nPlayers = 0;
    pSlot->nMaxPlayers = 0;
    pSlot->nPing = 9999;
    pSlot->bWrong = false;
    pSlot->bIsInBook = false;

    nCurrentSlot = 0;
    for (int k = 1; k < slots.size(); k++)
        if (slots[k].addr == curAddress)
            nCurrentSlot = k;
    if (nCurrentSlot == 0)
        curAddress.Clear();

    if (slots.size() > CON_NUM_SLOTS_VISIBLE)
    {
        int n;
        pScroll->GetPos(n);
        pScroll->SetRange(slots.size() - CON_NUM_SLOTS_VISIBLE);
        pScroll->SetPos(n);
    }
    else
        pScroll->SetRange(0);
    nPos = 0;
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::RedrawSlots()
{
    bool bShowControls = (nCurrentSlot == 0);
    if (bEnterIP)
        bShowControls = false;
    pPlayersSlider->Show(bShowControls);
    pServerName->Show(bShowControls);
    pServerName->Activate(bShowControls);

    CString szTmp;
#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    CDC *pDC = pSurf->GetDC();
    pSurf->Clear();
    int i, n;
    figures[4]->Show(FALSE);
    CRect rc(12, 11, 265, 32);
    CPoint shift(0, 24);
    for (i = 0, n = nPos; i < CON_NUM_SLOTS_VISIBLE && n < slots.size(); i++, n++)
    {
        if (n == nCurrentSlot && !bEnterIP)
        {
            figures[4]->Show(TRUE);
            figures[4]->SetRelativeToScreen(239, 120 + i * 24, 3);
        }

        SServerDesc &slot = slots[n];

        if (bEnterIP || slot.bWrong)
            CON_COLOR_TEXT = RGB(130, 130, 130);
        else
        {
            if (slot.bIsInBook)
                CON_COLOR_TEXT = RGB(238, 227, 49);
            else
                CON_COLOR_TEXT = RGB(228, 215, 167);
        }
        if (n == 0) // create server
        {
            szTmp.Format("%s %s", szStrings[NEW_SERVER], pServerName->GetString());
            pSurf->DrawText(szTmp, rc, 1, CON_COLOR_TEXT, DT_LEFT | DT_NOPREFIX);
        }
        else
        {
            pSurf->DrawText(slot.szName, rc, 1, CON_COLOR_TEXT, DT_LEFT | DT_NOPREFIX);
            rc.left = 142;
            szTmp.Format("%d/%d", slot.nPlayers, slot.nMaxPlayers);
            pSurf->DrawText(szTmp, rc, 1, CON_COLOR_TEXT, DT_LEFT);
            rc.left = 170;
            if (slot.allod == LMPA_NONE)
                szTmp = "?";
            else
                szTmp.Format("%s", szAllodNames[slot.allod]);
            pSurf->DrawText(szTmp, rc, 1, CON_COLOR_TEXT, DT_LEFT);
            rc.left = 240;
            szTmp.Format("%d", slot.nPing);
            pSurf->DrawText(szTmp, rc, 1, CON_COLOR_TEXT, DT_RIGHT);
            rc.left = 12;
        }
        rc += shift;
    }

    for (i = 0; i < NUM_ALLODS; i++)
        figures[7 + i]->Show(FALSE);

    if (nCurrentSlot < 0 || nCurrentSlot >= slots.size() || bEnterIP)
    {
        pSurf->ReleaseDC();
        return;
    }

    CON_COLOR_TEXT = RGB(228, 215, 167);

    SServerDesc *pSlot = &slots[nCurrentSlot];
    rc = CRect(340, 20, 590, 44);

    if (nCurrentSlot == 0)
    {
        for (i = 0; i < NUM_ALLODS; i++)
            figures[7 + i]->Show(TRUE);
        pSurf->DrawText(szStrings[SERVER], rc, 1, CON_COLOR_TEXT, DT_LEFT);
        rc += shift;
        rc += shift;
        szTmp.Format("%s %d", szStrings[MAX_PLAYERS], nNewMaxPlayers);
        pSurf->DrawText(szTmp, rc, 1, CON_COLOR_TEXT, DT_LEFT);
        rc += shift;
        rc += shift;
        pSurf->DrawText(szStrings[ALLOD], rc, 1, CON_COLOR_TEXT, DT_LEFT);
        rc += shift;
        rc.left = 380;
        for (int i = 0; i < NUM_ALLODS; i++)
        {
            pSurf->DrawText(szAllodNames[i], rc, 1, CON_COLOR_TEXT, DT_LEFT);
            rc += shift;
        }
    }
    else
    {
        szTmp.Format("%s %s", szStrings[SERVER], pSlot->szName);
        pSurf->DrawText(szTmp, rc, 1, CON_COLOR_TEXT, DT_LEFT);
        rc += shift;
        szTmp.Format("%s %d / %d", szStrings[PLAYERS], pSlot->nPlayers, pSlot->nMaxPlayers);
        pSurf->DrawText(szTmp, rc, 1, CON_COLOR_TEXT, DT_LEFT);
        rc += shift;
        if (pSlot->allod == LMPA_NONE)
            szTmp = szStrings[ALLOD] + " ?";
        else
            szTmp.Format("%s %s", szStrings[ALLOD], szAllodNames[pSlot->allod]);
        pSurf->DrawText(szTmp, rc, 1, CON_COLOR_TEXT, DT_LEFT);
        rc += shift;
        szTmp.Format("%s %s", szStrings[QUEST], pSlot->szZone);
        int nLines = pSurf->DrawMultiText(szTmp, rc, 3, 1, CON_COLOR_TEXT, DT_LEFT);
        while (nLines--)
            rc += shift;
        szTmp.Format("%s %s", szStrings[IP], pSlot->addr.GetFastName().c_str());
        pSurf->DrawText(szTmp, rc, 1, CON_COLOR_TEXT, DT_LEFT);
        rc += shift;
        szTmp.Format("%s %d", szStrings[PING], pSlot->nPing);
        pSurf->DrawText(szTmp, rc, 1, CON_COLOR_TEXT, DT_LEFT);
        rc += shift;
    }
    pSurf->ReleaseDC();
#endif // NOGETDC
}
////////////////////////////////////////////////////////////////////////////////

#else // MASTER_SERVER

////////////////////////////////////////////////////////////////////////////////
const int CON_NUM_SLOTS_VISIBLE = 15;
COLORREF CON_COLOR_TEXT = RGB(228, 215, 167);
const int NUM_BUTTONS = 10;
const int NUM_ALLODS = 4;
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::OnActivate()
{
    SetRect(0, 0, 800, 600);

    AddTexture("saveload");
    AddTexture("scrollbar");

    CI3DMapObjTranslation *pObj = AddMapObjTranslation();
    pObj->Init("unmoco2", "MainMenu00", "MainMenu00labels");
    pObj->SetRelativeToScreen(400, 350, 8.f);
    pObj->SetOrientation(
        CQuaternion(CPlot3D(1, 0, 0),
                    FP_PI2 * 1.2f) /* CQuaternion( CPlot3D(0,0,1), g_PI_DIV_2 * 2.00f ) */);

    pObj->ShowAllBodyParts(false);
    pObj->ShowBodyPart("but03", true);
    pObj->ShowBodyPart("button03_multiplayer", true);

    CI3DPolyArea *pArea;
    pArea = AddArea();
    pArea->MakeRect(163, 526, 237, 574); // ok
    pArea->SetToolTip(40600);
    pArea = AddArea();
    pArea->MakeRect(563, 526, 637, 574); // cancel
    pArea->SetToolTip(40601);

    pArea = AddArea();
    pArea->MakeRect(490, 441, 620, 465); // refresh
    pArea->SetToolTip(40610);
    pArea = AddArea();
    pArea->MakeRect(490, 465, 620, 489); // add/remove favorites
    pArea->SetToolTip(40611);

    pArea = AddArea();
    pArea->MakeRect(440, 346, 670, 367); // private
    pArea = AddArea();
    pArea->MakeRect(440, 370, 670, 391); // password

    pArea = AddArea();
    pArea->MakeRect(106, 106, 236, 127); // name sort
    pArea = AddArea();
    pArea->MakeRect(236, 106, 264, 127); // pl sort
    pArea = AddArea();
    pArea->MakeRect(264, 106, 334, 127); // allod sort
    pArea = AddArea();
    pArea->MakeRect(334, 106, 375, 127); // ping sort

    CI3DFigure *pFig;

    pFig = AddFigure(); // ok
    pFig->MakeBar(3, 100, 163, 526, 237, 574, 81, 2, 155, 50);
    pFig = AddFigure(); // cancel
    pFig->MakeBar(3, 100, 563, 526, 637, 574, 160, 2, 234, 50);

    pFig = AddFigure(); // selection
    pFig->MakeBar(3, 100, 108, 108, 370, 132);
    pFig->SetTexture(0);
    pFig->SetDefaultColor(RGBA_MAKE(160, 104, 0, 200));
    pFig->UseDefaultColors(TRUE);

    pFig = AddFigure(); // sort mode
    pFig->SetTexture(textures[1]);
    pFig->MakeBar(4, 100, 0, 0, 10, 12, 40, 128, 80, 176);
    pFig->Show(FALSE);

    pFig = AddFigure();
    pFig->MakeBar(4, 100, 440, 346, 461, 367, 209, 55, 230, 76);
    pFig->SetUShift(-23 / 256.f);

    pFig = AddFigure();
    pFig->MakeBar(4, 100, 440, 370, 461, 391, 209, 55, 230, 76);

    CTextManager *pTxtMng = IWorld.GetTextManager();
    CString szTmp;
    for (int i = 0; i < NUM_ALLODS; i++)
    {
        szTmp.Format("lmp_allod_name_%d", i);
        pTxtMng->GetSystemString(szTmp, szTmp);
        szAllodNames.Add(szTmp);
        pFig = AddFigure();
        pFig->MakeBar(4, 100, 460, 238 + i * 24, 481, 259 + i * 24, 209, 55, 230, 76);
        AddArea()->MakeRect(460, 238 + i * 24, 670, 259 + i * 24);
    }
    figures[6]->SetUShift(23 / 256.f);
    nAllod = 0;
    bPrivate = true;
    szPassword.Empty();

    for (int i = 0; i < 2; i++) // refresh, add/remove
    {
        pFig = AddFigure();
        pFig->MakeBar(3, 100, 490, 444 + i * 24, 620, 465 + i * 24, 122, 108, 252, 127);
    }

    pFig = AddFigure();
    pFig->MakeBarFrame(2, 100, 95, 95, 395, 505, 5, 2, 254, 180, 249);
    pFig = AddFigure();
    pFig->MakeBarFrame(2, 100, 405, 95, 705, 415, 5, 2, 254, 180, 249);
    pFig = AddFigure();
    pFig->MakeBarFrame(2, 100, 405, 425, 705, 505, 5, 2, 254, 180, 249);

    pFig = AddFigure();
    pFig->MakeBar(5, 100, 100, 100, 390, 500);
    pFig->SetTexture(0);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));
    pFig = AddFigure();
    pFig->MakeBar(5, 100, 410, 100, 700, 410);
    pFig->SetTexture(0);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));
    pFig = AddFigure();
    pFig->MakeBar(5, 100, 410, 430, 700, 500);
    pFig->SetTexture(0);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(0, 0, 0, 160));

    // slot areas
    for (int i = 0; i < CON_NUM_SLOTS_VISIBLE; i++)
        AddArea()->MakeRect(108, 132 + i * 24, 370, 156 + i * 24);

    AddSurface(dbgnew CI3DSurface(pI3D, 100, 100, 700, 500));

    AddObject(pScroll = dbgnew CI3DScrollBar(pI3D, CRect(108, 132, 384, 492), 3));

    AddObject(pPlayersSlider = dbgnew CI3DHScrollBar(pI3D, CRect(450, 196, 650, 206), 3, 5));
    pPlayersSlider->SetRect(CRect(450, 196, 650, 206));
    pPlayersSlider->SetPos(5);
    nNewMaxPlayers = 6;

    AddObject(pServerName = dbgnew CI3DEditBox(pI3D, CRect(450, 144, 650, 160)));
    pServerName->SetParams(EBF_DIGITS | EBF_LETTERS, FALSE, 1, 10, CON_COLOR_TEXT,
                           RGB(255, 179, 49));
    CString szServerName(IWorldClient.GetParty().people[0].szName);
    int nCh = szServerName.Find("|");
    if (nCh >= 0)
        szServerName = szServerName.Left(nCh);
    if (szServerName.GetLength() > 10)
        szServerName = szServerName.Left(10);
    szServerName.TrimLeft();
    szServerName.TrimRight();
    pServerName->SetString(szServerName);
    pServerName->Show(FALSE);
    pServerName->Activate(false);

    for (int i = 0; i < TOTAL; i++)
    {
        szTmp.Format("lmp_connection_%d", i);
        pTxtMng->GetSystemString(szTmp, szTmp);
        szStrings.Add(szTmp);
    }

    IWorldConnector.StartLMPClient();
    pMasterRequest = 0;

    CCSAddressBook book;
    CStructureSaver saver;
    CFileSave file;
    if (file.OpenRead(IWorld.GetWorkPath() + "config\\addrbook.ini"))
    {
        saver.Start(file, true);
        saver.Add(1, book);
        saver.Finish(file);
        file.CloseFile();
        if (book.nUniqueClientID == 0)
        {
            // generate unique client ID (hope this will be sufficient to separate clients
            book.nUniqueClientID = random.Get() ^ GetTickCount();
        }
        pChatClient->SetAddressBook(book);
    }

    nSortMode = 0;

    bOldConnectState = false;
    nCurrentSlot = 0;
    bNewServer = false;
    curAddress.Clear();

    FormSlotsList();

    RedrawSlots();

    pI3D->CheckTutorial();
    fTime = 0;

    CI3DSurface *pSurf = AddSurface(dbgnew CI3DSurface(pI3D, 0, 0, 300, 20));
#ifndef NOGETDC
    pSurf->GetDC();
    pSurf->Clear();
    pSurf->DrawText(szStrings[CONNECTING], CRect(0, 0, 300, 20), 2, CON_COLOR_TEXT, DT_LEFT);
    pSurf->ReleaseDC();
#endif // NOGETDC
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::OnDeactivate()
{
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::OnExitFromModal(DWORD dwRetValue)
{
    switch (dwRetValue)
    {
    case 0x00020001: {
        CCSNodeAddress addr;
        if (addr.SetInetName(pszBuffer, NCSParams::nCSServerPort))
            pChatClient->AddAddressToBook(addr);
        SaveAddressBook();
        RedrawSlots();
    }
    break;
    case 0x00030001:
    case 0x00030002:
        szPassword = pszBuffer;
        figures[5]->SetUShift(szPassword.IsEmpty() ? 0 : (-23 / 256.f));
        break;
    case 0x00040002:
        RedrawSlots();
        break;
    case 0x00050001:
        IWorldConnector.ConnectLMPServer(slots[nCurrentSlot].addr, pszBuffer);
        pChatClient->allod = slots[nCurrentSlot].allod;
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
LPCTSTR CI3DScreenConnection::GetTutorialScreenName()
{
    return 0;
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::OnIdle()
{
    bool bNewConnectState;
    if (bNewConnectState = pChatClient->IsConnecting())
        surfaces[1]->Show(TRUE);
    else
        surfaces[1]->Show(FALSE);
    if (!bNewConnectState && bOldConnectState) // server rejected
    {
        bOldConnectState = bNewConnectState;
        CString szReason;
        switch (pChatClient->GetRejectReason())
        {
        case CSR_SERVER_FULL:
            szReason = "lmp_server_full";
            break;
        case CSR_IP_BANNED:
            szReason = "lmp_you_are_banned";
            break;
        case CSR_WRONG_PASSWORD:
            szReason = "lmp_wrong_password";
            break;
        default:
            szReason = "lmp_cannot_connect";
            break;
        }
        CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(
            pI3D, szReason, (LPTSTR)(LPCTSTR)(szReason + "_msg"), 0x00010000, MBF_OK);
        pI3D->DoModal(pScr);
        return;
    }
    bOldConnectState = bNewConnectState;

    CI3DScreen::OnIdle();
    fTime += GetFrameTime();
    if (fTime > 0.5f)
    {
        while (fTime > 0.5f)
            fTime -= 0.5f;
        if (nCurrentSlot > 0 && nCurrentSlot < slots.size())
            pChatClient->SetActiveServer(slots[nCurrentSlot].addr);
        else
        {
            CCSNodeAddress addr;
            pChatClient->SetActiveServer(addr);
        }
        pChatClient->SendServerReqs();
        FormSlotsList();
        RedrawSlots();
    }
    int n;
    if (pScroll->GetPos(n))
    {
        nPos = n;
        RedrawSlots();
    }
    if (pPlayersSlider->GetPos(n))
    {
        nNewMaxPlayers = n + 1;
        RedrawSlots();
    }
    if (pServerName->IsUpdated())
        RedrawSlots();
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::OnLButtonDown(UINT nFlags, CPoint point)
{
    int ht = HitTest(point);
    switch (ht)
    {
    case 0: // OK
        EnterGame();
        break;
    case 1: // cancel
        pI3D->BeginFX("buttons\\messbox\\cancel.wav");
        SaveAddressBook();
        IWorldConnector.StopLMP();
        pI3D->ExitFromModal(0x01000001);
        break;
    case 2:
        Refresh();
        break;
    case 3: // address book
        AddressBookAdd();
        break;
    case 4:
        bPrivate = !bPrivate;
        figures[4]->SetUShift(bPrivate ? (-23 / 256.f) : 0);
        break;
    case 5:
        if (szPassword.IsEmpty())
        {
            strcpy(pszBuffer, szPassword);
            CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(pI3D,
                                                                     szStrings[ENTER_PASSWORD],
                                                                     pszBuffer, 0x00030000,
                                                                     MBF_OK | MBF_CANCEL |
                                                                         MBF_ENTER);
            pI3D->DoModal(pScr);
        }
        else
        {
            szPassword.Empty();
            figures[5]->SetUShift(0);
        }
        break;
    case 6:
        nSortMode = (nSortMode == 1 ? 2 : 1);
        FormSlotsList();
        RedrawSlots();
        break;
    case 7:
        nSortMode = (nSortMode == 3 ? 4 : 3);
        FormSlotsList();
        RedrawSlots();
        break;
    case 8:
        nSortMode = (nSortMode == 5 ? 6 : 5);
        FormSlotsList();
        RedrawSlots();
        break;
    case 9:
        nSortMode = (nSortMode == 7 ? 8 : 7);
        FormSlotsList();
        RedrawSlots();
        break;
    }
    if (ht >= NUM_BUTTONS && ht < NUM_BUTTONS + NUM_ALLODS && nCurrentSlot == 0)
    {
        nAllod = ht - NUM_BUTTONS;
        for (int i = 0; i < NUM_ALLODS; i++)
            figures[6 + i]->SetUShift(0);
        figures[6 + nAllod]->SetUShift(23 / 256.f);
    }
    if (ht >= NUM_BUTTONS + NUM_ALLODS) // another slot
    {
        int n = nPos + ht - NUM_BUTTONS - NUM_ALLODS;
        if (n < slots.size())
        {
            pI3D->BeginFX("buttons\\save\\select.wav");
            nCurrentSlot = n;
            curAddress = slots[nCurrentSlot].addr;
            RedrawSlots();
        }
    }
    CI3DScreen::OnLButtonDown(nFlags, point);
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    int ht = HitTest(point);
    if (ht >= NUM_BUTTONS + NUM_ALLODS) // another slot
    {
        int n = nPos + ht - NUM_BUTTONS - NUM_ALLODS;
        if (n < slots.size())
        {
            nCurrentSlot = n;
            curAddress = slots[nCurrentSlot].addr;
            EnterGame();
        }
    }
    CI3DScreen::OnLButtonDblClk(nFlags, point);
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    CI3DScreen::OnKeyDown(nChar, nRepCnt, nFlags);
    switch (nChar)
    {
    case 13:
        EnterGame();
        SetProcessed();
        break;
    case VK_ESCAPE:
        pI3D->BeginFX("buttons\\messbox\\cancel.wav");
        IWorldConnector.StopLMP();
        pI3D->ExitFromModal(0x01000001);
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
        nCurrentSlot -= CON_NUM_SLOTS_VISIBLE;
        MoveScrollToSelection();
        SetProcessed();
        break;
    case VK_NEXT:
        pI3D->BeginFX("buttons\\save\\select.wav");
        nCurrentSlot += CON_NUM_SLOTS_VISIBLE;
        MoveScrollToSelection();
        SetProcessed();
        break;
    }
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::MoveScrollToSelection()
{
    if (nCurrentSlot < 0)
        nCurrentSlot = 0;
    if (nCurrentSlot >= slots.size())
        nCurrentSlot = slots.size() - 1;
    curAddress = slots[nCurrentSlot].addr;
    if (nPos + CON_NUM_SLOTS_VISIBLE <= nCurrentSlot)
        nPos = nCurrentSlot - CON_NUM_SLOTS_VISIBLE + 1;
    if (nPos > nCurrentSlot)
        nPos = nCurrentSlot;
    pScroll->SetPos(nPos);
    pScroll->GetPos(nPos);
    RedrawSlots();
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::EnterGame()
{
    pI3D->BeginFX("buttons\\messbox\\ok.wav");
    // kakoy CRAP!
    if (nCurrentSlot == 0)
    {
        int nMaxPlayers;
        pPlayersSlider->GetPos(nMaxPlayers);
        nMaxPlayers++;
        CString szServerName(pServerName->GetString());
        if (szServerName.IsEmpty())
            szServerName = "?";
        SaveAddressBook();
        //
        IWorldConnector.StartLMPServer(szServerName, nMaxPlayers, (ELMPAllod)nAllod, szPassword,
                                       bPrivate, pChatClient->GetAddressBook().nUniqueClientID);
    }
    else
    {
        if (nCurrentSlot < 0 || nCurrentSlot >= slots.size())
            return;
        if (slots[nCurrentSlot].bWrong)
        {
            CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(pI3D, "lmp_wrong_protocol",
                                                                     "lmp_wrong_protocol_msg",
                                                                     0x00010000, MBF_OK);
            pI3D->DoModal(pScr);
            return;
        }
        SaveAddressBook();
        if (slots[nCurrentSlot].bPassword)
        {
            pszBuffer[0] = 0;
            CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(pI3D,
                                                                     szStrings[ENTER_PASSWORD],
                                                                     pszBuffer, 0x00050000,
                                                                     MBF_OK | MBF_CANCEL |
                                                                         MBF_ENTER);
            pI3D->DoModal(pScr);
        }
        IWorldConnector.ConnectLMPServer(slots[nCurrentSlot].addr, "");
        pChatClient->allod = slots[nCurrentSlot].allod;
    }
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::AddressBookAdd()
{
    if (nCurrentSlot < 0 || nCurrentSlot >= slots.size())
        return;
    if (slots[nCurrentSlot].type != CCSServerInfo::ST_AddressBook)
    {
        pszBuffer[0] = 0;
        if (nCurrentSlot > 0 && nCurrentSlot < slots.size())
            strcpy(pszBuffer, slots[nCurrentSlot].addr.GetFastName().c_str());
        CI3DScreenMessageBox *pScr = dbgnew CI3DScreenMessageBox(pI3D, szStrings[ENTER_IP],
                                                                 pszBuffer, 0x00020000,
                                                                 MBF_OK | MBF_CANCEL | MBF_ENTER);
        pI3D->DoModal(pScr);
    }
    else
    {
        pChatClient->RemoveAddressFromBook(slots[nCurrentSlot].addr);
        SaveAddressBook();
    }
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::Refresh()
{
    strcpy(pszBuffer, "");
    CCSNodeAddress addr;
    addr.SetInetName(theSettings.szMasterServerName, 28004);
    int nClientID = 0;
    if (pChatClient)
        nClientID = pChatClient->GetAddressBook().nUniqueClientID;
    CI3DTCPMessageBox *pScr = dbgnew CI3DTCPMessageBox(pI3D, szStrings[RETRIEVING], pszBuffer,
                                                       0x00040000, addr, nClientID);
    pI3D->DoModal(pScr);
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::SaveAddressBook()
{
    CStructureSaver saver;
    CFileSave file;
    if (file.OpenWrite(IWorld.GetWorkPath() + "config\\addrbook.ini"))
    {
        saver.Start(file, false);
        saver.Add(1, pChatClient->GetAddressBook());
        saver.Finish(file);
        file.CloseFile();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
class CServerDescLessThenFunctional
{
    int nSortMode;

public:
    explicit CServerDescLessThenFunctional(int _nSortMode)
        : nSortMode(_nSortMode)
    {
    }
    bool operator()(const SServerDesc &pSlot1, const SServerDesc &pSlot2) const
    {
        switch (nSortMode)
        {
        case 1:
            return (pSlot1.szName < pSlot2.szName);
        case 2:
            return (pSlot1.szName > pSlot2.szName);
        case 3:
            return (pSlot1.nPlayers < pSlot2.nPlayers);
        case 4:
            return (pSlot1.nPlayers > pSlot2.nPlayers);
        case 5:
            return (pSlot1.allod < pSlot2.allod);
        case 6:
            return (pSlot1.allod > pSlot2.allod);
        case 7:
            return (pSlot1.nPing < pSlot2.nPing);
        case 8:
            return (pSlot1.nPing > pSlot2.nPing);
        default:
            return (pSlot1.type < pSlot2.type);
        }
    }
};
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::FormSlotsList()
{
    slots.clear();

    CCSAddressBook &book = pChatClient->GetAddressBook();
    // CCSClient::CServerInfoList::iterator i;
    SServerDesc *pSlot;
    for (int k = 0;; k++)
    {
        CChatServerInfo *pInfo = (CChatServerInfo *)pChatClient->GetServerInfo(k);
        if (!pInfo)
            break;
        slots.emplace_back();
        SServerDesc *pSlot = &slots.back();
        pSlot->addr = pInfo->addr;
        pSlot->allod = pInfo->allod;
        if (pSlot->allod < 0 || pSlot->allod > LMPA_RIK)
            pSlot->allod = LMPA_GIPAT;
        pSlot->szName = pInfo->szName.c_str();
        pSlot->szZone = pInfo->szQuestName.c_str();
        pSlot->nPlayers = pInfo->nPlayers;
        pSlot->nMaxPlayers = pInfo->nMaxPlayers;
        pSlot->nPing = Float2Int(pInfo->fpPing * 1000);
        pSlot->bWrong = pInfo->bWrongVersion;
        pSlot->type = pInfo->type;
        pSlot->bPassword = pInfo->bUnderPassword;
    }
    CServerDescLessThenFunctional less_func(nSortMode);
    std::sort(slots.begin(), slots.end(), less_func);

    slots.emplace(slots.begin());
    pSlot = &slots.front();
    pSlot->addr.Clear();
    pSlot->allod = LMPA_NONE;
    pSlot->szName = "??";
    pSlot->szZone = "??";
    pSlot->nPlayers = 0;
    pSlot->nMaxPlayers = 0;
    pSlot->nPing = 9999;
    pSlot->bWrong = false;
    pSlot->type = CCSServerInfo::ST_Local;

    nCurrentSlot = 0;
    for (int k = 1; k < slots.size(); k++)
        if (slots[k].addr == curAddress)
            nCurrentSlot = k;
    if (nCurrentSlot == 0)
        curAddress.Clear();

    if (slots.size() > CON_NUM_SLOTS_VISIBLE)
    {
        int n;
        pScroll->GetPos(n);
        pScroll->SetRange(slots.size() - CON_NUM_SLOTS_VISIBLE);
        pScroll->SetPos(n);
    }
    else
        pScroll->SetRange(0);
    nPos = 0;
}
////////////////////////////////////////////////////////////////////////////////
void CI3DScreenConnection::RedrawSlots()
{
    bool bShowControls = (nCurrentSlot == 0);
    pPlayersSlider->Show(bShowControls);
    pServerName->Show(bShowControls);
    pServerName->Activate(bShowControls);
    figures[4]->Show(bShowControls);
    figures[5]->Show(bShowControls);
    if (bShowControls)
    {
        if (!bNewServer)
        {
            areas[4]->SetToolTip(40612);
            areas[5]->SetToolTip(40613);
        }
    }
    else
    {
        areas[4]->SetToolTip(0);
        areas[5]->SetToolTip(0);
    }
    bNewServer = bShowControls;

    CString szTmp;
#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    CDC *pDC = pSurf->GetDC();
    pSurf->Clear();
    int i, n;
    figures[2]->Show(FALSE);

    pSurf->DrawText(szStrings[REFRESH], CRect(390, 345, 520, 365), 1, CON_COLOR_TEXT,
                    DT_CENTER); // refresh
    pSurf->DrawText(szStrings[FAVORITES], CRect(390, 369, 520, 389), 1, CON_COLOR_TEXT,
                    DT_CENTER); // add/remove favorites

    CRect rc(20, 8, 290, 29);
    CPoint shift(0, 24);

    pSurf->DrawText(szStrings[HD_NAME], rc, 1, CON_COLOR_TEXT, DT_LEFT);
    rc.left = 150;
    pSurf->DrawText(szStrings[HD_PLAYERS], rc, 1, CON_COLOR_TEXT, DT_LEFT);
    rc.left = 178;
    pSurf->DrawText(szStrings[HD_ALLOD], rc, 1, CON_COLOR_TEXT, DT_LEFT);
    rc.left = 248;
    pSurf->DrawText(szStrings[HD_PING], rc, 1, CON_COLOR_TEXT, DT_LEFT);
    rc.left = 12;
    rc.right = 265;

    if (nSortMode % 2)
        figures[3]->MakeBar(4, 100, 0, 0, 10, 12, 40, 176, 80, 128);
    else
        figures[3]->MakeBar(4, 100, 0, 0, 10, 12, 40, 128, 80, 176);
    if (nSortMode)
    {
        figures[3]->Show(TRUE);
        float x;
        switch ((nSortMode - 1) / 2)
        {
        case 0:
            x = 112;
            break;
        case 1:
            x = 242;
            break;
        case 2:
            x = 270;
            break;
        case 3:
            x = 340;
            break;
        }
        figures[3]->SetRelativeToScreen(x, 116, 4);
    }

    CPen pen(PS_SOLID, 1, CON_COLOR_TEXT);
    CPen *pOldPen = pDC->SelectObject(&pen);
    CPoint p1(8, rc.bottom - 2);
    CPoint p2(270, rc.bottom - 2);
    pI3D->VirtualToScreenLocal(p1);
    pI3D->VirtualToScreenLocal(p2);
    pDC->MoveTo(p1.x, p1.y);
    pDC->LineTo(p2.x, p2.y);

    rc += CPoint(0, 27);

    for (i = 0, n = nPos; i < CON_NUM_SLOTS_VISIBLE && n < slots.size(); i++, n++)
    {
        if (n == nCurrentSlot)
        {
            figures[2]->Show(TRUE);
            figures[2]->SetRelativeToScreen(239, 144 + i * 24, 3);
        }

        SServerDesc &slot = slots[n];
        /*
                        if ( slot.bWrong )
                                CON_COLOR_TEXT = RGB( 130, 130, 130 );
                        else
        */
        if (slot.type == CCSServerInfo::ST_AddressBook)
            CON_COLOR_TEXT = RGB(255, 179, 49);
        else if (slot.type == CCSServerInfo::ST_Master)
            CON_COLOR_TEXT = RGB(238, 227, 49);
        else
            CON_COLOR_TEXT = RGB(228, 215, 167);

        if (n == 0) // create server
        {
            szTmp.Format("%s %s", szStrings[NEW_SERVER], pServerName->GetString());
            pSurf->DrawText(szTmp, rc, 1, CON_COLOR_TEXT, DT_LEFT | DT_NOPREFIX);
        }
        else
        {
            pSurf->DrawText(slot.szName, rc, 1, CON_COLOR_TEXT, DT_LEFT | DT_NOPREFIX);
            rc.left = 142;
            szTmp.Format("%d/%d", slot.nPlayers, slot.nMaxPlayers);
            pSurf->DrawText(szTmp, rc, 1, CON_COLOR_TEXT, DT_LEFT);
            rc.left = 170;
            if (slot.allod == LMPA_NONE)
                szTmp = "?";
            else
                szTmp.Format("%s", szAllodNames[slot.allod]);
            pSurf->DrawText(szTmp, rc, 1, CON_COLOR_TEXT, DT_LEFT);
            rc.left = 220; // 238;
            szTmp.Format("%d", slot.nPing);
            pSurf->DrawText(szTmp, rc, 1, CON_COLOR_TEXT, DT_RIGHT);
            rc.left = 12;
        }
        rc += shift;
    }

    for (i = 0; i < NUM_ALLODS; i++)
        figures[6 + i]->Show(FALSE);

    if (nCurrentSlot < 0 || nCurrentSlot >= slots.size())
    {
        pDC->SelectObject(pOldPen);
        pSurf->ReleaseDC();
        return;
    }

    CON_COLOR_TEXT = RGB(228, 215, 167);

    SServerDesc *pSlot = &slots[nCurrentSlot];
    rc = CRect(340, 20, 590, 44);

    if (nCurrentSlot == 0)
    {
        for (i = 0; i < NUM_ALLODS; i++)
            figures[6 + i]->Show(TRUE);
        pSurf->DrawText(szStrings[SERVER], rc, 1, CON_COLOR_TEXT, DT_LEFT);
        rc += shift;
        rc += shift;
        szTmp.Format("%s %d", szStrings[MAX_PLAYERS], nNewMaxPlayers);
        pSurf->DrawText(szTmp, rc, 1, CON_COLOR_TEXT, DT_LEFT);
        rc += shift;
        rc += shift;
        pSurf->DrawText(szStrings[ALLOD], rc, 1, CON_COLOR_TEXT, DT_LEFT);
        rc += shift;
        rc.left = 390;
        for (int i = 0; i < NUM_ALLODS; i++)
        {
            pSurf->DrawText(szAllodNames[i], rc, 1, CON_COLOR_TEXT, DT_LEFT);
            rc += shift;
        }
        rc.left = 370;
        rc += CPoint(0, 12);
        pSurf->DrawText(szStrings[PRIVATE], rc, 1, CON_COLOR_TEXT, DT_LEFT);
        rc += shift;
        pSurf->DrawText(szStrings[PASSWORD], rc, 1, CON_COLOR_TEXT, DT_LEFT);
    }
    else
    {
        szTmp.Format("%s %s", szStrings[SERVER], pSlot->szName);
        pSurf->DrawText(szTmp, rc, 1, CON_COLOR_TEXT, DT_LEFT);
        rc += shift;
        szTmp.Format("%s %d / %d", szStrings[PLAYERS], pSlot->nPlayers, pSlot->nMaxPlayers);
        pSurf->DrawText(szTmp, rc, 1, CON_COLOR_TEXT, DT_LEFT);
        rc += shift;
        if (pSlot->allod == LMPA_NONE)
            szTmp = szStrings[ALLOD] + " ?";
        else
            szTmp.Format("%s %s", szStrings[ALLOD], szAllodNames[pSlot->allod]);
        pSurf->DrawText(szTmp, rc, 1, CON_COLOR_TEXT, DT_LEFT);
        rc += shift;
        szTmp.Format("%s %s", szStrings[QUEST], pSlot->szZone);
        int nLines = pSurf->DrawMultiText(szTmp, rc, 3, 1, CON_COLOR_TEXT, DT_LEFT);
        while (nLines--)
            rc += shift;
        szTmp.Format("%s %s", szStrings[IP], pSlot->addr.GetFastName().c_str());
        pSurf->DrawText(szTmp, rc, 1, CON_COLOR_TEXT, DT_LEFT);
        rc += shift;
        szTmp.Format("%s %d", szStrings[PING], pSlot->nPing);
        pSurf->DrawText(szTmp, rc, 1, CON_COLOR_TEXT, DT_LEFT);
        rc += shift;
        if (pSlot->bPassword)
        {
            pSurf->DrawText(szStrings[IS_PASSWORD], rc, 1, CON_COLOR_TEXT, DT_LEFT);
            rc += shift;
        }
    }
    pDC->SelectObject(pOldPen);
    pSurf->ReleaseDC();
#endif // NOGETDC
}
////////////////////////////////////////////////////////////////////////////////
CI3DTCPMessageBox::CI3DTCPMessageBox(CInterface3D *pI, LPCTSTR pszTitleID, LPTSTR pszTextID,
                                     DWORD dwPar, const CCSNodeAddress &addr,
                                     unsigned int nClientID)
    : CI3DScreenMessageBox(pI, pszTitleID, pszTextID, dwPar, MBF_CANCEL)
    , request(addr)
{
    pProgress = AddFigure();
    pProgress->SetTexture(0);
    pProgress->UseDefaultColors(TRUE);
    pProgress->SetDefaultColor(RGBA_MAKE(160, 104, 0, 255));

    request.request.Write(&nClientID, 4);
}
////////////////////////////////////////////////////////////////////////////////
void CI3DTCPMessageBox::OnIdle()
{
    float percent = pChatClient->GetMSReplyProgress(request.res);
    pProgress->MakeBar(2.5f, 100, 220, 280, 225 + 355 * percent, 300);

    if (request.GetState() == CTCPRequest::COMPLETE)
        pChatClient->ProcessMasterServerReply(request.res);
    if (request.GetState() == CTCPRequest::COMPLETE || request.GetState() == CTCPRequest::FAILURE)
        pI3D->ExitFromModal(0x00040002);
}
/////////////////////////////////////////////////////////////////////////////////////

#endif // MASTER_SERVER
