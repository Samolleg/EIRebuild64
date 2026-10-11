#include "StdAfx.h"
#include "GameJournal.h"
#include "ModPackage.h"
#include "ModCampStats.h"
#include "ModProgression.h"
#include "ModRuntime.h"
#include "ModAlchemyItem.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DScreenConstructor.h"
#include "I3DScreenMessageBox.h"
#include "WorldInterface.h"
#include "WorldClient.h"
#include "TextManager.h"
#include "ItemManager.h"
#include "SpellManager.h"
#include "PerkDatabase.h"
#include "Party.h"
#include "MapFormat.h"
#include "GeneralSettings.h"
/////////////////////////////////////////////////////////////////////////////////////
const COLORREF INV_COLOR_NUMBERS = RGB(228, 215, 167);
const COLORREF INV_COLOR_PRICE = RGB(228, 215, 167);

const float INV_MAX_SPEED = 7.f;
const float INV_BREAK_PATH = 0.5f;
const float INV_DECEL_TIME = .25f;
const float INV_STEP_TIME = .1f;
const float INV_ACCELERATE = 15.f;
const float INV_PUSH_STEP = .1f;

const int FRAME_R_FROM = 80;
const int FRAME_G_FROM = 80;
const int FRAME_B_FROM = 80;
const int FRAME_A_FROM = 130;

const int FRAME_R_TO = 160;
const int FRAME_G_TO = 160;
const int FRAME_B_TO = 0;
const int FRAME_A_TO = 240;

const int FRAMEADD_R_TO = 0;
const int FRAMEADD_G_TO = 0;
const int FRAMEADD_B_TO = 160;
const int FRAMEADD_A_TO = 240;
/////////////////////////////////////////////////////////////////////////////////////
const int CAMP_NUM_AV_PERKS_VISIBLE = 23;
const int CAMP_NUM_REAL_PERKS_VISIBLE = 10;
const COLORREF CAMP_COLOR_SKILLS = RGB(228, 215, 167);
const COLORREF CAMP_COLOR_SKILLS_DIS = RGB(130, 130, 130);
// const COLORREF CAMP_COLOR_NAME = RGB( 255, 255, 255 );
const COLORREF CAMP_COLOR_HEADER = RGB(255, 255, 255);
const int ARMOR_POS_XS[RPG_NUM_ARMOR_TYPES] = { 250, 250, 250, 550, 550, 550, 550 };
const int ARMOR_POS_YS[RPG_NUM_ARMOR_TYPES] = { 150, 300, 450, 150, 350, 450, 250 };
/////////////////////////////////////////////////////////////////////////////////////
LPCTSTR pszSkillNames[RPG_NUM_ACTUAL_AUTO_SKILLS + RPG_NUM_ACTUAL_MANUAL_SKILLS] = {
    "Melee", "Archery", "Backstab", "Elemental", "Sense", "Astral",
    //	"Stealth",
    //	"Awareness",
    "Science",
    //	"Stealing",
    //	"Tame"
};
/////////////////////////////////////////////////////////////////////////////////////
namespace
{
int ManualSkillTrainingLimit(int index, int fallback)
{
    const auto *mod = ModRuntime::Current();
    // f01 patches the first displayed manual skill (Science), not a hidden skill.
    return index == 0 && mod && mod->SpellAddon().Feature01() == 1 ? 120 : fallback;
}
}
const int CAMP_NUM_STATS = 8;
LPCTSTR pszStatsNames[CAMP_NUM_STATS] = {
    "Strength",
    "Dexterity",
    "Intelligence",
    "Health",
    "Mana",
    "Actions",
    //"Move",
    "Experience",
    "Encumbrance",
};
/////////////////////////////////////////////////////////////////////////////////////
const COLORREF CONSTR_COLOR_PRICE = RGB(228, 215, 167);
const COLORREF CONSTR_COLOR_NUMBERS = RGB(228, 215, 167);
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInventory::Init(bool bUp)
{
    if (bUp)
    {
        nTopHeight = 0;
        fDepth = 8;
    }
    else
    {
        nTopHeight = 500;
        fDepth = 4;
    }

    SetRect(0, nTopHeight, 800, nTopHeight + 100);

    for (int j = 0; j < 6; j++)
        fCurPos[j] = 0;

    fToScroll = 0;
    fSpeed = 0;
    fLockTime = 0;
    nLock = 0;
    nSortedItems = 0;
    nSelected = -1;
    bIsSorted = true;

    CI3DPolyArea *pArea;
    // scrolling arrows
    pArea = AddArea();
    pArea->MakeRect(30, 35 + nTopHeight, 50, 65 + nTopHeight); // 0
    pArea = AddArea();
    pArea->MakeRect(750, 35 + nTopHeight, 770, 65 + nTopHeight); // 1

    // sort buttons
    AddArea()->MakeRect(0, 5 + nTopHeight, 30, 35 + nTopHeight); // 2
    AddArea()->MakeRect(0, 35 + nTopHeight, 30, 65 + nTopHeight);
    AddArea()->MakeRect(0, 65 + nTopHeight, 30, 95 + nTopHeight);
    AddArea()->MakeRect(770, 5 + nTopHeight, 800, 35 + nTopHeight);
    AddArea()->MakeRect(770, 35 + nTopHeight, 800, 65 + nTopHeight);
    AddArea()->MakeRect(770, 65 + nTopHeight, 800, 95 + nTopHeight); // 7

    // items
    AddArea()->MakeRect(50, nTopHeight, 750, 100 + nTopHeight); // 8

    // left and right blocks
    CI3DFigure *pFig = AddFigure();
    pFig->MakeBar(fDepth * .8f, 100, 0, nTopHeight, 50, 100.f + nTopHeight, 192, 14, 142, 114);

    pFig = AddFigure();
    pFig->MakeBar(fDepth * .8f, 100, 750, nTopHeight, 800, 100.f + nTopHeight, 142, 14, 192, 114);

    // sort buttons
    // armors
    pFig = AddFigure();
    pFig->MakeBar(fDepth * .6f, 80, 0, 5.f + nTopHeight, 30, 35.f + nTopHeight, 2, 130, 42, 170);
    // weapons
    pFig = AddFigure();
    pFig->MakeBar(fDepth * .6f, 80, 0, 35.f + nTopHeight, 30, 65.f + nTopHeight, 2, 172, 42, 212);
    // misc
    pFig = AddFigure();
    pFig->MakeBar(fDepth * .6f, 80, 0, 65.f + nTopHeight, 30, 95.f + nTopHeight, 2, 214, 42, 254);
    // spells
    pFig = AddFigure();
    pFig->MakeBar(fDepth * .6f, 80, 770, 5.f + nTopHeight, 800, 35.f + nTopHeight, 44, 130, 84,
                  170);
    // quick
    pFig = AddFigure();
    pFig->MakeBar(fDepth * .6f, 80, 770, 35.f + nTopHeight, 800, 65.f + nTopHeight, 44, 172, 84,
                  212);
    // quest
    pFig = AddFigure();
    pFig->MakeBar(fDepth * .6f, 80, 770, 65.f + nTopHeight, 800, 95.f + nTopHeight, 44, 214, 84,
                  254);

    pFig = AddFigure();
    pFig->MakeBarFrame2(fDepth * .9f, 100, -40, -40, 40, 40, 5);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(139, 139, 55, 255));
    pFig->SetTexture(0);
    pFig->Show(FALSE);

    pFig = AddFigure();
    pFig->MakeTilingBar(fDepth * 2, 100, 50, nTopHeight, 50 + NUM_VISIBLE_ITEMS * 100,
                        100.f + nTopHeight, 14, 14, 114, 114, NUM_VISIBLE_ITEMS);

    for (int i = 0; i < NUM_VISIBLE_ITEMS; i++)
    {
        pFig = AddFigure();
        pFig->MakeBarFrame2(fDepth, 100, 50.f + i * 100, nTopHeight, 150.f + i * 100,
                            100.f + nTopHeight, 15, 14, 14, 114, 114);
    }

    nSortMode = 5;
    SetSortMode(5);

    hSnd = SND_EMPTY;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInventory::SetSortMode(int nMode)
{
    fCurPos[nSortMode] = (float)(int)(fCurPos[nSortMode] + fToScroll);
    nSortMode = nMode;

    for (int i = 0; i < 6; i++)
        if (i == nMode)
            figures[2 + i]->SetAmbient(1, 1, 1);
        else
            figures[2 + i]->SetAmbient(.5f, .5f, .5f);

    fToScroll = 0;
    fSpeed = 0;
    fLockTime = 0;
    nLock = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInventory::OnIdle()
{
    // bool bNeedCheckStopScroll = false;
    int nCurPos;
    float ftime = GetFrameTime();

    if (nLock)
    {
        int nTimes = 0;
        if (fLockTime == 0)
        {
            nTimes = 1;
            fLockTime = .001f;
        }
        else
        {
            fLockTime += ftime;
            while (fLockTime > INV_PUSH_STEP)
            {
                nTimes++;
                fLockTime -= INV_PUSH_STEP;
            }
        }

        if (nLock == 1)
            fToScroll -= nTimes;
        else
            fToScroll += nTimes;
    }

    if (fToScroll != 0)
    {
        // bNeedCheckStopScroll = true;
        float offset;
        float spd;

        if (fabs(fToScroll) <= INV_BREAK_PATH)
        {
            if (fToScroll > 0)
                spd = (float)(sqrt(fToScroll * INV_BREAK_PATH) / INV_DECEL_TIME);
            else
                spd = (float)(-sqrt(-fToScroll * INV_BREAK_PATH) / INV_DECEL_TIME);
        }
        else
        {
            if (fToScroll > 0)
            {
                fSpeed += INV_ACCELERATE * ftime;
                if (fSpeed > INV_MAX_SPEED)
                    fSpeed = INV_MAX_SPEED;

                spd = Min((fToScroll - INV_BREAK_PATH) / ftime, fSpeed);
                spd = (spd + fSpeed) / 2.f;
            }
            else
            {
                fSpeed -= INV_ACCELERATE * ftime;
                if (fSpeed < -INV_MAX_SPEED)
                    fSpeed = -INV_MAX_SPEED;

                spd = Max((fToScroll + INV_BREAK_PATH) / ftime, fSpeed);
                spd = (spd + fSpeed) / 2.f;
            }
        }

        fSpeed = spd;

        offset = spd * ftime;

        if (fabs(offset) > fabs(fToScroll))
            offset = fToScroll;

        fCurPos[nSortMode] += offset;
        fToScroll -= offset;

        nCurPos = (int)fCurPos[nSortMode];
        if (fCurPos[nSortMode] - nCurPos < .01f && fabs(fToScroll) < .02f)
        {
            fCurPos[nSortMode] = (float)nCurPos;
            fToScroll = 0;
        }
        else if (fCurPos[nSortMode] - nCurPos > .99f && fabs(fToScroll) < .02f)
        {
            fCurPos[nSortMode] = nCurPos + 1.f;
            fToScroll = 0;
        }
    }

    if (fToScroll == 0)
        fSpeed = 0;

    FormSortedList();

    nCurPos = (int)fCurPos[nSortMode];

    if (fCurPos[nSortMode] > float(nSortedItems - NUM_VISIBLE_ITEMS + 1))
    {
        nCurPos = nSortedItems - NUM_VISIBLE_ITEMS + 1;
        fCurPos[nSortMode] = (float)nCurPos;
        fToScroll = 0;
        fSpeed = 0;
    }
    if (fCurPos[nSortMode] < 0)
    {
        nCurPos = 0;
        fCurPos[nSortMode] = 0;
        fToScroll = 0;
        fSpeed = 0;
    }
    /*
    if ( fToScroll == 0 && hSnd != SND_EMPTY )
    {
            pI3D->StopFX( hSnd );
            hSnd = SND_EMPTY;
    }
    */
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInventory::OnLButtonDown(UINT nFlags, CPoint point)
{
    int ht = HitTest(point);
    JournalHit(ht);
    if (ht == 0)
    {
        if (pI3D->IsShiftDown())
        {
            fCurPos[nSortMode] = 0;
            fToScroll = 0;
            fSpeed = 0;
        }
        else
        {
            // if ( hSnd == SND_EMPTY )
            //	hSnd = pI3D->BeginFX( "buttons\\camp\\scroll.wav", TRUE );
            pI3D->BeginFX("buttons\\camp\\scroll.wav");
            pI3D->SetCapture(this);
            nLock = 1;
        }
    }
    if (ht == 1)
    {
        if (pI3D->IsShiftDown())
        {
            fCurPos[nSortMode] = 1e6;
            fToScroll = 0;
            fSpeed = 0;
        }
        else
        {
            // if ( hSnd == SND_EMPTY )
            //	hSnd = pI3D->BeginFX( "buttons\\camp\\scroll.wav", TRUE );
            pI3D->BeginFX("buttons\\camp\\scroll.wav");
            pI3D->SetCapture(this);
            nLock = 2;
        }
    }
    if (bIsSorted && ht >= 2 && ht < 8)
    {
        pI3D->BeginFX("buttons\\camp\\mode.wav");
        SetSortMode(ht - 2);
    }
    if (ht >= 0)
        SetProcessed();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInventory::OnMouseMove(UINT nFlags, CPoint point)
{
    int ht = HitTest(point);
    if (nLock == 1 && ht != 0 || nLock == 2 && ht != 1)
    {
        if (fToScroll < 0)
        {
            fToScroll = fToScroll - (float)(int)fToScroll;
            if (fToScroll > -INV_BREAK_PATH)
                fToScroll -= 1.f;
        }
        else if (fToScroll > 0)
        {
            fToScroll = fToScroll - (float)(int)fToScroll;
            if (fToScroll < INV_BREAK_PATH)
                fToScroll += 1.f;
        }
        nLock = 0;
        fLockTime = 0;
        pI3D->ReleaseCapture();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInventory::OnLButtonUp(UINT nFlags, CPoint point)
{
    if (nLock)
    {
        if (fToScroll < 0)
        {
            fToScroll = fToScroll - (float)(int)fToScroll;
            if (fToScroll > -INV_BREAK_PATH)
                fToScroll -= 1.f;
        }
        else if (fToScroll > 0)
        {
            fToScroll = fToScroll - (float)(int)fToScroll;
            if (fToScroll < INV_BREAK_PATH)
                fToScroll += 1.f;
        }
        nLock = 0;
        fLockTime = 0;
        pI3D->ReleaseCapture();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DObjectInventory::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    int ht = HitTest(pt);
    if (ht == 8)
    {
        fToScroll += zDelta / WHEEL_DELTA;
        // if ( hSnd == SND_EMPTY )
        //	hSnd = pI3D->BeginFX( "buttons\\camp\\scroll.wav", TRUE );
        pI3D->BeginFX("buttons\\camp\\scroll.wav");
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInventoryItems::Init(CSimplyArray<CItem *, CItem *> *pItems, bool bUp)
{
    if (bUp)
        AddTexture("Inventory02");
    else
        AddTexture("Inventory01");

    CI3DObjectInventory::Init(bUp);

    GetArea(0)->SetToolTip(20400);
    GetArea(1)->SetToolTip(20401);

    AddSurface(dbgnew CI3DSurface(pI3D, 50, 15 + nTopHeight, 750, 35 + nTopHeight));
    AddSurface(dbgnew CI3DSurface(pI3D, 50, 65 + nTopHeight, 750, 85 + nTopHeight));
    ePriceType = ISK_TOTAL;

    pAllItems = pItems;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInventoryItems::OnLButtonDown(UINT nFlags, CPoint point)
{
    CI3DObjectInventory::OnLButtonDown(nFlags, point);
    int nSlotType;
    CItem *pItem = GetMouseItem(point, nSlotType);
    if (pItem)
        pScreen->ClickOnItem(pItem, ePartType);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInventoryItems::FormSortedList()
{
    pSortedItems.RemoveAll();
    if (nSortType == 0 && nSortMode == 5)
    {
        for (int i = 0; i < pAllItems->GetSize(); i++)
            pSortedItems.Add((*pAllItems)[i]);
    }
    else
    {
        for (int i = 0; i < pAllItems->GetSize(); i++)
            if (GetItemSortMode((*pAllItems)[i]) == nSortMode)
                pSortedItems.Add((*pAllItems)[i]);
    }
    nSortedItems = pSortedItems.GetSize();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInventoryItems::SetSortType(int nType)
{
    int i;
    for (i = 0; i < 6; i++)
        fCurPos[i] = 0;
    fToScroll = 0;
    fSpeed = 0;

    nSortType = nType;

    switch (nSortType)
    {
    case 2:
        for (i = 2; i < 8; i++)
        {
            figures[i]->SetUShift(0);
            GetArea(i)->SetToolTip(21000 + i - 2);
        }
        break;
    case 1:
        for (i = 2; i < 8; i++)
        {
            figures[i]->SetUShift(84 / 256.f);
            GetArea(i)->SetToolTip(21200 + i - 2);
        }
        break;
    case 0:
        for (i = 2; i < 8; i++)
        {
            figures[i]->SetUShift(168 / 256.f);
            GetArea(i)->SetToolTip(21100 + i - 2);
        }
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInventoryItems::SwitchToMode(int nMode)
{
    if (theSettings.nSwitchFilters == 0)
        return;
    SetSortMode(nMode);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInventoryItems::SwitchToAlternativeMode(int nMode, int nCount)
{
    if (theSettings.nSwitchFilters == 0)
        return;
    int nNewMode = nMode;
    if (nMode <= nSortMode && nSortMode < nMode + nCount)
    {
        nNewMode = nSortMode + 1;
        if (nNewMode == nMode + nCount)
            nNewMode = nMode;
    }
    SetSortMode(nNewMode);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInventoryItems::SetToItem(CItem *pItem)
{
    if (!pItem)
        return;

    float fNewCurPos = -1;
    pSortedItems.RemoveAll();
    if (nSortType == 0 && nSortMode == 5)
    {
        for (int i = 0; i < pAllItems->GetSize(); i++)
        {
            if (pItem->Compare((*pAllItems)[i]))
                fNewCurPos = pSortedItems.GetSize();
            pSortedItems.Add((*pAllItems)[i]);
        }
    }
    else
    {
        SwitchToMode(GetItemSortMode(pItem));
        for (int i = 0; i < pAllItems->GetSize(); i++)
            if (GetItemSortMode((*pAllItems)[i]) == nSortMode)
            {
                if (pItem->Compare((*pAllItems)[i]))
                    fNewCurPos = pSortedItems.GetSize();
                pSortedItems.Add((*pAllItems)[i]);
            }
    }

    if (fNewCurPos >= 0)
    {
        if (fCurPos[nSortMode] < fNewCurPos - NUM_VISIBLE_ITEMS + 2)
            fCurPos[nSortMode] = fNewCurPos - NUM_VISIBLE_ITEMS + 2;
        else if (fCurPos[nSortMode] > fNewCurPos)
            fCurPos[nSortMode] = fNewCurPos;
    }

    fToScroll = 0;
    fSpeed = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
int CI3DObjectInventoryItems::GetItemSortMode(CItem *pItem)
{
    switch (nSortType)
    {
    case 0:
        return SortFunction0(pItem);
        break;
    case 1:
        return SortFunction1(pItem);
        break;
    case 2:
        return SortFunction2(pItem);
        break;
    }
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
int CI3DObjectInventoryItems::SortFunction0(CItem *pItem)
{
    if (pScreen->IsItemInstruction(pItem) || pScreen->IsItemMaterial(pItem))
        return 0;
    else if (pScreen->IsItemPrototype(pItem) || pScreen->IsItemModifier(pItem))
        return 1;
    else if (pItem->GetTypeID() == ITID_ITEM_WEAPON || pItem->GetTypeID() == ITID_ITEM_ARMOR ||
             pItem->GetTypeID() == ITID_ITEM_QUICK || pItem->GetTypeID() == ITID_ITEM_SPELL)
        return 2;
    else if (pItem->GetTypeID() == ITID_ITEM_LOOT)
        return 3;
    else if (pItem->GetTypeID() == ITID_ITEM_QUEST)
        return 4;
    return 5;
}
/////////////////////////////////////////////////////////////////////////////////////
int CI3DObjectInventoryItems::SortFunction1(CItem *pItem)
{
    if (pScreen->IsItemPrototype(pItem))
    {
        CItemLoot *pLoot = static_cast<CItemLoot *>(pItem);
        CSpellManager *pSM = theWorld.GetSpellManager();
        const SSDBSpellPrototype *pPrototype = pSM->GetPrototype(pLoot->GetInstructionItemIndex());
        const auto* mod = ModRuntime::Current();
        const int group = mod && mod->CombatSetting("JMVPerks")
            ? int(pPrototype->mstSubtype) : int(pPrototype->msSchool);
        switch (group)
        {
        case RPGMS_ELEMENTAL:
            return 0;
        case RPGMS_SENSES:
            return 1;
        case RPGMS_ASTRAL:
            return 2;
        }
    }
    else if (pScreen->IsItemModifier(pItem))
    {
        CItemLoot *pLoot = static_cast<CItemLoot *>(pItem);
        CSpellManager *pSM = theWorld.GetSpellManager();
        const SSDBModifier *pModifier = pSM->GetModifier(pLoot->GetInstructionItemIndex());
        if (pModifier->smtType > SDBSMT_MANA)
            return 4;
        else
            return 3;
    }
    return 5;
}
/////////////////////////////////////////////////////////////////////////////////////
int CI3DObjectInventoryItems::SortFunction2(CItem *pItem)
{
    if (pScreen->IsItemInstruction(pItem))
    {
        CItemLoot *pLoot = static_cast<CItemLoot *>(pItem);
        switch (pLoot->GetInstructionType())
        {
        case IDBINS_WEAPON:
            return 0;
        case IDBINS_ARMOR: {
            CItemManager *pIM = static_cast<CItemManager *>(pItem->GetManager());
            const SIDBItemGeneralShape *pShape = pIM->GetArmorShape(
                pLoot->GetInstructionItemIndex());
            if (pShape->dwTypeID > RPGAST_LEGGINGS)
                return 1;
            else
                return 2;
        }
        case IDBINS_QUICK:
            return 3;
        }
    }
    else
    {
        if (pItem->GetTypeID() == ITID_ITEM_QUICK)
            return 3;
        else if (pScreen->IsItemMaterial(pItem))
            return 4;
    }
    return 5;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DObjectInventoryItems::Draw(CRenderer *pRend)
{
    CItem* pVisItems[NUM_VISIBLE_ITEMS];
    SDrawItem item;
    int i;

    int nCurPos = (int)fCurPos[nSortMode];
    float shift = fCurPos[nSortMode] - (float)nCurPos;

    if (fCurPos[nSortMode] > 0)
        figures[0]->SetUShift(50.f / 256);
    else
        figures[0]->SetUShift(0);

    if (fCurPos[nSortMode] < float(pSortedItems.GetSize() - NUM_VISIBLE_ITEMS + 1))
        figures[1]->SetUShift(50.f / 256);
    else
        figures[1]->SetUShift(0);

    figures[9]->SetRelativeToScreen(50.f - shift * 100, (float)nTopHeight, fDepth * 2);
    for (i = 0; i < NUM_VISIBLE_ITEMS; i++)
        figures[10 + i]->SetRelativeToScreen(100.f + (i - shift) * 100, 50.f + nTopHeight, fDepth);

    CI3DObject::Draw(pRend);

    // figures
    for (i = 0; i < NUM_VISIBLE_ITEMS; i++)
        if (nCurPos + i >= 0 && nCurPos + i < pSortedItems.GetSize())
        {
            pVisItems[i] = pSortedItems[nCurPos + i];
            item.pItem = pVisItems[i];
            item.fX = 100.f + (i - shift) * 100;
            item.fY = 50.f + nTopHeight;
            item.fZ = fDepth * 1.5f;
            item.fPixSize = 100;
            item.eFrom = ePartType;
            pScreen->DrawItem(pRend, item);
        }
        else
            pVisItems[i] = NULL;

    // numbers
    CString szNum;
#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    pSurf->GetDC();
    pSurf->Clear();
    for (i = 0; i < NUM_VISIBLE_ITEMS; i++)
        if (pVisItems[i] && pVisItems[i]->GetCount() != 1)
        {
            szNum.Format("%d", pVisItems[i]->GetCount());
            pSurf->DrawText(szNum,
                            CRect(int((i - shift) * 100) + 15, 0, int((i - shift) * 100) + 85, 20),
                            1, INV_COLOR_NUMBERS, DT_RIGHT);
        }
    pSurf->ReleaseDC();

    pSurf = surfaces[1];
    pSurf->GetDC();
    pSurf->Clear();
    for (i = 0; i < NUM_VISIBLE_ITEMS; i++)
        if (pVisItems[i] && ePriceType != ISK_TOTAL &&
            pScreen->CanClickOnItem(pVisItems[i], ePartType))
        {
            szNum.Format("%d", pScreen->GetItemPrice(pVisItems[i], ePriceType));
            pSurf->DrawText(szNum,
                            CRect(int((i - shift) * 100) + 15, 0, int((i - shift) * 100) + 85, 20),
                            1, INV_COLOR_PRICE, DT_CENTER);
        }
    pSurf->ReleaseDC();
#endif // NOGETDC

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInventoryItems::SetPriceType(EItemScreenKoeffs eNewType)
{
    ePriceType = eNewType;
}
/////////////////////////////////////////////////////////////////////////////////////
CItem *CI3DObjectInventoryItems::GetMouseItem(CPoint point, int &nSlotType)
{
    if (!IsVisible())
        return 0;
    int ht = HitTest(point);
    if (ht == 8)
    {
        switch (ePartType)
        {
        case ISP_WAGON:
            nSlotType = IST_WAGON;
            break;
        case ISP_SPELLASSORT:
            nSlotType = IST_SPELL_ASSORT;
            break;
        case ISP_ITEMASSORT:
            nSlotType = IST_ITEM_ASSORT;
            break;
        }

        CPoint pt(point);
        pI3D->ScreenToVirtual(pt);
        int nItem = int((pt.x - 50) / 100.f + fCurPos[nSortMode]);
        FormSortedList();
        if (nItem >= 0 && nItem < pSortedItems.GetSize())
            return pSortedItems[nItem];
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectQuickSlots::Init(bool bArmors)
{
    bDrawArmors = bArmors;
    if (bDrawArmors)
        SetRect(0, 0, 800, 500);
    else
        SetRect(0, 0, 800, 100);

    ePartType = ISP_CAMPSLOTS;

    // weapons and items
    for (int i = 0; i < 8; i++)
        AddArea()->MakeRect(i * 100, 0, (i + 1) * 100, 100);

    CI3DFigure *pFig;

    pFig = AddFigure();
    pFig->MakeBarFrame2(3, 100, -40, -40, 40, 40, 5);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(139, 139, 55, 255));
    pFig->SetTexture(0);
    pFig->Show(FALSE);

    pFig = AddFigure();
    pFig->MakeBarFrame2(3, 100, -40, -40, 40, 40, 5);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(139, 139, 55, 255));
    pFig->SetTexture(0);
    pFig->Show(FALSE);

    for (int i = 0; i < 4; i++)
    {
        pFig = AddFigure();
        pFig->MakeBarFrame2(8, 100, i * 100.f, 0, (i + 1) * 100.f, 100, 15, 14, 14, 114, 114);
    }
    pFig = AddFigure();
    pFig->MakeTilingBar(16, 100, 0, 0, 400, 100, 14, 14, 114, 114, 4);

    for (int i = 4; i < 8; i++)
    {
        pFig = AddFigure();
        pFig->MakeBarFrame2(8, 100, i * 100.f, 0, (i + 1) * 100.f, 100, 15, 142, 14, 242, 114);
    }
    pFig = AddFigure();
    pFig->MakeTilingBar(16, 100, 400, 0, 800, 100, 142, 14, 242, 114, 4);

    if (bDrawArmors)
    {
        for (int i = 0; i < RPG_NUM_ARMOR_TYPES; i++)
            AddArea()->MakeRect(ARMOR_POS_XS[i] - 50, ARMOR_POS_YS[i] - 50, ARMOR_POS_XS[i] + 50,
                                ARMOR_POS_YS[i] + 50);

        AddTexture("camp1");
        AddTexture("camp2");
        AddTexture("camp3");
        AddTexture("camp4");

        pFig = AddFigure();
        pFig->MakeBar(7, 100, 200, 100, 400, 300, 28, 28, 228, 228);
        pFig->SetTexture(textures[1]);
        pFig = AddFigure();
        pFig->MakeBar(7, 100, 400, 100, 600, 300, 28, 28, 228, 228);
        pFig->SetTexture(textures[2]);
        pFig = AddFigure();
        pFig->MakeBar(7, 100, 200, 300, 400, 500, 28, 28, 228, 228);
        pFig->SetTexture(textures[3]);
        pFig = AddFigure();
        pFig->MakeBar(7, 100, 400, 300, 600, 500, 28, 28, 228, 228);
        pFig->SetTexture(textures[4]);
    }

    nCurrentQItem = -1;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DObjectQuickSlots::Draw(CRenderer *pRend)
{
    CPersonage *pPers = pScreen->GetCurrentPers();

    if (pPers->dwCurrentWeapon >= 0 && pPers->dwCurrentWeapon < pPers->weapons.GetSize())
    {
        figures[0]->Show(TRUE);
        figures[0]->SetRelativeToScreen(50 + pPers->dwCurrentWeapon * 100, 50, 3);
    }
    else
        figures[0]->Show(FALSE);

    if (nCurrentQItem >= 0 && nCurrentQItem < pPers->items.GetSize())
    {
        figures[1]->Show(TRUE);
        figures[1]->SetRelativeToScreen(750 - nCurrentQItem * 100, 50, 3);
    }
    else
        figures[1]->Show(FALSE);

    CI3DObject::Draw(pRend);

    SDrawItem item;

    for (int i = 0; i < pPers->weapons.GetSize(); i++)
    {
        item.pItem = pPers->weapons[i];
        item.fX = 50 + i * 100;
        item.fY = 50;
        item.fZ = 12;
        item.fPixSize = 100;
        item.eFrom = ePartType;
        pScreen->DrawItem(pRend, item);
    }
    for (int i = 0; i < pPers->items.GetSize(); i++)
    {
        item.pItem = pPers->items[i];
        item.fX = 750 - i * 100;
        item.fY = 50;
        item.fZ = 12;
        item.fPixSize = 100;
        item.eFrom = ePartType;
        pScreen->DrawItem(pRend, item);
    }

    if (bDrawArmors)
    {
        CRect rcTmp;
        D3DRECT rects[RPG_NUM_ARMOR_TYPES];
        for (int i = 0; i < RPG_NUM_ARMOR_TYPES; i++)
        {
            rcTmp.left = ARMOR_POS_XS[i] - 35;
            rcTmp.top = ARMOR_POS_YS[i] - 35;
            rcTmp.right = ARMOR_POS_XS[i] + 35;
            rcTmp.bottom = ARMOR_POS_YS[i] + 35;
            pI3D->VirtualToScreen(rcTmp);
            rects[i].x1 = rcTmp.left;
            rects[i].y1 = rcTmp.top;
            rects[i].x2 = rcTmp.right;
            rects[i].y2 = rcTmp.bottom;
        }

        pI3D->GetRenderer()->FlushBuffers();
        pI3D->GetRenderer()->Clear(0, 0, D3DCLEAR_ZBUFFER, 0, 0, 0);
        pI3D->GetRenderer()->Clear(RPG_NUM_ARMOR_TYPES, rects, D3DCLEAR_ZBUFFER, 0, 1, 0);

        for (int i = 0; i < pPers->armors.GetSize(); i++)
        {
            item.pItem = pPers->armors[i];
            int type = ((CItemArmor *)item.pItem)->GetArmorType();
            item.fX = ARMOR_POS_XS[type];
            item.fY = ARMOR_POS_YS[type];
            item.fZ = 6;
            item.fPixSize = 100;
            item.eFrom = ePartType;
            pScreen->DrawItem(pRend, item);
        }

        pI3D->GetRenderer()->FlushBuffers();
        pI3D->GetRenderer()->Clear(0, 0, D3DCLEAR_ZBUFFER, 0, 1, 0);
        pI3D->GetRenderer()->FlushBuffers();
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
CItem *CI3DObjectQuickSlots::GetMouseItem(CPoint point, int &nSlotType)
{
    nSlotType = 0;
    if (!IsVisible())
        return 0;
    int ht = HitTest(point);
    if (ht < 0)
        return 0;
    CPersonage *pPers = pScreen->GetCurrentPers();
    if (ht >= 0 && ht < 4) // weapon
    {
        nSlotType = IST_WEAPON;
        if (ht < pPers->weapons.GetSize())
            return pPers->weapons[ht];
    }
    if (ht >= 4 && ht < 8) // item
    {
        nSlotType = IST_QITEM;
        if (7 - ht < pPers->items.GetSize())
            return pPers->items[7 - ht];
    }
    if (!bDrawArmors)
        return 0;
    if (ht >= 8 && ht < 15) // armor
    {
        nSlotType = IST_ARMOR_HELM + ht - 8;
        for (int i = 0; i < pPers->armors.GetSize(); i++)
            if (pPers->armors[i]->GetArmorType() == ht - 8)
                return pPers->armors[i];
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectQuickSlots::OnRButtonDown(UINT nFlags, CPoint point)
{
    if (!bDrawArmors)
        return;
    if (!IsVisible())
        return;
    int ht = HitTest(point);
    JournalHit(ht);
    if (ht < 0)
        return;
    CPersonage *pPers = pScreen->GetCurrentPers();
    if (ht >= 0 && ht < pPers->weapons.GetSize() && ht < 4)
    {
        pI3D->BeginFX("buttons\\camp\\change.wav");
        pPers->dwCurrentWeapon = ht;
    }
    pScreen->UpdateWeapons();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectQuickSlots::SetCurrentWeapon(CItem *pWeapon)
{
    CPersonage *pPers = pScreen->GetCurrentPers();
    pPers->dwCurrentWeapon = -1;
    for (int i = 0; i < pPers->weapons.GetSize(); i++)
        if (pPers->weapons[i] == pWeapon)
        {
            pPers->dwCurrentWeapon = i;
            break;
        }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectQuickSlots::SetCurrentQItem(CItem *pQuick)
{
    CPersonage *pPers = pScreen->GetCurrentPers();
    nCurrentQItem = -1;
    for (int i = 0; i < pPers->items.GetSize(); i++)
        if (pPers->items[i] == pQuick)
        {
            nCurrentQItem = i;
            break;
        }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectSpellSlots::Init(bool bUp)
{
    if(auto*mod=ModRuntime::Current())mod->GrantSkillPrototypes(IWorldClient.GetParty());
    nTopHeight = bUp ? 0 : 500;
    const float inventoryDepth = bUp ? 8.f : 4.f;

    SetRect(0, nTopHeight, 800, nTopHeight + 100);
    ePartType = ISP_CAMPSLOTS;
    nCurrentSpell = -1;
    nFirstSpell = 0;
    fSpellPosition = 0.f;
    fSpellToScroll = 0.f;
    fSpellScrollSpeed = 0.f;

    for (int i = 0; i < 7; ++i)
        AddArea()->MakeRect(50 + i * 100, nTopHeight,
                            150 + i * 100, nTopHeight + 100);
    AddArea()->MakeRect(0, 0, 0, 0);
    AddArea()->MakeRect(30, 35 + nTopHeight, 50, 65 + nTopHeight);
    AddArea()->MakeRect(750, 35 + nTopHeight, 770, 65 + nTopHeight);
    GetArea(8)->SetToolTip(20400);
    GetArea(9)->SetToolTip(20401);

    CI3DFigure *pFig = AddFigure();
    pFig->MakeBarFrame2(3, 100, -40, -40, 40, 40, 5);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(139, 139, 55, 255));
    pFig->SetTexture(0);
    pFig->Show(FALSE);

    pFig = AddFigure();
    pFig->MakeTilingBar(inventoryDepth * 2.f, 100,
                        50, nTopHeight, 850, nTopHeight + 100,
                        14, 142, 114, 242, 8);

    pFig = AddFigure();
    pFig->MakeBar(inventoryDepth * .8f, 100, 0, nTopHeight,
                  50, 100.f + nTopHeight, 192, 14, 142, 114);
    pFig = AddFigure();
    pFig->MakeBar(inventoryDepth * .8f, 100, 750, nTopHeight,
                  800, 100.f + nTopHeight, 142, 14, 192, 114);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectSpellSlots::SetCurrentSpell(CItem *pSpell)
{
    CPersonage *pPers = pScreen ? pScreen->GetCurrentPers() : 0;
    nCurrentSpell = -1;
    if (!pPers)
        return;

    for (int i = 0; i < pPers->spells.GetSize(); ++i)
        if (pPers->spells[i] == pSpell)
        {
            nCurrentSpell = i;
            const int maxFirst = max(0, min(pPers->spells.GetSize(),
                static_cast<int>(QI_MAX_SPELLS)) - 7);
            nFirstSpell = min(maxFirst, max(0, i - 6));
            fSpellPosition = static_cast<float>(nFirstSpell);
            fSpellToScroll = 0.f;
            fSpellScrollSpeed = 0.f;
            break;
        }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DObjectSpellSlots::Draw(CRenderer *pRend)
{
    CPersonage *pPers = pScreen ? pScreen->GetCurrentPers() : 0;
    if (!pPers || figures.GetSize() < 4)
        return CI3DObject::Draw(pRend);

    const int spellCount = min(pPers->spells.GetSize(),
                               static_cast<int>(QI_MAX_SPELLS));
    const int maxFirst = max(0, spellCount - 7);
    if (fSpellPosition < 0.f || fSpellPosition > static_cast<float>(maxFirst))
    {
        fSpellPosition = min(static_cast<float>(maxFirst), max(0.f, fSpellPosition));
        fSpellToScroll = 0.f;
        fSpellScrollSpeed = 0.f;
    }

    if (fSpellToScroll != 0.f)
    {
        const float frameTime = max(.001f, GetFrameTime());
        float speed;
        if (fabs(fSpellToScroll) <= INV_BREAK_PATH)
        {
            speed = fSpellToScroll > 0.f
                ? static_cast<float>(sqrt(fSpellToScroll * INV_BREAK_PATH) / INV_DECEL_TIME)
                : static_cast<float>(-sqrt(-fSpellToScroll * INV_BREAK_PATH) / INV_DECEL_TIME);
        }
        else if (fSpellToScroll > 0.f)
        {
            fSpellScrollSpeed = min(INV_MAX_SPEED,
                                    fSpellScrollSpeed + INV_ACCELERATE * frameTime);
            speed = min((fSpellToScroll - INV_BREAK_PATH) / frameTime,
                        fSpellScrollSpeed);
            speed = (speed + fSpellScrollSpeed) / 2.f;
        }
        else
        {
            fSpellScrollSpeed = max(-INV_MAX_SPEED,
                                    fSpellScrollSpeed - INV_ACCELERATE * frameTime);
            speed = max((fSpellToScroll + INV_BREAK_PATH) / frameTime,
                        fSpellScrollSpeed);
            speed = (speed + fSpellScrollSpeed) / 2.f;
        }

        fSpellScrollSpeed = speed;
        float offset = speed * frameTime;
        if (fabs(offset) > fabs(fSpellToScroll))
            offset = fSpellToScroll;
        fSpellPosition += offset;
        fSpellToScroll -= offset;

        const int roundedPosition = static_cast<int>(floor(fSpellPosition + .5f));
        if (fabs(fSpellPosition - roundedPosition) < .01f &&
            fabs(fSpellToScroll) < .02f)
        {
            fSpellPosition = static_cast<float>(roundedPosition);
            fSpellToScroll = 0.f;
        }
    }
    if (fSpellToScroll == 0.f)
        fSpellScrollSpeed = 0.f;

    fSpellPosition = min(static_cast<float>(maxFirst), max(0.f, fSpellPosition));
    nFirstSpell = static_cast<int>(floor(fSpellPosition));
    const float shift = fSpellPosition - static_cast<float>(nFirstSpell);
    const int visibleEnd = min(nFirstSpell + 7, spellCount);
    const int renderEnd = min(nFirstSpell + 8, spellCount);
    const float inventoryDepth = nTopHeight == 0 ? 8.f : 4.f;

    figures[1]->SetRelativeToScreen(50.f - shift * 100.f,
                                    static_cast<float>(nTopHeight),
                                    inventoryDepth * 2.f);
    if (nCurrentSpell >= nFirstSpell && nCurrentSpell < visibleEnd)
    {
        figures[0]->Show(TRUE);
        figures[0]->SetRelativeToScreen(
            100.f + (nCurrentSpell - nFirstSpell - shift) * 100.f,
            nTopHeight + 50.f, 3.f);
    }
    else
        figures[0]->Show(FALSE);

    figures[2]->SetUShift(fSpellPosition > 0.f ? 50.f / 256.f : 0.f);
    figures[3]->SetUShift(fSpellPosition < static_cast<float>(maxFirst)
                              ? 50.f / 256.f : 0.f);
    CI3DObject::Draw(pRend);

    for (int i = nFirstSpell; i < renderEnd; ++i)
    {
        SDrawItem item;
        item.pItem = pPers->spells[i];
        item.fX = 100.f + (i - nFirstSpell - shift) * 100.f;
        item.fY = nTopHeight + 50;
        item.fZ = 12;
        item.fPixSize = 100;
        item.eFrom = ePartType;
        pScreen->DrawItem(pRend, item);
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
CItem *CI3DObjectSpellSlots::GetMouseItem(CPoint point, int &nSlotType)
{
    nSlotType = 0;
    if (!IsVisible())
        return 0;

    const int hit = HitTest(point);
    CPersonage *pPers = pScreen ? pScreen->GetCurrentPers() : 0;
    if (!pPers || hit < 0 || hit >= 7)
        return 0;

    nSlotType = IST_SPELL;
    const int spellIndex = static_cast<int>(floor(fSpellPosition)) + hit;
    return spellIndex < min(pPers->spells.GetSize(), static_cast<int>(QI_MAX_SPELLS))
        ? pPers->spells[spellIndex]
        : 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectSpellSlots::OnLButtonDown(UINT nFlags, CPoint point)
{
    const int hit = HitTest(point);
    if (hit == 8 || hit == 9)
    {
        CPersonage *pPers = pScreen ? pScreen->GetCurrentPers() : 0;
        if (!pPers)
            return;
        const int spellCount = min(pPers->spells.GetSize(),
                                   static_cast<int>(QI_MAX_SPELLS));
        const int maxFirst = max(0, spellCount - 7);
        const float destination = pI3D->IsShiftDown()
            ? (hit == 8 ? 0.f : static_cast<float>(maxFirst))
            : min(static_cast<float>(maxFirst), max(0.f,
                fSpellPosition + (hit == 8 ? -1.f : 1.f)));
        fSpellToScroll = destination - fSpellPosition;
        if (fSpellToScroll == 0.f)
            fSpellScrollSpeed = 0.f;
        pI3D->BeginFX("buttons\\camp\\scroll.wav");
        SetProcessed();
        return;
    }
    CI3DItemObject::OnLButtonDown(nFlags, point);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DObjectSpellSlots::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    if (!IsVisible() || HitTest(pt) < 0)
        return FALSE;

    CPersonage *pPers = pScreen ? pScreen->GetCurrentPers() : 0;
    if (!pPers)
        return FALSE;
    const int spellCount = min(pPers->spells.GetSize(),
                               static_cast<int>(QI_MAX_SPELLS));
    const int maxFirst = max(0, spellCount - 7);
    if (maxFirst == 0)
        return FALSE;

    const float wheelSteps = -static_cast<float>(zDelta) /
                             static_cast<float>(WHEEL_DELTA);
    const float destination = min(static_cast<float>(maxFirst),
        max(0.f, fSpellPosition + wheelSteps));
    fSpellToScroll = destination - fSpellPosition;
    pI3D->BeginFX("buttons\\camp\\scroll.wav");
    SetProcessed();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectNameStats::Init()
{
    SetRect(0, 100, 200, 400);

    CI3DPolyArea *pArea;
    pArea = AddArea();
    pArea->MakeRect(0, 125, 30, 145);
    pArea = AddArea();
    pArea->MakeRect(170, 125, 200, 145);

    AddSurface(dbgnew CI3DSurface(pI3D, 0, 100, 200, 400)); // skills

    CI3DFigure *pFig;
    // next / prev personage
    pFig = AddFigure();
    pFig->MakeBar(5, 100, 176, 125, 192, 145, 2, 129, 18, 149);
    pFig = AddFigure();
    pFig->MakeBar(5, 100, 8, 125, 24, 145, 18, 129, 2, 149);

    pFig = AddFigure();
    pFig->MakeBar(6, 100, 14, 110, 100, 160, 231, 76, 145, 136);
    pFig = AddFigure();
    pFig->MakeBar(6, 100, 100, 110, 186, 160, 145, 76, 231, 136);
    /*
    144, 177, 164, 192
    144, 161, 164, 176
    */
    AddFigure()->MakeBar(5, 100, 140, 165, 160, 180, 144, 177, 164, 192);
    AddFigure()->MakeBar(5, 100, 140, 180, 160, 195, 144, 177, 164, 192);
    AddFigure()->MakeBar(5, 100, 140, 195, 160, 210, 144, 177, 164, 192);
    AddFigure()->MakeBar(5, 100, 140, 240, 160, 255, 144, 177, 164, 192);
    AddFigure()->MakeBar(5, 100, 160, 165, 180, 180, 144, 177, 164, 192);
    AddFigure()->MakeBar(5, 100, 160, 180, 180, 195, 144, 177, 164, 192);
    AddFigure()->MakeBar(5, 100, 160, 195, 180, 210, 144, 177, 164, 192);
    AddFigure()->MakeBar(5, 100, 160, 240, 180, 255, 144, 177, 164, 192);

    CTextManager *pTxtMng = theWorld.GetTextManager();
    CString szTmp;
    pTxtMng->GetSystemString("infounit_3", szTmp);
    szStrings.Add(szTmp);
    pTxtMng->GetSystemString("infounit_4", szTmp);
    szStrings.Add(szTmp);
    pTxtMng->GetSystemString("infounit_9", szTmp);
    szStrings.Add(szTmp);
    pTxtMng->GetSystemString("infounit_14", szTmp);
    szStrings.Add(szTmp);
    pTxtMng->GetSystemString("infounit_17", szTmp);
    szStrings.Add(szTmp);
    pTxtMng->GetSystemString("lmp_points_left", szTmp);
    szStrings.Add(szTmp);
    pTxtMng->GetSystemString("lmp_pers_tall", szTmp);
    szStrings.Add(szTmp);
    pTxtMng->GetSystemString("magic_skills", szTmp);
    szStrings.Add(szTmp);

    nLock = -1;
    fTime = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectNameStats::SetPersonage(int nNewCurPers, LPCTSTR pszName)
{
    StopPrimaryRepeat();
    if (IWorldClient.GetParty().people.GetSize() > 1)
    {
        figures[0]->Show(TRUE);
        figures[1]->Show(TRUE);
        areas[0]->SetToolTip(20300);
        areas[1]->SetToolTip(20301);
    }
    else
    {
        figures[0]->Show(FALSE);
        figures[1]->Show(FALSE);
        areas[0]->SetToolTip(0);
        areas[1]->SetToolTip(0);
    }

    nCurPers = nNewCurPers;
    if (pszName)
        szPersName = pszName;
    nTall = (int)(pScreen->GetCurrentPers()->complection.tall * 100);

    UpdateStats();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectNameStats::SetScreenMode(EItemScreenModes eCampMode)
{
    StopPrimaryRepeat();
    eCurMode = eCampMode;
    CPoint shift(0, 15);
    CRect rc(20, 165, 180, 180);
    int i;
    while (areas.GetSize() > 2)
        RemoveArea(2);
    for (i = 4; i < figures.GetSize(); i++)
        figures[i]->Show(FALSE);
    switch (eCurMode)
    {
    case ISM_SKIN:
        for (i = 4; i < figures.GetSize(); i++)
            figures[i]->Show(TRUE);
        rc = CRect(20, 165, 140, 180);
        for (i = 0; i < 4; i++)
        {
            AddArea()->MakeRect(rc);
            rc += shift;
            if (i == 2)
            {
                rc += shift;
                rc += shift;
            }
        }
        rc = CRect(140, 165, 160, 180);
        for (i = 0; i < 4; i++)
        {
            AddArea()->MakeRect(rc);
            rc += shift;
            if (i == 2)
            {
                rc += shift;
                rc += shift;
            }
        }
        rc = CRect(160, 165, 180, 180);
        for (i = 0; i < 4; i++)
        {
            AddArea()->MakeRect(rc);
            rc += shift;
            if (i == 2)
            {
                rc += shift;
                rc += shift;
            }
        }
        AddArea()->MakeRect(20, 210, 180, 225);
        break;

    case ISM_WEAPONS:
        rc += shift;
        for (i = 0; i < 4; i++)
        {
            AddArea()->MakeRect(rc);
            rc += shift;
        }
        rc += shift;
        rc += shift;
        AddArea()->MakeRect(rc);
        rc += shift;
        AddArea()->MakeRect(rc);
        rc += shift;
        AddArea()->MakeRect(rc);
        rc += shift;
        rc += shift;
        AddArea()->MakeRect(rc);
        rc += shift;
        rc += shift;
        AddArea()->MakeRect(rc);
        break;

    default:
        for (i = 0; i < 8; i++)
        {
            AddArea()->MakeRect(rc);
            rc += shift;
            if (i == 2)
                rc += shift;
        }
        break;
    }
    UpdateStats();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectNameStats::UpdateStats()
{
#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    CDC *pDC = pSurf->GetDC();
    pSurf->Clear();

    pSurf->DrawText(szPersName, CRect(0, 27, 200, 43), 1, CAMP_COLOR_HEADER, DT_CENTER);

    auto* statsMod=ModRuntime::Current();
    if(statsMod && !statsMod->AdvancedUI("useLS_UpdateStats"))statsMod=nullptr;
    CPersonage *pPers = pScreen->GetCurrentPers();
    SRPGUnitStats *pStats = &pPers->stats;
    CUnit *pUnit = pScreen->GetUnit(nCurPers);
    CString szOut, szTmp;
    CPoint shift(0, 15);
    CTextManager *pTxtMng = theWorld.GetTextManager();

    CRect rc;
    if (eCurMode == ISM_SKIN)
    {
        int points;
        if (!pScreen->GetCreationPrimaryPoints(points))
        {
            pSurf->ReleaseDC();
            return;
        }
        COLORREF cr;
        int nStr = (int)pStats->psStrength.fpBaseValue;
        int nDex = (int)pStats->psDexterity.fpBaseValue;
        int nInt = (int)pStats->psIntelligence.fpBaseValue;
        rc = CRect(20, 65, 130, 80);
        OutputSkill(rc, pszStatsNames[0], nStr);
        rc += shift;
        OutputSkill(rc, pszStatsNames[1], nDex);
        rc += shift;
        OutputSkill(rc, pszStatsNames[2], nInt);
        rc += shift;
        OutputSkill(rc, szStrings[POINTSLEFT], points);
        rc += shift;
        rc += shift;
        OutputSkill(rc, szStrings[TALL], nTall);

        rc = CRect(140, 65, 160, 80);
        cr = (pScreen->CanAdjustCreationPrimary(0, -1)) ? CAMP_COLOR_SKILLS : CAMP_COLOR_SKILLS_DIS;
        pSurf->DrawText("-", rc, 0, cr, DT_CENTER);
        rc += shift;
        cr = (pScreen->CanAdjustCreationPrimary(1, -1)) ? CAMP_COLOR_SKILLS : CAMP_COLOR_SKILLS_DIS;
        pSurf->DrawText("-", rc, 0, cr, DT_CENTER);
        rc += shift;
        cr = (pScreen->CanAdjustCreationPrimary(2, -1)) ? CAMP_COLOR_SKILLS : CAMP_COLOR_SKILLS_DIS;
        pSurf->DrawText("-", rc, 0, cr, DT_CENTER);
        rc += shift;
        rc += shift;
        rc += shift;
        cr = (nTall > 0) ? CAMP_COLOR_SKILLS : CAMP_COLOR_SKILLS_DIS;
        pSurf->DrawText("-", rc, 0, cr, DT_CENTER);

        rc = CRect(160, 65, 180, 80);
        cr = (pScreen->CanAdjustCreationPrimary(0, 1)) ? CAMP_COLOR_SKILLS : CAMP_COLOR_SKILLS_DIS;
        pSurf->DrawText("+", rc, 0, cr, DT_CENTER);
        rc += shift;
        cr = (pScreen->CanAdjustCreationPrimary(1, 1)) ? CAMP_COLOR_SKILLS : CAMP_COLOR_SKILLS_DIS;
        pSurf->DrawText("+", rc, 0, cr, DT_CENTER);
        rc += shift;
        cr = (pScreen->CanAdjustCreationPrimary(2, 1)) ? CAMP_COLOR_SKILLS : CAMP_COLOR_SKILLS_DIS;
        pSurf->DrawText("+", rc, 0, cr, DT_CENTER);
        rc += shift;
        rc += shift;
        rc += shift;
        cr = (nTall < 100) ? CAMP_COLOR_SKILLS : CAMP_COLOR_SKILLS_DIS;
        pSurf->DrawText("+", rc, 0, cr, DT_CENTER);
    }
    else if (eCurMode == ISM_WEAPONS)
    {
        rc = CRect(20, 65, 175, 80);
        pTxtMng->GetSystemString("infoitem_25", szTmp);
        pSurf->DrawText(szTmp, rc, 0, CAMP_COLOR_SKILLS, DT_LEFT);
        rc += shift;
        OutputHLAbsorb(rc, "skull", RPGHL_HEAD);
        rc += shift;
        OutputHLAbsorb(rc, "arms", RPGHL_LHAND);
        rc += shift;
        OutputHLAbsorb(rc, "torso", RPGHL_BODY);
        rc += shift;
        OutputHLAbsorb(rc, "legs", RPGHL_LLEG);
        rc += shift;
        rc += shift;

        if (auto* mod=statsMod;mod && pUnit)
        {
            const auto display=mod->CampDescriptions(*pPers,*pUnit);
            const int slot=int(pPers->dwCurrentWeapon);
            const bool weapon=slot>=0&&slot<pPers->weapons.GetSize()&&pPers->weapons[slot];
            if(weapon){
                pSurf->DrawText(szStrings[CURWEAPON],rc,0,CAMP_COLOR_SKILLS,DT_LEFT);rc+=shift;
                szOut.Format("     %s",(LPCSTR)szStrings[ATTACK]);pSurf->DrawText(szOut,rc,0,CAMP_COLOR_SKILLS,DT_LEFT);
                szOut.Format("%d",display.attack);pSurf->DrawText(szOut,rc,0,CAMP_COLOR_SKILLS,DT_RIGHT);rc+=shift;
                szOut.Format("     %s",(LPCSTR)szStrings[DEFENSE]);pSurf->DrawText(szOut,rc,0,CAMP_COLOR_SKILLS,DT_LEFT);
                szOut.Format("%d",display.defense);pSurf->DrawText(szOut,rc,0,CAMP_COLOR_SKILLS,DT_RIGHT);rc+=shift;
                for(const auto& damage:display.damages){
                    szOut.Format("damage_%d",damage.type);pTxtMng->GetSystemString(szOut,szTmp);
                    szOut.Format("     %s %s",(LPCSTR)szStrings[DAMAGE],(LPCSTR)szTmp);pSurf->DrawText(szOut,rc,0,CAMP_COLOR_SKILLS,DT_LEFT);
                    szOut.Format("%.0f-%.0f",damage.minimum,damage.maximum);pSurf->DrawText(szOut,rc,0,CAMP_COLOR_SKILLS,DT_RIGHT);rc+=shift;
                }
                rc+=shift;
            }else for(int i=0;i<5;++i)rc+=shift;
        }
        else if (pScreen->GetCurrentPers()->dwCurrentWeapon != -1)
        {
            pSurf->DrawText(szStrings[CURWEAPON], rc, 0, CAMP_COLOR_SKILLS, DT_LEFT);
            rc += shift;

            SAttackParams attackParams;
            pUnit->GetAttackParams(attackParams);

            // ATTACK & DEFENSE
            szOut.Format("     %s", szStrings[ATTACK]);
            pSurf->DrawText(szOut, rc, 0, CAMP_COLOR_SKILLS, DT_LEFT);
            szOut.Format("%d", attackParams.nAttack);
            pSurf->DrawText(szOut, rc, 0, CAMP_COLOR_SKILLS, DT_RIGHT);
            rc += shift;
            szOut.Format("     %s", szStrings[DEFENSE]);
            pSurf->DrawText(szOut, rc, 0, CAMP_COLOR_SKILLS, DT_LEFT);
            szOut.Format("%d", attackParams.nDefense);
            pSurf->DrawText(szOut, rc, 0, CAMP_COLOR_SKILLS, DT_RIGHT);
            rc += shift;

            // DAMAGE
            if (attackParams.rpgAttackType < RPG_NUM_ATTACK_TYPES)
            {
                szOut.Format("damage_%d", attackParams.rpgAttackType);
                pTxtMng->GetSystemString(szOut, szTmp);
            }
            szOut.Format("     %s %s", szStrings[DAMAGE], szTmp);
            pSurf->DrawText(szOut, rc, 0, CAMP_COLOR_SKILLS, DT_LEFT);
            szOut.Format("%d-%d", static_cast<int>(attackParams.fDamageMin),
                         static_cast<int>(attackParams.fDamageMax));
            pSurf->DrawText(szOut, rc, 0, CAMP_COLOR_SKILLS, DT_RIGHT);
            rc += shift;
            rc += shift;
        }
        else
        {
            for (int i = 0; i < 5; i++)
                rc += shift;
        }

        int nWeight = (int)pScreen->GetCurrentPers()->GetItemsWeight();
        int nEncumbr = (int)pStats->fpMaxEncumbrance;
        if(auto*mod=statsMod){
            float seconds=0;mod->CampInfo(*pPers,pUnit,nEncumbr,seconds);
            if(pTxtMng->GetText("perk Encumbrance"))pTxtMng->GetLine(szTmp);
            pSurf->DrawText(szTmp,rc,0,CAMP_COLOR_SKILLS,DT_LEFT);
            int percent=nEncumbr>0?int(100.0f*nWeight/nEncumbr):0;
            szOut.Format(nWeight>nEncumbr?"%d/%d(%d%%)!":"%d/%d(%d%%)",nWeight,nEncumbr,percent);
            COLORREF color=percent>=100?RGB(255,110,70):percent>=80?RGB(245,130,90):percent>=65?RGB(240,150,110):percent>=50?RGB(235,170,130):CAMP_COLOR_SKILLS;
            pSurf->DrawText(szOut,rc,0,color,DT_RIGHT);rc+=shift;
            CString unitText;if(pTxtMng->GetText("string infounit_18"))pTxtMng->GetLine(szTmp);
            if(pTxtMng->GetText("string infoskill_49"))pTxtMng->GetLine(unitText);
            szOut.Format("%s %.1f%s",(LPCSTR)szTmp,seconds,(LPCSTR)unitText);
            pSurf->DrawText(szOut,rc,0,CAMP_COLOR_SKILLS,DT_LEFT);rc+=shift;
        }else{
            OutputSkill(rc, pszStatsNames[7], nWeight, nEncumbr);rc+=shift;
            if(nWeight>nEncumbr)pSurf->DrawText(szStrings[OVERLOAD],rc,0,RGB(255,0,0),DT_RIGHT);
            rc+=shift;
        }
        pSurf->DrawText(szStrings[MAGICSKILLS], rc, 0, CAMP_COLOR_SKILLS, DT_LEFT);
    }
    else if (statsMod && pUnit)
    {
        // Current addon UpdateStats uses replicated primary stats, difficulty HP,
        // overload-adjusted mana and unwounded camp action rate.
        int capacity=0;float seconds=0,actions=0,mana=0;
        statsMod->CampInfo(*pPers,pUnit,capacity,seconds,&actions,&mana,eCurMode==ISM_STATS);
        auto* user=pUnit->GetUserUnitStats();
        rc=CRect(20,65,180,80);
        OutputSkill(rc,"Strength",int(user->cStrength));rc+=shift;
        OutputSkill(rc,"Dexterity",int(user->cDexterity));rc+=shift;
        OutputSkill(rc,"Intelligence",int(user->cIntelligence));rc+=shift;rc+=shift;
        OutputSkill(rc,"Health",int(pUnit->GetMaxHP()*(1+theSettings.nDifficultyLevel*.25f)));rc+=shift;
        OutputSkill(rc,"Mana",int(mana));rc+=shift;
        OutputSkill(rc,"Actions",actions);rc+=shift;
        OutputSkill(rc,"Experience",int(pStats->GetRealExp()));rc+=shift;
        int weight=ModCampStats::Weight(int(pPers->GetItemsWeight()),eCurMode==ISM_STATS);
        OutputSkill(rc,"Encumbrance",weight,capacity);rc+=shift;
        if(weight>capacity)pSurf->DrawText(szStrings[OVERLOAD],rc,0,RGB(255,0,0),DT_RIGHT);
        CString sight,meters;
        if(pTxtMng->GetText("string infounit_6"))pTxtMng->GetLine(sight);
        if(pTxtMng->GetText("string infounit_13"))pTxtMng->GetLine(meters);
        pSurf->DrawText(sight,rc,0,CAMP_COLOR_SKILLS,DT_LEFT);
        // The addon displays the character's day sight range with the
        // Trickster perk bonus.
        szOut.Format("%d%s", ModCampStats::Sight(*pStats), (LPCSTR)meters);
        pSurf->DrawText(szOut,rc,0,CAMP_COLOR_SKILLS,DT_RIGHT);
    }
    else
    {
        rc = CRect(20, 65, 180, 80);
        OutputSkill(rc, pszStatsNames[0], (int)pStats->psStrength.fpEffectiveValue);
        rc += shift;
        OutputSkill(rc, pszStatsNames[1], (int)pStats->psDexterity.fpEffectiveValue);
        rc += shift;
        OutputSkill(rc, pszStatsNames[2], (int)pStats->psIntelligence.fpEffectiveValue);
        rc += shift;
        rc += shift;
        float val = pStats->RecalcHP();
        // OutputSkill( rc, pszStatsNames[3], (int)pStats->CollectHP( val ), (int)val );
        OutputSkill(rc, pszStatsNames[3], (int)val);
        rc += shift;
        val = pStats->RecalcMP();
        // OutputSkill( rc, pszStatsNames[4], (int)val, (int)val );
        OutputSkill(rc, pszStatsNames[4], (int)val);
        rc += shift;
        OutputSkill(rc, pszStatsNames[5], (int)pStats->action.fpEffectiveValue);
        rc += shift;
        //		OutputSkill( rc, pszStatsNames[6], pStats->fpSpeeds[1] * FP_CL_MP_SIZE
        //* 7.5f
        //* pStats->move.fpEffectiveValue ); 		rc += shift;
        OutputSkill(rc, pszStatsNames[6], (int)pStats->GetRealExp());
        rc += shift;
        int nWeight = 0;
        if (eCurMode == ISM_STATS)
            nWeight = (int)pScreen->GetItemsWeight();
        else
            nWeight = (int)pScreen->GetCurrentPers()->GetItemsWeight();
        int nEncumbr = (int)pStats->fpMaxEncumbrance;
        OutputSkill(rc, pszStatsNames[7], nWeight, nEncumbr);
        rc += shift;
        if (nWeight > nEncumbr)
            pSurf->DrawText(szStrings[OVERLOAD], rc, 0, RGB(255, 0, 0), DT_RIGHT);
        rc += shift;
    }

    pSurf->ReleaseDC();
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectNameStats::OutputSkill(CRect &rc, LPCTSTR pszSkillName, int nVal, int nSecondVal)
{
    CTextManager *pTxtMng = IWorld.GetTextManager();
    CString szName, szVal;

    if (pTxtMng->GetText(CString("perk ") + pszSkillName))
        pTxtMng->GetLine(szName);
    else
        szName = pszSkillName;
    if (nSecondVal >= 0)
        szVal.Format("%d/%d", nVal, nSecondVal);
    else
        szVal.Format("%d", nVal);

    surfaces[0]->DrawText(szName, rc, 0, CAMP_COLOR_SKILLS, DT_LEFT);
    surfaces[0]->DrawText(szVal, rc, 0, CAMP_COLOR_SKILLS, DT_RIGHT);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectNameStats::OutputSkill(CRect &rc, LPCTSTR pszSkillName, float fVal)
{
    CTextManager *pTxtMng = IWorld.GetTextManager();
    CString szName, szVal;

    if (pTxtMng->GetText(CString("perk ") + pszSkillName))
        pTxtMng->GetLine(szName);
    else
        szName = pszSkillName;
    szVal.Format("%.1f", fVal);

    surfaces[0]->DrawText(szName, rc, 0, CAMP_COLOR_SKILLS, DT_LEFT);
    surfaces[0]->DrawText(szVal, rc, 0, CAMP_COLOR_SKILLS, DT_RIGHT);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectNameStats::OutputHLAbsorb(CRect &rc, LPCTSTR pszHLName, ERPGHitLocations eHL)
{
    SRPGUnitStats *pStats = &pScreen->GetCurrentPers()->stats;

    CTextManager *pTxtMng = IWorld.GetTextManager();
    CString szName, szTmp, szAbsorb;

    pTxtMng->GetSystemString(CString("hl_") + pszHLName, szName);

    szName = CString("     ") + szName;
    surfaces[0]->DrawText(szName, rc, 0, CAMP_COLOR_SKILLS, DT_LEFT);

    float fpAbsorbtions[RPG_NUM_ATTACK_TYPES];

    for (int i = 0; i < RPG_NUM_ATTACK_TYPES; i++)
    {
        fpAbsorbtions[i] = 0;
        for (int j = 0; j < RPG_NUM_ARMOR_LAYERS; j++)
            fpAbsorbtions[i] += pStats->hls[eHL].layers[j].fpAbsorbtions[i] *
                ((ModRuntime::Current() && ModRuntime::Current()->AdvancedUI("useLS_UpdateStats"))?1+pStats->GetPerkModifier(i<3?5:22)*.01f:1);
    }

    float fMaxAbsorb = 0;
    for (int i = 0; i < RPG_NUM_ATTACK_TYPES - 1; i++)
        fMaxAbsorb = Max(fpAbsorbtions[i], fMaxAbsorb);
    if((ModRuntime::Current() && ModRuntime::Current()->AdvancedUI("useLS_UpdateStats"))){
        float physical=0,magic=0;
        for(int i=0;i<3;++i)physical=Max(physical,fpAbsorbtions[i]);
        for(int i=3;i<RPG_NUM_ATTACK_TYPES-1;++i)magic=Max(magic,fpAbsorbtions[i]);
        CString physicalIcon,magicIcon;
        if(pTxtMng->GetText("string infoitem_83"))pTxtMng->GetLine(physicalIcon);
        if(pTxtMng->GetText("string infoitem_82"))pTxtMng->GetLine(magicIcon);
        szTmp.Format("%s%.1f %s%.1f",(LPCSTR)physicalIcon,physical,(LPCSTR)magicIcon,magic);
    }else szTmp.Format("%.1f", fMaxAbsorb);
    surfaces[0]->DrawText(szTmp, rc, 0, CAMP_COLOR_SKILLS, DT_RIGHT);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectNameStats::OnLButtonDown(UINT nFlags, CPoint point)
{
    int ht = HitTest(point);
    JournalHit(ht);
    if (eCurMode == ISM_SKIN)
    {
        StopPrimaryRepeat();
        if (!pScreen->CanEditCreationPrimary())
            return;
        CPersonage *pPers = pScreen->GetCurrentPers();
        switch (ht)
        {
        case 6:
            if (pScreen->AdjustCreationPrimary(0, -1))
                nLock = ht;
            break;
        case 7:
            if (pScreen->AdjustCreationPrimary(1, -1))
                nLock = ht;
            break;
        case 8:
            if (pScreen->AdjustCreationPrimary(2, -1))
                nLock = ht;
            break;
        case 9:
            if (nTall >= 1)
            {
                nTall--;
                pPers->complection.tall = nTall / 100.f;
                nLock = ht;
            }
            break;
        case 10:
            if (pScreen->AdjustCreationPrimary(0, 1))
                nLock = ht;
            break;
        case 11:
            if (pScreen->AdjustCreationPrimary(1, 1))
                nLock = ht;
            break;
        case 12:
            if (pScreen->AdjustCreationPrimary(2, 1))
                nLock = ht;
            break;
        case 13:
            if (nTall <= 99)
            {
                nTall++;
                pPers->complection.tall = nTall / 100.f;
                nLock = ht;
            }
            break;
        }
        if (ht >= 6 && ht < 14)
        {
            pI3D->BeginFX("buttons\\camp\\perk.wav");
            pScreen->UpdateAttributes();
        }
        if (nLock > 0)
        {
            fTime = -.5f;
            pI3D->SetCapture(this);
        }
    }
    else
    {
        switch (ht)
        {
        case 0:
            pI3D->BeginFX("buttons\\camp\\hero.wav");
            pScreen->PrevPersonage();
            break;
        case 1:
            pI3D->BeginFX("buttons\\camp\\hero.wav");
            pScreen->NextPersonage();
            break;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectNameStats::StopPrimaryRepeat()
{
    if (pI3D->GetCapture() == this)
        pI3D->ReleaseCapture();
    nLock = -1;
    fTime = 0;
}

void CI3DObjectNameStats::OnLButtonUp(UINT nFlags, CPoint point)
{
    StopPrimaryRepeat();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectNameStats::OnIdle()
{
    if (eCurMode != ISM_SKIN)
    {
        StopPrimaryRepeat();
        return;
    }
    if (nLock < 0)
        return;
    if (!pScreen->CanEditCreationPrimary() || pI3D->GetCapture() != this)
    {
        StopPrimaryRepeat();
        return;
    }
    fTime += GetFrameTime();
    if (fTime < .25f)
        return;
    CPersonage *pPers = pScreen->GetCurrentPers();
    bool bUnlock = true;
    switch (nLock)
    {
    case 6:
        if (pScreen->AdjustCreationPrimary(0, -1))
            bUnlock = false;
        break;
    case 7:
        if (pScreen->AdjustCreationPrimary(1, -1))
            bUnlock = false;
        break;
    case 8:
        if (pScreen->AdjustCreationPrimary(2, -1))
            bUnlock = false;
        break;
    case 9:
        if (nTall >= 1)
        {
            nTall--;
            pPers->complection.tall = nTall / 100.f;
            bUnlock = false;
        }
        break;
    case 10:
        if (pScreen->AdjustCreationPrimary(0, 1))
            bUnlock = false;
        break;
    case 11:
        if (pScreen->AdjustCreationPrimary(1, 1))
            bUnlock = false;
        break;
    case 12:
        if (pScreen->AdjustCreationPrimary(2, 1))
            bUnlock = false;
        break;
    case 13:
        if (nTall <= 99)
        {
            nTall++;
            pPers->complection.tall = nTall / 100.f;
            bUnlock = false;
        }
        break;
    }
    pScreen->UpdateAttributes();
    if (bUnlock)
    {
        StopPrimaryRepeat();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectNameStats::GetMouseSkill(CPoint point, SCampSkillMouseInfo *pSkillInfo)
{
    CPersonage *pPers = pScreen->GetCurrentPers();
    pSkillInfo->nType = -1;
    pSkillInfo->pStats = &pPers->stats;
    pSkillInfo->pUnit = pScreen->GetUnit(nCurPers);
    if(eCurMode==ISM_WEAPONS && ModRuntime::Current() && ModRuntime::Current()->AdvancedUI("useLS_UpdateStats") && pSkillInfo->pUnit){
        if(point.x<20||point.x>=180||point.y<165||point.y>=500)return;
        const int row=(point.y-165)/15,slot=int(pPers->dwCurrentWeapon);
        const bool weapon=slot>=0&&slot<pPers->weapons.GetSize()&&pPers->weapons[slot];
        const auto display=ModRuntime::Current()->CampDescriptions(*pPers,*pSkillInfo->pUnit);
        pSkillInfo->nType=ModCampDescriptions::WeaponHover(row,weapon,int(display.damages.size()));
        if(pSkillInfo->nType==4){
            const ERPGHitLocations locations[]={RPGHL_HEAD,RPGHL_LHAND,RPGHL_BODY,RPGHL_LLEG};
            const char* names[]={"hl_skull","hl_arms","hl_torso","hl_legs"};
            pSkillInfo->eHL=locations[row-1];pSkillInfo->szNonlocalName=names[row-1];
        }else if(pSkillInfo->nType==5){pSkillInfo->pWeapon=pPers->weapons[slot];pSkillInfo->szNonlocalName=row==7?"attack":"defense";}
        else if(pSkillInfo->nType==7)pSkillInfo->szNonlocalName="camp_damage";
        else if(pSkillInfo->nType==0){pSkillInfo->nSkill=7;pSkillInfo->szNonlocalName=pszStatsNames[7];}
        else if(pSkillInfo->nType==6)pSkillInfo->szNonlocalName="magic_skills";
        return;
    }
    int ht = HitTest(point);
    if (((eCurMode == ISM_SPELLS || eCurMode == ISM_STATS) && ht >= 2) ||
        (eCurMode == ISM_SKIN && ht >= 2 && ht < 5))
    {
        pSkillInfo->nType = 0;
        pSkillInfo->nSkill = ht - 2;
        pSkillInfo->szNonlocalName = pszStatsNames[ht - 2];
    }
    if (eCurMode == ISM_SKIN && ht == 14)
    {
        pSkillInfo->nType = 7;
        pSkillInfo->szNonlocalName = "lmp_points_left";
    }
    if (eCurMode == ISM_SKIN && ht == 5)
    {
        pSkillInfo->nType = 7;
        pSkillInfo->szNonlocalName = "lmp_pers_tall";
    }
    if (eCurMode == ISM_WEAPONS && ht >= 2)
    {
        switch (ht - 2)
        {
        case 0:
            pSkillInfo->nType = 4;
            pSkillInfo->eHL = RPGHL_HEAD;
            pSkillInfo->szNonlocalName = "hl_skull";
            break;
        case 1:
            pSkillInfo->nType = 4;
            pSkillInfo->eHL = RPGHL_LHAND;
            pSkillInfo->szNonlocalName = "hl_arms";
            break;
        case 2:
            pSkillInfo->nType = 4;
            pSkillInfo->eHL = RPGHL_BODY;
            pSkillInfo->szNonlocalName = "hl_torso";
            break;
        case 3:
            pSkillInfo->nType = 4;
            pSkillInfo->eHL = RPGHL_LLEG;
            pSkillInfo->szNonlocalName = "hl_legs";
            break;
        case 4:
            if (pPers->dwCurrentWeapon >= 0 && pPers->dwCurrentWeapon < pPers->weapons.GetSize())
            {
                pSkillInfo->nType = 5;
                pSkillInfo->pWeapon = pPers->weapons[pPers->dwCurrentWeapon];
                pSkillInfo->szNonlocalName = "attack";
            }
            break;
        case 5:
            if (pPers->dwCurrentWeapon >= 0 && pPers->dwCurrentWeapon < pPers->weapons.GetSize())
            {
                pSkillInfo->nType = 5;
                pSkillInfo->pWeapon = pPers->weapons[pPers->dwCurrentWeapon];
                pSkillInfo->szNonlocalName = "defense";
            }
            break;
        case 6:
            pSkillInfo->nType = 7;
            pSkillInfo->szNonlocalName = "camp_damage";
            break;
        case 7:
            pSkillInfo->nType = 0;
            pSkillInfo->nSkill = 7;
            pSkillInfo->szNonlocalName = pszStatsNames[7];
            break;
        case 8:
            pSkillInfo->nType = 6;
            pSkillInfo->szNonlocalName = "magic_skills";
            break;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectPerks::Init()
{
    SetRect(200, 100, 600, 500);
    AddSurface(dbgnew CI3DSurface(pI3D, 200, 100, 600, 500)); // perks

    nCurPers = 0;
    nPosReal = 0;
    nPosAv = 0;

    AddArea()->MakeRect(360, 320, 380, 330); // scroll real up
    AddArea()->MakeRect(360, 480, 380, 490); // scroll real down
    AddArea()->MakeRect(560, 125, 580, 135); // scroll avail up
    AddArea()->MakeRect(560, 480, 580, 490); // scroll avail down

    CRect rc(215, 135, 385, 150);
    CPoint shift(0, 15);
    for (int i = 0; i < 2; i++) // skills buttons
    {
        AddArea()->MakeRect(rc);
        rc += shift;
    }
    rc += shift;
    rc += shift;
    AddArea()->MakeRect(0, 0, 0, 0);
    for (int i = 0; i < 3; i++) // skills buttons
    {
        AddArea()->MakeRect(rc);
        rc += shift;
    }
    rc += shift;
    AddArea()->MakeRect(215, 165, 385, 180);
    for (int i = 0; i < CAMP_NUM_AV_PERKS_VISIBLE; i++) // perks buttons
        AddArea()->MakeRect(415, 135 + i * 15, 585, 150 + i * 15);
    for (int i = 0; i < CAMP_NUM_REAL_PERKS_VISIBLE; i++) // perks buttons
        AddArea()->MakeRect(215, 330 + i * 15, 385, 345 + i * 15);

    CI3DFigure *pFig;
    pFig = AddFigure();
    pFig->MakeBar(6, 100, 360, 320, 380, 330, 130, 3, 150, 13); // scroll real up
    pFig = AddFigure();
    pFig->MakeBar(6, 100, 360, 480, 380, 490, 130, 13, 150, 3); // scroll real down
    pFig = AddFigure();
    pFig->MakeBar(6, 100, 560, 125, 580, 135, 130, 3, 150, 13); // scroll avail up
    pFig = AddFigure();
    pFig->MakeBar(6, 100, 560, 480, 580, 490, 130, 13, 150, 3); // scroll avail down

    CTextManager *pTxtMng = IWorld.GetTextManager();
    CString szTmp;
    pTxtMng->GetSystemString("skills", szTmp);
    szStrings.Add(szTmp);
    pTxtMng->GetSystemString("perks", szTmp);
    szStrings.Add(szTmp);
    pTxtMng->GetSystemString("perks_av", szTmp);
    szStrings.Add(szTmp);

    if(ModRuntime::Current()){
        GetArea(8)->MakeRect(0,0,0,0);
        GetArea(9)->MakeRect(0,0,0,0);
    }
    UpdateBackup();

    nLock = -1;
    fTime = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectPerks::UpdateBackup()
{
    backup.clear();
    SPerksBackup entry;
    for (int i = 0; i < IWorldClient.GetParty().people.GetSize(); i++)
    {
        backup.push_back(entry);
        nCurPers = i;
        CopySkillsAndPerks();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
bool CI3DObjectPerks::IsModified()
{
    return ModProgression::Index(nCurPers,int(backup.size())) && backup[nCurPers].bModified;
}
/////////////////////////////////////////////////////////////////////////////////////
int CI3DObjectPerks::GetExpTotal()
{
    if(!ModProgression::Index(nCurPers,int(backup.size())))return 0;
    SPerksBackup *pBackup = &backup[nCurPers];
    return int(pBackup->fExperienceCopy + pBackup->fExpPenaltyCopy - pBackup->fSpentExpCopy);
}
/////////////////////////////////////////////////////////////////////////////////////
int CI3DObjectPerks::GetExpPrice()
{
    if(!ModProgression::Index(nCurPers,int(backup.size())) || !pScreen || !pScreen->GetCurrentPers())return 0;
    SPerksBackup *pBackup = &backup[nCurPers];
    SRPGUnitStats *pStats = &pScreen->GetCurrentPers()->stats;
    return int(pStats->fSpentExp - pBackup->fSpentExpCopy);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectPerks::Confirm(bool bConfirm, bool bForAll)
{
    if(!ModProgression::Index(nCurPers,int(backup.size())))return;
    if (bForAll)
    {
        int nPersTmp = nCurPers;
        for (int i = 0; i < backup.size(); i++)
            if (backup[i].bModified)
            {
                nCurPers = i;
                if (bConfirm)
                    CopySkillsAndPerks();
                else
                    RestoreSkillsAndPerks();
            }
        nCurPers = nPersTmp;
    }
    else if (backup[nCurPers].bModified)
    {
        if (bConfirm)
            CopySkillsAndPerks();
        else
            RestoreSkillsAndPerks();
        UpdatePerks();
    }
    if(auto* mod=ModRuntime::Current();mod && mod->CombatSetting("JMVPerks") && pScreen)pScreen->RefreshProgression();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectPerks::GetMouseSkill(CPoint point, SCampSkillMouseInfo *pSkillInfo)
{
    pSkillInfo->nType = -1;
    if (!IsVisible())
        return;
    int num;
    int ht = HitTest(point);
    if (ht >= 4 && ht < 4 + RPG_NUM_ACTUAL_AUTO_SKILLS)
    {
        num = ht - 4;
        pSkillInfo->nType = 1;
        pSkillInfo->nSkill = num;
        pSkillInfo->szNonlocalName = pszSkillNames[num];
        pSkillInfo->pStats = &pScreen->GetCurrentPers()->stats;
        return;
    }
    if (ht >= 4 + RPG_NUM_ACTUAL_AUTO_SKILLS &&
        ht < 4 + RPG_NUM_ACTUAL_AUTO_SKILLS + RPG_NUM_ACTUAL_MANUAL_SKILLS)
    {
        num = ht - 4 - RPG_NUM_ACTUAL_AUTO_SKILLS;
        pSkillInfo->nType = 2;
        pSkillInfo->nSkill = num;
        pSkillInfo->szNonlocalName = pszSkillNames[ht - 4];
        pSkillInfo->pStats = &pScreen->GetCurrentPers()->stats;
        return;
    }
    if (ht >= 4 + RPG_NUM_ACTUAL_AUTO_SKILLS + RPG_NUM_ACTUAL_MANUAL_SKILLS &&
        ht < 4 + RPG_NUM_ACTUAL_AUTO_SKILLS + RPG_NUM_ACTUAL_MANUAL_SKILLS +
                 CAMP_NUM_AV_PERKS_VISIBLE)
    {
        num = nPosAv + ht - 4 - RPG_NUM_ACTUAL_AUTO_SKILLS - RPG_NUM_ACTUAL_MANUAL_SKILLS;
        if (num < perksAv.size())
        {
            pSkillInfo->nType = 3;
            pSkillInfo->nSkill = 0;
            pSkillInfo->szNonlocalName = perksAv[num].szCode;
            pSkillInfo->szNonlocalName.TrimRight("123");
            pSkillInfo->szNonlocalName += "0";
            pSkillInfo->pStats = &pScreen->GetCurrentPers()->stats;
        }
        return;
    }
    if (ht >=
        4 + RPG_NUM_ACTUAL_AUTO_SKILLS + RPG_NUM_ACTUAL_MANUAL_SKILLS + CAMP_NUM_AV_PERKS_VISIBLE)
    {
        num = nPosReal + ht - 4 - RPG_NUM_ACTUAL_AUTO_SKILLS - RPG_NUM_ACTUAL_MANUAL_SKILLS -
              CAMP_NUM_AV_PERKS_VISIBLE;
        if (num < perksReal.size())
        {
            pSkillInfo->nType = 3;
            pSkillInfo->nSkill = perksReal[num].nValue;
            pSkillInfo->szNonlocalName = perksReal[num].szCode;
            pSkillInfo->szNonlocalName.TrimRight("123");
            pSkillInfo->szNonlocalName += "0";
            pSkillInfo->pStats = &pScreen->GetCurrentPers()->stats;
        }
        return;
    }
    pSkillInfo->nType = -1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectPerks::SetPersonage(int nNewCurPers)
{
    nCurPers = nNewCurPers;
    nPosReal = 0;
    nPosAv = 0;
    UpdatePerks();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectPerks::UpdatePerks()
{
    SRPGUnitStats *pStats = &pScreen->GetCurrentPers()->stats;
    pStats->RecalcInternalHeroStats(0);
    pScreen->UpdateAttributes();

    CTextManager *pTxtMng = IWorld.GetTextManager();
    perksReal.clear();
    perksAv.clear();
    skillsAuto.clear();
    skillsManual.clear();
    const SPDBPerk *pPerk;
    SPerkLineDesc entry, *pEntry;
    for (int i = 0; i < RPG_NUM_ACTUAL_AUTO_SKILLS; i++)
    {
        skillsAuto.push_back(entry);
        pEntry = &skillsAuto.back();
        if (pTxtMng->GetText(CString("perk ") + pszSkillNames[i]))
            pTxtMng->GetLine(pEntry->szName);
        else
            pEntry->szName = pszSkillNames[i];
        pEntry->nValue = pStats->cAutoSkills[i];
        pEntry->nPrice = pStats->GetSkillPrice(pEntry->nValue, pEntry->nValue + 1);
    }
    for (int i = 0; i < RPG_NUM_ACTUAL_MANUAL_SKILLS; i++)
    {
        skillsManual.push_back(entry);
        pEntry = &skillsManual.back();
        if (pTxtMng->GetText(CString("perk ") + pszSkillNames[i + RPG_NUM_ACTUAL_AUTO_SKILLS]))
            pTxtMng->GetLine(pEntry->szName);
        else
            pEntry->szName = pszSkillNames[i + RPG_NUM_ACTUAL_AUTO_SKILLS];
        pEntry->nValue = pStats->cManualSkills[i];
        pEntry->nPrice = pStats->GetSkillPrice(pEntry->nValue, pEntry->nValue + 1);
    }
    CString szBasePerk, szSubPerk;
    pTxtMng->GetSystemString("perk_level", szSubPerk);
    for (int i = 0; i < RPG_NUM_PERKS; i++)
    {
        int nPerk;

        int nLevel = pStats->cPerks[i];
        if (nLevel > 0)
        {
            nPerk = i * 3 + nLevel - 1;
            pPerk = thePerkDatabase.GetPerk(nPerk);
            if (!pPerk)
                continue;
            perksReal.push_back(entry);
            pEntry = &perksReal.back();

            szBasePerk = pPerk->szCode;
            szBasePerk.TrimRight("123");
            szBasePerk += "0";
            if (pTxtMng->GetText(CString("perk ") + szBasePerk))
                pTxtMng->GetLine(szBasePerk);
            else
                szBasePerk = pPerk->szName;

            if (nLevel > 1)
                pEntry->szName.Format("%s (%s%d)", szBasePerk, szSubPerk, nLevel);
            else
                pEntry->szName = szBasePerk;

            if (nLevel < 3)
            {
                const SPDBPerk *pNextPerk = thePerkDatabase.GetPerk(nPerk + 1);
                pEntry->szNextCode = pNextPerk->szCode;
                pEntry->nPrice = pStats->GetPerkPrice(pNextPerk);
            }
            else
                pEntry->nPrice = 0;
            pEntry->szCode = pPerk->szCode;
            pEntry->nValue = nLevel;
        }
    }
    int nPerksTotal = thePerkDatabase.GetNumPerks();
    for (int i = 0; i < nPerksTotal; i += 3)
    {
        pPerk = thePerkDatabase.GetPerk(i);
        if (!pPerk)
            continue;
        if (pStats->CanAddPerk(&thePerkDatabase, pPerk))
        {
            perksAv.push_back(entry);
            pEntry = &perksAv.back();
            szBasePerk = pPerk->szCode;
            szBasePerk.TrimRight("123");
            szBasePerk += "0";
            if (pTxtMng->GetText(CString("perk ") + szBasePerk))
                pTxtMng->GetLine(pEntry->szName);
            else
                pEntry->szName = pPerk->szName;
            pEntry->nPrice = pStats->GetPerkPrice(pPerk);
            pEntry->szCode = pPerk->szCode;
            pEntry->bValid = true;
            pEntry->nValue = 0;
        }
    }
    nCharPoints = int(pStats->fpExperience + pStats->fExpPenalty - pStats->fSpentExp);
    CheckRanges();
    RedrawPerks();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectPerks::FormPerkPrice(int nPrice, CString &szOut)
{
    if (nPrice < 0)
        nPrice = INT_MAX;
    if (nPrice < 1000)
        szOut.Format("%d", nPrice);
    else if (nPrice < 10000)
        szOut.Format("%.1fk", nPrice / 1e3f);
    else if (nPrice < 1000000)
        szOut.Format("%dk", nPrice / 1000);
    else if (nPrice < 10000000)
        szOut.Format("%.1fm", nPrice / 1e6f);
    else if (nPrice < 1000000000)
        szOut.Format("%dm", nPrice / 1000000);
    else
        szOut.Format("%.1fg", nPrice / 1e9f);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectPerks::RedrawPerks()
{
#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    pSurf->GetDC();
    pSurf->Clear();

    CString szOut;
    CPoint shift(0, 15);
    int i, n;
    COLORREF cr, cr2;
    auto perkColor=[&](const CString& code,COLORREF fallback){
        if(!ModRuntime::Current())return fallback;
        int index=thePerkDatabase.GetPerkIndex(code);if(index<0)return fallback;
        int group=index/3;
        if(group<=6||group==29)return RGB(230,160,160);
        if((group>=10&&group<=16)||group==30||group==23)return RGB(160,230,160);
        if((group>=17&&group<=23)||(group>=7&&group<=9)||group==31)return RGB(160,160,200);
        return CAMP_COLOR_SKILLS;
    };

    while (figures.GetSize() > 4)
        RemoveFigure(figures.GetSize() - 1);

    pSurf->DrawText(szStrings[SKILLS], CRect(0, 10, 200, 25), 0, CAMP_COLOR_HEADER, DT_CENTER);
    pSurf->DrawText(szStrings[PERKS], CRect(0, 205, 200, 220), 0, CAMP_COLOR_HEADER, DT_CENTER);
    pSurf->DrawText(szStrings[PERKSAV], CRect(200, 10, 400, 25), 0, CAMP_COLOR_HEADER, DT_CENTER);

    CRect rc(15, 35, 180, 50);
    for (i = 0; i < ((ModRuntime::Current()&&ModRuntime::Current()->CombatSetting("JMVPerks"))?4:RPG_NUM_ACTUAL_AUTO_SKILLS); i++)
    {
        if (i == 2)
        {
            rc += shift;
            rc += shift;
            continue;
        }
        pSurf->DrawText(skillsAuto[i].szName, rc, 0, CAMP_COLOR_SKILLS, DT_LEFT);
        szOut.Format("%d", skillsAuto[i].nValue);
        pSurf->DrawText(szOut, rc - CPoint(40, 0), 0, CAMP_COLOR_SKILLS, DT_RIGHT);
        if (skillsAuto[i].nValue < (ModRuntime::Current()?ModRuntime::Current()->AutomaticSkillLimit(i,ModRuntime::Current()->CombatSetting("JMVPerks")!=0):100))
        {
            FormPerkPrice(skillsAuto[i].nPrice, szOut);
            if (skillsAuto[i].nPrice <= nCharPoints)
                cr = CAMP_COLOR_SKILLS;
            else
                cr = CAMP_COLOR_SKILLS_DIS;
            pSurf->DrawText(szOut, rc, 0, cr, DT_RIGHT);
            AddFigure()->MakeBar(6, 100, 350, rc.top + 100, 390, rc.bottom + 100, 124, 138, 164,
                                 153);
        }
        rc += shift;
        // if ( i == 2 )
        // rc += shift;
    }
    // rc += shift;
    rc = CRect(15, 65, 180, 80);
    for (i = 0; i < RPG_NUM_ACTUAL_MANUAL_SKILLS; i++)
    {
        pSurf->DrawText(skillsManual[i].szName, rc, 0, CAMP_COLOR_SKILLS, DT_LEFT);
        szOut.Format("%d", skillsManual[i].nValue);
        pSurf->DrawText(szOut, rc - CPoint(40, 0), 0, CAMP_COLOR_SKILLS, DT_RIGHT);
        if (skillsManual[i].nValue < ManualSkillTrainingLimit(i, (ModRuntime::Current()&&ModRuntime::Current()->CombatSetting("JMVPerks"))?120:100))
        {
            FormPerkPrice(skillsManual[i].nPrice, szOut);
            if (skillsManual[i].nPrice <= nCharPoints)
                cr = CAMP_COLOR_SKILLS;
            else
                cr = CAMP_COLOR_SKILLS_DIS;
            pSurf->DrawText(szOut, rc, 0, cr, DT_RIGHT);
            AddFigure()->MakeBar(6, 100, 350, rc.top + 100, 390, rc.bottom + 100, 124, 138, 164,
                                 153);
        }
        rc += shift;
    }

    rc = CRect(15, 230, 180, 245);
    for (i = 0, n = nPosReal; i < CAMP_NUM_REAL_PERKS_VISIBLE && n < perksReal.size(); i++, n++)
    {
        pSurf->DrawText(perksReal[n].szName, rc, 0, perkColor(perksReal[n].szCode,CAMP_COLOR_SKILLS), DT_LEFT);
        if (!perksReal[n].szNextCode.IsEmpty())
        {
            if (perksReal[n].nPrice <= nCharPoints)
                cr = CAMP_COLOR_SKILLS;
            else
                cr = CAMP_COLOR_SKILLS_DIS;

            FormPerkPrice(perksReal[n].nPrice, szOut);
            pSurf->DrawText(szOut, rc, 0, cr, DT_RIGHT);
            AddFigure()->MakeBar(6, 100, 350, rc.top + 100, 390, rc.bottom + 100, 124, 138, 164,
                                 153);
        }
        rc += shift;
    }

    rc = CRect(215, 35, 380, 50);
    for (i = 0, n = nPosAv; i < CAMP_NUM_AV_PERKS_VISIBLE && n < perksAv.size(); i++, n++)
    {
        if (perksAv[n].bValid)
        {
            cr = CAMP_COLOR_SKILLS;
            if (perksAv[n].nPrice <= nCharPoints)
                cr2 = CAMP_COLOR_SKILLS;
            else
                cr2 = CAMP_COLOR_SKILLS_DIS;
        }
        else
        {
            cr = CAMP_COLOR_SKILLS_DIS;
            cr2 = CAMP_COLOR_SKILLS_DIS;
        }
        pSurf->DrawText(perksAv[n].szName, rc, 0, perkColor(perksAv[n].szCode,cr), DT_LEFT);
        FormPerkPrice(perksAv[n].nPrice, szOut);
        pSurf->DrawText(szOut, rc, 0, cr2, DT_RIGHT);
        AddFigure()->MakeBar(6, 100, 550, rc.top + 100, 590, rc.bottom + 100, 124, 138, 164, 153);
        rc += shift;
    }
    pSurf->ReleaseDC();
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectPerks::OnLButtonDown(UINT nFlags, CPoint point)
{
    int ht = HitTest(point);
    JournalHit(ht);

    switch (ht)
    {
    case 0: // scroll real up
        --nPosReal;
        CheckRanges();
        return;
    case 1: // scroll real down
        ++nPosReal;
        CheckRanges();
        return;
    case 2: // scroll avail up
        --nPosAv;
        CheckRanges();
        return;
    case 3: // scroll avail down
        ++nPosAv;
        CheckRanges();
        return;
    }

    if (ht >= 4)
        pI3D->BeginFX("buttons\\camp\\perk.wav");

    auto* mod=ModRuntime::Current();const bool native=mod && mod->CombatSetting("JMVPerks");
    if(!pScreen || !pScreen->GetCurrentPers() || !ModProgression::Index(nCurPers,int(backup.size())))return;
    SRPGUnitStats *pStats = &pScreen->GetCurrentPers()->stats;
    SPerksBackup *pBackup = &backup[nCurPers];

    int num;
    if (ht >= 4 && ht < 4 + RPG_NUM_ACTUAL_AUTO_SKILLS)
    {
        num = ht - 4;
        if (!ModProgression::SkillPending(native,true,num,true,skillsAuto[num].nValue,skillsAuto[num].nPrice,nCharPoints,pStats->fSpentExp,mod?mod->AutomaticSkillLimit(num):100))
            return;
        pStats->cAutoSkills[num]++;
        pStats->fSpentExp += skillsAuto[num].nPrice;
        pBackup->bModified = true;
        UpdatePerks();
        nLock = ht;
        fTime = -.5f;
        pI3D->SetCapture(this);
        if(native)pScreen->RefreshProgression();
        return;
    }
    if (ht >= 4 + RPG_NUM_ACTUAL_AUTO_SKILLS &&
        ht < 4 + RPG_NUM_ACTUAL_AUTO_SKILLS + RPG_NUM_ACTUAL_MANUAL_SKILLS)
    {
        num = ht - 4 - RPG_NUM_ACTUAL_AUTO_SKILLS;
        if (!ModProgression::SkillPending(native,false,num,true,skillsManual[num].nValue,skillsManual[num].nPrice,nCharPoints,pStats->fSpentExp,ManualSkillTrainingLimit(num,100)))
            return;
        pStats->cManualSkills[num]++;
        pStats->fSpentExp += skillsManual[num].nPrice;
        pBackup->bModified = true;
        UpdatePerks();
        nLock = ht;
        fTime = -.5f;
        pI3D->SetCapture(this);
        if(native)pScreen->RefreshProgression();
        return;
    }
    if (ht >= 4 + RPG_NUM_ACTUAL_AUTO_SKILLS + RPG_NUM_ACTUAL_MANUAL_SKILLS &&
        ht < 4 + RPG_NUM_ACTUAL_AUTO_SKILLS + RPG_NUM_ACTUAL_MANUAL_SKILLS +
                 CAMP_NUM_AV_PERKS_VISIBLE)
    {
        num = nPosAv + ht - 4 - RPG_NUM_ACTUAL_AUTO_SKILLS - RPG_NUM_ACTUAL_MANUAL_SKILLS;
        if (!ModProgression::Index(num,int(perksAv.size())) || !ModProgression::PerkPending(native,perksAv[num].bValid,perksAv[num].nPrice,nCharPoints,pStats->fSpentExp))
            return;
        const auto* perk=thePerkDatabase.GetPerk(perksAv[num].szCode);if(!perk)return;
        pStats->AddPerk(&thePerkDatabase, perk);
        pStats->fSpentExp += perksAv[num].nPrice;
        pBackup->bModified = true;
        UpdatePerks();
        if(native)pScreen->RefreshProgression();
        return;
    }
    if (ht >=
        4 + RPG_NUM_ACTUAL_AUTO_SKILLS + RPG_NUM_ACTUAL_MANUAL_SKILLS + CAMP_NUM_AV_PERKS_VISIBLE)
    {
        num = nPosReal + ht - 4 - RPG_NUM_ACTUAL_AUTO_SKILLS - RPG_NUM_ACTUAL_MANUAL_SKILLS -
              CAMP_NUM_AV_PERKS_VISIBLE;
        if (!ModProgression::Index(num,int(perksReal.size())) ||
            !ModProgression::PerkPending(native,!perksReal[num].szNextCode.IsEmpty(),perksReal[num].nPrice,nCharPoints,pStats->fSpentExp))
            return;
        if(native && !ModProgression::Additional(*pStats,
            [&](int field){return mod->ProgressionRequirementCode(perksReal[num].szNextCode,field);},
            [&](const char* code){return thePerkDatabase.GetPerkIndex(code);})){pI3D->BeginFX("buttons\\MessBox\\Cancel.wav");return;}
        const auto* perk=thePerkDatabase.GetPerk(perksReal[num].szNextCode);if(!perk)return;
        pStats->AddPerk(&thePerkDatabase, perk);
        pStats->fSpentExp += perksReal[num].nPrice;
        pBackup->bModified = true;
        UpdatePerks();
        if(native)pScreen->RefreshProgression();
        return;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectPerks::OnLButtonUp(UINT nFlags, CPoint point)
{
    pI3D->ReleaseCapture();
    nLock = -1;
    fTime = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectPerks::OnIdle()
{
    if (nLock < 0)
        return;
    fTime += GetFrameTime();
    if (fTime < .25f)
        return;
    int ht = nLock;
    int num;
    if(!pScreen || !pScreen->GetCurrentPers() || !ModProgression::Index(nCurPers,int(backup.size())))return;
    SRPGUnitStats *pStats = &pScreen->GetCurrentPers()->stats;
    if (ht >= 4 && ht < 4 + RPG_NUM_ACTUAL_AUTO_SKILLS)
    {
        num = ht - 4;
        if (skillsAuto[num].nValue >= (ModRuntime::Current()?ModRuntime::Current()->AutomaticSkillLimit(num):100) || skillsAuto[num].nPrice > nCharPoints)
        {
            pI3D->ReleaseCapture();
            nLock = -1;
            return;
        }
        pStats->cAutoSkills[num]++;
        pStats->fSpentExp += skillsAuto[num].nPrice;
        UpdatePerks();
        return;
    }
    if (ht >= 4 + RPG_NUM_ACTUAL_AUTO_SKILLS &&
        ht < 4 + RPG_NUM_ACTUAL_AUTO_SKILLS + RPG_NUM_ACTUAL_MANUAL_SKILLS)
    {
        num = ht - 4 - RPG_NUM_ACTUAL_AUTO_SKILLS;
        if (skillsManual[num].nValue >= ManualSkillTrainingLimit(num,100) || skillsManual[num].nPrice > nCharPoints)
        {
            pI3D->ReleaseCapture();
            nLock = -1;
            return;
        }
        pStats->cManualSkills[num]++;
        pStats->fSpentExp += skillsManual[num].nPrice;
        UpdatePerks();
        return;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectPerks::CheckRanges()
{
    if (nPosReal > static_cast<int>(perksReal.size()) - CAMP_NUM_REAL_PERKS_VISIBLE)
        nPosReal = static_cast<int>(perksReal.size()) - CAMP_NUM_REAL_PERKS_VISIBLE;
    if (nPosReal < 0)
        nPosReal = 0;
    if (nPosAv > static_cast<int>(perksAv.size()) - CAMP_NUM_AV_PERKS_VISIBLE)
        nPosAv = static_cast<int>(perksAv.size()) - CAMP_NUM_AV_PERKS_VISIBLE;
    if (nPosAv < 0)
        nPosAv = 0;
    if (nPosReal > 0)
        figures[0]->Show(TRUE);
    else
        figures[0]->Show(FALSE);
    if (nPosReal < static_cast<int>(perksReal.size()) - CAMP_NUM_REAL_PERKS_VISIBLE)
        figures[1]->Show(TRUE);
    else
        figures[1]->Show(FALSE);
    if (nPosAv > 0)
        figures[2]->Show(TRUE);
    else
        figures[2]->Show(FALSE);
    if (nPosAv < static_cast<int>(perksAv.size()) - CAMP_NUM_AV_PERKS_VISIBLE)
        figures[3]->Show(TRUE);
    else
        figures[3]->Show(FALSE);
    RedrawPerks();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectPerks::CopySkillsAndPerks()
{
    if (!ModProgression::Index(nCurPers,int(backup.size())) ||
        !ModProgression::Index(nCurPers,IWorldClient.GetParty().people.GetSize()))
        return;
    SPerksBackup *pBackup = &backup[nCurPers];
    SRPGUnitStats *pStats = &IWorldClient.GetParty().people[nCurPers].stats;
    ModProgression::Copy(*pStats,*pBackup);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectPerks::RestoreSkillsAndPerks()
{
    if (!ModProgression::Index(nCurPers,int(backup.size())) ||
        !ModProgression::Index(nCurPers,IWorldClient.GetParty().people.GetSize()))
        return;
    SPerksBackup *pBackup = &backup[nCurPers];
    SRPGUnitStats *pStats = &IWorldClient.GetParty().people[nCurPers].stats;

    float fOldMaxHP = pStats->RecalcHP();
    float fOldCurHP = pStats->CollectHP(fOldMaxHP);

    ModProgression::Restore(*pStats,*pBackup);

    float fNewMaxHP = pStats->RecalcHP();
    if ((fOldMaxHP > 1) && (fNewMaxHP > 1))
    {
        float fPercentage = fOldCurHP / fOldMaxHP;
        float fNewCurHP = pStats->CollectHP(fNewMaxHP);
        pStats->Heal(fNewMaxHP, fNewMaxHP * fPercentage - fNewCurHP);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DTradeTable::Init(CItemsArray *pItBuy, CItemsArray *pItSell, bool bSpell, bool bEx)
{
    SetRect(200, 100, 600, 500);

    pItemsBuy = pItBuy;
    pItemsSell = pItSell;

    if (bSpell)
    {
        eBuyType = ISK_SPELL_BUY;
        eSellType = ISK_SPELL_SELL;
    }
    else
    {
        eBuyType = ISK_ITEM_BUY;
        eSellType = ISK_ITEM_SELL;
    }
    bExchange = bEx;

    AddTexture("trade1");
    AddTexture("trade2");
    AddTexture("trade3");
    AddTexture("trade4");

    int i, j;
    CI3DFigure *pFig;
    float fDepth = 32;
    int nMapX = 28, nMapY = 28;
    for (j = 0; j < 4; j++)
    {
        for (i = 0; i < 4; i++)
        {
            CTexture *pTex = 0;
            if (j > 1)
                pTex = i > 1 ? textures[3] : textures[2];
            else
                pTex = i > 1 ? textures[1] : textures[0];

            pFig = AddFigure();
            pFig->MakeBar(fDepth * 2, 100, 200 + i * 100, 100 + j * 100, 300 + i * 100,
                          200 + j * 100, nMapX, nMapY, nMapX + 100, nMapY + 100);
            pFig->SetTexture(pTex);

            pFig = AddFigure();
            pFig->MakeBarFrame2(fDepth, 100, 200 + i * 100, 100 + j * 100, 300 + i * 100,
                                200 + j * 100, 15, nMapX, nMapY, nMapX + 100, nMapY + 100);
            pFig->SetTexture(pTex);

            AddArea()->MakeRect(200 + i * 100, 100 + j * 100, 300 + i * 100, 200 + j * 100);
            nMapX = 156 - nMapX;
        }
        nMapY = 156 - nMapY;
        fDepth /= 2;
    }
    AddSurface(dbgnew CI3DSurface(pI3D, 200, 100, 600, 500));
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DTradeTable::Draw(CRenderer *pRend)
{
    pI3D->GetRenderer()->FlushBuffers();

    D3DRECT rect;
    CRect rcTmp(200, 100, 600, 500);
    pI3D->VirtualToScreen(rcTmp);

    rect.x1 = 0;
    rect.y1 = 0;
    rect.x2 = pI3D->GetWidth();
    rect.y2 = pI3D->GetHeight();
    pI3D->GetRenderer()->Clear(1, &rect, D3DCLEAR_ZBUFFER, 0, 0, 0);
    rect.x1 = rcTmp.left;
    rect.y1 = rcTmp.top;
    rect.x2 = rcTmp.right;
    rect.y2 = rcTmp.bottom;
    pI3D->GetRenderer()->Clear(1, &rect, D3DCLEAR_ZBUFFER, 0, 1, 0);

    pI3D->GetRenderer()->FlushBuffers();
    CI3DObject::Draw(pRend);

#ifndef NOGETDC
    int x, y;
    CString szTmp;
    SDrawItem item;

    CI3DSurface *pSurf = surfaces[0];
    pSurf->GetDC();
    pSurf->Clear();
    for (int i = 0; i < pItemsBuy->GetSize(); i++)
    {
        x = (i % 4) * 100;
        y = (i / 4) * 100;
        item.pItem = (*pItemsBuy)[i];
        item.fX = x + 250.f;
        item.fY = y + 150.f;
        item.fZ = (i > 3) ? 24.f : 48.f;
        item.fPixSize = 100;
        item.eFrom = ePartType;
        pScreen->DrawItem(pRend, item);
        if (item.pItem->GetCount() > 1)
        {
            szTmp.Format("%d", item.pItem->GetCount());
            pSurf->DrawText(szTmp, CRect(15 + x, 15 + y, 85 + x, 35 + y), 1, CONSTR_COLOR_NUMBERS,
                            DT_RIGHT);
        }
        if (!bExchange)
        {
            szTmp.Format("%d", pScreen->GetItemPrice(item.pItem, eBuyType));
            pSurf->DrawText(szTmp, CRect(15 + x, 65 + y, 85 + x, 85 + y), 1, CONSTR_COLOR_PRICE,
                            DT_CENTER);
        }
    }
    for (int i = 0; i < pItemsSell->GetSize(); i++)
    {
        x = (i % 4) * 100;
        y = (i / 4) * 100;
        item.pItem = (*pItemsSell)[i];
        item.fX = x + 250.f;
        item.fY = y + 350.f;
        item.fZ = (i > 3) ? 6.f : 12.f;
        item.fPixSize = 100;
        item.eFrom = ePartType;
        pScreen->DrawItem(pRend, item);
        if (item.pItem->GetCount() > 1)
        {
            szTmp.Format("%d", item.pItem->GetCount());
            pSurf->DrawText(szTmp, CRect(15 + x, 215 + y, 85 + x, 235 + y), 1, CONSTR_COLOR_NUMBERS,
                            DT_RIGHT);
        }
        if (!bExchange)
        {
            szTmp.Format("%d", pScreen->GetItemPrice(item.pItem, eSellType));
            pSurf->DrawText(szTmp, CRect(15 + x, 265 + y, 85 + x, 285 + y), 1, CONSTR_COLOR_PRICE,
                            DT_CENTER);
        }
    }
    pSurf->ReleaseDC();
#endif // NOGETDC

    pI3D->GetRenderer()->FlushBuffers();
    rect.x1 = 0;
    rect.y1 = 0;
    rect.x2 = pI3D->GetWidth();
    rect.y2 = pI3D->GetHeight();
    pI3D->GetRenderer()->Clear(1, &rect, D3DCLEAR_ZBUFFER, 0, 1, 0);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
CItem *CI3DTradeTable::GetMouseItem(CPoint point, int &nSlotType)
{
    nSlotType = 0;
    if (!IsVisible())
        return 0;
    int ht = HitTest(point);
    if (ht >= 0 && ht < 8)
    {
        if (bExchange)
            nSlotType = IST_EXCH_BUY;
        else
            nSlotType = (eBuyType == ISK_SPELL_BUY) ? IST_SPELL_BUY : IST_ITEM_BUY;
        if (ht < pItemsBuy->GetSize())
            return (*pItemsBuy)[ht];
    }
    if (ht >= 8 && ht < 16)
    {
        if (bExchange)
            nSlotType = IST_EXCH_SELL;
        else
            nSlotType = (eBuyType == ISK_SPELL_BUY) ? IST_SPELL_SELL : IST_ITEM_SELL;
        if (ht - 8 < pItemsSell->GetSize())
            return (*pItemsSell)[ht - 8];
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DRepairTable::Init(CItemsArray *pItRepair)
{
    SetRect(200, 100, 600, 500);
    ePartType = ISP_ITEMREPAIR;

    pItemsRepair = pItRepair;

    AddTexture("repair1");
    AddTexture("repair2");
    AddTexture("repair3");
    AddTexture("repair4");

    int i, j;
    CI3DFigure *pFig;
    float fDepth = 32;
    int nMapX = 28, nMapY = 28;
    for (j = 0; j < 4; j++)
    {
        for (i = 0; i < 4; i++)
        {
            CTexture *pTex = 0;
            if (j > 1)
                pTex = i > 1 ? textures[3] : textures[2];
            else
                pTex = i > 1 ? textures[1] : textures[0];

            pFig = AddFigure();
            pFig->MakeBar(fDepth * 2, 100, 200 + i * 100, 100 + j * 100, 300 + i * 100,
                          200 + j * 100, nMapX, nMapY, nMapX + 100, nMapY + 100);
            pFig->SetTexture(pTex);

            pFig = AddFigure();
            pFig->MakeBarFrame2(fDepth, 100, 200 + i * 100, 100 + j * 100, 300 + i * 100,
                                200 + j * 100, 15, nMapX, nMapY, nMapX + 100, nMapY + 100);
            pFig->SetTexture(pTex);

            AddArea()->MakeRect(200 + i * 100, 100 + j * 100, 300 + i * 100, 200 + j * 100);
            nMapX = 156 - nMapX;
        }
        nMapY = 156 - nMapY;
        fDepth /= 2;
    }
    AddSurface(dbgnew CI3DSurface(pI3D, 200, 100, 600, 500));
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DRepairTable::Draw(CRenderer *pRend)
{
    pI3D->GetRenderer()->FlushBuffers();

    D3DRECT rect;
    CRect rcTmp(200, 100, 600, 500);
    pI3D->VirtualToScreen(rcTmp);

    rect.x1 = 0;
    rect.y1 = 0;
    rect.x2 = pI3D->GetWidth();
    rect.y2 = pI3D->GetHeight();
    pI3D->GetRenderer()->Clear(1, &rect, D3DCLEAR_ZBUFFER, 0, 0, 0);
    rect.x1 = rcTmp.left;
    rect.y1 = rcTmp.top;
    rect.x2 = rcTmp.right;
    rect.y2 = rcTmp.bottom;
    pI3D->GetRenderer()->Clear(1, &rect, D3DCLEAR_ZBUFFER, 0, 1, 0);

#ifndef NOGETDC
    CString szTmp;
    int x, y;
    SDrawItem item;
    CI3DSurface *pSurf = surfaces[0];
    pSurf->GetDC();
    pSurf->Clear();
    for (int i = 0; i < pItemsRepair->GetSize(); i++)
    {
        x = (i % 4) * 100;
        y = (i / 4) * 100;
        item.pItem = (*pItemsRepair)[i];
        item.fX = x + 250.f;
        item.fY = y + 150.f;
        item.fZ = (1 << (3 - (i >> 2))) * 6.f;
        item.fPixSize = 100;
        item.eFrom = ePartType;
        pScreen->DrawItem(pRend, item);
        if (item.pItem->GetCount() > 1)
        {
            szTmp.Format("%d", item.pItem->GetCount());
            pSurf->DrawText(szTmp, CRect(15 + x, 15 + y, 85 + x, 35 + y), 1, CONSTR_COLOR_NUMBERS,
                            DT_RIGHT);
        }
        szTmp.Format("%d", pScreen->GetItemPrice(item.pItem, ISK_ITEM_REPAIR));
        pSurf->DrawText(szTmp, CRect(15 + x, 65 + y, 85 + x, 85 + y), 1, CONSTR_COLOR_PRICE,
                        DT_CENTER);
    }
    pSurf->ReleaseDC();
#endif // NOGETDC

    CI3DObject::Draw(pRend);

    pI3D->GetRenderer()->FlushBuffers();
    rect.x1 = 0;
    rect.y1 = 0;
    rect.x2 = pI3D->GetWidth();
    rect.y2 = pI3D->GetHeight();
    pI3D->GetRenderer()->Clear(1, &rect, D3DCLEAR_ZBUFFER, 0, 1, 0);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
CItem *CI3DRepairTable::GetMouseItem(CPoint point, int &nSlotType)
{
    nSlotType = 0;
    if (!IsVisible())
        return 0;
    int ht = HitTest(point);
    if (ht >= 0 && ht < 16)
    {
        nSlotType = IST_ITEM_REPAIR;
        if (ht < pItemsRepair->GetSize())
            return (*pItemsRepair)[ht];
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DConstrSpell::Init(CParty *pItems)
{
    SetRect(200, 100, 600, 500);
    ePartType = ISP_SPELLCONSTR;

    pTableParty = pItems;

    AddTexture("constrspell1");
    AddTexture("constrspell2");
    AddTexture("constrspell3");
    AddTexture("constrspell4");

    // result
    AddArea()->MakeRect(200, 250, 300, 350);
    // prototype
    AddArea()->MakeRect(300, 250, 400, 350);
    // modifiers
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 4; j++)
            AddArea()->MakeRect(400 + i * 100, 100 + j * 100, 500 + i * 100, 200 + j * 100);
    // max complexity
    AddArea()->MakeRect(200, 450, 400, 470);
    // prototype
    AddArea()->MakeRect(200, 470, 400, 490);

    CI3DFigure *pFig;
    pFig = AddFigure();
    pFig->MakeBar(7, 100, 200, 100, 400, 300, 28, 28, 228, 228);
    pFig->SetTexture(textures[0]);
    pFig = AddFigure();
    pFig->MakeBar(7, 100, 400, 100, 600, 300, 28, 28, 228, 228);
    pFig->SetTexture(textures[1]);
    pFig = AddFigure();
    pFig->MakeBar(7, 100, 200, 300, 400, 500, 28, 28, 228, 228);
    pFig->SetTexture(textures[2]);
    pFig = AddFigure();
    pFig->MakeBar(7, 100, 400, 300, 600, 500, 28, 28, 228, 228);
    pFig->SetTexture(textures[3]);

    pFig = AddFigure();
    pFig->MakeBarFrame2(3, 100, -40, -40, 40, 40, 5);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(255, 255, 255, 150));
    pFig->SetTexture(0);
    pFig->Show(FALSE);

    AddSurface(dbgnew CI3DSurface(pI3D, 200, 450, 400, 490));

    fTime = 0;

    const int nStr[] = { 35, 36, -1 };
    CTextManager *pTxtMng = theWorld.GetTextManager();
    CString szTmp;
    const int *pnStr = nStr;
    while (*pnStr != -1)
    {
        szTmp.Format("infoitem_%d", *pnStr);
        pTxtMng->GetSystemString(szTmp, szTmp);
        szStrings.Add(szTmp);
        pnStr++;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DConstrSpell::Draw(CRenderer *pRend)
{
    DWORD dwComplexity = 0;
    float fMana = 0, fPrototypeMana = 0;
    ERPGMagicSubtypes subtype;
    bool bDrawNoFrames = false;
    bool bIsPrototype = false;
    int x = 0, y = 0;
    int nMods = 0;
    SDrawItem item;
    for (int i = 0; i < pTableParty->allWagonItems.GetSize(); i++)
    {
        item.pItem = pTableParty->allWagonItems[i];
        // Native37753-37829 output roles use the existing typed DrawItem owner.
        if(ModAlchemy::PipelineEnabled())
        {
            if(pScreen->IsItemInstruction(item.pItem))
            {
                item.fX=350;item.fY=300;item.fZ=6;item.fPixSize=100;item.eFrom=ePartType;
                pScreen->DrawItem(pRend,item);
            }
            else if(pScreen->IsItemMaterial(item.pItem))
            {
                item.fX=x*100+450.f;item.fY=y*100+150.f;item.fZ=6;item.fPixSize=100;item.eFrom=ePartType;
                pScreen->DrawItem(pRend,item);
                if(++y>3){y=0;x++;}
            }
            else if(pScreen->IsItemSpell(item.pItem))
            {
                item.fX=300;item.fY=176;item.fZ=6;item.fPixSize=200;item.eFrom=ePartType;
                pScreen->DrawItem(pRend,item);
            }
        }
        if (pScreen->IsItemSpell(item.pItem) || ModAlchemy::Scroll(item.pItem))
        {
            item.fX = 250;
            item.fY = 300;
            item.fZ = 6;
            item.fPixSize = 100;
            item.eFrom = ePartType;
            pScreen->DrawItem(pRend, item);
            bDrawNoFrames = true;
        }
        else if (pScreen->IsItemPrototype(item.pItem))
        {
            CItemLoot *pLoot = static_cast<CItemLoot *>(item.pItem);
            CSpellManager *pSM = theWorld.GetSpellManager();
            const SSDBSpellPrototype *pPrototype = pSM->GetPrototype(
                pLoot->GetInstructionItemIndex());
            dwComplexity += pPrototype->dwComplexity;
            fMana += pPrototype->fpMana;
            fPrototypeMana = pPrototype->fpMana;
            subtype = pPrototype->mstSubtype;
            item.fX = 350;
            item.fY = 300;
            item.fZ = 6;
            item.fPixSize = 100;
            item.eFrom = ePartType;
            pScreen->DrawItem(pRend, item);
            bIsPrototype = true;
        }
        else if (pScreen->IsItemModifier(item.pItem))
        {
            CItemLoot *pLoot = static_cast<CItemLoot *>(item.pItem);
            CSpellManager *pSM = theWorld.GetSpellManager();
            const SSDBModifier *pModifier = pSM->GetModifier(pLoot->GetInstructionItemIndex());
            dwComplexity += pModifier->dwComplexity;
            fMana += pModifier->fpMana;
            nMods++;
            item.fX = x * 100 + 450.f;
            item.fY = y * 100 + 150.f;
            item.fZ = 6;
            item.fPixSize = 100;
            item.eFrom = ePartType;
            pScreen->DrawItem(pRend, item);
            if (++y > 3)
            {
                y = 0;
                x++;
            }
        }
    }

    int nMaxComplexity = 0, nMaxMana = 0;
    if (bIsPrototype)
        pScreen->GetMaxManaAndComplexity(subtype, nMaxComplexity, nMaxMana);
#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    pSurf->GetDC();
    pSurf->Clear();

    if (bIsPrototype)
    {
        CString szOut;
        szOut.Format("%s %d (%d)", szStrings[MAXCOMPL], nMaxComplexity, dwComplexity);
        pSurf->DrawText(szOut, CRect(10, 0, 200, 20), 1, INV_COLOR_NUMBERS, DT_LEFT);
        szOut.Format("%s %d (%d)", szStrings[MAXMANA], nMaxMana, (int)Max(fMana, fPrototypeMana));
        pSurf->DrawText(szOut, CRect(10, 20, 200, 40), 1, INV_COLOR_NUMBERS, DT_LEFT);
    }
    else
    {
        pSurf->DrawText(szStrings[MAXCOMPL], CRect(10, 0, 200, 20), 1, INV_COLOR_NUMBERS, DT_LEFT);
        pSurf->DrawText(szStrings[MAXMANA], CRect(10, 20, 200, 40), 1, INV_COLOR_NUMBERS, DT_LEFT);
    }

    pSurf->ReleaseDC();
#endif // NOGETDC

    CI3DObject::Draw(pRend);

    figures[4]->Show(TRUE);
    fTime += GetFrameTime();
    float fPart = .5f + (float)cos(fTime * 5) * .5f;
    if (bDrawNoFrames)
    {
        figures[4]->Show(FALSE);
        return TRUE;
    }
    if (!bIsPrototype)
    {
        figures[4]->SetDefaultColor(RGBA_MAKE(
            Max(0, Min(255, static_cast<int>(fPart * FRAME_R_TO + (1 - fPart) * FRAME_R_FROM))),
            Max(0, Min(255, static_cast<int>(fPart * FRAME_G_TO + (1 - fPart) * FRAME_G_FROM))),
            Max(0, Min(255, static_cast<int>(fPart * FRAME_B_TO + (1 - fPart) * FRAME_B_FROM))),
            Max(0, Min(255, static_cast<int>(fPart * FRAME_A_TO + (1 - fPart) * FRAME_A_FROM)))));

        figures[4]->SetRelativeToScreen(250, 300, 3);
        figures[4]->Draw(pRend);
        figures[4]->SetRelativeToScreen(350, 300, 3);
        figures[4]->Draw(pRend);
    }
    else
    {
        figures[4]->SetDefaultColor(RGBA_MAKE(
            Max(0, Min(255, static_cast<int>(fPart * FRAMEADD_R_TO + (1 - fPart) * FRAME_R_FROM))),
            Max(0, Min(255, static_cast<int>(fPart * FRAMEADD_G_TO + (1 - fPart) * FRAME_G_FROM))),
            Max(0, Min(255, static_cast<int>(fPart * FRAMEADD_B_TO + (1 - fPart) * FRAME_B_FROM))),
            Max(0,
                Min(255, static_cast<int>(fPart * FRAMEADD_A_TO + (1 - fPart) * FRAME_A_FROM)))));

        int nSlotsFree = 0;
        nMaxComplexity -= (int)dwComplexity;
        if (nMaxComplexity > 0)
            nSlotsFree = nMaxComplexity / 10 + (nMaxComplexity % 10 ? 1 : 0);
        else
            nSlotsFree = 0;
        int n = 0;
        for (x = 450; x < 650; x += 100)
            for (y = 150; y < 550; y += 100)
            {
                //			if ( n >= nMods && n < nMods + nSlotsFree )
                {
                    figures[4]->SetRelativeToScreen(x, y, 3);
                    figures[4]->Draw(pRend);
                }
                n++;
            }
    }
    figures[4]->Show(FALSE);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
CItem *CI3DConstrSpell::GetMouseItem(CPoint point, int &nSlotType)
{
    nSlotType = 0;
    if (!IsVisible())
        return 0;
    int ht = HitTest(point);
    if (ht == 0)
    {
        nSlotType = IST_SPELL_READY;
        for (int i = 0; i < pTableParty->allWagonItems.GetSize(); i++)
            if (pScreen->IsItemSpell(pTableParty->allWagonItems[i]) ||
                ModAlchemy::Scroll(pTableParty->allWagonItems[i]))
                return pTableParty->allWagonItems[i];
    }
    if (ht == 1)
    {
        nSlotType = IST_SPELL_PROT;
        for (int i = 0; i < pTableParty->allWagonItems.GetSize(); i++)
            if (pScreen->IsItemPrototype(pTableParty->allWagonItems[i]))
                return pTableParty->allWagonItems[i];
    }
    if (ht >= 2 && ht < 10)
    {
        nSlotType = IST_SPELL_MOD;
        int n = 0;
        for (int i = 0; i < pTableParty->allWagonItems.GetSize(); i++)
            if (pScreen->IsItemModifier(pTableParty->allWagonItems[i]))
            {
                if (n == ht - 2)
                    return pTableParty->allWagonItems[i];
                n++;
            }
    }
    if (ht == 10)
        nSlotType = IST_MAX_COMPLEXITY;
    if (ht == 11)
        nSlotType = IST_MAX_MANA;
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DConstrItem::Init(CParty *pItems)
{
    SetRect(200, 100, 600, 500);
    ePartType = ISP_ITEMCONSTR;

    pTableParty = pItems;

    AddTexture("constritem1");
    AddTexture("constritem2");
    AddTexture("constritem3");
    AddTexture("constritem4");

    // result
    AddArea()->MakeRect(200, 250, 300, 350);
    // instruction
    AddArea()->MakeRect(300, 200, 400, 300);
    // spell
    AddArea()->MakeRect(300, 300, 400, 400);
    // materials
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 4; j++)
            AddArea()->MakeRect(400 + i * 100, 100 + j * 100, 500 + i * 100, 200 + j * 100);

    CI3DFigure *pFig;
    pFig = AddFigure();
    pFig->MakeBar(7, 100, 200, 100, 400, 300, 28, 28, 228, 228);
    pFig->SetTexture(textures[0]);
    pFig = AddFigure();
    pFig->MakeBar(7, 100, 400, 100, 600, 300, 28, 28, 228, 228);
    pFig->SetTexture(textures[1]);
    pFig = AddFigure();
    pFig->MakeBar(7, 100, 200, 300, 400, 500, 28, 28, 228, 228);
    pFig->SetTexture(textures[2]);
    pFig = AddFigure();
    pFig->MakeBar(7, 100, 400, 300, 600, 500, 28, 28, 228, 228);
    pFig->SetTexture(textures[3]);

    pFig = AddFigure();
    pFig->MakeBarFrame2(3, 100, -40, -40, 40, 40, 5);
    pFig->UseDefaultColors(TRUE);
    pFig->SetDefaultColor(RGBA_MAKE(255, 255, 255, 150));
    pFig->SetTexture(0);
    pFig->Show(FALSE);

    fTime = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DConstrItem::Draw(CRenderer *pRend)
{
    const SIDBItemGeneralShape *pShape = 0;
    bool bDrawNoFrames = false;
    bool bIsInstr = false;
    int nMats = 0;
    D3DRECT rect;
    CRect rcTmp;
    int x = 0, y = 0;
    SDrawItem item;
    CI3DObject::Draw(pRend);
    for (int i = 0; i < pTableParty->allWagonItems.GetSize(); i++)
    {
        item.pItem = pTableParty->allWagonItems[i];
        if (pScreen->IsItemConstructed(item.pItem))
        {
            pI3D->GetRenderer()->FlushBuffers();
            rcTmp.left = 215;
            rcTmp.top = 265;
            rcTmp.right = 285;
            rcTmp.bottom = 335;
            pI3D->VirtualToScreen(rcTmp);
            rect.x1 = rcTmp.left;
            rect.y1 = rcTmp.top;
            rect.x2 = rcTmp.right;
            rect.y2 = rcTmp.bottom;
            pI3D->GetRenderer()->Clear(0, 0, D3DCLEAR_ZBUFFER, 0, 0, 0);
            pI3D->GetRenderer()->Clear(1, &rect, D3DCLEAR_ZBUFFER, 0, 1, 0);

            item.fX = 250;
            item.fY = 300;
            item.fZ = 6;
            item.fPixSize = 100;
            item.eFrom = ePartType;
            pScreen->DrawItem(pRend, item);

            pI3D->GetRenderer()->FlushBuffers();
            pI3D->GetRenderer()->Clear(0, 0, D3DCLEAR_ZBUFFER, 0, 1, 0);
            pI3D->GetRenderer()->FlushBuffers();

            bDrawNoFrames = true;
        }
        else if (pScreen->IsItemInstruction(item.pItem))
        {
            pI3D->GetRenderer()->FlushBuffers();
            rcTmp.left = 315;
            rcTmp.top = 215;
            rcTmp.right = 385;
            rcTmp.bottom = 285;
            pI3D->VirtualToScreen(rcTmp);
            rect.x1 = rcTmp.left;
            rect.y1 = rcTmp.top;
            rect.x2 = rcTmp.right;
            rect.y2 = rcTmp.bottom;
            pI3D->GetRenderer()->Clear(0, 0, D3DCLEAR_ZBUFFER, 0, 0, 0);
            pI3D->GetRenderer()->Clear(1, &rect, D3DCLEAR_ZBUFFER, 0, 1, 0);

            item.fX = 350;
            item.fY = 250;
            item.fZ = 6;
            item.fPixSize = 100;
            item.eFrom = ePartType;
            pScreen->DrawItem(pRend, item);

            pI3D->GetRenderer()->FlushBuffers();
            pI3D->GetRenderer()->Clear(0, 0, D3DCLEAR_ZBUFFER, 0, 1, 0);
            pI3D->GetRenderer()->FlushBuffers();

            bIsInstr = true;

            CItemManager *pIM = static_cast<CItemManager *>(item.pItem->GetManager());
            CItemLoot *pLoot = static_cast<CItemLoot *>(item.pItem);
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
        else if (pScreen->IsItemSpell(item.pItem))
        {
            item.fX = 350;
            item.fY = 350;
            item.fZ = 6;
            item.fPixSize = 100;
            item.eFrom = ePartType;
            pScreen->DrawItem(pRend, item);
            bDrawNoFrames = true;
        }
        else if (pScreen->IsItemMaterial(item.pItem))
        {
            nMats++;
            item.fX = x * 100 + 450.f;
            item.fY = y * 100 + 150.f;
            item.fZ = 6;
            item.fPixSize = 100;
            item.eFrom = ePartType;
            pScreen->DrawItem(pRend, item);
            if (++y > 3)
            {
                y = 0;
                x++;
            }
        }
    }

    figures[4]->Show(TRUE);
    fTime += GetFrameTime();
    float fPart = .5f + (float)cos(fTime * 5) * .5f;
    figures[4]->SetDefaultColor(RGBA_MAKE(
        Max(0, Min(255, static_cast<int>(fPart * FRAME_R_TO + (1 - fPart) * FRAME_R_FROM))),
        Max(0, Min(255, static_cast<int>(fPart * FRAME_G_TO + (1 - fPart) * FRAME_G_FROM))),
        Max(0, Min(255, static_cast<int>(fPart * FRAME_B_TO + (1 - fPart) * FRAME_B_FROM))),
        Max(0, Min(255, static_cast<int>(fPart * FRAME_A_TO + (1 - fPart) * FRAME_A_FROM)))));
    if (bDrawNoFrames)
    {
        figures[4]->Show(FALSE);
        return TRUE;
    }
    if (!bIsInstr)
    {
        figures[4]->SetRelativeToScreen(250, 300, 3);
        figures[4]->Draw(pRend);
        figures[4]->SetRelativeToScreen(350, 250, 3);
        figures[4]->Draw(pRend);
    }
    else if (pShape)
    {
        bool bNoFreeSlots = true;
        int nComp = pShape->dwComponents;
        int n = 0;
        for (x = 450; x < 650; x += 100)
            for (y = 150; y < 550; y += 100)
                if (++n > nMats && n <= nComp)
                {
                    figures[4]->SetRelativeToScreen(x, y, 3);
                    figures[4]->Draw(pRend);
                    bNoFreeSlots = false;
                }
        if (bNoFreeSlots)
        {
            figures[4]->SetDefaultColor(RGBA_MAKE(
                Max(0,
                    Min(255, static_cast<int>(fPart * FRAMEADD_R_TO + (1 - fPart) * FRAME_R_FROM))),
                Max(0,
                    Min(255, static_cast<int>(fPart * FRAMEADD_G_TO + (1 - fPart) * FRAME_G_FROM))),
                Max(0,
                    Min(255, static_cast<int>(fPart * FRAMEADD_B_TO + (1 - fPart) * FRAME_B_FROM))),
                Max(0, Min(255,
                           static_cast<int>(fPart * FRAMEADD_A_TO + (1 - fPart) * FRAME_A_FROM)))));
            figures[4]->SetRelativeToScreen(350, 350, 3);
            figures[4]->Draw(pRend);
        }
    }
    figures[4]->Show(FALSE);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
CItem *CI3DConstrItem::GetMouseItem(CPoint point, int &nSlotType)
{
    nSlotType = 0;
    if (!IsVisible())
        return 0;
    int ht = HitTest(point);
    if (ht == 0)
    {
        nSlotType = IST_ITEM_READY;
        for (int i = 0; i < pTableParty->allWagonItems.GetSize(); i++)
            if (pScreen->IsItemConstructed(pTableParty->allWagonItems[i]))
                return pTableParty->allWagonItems[i];
    }
    if (ht == 1)
    {
        nSlotType = IST_ITEM_INSTR;
        for (int i = 0; i < pTableParty->allWagonItems.GetSize(); i++)
            if (pScreen->IsItemInstruction(pTableParty->allWagonItems[i]))
                return pTableParty->allWagonItems[i];
    }
    if (ht == 2)
    {
        nSlotType = IST_ITEM_SPELL;
        for (int i = 0; i < pTableParty->allWagonItems.GetSize(); i++)
            if (pScreen->IsItemSpell(pTableParty->allWagonItems[i]))
                return pTableParty->allWagonItems[i];
    }
    if (ht > 2)
    {
        nSlotType = IST_ITEM_MAT;
        int n = 0;
        for (int i = 0; i < pTableParty->allWagonItems.GetSize(); i++)
            if (pScreen->IsItemMaterial(pTableParty->allWagonItems[i]))
            {
                if (n == ht - 3)
                    return pTableParty->allWagonItems[i];
                n++;
            }
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInventoryFaces::Init(std::vector<CString> *pFaces, bool bUp, bool bNoTips)
{
    pPeople = pFaces;

    AddTexture("InventoryM01");
    CI3DObjectInventory::Init(bUp);

    if (!bNoTips)
    {
        GetArea(0)->SetToolTip(40400);
        GetArea(1)->SetToolTip(40401);
    }

    bIsSorted = false;

    for (int i = 2; i < 8; i++)
        figures[i]->Show(FALSE);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInventoryFaces::FormSortedList()
{
    nSortedItems = pPeople->size();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DObjectInventoryFaces::Draw(CRenderer *pRend)
{
    SDrawHead head;
    int i;

    int nCurPos = (int)fCurPos[nSortMode];
    float shift = fCurPos[nSortMode] - (float)nCurPos;

    if (fCurPos[nSortMode] > 0)
        figures[0]->SetUShift(50.f / 256);
    else
        figures[0]->SetUShift(0);

    if (fCurPos[nSortMode] < float(nSortedItems - NUM_VISIBLE_ITEMS + 1))
        figures[1]->SetUShift(50.f / 256);
    else
        figures[1]->SetUShift(0);

    figures[9]->SetRelativeToScreen(50.f - shift * 100, (float)nTopHeight, fDepth * 2);
    for (i = 0; i < NUM_VISIBLE_ITEMS; i++)
        figures[10 + i]->SetRelativeToScreen(100.f + (i - shift) * 100, 50.f + nTopHeight, fDepth);

    figures[8]->Show(FALSE);
    if (nSelected >= 0 && nSelected < nSortedItems)
    {
        i = nSelected - nCurPos;
        if (i >= 0 && i < NUM_VISIBLE_ITEMS)
        {
            figures[8]->Show(TRUE);
            figures[8]->SetRelativeToScreen(100.f + (i - shift) * 100, 50.f + nTopHeight,
                                            fDepth * .9f);
        }
    }

    CI3DObject::Draw(pRend);

    // figures
    for (i = 0; i < NUM_VISIBLE_ITEMS; i++)
        if (nCurPos + i >= 0 && nCurPos + i < nSortedItems)
        {
            head.szPrototype = (*pPeople)[nCurPos + i];
            head.fX = 100.f + (i - shift) * 100;
            head.fY = 50.f + nTopHeight;
            head.fZ = fDepth * 1.5f;
            head.fPixSize = 100;
            pScreen->DrawHead(pRend, head);
        }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectInventoryFaces::OnLButtonDown(UINT nFlags, CPoint point)
{
    CI3DObjectInventory::OnLButtonDown(nFlags, point);
    int ht = HitTest(point);
    JournalHit(ht);
    if (ht == 8)
    {
        CPoint pt(point);
        pI3D->ScreenToVirtual(pt);
        int nItem = int((pt.x - 50) / 100.f + fCurPos[nSortMode]);
        if (nItem >= 0 && nItem < nSortedItems)
        {
            pI3D->BeginFX("buttons\\battle\\on_off.wav");
            pScreen->SetPrototype((*pPeople)[nItem]);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
