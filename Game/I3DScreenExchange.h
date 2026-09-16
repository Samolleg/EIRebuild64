#pragma once

#include "I3DScreenConstructor.h"
/////////////////////////////////////////////////////////////////////////////////////
class CParty;
class CI3DEditBox;
/////////////////////////////////////////////////////////////////////////////////////
class CI3DScreenExchange : public CI3DItemScreen
{
private:
    CParty *pItemsOpp;
    CParty *pItemsOppTrade;
    CParty *pItemsOurTrade;

    CI3DObjectInventoryItems *pOurWagon;
    CI3DObjectInventoryFaces *pOppWagon;
    CI3DTradeTable *pTradeItems;

    CI3DEditBox *pMoney;
    std::vector<CString> faces;

public:
    CI3DScreenExchange(CInterface3D *pI)
        : CI3DItemScreen(pI)
    {
    }
    virtual void OnActivate();
    virtual void OnDeactivate();
    virtual void OnExitFromModal(DWORD dwRetValue);

    virtual void LMPItemXCHGComplete(CParty &itemsRemote, CParty &itemsOffer, CParty &itemsInv);

    virtual bool CanClickOnItem(CItem *pItem, EItemScreenParts eFrom);
    virtual void ClickOnItem(CItem *pItem, EItemScreenParts eFrom);

    virtual void OnIdle();
    virtual BOOL Draw(CRenderer *pRend);
    virtual void OnLButtonDown(UINT nFlags, CPoint point);
    virtual void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
};
