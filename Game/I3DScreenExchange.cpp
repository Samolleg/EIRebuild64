#include "StdAfx.h"
#include "CorePerf.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DScreenExchange.h"
#include "WorldInterface.h"
#include "A3Cursor.h"
#include "ChatServer.h"
#include "TextManager.h"
#include "I3DScrollBar.h"
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenExchange::OnActivate()
{
    AddTexture("campinfo");

    CI3DPolyArea *pArea;
    pArea = AddArea();
    pArea->MakeRect(12, 450, 52, 490);
    pArea->SetToolTip(41000);
    pArea = AddArea();
    pArea->MakeRect(148, 450, 188, 490);
    pArea->SetToolTip(41001);
    pArea = AddArea();
    pArea->MakeRect(740, 460, 780, 490);
    pArea->SetToolTip(41003);

    CI3DFigure *pFig;
    // ok
    pFig = AddFigure();
    pFig->MakeBar(3, 100, 12, 450, 52, 490, 165, 172, 205, 212);
    // cancel
    pFig = AddFigure();
    pFig->MakeBar(3, 100, 148, 450, 188, 490, 165, 214, 205, 254);
    // ok opp
    pFig = AddFigure();
    pFig->MakeBar(3, 100, 12, 150, 52, 190, 165, 172, 205, 212);
    pFig->Show(FALSE);
    // exit
    pFig = AddFigure();
    pFig->MakeBar(3, 100, 740, 460, 780, 490, 125, 224, 165, 254);
    pFig->SetAmbient(.5f, .5f, .5f);

    AddBackground();
    pCampBG->SetHeight(300);

    pItemsOpp = dbgnew CParty();
    pItemsOpp->Create();
    pItemsOurTrade = &pChatClient->itemsOffer;
    pItemsOppTrade = &pChatClient->itemsRemote;
    pItemsOurTrade->Clear();

    AddObject(pItemInfo = dbgnew CI3DObjectItemInfo(pI3D, this));
    pItemInfo->Init(600, 100);

    AddObject(pOurWagon = dbgnew CI3DObjectInventoryItems(pI3D, this));
    pOurWagon->Init(&IWorldClient.GetParty().allWagonItems);
    pOurWagon->SetPartType(ISP_WAGON);
    pOurWagon->SetSortType(0);

    AddObject(pOppWagon = dbgnew CI3DObjectInventoryFaces(pI3D, this));
    pOppWagon->Init(&faces, true, true);

    AddObject(pTradeItems = dbgnew CI3DTradeTable(pI3D, this));
    pTradeItems->Init(&pItemsOppTrade->allWagonItems, &pItemsOurTrade->allWagonItems, false, true);
    pTradeItems->SetPartType(ISP_ITEMTRADE);

    AddObject(pMoney = dbgnew CI3DEditBox(pI3D, CRect(0, 350, 200, 366)));
    pMoney->SetParams(EBF_DIGITS, true, 1, 9, RGB(255, 255, 255), RGB(255, 179, 49));
    pMoney->SetString("");

    AddSurface(dbgnew CI3DSurface(pI3D, 0, 410, 200, 450));
    AddSurface(dbgnew CI3DSurface(pI3D, 0, 310, 200, 350));
    AddSurface(dbgnew CI3DSurface(pI3D, 0, 210, 200, 270));
    AddSurface(dbgnew CI3DSurface(pI3D, 0, 110, 200, 170));
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenExchange::OnDeactivate()
{
    pItemInfo->SetItem(0);
    if (pItemsOpp)
        delete pItemsOpp;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenExchange::OnExitFromModal(DWORD dwRetValue)
{
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenExchange::OnIdle()
{
    CI3DScreen::OnIdle();
    if (!pChatClient->pItemXCHG->IsValid())
    {
        MoveAllItems(&IWorldClient.GetParty(), pItemsOurTrade);
        IWorldClient.FinishConstructing();
        return;
    }
    if (pMoney->IsUpdated())
    {
        pItemsOurTrade->nMoney = atoi(pMoney->GetString());
        pChatClient->UpdateOfferItems();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DScreenExchange::Draw(CRenderer *pRend)
{
    CorePerf::Scope coreTiming(CorePerf::ScreenExchange);
    CPoint point = theCursor.GetPos();

    pMouseItem = 0;
    int nSlotType = 0;

    if (!pMouseItem && !nSlotType)
        pMouseItem = pOurWagon->GetMouseItem(point, nSlotType);
    //	if ( !pMouseItem && !nSlotType )
    //		pMouseItem = pOppWagon->GetMouseItem( point, nSlotType );
    if (!pMouseItem && !nSlotType)
        pMouseItem = pTradeItems->GetMouseItem(point, nSlotType);

    if (pMouseItem)
    {
        if (pItemInfo)
            pItemInfo->SetItem(pMouseItem);
    }
    else if (nSlotType)
    {
        if (pItemInfo)
            pItemInfo->SetHelp(nSlotType);
    }
    else
    {
        if (pItemInfo)
            pItemInfo->SetItem(0);
    }

    CTextManager *pTxtMng = IWorld.GetTextManager();
    CString szTmp, szOut;
#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    pSurf->GetDC();
    pSurf->Clear();
    pTxtMng->GetSystemString("xch_current_money", szTmp);
    szOut.Format("%s %d", szTmp, IWorldClient.GetParty().nMoney);
    pSurf->DrawText(szOut, CRect(0, 0, 200, 20), 1, RGB(255, 255, 255), DT_CENTER);
    pTxtMng->GetSystemString("xch_accept", szTmp);
    pSurf->DrawText(szTmp, CRect(0, 20, 200, 40), 1, RGB(255, 255, 255), DT_CENTER);
    pSurf->ReleaseDC();

    pSurf = surfaces[1];
    CDC *pDC = pSurf->GetDC();
    pSurf->Clear();
    pTxtMng->GetSystemString("xch_money_offer", szTmp);
    pSurf->DrawMultiText(szTmp, CRect(20, 0, 180, 20), 2, 1, RGB(255, 255, 255), DT_CENTER);
    pSurf->ReleaseDC();

    pSurf = surfaces[2];
    pSurf->GetDC();
    pSurf->Clear();
    if (pChatClient->GetPeerTrade()->IsValid())
    {
        pTxtMng->GetSystemString("xch_money_remote", szTmp);
        pSurf->DrawMultiText(szTmp, CRect(20, 0, 180, 20), 2, 1, RGB(255, 255, 255), DT_CENTER);
        szOut.Format("%d", pItemsOppTrade->nMoney);
        pSurf->DrawText(szOut, CRect(0, 40, 200, 60), 1, RGB(255, 255, 255), DT_CENTER);
    }
    pSurf->ReleaseDC();

    pSurf = surfaces[3];
    pSurf->GetDC();
    pSurf->Clear();
    if (pChatClient->GetPeerTrade()->IsValid())
    {
        pTxtMng->GetSystemString("xch_remote_header", szTmp);
        pSurf->DrawMultiText(szTmp, CRect(20, 0, 180, 20), 2, 1, RGB(255, 255, 255), DT_CENTER);
        if (pChatClient->GetPeerTrade()->bOkTrade)
            pTxtMng->GetSystemString("xch_remote_ok", szTmp);
        else
            pTxtMng->GetSystemString("xch_remote_cancel", szTmp);
        pSurf->DrawText(szTmp, CRect(0, 40, 200, 60), 1, RGB(228, 215, 167), DT_CENTER);
    }
    pSurf->ReleaseDC();
#endif // NOGETDC

    if (pChatClient->pProposal->IsValid() && pChatClient->pProposal->bOkTrade)
    {
        figures[0]->SetAmbient(.5f, .5f, .5f);
        figures[1]->SetAmbient(1, 1, 1);
    }
    else
    {
        figures[0]->SetAmbient(1, 1, 1);
        figures[1]->SetAmbient(.5f, .5f, .5f);
    }
    if (pChatClient->GetPeerTrade()->IsValid() && pChatClient->GetPeerTrade()->bOkTrade)
        figures[2]->SetAmbient(.5f, .5f, .5f);
    else
        figures[2]->SetAmbient(1, 1, 1);
    CI3DScreen::Draw(pRend);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenExchange::OnLButtonDown(UINT nFlags, CPoint point)
{
    CI3DScreen::OnLButtonDown(nFlags, point);
    int ht = HitTest(point);
    switch (ht)
    {
    case 0:
        if (pChatClient->pProposal->IsValid() && !pChatClient->pProposal->bOkTrade &&
            pItemsOurTrade->nMoney <= IWorldClient.GetParty().nMoney)
        {
            pI3D->BeginFX("buttons\\camp\\buy.wav");
            pChatClient->TradeOk(IWorldClient.GetParty());
        }
        else
            pI3D->BeginFX("buttons\\messbox\\cancel.wav");
        break;
    case 1:
        if (pChatClient->pProposal->IsValid() && pChatClient->pProposal->bOkTrade)
        {
            pI3D->BeginFX("buttons\\messbox\\cancel.wav");
            pChatClient->ResetTradeOk();
        }
        break;
    case 2:
        pI3D->BeginFX("buttons\\globalmap\\transit.wav");
        pChatClient->CancelTradeRequest();
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenExchange::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    CI3DScreen::OnKeyDown(nChar, nRepCnt, nFlags);
    if (GetProcessed())
        return;
    switch (nChar)
    {
    case 13:
        pI3D->BeginChatLine();
        SetProcessed();
        return;
    case VK_BACK:
        pI3D->ClearChatLines();
        SetProcessed();
        return;
    case VK_ESCAPE:
        pChatClient->CancelTradeRequest();
        SetProcessed();
        return;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenExchange::ClickOnItem(CItem *pItem, EItemScreenParts eFrom)
{
    if (!CanClickOnItem(pItem, eFrom))
    {
        pI3D->BeginFX("buttons\\messbox\\cancel.wav");
        return;
    }
    switch (eFrom)
    {
    case ISP_WAGON:
        pI3D->BeginFX("buttons\\camp\\put_on.wav");
        MoveItem(pItem, pItemsOurTrade, &IWorldClient.GetParty(), 8);
        pChatClient->UpdateOfferItems();
        break;

    case ISP_ITEMTRADE: {
        for (int i = 0; i < pItemsOurTrade->allWagonItems.GetSize(); i++)
            if (pItem == pItemsOurTrade->allWagonItems[i])
            {
                pI3D->BeginFX("buttons\\camp\\put_off.wav");
                pOurWagon->SetToItem(MoveItem(pItem, &IWorldClient.GetParty(), pItemsOurTrade));
                pChatClient->UpdateOfferItems();
                break;
            }
    }
    break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
bool CI3DScreenExchange::CanClickOnItem(CItem *pItem, EItemScreenParts eFrom)
{
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenExchange::LMPItemXCHGComplete(CParty &itemsRemote, CParty &itemsOffer,
                                             CParty &itemsInv)
{
    IWorldClient.GetParty().RemoveAllItems();
    MoveAllItems(&IWorldClient.GetParty(), &itemsInv);
    MoveAllItems(&IWorldClient.GetParty(), &itemsRemote);
    IWorldClient.GetParty().nMoney = itemsInv.nMoney + itemsRemote.nMoney - itemsOffer.nMoney;
    IWorldClient.FinishConstructing();
}
/////////////////////////////////////////////////////////////////////////////////////
