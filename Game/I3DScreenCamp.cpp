#include "StdAfx.h"
#include "OwnedRender/E4Camp.h"
#include "GameJournal.h"
#include "ModPackage.h"
#include "ModRuntime.h"
#include "ModStatRules.h"
#include "ModCampEquipment.h"
#include "ModCampRepair.h"
#include "ModCampItemPrice.h"
#include "ModCampSale.h"
#include "ModAlchemyItem.h"
#include "Object3DClientSpecific.h"
#include "WideScreen.h"
static const char LOCAL_FILE[] = __FILE__;
namespace { void RefreshCampEquipmentPose(CUnit*, CPersonage*, bool); }
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DScreenCamp.h"
#include "ModProgression.h"
#include "I3DMain.h"
#include "I3DScreenGlobalMap.h"
#include "I3DScreenMessageBox.h"
#include "I3DCursor.h"
#include "Party.h"
#include "Unit.h"
#include "WorldClient.h"
#include "WorldInterface.h"
#include "ScenarioTracker.h"
#include "MusicSystem.h"
#include "randomGen.h"
#include "MapObjectManager.h"
#include "ItemManager.h"
#include "SpellManager.h"
#include "SpellConstructor.h"
#include "StringProcessing.h"
#include "TextManager.h"
#include "ItemConstructor.h"
#include "aiSettings.h"
#include "Renderer.h"
/////////////////////////////////////////////////////////////////////////////////////
static bool CampUsesNativeRepair(){
 auto* mod=ModRuntime::Current();return mod&&mod->CombatSetting("JMVPerks");
}
static bool CampRepairMetal(CItem* item){
 return theItemDatabase.materials[item->GetMaterial()]->szType.CompareNoCase("Metal")==0;
}
const COLORREF CAMP_COLOR_SKILLS = RGB(228, 215, 167);
const COLORREF CAMP_COLOR_HEADER = RGB(255, 255, 255);

const COLORREF CONSTR_COLOR_PRICE = RGB(255, 255, 255);
const COLORREF CONSTR_COLOR_NUMBERS = RGB(255, 255, 255);
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::OnActivate()
{
    struct SFieldCampCommerceGuard {
        bool active;
        SFieldCampCommerceGuard() : active(IWorldClient.BeginFieldCampCommerceSuppression()) {}
        ~SFieldCampCommerceGuard() { IWorldClient.EndFieldCampCommerceSuppression(active); }
    } commerceGuard;
    bIsSpellConstr = false;
    bIsItemConstr = false;
    pSpellsAssort = 0;
    pItemsAssort = 0;

    IWorld.CombatPause(true);
    //
    pShop = 0;
    // get current shop
    int nShop = (int)IWorldClient.GSGetVar("constr.current");
    GameJournal::Event("SHOP", "select", "constr.current=%d suppressed=%d", nShop, commerceGuard.active);
    if (nShop > 0)
    {
        pShop = theHat.shops.GetShop(nShop);
        if (!pShop)
        {
            IWorldClient.FinishConstructing();
            return;
        }
        bIsSpellConstr = pShop->bSpell;
        bIsItemConstr = pShop->bItem;
        for (int i = 0; i < ISK_TOTAL; i++)
            fPriceKoeffs[i] = pShop->fKoeffs[i];
        pSpellsAssort = &pShop->spellAssort;
        pItemsAssort = &pShop->itemAssort;
    }

    // CRAP{
    bCrapNoConstr = (IWorldClient.GSGetVar("i.noconstr") == 1.f);
    if (IWorldClient.GSGetVar("i.offspellconstr") == 1.f)
        bIsSpellConstr = false;
    if (IWorldClient.GSGetVar("i.onspellconstr") == 1.f)
        bIsSpellConstr = true;
    if (IWorldClient.GSGetVar("i.offitemconstr") == 1.f)
        bIsItemConstr = false;
    if (IWorldClient.GSGetVar("i.onitemconstr") == 1.f)
        bIsItemConstr = true;
    // CRAP}

    AddTexture("campinfo");

    CI3DPolyArea *pArea;

    pArea = AddArea();
    pArea->MakeRect(12, 450, 52, 490);
    pArea->SetToolTip(20200);
    pArea = AddArea();
    pArea->MakeRect(148, 450, 188, 490);
    pArea->SetToolTip(20201);

    pArea = AddArea(); // rotate pers
    pArea->MakeRect(300, 470, 330, 500);
    pArea = AddArea(); // rotate pers
    pArea->MakeRect(470, 470, 500, 500);

    // weapons
    pArea = AddArea();
    pArea->MakeRect(620, 430, 660, 460);
    pArea->SetToolTip(20100);
    // spells
    pArea = AddArea();
    pArea->MakeRect(620, 460, 660, 490);
    pArea->SetToolTip(20101);
    // trade spell
    pArea = AddArea();
    pArea->MakeRect(660, 460, 700, 490);
    if (bIsSpellConstr)
        pArea->SetToolTip(20105);
    // constr spell
    pArea = AddArea();
    pArea->MakeRect(700, 460, 740, 490);
    if (bIsSpellConstr && !bCrapNoConstr)
        pArea->SetToolTip(20106);
    // trade item
    pArea = AddArea();
    pArea->MakeRect(660, 430, 700, 460);
    if (bIsItemConstr)
        pArea->SetToolTip(20102);
    // constr item
    pArea = AddArea();
    pArea->MakeRect(700, 430, 740, 460);
    if (bIsItemConstr && !bCrapNoConstr)
        pArea->SetToolTip(20103);
    // repair item
    pArea = AddArea();
    pArea->MakeRect(740, 430, 780, 460);
    if (bIsItemConstr)
        pArea->SetToolTip(20104);
    // exit
    pArea = AddArea();
    pArea->MakeRect(740, 460, 780, 490);
    pArea->SetToolTip(20107);

    CI3DFigure *pFig;

    // ok
    pFig = AddFigure();
    pFig->MakeBar(3, 100, 12, 450, 52, 490, 165, 172, 205, 212);

    // cancel
    pFig = AddFigure();
    pFig->MakeBar(3, 100, 148, 450, 188, 490, 165, 214, 205, 254);

    // weapons
    pFig = AddFigure();
    pFig->MakeBar(3, 100, 620, 430, 660, 460, 2, 193, 42, 223);
    // spells
    pFig = AddFigure();
    pFig->MakeBar(3, 100, 620, 460, 660, 490, 2, 224, 42, 254);

    // trade spell
    pFig = AddFigure();
    pFig->MakeBar(3, 100, 660, 460, 700, 490, 43, 224, 83, 254);
    pFig->Show(bIsSpellConstr);
    // constr spell
    pFig = AddFigure();
    pFig->MakeBar(3, 100, 700, 460, 740, 490, 84, 224, 124, 254);
    pFig->Show(bIsSpellConstr);
    if (bCrapNoConstr)
        pFig->Show(FALSE);

    // trade item
    pFig = AddFigure();
    pFig->MakeBar(3, 100, 660, 430, 700, 460, 43, 193, 83, 223);
    pFig->Show(bIsItemConstr);
    // constr item
    pFig = AddFigure();
    pFig->MakeBar(3, 100, 700, 430, 740, 460, 84, 193, 124, 223);
    pFig->Show(bIsItemConstr);
    if (bCrapNoConstr)
        pFig->Show(FALSE);
    // repair item
    pFig = AddFigure();
    pFig->MakeBar(3, 100, 740, 430, 780, 460, 125, 193, 165, 223);
    pFig->Show(bIsItemConstr);

    // exit
    pFig = AddFigure();
    pFig->MakeBar(3, 100, 740, 460, 780, 490, 125, 224, 165, 254);
    pFig->SetAmbient(.5f, .5f, .5f);

    AddBackground();

    AddSurface(dbgnew CI3DSurface(pI3D, 0, 400, 200, 500));
    AddSurface(dbgnew CI3DSurface(pI3D, 600, 400, 800, 430));

    // pSpellsAssort = dbgnew CParty();
    // pSpellsAssort->Create();
    pSpellsBuy = dbgnew CParty();
    pSpellsBuy->Create();
    pSpellsSell = dbgnew CParty();
    pSpellsSell->Create();
    pSpellsConstr = dbgnew CParty();
    pSpellsConstr->Create();

    // pItemsAssort = dbgnew CParty();
    // pItemsAssort->Create();
    pItemsBuy = dbgnew CParty();
    pItemsBuy->Create();
    pItemsSell = dbgnew CParty();
    pItemsSell->Create();
    pItemsConstr = dbgnew CParty();
    pItemsConstr->Create();
    pItemsRepair = dbgnew CParty();
    pItemsRepair->Create();

    pConstrItem = 0;
    pConstrSpell = 0;

    AddObject(pItemInfo = dbgnew CI3DObjectItemInfo(pI3D, this));
    pItemInfo->Init(600, 100);

    AddObject(pNameStats = dbgnew CI3DObjectNameStats(pI3D, this));
    pNameStats->SetTexture(textures[0]);
    pNameStats->Init();

    AddObject(pConstrInfo = dbgnew CI3DObjectItemInfo(pI3D, this));
    pConstrInfo->Init(0, 100);

    AddObject(pWagon = dbgnew CI3DObjectInventoryItems(pI3D, this));
    pWagon->Init(&IWorldClient.GetParty().allWagonItems);
    pWagon->SetPartType(ISP_WAGON);
    pWagon->SetSortType(0);

    AddObject(pSlots = dbgnew CI3DObjectQuickSlots(pI3D, this));
    pSlots->SetTexture(textures[2]);
    pSlots->Init();

    AddObject(pSpells = dbgnew CI3DObjectSpellSlots(pI3D, this));
    pSpells->SetTexture(textures[2]);
    pSpells->Init();

    AddObject(pPerks = dbgnew CI3DObjectPerks(pI3D, this));
    pPerks->SetTexture(textures[0]);
    pPerks->Init();

    if (bIsSpellConstr)
    {
        AddObject(pAssortSpells = dbgnew CI3DObjectInventoryItems(pI3D, this));
        pAssortSpells->Init(&pSpellsAssort->allWagonItems, true);
        pAssortSpells->SetPartType(ISP_SPELLASSORT);
        pAssortSpells->SetSortType(1);
        pAssortSpells->SetPriceType(ISK_SPELL_BUY);

        AddObject(pTradeSpells = dbgnew CI3DTradeTable(pI3D, this));
        pTradeSpells->Init(&pSpellsBuy->allWagonItems, &pSpellsSell->allWagonItems, true);
        pTradeSpells->SetPartType(ISP_SPELLTRADE);

        AddObject(pConstrSpells = dbgnew CI3DConstrSpell(pI3D, this));
        pConstrSpells->Init(pSpellsConstr);
    }

    if (bIsItemConstr)
    {
        AddObject(pAssortItems = dbgnew CI3DObjectInventoryItems(pI3D, this));
        pAssortItems->Init(&pItemsAssort->allWagonItems, true);
        pAssortItems->SetPartType(ISP_ITEMASSORT);
        pAssortItems->SetSortType(2);
        pAssortItems->SetPriceType(ISK_ITEM_BUY);

        AddObject(pTradeItems = dbgnew CI3DTradeTable(pI3D, this));
        pTradeItems->Init(&pItemsBuy->allWagonItems, &pItemsSell->allWagonItems, false);
        pTradeItems->SetPartType(ISP_ITEMTRADE);

        AddObject(pConstrItems = dbgnew CI3DConstrItem(pI3D, this));
        pConstrItems->Init(pItemsConstr);

        AddObject(pRepairItems = dbgnew CI3DRepairTable(pI3D, this));
        pRepairItems->Init(&pItemsRepair->allWagonItems);
    }

    fPersAngle = 0;
    nLock = 0;

    // deploy
    CPersonages personages;
    CParty &party = IWorldClient.GetParty();
    ASSERT(party.people.GetSize());
    int nAllPeople = party.people.GetSize();

    int i;
    ETypeIDs tid;
    CMapObjectManager *pMobMng = ((CMapObjectManager *)theWorld.GetMapObjectManager());

    // deploy only human personages
    for (i = 0; i < nAllPeople; i++)
    {
        party.people[i].nID += 100000000;
        tid = pMobMng->GetTypeIDFromPrototype(party.people[i].szPrototype);
        if (tid == MOTID_HUMAN)
            personages.Add(&party.people[i]);
    }
    IWorldClient.DeployClientPersonages(personages);

    // all non-human pUnits will be zeros
    pUnits.SetSize(nAllPeople);
    for (i = 0; i < nAllPeople; i++)
        pUnits[i] = 0;

    for (i = 0; i < nAllPeople; i++)
    {
        tid = pMobMng->GetTypeIDFromPrototype(party.people[i].szPrototype);
        if (tid == MOTID_HUMAN)
        {
            pUnits[i] = IWorldClient.GetUnit(party.people[i].nID);
            const SADEntry *pEntry = pUnits[i]->GetSpecialAnimationEntry(33);
            if (pEntry)
                pUnits[i]->LowStartAnimationExtended(0xffffffffu, pEntry->dwEntryIndex,
                                             (timeGetTime() / 1024.0f) * 15 - random.Get(0, 50), 0);
            pUnits[i]->ChangeWeapon(-1);
            pUnits[i]->GetUnitSpecific()->NotifyWeaponUpdated();
            pUnits[i]->GetUnitSpecific()->NewAnimationWasReceived((timeGetTime() / 1024.0f) * 15);

            pUnits[i]->SetupCampUnitWithUnitStats(&party.people[i].stats);
            pUnits[i]->GetUnitSpecific()->NotifyHitPointsUpdated();

            party.people[i].stats.RemoveAllArmors();
            for (int j = 0; j < party.people[i].armors.GetSize(); j++)
                party.people[i].stats.Wear(party.people[i].armors[j]);
        }
    }

    for (i = 0; i < nAllPeople; i++)
        party.people[i].nID -= 100000000;

    nCurPers = -1;
    NextPersonage();

    CTextManager *pTxtMng = IWorld.GetTextManager();
    CString szMode;
    pTxtMng->GetSystemString("mode_weapons", szMode);
    szModes.Add(szMode);
    pTxtMng->GetSystemString("mode_spells", szMode);
    szModes.Add(szMode);
    pTxtMng->GetSystemString("mode_spelltrade", szMode);
    szModes.Add(szMode);
    pTxtMng->GetSystemString("mode_spellconstr", szMode);
    szModes.Add(szMode);
    pTxtMng->GetSystemString("mode_itemtrade", szMode);
    szModes.Add(szMode);
    pTxtMng->GetSystemString("mode_itemconstr", szMode);
    szModes.Add(szMode);
    pTxtMng->GetSystemString("mode_itemrepair", szMode);
    szModes.Add(szMode);
    szModes.Add("quit");

    if (bIsItemConstr)
        eScreenMode = ISM_ITEMTRADE;
    else if (bIsSpellConstr)
        eScreenMode = ISM_SPELLTRADE;
    else
        eScreenMode = ISM_WEAPONS;
    GameJournal::Event("SHOP", "resolved", "shop=%d found=%d item_trade=%d spell_trade=%d", nShop, pShop != nullptr, bIsItemConstr, bIsSpellConstr);
    SetScreenMode(eScreenMode);

    CheckValidTransaction();
#ifdef NODEBUGINFO
    if (pShop && pShop->bNeedUpdate)
#else
    if (pShop)
#endif // NODEBUGINFO
    {
        float fMinLevels[RPG_NUM_MAGIC_SUBTYPES], fMaxLevels[RPG_NUM_MAGIC_SUBTYPES];
        ERPGMagicSubtypes subtype;
        SRPGUnitStats *pStats;
        for (int i = 0; i < RPG_NUM_MAGIC_SUBTYPES; i++)
        {
            fMinLevels[i] = 1e20f;
            fMaxLevels[i] = -1e20f;
            subtype = (ERPGMagicSubtypes)i;
            for (int j = 0; j < IWorldClient.GetParty().people.GetSize(); j++)
            {
                pStats = &IWorldClient.GetParty().people[j].stats;
                float fSkill = pStats->GetMagicSkill(subtype);
                fMinLevels[i] = Min(fMinLevels[i], fSkill);
                fMaxLevels[i] = Max(fMaxLevels[i], fSkill);
            }
        }
        pShop->SetAssortmentParameters(fMinLevels, fMaxLevels);
        pShop->GenerateSpells();
        pShop->GenerateItems();
#ifndef __DEMO__
        pShop->bNeedUpdate = false;
#endif // __DEMO__
    }

    if (auto *mod = ModRuntime::Current())
    {
        if (mod->CombatSetting("JMVShopUpdate"))
        {
            CUnit *current = nCurPers >= 0 && nCurPers < pUnits.GetSize() ? pUnits[nCurPers] : 0;
            if (current)
            {
                if (CPersonage* pers=GetCurrentPers(); pers && int(pers->dwCurrentWeapon)>=0 && pers->dwCurrentWeapon<pers->weapons.GetSize() && pers->weapons[pers->dwCurrentWeapon])
                {
                    RefreshCampEquipmentPose(current,pers,true);
                }
                current->RecalcRPGStats();
            }
            if (pShop)
            {
                const float limit = float(mod->ShopConstructLimit(pShop->nID));
                float minLevels[RPG_NUM_MAGIC_SUBTYPES], maxLevels[RPG_NUM_MAGIC_SUBTYPES];
                for (int i = 0; i < RPG_NUM_MAGIC_SUBTYPES; ++i)
                {
                    minLevels[i] = 1e20f;
                    maxLevels[i] = -1e20f;
                    const ERPGMagicSubtypes subtype = ERPGMagicSubtypes(i);
                    for (int j = 0; j < IWorldClient.GetParty().people.GetSize(); ++j)
                    {
                        const float skill = IWorldClient.GetParty().people[j].stats.GetMagicSkill(subtype);
                        minLevels[i] = Max(Min(minLevels[i], skill), 10.0f);
                        maxLevels[i] = Max(Max(maxLevels[i], skill), limit);
                    }
                }
                pShop->SetAssortmentParameters(minLevels, maxLevels);
                pShop->bNeedUpdate = true;
            }
            for (int i = 0; i < pUnits.GetSize(); ++i)
                if (pUnits[i] && pUnits[i]->GetObjectSpecific())
                    static_cast<CObject3DClientSpecific *>(pUnits[i]->GetObjectSpecific())->ReleaseWounds();
        }
    }
    const CString szMusicZone = IWorldClient.GetCurrentZoneName();
    const SScenarioZone *pZone = theMap.GetZoneByName(szMusicZone);
    const CString szMusicSelector = pZone ?
        (CMusicSystem::UseZoneSpecificMusic() ? szMusicZone : pZone->szAllodName) : CString("gipat");
    GameJournal::Event("music", "camp_selector", "zone=%s selector=%s zone_found=%d",
                       GameJournal::Quote(szMusicZone).c_str(),
                       GameJournal::Quote(szMusicSelector).c_str(), pZone != nullptr);
    theWorld.GetMusicSystem()->SetConstructorMode(szMusicSelector);
    // set antialiasing - artists want it!!!
    /*theRenderer.GetD3D()->GetDeviceDesc(devdesc);
    if (devdesc.dpcTriCaps.dwRasterCaps & D3DPRASTERCAPS_ANTIALIASSORTINDEPENDENT)
        theRenderer.SetRenderState(D3DRENDERSTATE_ANTIALIAS,
                                                    D3DANTIALIAS_SORTINDEPENDENT);*/
    //
    // Current mod has no battle00.x2; use neutral black margins from the reference.
    LPTEXTURE wideTexture = AddTexture("battle00");
    float sideAmbient = ModRuntime::Current()?0.0f:.3f;

    // WideScreen.cpp side fillers; keep the original center layout.
    CI3DFigure *wideLeft = AddFigure();
    wideLeft->MakeBar(2, 100, -float(WideScreen::VirtualShift()), 0, 0, 600, 227, 62, 256, 176);
    wideLeft->SetAmbient(sideAmbient, sideAmbient, sideAmbient);
    CI3DFigure *wideRight = AddFigure();
    wideRight->MakeBar(2, 100, 800, 0, 800 + float(WideScreen::VirtualShift()), 600, 256, 176, 227, 62);
    wideRight->SetAmbient(sideAmbient, sideAmbient, sideAmbient);
    wideLeft->SetTexture(wideTexture);
    wideRight->SetTexture(wideTexture);
    E4CampAccess::Activate(this);
}

/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::OnDeactivate()
{
    E4CampAccess::Deactivate(this);
    IWorld.CombatPause(false);
    // remove antializasing
    theRenderer.SetRenderState(D3DRENDERSTATE_ANTIALIAS, D3DANTIALIAS_NONE);
    // remove
    CPersonages personages;
    CParty &party = IWorldClient.GetParty();
    for (int i = 0; i < party.people.GetSize(); i++)
    {
        party.people[i].nID += 100000000;
        personages.Add(&party.people[i]);
    }
    IWorldClient.RemoveClientPersonages(personages);

    for (int i = 0; i < party.people.GetSize(); i++)
        party.people[i].nID -= 100000000;

    pUnits.RemoveAll();

    if (pConstrSpell)
        pConstrSpell->Release();
    pConstrSpell = 0;

    if (pConstrItem)
        pConstrItem->Release();
    pConstrItem = 0;

    pConstrInfo->SetItem(0);
    pItemInfo->SetItem(0);

    //	pSpellsAssort->Destroy();
    //	delete pSpellsAssort;
    pSpellsBuy->Destroy();
    delete pSpellsBuy;
    pSpellsSell->Destroy();
    delete pSpellsSell;
    pSpellsConstr->Destroy();
    delete pSpellsConstr;

    //	pItemsAssort->Destroy();
    //	delete pItemsAssort;
    pItemsBuy->Destroy();
    delete pItemsBuy;
    pItemsSell->Destroy();
    delete pItemsSell;
    pItemsConstr->Destroy();
    delete pItemsConstr;
    pItemsRepair->Destroy();
    delete pItemsRepair;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::OnExitFromModal(DWORD dwRetValue)
{
    switch (dwRetValue)
    {
    case 0x00010001: {
        CPersonage *pPers = GetCurrentPers();
        if (pPers->weapons.GetSize() > 0)
        {
            CItemWeapon *pWeapon = pPers->weapons[0];
            CItem *pItemRes = IWorldClient.GetParty().AddItemToWagon(pWeapon);
            pPers->RemoveWeapon(pWeapon);
            pPers->dwCurrentWeapon = -1;
            pWagon->SetToItem(pItemRes);
            UpdateWeapons();
        }
    }
    break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
LPCTSTR CI3DScreenCamp::GetTutorialScreenName()
{
    switch (eScreenMode)
    {
    case ISM_WEAPONS:
        return "camp_weapons";
    case ISM_SPELLS:
        return "camp_skills";
    case ISM_SPELLTRADE:
        return "camp_spell_trade";
    case ISM_SPELLCONSTR:
        return "camp_spell_constr";
    case ISM_ITEMTRADE:
        return "camp_item_trade";
    case ISM_ITEMCONSTR:
        return "camp_item_constr";
    case ISM_ITEMREPAIR:
        return "camp_repair";
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::SetScreenMode(EItemScreenModes eNewMode)
{
    GameJournal::Event("SCREEN", "camp_mode", "old=%d old_name=%s new=%d new_name=%s shop=%d item_trade=%d spell_trade=%d", int(eScreenMode), GameJournal::CampMode(int(eScreenMode)), int(eNewMode), GameJournal::CampMode(int(eNewMode)), pShop ? pShop->nID : 0, bIsItemConstr, bIsSpellConstr);
    pSlots->Show(FALSE);
    pSpells->Show(FALSE);
    pPerks->Show(FALSE);

    pNameStats->Show(FALSE);
    pConstrInfo->Show(FALSE);

    if (bIsSpellConstr)
    {
        pTradeSpells->Show(FALSE);
        pConstrSpells->Show(FALSE);
        pAssortSpells->Show(FALSE);
    }

    if (bIsItemConstr)
    {
        pTradeItems->Show(FALSE);
        pRepairItems->Show(FALSE);
        pConstrItems->Show(FALSE);
        pAssortItems->Show(FALSE);
    }

    pWagon->SetPriceType(ISK_TOTAL);
    bool bCanSwitch = true;
    switch (eNewMode)
    {
    case ISM_WEAPONS:
        pNameStats->Show(TRUE);
        pSlots->Show(TRUE);
        break;
    case ISM_SPELLS:
        pNameStats->Show(TRUE);
        pSpells->Show(TRUE);
        pPerks->Show(TRUE);
        break;

    case ISM_SPELLTRADE:
        if (bCanSwitch = bIsSpellConstr)
        {
            pConstrInfo->Show(TRUE);
            pConstrInfo->SetHelp(101);
            pTradeSpells->Show(TRUE);
            pAssortSpells->Show(TRUE);
            pWagon->SetPriceType(ISK_SPELL_SELL);
        }
        break;
    case ISM_SPELLCONSTR:
        if (bCanSwitch = (bIsSpellConstr && !bCrapNoConstr))
        {
            pConstrInfo->Show(TRUE);
            pConstrSpells->Show(TRUE);
            pAssortSpells->Show(TRUE);
        }
        break;

    case ISM_ITEMTRADE:
        if (bCanSwitch = bIsItemConstr)
        {
            pConstrInfo->Show(TRUE);
            pConstrInfo->SetHelp(100);
            pTradeItems->Show(TRUE);
            pAssortItems->Show(TRUE);
            pWagon->SetPriceType(ISK_ITEM_SELL);
        }
        break;
    case ISM_ITEMCONSTR:
        if (bCanSwitch = (bIsItemConstr && !bCrapNoConstr))
        {
            pConstrInfo->Show(TRUE);
            pConstrItems->Show(TRUE);
            pAssortItems->Show(TRUE);
        }
        break;
    case ISM_ITEMREPAIR:
        //		bCanSwitch = false;
        if (bCanSwitch = bIsItemConstr)
        {
            pConstrInfo->Show(TRUE);
            pConstrInfo->SetHelp(104);
            pRepairItems->Show(TRUE);
            pAssortItems->Show(TRUE);
        }
        break;

    case ISM_EXIT:
        pPerks->Confirm(false, true);
        MoveAllItems(pSpellsAssort, pSpellsBuy);
        MoveAllItems(&IWorldClient.GetParty(), pSpellsSell);
        MoveAllItems(pItemsAssort, pItemsBuy);
        MoveAllItems(&IWorldClient.GetParty(), pItemsSell);
        MoveAllItems(&IWorldClient.GetParty(), pItemsRepair);
        while (pSpellsConstr->allWagonItems.GetSize())
            DropFromSpellConstr(pSpellsConstr->allWagonItems[0]);
        while (pItemsConstr->allWagonItems.GetSize())
            DropFromItemConstr(pItemsConstr->allWagonItems[0]);
        pI3D->BeginFX("buttons\\globalmap\\transit.wav");
        if (mbLocal)
            pI3D->ExitFromModal();
        else
            IWorldClient.FinishConstructing();
        return;
    }

    if (bCanSwitch)
    {
        if (eScreenMode != eNewMode)
            pI3D->BeginFX("buttons\\camp\\slot.wav");
        eScreenMode = eNewMode;
        pNameStats->SetScreenMode(eScreenMode);
        pPerks->SetPersonage(nCurPers);
#ifndef NOGETDC
        surfaces[1]->GetDC();
        surfaces[1]->Clear();
        surfaces[1]->DrawText(szModes[eScreenMode], CRect(0, 10, 200, 30), 1, CAMP_COLOR_HEADER,
                              DT_CENTER);
        surfaces[1]->ReleaseDC();
#endif // NOGETDC
    }
    else
        SetScreenMode(eScreenMode);

    if (eScreenMode == ISM_WEAPONS && (!bIsItemConstr || bCrapNoConstr))
    {
        figures[0]->Show(FALSE);
        figures[1]->Show(FALSE);
        areas[0]->SetToolTip(0);
        areas[1]->SetToolTip(0);
    }
    else

    {
        figures[0]->Show(TRUE);
        figures[1]->Show(TRUE);
        areas[0]->SetToolTip(20200);
        areas[1]->SetToolTip(20201);
    }

    for (int i = 0; i < ISM_TOTAL; i++)
        figures[2 + i]->SetAmbient(.5f, .5f, .5f);
    figures[2 + eScreenMode]->SetAmbient(1, 1, 1);

    pI3D->CheckTutorial();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DScreenCamp::Draw(CRenderer *pRend)
{
    E4CampAccess::BeginDraw(this);
    OwnedRender::E4::Frame campFrame(pRend);
    CPoint point = theCursor.GetPos();

    pMouseItem = 0;
    int nSlotType = 0;

    int ht = HitTest(point);

    if (ht >= 4 && ht < 4 + ISM_TOTAL)
    {
        switch ((EItemScreenModes)(ht - 4))
        {
        case ISM_WEAPONS:
            nSlotType = 105;
            break;
        case ISM_SPELLS:
            nSlotType = 106;
            break;
        case ISM_SPELLTRADE:
            if (bIsSpellConstr)
                nSlotType = 101;
            break;
        case ISM_SPELLCONSTR:
            if (bIsSpellConstr && !bCrapNoConstr)
                nSlotType = 103;
            break;
        case ISM_ITEMTRADE:
            if (bIsItemConstr)
                nSlotType = 100;
            break;
        case ISM_ITEMCONSTR:
            if (bIsItemConstr && !bCrapNoConstr)
                nSlotType = 102;
            break;
        case ISM_ITEMREPAIR:
            if (bIsItemConstr)
                nSlotType = 104;
            break;
        case ISM_EXIT:
            nSlotType = 107;
            break;
        }
    }

    if (!pMouseItem && !nSlotType)
        pMouseItem = pSlots->GetMouseItem(point, nSlotType);
    if (!pMouseItem && !nSlotType)
        pMouseItem = pSpells->GetMouseItem(point, nSlotType);
    if (!pMouseItem && !nSlotType)
        pMouseItem = pWagon->GetMouseItem(point, nSlotType);
    if (bIsSpellConstr)
    {
        if (!pMouseItem && !nSlotType)
            pMouseItem = pTradeSpells->GetMouseItem(point, nSlotType);
        if (!pMouseItem && !nSlotType)
            pMouseItem = pConstrSpells->GetMouseItem(point, nSlotType);
        if (!pMouseItem && !nSlotType)
            pMouseItem = pAssortSpells->GetMouseItem(point, nSlotType);
    }
    if (bIsItemConstr)
    {
        if (!pMouseItem && !nSlotType)
            pMouseItem = pTradeItems->GetMouseItem(point, nSlotType);
        if (!pMouseItem && !nSlotType)
            pMouseItem = pConstrItems->GetMouseItem(point, nSlotType);
        if (!pMouseItem && !nSlotType)
            pMouseItem = pRepairItems->GetMouseItem(point, nSlotType);
        if (!pMouseItem && !nSlotType)
            pMouseItem = pAssortItems->GetMouseItem(point, nSlotType);
    }

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
        SCampSkillMouseInfo skillInfo;
        skillInfo.nType = -1;
        skillInfo.pUnit = GetCurrentUnit();
        if (pPerks->IsVisible())
            pPerks->GetMouseSkill(point, &skillInfo);
        if (skillInfo.nType < 0)
        {
            if (pNameStats->IsVisible())
                pNameStats->GetMouseSkill(point, &skillInfo);
        }
        if (pItemInfo)
            pItemInfo->SetSkill(&skillInfo);
    }

    CheckValidTransaction();
    E4CampAccess::Cards(this, pRend);
    CI3DScreen::Draw(pRend);

    if (eScreenMode == ISM_WEAPONS)
    {
        CUnit *pDrawUnit = pUnits[nCurPers];
        owned::camp::VisualScope campUnit(owned::camp::composition(OwnedRender::E4::Layout(), OwnedRender::E4::Uniform(OwnedRender::E4::Role::quick)), OwnedRender::E4::Active());
        DrawUnit(pRend, pDrawUnit, fPersAngle, 400, true);
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::OnLButtonDown(UINT nFlags, CPoint point)
{
    CI3DScreen::OnLButtonDown(nFlags, point);
    int ht = HitTest(point);
    JournalHit(ht);
    switch (ht)
    {
    case 0:
        CheckValidTransaction();
        if (bValidOK)
            TransactionOK();
        break;
    case 1:
        CheckValidTransaction();
        if (bValidCancel)
            TransactionCancel();
        break;
    case 2:
        if (eScreenMode == ISM_WEAPONS)
        {
            pI3D->BeginFX("buttons\\camp\\move.wav");
            nLock = 1;
            pI3D->SetCapture(this);
        }
        break;
    case 3:
        if (eScreenMode == ISM_WEAPONS)
        {
            pI3D->BeginFX("buttons\\camp\\move.wav");
            nLock = 2;
            pI3D->SetCapture(this);
        }
        break;
    }
    if (ht >= 4 && ht < 4 + ISM_TOTAL)
    {
        EItemScreenModes eMode = (EItemScreenModes)(ht - 4);
        if (eScreenMode != eMode)
            SetScreenMode(eMode);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::OnLButtonUp(UINT nFlags, CPoint point)
{
    CI3DScreen::OnLButtonUp(nFlags, point);
    if (pI3D->GetCapture() == this)
        pI3D->ReleaseCapture();
    nLock = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    switch (nChar)
    {
    case 13:
        if (IWorldConnector.IsLMP())
        {
            pI3D->BeginChatLine();
            SetProcessed();
            return;
        }
        break;
    case VK_BACK:
        if (IWorldConnector.IsLMP())
        {
            pI3D->ClearChatLines();
            SetProcessed();
            return;
        }
        break;
    case VK_ESCAPE:
        SetScreenMode(ISM_EXIT);
        SetProcessed();
        return;
    }
    CI3DItemScreen::OnKeyDown(nChar, nRepCnt, nFlags);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    CI3DItemScreen::OnKeyUp(nChar, nRepCnt, nFlags);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::OnIdle()
{
    CI3DScreen::OnIdle();
    if (nLock == 1)
        fPersAngle = (float)fmod(fPersAngle + GetFrameTime() * 2.f, FP_2PI);
    else if (nLock == 2)
        fPersAngle = (float)fmod(fPersAngle - GetFrameTime() * 2.f, FP_2PI);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::NextPersonage()
{
    CMapObjectManager *pMobMng = ((CMapObjectManager *)theWorld.GetMapObjectManager());
    CParty &party = IWorldClient.GetParty();

    int nNewPers = nCurPers, nSteps = 0;
    bool bHuman = false;
    while (!bHuman && nSteps <= party.people.GetSize())
    {
        nNewPers++;
        nSteps++;
        if (nNewPers >= party.people.GetSize())
            nNewPers = 0;
        if (pMobMng->GetTypeIDFromPrototype(party.people[nNewPers].szPrototype) == MOTID_HUMAN)
            bHuman = true;
    }

    if (!bHuman)
        ReportError("ERROR: no human personages in the party.\n");

    if (nNewPers != nCurPers)
    {
        nCurPers = nNewPers;
        UpdatePersonage();
        UpdateWeapons();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::PrevPersonage()
{
    CMapObjectManager *pMobMng = ((CMapObjectManager *)theWorld.GetMapObjectManager());
    CParty &party = IWorldClient.GetParty();

    int nNewPers = nCurPers, nSteps = 0;
    bool bHuman = false;
    while (!bHuman && nSteps <= party.people.GetSize())
    {
        nNewPers--;
        nSteps++;
        if (nNewPers < 0)
            nNewPers = party.people.GetSize() - 1;
        if (pMobMng->GetTypeIDFromPrototype(party.people[nNewPers].szPrototype) == MOTID_HUMAN)
            bHuman = true;
    }

    if (!bHuman)
        ReportError("ERROR: no human personages in the party.\n");

    if (nNewPers != nCurPers)
    {
        nCurPers = nNewPers;
        UpdatePersonage();
        UpdateWeapons();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::UpdatePersonage()
{
    CUnit *pUnit = pUnits[nCurPers];
    CTextManager *pTxtMng = IWorld.GetTextManager();
    CString szPersName;
    CString szTmp = pUnit->GetName();
    if (szTmp.IsEmpty())
    {
        szTmp = GetCurrentPers()->szInternalName;
        if (pTxtMng->GetText(CString("pers ") + szTmp))
            pTxtMng->GetLine(szPersName);
    }
    else
        szPersName = szTmp;
    pNameStats->SetPersonage(nCurPers, szPersName);
    pPerks->SetPersonage(nCurPers);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::RefreshProgression()
{
    if(nCurPers<0 || nCurPers>=pUnits.GetSize() || !pUnits[nCurPers] || !pNameStats || !pPerks)return;
    pNameStats->UpdateStats();
    UpdatePersonage();
    UpdateWeapons();
}

void CI3DScreenCamp::UpdateAttributes()
{
    if (pUnits[nCurPers])
    {
        pUnits[nCurPers]->SetupCampUnitWithUnitStats(&GetCurrentPers()->stats);
        pUnits[nCurPers]->GetUnitSpecific()->NotifyHitPointsUpdated();
    }
    pNameStats->UpdateStats();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::UpdateWeapons()
{
    CPersonage *pPers = GetCurrentPers();
    CUnit *pUnit = pUnits[nCurPers];
    pUnit->ChangeWeapon(-1);
    for (int i = 0; i < QI_MAX_WEAPONS; i++)
        pUnit->RemoveWeapon(i);
    for (int i = 0; i < pPers->weapons.GetSize(); i++)
        pUnit->AddWeapon(pPers->weapons[i]);
    pUnit->ConvertWeaponsOnClient();
    pUnit->ChangeWeapon(pPers->dwCurrentWeapon);
    pUnit->GetUnitSpecific()->NotifyWeaponUpdated();
    UpdateAttributes();
    RefreshCampEquipmentPose(pUnit,pPers,true);
}
/////////////////////////////////////////////////////////////////////////////////////
CPersonage *CI3DScreenCamp::GetCurrentPers()
{
    CParty &party = IWorldClient.GetParty();
    if (nCurPers < 0 || nCurPers >= party.people.GetSize() || !GetCurrentUnit())
        return nullptr;
    return &party.people[nCurPers];
}
/*
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::DismissPersonage()
{
        // remove
        if ( nCurPers > 0 )
        {
                CPersonages personages;
                CParty& party = IWorldClient.GetParty();

                CString szName = party.people[nCurPers].szInternalName;
                if ( szName.Left(4).CompareNoCase( "merc" ) == 0 )
                {
                        int nMerc = atoi( szName.Mid(4) );
                        CString szVarName;
                        szVarName.Format( "b.merc%d.n%d", nMerc, nMerc );
                        party.vars.SetVar( szVarName, theAIGeneralSettings.nQuestsToRespawn + 1 );
                }
                party.people[nCurPers].nID += 100000000;
                personages.Add( &party.people[nCurPers] );
                IWorldClient.RemoveClientPersonages( personages );
                party.people[nCurPers].nID -= 100000000;
                party.people.RemoveAt( nCurPers );
                pUnits.RemoveAt( nCurPers );
                PrevPersonage();
        }
}
*/
/////////////////////////////////////////////////////////////////////////////////////
int CI3DScreenCamp::GetItemPrice(CItem *pItem, EItemScreenKoeffs eKoeffType)
{
    float fPriceRepair = 0;
    float fPercentDamage = 0;
    if (pItem->GetTypeID() == ITID_ITEM_WEAPON || pItem->GetTypeID() == ITID_ITEM_ARMOR ||
        eKoeffType == ISK_ITEM_REPAIR)
    {
        fPercentDamage = 1.f - pItem->GetHP() / pItem->GetMaxHP();
        fPriceRepair = pItem->GetPrice() * fPriceKoeffs[ISK_ITEM_REPAIR] * fPercentDamage;
    }
    if (eKoeffType == ISK_ITEM_REPAIR)
    {
        if (fPercentDamage == 0)
            return 0;
        if (fPriceKoeffs[ISK_ITEM_REPAIR] == 0)
            return 0;
        int nPr = static_cast<int>(fPriceRepair);
        return (nPr <= 0) ? 1 : nPr;
    }
    if (pItem->GetTypeID() == ITID_ITEM_WEAPON || pItem->GetTypeID() == ITID_ITEM_ARMOR)
        return static_cast<int>(pItem->GetPrice() * fPriceKoeffs[eKoeffType] - fPriceRepair);
    return static_cast<int>(pItem->GetPrice() * fPriceKoeffs[eKoeffType]);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::DropFromSpellConstr(CItem *pItem)
{
    if (pItem->GetUserData() && static_cast<CRotateItem *>(pItem->GetUserData())->bIncludePrice)
        pAssortSpells->SetToItem(MoveItem(pItem, pSpellsAssort, pSpellsConstr));
    else
        pWagon->SetToItem(MoveItem(pItem, &IWorldClient.GetParty(), pSpellsConstr));
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::DropFromItemConstr(CItem *pItem)
{
    if (pItem->GetUserData() && static_cast<CRotateItem *>(pItem->GetUserData())->bIncludePrice)
        pAssortItems->SetToItem(MoveItem(pItem, pItemsAssort, pItemsConstr));
    else
        pWagon->SetToItem(MoveItem(pItem, &IWorldClient.GetParty(), pItemsConstr));
}
/////////////////////////////////////////////////////////////////////////////////////
namespace {
bool CampEquipmentActive()
{
    auto* mod=ModRuntime::Current();return mod&&mod->CombatSetting("JMVPerks");
}
void ClearCampArmorShells(CUnit* unit)
{
    if(unit&&unit->GetTypeID()==MOTID_HUMAN)
        ModCampEquipment::ClearShells([&](const std::string& name){unit->RemoveFigure(name.c_str());});
}
void RebuildCampArmorShells(CUnit* unit)
{
    if(!unit)return;
    ClearCampArmorShells(unit);CStringArray names;
    for(int i=0;i<RPG_NUM_ARMOR_TYPES;++i)if(auto* armor=unit->GetQArmor(i)){
        const auto* shape=armor->GetShapeEntry();
        if(shape)ModCampEquipment::ArmorShells(int(shape->dwTypeID),int(shape->dwTextureTypeIndex),
            [&](const std::string& name){names.Add(name.c_str());});
    }
    unit->LoadFigures(unit->GetMaskName(),names,unit->CheckFlag(OBJ_LOADED));
}
void RefreshCampEquipmentPose(CUnit* unit,CPersonage* pers,bool allowEmpty)
{
    if(!unit)return;
    const int slot=int(pers->dwCurrentWeapon);
    const bool valid=slot>=0&&slot<pers->weapons.GetSize()&&slot<QI_MAX_WEAPONS&&pers->weapons[slot];
    if(!valid&&!allowEmpty)return;
    const auto* shape=valid?pers->weapons[slot]->GetShapeEntry():nullptr;
    const int type=shape?int(shape->dwTypeID):-1;
    const auto* entry=unit->GetAnimationEntry(ModCampEquipment::IdlePose(type,shape&&ModStats::IsTwohandedWeapon(type,unit)));
    if(entry){unit->LowStartAnimationExtended(0xffffffffu,entry->dwEntryIndex,
        (timeGetTime()/1024.0f)*15-random.Get(0,50),0);
        unit->GetUnitSpecific()->NewAnimationWasReceived((timeGetTime()/1024.0f)*15);}
}
void NotifyCampEquipment(CUnit* unit)
{
    if(unit){unit->GetUnitSpecific()->NotifyItemsUpdated();unit->GetUnitSpecific()->NotifyWeaponUpdated();}
}
}

void CI3DScreenCamp::InventoryToPers(CItem *pItem)
{
    pI3D->BeginFX("buttons\\camp\\put_on.wav");

    CPersonage *pPers = GetCurrentPers();
    const bool nativeEquipment=CampEquipmentActive();
    CUnit* equipmentUnit=GetCurrentUnit();
    switch (pItem->GetTypeID())
    {
    case ITID_ITEM_WEAPON: {
        if(nativeEquipment){const auto* shape=static_cast<CItemWeapon*>(pItem)->GetShapeEntry();
            ModCampEquipment::ReplaceShields(shape&&shape->dwTypeID==RPGWST_HANDS1,
                [&](){return pPers->weapons.GetSize();},
                [&](int i){const auto* old=pPers->weapons[i]->GetShapeEntry();return old&&old->dwTypeID==RPGWST_HANDS1;},
                [&](int i){PersToInventory(pPers->weapons[i]);});}
        if (pPers->weapons.GetSize() >= QI_MAX_WEAPONS)
            return;
        const ModCampEquipment::PrimarySnapshot primaries(nativeEquipment?pPers->stats.psStrength.fpEffectiveValue:0,
            nativeEquipment?pPers->stats.psDexterity.fpEffectiveValue:0,nativeEquipment?pPers->stats.psIntelligence.fpEffectiveValue:0);
        pPers->AddWeapon((CItemWeapon *)pItem);
        if (pPers->weapons.GetSize() == 1)
            pPers->dwCurrentWeapon = 0;
        UpdateWeapons();
        if(nativeEquipment){primaries.Restore(pPers->stats.psStrength.fpEffectiveValue,
            pPers->stats.psDexterity.fpEffectiveValue,pPers->stats.psIntelligence.fpEffectiveValue);
            const EItemScreenModes oldMode=eScreenMode;SetScreenMode(ISM_SPELLS);SetScreenMode(oldMode);
            pNameStats->UpdateStats();RefreshCampEquipmentPose(equipmentUnit,pPers,false);}
        break;
    }
    case ITID_ITEM_ARMOR: {
        int type = ((CItemArmor *)pItem)->GetArmorType();
        for (int i = 0; i < pPers->armors.GetSize(); i++)
            if (pPers->armors[i]->GetArmorType() == type)
                PersToInventory(pPers->armors[i]);
        if (pUnits[nCurPers])
        {
            pUnits[nCurPers]->Wear(pPers->AddArmor((CItemArmor *)pItem));
            pUnits[nCurPers]->GetUnitSpecific()->NotifyItemsUpdated();
        }
        pPers->stats.RemoveAllArmors();
        for (int i = 0; i < pPers->armors.GetSize(); i++)
            pPers->stats.Wear(pPers->armors[i]);
        if(nativeEquipment)RebuildCampArmorShells(equipmentUnit);
    }
    break;
    case ITID_ITEM_QUICK:
        if (pPers->items.GetSize() >= QI_MAX_ITEMS)
            return;
        if(nativeEquipment){
            auto* mod=ModRuntime::Current();
            if(!ModSettingsPolicy::EquipQuick(pItem->GetShapeEntry()->GetCode(),
                mod->CombatSetting("JMVOneAmuletLimit")!=0,mod->CombatSetting("JMVRingsLimit")!=0,
                [&](){return pPers->items.GetSize();},
                [&](int i){return std::string(pPers->items[i]->GetShapeEntry()->GetCode());},
                [&](int i){PersToInventory(pPers->items[i]);}))return;
        }
        if(nativeEquipment&&equipmentUnit)equipmentUnit->AddCampQItem(static_cast<CItemQuick*>(pItem));
        pPers->AddQuick((CItemQuick *)pItem);
        if(nativeEquipment)SetScreenMode(eScreenMode);
        break;
    case ITID_ITEM_SPELL:
        if (pPers->spells.GetSize() >= QI_MAX_SPELLS)
            return;
        pPers->AddSpell((CItemSpell *)pItem);
        break;
    default:
        return;
    }
    IWorldClient.GetParty().RemoveItemFromWagon(pItem);
    pNameStats->UpdateStats();
    if(nativeEquipment)NotifyCampEquipment(equipmentUnit);
}
/////////////////////////////////////////////////////////////////////////////////////
CItem *CI3DScreenCamp::PersToInventory(CItem *pItem)
{
    CItem *pItemRes = 0;
    BOOL bStack = FALSE;
    pI3D->BeginFX("buttons\\camp\\put_off.wav");

    switch (pItem->GetTypeID())
    {
    case ITID_ITEM_WEAPON:
    case ITID_ITEM_ARMOR:
    case ITID_ITEM_QUICK:
    case ITID_ITEM_SPELL:
        break;
    default:
        return 0;
    }

    CPersonage *pPers = GetCurrentPers();
    /*
            if ( pItem->GetTypeID() == ITID_ITEM_WEAPON &&
                 pPers->weapons.GetSize() == 1 )
            {
                    CI3DScreenMessageBox* pScr = dbgnew CI3DScreenMessageBox( pI3D, "last_weapon",
       "last_weapon_msg", 0x00010000 ); pI3D->DoModal( pScr ); return 0;
            }
    */
    const bool nativeEquipment=CampEquipmentActive();
    CUnit* equipmentUnit=GetCurrentUnit();
    pItemRes = IWorldClient.GetParty().AddItemToWagon(pItem);
    switch (pItem->GetTypeID())
    {
    case ITID_ITEM_WEAPON: {
        int nWeapon = -1;
        for (int i = 0; i < pPers->weapons.GetSize(); i++)
            if (pPers->weapons[i] == pItem)
            {
                nWeapon = i;
                break;
            }
        if (nWeapon >= 0 && nWeapon < pPers->dwCurrentWeapon)
            pPers->dwCurrentWeapon--;
        pPers->RemoveWeapon((CItemWeapon *)pItem);
        if (pPers->dwCurrentWeapon >= pPers->weapons.GetSize())
            pPers->dwCurrentWeapon = pPers->weapons.GetSize() - 1;
        UpdateWeapons();
        if(nativeEquipment){const EItemScreenModes oldMode=eScreenMode;SetScreenMode(ISM_SPELLS);SetScreenMode(oldMode);
            pNameStats->UpdateStats();RefreshCampEquipmentPose(equipmentUnit,pPers,true);}
    }
    break;
    case ITID_ITEM_ARMOR: {
        // Capture while party/unit still own input; wagon holds a duplicate.
        const auto* removedShape=nativeEquipment?static_cast<CItemArmor*>(pItem)->GetShapeEntry():nullptr;
        const bool removedLeggings=removedShape&&removedShape->dwTypeID==2;
        if (pUnits[nCurPers])
        {
            pUnits[nCurPers]->Remove(((CItemArmor *)pItem)->GetArmorType());
            pUnits[nCurPers]->ChangeWeapon(pPers->dwCurrentWeapon);
            pUnits[nCurPers]->GetUnitSpecific()->NotifyWeaponUpdated();
            pUnits[nCurPers]->GetUnitSpecific()->NotifyItemsUpdated();
        }
        pPers->RemoveArmor((CItemArmor *)pItem);
        pPers->stats.RemoveAllArmors();
        for (int i = 0; i < pPers->armors.GetSize(); i++)
            pPers->stats.Wear(pPers->armors[i]);
        if(nativeEquipment&&removedLeggings)ClearCampArmorShells(equipmentUnit);
    }
    break;
    case ITID_ITEM_QUICK: {
        int quick=-1;
        if(nativeEquipment)for(int i=0;i<pPers->items.GetSize();++i)if(pPers->items[i]==pItem){quick=i;break;}
        pPers->RemoveQuick((CItemQuick *)pItem);
        if(nativeEquipment){if(equipmentUnit)equipmentUnit->RemoveCampQItem(quick);
            NotifyCampEquipment(equipmentUnit);SetScreenMode(eScreenMode);}
        break;
    }
    case ITID_ITEM_SPELL:
        pPers->RemoveSpell((CItemSpell *)pItem);
        break;
    }
    pNameStats->UpdateStats();
    if(nativeEquipment)NotifyCampEquipment(equipmentUnit);
    return pItemRes;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::GetMaxManaAndComplexity(ERPGMagicSubtypes subtype, int &nComplexity,
                                             int &nMana)
{
    if (auto* mod=ModRuntime::Current();mod && mod->AdvancedUI("useLS_GetMaxManaAndComplexity") && pShop) {
        const auto& people=IWorldClient.GetParty().people;
        std::vector<ModCampSpellLimitInput> inputs;
        inputs.reserve(people.GetSize());
        bool displayed=true;
        for (int i=0;i<people.GetSize();++i) {
            if (i>=pUnits.GetSize() || !pUnits[i]) {displayed=false;break;}
            inputs.push_back({&people[i].stats,int(pUnits[i]->GetUserUnitStats()->cIntelligence)});
        }
        if (displayed) {
            mod->CampSpellLimits(pShop->nID,int(subtype),inputs,nComplexity,nMana);
            return;
        }
    }
    int nMaxComplexity = 0, nMaxMana = 0;
    SRPGUnitStats *pStats;
    for (int i = 0; i < IWorldClient.GetParty().people.GetSize(); i++)
    {
        pStats = &IWorldClient.GetParty().people[i].stats;
        nMaxComplexity = Max(nMaxComplexity, pStats->GetAvailableComplexity(subtype));
        nMaxMana = Max(nMaxMana, (int)(pStats->RecalcMP() / pStats->GetManaBonusMultiplier()));
    }
    nComplexity = nMaxComplexity;
    nMana = nMaxMana;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CI3DScreenCamp::CanClickOnItem(CItem *pItem, EItemScreenParts eFrom)
{
    if (!pItem) return false;
    if(auto*mod=ModRuntime::Current()){
        if(eScreenMode==ISM_SPELLCONSTR&&(eFrom==ISP_WAGON||eFrom==ISP_SPELLASSORT)&&mod->SkillItemBlocked(pItem,0))return false;
        if(eScreenMode==ISM_ITEMCONSTR&&(eFrom==ISP_WAGON||eFrom==ISP_ITEMASSORT)&&mod->SkillItemBlocked(pItem,1))return false;
        if(eScreenMode==ISM_SPELLTRADE&&eFrom==ISP_WAGON&&mod->SkillItemBlocked(pItem,2))return false;
    }
    const bool removeSpellConstructReqs = ModRuntime::Current() &&
        ModRuntime::Current()->CombatSetting("RemoveSpellConstructReqs");
    switch (eScreenMode)
    {
    case ISM_WEAPONS: {
        ETypeIDs tid = pItem->GetTypeID();
        if ((tid == ITID_ITEM_WEAPON || tid == ITID_ITEM_ARMOR) && pItem->IsBroken() &&
            eFrom == ISP_WAGON)
            return false;
        return (tid == ITID_ITEM_WEAPON || tid == ITID_ITEM_ARMOR || tid == ITID_ITEM_QUICK);
    }

    case ISM_SPELLS:
        if (eFrom == ISP_WAGON)
        {
            if (!IsItemSpell(pItem))
                return false;
            CSpell *pSpell = pItem->GetSpell(0);
            SRPGUnitStats *pStats = &GetCurrentPers()->stats;
            if (!pStats->CanCastSpell(pSpell->GetMagicSubtype(), pSpell->GetComplexity()))
                return false;
            if ((int)(pStats->RecalcMP() / pStats->GetManaBonusMultiplier()) <
                (int)pSpell->GetManaCost())
                return false;
            return true;
        }
        else
            return true;

    case ISM_SPELLTRADE:
        return (IsItemSpell(pItem) || IsItemPrototype(pItem) || IsItemModifier(pItem));

    case ISM_ITEMTRADE:
        return (pItem->GetTypeID() != ITID_ITEM_QUEST && !IsItemSpell(pItem) &&
                !IsItemPrototype(pItem) && !IsItemModifier(pItem));

    case ISM_ITEMREPAIR: {
        ETypeIDs tid = pItem->GetTypeID();
        return ((tid == ITID_ITEM_WEAPON || tid == ITID_ITEM_ARMOR) &&
                pItem->GetHP() < pItem->GetMaxHP());
    }

    case ISM_SPELLCONSTR:
        if (eFrom == ISP_WAGON || eFrom == ISP_SPELLASSORT)
        {
            if (IsItemSpell(pItem))
            {
                if (pSpellsConstr->allWagonItems.GetSize() > 0)
                    return false;
                else
                    return true;
            }
            else if (IsItemPrototype(pItem))
            {
                if (pSpellsConstr->allWagonItems.GetSize() > 0)
                    return false;
                CItemLoot *pLoot = static_cast<CItemLoot *>(pItem);
                CSpellManager *pSM = theWorld.GetSpellManager();
                const SSDBSpellPrototype *pPrototype = pSM->GetPrototype(
                    pLoot->GetInstructionItemIndex());
                int nMaxComplexity = 0, nMaxMana = 0;
                GetMaxManaAndComplexity(pPrototype->mstSubtype, nMaxComplexity, nMaxMana);
                if (!removeSpellConstructReqs && nMaxMana < (int)pPrototype->fpMana)
                    return false;
                if (!removeSpellConstructReqs && nMaxComplexity < (int)pPrototype->dwComplexity)
                    return false;
                return true;
            }
            else if (ModAlchemy::Scroll(pItem,false))
                return true;
            else if (IsItemModifier(pItem))
            {
                int nMods = 0;
                DWORD dwComplexity = 0;
                float fMana = 0;
                ERPGMagicSubtypes subtype;
                bool bIsTherePrototype = false;
                for (int i = 0; i < pSpellsConstr->allWagonItems.GetSize(); i++)
                {
                    if (IsItemSpell(pSpellsConstr->allWagonItems[i]))
                        return false;
                    else if (IsItemPrototype(pSpellsConstr->allWagonItems[i]))
                    {
                        bIsTherePrototype = true;
                        CItemLoot *pLoot = static_cast<CItemLoot *>(
                            pSpellsConstr->allWagonItems[i]);
                        CSpellManager *pSM = theWorld.GetSpellManager();
                        const SSDBSpellPrototype *pPrototype = pSM->GetPrototype(
                            pLoot->GetInstructionItemIndex());
                        dwComplexity += pPrototype->dwComplexity;
                        fMana += pPrototype->fpMana;
                        subtype = pPrototype->mstSubtype;
                        //
                        CItemLoot *pModLoot = static_cast<CItemLoot *>(pItem);
                        const SSDBModifier *pModifier = pSM->GetModifier(
                            pModLoot->GetInstructionItemIndex());
                        int nType = pModifier->GetAllowedRuneType();
                        if (nType < SDB_NUM_ALLOWANCES && !pPrototype->bRuneAllowed[nType])
                            return false;
                    }
                    else if (IsItemModifier(pSpellsConstr->allWagonItems[i]))
                    {
                        nMods++;
                        CItemLoot *pLoot = static_cast<CItemLoot *>(
                            pSpellsConstr->allWagonItems[i]);
                        CSpellManager *pSM = theWorld.GetSpellManager();
                        const SSDBModifier *pModifier = pSM->GetModifier(
                            pLoot->GetInstructionItemIndex());
                        dwComplexity += pModifier->dwComplexity;
                        fMana += pModifier->fpMana;
                    }
                }
                if (bIsTherePrototype && nMods < 8)
                {
                    CItemLoot *pLoot = static_cast<CItemLoot *>(pItem);
                    CSpellManager *pSM = theWorld.GetSpellManager();
                    const SSDBModifier *pModifier = pSM->GetModifier(
                        pLoot->GetInstructionItemIndex());
                    dwComplexity += pModifier->dwComplexity;
                    fMana += pModifier->fpMana;
                    int nMaxComplexity = 0, nMaxMana = 0;
                    GetMaxManaAndComplexity(subtype, nMaxComplexity, nMaxMana);
                    if (!removeSpellConstructReqs && nMaxComplexity < (int)dwComplexity)
                        return false;
                    if (!removeSpellConstructReqs && nMaxMana < (int)fMana)
                        return false;
                    return true;
                }
                else
                    return false;
            }
        }
        else if (eFrom == ISP_SPELLCONSTR)
            return true;
        break;

    case ISM_ITEMCONSTR:
        if (eFrom == ISP_WAGON || eFrom == ISP_ITEMASSORT)
        {
            if (IsItemInstruction(pItem) || IsItemConstructed(pItem))
            {
                if (pItemsConstr->allWagonItems.GetSize() > 0)
                    return false;
                else
                    return true;
            }
            else if (IsItemMaterial(pItem))
            {
                CItemManager *pIM = pItemsConstr->GetItemManager();
                CSpellManager *pSM = pItemsConstr->GetSpellManager();
                int nMats = 0;
                const SIDBItemGeneralShape *pShape = 0;
                const SIDBMaterial *pMaterial = 0;
                for (int i = 0; i < pItemsConstr->allWagonItems.GetSize(); i++)
                {
                    if (IsItemConstructed(pItemsConstr->allWagonItems[i]))
                        return false;
                    else if (IsItemInstruction(pItemsConstr->allWagonItems[i]))
                    {
                        CItemLoot *pLoot = static_cast<CItemLoot *>(pItemsConstr->allWagonItems[i]);
                        switch (pLoot->GetInstructionType())
                        {
                        case IDBINS_WEAPON:
                            pShape = pIM->GetWeaponShape(pLoot->GetInstructionItemIndex());
                            break;
                        case IDBINS_ARMOR:
                            pShape = pIM->GetArmorShape(pLoot->GetInstructionItemIndex());
                            break;
                        case IDBINS_QUICK:
                            pShape = pIM->GetQuickItemShape(pLoot->GetInstructionItemIndex());
                            break;
                        }
                    }
                    else if (IsItemMaterial(pItemsConstr->allWagonItems[i]))
                    {
                        nMats++;
                        if (!pMaterial)
                        {
                            CItemLoot *pLoot = static_cast<CItemLoot *>(
                                pItemsConstr->allWagonItems[i]);
                            pMaterial = pIM->GetMaterial(pLoot->GetMaterialIndex());
                        }
                    }
                }
                if (!pShape)
                    return false;
                CItemLoot *pLoot = static_cast<CItemLoot *>(pItem);
                const SIDBMaterial *pMatNew = pIM->GetMaterial(pLoot->GetMaterialIndex());
                if (pMaterial)
                {
                    if (pMatNew == pMaterial && nMats < pShape->dwComponents)
                        return true;
                }
                else
                {
                    if (pShape->szMaterialType.CompareNoCase(pMatNew->szType) == 0)
                        return true;
                }
            }
            else if (IsItemSpell(pItem))
            {
                if (!pConstrItem)
                    return false;
                if (!pItem->GetSpell(0))
                    return false;
                if (pConstrItem->GetTypeID() != ITID_ITEM_QUICK &&
                    !pItem->GetSpell(0)->CanInsertToItem())
                    return false;
                if (pItem->GetSpell(0)->GetComplexity() > pConstrItem->GetSpellSlots())
                    return false;
                if (pItem->GetSpell(0)->GetManaCost() > pConstrItem->GetMaxMP())
                    return false;
                for (int i = 0; i < pItemsConstr->allWagonItems.GetSize(); i++)
                    if (IsItemSpell(pItemsConstr->allWagonItems[i]))
                        return false;
                return true;
            }
        }
        else if (eFrom == ISP_ITEMCONSTR)
            return true;
        break;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::ClickOnWagonItem(CItem *pItem)
{
    switch (eScreenMode)
    {
    case ISM_WEAPONS:
    case ISM_SPELLS:
        InventoryToPers(pItem);
        break;

    case ISM_SPELLTRADE:
        MoveItem(pItem, pSpellsSell, &IWorldClient.GetParty(), 8);
        pI3D->BeginFX("buttons\\camp\\put_on.wav");
        break;

    case ISM_ITEMTRADE:
        MoveItem(pItem, pItemsSell, &IWorldClient.GetParty(), 8);
        pI3D->BeginFX("buttons\\camp\\put_on.wav");
        break;

    case ISM_ITEMREPAIR:
        MoveItem(pItem, pItemsRepair, &IWorldClient.GetParty(), 16);
        pI3D->BeginFX("buttons\\camp\\put_on.wav");
        break;

    case ISM_SPELLCONSTR:
        MoveItem(pItem, pSpellsConstr, &IWorldClient.GetParty(), 0, false);
        pI3D->BeginFX("buttons\\camp\\put_on.wav");
        break;

    case ISM_ITEMCONSTR: {
        if (IsItemMaterial(pItem) && pI3D->IsShiftDown())
        {
            int i = pItem->GetCount();
            while (i-- > 0 && CanClickOnItem(pItem, ISP_WAGON))
                MoveItem(pItem, pItemsConstr, &IWorldClient.GetParty(), 0, false);
        }
        else
            MoveItem(pItem, pItemsConstr, &IWorldClient.GetParty(), 0, false);
        pI3D->BeginFX("buttons\\camp\\put_on.wav");
    }
    break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::ClickOnItem(CItem *pItem, EItemScreenParts eFrom)
{
    if (!CanClickOnItem(pItem, eFrom))
    {
        pI3D->BeginFX("buttons\\messbox\\cancel.wav");
        return;
    }
    switch (eFrom)
    {
    case ISP_CAMPSLOTS:
        pWagon->SetToItem(PersToInventory(pItem));
        break;

    case ISP_WAGON:
        ClickOnWagonItem(pItem);
        break;

    case ISP_SPELLCONSTR: {
        if (IsItemModifier(pItem))
            DropFromSpellConstr(pItem);
        else
        {
            while (pSpellsConstr->allWagonItems.GetSize())
                DropFromSpellConstr(pSpellsConstr->allWagonItems[0]);
        }
        pI3D->BeginFX("buttons\\camp\\put_off.wav");
    }
    break;

    case ISP_ITEMCONSTR: {
        if (IsItemSpell(pItem))
            DropFromItemConstr(pItem);
        else if (IsItemMaterial(pItem))
        {
            DropFromItemConstr(pItem);
            if (pI3D->IsShiftDown())
            {
                for (int i = 0; i < pItemsConstr->allWagonItems.GetSize(); i++)
                    if (IsItemMaterial(pItemsConstr->allWagonItems[i]))
                    {
                        DropFromItemConstr(pItemsConstr->allWagonItems[i]);
                        i--;
                    }
            }
            for (int i = 0; i < pItemsConstr->allWagonItems.GetSize(); i++)
                if (IsItemSpell(pItemsConstr->allWagonItems[i]))
                {
                    DropFromItemConstr(pItemsConstr->allWagonItems[i]);
                    break;
                }
        }
        else
        {
            while (pItemsConstr->allWagonItems.GetSize())
                DropFromItemConstr(pItemsConstr->allWagonItems[0]);
        }
        pI3D->BeginFX("buttons\\camp\\put_off.wav");
    }
    break;

    case ISP_ITEMREPAIR:
        pWagon->SetToItem(MoveItem(pItem, &IWorldClient.GetParty(), pItemsRepair));
        pI3D->BeginFX("buttons\\camp\\put_off.wav");
        break;

    case ISP_SPELLASSORT:
        if (eScreenMode == ISM_SPELLTRADE)
            MoveItem(pItem, pSpellsBuy, pSpellsAssort, 8);
        else if (eScreenMode == ISM_SPELLCONSTR)
            MoveItem(pItem, pSpellsConstr, pSpellsAssort, 0, false, true);
        pI3D->BeginFX("buttons\\camp\\put_on.wav");
        break;

    case ISP_ITEMASSORT:
        if (eScreenMode == ISM_ITEMTRADE)
            MoveItem(pItem, pItemsBuy, pItemsAssort, 8);
        else if (eScreenMode == ISM_ITEMCONSTR)
        {
            if (IsItemMaterial(pItem) && pI3D->IsShiftDown())
            {
                int i = pItem->GetCount();
                while (i-- > 0 && CanClickOnItem(pItem, ISP_ITEMASSORT))
                    MoveItem(pItem, pItemsConstr, pItemsAssort, 0, false, true);
            }
            else
                MoveItem(pItem, pItemsConstr, pItemsAssort, 0, false, true);
        }
        pI3D->BeginFX("buttons\\camp\\put_on.wav");
        break;

    case ISP_SPELLTRADE: {
        for (int i = 0; i < pSpellsBuy->allWagonItems.GetSize(); i++)
            if (pItem == pSpellsBuy->allWagonItems[i])
            {
                pAssortSpells->SetToItem(MoveItem(pItem, pSpellsAssort, pSpellsBuy));
                pI3D->BeginFX("buttons\\camp\\put_off.wav");
                return;
            }
        for (int i = 0; i < pSpellsSell->allWagonItems.GetSize(); i++)
            if (pItem == pSpellsSell->allWagonItems[i])
            {
                pWagon->SetToItem(MoveItem(pItem, &IWorldClient.GetParty(), pSpellsSell));
                pI3D->BeginFX("buttons\\camp\\put_off.wav");
                return;
            }
    }
    break;

    case ISP_ITEMTRADE: {
        for (int i = 0; i < pItemsBuy->allWagonItems.GetSize(); i++)
            if (pItem == pItemsBuy->allWagonItems[i])
            {
                pAssortItems->SetToItem(MoveItem(pItem, pItemsAssort, pItemsBuy));
                pI3D->BeginFX("buttons\\camp\\put_off.wav");
                return;
            }
        for (int i = 0; i < pItemsSell->allWagonItems.GetSize(); i++)
            if (pItem == pItemsSell->allWagonItems[i])
            {
                pWagon->SetToItem(MoveItem(pItem, &IWorldClient.GetParty(), pItemsSell));
                pI3D->BeginFX("buttons\\camp\\put_off.wav");
                return;
            }
    }
    break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::ClickOnSlot(int nSlotType, EItemScreenParts eFrom)
{
    switch ((EItemSlotTypes)nSlotType)
    {
    case IST_SPELL:
    case IST_WEAPON:
    case IST_QITEM:
    case IST_ARMOR_HELM:
    case IST_ARMOR_PLATE:
    case IST_ARMOR_LEGGINGS:
    case IST_ARMOR_SHIRT:
    case IST_ARMOR_PANTS:
    case IST_ARMOR_BOOTS:
    case IST_ARMOR_GLOVES:
        pWagon->SwitchToMode(2);
        break;
    case IST_ITEM_READY:
        pWagon->SwitchToMode(2);
        pAssortItems->SwitchToMode(5);
        break;
    case IST_ITEM_INSTR:
        pWagon->SwitchToMode(0);
        pAssortItems->SwitchToAlternativeMode(0, 4);
        break;
    case IST_ITEM_MAT:
        pWagon->SwitchToMode(0);
        pAssortItems->SwitchToMode(4);
        break;
    case IST_ITEM_SPELL:
        pWagon->SwitchToMode(CampEquipmentActive() ? 1 : 2);
        break;
    case IST_SPELL_READY:
        pWagon->SwitchToMode(CampEquipmentActive() ? 1 : 2);
        pAssortSpells->SwitchToMode(5);
        break;
    case IST_SPELL_PROT:
        pWagon->SwitchToMode(1);
        pAssortSpells->SwitchToAlternativeMode(0, 3);
        break;
    case IST_SPELL_MOD:
        pWagon->SwitchToMode(1);
        pAssortSpells->SwitchToAlternativeMode(3, 2);
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::TransactionOK()
{
    GameJournal::Event("TRADE", "request", "mode=%d shop=%d price=%d money=%d valid=%d", int(eScreenMode), pShop ? pShop->nID : 0, nPrice, IWorldClient.GetParty().nMoney, bValidOK);
    if(SkillTransactionBlocked()){bValidOK=false;return;}
    pI3D->BeginFX("buttons\\camp\\buy.wav");

    CItem *pItem;
    const bool nativeRepair=CampUsesNativeRepair();const int repairShop=pShop?pShop->nID:0;
    switch (eScreenMode)
    {
    case ISM_WEAPONS:
        if (bIsItemConstr && !bCrapNoConstr)
        {
            CPersonage *pPers = GetCurrentPers();
            ModCampRepair::RestoreItems(nativeRepair,ModCampRepair::Mode::Equipped,repairShop,pPers->weapons.GetSize(),
                [&](int i){return CampRepairMetal(pPers->weapons[i]);},[&](int i){pPers->weapons[i]->RestoreHP();});
            ModCampRepair::RestoreItems(nativeRepair,ModCampRepair::Mode::Equipped,repairShop,pPers->armors.GetSize(),
                [&](int i){return CampRepairMetal(pPers->armors[i]);},[&](int i){pPers->armors[i]->RestoreHP();});
        }
        break;

    case ISM_SPELLS:
        if(auto* mod=ModRuntime::Current();mod && mod->CombatSetting("JMVPerks") &&
           !ModProgression::CampAffordable(pPerks->GetExpPrice(),pPerks->GetExpTotal())){bValidOK=false;return;}
        pPerks->Confirm(true);
        return;

    case ISM_ITEMREPAIR: {
        ModCampRepair::RestoreItems(nativeRepair,ModCampRepair::Mode::Selected,repairShop,pItemsRepair->allWagonItems.GetSize(),
            [&](int i){return CampRepairMetal(pItemsRepair->allWagonItems[i]);},
            [&](int i){pItemsRepair->allWagonItems[i]->RestoreHP();});
        pWagon->SetToItem(MoveAllItems(&IWorldClient.GetParty(), pItemsRepair));
    }
    break;

    case ISM_SPELLTRADE: {
        CSpellManager *pSM = pShop->spellAssort.GetSpellManager();
        CItem *pItem;
        for (int i = 0; i < pSpellsSell->allWagonItems.GetSize(); i++)
        {
            pItem = pSpellsSell->allWagonItems[i];
            pItem->RestoreHP();
            if (IsItemSpell(pItem))
            {
                const SSDBSpellPrototype *pProt = pItem->GetSpell(0)->GetPrototype();
                pShop->spellGenerator.AddSpellPrototype(pSM->GetPrototype(pProt->szCodeName));
            }
            else if (IsItemPrototype(pItem))
            {
                const SSDBSpellPrototype *pProt = pSM->GetPrototype(
                    ((CItemLoot *)pItem)->GetInstructionItemIndex());
                pShop->spellGenerator.AddSpellPrototype(pProt);
            }
            else if (IsItemModifier(pItem))
            {
                const SSDBModifier *pMod = pSM->GetModifier(
                    ((CItemLoot *)pItem)->GetInstructionItemIndex());
                pShop->spellGenerator.AddSpellModifier(pMod);
            }
        }
        pAssortSpells->SetToItem(MoveAllItems(pSpellsAssort, pSpellsSell));
        pWagon->SetToItem(MoveAllItems(&IWorldClient.GetParty(), pSpellsBuy));
    }
    break;

    case ISM_SPELLCONSTR: {
        const bool scroll=pSpellsConstr->allWagonItems.GetSize()>0&&
            ModAlchemy::Scroll(pSpellsConstr->allWagonItems[0]);
        if (bDeconstruct)
        {
            if(scroll){if(!DeconstructItemIn(pSpellsConstr,pConstrItem))return;}
            else DeconstructSpell();
        }
        else
        {
            auto* mod=ModRuntime::Current();
            const bool alchemy=mod&&ModAlchemy::Enabled(mod->CombatSetting("JMVPerks")!=0,mod->CombatSetting("JMVAlchemy")!=0);
            auto consume=[&](){
                while (pSpellsConstr->allWagonItems.GetSize())
                {
                    pItem = pSpellsConstr->allWagonItems[0];
                    pSpellsConstr->RemoveItemFromWagon(pItem);
                }
            };
            auto create=[&](const char* name){return pSpellsConstr->GetItemManager()->CreateItem(name);};
            auto publish=[&](CItem* result){pSpellsConstr->AddItemToWagon(result,false,false);};
            if(scroll)ModAlchemy::Commit(false,pConstrItem,consume,create,publish);
            else ModAlchemy::Commit(alchemy,pConstrSpell,consume,create,publish);
        }
        break;
    }

    case ISM_ITEMTRADE: {
        if(nativeRepair){
            auto* mod=ModRuntime::Current();
            if(pShop)for(int i=pItemsSell->allWagonItems.GetSize()-1;i>=0;--i){
                CItem* item=pItemsSell->allWagonItems[i];
                const int shop=pShop->nID;
                if(shop==2)continue;
                auto& generator=pShop->itemGenerator;
                CItemManager* manager=pShop->itemAssort.GetItemManager();
                const SIDBMaterial* material=manager->GetMaterial(item->GetMaterial());
                const SIDBItemGeneralShape* shape=nullptr;ETypeIDs shapeType=TID_EMPTY;
                ModCampSale::Process(shop,material->szType.CompareNoCase("Metal")==0,[&](){
                    ModCampSale::Role role=ModCampSale::Role::Other;int components=0;
                    DWORD index=item->GetShape();
                    if(IsItemConstructed(item)){
                        shapeType=item->GetTypeID();
                        if(shapeType==ITID_ITEM_WEAPON){role=ModCampSale::Role::Weapon;shape=manager->GetWeaponShape(index);}
                        else if(shapeType==ITID_ITEM_ARMOR){role=ModCampSale::Role::Armor;shape=manager->GetArmorShape(index);}
                        else if(shapeType==ITID_ITEM_QUICK){role=ModCampSale::Role::Quick;shape=manager->GetQuickItemShape(index);}
                        components=int(item->GetMaterialAmount());
                    }else if(IsItemInstruction(item)){
                        auto* loot=static_cast<CItemLoot*>(item);index=loot->GetInstructionItemIndex();
                        role=ModCampSale::Role::Instruction;
                        switch(loot->GetInstructionType()){
                        case IDBINS_WEAPON:shapeType=ITID_ITEM_WEAPON;shape=manager->GetWeaponShape(index);break;
                        case IDBINS_ARMOR:shapeType=ITID_ITEM_ARMOR;shape=manager->GetArmorShape(index);break;
                        case IDBINS_QUICK:shapeType=ITID_ITEM_QUICK;shape=manager->GetQuickItemShape(index);break;
                        default:break;
                        }
                    }else if(item->GetTypeID()==ITID_ITEM_LOOT){
                        const auto type=static_cast<CItemLoot*>(item)->GetLootType();
                        if(type==IDBLIT_TREASURE)role=ModCampSale::Role::Treasure;
                        else if(type==IDBLIT_MATERIAL)role=ModCampSale::Role::Material;
                    }
                    return ModCampSale::Info{role,components,item->GetCount()};
                },[&](bool forShape){return mod->InfoForbiddenSpell(forShape?shape->szName:material->szName,shop);},
                [&](ModCampSale::Event event,int low,int high){switch(event){
                case ModCampSale::Event::Restore:item->RestoreHP();break;
                case ModCampSale::Event::Shape:
                    if(shapeType==ITID_ITEM_WEAPON)generator.AddWeaponShape(static_cast<const SIDBWeaponShape*>(shape));
                    else if(shapeType==ITID_ITEM_ARMOR)generator.AddArmorShape(static_cast<const SIDBArmorShape*>(shape));
                    else if(shapeType==ITID_ITEM_QUICK)generator.AddQuickItemShape(static_cast<const SIDBQuickItemShape*>(shape));
                    break;
                case ModCampSale::Event::Material:{const int count=generator.GetMaterialAmount(material);generator.AddMaterial(material,count+low,count+high);break;}
                case ModCampSale::Event::TreasureErase:pItemsSell->RemoveItemFromWagon(item,true);break;
                }});
            }
        }else{
            CItemManager *pIM = pShop->itemAssort.GetItemManager();
            CItem *pItem;
            for (int i = pItemsSell->allWagonItems.GetSize() - 1; i >= 0; i--)
            {
                pItem = pItemsSell->allWagonItems[i];
                pItem->RestoreHP();
                if (IsItemConstructed(pItem))
                {
                    switch (pItem->GetTypeID())
                    {
                    case ITID_ITEM_WEAPON:
                        pShop->itemGenerator.AddWeaponShape(pIM->GetWeaponShape(pItem->GetShape()));
                        break;
                    case ITID_ITEM_ARMOR:
                        pShop->itemGenerator.AddArmorShape(pIM->GetArmorShape(pItem->GetShape()));
                        break;
                    case ITID_ITEM_QUICK:
                        pShop->itemGenerator.AddQuickItemShape(
                            pIM->GetQuickItemShape(pItem->GetShape()));
                        break;
                    }
                }
                else if (IsItemInstruction(pItem))
                {
                    CItemLoot *pLoot = static_cast<CItemLoot *>(pItem);
                    switch (pLoot->GetInstructionType())
                    {
                    case IDBINS_WEAPON:
                        pShop->itemGenerator.AddWeaponShape(
                            pIM->GetWeaponShape(pLoot->GetInstructionItemIndex()));
                        break;
                    case IDBINS_ARMOR:
                        pShop->itemGenerator.AddArmorShape(
                            pIM->GetArmorShape(pLoot->GetInstructionItemIndex()));
                        break;
                    case IDBINS_QUICK:
                        pShop->itemGenerator.AddQuickItemShape(
                            pIM->GetQuickItemShape(pLoot->GetInstructionItemIndex()));
                        break;
                    }
                }
                else if (pItem->GetTypeID() == ITID_ITEM_LOOT)
                {
                    CItemLoot *pLoot = static_cast<CItemLoot *>(pItem);
                    if (pLoot->GetLootType() == IDBLIT_TREASURE)
                        pItemsSell->RemoveItemFromWagon(pItem, true);
                }
            }
        }
        pAssortItems->SetToItem(MoveAllItems(pItemsAssort, pItemsSell));
        pWagon->SetToItem(MoveAllItems(&IWorldClient.GetParty(), pItemsBuy));
    }
    break;

    case ISM_ITEMCONSTR:
        if (bDeconstruct)
        {
            if (!DeconstructItem())
                return;
        }
        else
        {
            if (pConstrItem)
            {
                while (pItemsConstr->allWagonItems.GetSize())
                {
                    pItem = pItemsConstr->allWagonItems[0];
                    pItemsConstr->RemoveItemFromWagon(pItem);
                }
                pItemsConstr->AddItemToWagon(pConstrItem, false, false);
                pConstrItem->Release();
                pConstrItem = 0;
            }
        }
        break;
    }

    if(eScreenMode==ISM_ITEMTRADE || eScreenMode==ISM_SPELLTRADE) GameJournal::TradeCommit(this, pShop?pShop->nID:0, nPrice);
    IWorldClient.GetParty().nMoney -= nPrice;
    GameJournal::Event("TRADE", "committed", "mode=%d shop=%d price=%d money=%d", int(eScreenMode), pShop ? pShop->nID : 0, nPrice, IWorldClient.GetParty().nMoney);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::TransactionCancel()
{
    GameJournal::Event("TRADE", "cancel", "mode=%d shop=%d", int(eScreenMode), pShop ? pShop->nID : 0);
    pI3D->BeginFX("buttons\\messbox\\cancel.wav");
    switch (eScreenMode)
    {
    case ISM_SPELLS:
        pPerks->Confirm(false);
        break;

    case ISM_SPELLTRADE:
        pAssortSpells->SetToItem(MoveAllItems(pSpellsAssort, pSpellsBuy));
        pWagon->SetToItem(MoveAllItems(&IWorldClient.GetParty(), pSpellsSell));
        break;

    case ISM_SPELLCONSTR: {
        while (pSpellsConstr->allWagonItems.GetSize())
            DropFromSpellConstr(pSpellsConstr->allWagonItems[0]);
    }
    break;

    case ISM_ITEMTRADE:
        pAssortItems->SetToItem(MoveAllItems(pItemsAssort, pItemsBuy));
        pWagon->SetToItem(MoveAllItems(&IWorldClient.GetParty(), pItemsSell));
        break;

    case ISM_ITEMCONSTR: {
        while (pItemsConstr->allWagonItems.GetSize())
            DropFromItemConstr(pItemsConstr->allWagonItems[0]);
    }
    break;

    case ISM_ITEMREPAIR:
        pWagon->SetToItem(MoveAllItems(&IWorldClient.GetParty(), pItemsRepair));
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::CheckValidTransaction()
{
    GameJournal::TradeReset(this);
    CString szActionToDo, szAmount("camp_current_money");
    nPrice = 0;
    bValidOK = false;
    bValidCancel = false;
    const bool nativeRepair=CampUsesNativeRepair();const int repairShop=pShop?pShop->nID:0;
    // The native internal quote method is separate from retail virtual UI prices.
    auto quotePrice=[&](CItem* item,EItemScreenKoeffs coefficient){
        if(nativeRepair&&coefficient==ISK_ITEM_REPAIR)
            return ModCampRepair::Price(item->GetPrice(),fPriceKoeffs[ISK_ITEM_REPAIR],item->GetHP(),item->GetMaxHP());
        if(nativeRepair&&coefficient!=ISK_ITEM_REPAIR&&
           (item->GetTypeID()==ITID_ITEM_WEAPON||item->GetTypeID()==ITID_ITEM_ARMOR))
            return ModCampItemPrice::WeaponArmor(item->GetPrice(),fPriceKoeffs[coefficient],fPriceKoeffs[ISK_ITEM_REPAIR],item->GetHP(),item->GetMaxHP());
        return GetItemPrice(item,coefficient);
    };
    switch (eScreenMode)
    {
    case ISM_WEAPONS:
        if (bIsItemConstr && !bCrapNoConstr)
        {
            CPersonage *pPers = GetCurrentPers();
            nPrice += ModCampRepair::Quote(nativeRepair,repairShop,pPers->weapons.GetSize(),
                [&](int i){return CampRepairMetal(pPers->weapons[i]);},
                [&](int i){return quotePrice(pPers->weapons[i],ISK_ITEM_REPAIR);});
            nPrice += ModCampRepair::Quote(nativeRepair,repairShop,pPers->armors.GetSize(),
                [&](int i){return CampRepairMetal(pPers->armors[i]);},
                [&](int i){return quotePrice(pPers->armors[i],ISK_ITEM_REPAIR);});

            bValidOK = (nPrice > 0);
            if (bValidOK)
                szActionToDo = "camp_repair_all";
        }
        break;

    case ISM_SPELLS: {
        bValidOK = bValidCancel = pPerks->IsModified();
        nPrice = pPerks->GetExpPrice();
        if(auto* mod=ModRuntime::Current();mod && mod->CombatSetting("JMVPerks") &&
           !ModProgression::CampAffordable(nPrice,pPerks->GetExpTotal()))bValidOK=false;
        if (bValidOK)
            szActionToDo = "camp_level_up";
        szAmount = "camp_current_exp";
    }
    break;

    case ISM_SPELLTRADE: {
        if (pSpellsBuy->allWagonItems.GetSize() > 0 || pSpellsSell->allWagonItems.GetSize() > 0)
        {
            szActionToDo = "camp_spell_trade";
            bValidOK = true;
            bValidCancel = true;
            CItem *pItem;
            for (int i = 0; i < pSpellsBuy->allWagonItems.GetSize(); i++)
            {
                pItem = pSpellsBuy->allWagonItems[i];
                const int journalPrice=quotePrice(pItem, ISK_SPELL_BUY);
                GameJournal::TradeQuote(this, pItem, "buy", journalPrice);
                nPrice += journalPrice * pItem->GetCount();
            }
            for (int i = 0; i < pSpellsSell->allWagonItems.GetSize(); i++)
            {
                pItem = pSpellsSell->allWagonItems[i];
                const int journalPrice=quotePrice(pItem, ISK_SPELL_SELL);
                GameJournal::TradeQuote(this, pItem, "sell", journalPrice);
                nPrice -= journalPrice * pItem->GetCount();
            }
        }
    }
    break;

    case ISM_SPELLCONSTR: {
        if (pConstrSpell)
            pConstrSpell->Release();
        pConstrSpell = 0;

        bDeconstruct = false;
        bool bIsThereItemWhichWeNeedToShowInInfoView = false;
        if (pSpellsConstr->allWagonItems.GetSize() > 0)
        {
            bValidCancel = true;
            for (int i = 0; i < pSpellsConstr->allWagonItems.GetSize(); i++)
            {
                CItem *pItem = pSpellsConstr->allWagonItems[i];
                nPrice += quotePrice(pItem, ISK_SPELL_CREATE);
                if (pItem->GetUserData() &&
                    static_cast<CRotateItem *>(pItem->GetUserData())->bIncludePrice)
                    nPrice += quotePrice(pItem, ISK_SPELL_BUY);
                if (IsItemSpell(pItem) || ModAlchemy::Scroll(pItem))
                {
                    bValidOK = true;
                    // recalc price for deconstructing
                    nPrice = quotePrice(pItem, ISK_SPELL_DESTROY);
                    if (pItem->GetUserData() &&
                        static_cast<CRotateItem *>(pItem->GetUserData())->bIncludePrice)
                        nPrice += quotePrice(pItem, ISK_SPELL_BUY);
                    bDeconstruct = true;
                    pConstrInfo->SetItem(pItem);
                    bIsThereItemWhichWeNeedToShowInInfoView = true;
                    szActionToDo = "camp_spell_deconstr";
                    break;
                }
                else if (IsItemPrototype(pItem))
                {
                    bValidOK = ConstructSpell();
                    pConstrInfo->SetItem(pConstrSpell);
                    bIsThereItemWhichWeNeedToShowInInfoView = (pConstrSpell != 0);
                    if (bValidOK)
                        szActionToDo = "camp_spell_constr";
                }
            }
            if (!bValidOK)
                nPrice = 0;
        }
        if (!bIsThereItemWhichWeNeedToShowInInfoView)
            pConstrInfo->SetHelp(103);
    }
    break;

    case ISM_ITEMTRADE: {
        if (pItemsBuy->allWagonItems.GetSize() > 0 || pItemsSell->allWagonItems.GetSize() > 0)
        {
            szActionToDo = "camp_item_trade";
            bValidOK = true;
            bValidCancel = true;
            CItem *pItem;
            for (int i = 0; i < pItemsBuy->allWagonItems.GetSize(); i++)
            {
                pItem = pItemsBuy->allWagonItems[i];
                const int journalPrice=quotePrice(pItem, ISK_ITEM_BUY);
                GameJournal::TradeQuote(this, pItem, "buy", journalPrice);
                nPrice += journalPrice * pItem->GetCount();
            }
            for (int i = 0; i < pItemsSell->allWagonItems.GetSize(); i++)
            {
                pItem = pItemsSell->allWagonItems[i];
                const int journalPrice=quotePrice(pItem, ISK_ITEM_SELL);
                GameJournal::TradeQuote(this, pItem, "sell", journalPrice);
                nPrice -= journalPrice * pItem->GetCount();
            }
        }
    }
    break;

    case ISM_ITEMREPAIR: {
        if (pItemsRepair->allWagonItems.GetSize() > 0)
        {
            if(!nativeRepair||pShop)
                nPrice += ModCampRepair::Quote(nativeRepair,repairShop,pItemsRepair->allWagonItems.GetSize(),
                    [&](int i){return CampRepairMetal(pItemsRepair->allWagonItems[i]);},
                    [&](int i){CItem* item=pItemsRepair->allWagonItems[i];return quotePrice(item,ISK_ITEM_REPAIR)*item->GetCount();});
            const auto valid=ModCampRepair::SelectedValidity(nativeRepair,pItemsRepair->allWagonItems.GetSize(),nPrice);
            bValidOK=valid.confirm;bValidCancel=valid.cancel;
            if(bValidOK)szActionToDo = "camp_item_repair";
        }
    }
    break;

    case ISM_ITEMCONSTR: {
        if (pConstrItem)
            pConstrItem->Release();
        pConstrItem = 0;

        bDeconstruct = false;
        bool bIsThereItemWhichWeNeedToShowInInfoView = false;
        if (pItemsConstr->allWagonItems.GetSize() > 0)
        {
            bValidCancel = true;
            for (int i = 0; i < pItemsConstr->allWagonItems.GetSize(); i++)
            {
                CItem *pItem = pItemsConstr->allWagonItems[i];
                nPrice += quotePrice(pItem, ISK_ITEM_CREATE);
                if (pItem->GetUserData() &&
                    static_cast<CRotateItem *>(pItem->GetUserData())->bIncludePrice)
                    nPrice += quotePrice(pItem, ISK_ITEM_BUY);
                if (IsItemConstructed(pItem))
                {
                    bValidOK = true;
                    // recalc price for deconstructing
                    nPrice = quotePrice(pItem, ISK_ITEM_DESTROY);
                    if (pItem->GetUserData() &&
                        static_cast<CRotateItem *>(pItem->GetUserData())->bIncludePrice)
                        nPrice += quotePrice(pItem, ISK_ITEM_BUY);
                    bDeconstruct = true;
                    pConstrInfo->SetItem(pItem);
                    bIsThereItemWhichWeNeedToShowInInfoView = true;
                    szActionToDo = "camp_item_deconstr";
                    break;
                }
                else if (IsItemInstruction(pItem))
                {
                    bValidOK = ConstructItem();
                    pConstrInfo->SetItem(pConstrItem);
                    bIsThereItemWhichWeNeedToShowInInfoView = (pConstrItem != 0);
                    if (bValidOK)
                        szActionToDo = "camp_item_constr";
                }
            }
            if (!bValidOK)
                nPrice = 0;
        }
        if (!bIsThereItemWhichWeNeedToShowInInfoView)
            pConstrInfo->SetHelp(102);
    }
    break;
    }

    int nOutputAmount = 0;
    if (eScreenMode != ISM_SPELLS)
    {
        nOutputAmount = IWorldClient.GetParty().nMoney;
        if (IWorldClient.GetParty().nMoney < nPrice)
            bValidOK = false;
    }
    else
        nOutputAmount = pPerks->GetExpTotal();

    CString szTmp, szOut;
#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    pSurf->GetDC();
    pSurf->Clear();
    if (!szActionToDo.IsEmpty())
    {
        theWorld.GetTextManager()->GetSystemString(szActionToDo, szTmp);
        pSurf->DrawText(szTmp, CRect(0, 30, 200, 50), 1, CONSTR_COLOR_PRICE, DT_CENTER);
        if (nPrice != 0)
            szTmp.Format("%+d", -nPrice);
        else
            szTmp = "0";
        pSurf->DrawText(szTmp, CRect(0, 50, 200, 70), 1, CONSTR_COLOR_PRICE, DT_CENTER);
    }
    theWorld.GetTextManager()->GetSystemString(szAmount, szTmp);
    szOut.Format("%s %d", szTmp, nOutputAmount);
    pSurf->DrawText(szOut, CRect(0, 10, 200, 30), 1, CONSTR_COLOR_PRICE, DT_CENTER);
    if(ModRuntime::Current()&&theWorld.GetTextManager()->GetText("camphelp 22")){
        theWorld.GetTextManager()->GetLine(szTmp);
        szOut.Format("%s %d",(LPCSTR)szTmp,IWorldClient.GetParty().allWagonItems.GetSize());
        pSurf->DrawText(szOut,CRect(0,-3,200,14),1,CONSTR_COLOR_PRICE,DT_CENTER);
    }
    pSurf->ReleaseDC();
#endif // NOGETDC

    if(SkillTransactionBlocked())bValidOK=false;
    if (bValidOK)
        figures[0]->SetAmbient(1, 1, 1);
    else
        figures[0]->SetAmbient(.5f, .5f, .5f);

    if (bValidCancel)
        figures[1]->SetAmbient(1, 1, 1);
    else
        figures[1]->SetAmbient(.5f, .5f, .5f);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CI3DScreenCamp::ConstructSpell()
{
    CSpellConstructor *pSC = pSpellsConstr->GetSpellConstructor();
    CItemManager *pIM = pSpellsConstr->GetItemManager();
    CSpellManager *pSM = pSpellsConstr->GetSpellManager();

    const SSDBSpellPrototype *pPrototype = 0;
    const SSDBModifier *pModifiers[8];
    int nMod = 0;
    for (int i = 0; i < pSpellsConstr->allWagonItems.GetSize(); i++)
    {
        CItemLoot *pLoot = static_cast<CItemLoot *>(pSpellsConstr->allWagonItems[i]);
        if (IsItemPrototype(pLoot))
            pPrototype = pSM->GetPrototype(pLoot->GetInstructionItemIndex());
        else if (IsItemModifier(pLoot))
            pModifiers[nMod++] = pSM->GetModifier(pLoot->GetInstructionItemIndex());
    }
    if (!pPrototype)
        return false;

    pSC->SetSpellManager(pSM);
    if (!pSC->BeginConstructSpell(pPrototype))
    {
        pSC->SetSpellManager(0);
        return false;
    }
    for (int i = 0; i < nMod; i++)
    {
        if (!pSC->AddRune(pModifiers[i]))
        {
            pSC->SetSpellManager(0);
            return false;
        }
    }
    if (!pSC->EndConstructSpell())
    {
        pSC->SetSpellManager(0);
        return false;
    }

    CSpell *pSpell = pSC->GetSpellContainer()->GetSpell().Duplicate(pSM);
    pSC->SetSpellManager(0);

    ASSERT(pSpell);

    // this part is from CItemManager::CreateItemSpell
    CItemSpell *pItemSpell = 0;
    // CRAP - material & shape for itemspell are unknown
    pItemSpell = (CItemSpell *)pIM->CreateItem(
        ITID_ITEM_SPELL, pIM->GetShapeIndex(pIM->GetQuickItemShape("spell container")), 0, -1);
    if (pItemSpell)
        pItemSpell->AddSpell(pSpell);

    pConstrSpell = pItemSpell;
    if (!pConstrSpell)
        return false;
    pConstrSpell->AddRef();

    const bool removeSpellConstructReqs = ModRuntime::Current() &&
        ModRuntime::Current()->CombatSetting("RemoveSpellConstructReqs");
    int nMaxComplexity = 0, nMaxMana = 0;
    GetMaxManaAndComplexity(pPrototype->mstSubtype, nMaxComplexity, nMaxMana);
    if (!removeSpellConstructReqs && nMaxMana < (int)pSpell->GetManaCost())
        return false;
    if (!removeSpellConstructReqs && nMaxComplexity < (int)pSpell->GetComplexity())
        return false;

    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CI3DScreenCamp::DeconstructSpell()
{
    if(auto*mod=ModRuntime::Current())if(mod->SkillContainerBlocked(pSpellsConstr,0))return false;
    CItemLoot *pLoot = 0;
    ASSERT(pSpellsConstr->allWagonItems.GetSize() == 1);
    pConstrSpell = pSpellsConstr->DuplicateItem(pSpellsConstr->allWagonItems[0]);
    pConstrSpell->AddRef();
    pSpellsConstr->RemoveItemFromWagon(pSpellsConstr->allWagonItems[0]);

    CSpell *pSpell = pConstrSpell->GetSpell(0);

    pLoot = static_cast<CItemLoot *>(pSpellsConstr->GetItemManager()->CreateItem("Prototype"));
    pLoot->AddRef();
    pLoot->SetInstructionItemIndex(pSpell->GetSubtypeID() >> 16);
    pSpellsConstr->AddItemToWagon(pLoot, false, false);
    pLoot->Release();

    for (int i = 0; i < pSpell->GetRunesCount(); i++)
    {
        pLoot = static_cast<CItemLoot *>(pSpellsConstr->GetItemManager()->CreateItem("Rune"));
        pLoot->AddRef();
        pLoot->SetInstructionItemIndex(static_cast<DWORD>(pSpell->GetRunes()[i]) - 1);
        pSpellsConstr->AddItemToWagon(pLoot, false, false);
        pLoot->Release();
    }

    pConstrSpell->Release();
    pConstrSpell = 0;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CI3DScreenCamp::ConstructItem()
{
    if(auto*mod=ModRuntime::Current())if(mod->SkillContainerBlocked(pItemsConstr,1))return false;
    CItemConstructor *pIC = pItemsConstr->GetItemConstructor();
    CItemManager *pIM = pItemsConstr->GetItemManager();
    CSpellManager *pSM = pItemsConstr->GetSpellManager();

    const SIDBItemGeneralShape *pShape = 0;
    const SIDBMaterial *pMaterial = 0;
    CSmartPtr<CSpell> pSpell;
    int nMats = 0;
    ETypeIDs type;
    for (int i = 0; i < pItemsConstr->allWagonItems.GetSize(); i++)
    {
        CItem *pItem = pItemsConstr->allWagonItems[i];
        if (IsItemSpell(pItem))
        {
            pSpell = pItem->GetSpell(0)->Duplicate(pSM);
        }
        else if (IsItemInstruction(pItem))
        {
            CItemLoot *pLoot = static_cast<CItemLoot *>(pItem);
            switch (pLoot->GetInstructionType())
            {
            case IDBINS_WEAPON:
                type = ITID_ITEM_WEAPON;
                pShape = pIM->GetWeaponShape(pLoot->GetInstructionItemIndex());
                break;
            case IDBINS_ARMOR:
                type = ITID_ITEM_ARMOR;
                pShape = pIM->GetArmorShape(pLoot->GetInstructionItemIndex());
                break;
            case IDBINS_QUICK:
                type = ITID_ITEM_QUICK;
                pShape = pIM->GetQuickItemShape(pLoot->GetInstructionItemIndex());
                break;
            }
        }
        else if (IsItemMaterial(pItem))
        {
            CItemLoot *pLoot = static_cast<CItemLoot *>(pItem);
            const SIDBMaterial *pNewMaterial = pIM->GetMaterial(pLoot->GetMaterialIndex());
            if (!pMaterial)
            {
                pMaterial = pNewMaterial;
                nMats = 1;
            }
            else
            {
                if (pMaterial != pNewMaterial)
                    return false;
                else
                    nMats++;
            }
        }
    }
    if (!pMaterial)
        return false;
    if (!pShape)
        return false;
    if (pShape->dwComponents != nMats)
        return false;
    if (pShape->szMaterialType.CompareNoCase(pMaterial->szType) != 0)
        return false;

    pIC->SetItemManager(pIM);
    if (!pIC->BeginConstructItem(type))
    {
        pIC->SetItemManager(0);
        return false;
    }
    if (!pIC->SetShape(pShape))
    {
        pIC->SetItemManager(0);
        return false;
    }
    if (!pIC->SetMaterial(pMaterial))
    {
        pIC->SetItemManager(0);
        return false;
    }
    if (!pIC->SetSpell(pSpell))
    {
        pIC->SetItemManager(0);
        return false;
    }
    pConstrItem = pIC->EndConstructItem();

    pIC->SetItemManager(0);

    if (!pConstrItem)
        return false;
    pConstrItem->AddRef();

    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CI3DScreenCamp::DeconstructItem()
{
    return DeconstructItemIn(pItemsConstr,pConstrItem);
}
bool CI3DScreenCamp::DeconstructItemIn(CParty* party,CItem*& result)
{
    CItemLoot *pLoot = 0;
    if (!party || party->allWagonItems.GetSize() != 1 || !party->allWagonItems[0])
        return false;
    CItem *pInput = party->allWagonItems[0];
    return ModAlchemy::Deconstruct<CItemSpell>(result,[&](CItemSpell*& pItemSpell){
    if (pInput->GetSpell(0))
    {
        CItemManager *pIM = party->GetItemManager();
        if (!pIM)
            return false;
        const SIDBQuickItemShape *pShape = pIM->GetQuickItemShape("spell container");
        if (!pShape)
            return false;
        const int shapeIndex = pIM->GetShapeIndex(pShape);
        if (shapeIndex < 0)
            return false;
        pItemSpell = static_cast<CItemSpell *>(
            pIM->CreateItem(ITID_ITEM_SPELL, shapeIndex, 0, -1));
        if (!pItemSpell)
            return false;
        pItemSpell->AddRef();
    }
    return true;
    },[&](){return party->DuplicateItem(pInput);},[&](){
        party->RemoveItemFromWagon(party->allWagonItems[0]);
    },[&](CItem* result,CItemSpell* pItemSpell){
    switch (result->GetTypeID())
    {
    case ITID_ITEM_WEAPON:
        pLoot = static_cast<CItemLoot *>(
            party->GetItemManager()->CreateItem("Instruction Weapon"));
        break;
    case ITID_ITEM_ARMOR:
        pLoot = static_cast<CItemLoot *>(
            party->GetItemManager()->CreateItem("Instruction Armor"));
        break;
    case ITID_ITEM_QUICK:
        pLoot = static_cast<CItemLoot *>(
            party->GetItemManager()->CreateItem("Instruction Quick Item"));
        break;
    }
    if (pLoot)
    {
        pLoot->AddRef();
        pLoot->SetInstructionItemIndex(result->GetShape());
        party->AddItemToWagon(pLoot, false, false);
        pLoot->Release();
    }

    for (int i = 0; i < result->GetMaterialAmount(); i++)
    {
        CString szMaterialName = CString("material") + '.' +
                                 theItemDatabase.materials[result->GetMaterial()]->szName;
        pLoot = static_cast<CItemLoot *>(
            party->GetItemManager()->CreateItem(szMaterialName));
        if (pLoot)
        {
            pLoot->AddRef();
            party->AddItemToWagon(pLoot, false, false);
            pLoot->Release();
        }
    }

    if (pItemSpell)
    {
        pItemSpell->AddSpell(
            result->GetSpell(0)->Duplicate(party->GetSpellManager()));
        party->AddItemToWagon(pItemSpell, false, false);
    }

    });
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DScreenCamp::GenerateCheatAssortment()
{
    CStringArray szTmpArray, szMatNames;
    int i, j;
    theWorld.GetItemManager()->GetMaterialNames(szMatNames);

    if (pItemsAssort)
    {
        pItemsAssort->Clear();
        theWorld.GetItemManager()->GetWeaponShapeNames(szTmpArray);
        for (i = 0; i < szTmpArray.GetSize(); i++)
        {
            CItemLoot *pItem = (CItemLoot *)(pItemsAssort->GetItemManager()->CreateItem(
                "Instruction Weapon"));
            pItem->AddRef();
            pItem->SetInstructionItemIndex(i);
            pItem->SetCount(10);
            pItemsAssort->AddItemToWagon(pItem, true);
            pItem->Release();

            CItemManager *pIM = pItemsAssort->GetItemManager();
            CItemConstructor *pIC = pItemsAssort->GetItemConstructor();
            const SIDBItemGeneralShape *pShape = pIM->GetWeaponShape(i);
            for (j = 0; j < szMatNames.GetSize(); j++)
                if (pIM->GetMaterial(j)->szType.CompareNoCase(pShape->szMaterialType) == 0)
                {
                    pIC->SetItemManager(pIM);
                    pIC->BeginConstructItem(ITID_ITEM_WEAPON);
                    pIC->SetShape(pShape);
                    pIC->SetMaterial(pIM->GetMaterial(j));
                    CItem *pItemRes = pIC->EndConstructItem();
                    pIC->SetItemManager(0);

                    pItemRes->AddRef();
                    pItemsAssort->AddItemToWagon(pItemRes, true);
                    pItemRes->Release();
                }
        }
        theWorld.GetItemManager()->GetArmorShapeNames(szTmpArray);
        for (i = 0; i < szTmpArray.GetSize(); i++)
        {
            CItemManager *pIM = pItemsAssort->GetItemManager();
            const SIDBItemGeneralShape *pShape = pIM->GetArmorShape(i);
            if (pShape->szName.Find("prototype") >= 0)
                continue;

            CItemLoot *pItem = (CItemLoot *)(pItemsAssort->GetItemManager()->CreateItem(
                "Instruction Armor"));
            pItem->AddRef();
            pItem->SetInstructionItemIndex(i);
            pItem->SetCount(10);
            pItemsAssort->AddItemToWagon(pItem, true);
            pItem->Release();

            CItemConstructor *pIC = pItemsAssort->GetItemConstructor();
            for (j = 0; j < szMatNames.GetSize(); j++)
                if (pIM->GetMaterial(j)->szType.CompareNoCase(pShape->szMaterialType) == 0)
                {
                    pIC->SetItemManager(pIM);
                    pIC->BeginConstructItem(ITID_ITEM_ARMOR);
                    pIC->SetShape(pShape);
                    pIC->SetMaterial(pIM->GetMaterial(j));
                    CItem *pItemRes = pIC->EndConstructItem();
                    pIC->SetItemManager(0);

                    pItemRes->AddRef();
                    pItemsAssort->AddItemToWagon(pItemRes, true);
                    pItemRes->Release();
                }
        }
        theWorld.GetItemManager()->GetQuickItemShapeNames(szTmpArray);
        for (i = 0; i < szTmpArray.GetSize(); i++)
        {
            CItemManager *pIM = pItemsAssort->GetItemManager();
            const SIDBItemGeneralShape *pShape = pIM->GetQuickItemShape(i);
            if (pShape->szName.Find("wand") < 0)
                continue;
            CItemLoot *pItem = (CItemLoot *)(pItemsAssort->GetItemManager()->CreateItem(
                "Instruction Quick Item"));
            pItem->AddRef();
            pItem->SetInstructionItemIndex(i);
            pItem->SetCount(10);
            pItemsAssort->AddItemToWagon(pItem, true);
            pItem->Release();
        }
        theWorld.GetItemManager()->GetMaterialNames(szTmpArray);
        for (i = 0; i < szTmpArray.GetSize(); i++)
        {
            CItemLoot *pItem = (CItemLoot *)(pItemsAssort->GetItemManager()->CreateItem(
                CString("Material") + "." + szTmpArray[i]));
            pItem->AddRef();
            pItem->SetCount(20);
            pItemsAssort->AddItemToWagon(pItem, true);
            pItem->Release();
        }
    }

    if (pSpellsAssort)
    {
        pSpellsAssort->Clear();
        theWorld.GetSpellManager()->GetRuneNames(szTmpArray);
        for (i = 0; i < szTmpArray.GetSize(); i++)
        {
            CItemLoot *pItem = (CItemLoot *)(pSpellsAssort->GetItemManager()->CreateItem("Rune"));
            pItem->AddRef();
            pItem->SetInstructionItemIndex(i);
            pItem->SetCount(10);
            pSpellsAssort->AddItemToWagon(pItem, true);
            pItem->Release();
        }
        theWorld.GetSpellManager()->GetPrototypeNames(szTmpArray);
        for (i = 0; i < szTmpArray.GetSize(); i++)
        {
            CItemLoot *pItem = (CItemLoot *)(pSpellsAssort->GetItemManager()->CreateItem(
                "Prototype"));
            pItem->AddRef();
            pItem->SetInstructionItemIndex(i);
            pItem->SetCount(10);
            pSpellsAssort->AddItemToWagon(pItem, true);
            pItem->Release();
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////

bool CI3DScreenCamp::SkillTransactionBlocked(){auto*mod=ModRuntime::Current();if(!mod)return false;
 if(eScreenMode==ISM_SPELLCONSTR)return mod->SkillContainerBlocked(pSpellsConstr,0);
 if(eScreenMode==ISM_ITEMCONSTR)return mod->SkillContainerBlocked(pItemsConstr,1)||mod->SkillItemBlocked(pConstrItem,1);
 if(eScreenMode==ISM_SPELLTRADE)return mod->SkillContainerBlocked(pSpellsSell,2);
 return false;
}
