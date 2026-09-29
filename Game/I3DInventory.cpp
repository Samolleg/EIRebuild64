#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DMain.h"
#include "I3DInventory.h"
#include "TextManager.h"
#include "FigureManager.h"
#include "TextureManager.h"
#include "ModRuntime.h"
#include "ModPackage.h"
static float ModInfoProperty(const char*callback,const char*name,int property)
{
    auto*mod=ModRuntime::Current();
    return mod ? mod->InfoProperty(callback,name,property) : 0.f;
}

#include "SpellManager.h"
#include "ItemManager.h"
#include "ItemDatabase.h"
#include "WorldClient.h"
#include "StringProcessing.h"
#include "PerkDatabase.h"
#include "UnitDatabase.h"
#include "Object3DClientSpecific.h"
#include "RandomGen.h"
#include "Party.h"
#include "GameMain.h"
#include "LuaState.h"
#include "UnitServer.h"
#include "WorldServer.h"
/////////////////////////////////////////////////////////////////////////////////////
static LPCSTR ModInfoMaterialName(const CItem *item)
{
    if (!item || item->GetMaterial() >= unsigned(theItemDatabase.materials.GetNumEntries())) return "";
    return theItemDatabase.materials.GetEntry(item->GetMaterial())->szName;
}

const COLORREF INV_COLOR_INFO = RGB(228, 215, 167);
const COLORREF INV_COLOR_HEADER = RGB(255, 255, 255);
const float INVROT_MAX_SPEED = 5.f; // rad/sec
const float INVROT_MIN_SPEED = .1f; // rad/sec
const float INVROT_ACCEL = 8.f;     // rad/sec/sec

const int NUM_DRAW_RECTS = 6;
const int DRAW_RECT_LEFT[NUM_DRAW_RECTS] = { 290, 272, 272, 510, 510, 510 };
const int DRAW_RECT_TOP[NUM_DRAW_RECTS] = { 100, 190, 340, 190, 290, 390 };
const int DRAW_RECT_RIGHT[NUM_DRAW_RECTS] = { 510, 290, 290, 528, 528, 528 };
const int DRAW_RECT_BOTTOM[NUM_DRAW_RECTS] = { 500, 260, 410, 210, 310, 410 };
/////////////////////////////////////////////////////////////////////////////////////
CI3DFigure *CI3DFigureLRUAllocator::CreateObject(const char *pszFigName)
{
    CFigureManager *pFigMng = (CFigureManager *)theWorld.GetFigureManager();
    CI3DFigure *pFig = (CI3DFigure *)pFigMng->CreateI3DFigure(pszFigName);
    if (!pFig)
    {
        pFig = (CI3DFigure *)pFigMng->CreateI3DFigure();
        pFig->MakeBar(10, 70, 0, 100, 100, 0, 0, 0, 128, 128);
        pFig->RecalcBV();
    }
    pFig->AddRef();
    return pFig;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DFigureLRUAllocator::DestroyObject(CI3DFigure *pFig)
{
    pFig->Release();
}
/////////////////////////////////////////////////////////////////////////////////////
CTexture *CTextureLRUAllocator::CreateObject(const char *pszTexName)
{
    // copy texture content to new object
    CTextureManager *pTexMng = (CTextureManager *)theWorld.GetTextureManager();
    const bool modGraphics = ModRuntime::Current() &&
        ModRuntime::Current()->CombatSetting("JMVGraphics2");
    CString resourceName = pszTexName;
    LPBYTE pTexData = nullptr;
    int atlasSize = 256;
    if (modGraphics)
    {
        // The mod's inventory atlas selects the highest available resource first.
        const char *suffixes[] = {".x4.mmp", ".x2.mmp", ".mmp"};
        for (int variant = 0; variant < 3; ++variant)
        {
            CString candidate = CString(pszTexName) + suffixes[variant];
            pTexData = pTexMng->GetMMPData(candidate);
            if (!pTexData) continue;
            resourceName = candidate;
            atlasSize = variant == 0 ? 2048 : variant == 1 ? 1024 : 256;
            if (variant != 2)
            {
                CString base(pszTexName);
                base.MakeLower();
                if (base.Left(6) == "unhuma" || base.Left(2) == "pl" ||
                    base.Left(2) == "lg" || base.Left(2) == "sh" ||
                    base.Left(2) == "pt" || base.Left(2) == "gl" ||
                    base.Left(2) == "bt")
                    atlasSize = 1024;
                else if (base.Left(5) == "spell" || base.Left(8) == "modifier" ||
                         base.Left(8) == "material" || base.Left(5) == "qitem" ||
                         base.Left(5) == "litem" || base.Left(6) == "quitem" ||
                         base.Left(11) == "prototype00")
                    atlasSize = 2048;
            }
            break;
        }
    }
    else
    {
        resourceName += ".mmp";
        pTexData = pTexMng->GetMMPData(resourceName);
    }

    int nSrcSizeX = 256, nSrcSizeY = 256;
    if (pTexData)
    {
        nSrcSizeX = ((SMMPFileHeader *)pTexData)->dwDimentionX;
        nSrcSizeY = ((SMMPFileHeader *)pTexData)->dwDimentionY;
    }
    const bool hasSource = pTexData != nullptr;
    pTexMng->CloseAllFiles();
    if (nSrcSizeX <= 0 || nSrcSizeY <= 0 || nSrcSizeX > atlasSize || nSrcSizeY > atlasSize)
        return 0;

    CTexture *pTex = pTexMng->CreateInterfaceTexture(atlasSize, atlasSize);
    if (!pTex) return 0;
    pTex->AddRef();
    const int dstY = atlasSize - nSrcSizeY;
    if (hasSource && !PasteTexture(pTex, resourceName, nSrcSizeX, nSrcSizeY,
                                  nSrcSizeX, nSrcSizeY, 0, dstY))
    {
        pTex->Release();
        return 0;
    }
    return pTex;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTextureLRUAllocator::DestroyObject(CTexture *pTex)
{
    pTex->Release();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DItemScreen::DrawUnit(CRenderer *pRend, CUnit *pDrawUnit, float fAngle, float fX,
                              bool bArmors)
{
    if (!pDrawUnit)
        return;

    D3DRECT rects[NUM_DRAW_RECTS];
    float fDist = 6.f;
    CPlot3D pos;
    CQuaternion rot;
    CRect rcTmp;

    pI3D->GetRenderer()->FlushBuffers();
    pI3D->GetRenderer()->Clear(0, 0, D3DCLEAR_ZBUFFER, 0, 0, 0);
    if (bArmors)
    {
        for (int i = 0; i < NUM_DRAW_RECTS; i++)
        {
            rcTmp.left = DRAW_RECT_LEFT[i];
            rcTmp.top = DRAW_RECT_TOP[i];
            rcTmp.right = DRAW_RECT_RIGHT[i];
            rcTmp.bottom = DRAW_RECT_BOTTOM[i];
            pI3D->VirtualToScreen(rcTmp);
            rects[i].x1 = rcTmp.left;
            rects[i].y1 = rcTmp.top;
            rects[i].x2 = rcTmp.right;
            rects[i].y2 = rcTmp.bottom;
        }
        pI3D->GetRenderer()->Clear(NUM_DRAW_RECTS, rects, D3DCLEAR_ZBUFFER, 0, 1, 0);
    }
    else
    {
        if (fX > 400)
            rcTmp = CRect(410, 100, 700, 500);
        else
            rcTmp = CRect(200, 100, 600, 500);
        pI3D->VirtualToScreen(rcTmp);
        rects[0].x1 = rcTmp.left;
        rects[0].y1 = rcTmp.top;
        rects[0].x2 = rcTmp.right;
        rects[0].y2 = rcTmp.bottom;
        pI3D->GetRenderer()->Clear(1, rects, D3DCLEAR_ZBUFFER, 0, 1, 0);
    }
    pos = pDrawUnit->GetCP();
    rot = pDrawUnit->GetOrientation();

    //* CQuaternion( CPlot3D(0,0,-1), FP_PI2 * .23f ) );

    DWORD oldamb;
    oldamb = pRend->GetAmbientColor();
    pRend->SetAmbientColor(1, 1, 1);

    pDrawUnit->SetFlag(OBJ_USE_DEFAULT_FOG);

    pDrawUnit->SetExactPosition((timeGetTime() / 1024.0f) * 15); // theWorld.GetPreciseTime() );
    //
    CQuaternion rotNew = CQuaternion(CPlot3D(0, 1, 0), fAngle);
    CPlot3D posNew(0, 0, 0);
    posNew = rotNew.Rotate(posNew);
    pDrawUnit->SetCP(CPlot3D((fX / 400.f - 1.f) * VIEWING_ANGLE_TAN * fDist,
                             (440 / 400.f - .75f) * VIEWING_ANGLE_TAN * fDist, fDist) +
                     posNew);
    rotNew = rotNew * CQuaternion(CPlot3D(1, 0, 0), FP_PI2);
    if (fX > 400)
        rotNew = rotNew * CQuaternion(CPlot3D(0, 0, -1), FP_PI2 * 0.15f);
    pDrawUnit->SetOrientation(rotNew);

    ((CObject3DClientSpecific *)pDrawUnit->GetObjectSpecific())->SetScale(1.2f, 1.2f, 1.2f);
    {
        // reset mip-mapping (and store old value)
        CRenderer *pRend = pI3D->GetRenderer();
        DWORD dwMipFilter0 = 0;
        // TODO ALEX mip - high quality texture
        pDrawUnit->DrawWithoutBBox(pRend);
    }
    ((CObject3DClientSpecific *)pDrawUnit->GetObjectSpecific())->SetScale(1.f, 1.f, 1.f);

    pDrawUnit->SetCP(pos);
    pDrawUnit->SetOrientation(rot);

    pDrawUnit->ResetFlag(OBJ_USE_DEFAULT_FOG);
    pI3D->GetRenderer()->FlushBuffers();
    pRend->SetAmbientColor(oldamb);
}
/////////////////////////////////////////////////////////////////////////////////////
static const D3DCOLORVALUE dcvMagicColors[] = { { 1, 0, 0, 1 }, { 0, 1, 1, 1 }, { 0, 1, 0, 1 },
                                                { 1, 1, 0, 1 }, { 1, 1, 0, 1 }, { 1, 0, 1, 1 },
                                                { 1, 0, 1, 1 }, { 0, 0, 1, 1 } };
void CI3DItemScreen::DrawItem(CRenderer *pRend, SDrawItem &item)
{
    CI3DFigure *pFig = figuresLRU.GetObject(item.pItem->GetI3DFigureName());
    CTexture *pTex = texturesLRU.GetObject(item.pItem->GetI3DTextureName());
    if (!pFig || !pTex)
        return;
    pFig->SetTexture(pTex);
    pFig->SetRelativeToScreen(item.fX, item.fY, item.fZ);
    pFig->SetScale(item.fZ * VIEWING_ANGLE_TAN * item.fPixSize * .75f / 800);
    pFig->SetMaterial(pMat1);
    //
    if (item.pItem->GetUserData() == 0)
    {
        CRotateItem *pInfo = dbgnew CRotateItem;
        pInfo->fAngle = 0;
        pInfo->fSpeed = 0;
        pInfo->bIncludePrice = false;
        pInfo->fFlashVal = random.GetFloat(0, FP_2PI);
        item.pItem->AttachUserData(pInfo);
    }

    if (item.eFrom != ISP_UNKNOWN)
    {
        pFig->SetAmbient(1, 1, 1);
        //
        float fTime = GetFrameTime();
        CRotateItem *pInfo = static_cast<CRotateItem *>(item.pItem->GetUserData());
        ETypeIDs tid = item.pItem->GetTypeID();
        // step magic flashing
        if (item.pItem->GetSpellsCount() != 0 &&
            (tid == ITID_ITEM_WEAPON || tid == ITID_ITEM_ARMOR))
        {
            const CSpell *pSpell = item.pItem->GetSpell(0);
            if (pSpell)
            {
                ERPGMagicSubtypes magic = pSpell->GetMagicSubtype();
                if (magic < RPGMST_UNKNOWN)
                {
                    const D3DCOLORVALUE &color = dcvMagicColors[magic];
                    pInfo->fFlashVal = fmod(pInfo->fFlashVal + fTime * 5.0, 2.0 * pi);
                    float fVal = 0.15f + sin(pInfo->fFlashVal) * 0.15f;
                    pFig->SetDefaultSpecular(
                        D3DRGB(color.r * fVal, color.g * fVal, color.b * fVal));
                }
                else
                    pInfo->fFlashVal = 0;
            }
            else
                pInfo->fFlashVal = 0;
        }
        // rotate
        if (item.pItem == pMouseItem)
            pInfo->fSpeed += fTime * INVROT_ACCEL;
        else
        {
            if (pInfo->fSpeed < INVROT_MIN_SPEED)
            {
                pInfo->fAngle = 0;
                pInfo->fSpeed = 0;
            }
            else
                pInfo->fSpeed = (2 * FP_PI - pInfo->fAngle) * 4.f;
        }
        if (pInfo->fSpeed > INVROT_MAX_SPEED)
            pInfo->fSpeed = INVROT_MAX_SPEED;
        pInfo->fAngle += fTime * pInfo->fSpeed;
        while (pInfo->fAngle > FP_2PI)
            pInfo->fAngle -= FP_2PI;

        if (IsItemSpell(item.pItem) || IsItemPrototype(item.pItem) || IsItemModifier(item.pItem))
            pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), FP_PI));
        else if (tid == ITID_ITEM_WEAPON)
        {
            pFig->SetOrientation(CQuaternion(CPlot3D(1, 1, 0), pInfo->fAngle) *
                                 CQuaternion(CPlot3D(1, 0, 0), FP_PI));
        }
        else if (IsItemInstruction(item.pItem))
        {
            /*
            EIDBInstructionType eType = ((CItemLoot*)item.pItem)->GetInstructionType();
            if ( eType == IDBINS_WEAPON )
                    pFig->SetOrientation( CQuaternion( CPlot3D(1,1,0), pInfo->fAngle ) *
                            CQuaternion( CPlot3D(1,0,0), FP_PI ) );
            else
                    pFig->SetOrientation( CQuaternion( CPlot3D(0,1,0), pInfo->fAngle ) *
                            CQuaternion( CPlot3D(1,0,0), FP_PI ) );
            */
            pFig->SetOrientation(CQuaternion(CPlot3D(1, 0, 0), FP_PI));
        }
        else
            pFig->SetOrientation(CQuaternion(CPlot3D(0, 1, 0), pInfo->fAngle) *
                                 CQuaternion(CPlot3D(1, 0, 0), FP_PI));

        // draw background
        CI3DFigure *pFigure = AddFigure();
        pFigure->MakeBar(item.fZ * 1.3f, 100, item.fX - 35, item.fY - 35, item.fX + 35,
                         item.fY + 35, 157, 157, 227, 227);
        pFigure->SetTexture(textures[2]);
        pFigure->Draw(pRend);
        RemoveFigure(pFigure);
    }
    else
    {
        pFig->SetAmbient(.5f, .5f, .5f);
        if (IsItemSpell(item.pItem) || IsItemPrototype(item.pItem) || IsItemModifier(item.pItem))
            pFig->SetOrientation(CQuaternion(CPlot3D(0, 0, 1), FP_PI));
        else
            pFig->SetOrientation(CQuaternion(CPlot3D(1, 0, 0), FP_PI));
        LPBOUNDINGVOLUME bv = pFig->GetBoundingVolume();
        float fMaxSize = Max(bv->ptMax.x, bv->ptMax.y);

        pFig->SetRelativeToScreen(item.fX - bv->ptCenter.x * item.fPixSize / 2,
                                  item.fY + bv->ptCenter.y * item.fPixSize / 2, item.fZ);
        pFig->SetScale(item.fZ * VIEWING_ANGLE_TAN * item.fPixSize / 800 / fMaxSize);
    }
    // draw
    //		pFig->SetDefaultSpecular( 0xff000000 );
    pFig->SetDefaultColor(
        D3DRGBA(pFig->GetAmbient().x, pFig->GetAmbient().y, pFig->GetAmbient().z, 1));
    pFig->UseDefaultColors(TRUE);
    if (item.pItem->GetSpellsCount() != 0)
        theRenderer.SetRenderState(D3DRENDERSTATE_SPECULARENABLE, TRUE);
    pFig->Draw(pRend);
    if (item.pItem->GetSpellsCount() != 0)
        theRenderer.SetRenderState(D3DRENDERSTATE_SPECULARENABLE, FALSE);

    if (IsItemSpell(item.pItem))
    {
        CSpell *pSpell = item.pItem->GetSpell(0);
        if (pSpell && pSpell->GetRunesCount() > 0)
        {
            float fX, fY, fZ, fXShift, fYShift;
            pFig->GetRelativeToScreen(fX, fY, fZ);
            float fScale = pFig->GetXScale();

            // draw modifiers
            CI3DFigure *pFigure = AddFigure();
            pFigure->MakeBar(fZ * .9f, 100, 0, 0, 35, 35, 0, 0, 32, 32);
            pFigure->SetTexture(textures[1]);
            pFigure->SetScale(fScale);
            if (item.eFrom != ISP_UNKNOWN)
                pFigure->SetAmbient(1, 1, 1);
            else
                pFigure->SetAmbient(.5f, .5f, .5f);

            for (int i = 0; i < pSpell->GetRunesCount(); i++)
            {
                int nMod = pSpell->GetRunes()[i] - 1;
                pFigure->SetUShift((nMod & 0x7) / 8.f);
                pFigure->SetVShift(-(nMod >> 3) / 8.f);

                fXShift = 550 * fScale / fZ;
                fYShift = 550 * fScale / fZ;
                if (i == 0 || i == 4)
                    fXShift = 0;
                else if (i == 5 || i == 6 || i == 7)
                    fXShift = -fXShift;
                if (i == 2 || i == 6)
                    fYShift = 0;
                else if (i == 0 || i == 1 || i == 7)
                    fYShift = -fYShift;

                pFigure->SetRelativeToScreen(fX + fXShift, fY + fYShift, fZ * .9f);
                pFigure->Draw(pRend);
            }

            RemoveFigure(pFigure);
        }
    }
    // draw grayed
    if (item.eFrom != ISP_UNKNOWN && !CanClickOnItem(item.pItem, item.eFrom))
    {
        float fDepth = item.fZ * 1.25f;
        if (IsItemPrototype(item.pItem) || IsItemModifier(item.pItem) || IsItemSpell(item.pItem))
            fDepth = item.fZ * .8f;
        CI3DFigure *pFigure = AddFigure();
        pFigure->MakeBar(fDepth, 100, item.fX - 35, item.fY - 35, item.fX + 35, item.fY + 35);
        pFigure->SetTexture(0);
        pFigure->UseDefaultColors(TRUE);
        pFigure->SetDefaultColor(RGBA_MAKE(128, 64, 64, 130));
        pFigure->Draw(pRend);
        RemoveFigure(pFigure);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DItemScreen::DrawHead(CRenderer *pRend, SDrawHead &head)
{
    const SUDBUnitPrototype *pProt = theUnitDatabase.prototypes[head.szPrototype];
    const SUDBRaceModel *pRace = theUnitDatabase.races[pProt->szRace];

    CString szSex, szFigureName, szTextureName;
    if (strcmp(pRace->szMaskName, "unhuma") == 0)
        szSex = "m";
    else if (strcmp(pRace->szMaskName, "unhufe") == 0)
        szSex = "f";
    szFigureName.Format("infa%sme%dface", pRace->szMaskName, pProt->dwHair + 1);
    szTextureName.Format("face%s%02d", szSex, pProt->dwSkinIndex);

    CI3DFigure *pFig = figuresLRU.GetObject(szFigureName);
    CTexture *pTex = texturesLRU.GetObject(szTextureName);
    if (!pFig || !pTex)
        return;
    pFig->SetTexture(pTex);
    pFig->SetRelativeToScreen(head.fX, head.fY, head.fZ);
    pFig->SetScale(head.fZ * VIEWING_ANGLE_TAN * head.fPixSize * .75f / 800);
    pFig->SetMaterial(pMat1);
    pFig->SetAmbient(1, 1, 1);
    CQuaternion qRot(CPlot3D(1, 0, 0), FP_PI);
    // qRot = CQuaternion( CPlot3D(0,1,0), (head.fX - 400) / 1000 ) * qRot;
    qRot = CQuaternion(CPlot3D(-1, 0, 0), (head.fY - 300) / 1000) * qRot;
    pFig->SetOrientation(qRot);

    // draw background
    CI3DFigure *pFigure = AddFigure();
    pFigure->MakeBar(head.fZ * 1.3f, 100, head.fX - 35, head.fY - 35, head.fX + 35, head.fY + 35,
                     157, 157, 227, 227);
    pFigure->SetTexture(textures[2]);
    pFigure->Draw(pRend);
    RemoveFigure(pFigure);

    // draw
    pFig->Draw(pRend);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DItemScreen::AddBackground()
{
    AddObject(pCampBG = dbgnew CI3DObjectCampBG(pI3D));
    pCampBG->SetTexture(textures[0]);
    pCampBG->Init();

    AddTexture("modifiers2");
    AddTexture("campslots");

    GetRenderer()->SetBGColor(0);
    GetRenderer()->GetTransformPipeline()->SetFogRange(99, 100);

    texturesLRU.SetMaxSize(30);
    figuresLRU.SetMaxSize(30);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CI3DItemScreen::IsItemPrototype(CItem *pItem)
{
    if (pItem->GetTypeID() != ITID_ITEM_LOOT)
        return false;
    if (static_cast<CItemLoot *>(pItem)->GetLootType() != IDBLIT_INSTRUCTION)
        return false;
    if (static_cast<CItemLoot *>(pItem)->GetInstructionType() != IDBINS_PROTOTYPE)
        return false;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CI3DItemScreen::IsItemModifier(CItem *pItem)
{
    if (pItem->GetTypeID() != ITID_ITEM_LOOT)
        return false;
    if (static_cast<CItemLoot *>(pItem)->GetLootType() != IDBLIT_INSTRUCTION)
        return false;
    if (static_cast<CItemLoot *>(pItem)->GetInstructionType() != IDBINS_RUNE)
        return false;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CI3DItemScreen::IsItemSpell(CItem *pItem)
{
    return pItem->GetTypeID() == ITID_ITEM_SPELL;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CI3DItemScreen::IsItemInstruction(CItem *pItem)
{
    if (pItem->GetTypeID() != ITID_ITEM_LOOT)
        return false;
    if (static_cast<CItemLoot *>(pItem)->GetLootType() != IDBLIT_INSTRUCTION)
        return false;
    EIDBInstructionType eType = static_cast<CItemLoot *>(pItem)->GetInstructionType();
    if (eType != IDBINS_WEAPON && eType != IDBINS_ARMOR && eType != IDBINS_QUICK)
        return false;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CI3DItemScreen::IsItemMaterial(CItem *pItem)
{
    if (pItem->GetTypeID() != ITID_ITEM_LOOT)
        return false;
    if (static_cast<CItemLoot *>(pItem)->GetLootType() != IDBLIT_MATERIAL)
        return false;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CI3DItemScreen::IsItemConstructed(CItem *pItem)
{
    if (pItem->GetShapeEntry() && !pItem->GetShapeEntry()->IsDeconstructable())
        return false;
    if (pItem->GetTypeID() == ITID_ITEM_WEAPON || pItem->GetTypeID() == ITID_ITEM_ARMOR)
        return true;
    if (pItem->GetTypeID() == ITID_ITEM_QUICK)
    {
        if (pItem->GetShapeEntry() &&
            pItem->GetShapeEntry()->szMaterialType.CompareNoCase("none") != 0)
            return true;
        else
            return false;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
CItem *CI3DItemScreen::MoveItem(CItem *pItem, CParty *pDst, CParty *pSrc, int nDstLimit,
                                bool bStack, bool bIncludePrice)
{
    CItem *pItemRes = 0;
    bool bAll = bStack && pI3D->IsShiftDown();
    if (nDstLimit <= 0 || pDst->allWagonItems.GetSize() < nDstLimit)
    {
        pItemRes = pDst->AddItemToWagon(pItem, bAll, bStack);
        pSrc->RemoveItemFromWagon(pItem, bAll);
        if (bIncludePrice)
        {
            CRotateItem *pInfo = static_cast<CRotateItem *>(pItemRes->GetUserData());
            if (pInfo == 0)
            {
                pInfo = dbgnew CRotateItem;
                pInfo->fAngle = 0;
                pInfo->fSpeed = 0;
                pItemRes->AttachUserData(pInfo);
            }
            pInfo->bIncludePrice = true;
        }
    }
    else
    {
        for (int i = 0; i < nDstLimit; i++)
            if (pItem->Compare(pDst->allWagonItems[i]))
            {
                pItemRes = pDst->AddItemToWagon(pItem, bAll, bStack);
                pSrc->RemoveItemFromWagon(pItem, bAll);
                break;
            }
    }
    return pItemRes;
}
/////////////////////////////////////////////////////////////////////////////////////
CItem *CI3DItemScreen::MoveAllItems(CParty *pDst, CParty *pSrc)
{
    CItem *pItemRes = 0;
    CItem *pItem;
    while (pSrc->allWagonItems.GetSize())
    {
        pItem = pSrc->allWagonItems[0];
        pItemRes = pDst->AddItemToWagon(pItem, true);
        pSrc->RemoveItemFromWagon(pItem, true);
    }
    return pItemRes;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DItemObject::OnLButtonDown(UINT nFlags, CPoint point)
{
    int nSlotType;
    CItem *pItem = GetMouseItem(point, nSlotType);
    if (pItem)
        pScreen->ClickOnItem(pItem, ePartType);
    else if (nSlotType)
        pScreen->ClickOnSlot(nSlotType, ePartType);
}
/////////////////////////////////////////////////////////////////////////////////////
CI3DObjectItemInfo::~CI3DObjectItemInfo()
{
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItemInfo::Init(int x, int y)
{
    nX = x;
    nY = y;
    AddSurface(dbgnew CI3DSurface(pI3D, nX, nY, nX + 200, nY + 300)); // text info

    CTextManager *pTxtMng = theWorld.GetTextManager();
    CString szTmp, szID;
    for (int i = 0; i < TOTAL; i++)
    {
        szID.Format("infoitem_%d", i);
        pTxtMng->GetSystemString(szID, szTmp);
        szStrings.Add(szTmp);
    }
    for (int i = 0; i < TOTALSKILL; i++)
    {
        szID.Format("infoskill_%d", i);
        pTxtMng->GetSystemString(szID, szTmp);
        szSkillStr.Add(szTmp);
    }
    itemParty.Create();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CI3DObjectItemInfo::Draw(CRenderer *pRend)
{
    CItem *pItem = GetItem();
    if (pItem)
    {
        SDrawItem item;
        item.pItem = pItem;
        item.fX = nX + 100.f;
        item.fY = nY + 200.f;
        item.fZ = 3;
        item.fPixSize = 120;
        item.eFrom = ISP_UNKNOWN;
        pScreen->DrawItem(pRend, item);
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItemInfo::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    CItem *pItem = GetItem();
    switch (nChar)
    {
    case VK_CONTROL:
        if (!(nFlags & 0x4000) && pItem)
            FormTextInfo();
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItemInfo::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    CItem *pItem = GetItem();
    switch (nChar)
    {
    case VK_CONTROL:
        if (pItem)
            FormTextInfo();
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItemInfo::ResetInputState()
{
    FormTextInfo();
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItemInfo::SetItem(CItem *pNewItem)
{
    CItem *pItem = GetItem();
    if (pItem && pNewItem && pItem->Compare(pNewItem))
        return;
    itemParty.Clear();
    if (pNewItem)
    {
        itemParty.AddItemToWagon(pNewItem);
    }
    FormTextInfo();
}
/////////////////////////////////////////////////////////////////////////////////////
CItem *CI3DObjectItemInfo::GetItem()
{
    if (itemParty.allWagonItems.GetSize())
        return itemParty.allWagonItems[0];
    else
        return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
CSpell *CI3DObjectItemInfo::GetSpell()
{
    CItem *pItem = GetItem();
    if (pItem)
        return pItem->GetSpell(0);
    else
        return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItemInfo::FormTextInfo()
{
    const auto *mod = ModRuntime::Current();
    const bool compactInfoLines = !mod || mod->CombatSetting("JMVItemInfoLineHeight");
    const int descriptionLineStep = compactInfoLines ? 12 : 0;
    CItem *pItem = GetItem();
    CSpell *pSpell = GetSpell();

#ifndef NOGETDC
    surfaces[0]->GetDC();
    surfaces[0]->Clear();
    surfaces[0]->ReleaseDC();
#endif // NOGETDC

    if (!pItem)
        return;

    if (pScreen->IsItemMaterial(pItem))
    {
        FormMaterialInfo();
        return;
    }

    if (pScreen->IsItemInstruction(pItem))
    {
        FormInstructionInfo();
        return;
    }

    if (pScreen->IsItemPrototype(pItem))
    {
        FormPrototypeInfo();
        return;
    }

    if (pScreen->IsItemModifier(pItem))
    {
        FormModifierInfo();
        return;
    }

    ETypeIDs tid = pItem->GetTypeID();

    if (tid == ITID_ITEM_QUICK &&
        static_cast<CItemQuick *>(pItem)->GetQuickItemType() == IDB_SCROLL)
    {
        FormPotionInfo();
        return;
    }

    if ((pI3D->IsCtrlDown() && pItem->GetSpell(0)) || pItem->GetTypeID() == ITID_ITEM_SPELL)
    {
        FormSpellInfo();
        return;
    }

    CTextManager *pTxtMng = theWorld.GetTextManager();
    CItemManager *pItmMng = theWorld.GetItemManager();

    CString szMaterial;
    szMaterial = pItmMng->GetMaterial(pItem->GetMaterial())->szName;
    NStrProc::ReplaceChars(szMaterial, ' ', '_');
    szMaterial.MakeLower();

    CString szName, szDesc, szType;
    pTxtMng->GetItemNameAndDesc(pItem, szName, szDesc, szType);

#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    pSurf->GetDC();

    CString szTmp, szOut;
    CRect rc(15, 10, 185, 25);
    CPoint shift(0, compactInfoLines ? 12 : 14);
    COLORREF color = INV_COLOR_INFO;

    // NAME
    pSurf->DrawMultiText(szName, rc, 2, 0, INV_COLOR_HEADER, DT_CENTER | DT_WORDBREAK, TRUE, descriptionLineStep);
    rc += shift;
    rc += shift;
    // ITEM TYPE
    szOut.Format("%s %s", szStrings[ITEMTYPE], szType);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // MATERIAL
    bool bNeedOut = true;
    if (tid == ITID_ITEM_QUICK)
    {
        if (pItem->GetShapeEntry() &&
            pItem->GetShapeEntry()->szMaterialType.CompareNoCase("none") == 0)
            bNeedOut = false;
    }
    if (bNeedOut && tid != ITID_ITEM_LOOT && tid != ITID_ITEM_QUEST)
    {
        szTmp = "Unknown";
        if (pTxtMng->GetText(CString("matshort ") + szMaterial))
            pTxtMng->GetLine(szTmp);
        szOut.Format("%s %s x %d", szStrings[MATERIAL], szTmp, pItem->GetMaterialAmount());
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;
    }

    // WEIGHT
    if (tid != ITID_ITEM_QUEST)
    {
        szOut.Format("%s %d", szStrings[WEIGHT], (int)pItem->GetWeight());
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;
    }

    // ENERGY
    if (tid == ITID_ITEM_ARMOR || tid == ITID_ITEM_WEAPON ||
        (tid == ITID_ITEM_QUICK &&
         static_cast<CItemQuick *>(pItem)->GetQuickItemType() == IDB_WAND))
    {
        int nSlotsFilled = 0;
        if (pItem->GetSpell(0))
            nSlotsFilled = pItem->GetSpell(0)->GetComplexity();
        szOut.Format("%s %d", szStrings[ENERGY], (int)pItem->GetMaxMP());
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;
        szOut.Format("%s %d/%d", szStrings[SLOTS], nSlotsFilled, pItem->GetSpellSlots());
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;
    }

    // DURABILITY
    if (tid == ITID_ITEM_ARMOR || tid == ITID_ITEM_WEAPON)
    {
        int nHP = pItem->GetHP() > 0 ? (int)pItem->GetHP() : 0;
        szOut.Format("%s %d/%d", szStrings[DURABILITY], nHP, (int)pItem->GetMaxHP());
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;
    }

    // DAMAGE
    if (tid == ITID_ITEM_WEAPON) // traps add later
    {
        float damages[RPG_NUM_ATTACK_TYPES];
        CItemWeapon *pWeapon = static_cast<CItemWeapon *>(pItem);
        pWeapon->GetDamageDistribution(damages);
        int i = 0;
        for (; i < RPG_NUM_ATTACK_TYPES; i++)
            if (damages[i] != 0)
                break;
        if (i < RPG_NUM_ATTACK_TYPES)
        {
            szOut.Format("damage_%d", i);
            pTxtMng->GetSystemString(szOut, szTmp);
        }
        szOut.Format("%s %d-%d %s", szStrings[DAMAGE], (int)pWeapon->GetDamageMin(),
                     (int)pWeapon->GetDamageMax(), szTmp);
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;

        szOut.Format("%s %d", szStrings[ACTIONPTS], pWeapon->GetActionPoints());
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;

        szOut.Format("%s %d", szStrings[MODATTACK], (int)pWeapon->GetAtackBonus());
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;

        if (pWeapon->GetWeaponType() == RPGWST_BOW || pWeapon->GetWeaponType() == RPGWST_CROSSBOW)
            szOut.Format("%s %d", szStrings[RANGE], (int)pWeapon->GetRange());
        else
            szOut.Format("%s %d", szStrings[MODDEFENSE], (int)pWeapon->GetDefenceBonus());
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;
    }

    // ABSORPTION
    if (tid == ITID_ITEM_ARMOR)
    {
        float absorbs[RPG_NUM_ATTACK_TYPES];
        CItemArmor *pArmor = static_cast<CItemArmor *>(pItem);
        pArmor->GetAbsorbtions(absorbs, 0);

        CStringArray szResArray;
        FormAbsorptionStrings(absorbs, szResArray, false);

        szOut = szStrings[ABSORPARMOR] + " " + szResArray[0];
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;
        /*
        if ( !szResArray[1].IsEmpty() )
        {
                szOut = szStrings[RESIST] + " " + szResArray[1];
                pSurf->DrawText( szOut, rc, 0, color, DT_LEFT );
                rc += shift;
        }
        */
        if (!szResArray[2].IsEmpty())
        {
            szOut = szStrings[VULNERABLE] + " " + szResArray[2];
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
        }

        const CString armorName = pArmor->GetShapeEntry()->szName;
        const CString armorMaterial = pItmMng->GetMaterial(pItem->GetMaterial())->szName;
        auto drawSignedPercent = [&](const CString &label, float value)
        {
            if (value == 0.0f)
                return;
            szOut.Format("%s %+.0f%%", label, value * 100.0f);
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
        };
        auto drawArmorProperty = [&](const CString &label, float value, bool percent)
        {
            if (value == 0.0f)
                return;
            if (percent)
                szOut.Format("%s %+.0f%%", label, value * 100.0f);
            else
                szOut.Format("%s %+.0f", label, value);
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
        };

        drawArmorProperty(szStrings[MODDEFENSE],
                          ModInfoProperty("GetArmorPropsInside", armorName, 2), false);
        drawArmorProperty(szStrings[MODATTACK],
                          ModInfoProperty("GetArmorPropsInside", armorName, 3), false);

        float visibility = ModInfoProperty("GetArmorPropsInside", armorName, 5) +
                           ModInfoProperty("GetMaterialPropsInside", armorMaterial, 13);
        szTmp = "Unknown";
        if (visibility != 0.0f)
        {
            if (pTxtMng->GetText("string infoitem_53"))
                pTxtMng->GetLine(szTmp);
            drawSignedPercent(szTmp, visibility);
        }
        float hearing = ModInfoProperty("GetArmorPropsInside", armorName, 6) +
                        ModInfoProperty("GetMaterialPropsInside", armorMaterial, 14);
        szTmp = "Unknown";
        if (hearing != 0.0f)
        {
            if (pTxtMng->GetText("string infoitem_51"))
                pTxtMng->GetLine(szTmp);
            drawSignedPercent(szTmp, hearing);
        }

        float regeneration = ModInfoProperty("GetMaterialPropsInside", armorMaterial, 2);
        szTmp = "Unknown";
        if (regeneration > 0.00001f)
        {
            if (pTxtMng->GetText("string infoitem_80"))
                pTxtMng->GetLine(szTmp);
            szOut.Format("%s +%.2f%%", szTmp, regeneration);
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
        }
        float restoration = ModInfoProperty("GetMaterialPropsInside", armorMaterial, 3);
        szTmp = "Unknown";
        if (restoration > 0.00001f)
        {
            if (pTxtMng->GetText("string infoitem_81"))
                pTxtMng->GetLine(szTmp);
            szOut.Format("%s +%.2f%%", szTmp, restoration);
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
        }

    }
    const bool fixedMaterialStats = !ModRuntime::Current() || ModRuntime::Current()->CombatSetting("JMVArmorMaterialStats");
    if ((!fixedMaterialStats || tid == ITID_ITEM_ARMOR) && ModInfoMaterialName(pItem))
    {
        const CString armorMaterial = ModInfoMaterialName(pItem);
        const COLORREF materialColor = fixedMaterialStats ? color : INV_COLOR_HEADER;
        float movement = ModInfoProperty("GetMaterialPropsInside", armorMaterial, 9);
        szTmp = "Unknown";
        if (fixedMaterialStats ? movement != 0.0f : movement > 0.0f)
        {
            if (pTxtMng->GetText("string infounit_2"))
                pTxtMng->GetLine(szTmp);
            if (fixedMaterialStats)
                szOut.Format("%s %+.1f%%", szTmp, movement);
            else
                szOut.Format("%s +%.2f", szTmp, ModInfoProperty("GetMaterialPropsInside", armorMaterial, 10));
            pSurf->DrawText(szOut, rc, 0, materialColor, DT_LEFT);
            rc += shift;
        }
        const int materialProperties[] = {10, 11, 12};
        const char *materialLabels[] = {"PERK Strength", "PERK Dexterity", "PERK Intelligence"};
        for (int i = 0; i < 3; ++i)
        {
            float value = ModInfoProperty("GetMaterialPropsInside", armorMaterial, materialProperties[i]);
            if (value <= 0.0f)
                continue;
            szTmp = "Unknown";
            if (pTxtMng->GetText(materialLabels[i]))
                pTxtMng->GetLine(szTmp);
            szOut.Format("%s +%.2f", szTmp, value);
            pSurf->DrawText(szOut, rc, 0, materialColor, DT_LEFT);
            rc += shift;
        }
    }

    // LATER: SKILLS MODIFIED
    rc += shift;

    // DESCRIPTION
    if (tid == ITID_ITEM_LOOT)
    {
        pTxtMng->GetSystemString("loot_item", szTmp);
        szDesc += szTmp;
        pSurf->DrawMultiText(szDesc, rc, 7, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, descriptionLineStep);
        pSurf->ReleaseDC();
        return;
    }

    pSurf->DrawMultiText(szDesc, rc, 4, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, descriptionLineStep);
    rc += shift;
    rc += shift;
    rc += shift;
    rc += shift;
    if (tid != ITID_ITEM_WEAPON)
        rc += shift;

    // SPELL INSIDE
    if (tid == ITID_ITEM_ARMOR || tid == ITID_ITEM_WEAPON ||
        (tid == ITID_ITEM_QUICK &&
         static_cast<CItemQuick *>(pItem)->GetQuickItemType() == IDB_WAND))
    {
        CSpell *pSpell = pItem->GetSpell(0);
        if (pSpell)
        {
            szTmp = "Unknown";
            if (pTxtMng->GetText(CString("spell ") + pSpell->GetPrototype()->szCodeName))
                pTxtMng->GetLine(szTmp);
            szOut.Format("%s %s", szStrings[SPELL], szTmp);
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
            pSurf->DrawText(szStrings[PRESS_CTRL], rc, 0, color, DT_LEFT);
        }
    }

    pSurf->ReleaseDC();
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItemInfo::FormPotionInfo()
{
    CItem *pItem = GetItem();
    CSpell *pSpell = GetSpell();

    CTextManager *pTxtMng = theWorld.GetTextManager();

    CString szName, szDesc, szType;
    pTxtMng->GetItemNameAndDesc(pItem, szName, szDesc, szType);

#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    pSurf->GetDC();

    CString szOut;
    CRect rc(15, 10, 185, 25);
    CPoint shift(0, 12);
    COLORREF color = INV_COLOR_INFO;

    // NAME
    pSurf->DrawMultiText(szName, rc, 2, 0, INV_COLOR_HEADER, DT_CENTER | DT_WORDBREAK, TRUE, 12);
    rc += shift;
    rc += shift;
    // ITEM TYPE
    szOut.Format("%s %s", szStrings[ITEMTYPE], szType);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    CSpell *pSpellIn = pItem->GetSpell(0);

    if (pSpellIn)
    {
        // EFFECT
        if (!pSpellIn->GetPrototype()->bRuneAllowed[SDBAR_EFFECT])
            szOut.Format("%s %s", szStrings[EFFECT], szStrings[FIXEDEFF]);
        else
            szOut.Format("%s %d", szStrings[EFFECT], (int)pSpellIn->GetPower());
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;

        // DURATION
        if (pSpellIn->GetDuration() < 2)
            szOut.Format("%s %s", szStrings[DURATION], szStrings[INSTANT]);
        else
            szOut.Format("%s %.1f%s", szStrings[DURATION], pSpellIn->GetDuration() / 15.f,
                         szStrings[SECONDS]);
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;
        rc += shift;
    }

    // DESCRIPTION
    pSurf->DrawMultiText(szDesc, rc, 8, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, 12);

    pSurf->ReleaseDC();
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItemInfo::FormSpellInfo()
{
    CItem *pItem = GetItem();
    CSpell *pSpell = GetSpell();

    if (!pSpell)
    {
        surfaces[0]->ReleaseDC();
        return;
    }

    CTextManager *pTxtMng = theWorld.GetTextManager();

    CString szName, szDesc, szType;
    if (pTxtMng->GetText(CString("spell ") + pSpell->GetPrototype()->szCodeName))
    {
        pTxtMng->GetLine(szName);
        pTxtMng->GetUntilNextCommand(szDesc);
    }
    pTxtMng->GetSystemString("item_spell", szType);

#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    pSurf->GetDC();

    CString szTmp, szOut;
    CRect rc(15, 10, 185, 25);
    CPoint shift(0, 12);
    COLORREF color = INV_COLOR_INFO;

    // NAME
    pSurf->DrawMultiText(szName, rc, 2, 0, INV_COLOR_HEADER, DT_CENTER | DT_WORDBREAK, TRUE, 12);
    rc += shift;
    rc += shift;
    // ITEM TYPE
    szOut.Format("%s %s", szStrings[ITEMTYPE], szType);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // MANA AND RUNES
    szOut.Format("%s %d", szStrings[MANA], (int)pSpell->GetManaCost());
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;
    szOut.Format("%s %d", szStrings[SLOTS], pSpell->GetComplexity());
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // SCHOOL OF MAGIC
    szTmp.Format("school_%d", pSpell->GetMagicSchool());
    pTxtMng->GetSystemString(szTmp, szOut);
    szOut = szStrings[MAGICSCHOOL] + " " + szOut;
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // EFFECT
    if (!pSpell->GetPrototype()->bRuneAllowed[SDBAR_EFFECT])
        szOut.Format("%s %s", szStrings[EFFECT], szStrings[FIXEDEFF]);
    else
        szOut.Format("%s %d", szStrings[EFFECT], (int)pSpell->GetPower());
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // SPEED
    if (pSpell->GetSpeed() < 1.f)
        szOut.Format("%s %s", szStrings[SPEED], szStrings[INSTANT]);
    else
        szOut.Format("%s %d", szStrings[SPEED], (int)pSpell->GetSpeed());
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // RANGE
    szOut.Format("%s %d%s", szStrings[RANGE], (int)pSpell->GetRange(), szStrings[METERS]);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // AREA
    if (pSpell->IsOnUnit())
        szOut.Format("%s %s", szStrings[AREA], szStrings[AREAUNIT]);
    else
        szOut.Format("%s %.1f%s", szStrings[AREA], pSpell->GetArea(), szStrings[METERS]);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // DURATION
    if (pSpell->GetDuration() < 2)
        szOut.Format("%s %s", szStrings[DURATION], szStrings[INSTANT]);
    else
        szOut.Format("%s %.1f%s", szStrings[DURATION], pSpell->GetDuration() / 15.f,
                     szStrings[SECONDS]);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // TARGETS
    szOut.Format("%s %d", szStrings[TARGETS], pSpell->GetNumTargets());
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // LATER: FILTERS
    //	rc += shift;
    rc += shift;

    // DESCRIPTION
    pSurf->DrawMultiText(szDesc, rc, 4, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, 12);
    rc += shift;
    rc += shift;
    rc += shift;
    rc += shift;

    // MODIFIERS
    CSpellManager *pSplMng = theWorld.GetSpellManager();
    if (pSpell->GetRunesCount() > 0)
    {
        szOut = szStrings[MODIFIERS];
        for (int i = 0; i < pSpell->GetRunesCount(); i++)
        {
            szOut += " ";
            szTmp = "?";
            if (pTxtMng->GetText(CString("modifier ") +
                                 pSplMng->GetModifier(pSpell->GetRunes()[i] - 1)->pszCode))
            {
                pTxtMng->SkipLine();
                pTxtMng->SkipLine();
                pTxtMng->GetLine(szTmp);
            }
            szOut += szTmp + ",";
        }
        szOut.TrimRight(",");
        szOut += ".";
        pSurf->DrawMultiText(szOut, rc, 3, 0, color, DT_LEFT | DT_WORDBREAK, TRUE, 12);
    }

    pSurf->ReleaseDC();
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItemInfo::FormMaterialInfo()
{
    CItem *pItem = GetItem();
    CSpell *pSpell = GetSpell();

    CTextManager *pTxtMng = theWorld.GetTextManager();
    CItemManager *pItmMng = theWorld.GetItemManager();

    CItemLoot *pLoot = static_cast<CItemLoot *>(pItem);
    const SIDBMaterial *pMat = pItmMng->GetMaterial(pLoot->GetMaterialIndex());
    if (!pMat)
        return;

    CString szName, szDesc, szType;
    pTxtMng->GetItemNameAndDesc(pItem, szName, szDesc, szType);

#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    pSurf->GetDC();

    CString szTmp, szOut;
    CRect rc(15, 10, 185, 25);
    CPoint shift(0, 12);
    COLORREF color = INV_COLOR_INFO;

    // NAME
    pSurf->DrawMultiText(szName, rc, 2, 0, INV_COLOR_HEADER, DT_CENTER | DT_WORDBREAK, TRUE, 12);
    rc += shift;
    rc += shift;
    // ITEM TYPE
    szOut.Format("%s %s", szStrings[ITEMTYPE], szType);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // TYPE
    szTmp = pMat->szType;
    szTmp.MakeLower();
    szTmp = CString("mattype_") + szTmp;
    pTxtMng->GetSystemString(szTmp, szOut);
    szOut = szStrings[MATTYPE] + " " + szOut;
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // WEIGHT
    szOut.Format("%s %d", szStrings[WEIGHT], (int)pMat->fpWeight);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // ENERGY
    szOut.Format("%s %d", szStrings[ENERGY], (int)pMat->fpManaCapacity);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;
    szOut.Format("%s %d", szStrings[SLOTS], pMat->dwSpellSlots);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // DURABILITY
    szOut.Format("%s %d", szStrings[DURABILITY], (int)pMat->fpDurability);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // DAMAGE
    szOut.Format("%s %.1f", szStrings[DAMAGE], pMat->fpDamageModifier);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // ABSORPTION
    CStringArray szResArray;
    float absorbs[RPG_NUM_ATTACK_TYPES];
    for (int i = 0; i < RPG_NUM_ATTACK_TYPES; i++)
        absorbs[i] = pMat->fpAbsorbtionMultipliers[i];

    FormAbsorptionStrings(absorbs, szResArray, false);
    szOut = szStrings[ABSORPTION] + " " + szResArray[0];
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;
    /*
    if ( !szResArray[1].IsEmpty() )
    {
            szOut = szStrings[RESIST] + " " + szResArray[1];
            pSurf->DrawText( szOut, rc, 0, color, DT_LEFT );
            rc += shift;
    }
    */
    if (!szResArray[2].IsEmpty())
    {
        szOut = szStrings[VULNERABLE] + " " + szResArray[2];
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;
    }

    rc += shift;

    // DESCRIPTION
    pSurf->DrawMultiText(szDesc, rc, 4, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, 12);

    pSurf->ReleaseDC();
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItemInfo::FormInstructionInfo()
{
    CItem *pItem = GetItem();
    CSpell *pSpell = GetSpell();

    enum
    {
        INSTR_WEAPON,
        INSTR_ARMOR,
        INSTR_WAND,
        INSTR_TRAP,
        INSTR_OTHER
    };

    int nType;

    CTextManager *pTxtMng = theWorld.GetTextManager();
    CItemManager *pItmMng = theWorld.GetItemManager();

    const SIDBItemGeneralShape *pShape;
    CItemLoot *pLoot = static_cast<CItemLoot *>(pItem);
    assert((pLoot->GetInstructionType() >= IDBINS_WEAPON) &&
           (pLoot->GetInstructionType() <= IDBINS_QUICK));
    switch (pLoot->GetInstructionType())
    {
    case IDBINS_WEAPON:
        nType = INSTR_WEAPON;
        pShape = pItmMng->GetWeaponShape(pLoot->GetInstructionItemIndex());
        break;
    case IDBINS_ARMOR:
        nType = INSTR_ARMOR;
        pShape = pItmMng->GetArmorShape(pLoot->GetInstructionItemIndex());
        break;
    case IDBINS_QUICK:
        nType = INSTR_WAND;
        pShape = pItmMng->GetQuickItemShape(pLoot->GetInstructionItemIndex());
        break;
    }

    CString szName, szDesc, szType;
    pTxtMng->GetItemNameAndDesc(pItem, szName, szDesc, szType);

#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    pSurf->GetDC();

    CString szTmp, szOut;
    CRect rc(15, 10, 185, 25);
    CPoint shift(0, 12);
    COLORREF color = INV_COLOR_INFO;

    // NAME
    pSurf->DrawMultiText(szName, rc, 2, 0, INV_COLOR_HEADER, DT_CENTER | DT_WORDBREAK, TRUE, 12);
    rc += shift;
    rc += shift;
    // ITEM TYPE
    szOut.Format("%s %s", szStrings[ITEMTYPE], szType);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // MATERIAL
    szTmp = pShape->szMaterialType;
    szTmp.MakeLower();
    szTmp = CString("mattype_") + szTmp;
    pTxtMng->GetSystemString(szTmp, szOut);
    szTmp = szOut;
    szOut.Format("%s %s x %d", szStrings[MATERIAL], szTmp, pShape->dwComponents);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // WEIGHT
    szOut.Format("%s %d", szStrings[WEIGHT], (int)pShape->fpWeight);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // ENERGY
    if (nType <= INSTR_WAND)
    {
        szOut.Format("%s %d", szStrings[ENERGY], (int)pShape->fpManaCapacity);
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;
        szOut.Format("%s %d", szStrings[SLOTS], pShape->dwSpellSlots);
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;
    }

    // DURABILITY
    if (nType <= INSTR_ARMOR)
    {
        szOut.Format("%s %d", szStrings[DURABILITY], (int)pShape->fpDurability);
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;
    }

    // LATER: add traps
    // DAMAGE
    if (nType == INSTR_WEAPON)
    {
        const SIDBWeaponShape *pWeaponShape = static_cast<const SIDBWeaponShape *>(pShape);
        int i = 0;
        for (; i < RPG_NUM_ATTACK_TYPES; i++)
            if (pWeaponShape->fpDamageDistributions[i] != 0)
                break;
        if (i < RPG_NUM_ATTACK_TYPES)
        {
            szOut.Format("damage_%d", i);
            pTxtMng->GetSystemString(szOut, szTmp);
        }
        szOut.Format("%s %d-%d %s", szStrings[DAMAGE], (int)(pWeaponShape->fpDamageMin * 10),
                     (int)((pWeaponShape->fpDamageMin + pWeaponShape->fpDamageRandom) * 10), szTmp);
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;

        szOut.Format("%s %d", szStrings[ACTIONPTS], pWeaponShape->dwActionPoints);
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;

        szOut.Format("%s %d", szStrings[MODATTACK], (int)pWeaponShape->GetAttackBonus());
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;

        if (pWeaponShape->dwTypeID == RPGWST_BOW || pWeaponShape->dwTypeID == RPGWST_CROSSBOW)
            szOut.Format("%s %d", szStrings[RANGE], (int)pWeaponShape->fpRange);
        else
            szOut.Format("%s %d", szStrings[MODDEFENSE], (int)pWeaponShape->GetDefenceBonus());
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;
    }

    // ABSORPTION
    if (nType == INSTR_ARMOR)
    {
        const SIDBArmorShape *pArmorShape = static_cast<const SIDBArmorShape *>(pShape);
        float absorbs[RPG_NUM_ATTACK_TYPES];
        for (int i = 0; i < RPG_NUM_ATTACK_TYPES; i++)
            absorbs[i] = pArmorShape->main.fpDistributions[i] * pArmorShape->main.fpAbsorbtion * 10;

        CStringArray szResArray;
        FormAbsorptionStrings(absorbs, szResArray, false);
        szOut = szStrings[ABSORPTION] + " " + szResArray[0];
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;
        /*
        if ( !szResArray[1].IsEmpty() )
        {
                szOut = szStrings[RESIST] + " " + szResArray[1];
                pSurf->DrawText( szOut, rc, 0, color, DT_LEFT );
                rc += shift;
        }
        */
        if (!szResArray[2].IsEmpty())
        {
            szOut = szStrings[VULNERABLE] + " " + szResArray[2];
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
        }
    }

    rc += shift;

    // DESCRIPTION
    pSurf->DrawMultiText(szDesc, rc, 4, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, 12);

    pSurf->ReleaseDC();
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItemInfo::FormPrototypeInfo()
{
    const auto *mod = ModRuntime::Current();
    const bool improveInfoWindows = mod && mod->CombatSetting("improveInfoWindows");
    CItem *pItem = GetItem();
    CSpell *pSpell = GetSpell();

    CTextManager *pTxtMng = theWorld.GetTextManager();

    CItemLoot *pLoot = static_cast<CItemLoot *>(pItem);
    const SSDBSpellPrototype *pProt = theWorld.GetSpellManager()->GetPrototype(
        pLoot->GetInstructionItemIndex());

    CString szName, szDesc, szType;
    pTxtMng->GetItemNameAndDesc(pItem, szName, szDesc, szType);

#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    pSurf->GetDC();

    CString szTmp, szOut;
    CRect rc(15, 10, 185, 25);
    CPoint shift(0, 12);
    COLORREF color = INV_COLOR_INFO;

    // NAME
    pSurf->DrawMultiText(szName, rc, 2, 0, INV_COLOR_HEADER, DT_CENTER | DT_WORDBREAK, TRUE, 12);
    rc += shift;
    rc += shift;
    // ITEM TYPE
    szOut.Format("%s %s", szStrings[ITEMTYPE], szType);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // MANA AND RUNES
    szOut.Format("%s %d", szStrings[MANA], (int)pProt->fpMana);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;
    szOut.Format("%s %d", szStrings[SLOTS], pProt->dwComplexity);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // SCHOOL OF MAGIC
    szTmp.Format("school_%d", pProt->msSchool);
    pTxtMng->GetSystemString(szTmp, szOut);
    szOut = szStrings[MAGICSCHOOL] + " " + szOut;
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // EFFECT
    if (!pProt->bRuneAllowed[SDBAR_EFFECT])
        szOut.Format("%s %s", szStrings[EFFECT], szStrings[FIXEDEFF]);
    else
        if (improveInfoWindows)
            szOut.Format("%s %.1f", szStrings[EFFECT], pProt->fpEffect);
        else
            szOut.Format("%s %d", szStrings[EFFECT], (int)pProt->fpEffect);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // SPEED
    if (pProt->fpSpeed < 1.f)
        szOut.Format("%s %s", szStrings[SPEED], szStrings[INSTANT]);
    else
        szOut.Format("%s %d", szStrings[SPEED], (int)pProt->fpSpeed);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // RANGE
    szOut.Format("%s %d%s", szStrings[RANGE], (int)pProt->fpRange, szStrings[METERS]);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // AREA
    if (pProt->fpArea < 1.f)
        szOut.Format("%s %s", szStrings[AREA], szStrings[AREAUNIT]);
    else
        szOut.Format("%s %.1f%s", szStrings[AREA], pProt->fpArea, szStrings[METERS]);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // DURATION
    if (pProt->dwDuration < 2)
        szOut.Format("%s %s", szStrings[DURATION], szStrings[INSTANT]);
    else
        szOut.Format("%s %.1f%s", szStrings[DURATION], pProt->dwDuration / 15.f,
                     szStrings[SECONDS]);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // TARGETS
    szOut.Format("%s %d", szStrings[TARGETS], pProt->dwNumTargets);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;
    rc += shift;

    // DESCRIPTION
    pSurf->DrawMultiText(szDesc, rc, 4, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, 12);

    pSurf->ReleaseDC();
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItemInfo::FormModifierInfo()
{
    CItem *pItem = GetItem();
    CSpell *pSpell = GetSpell();

    CTextManager *pTxtMng = theWorld.GetTextManager();
    CString szName, szDesc, szType;
    pTxtMng->GetItemNameAndDesc(pItem, szName, szDesc, szType);

    CItemLoot *pLoot = static_cast<CItemLoot *>(pItem);
    const SSDBModifier *pMod = theWorld.GetSpellManager()->GetModifier(
        pLoot->GetInstructionItemIndex());

#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    pSurf->GetDC();

    CString szOut;
    CRect rc(15, 10, 185, 25);
    CPoint shift(0, 12);
    COLORREF color = INV_COLOR_INFO;

    // NAME
    pSurf->DrawMultiText(szName, rc, 2, 0, INV_COLOR_HEADER, DT_CENTER | DT_WORDBREAK, TRUE, 12);
    rc += shift;
    rc += shift;
    // ITEM TYPE
    szOut.Format("%s %s", szStrings[ITEMTYPE], szType);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // MANA COST
    szOut.Format("%s %d", szStrings[MANA], (int)pMod->fpMana);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;

    // COMPLEXITY
    szOut.Format("%s %d", szStrings[SLOTS], pMod->dwComplexity);
    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
    rc += shift;
    rc += shift;

    // DESCRIPTION
    pSurf->DrawMultiText(szDesc, rc, 8, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, 12);

    pSurf->ReleaseDC();
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItemInfo::FormAbsorptionStrings(float *fValues, CStringArray &szResArray,
                                               bool bRound)
{
    CTextManager *pTxtMng = theWorld.GetTextManager();
    szResArray.RemoveAll();

    CString szDefault, szResist, szVulnerable, szTmp, szDamage;
    float fDefault = fValues[RPGAT_GENERAL];
    if (bRound)
        szDefault.Format("%d", (int)fDefault);
    else
        szDefault.Format("%.1f", fDefault);

    for (int i = 0; i < RPG_NUM_ATTACK_TYPES - 1; i++)
        if (fValues[i] > fDefault)
        {
            szTmp.Format("dmg_%d", i);
            pTxtMng->GetSystemString(szTmp, szDamage);
            szResist += szDamage;
            szResist += ", ";
        }
        else if (fValues[i] < fDefault)
        {
            szTmp.Format("dmg_%d", i);
            pTxtMng->GetSystemString(szTmp, szDamage);
            szVulnerable += szDamage;
            szVulnerable += ", ";
        }

    szResist.TrimRight(", ");
    szVulnerable.TrimRight(", ");

    szResArray.Add(szDefault);
    szResArray.Add(szResist);
    szResArray.Add(szVulnerable);
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItemInfo::SetSkill(SCampSkillMouseInfo *pSkillInfo)
{
    SetItem(0);
    if (pSkillInfo->nType < 0)
        return;

#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    pSurf->GetDC();

    CTextManager *pTxtMng = theWorld.GetTextManager();

    CString szTmp, szOut;
    CRect rc(15, 10, 185, 25);
    CPoint shift(0, 12);
    COLORREF color = INV_COLOR_INFO;

    // NAME
    CString szName, szDesc;

    switch (pSkillInfo->nType)
    {
    case 0:
    case 1:
    case 2:
    case 3:
        if (pTxtMng->GetText(CString("perk ") + pSkillInfo->szNonlocalName))
        {
            pTxtMng->GetLine(szName);
            pTxtMng->GetLine(szDesc);
        }
        break;
    case 4:
    case 5:
    case 6:
    case 7:
        pTxtMng->GetSystemString(pSkillInfo->szNonlocalName, szName);
        pTxtMng->GetSystemString(pSkillInfo->szNonlocalName + "_desc", szDesc);
        break;
    }

    pSurf->DrawMultiText(szName, rc, 2, 0, INV_COLOR_HEADER, DT_CENTER | DT_WORDBREAK, TRUE, 12);
    rc += shift;
    rc += shift;

    if (pSkillInfo->nType == 0)
    {
        switch (pSkillInfo->nSkill)
        {
        case 0: // str
            szOut.Format("%s %d%%", szSkillStr[HEALTHMULT],
                         int(pSkillInfo->pStats->GetHealthMultiplier() * 100));
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
            szOut.Format("%s %d", szSkillStr[ENCUMBRANCE],
                         int(pSkillInfo->pStats->GetMaxEncumbrance()));
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
            if (pSkillInfo->pUnit)
            {
                CUnit *serverUnit = theWorldServer.GetMapObjectManager()
                    ? theWorldServer.GetUnit(pSkillInfo->pUnit->GetID()) : nullptr;
                CUnitServer *regenServer = serverUnit && serverUnit->WhoAmI() == HPC_UNIT
                                               ? dynamic_cast<CUnitServer *>(serverUnit->GetUnitSpecific())
                                               : nullptr;
                CMapObjectServer *commonServer = regenServer ? regenServer->GetMapObjectServer() : nullptr;
                const SUDBRaceModel *race = pSkillInfo->pUnit->GetRaceModel();
                if (race && ModRuntime::Current())
                {
                    const bool accurate = ModRuntime::Current()->CombatSetting("JMVRegenerationTooltip") != 0;
                    CUnit *regenUnit = accurate && regenServer && commonServer ? serverUnit : pSkillInfo->pUnit;
                    const int magicBloodLevel = accurate
                        ? min(3, int(regenUnit->GetUserUnitStats()->cPerks[27]))
                        : thePerkDatabase.GetPerkIndex(thePerkDatabase.GetPerk(27));
                    const float magicBloodBonus =
                        float(ModRuntime::Current()->InfoMagicBlood(magicBloodLevel));
                    float jewelryRegeneration = 0.0f;
                    CPersonage *regenPerson = pScreen->GetCurrentPers();
                    for (int i = 0; regenPerson && i < regenPerson->items.GetSize(); ++i)
                    {
                        CItem *item = regenPerson->items[i];
                        if (item && item->GetShapeEntry())
                            jewelryRegeneration +=
                                ModInfoProperty("GetJewelryPropsInside", item->GetShapeEntry()->szName, 14) * 0.01f;
                    }
                    float armorRegeneration = 0.0f;
                    if (accurate)
                    {
                        for (int i = 0; i < RPG_NUM_ARMOR_TYPES; ++i)
                            if (CItemArmor *armor = regenUnit->GetQArmor(i))
                                armorRegeneration += ModInfoProperty("GetMaterialPropsInside", ModInfoMaterialName(armor), 2) * 0.01f;
                    }
                    else if (regenPerson)
                    {
                        for (int i = 0; i < regenPerson->armors.GetSize(); ++i)
                            if (CItemArmor *armor = regenPerson->armors[i])
                                armorRegeneration += ModInfoProperty("GetMaterialPropsInside", ModInfoMaterialName(armor), 2) * 0.01f;
                    }
                    const float complexRegeneration = accurate
                        ? ModRuntime::Current()->InfoComplex(*regenUnit, 10) * 0.01f : 0.0f;
                    const float hpRegeneration = race->fpRegenerateHP *
                        (magicBloodBonus + jewelryRegeneration + armorRegeneration + complexRegeneration);
                    float result, fullResult;
                    if (accurate)
                    {
                        const float maxHp = max(1.0f, float(regenUnit->GetMaxHP()));
                        const float regenerationSpell = hpRegeneration > 0.0f
                            ? regenUnit->GetEffectValue(SPELL_ID_REGENERATION) / 30.0f : 0.0f;
                        result = maxHp * hpRegeneration * float(2 + theSettings.nDifficultyLevel) + regenerationSpell;
                        fullResult = result > 0.000001f ? maxHp / result : 0.0f;
                    }
                    else
                    {
                        const float health = 1 + pSkillInfo->pStats->GetPerkModifier(2) * 0.01f;
                        const float hp = 30.0f * powf(1.10f, logf(pSkillInfo->pStats->fpExperience / 500.0f) / logf(1.61f));
                        const float hpResult = (hp * (pSkillInfo->pUnit->GetUserUnitStats()->cStrength / 25.0f) - 5) * health;
                        result = hpResult * hpRegeneration;
                        fullResult = hpResult / result;
                    }

                    CString szTmp2;
                    if (pTxtMng->GetText("string infoskill_50")) pTxtMng->GetLine(szTmp);
                    szOut.Format("%s", szTmp);
                    pSurf->DrawText(szOut, rc, 0, color, DT_CENTER);
                    rc += shift;
                    if (pTxtMng->GetText("string infoskill_45")) pTxtMng->GetLine(szTmp);
                    szOut.Format("%s %.4f/100", szTmp, hpRegeneration * 100.0f);
                    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
                    rc += shift;
                    if (pTxtMng->GetText("string infoskill_46")) pTxtMng->GetLine(szTmp);
                    if (pTxtMng->GetText("string infoskill_47")) pTxtMng->GetLine(szTmp2);
                    szOut.Format("%s %.2f%s", szTmp, result, szTmp2);
                    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
                    rc += shift;
                    if (pTxtMng->GetText("string infoskill_51")) pTxtMng->GetLine(szTmp);
                    if (pTxtMng->GetText("string infoskill_49")) pTxtMng->GetLine(szTmp2);
                    szOut.Format("%s %.1f%s", szTmp, fullResult, szTmp2);
                    pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
                    rc += shift;
                }
            }
            rc += shift;
            break;
        case 1: // dex
            szOut.Format("%s %d%%", szSkillStr[STAMINAMULT],
                         int(pSkillInfo->pStats->GetStaminaMultiplier() * 100));
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
            szOut.Format("%s %d", szSkillStr[ACTIONS], int(pSkillInfo->pStats->GetActionPoints()));
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
            rc += shift;
            break;
        case 2: // int
            szOut.Format("%s %d%%", szSkillStr[BONUSCP],
                         int(pSkillInfo->pStats->GetExpMultiplier() * 100));
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
            szOut.Format("%s %d%%", szSkillStr[BONUSMANA],
                         int(pSkillInfo->pStats->GetManaBonusMultiplier() * 100));
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
            rc += shift;
            break;
        }
        // DESCRIPTION
        pSurf->DrawMultiText(szDesc, rc, 13, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, 12);
    }

    if (pSkillInfo->nType == 1)
    {
        ERPGAutoSkills eSkill = (ERPGAutoSkills)pSkillInfo->nSkill;
        int nValue = pSkillInfo->pStats->GetBaseAutoSkill(eSkill);
        szOut.Format("%s %d", szSkillStr[SKILL2], nValue);
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;
        switch (eSkill)
        {
        case RPGASKL_MELEE:
        case RPGASKL_ARCHERY:
            szOut.Format("%s %d", szSkillStr[BASETOHIT],
                         int(pSkillInfo->pStats->GetAutoSkill(eSkill)));
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
            break;
        case RPGASKL_BACKSTAB:
            szOut.Format("%s %d%%", szSkillStr[DAMAGEMULT],
                         int(pSkillInfo->pStats->GetBaseBackstabDamageMultiplier() * 100));
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
            break;
            /*
            case RPGASKL_ELEMENTAL:
            case RPGASKL_SENSE:
            case RPGASKL_ASTRAL:
                    szOut.Format( "%s %d", szSkillStr[SPELLCOMPLEX],
            int(pSkillInfo->pStats->GetAutoSkill( eSkill )) ); pSurf->DrawText( szOut, rc, 0, color,
            DT_LEFT ); rc += shift; break;
            */
        }
        rc += shift;
        // DESCRIPTION
        pSurf->DrawMultiText(szDesc, rc, 13, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, 12);
    }

    if (pSkillInfo->nType == 2)
    {
        ERPGManualSkills eSkill = (ERPGManualSkills)pSkillInfo->nSkill;
        int nValue = pSkillInfo->pStats->GetBaseManualSkill(eSkill);
        szOut.Format("%s %d", szSkillStr[SKILL2], nValue);
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;
        switch (eSkill)
        {
        case RPGMSKL_SCIENCE:
            szOut.Format("%s %d", szSkillStr[USECOMPLEX],
                         int(pSkillInfo->pStats->GetManualSkill(eSkill)));
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
            break;
        }
        rc += shift;
        // DESCRIPTION
        pSurf->DrawMultiText(szDesc, rc, 13, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, 12);
    }

    if (pSkillInfo->nType == 3)
    {
        CString szPerkName(pSkillInfo->szNonlocalName);
        szPerkName.TrimRight("0");
        szPerkName += "1";
        int nPerk = thePerkDatabase.GetPerkIndex(szPerkName);
        int nPerkGroup = nPerk / 3;

        for (int j = 0; j < 3; j++)
        {
            const SPDBPerk *pPerk = thePerkDatabase.GetPerk(nPerkGroup * 3 + j);
            if (!pPerk)
                continue;
            if (pTxtMng->GetText(CString("perk ") + pPerk->szCode))
                pTxtMng->GetLine(szName);
            pSurf->DrawText(szName, rc, 0, color, DT_LEFT);
            if (pSkillInfo->nSkill > j)
                pSurf->DrawText(szSkillStr[PERKTAKEN], rc, 0, color, DT_RIGHT);
            rc += shift;

            // EFFECT
            switch (nPerkGroup)
            {
            case RPGPRK_SWORD:
            case RPGPRK_AXE:
            case RPGPRK_DAGGER:
            case RPGPRK_SPEAR:
            case RPGPRK_HAMMER:
            case RPGPRK_BOW:
            case RPGPRK_CROSSBOW:
                szOut.Format("     %s %d", szSkillStr[TOHITBONUS], pPerk->dwModifier);
                pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
                break;
            case RPGPRK_FIRE:
            case RPGPRK_LIGHTNING:
            case RPGPRK_ACID:
            case RPGPRK_ILLUSION:
            case RPGPRK_DIVINATION:
            case RPGPRK_ENCHANTMENTS:
            case RPGPRK_HEALING:
            case RPGPRK_DOMINATION:
                szOut.Format("     %s %d", szSkillStr[SPELLBONUS], pPerk->dwModifier);
                pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
                break;
            case RPGPRK_INFRA:
                szOut.Format("     %s %d%%", szSkillStr[NIGHTVISION],
                             (pPerk->dwModifier + 100) / 2);
                pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
                break;
            /*
            case RPGPRK_STRENGTH:
            case RPGPRK_DEXTERITY:
            case RPGPRK_INTELLIGENCE:
                    szOut.Format( "     %s %d", szSkillStr[ATTRBONUS], pPerk->dwModifier );
                    pSurf->DrawText( szOut, rc, 0, color, DT_LEFT );
                    break;
            */
            case RPGPRK_HEALTH:
                szOut.Format("     %s %d%%", szSkillStr[HEALTH], pPerk->dwModifier + 100);
                pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
                break;
            case RPGPRK_MANA:
                szOut.Format("     %s %d%%", szSkillStr[STAMINA], pPerk->dwModifier + 100);
                pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
                break;
            case RPGPRK_VITALITY:
                szOut.Format("     %s %d%%", szSkillStr[HEALTHRGNR], pPerk->dwModifier + 100);
                pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
                break;
            case RPGPRK_SPIRIT:
                szOut.Format("     %s %d%%", szSkillStr[STAMINARGNR], pPerk->dwModifier + 100);
                pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
                break;
            case RPGPRK_QUICKNESS:
                szOut.Format("     %s %d%%", szSkillStr[ACTIONS], pPerk->dwModifier + 100);
                pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
                break;
            case RPGPRK_LIFT:
                szOut.Format("     %s %d%%", szSkillStr[ENCUMBRANCE], pPerk->dwModifier + 100);
                pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
                break;
            case RPGPRK_BACKSTAB:
                szOut.Format("     %s %d%%", szSkillStr[BSTABMULT], pPerk->dwModifier + 300);
                pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
                break;
            case RPGPRK_STRENGTH:
            case RPGPRK_DEXTERITY:
            case RPGPRK_INTELLIGENCE:
                szOut.Format("     %s %d", szSkillStr[ATTRIBPLUS], pPerk->dwModifier);
                pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
                break;
            }
            rc += shift;

            // COST
            rc.right += 15;
            int nPerkPrice = pSkillInfo->pStats->GetPerkPrice(pPerk);
            if (nPerkPrice < 0)
                nPerkPrice = INT_MAX;
            szOut.Format("     %s %d", szSkillStr[COST], nPerkPrice);
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc.right -= 15;
            rc += shift;
        }
        /*
                        // REQUIRES
                        int nVal = 0;
                        szTmp = "none";
                        if ( pPerk->fStrength > 0 )
                        {
                                szTmp = "Strength";
                                nVal = (int)pPerk->fStrength;
                        }
                        else if ( pPerk->fDexterity > 0 )
                        {
                                szTmp = "Dexterity";
                                nVal = (int)pPerk->fDexterity;
                        }
                        else if ( pPerk->fIntelligence > 0 )
                        {
                                szTmp = "Intelligence";
                                nVal = (int)pPerk->fIntelligence;
                        }
                        if ( pTxtMng->GetText( CString("perk ") + szTmp ) )
                                pTxtMng->GetLine( szTmp );
                        szOut.Format( "     %s %d", szTmp, nVal );
                        pSurf->DrawText( szSkillStr[REQUIRES], rc, 0, color, DT_LEFT );
                        rc += shift;
                        pSurf->DrawText( szOut, rc, 0, color, DT_LEFT );
                        rc += shift;

                        if ( !pPerk->szSkillRequired.IsEmpty() && pPerk->szSkillRequired != "none" )
                        {
                                CString szOut2;
                                szTmp = "Unknown";
                                if ( pTxtMng->GetText( CString("perk ") + pPerk->szSkillRequired ) )
                                        pTxtMng->GetLine( szTmp );
                                szOut2.Format( "     %s %d", szTmp, pPerk->dwSkillLevel );
                                pSurf->DrawText( szOut2, rc, 0, color, DT_LEFT );
                                rc += shift;
                        }

                        // DEPENDENT
                        if ( !pPerk->szPerkRequired.IsEmpty() && pPerk->szPerkRequired != "none" )
                        {
                                pSurf->DrawText( szSkillStr[DEPENDENT], rc, 0, color, DT_LEFT );
                                rc += shift;
                                szTmp = "Unknown";
                                if ( pTxtMng->GetText( CString("perk ") + pPerk->szPerkRequired ) )
                                        pTxtMng->GetLine( szTmp );
                                szOut.Format( "     %s", szTmp );
                                pSurf->DrawText( szOut, rc, 0, color, DT_LEFT );
                                rc += shift;
                        }
        */
        rc += shift;
        // DESCRIPTION
        pSurf->DrawMultiText(szDesc, rc, 6, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, 12);
    }

    if (pSkillInfo->nType == 4)
    {
        pSurf->DrawText(szStrings[ABSORPARMOR], rc, 0, color, DT_LEFT);
        rc += shift;

        float fpAbsorbtions[RPG_NUM_ATTACK_TYPES];
        for (int i = 0; i < RPG_NUM_ATTACK_TYPES; i++)
        {
            fpAbsorbtions[i] = 0;
            for (int j = 0; j < RPG_NUM_ARMOR_LAYERS; j++)
                fpAbsorbtions[i] +=
                    pSkillInfo->pStats->hls[pSkillInfo->eHL].layers[j].fpAbsorbtions[i];
        }
        for (int i = 0; i < RPG_NUM_ATTACK_TYPES - 1; i++)
        {
            szOut.Format("damage_%d", i);
            pTxtMng->GetSystemString(szOut, szTmp);

            szOut.Format("     %s:", szTmp);
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            szOut.Format("%.1f", fpAbsorbtions[i]);
            pSurf->DrawText(szOut, rc - CPoint(10, 0), 0, color, DT_RIGHT);
            rc += shift;
        }
        rc += shift;
        // DESCRIPTION
        pSurf->DrawMultiText(szDesc, rc, 8, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, 12);
    }

    if (pSkillInfo->nType == 5)
    {
        ERPGWeaponTypes type = pSkillInfo->pWeapon->GetWeaponType();
        if (pSkillInfo->szNonlocalName == "defense" &&
            (type == RPGWST_BOW || type == RPGWST_CROSSBOW))
        {
            pSurf->DrawMultiText(szStrings[WSLBOWDEFENSE], rc, 2, 0, color,
                                 DT_CENTER | DT_WORDBREAK, TRUE, 12);
            rc += shift;
        }
        else
        {
            int nSum = 0;
            int nVal;
            szOut.Format("     %s", szStrings[WSLDEXT]);
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            szOut.Format("%d", nVal = (int)pSkillInfo->pStats->GetWSLDexterityBonus());
            pSurf->DrawText(szOut, rc - CPoint(10, 0), 0, color, DT_RIGHT);
            nSum += nVal;
            rc += shift;
            szOut.Format("     %s", szStrings[WSLSKILL]);
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            szOut.Format("%d", nVal = (int)pSkillInfo->pStats->GetWSLBaseSkill(type));
            pSurf->DrawText(szOut, rc - CPoint(10, 0), 0, color, DT_RIGHT);
            nSum += nVal;
            rc += shift;
            szOut.Format("     %s", szStrings[WSLPERK]);
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            szOut.Format("%d", nVal = (int)pSkillInfo->pStats->GetWSLPerkValue(type));
            pSurf->DrawText(szOut, rc - CPoint(10, 0), 0, color, DT_RIGHT);
            nSum += nVal;
            rc += shift;
            szOut.Format("     %s", szStrings[WSLWEAPON]);
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            nVal = (int)pSkillInfo->pStats->GetWSLWeaponModifier(
                pSkillInfo->pWeapon->GetSubtypeID());
            if (pSkillInfo->szNonlocalName == "defense")
                nVal += (int)pSkillInfo->pWeapon->GetDefenceBonus();
            else
                nVal += (int)pSkillInfo->pWeapon->GetAtackBonus();
            szOut.Format("%d", nVal);
            pSurf->DrawText(szOut, rc - CPoint(10, 0), 0, color, DT_RIGHT);
            nSum += nVal;
            rc += shift;
            rc += shift;
            szOut.Format("     %s", szStrings[WSLSUM]);
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            szOut.Format("%d", nSum);
            pSurf->DrawText(szOut, rc - CPoint(10, 0), 0, color, DT_RIGHT);
        }
        rc += shift;
        rc += shift;
        // DESCRIPTION
        pSurf->DrawMultiText(szDesc, rc, 9, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, 12);
    }

    if (pSkillInfo->nType == 6)
    {
        int nStr = MSLFIRE;
        // Addon has three elemental subtypes; retain native storage for save compatibility.
        const int subtypeCount = ModRuntime::Current() ? 3 : RPG_NUM_MAGIC_SUBTYPES;
        for (int i = 0; i < subtypeCount; i++)
        {
            ERPGMagicSubtypes subtype = (ERPGMagicSubtypes)i;
            if (subtype == RPGMST_DIVINATION || subtype == RPGMST_HEALING ||
                subtype == RPGMST_DOMINATION)
                continue;
            szOut.Format("     %s", szStrings[nStr++]);
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            float knowledge = pSkillInfo->pStats->GetMagicSkill(subtype);
            if (ModRuntime::Current() && pSkillInfo->pUnit)
                knowledge = pSkillInfo->pStats->GetBaseMagicSkill(subtype) +
                    pSkillInfo->pStats->GetPerkModifier(RPGPRK_FIRE + subtype) +
                    int(pSkillInfo->pUnit->GetUserUnitStats()->cIntelligence) - 25;
            szOut.Format("%d", (int)knowledge);
            pSurf->DrawText(szOut, rc - CPoint(10, 0), 0, color, DT_RIGHT);
            rc += shift;
        }
        rc += shift;
        // DESCRIPTION
        pSurf->DrawMultiText(szDesc, rc, 9, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, 12);
    }

    if (pSkillInfo->nType == 7)
        pSurf->DrawMultiText(szDesc, rc, 16, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, 12);

    pSurf->ReleaseDC();
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItemInfo::SetHelp(int nHelpID)
{
    SetItem(0);

#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    pSurf->GetDC();

    CTextManager *pTxtMng = theWorld.GetTextManager();

    CRect rc(15, 10, 185, 25);
    CPoint shift(0, 12);
    COLORREF color = INV_COLOR_INFO;

    // NAME
    CString szName, szDesc, szTmp;
    szTmp.Format("camphelp %d", nHelpID);
    if (pTxtMng->GetText(szTmp))
    {
        pTxtMng->GetLine(szName);
        pTxtMng->GetLine(szDesc);
    }
    // szName.Format( "Help #%d", nHelpID );
    pSurf->DrawMultiText(szName, rc, 2, 0, INV_COLOR_HEADER, DT_CENTER | DT_WORDBREAK, TRUE, 12);
    rc += shift;
    rc += shift;

    // DESCRIPTION
    pSurf->DrawMultiText(szDesc, rc, 16, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, 12);

    pSurf->ReleaseDC();
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectCampBG::Init()
{
    CI3DFigure *pFig;

    // background
    pFig = AddFigure();
    pFig->MakeTilingBar(8, 100, 0, 100, 800, 500, 14, 14, 114, 114, 8, 4);

    // all that frames
    for (int i = 0; i < 4; i++)
    {
        pFig = AddFigure();
        pFig->MakeTilingBar(7.9f, 100, i * 200, 100, i * 200 + 21.f, 500, 254, 76, 233, 176, 1, 4);
        pFig = AddFigure();
        pFig->MakeTilingBar(7.9f, 100, i * 200 + 179.f, 100, (i + 1) * 200, 500, 233, 76, 254, 176,
                            1, 4);

        pFig = AddFigure();
        pFig->MakeBar(7.8f, 100, i * 200, 100, i * 200 + 100.f, 130, 254, 2, 154, 32);
        pFig = AddFigure();
        pFig->MakeBar(7.8f, 100, i * 200, 470, i * 200 + 100.f, 500, 254, 32, 154, 2);

        pFig = AddFigure();
        pFig->MakeBar(7.8f, 100, i * 200 + 100, 100, (i + 1) * 200, 130, 154, 2, 254, 32);
        pFig = AddFigure();
        pFig->MakeBar(7.8f, 100, i * 200 + 100, 470, (i + 1) * 200, 500, 154, 32, 254, 2);
    }

    pFig = AddFigure();
    pFig->MakeBar(7.8f, 100, 0, 380, 100, 420, 254, 34, 154, 74);
    pFig = AddFigure();
    pFig->MakeBar(7.8f, 100, 100, 380, 200, 420, 154, 34, 254, 74);
    pFig = AddFigure();
    pFig->MakeBar(7.8f, 100, 600, 380, 700, 420, 254, 34, 154, 74);
    pFig = AddFigure();
    pFig->MakeBar(7.8f, 100, 700, 380, 800, 420, 154, 34, 254, 74);

    pFig = AddFigure();
    pFig->MakeBar(7.8f, 100, 200, 280, 300, 320, 254, 34, 154, 74);
    pFig = AddFigure();
    pFig->MakeBar(7.8f, 100, 300, 280, 400, 320, 154, 34, 254, 74);
    /*
    pFig = AddFigure();
    pFig->MakeBar( 7.8f, 100, 400, 440, 500, 480, 254, 34, 154, 74 );
    pFig = AddFigure();
    pFig->MakeBar( 7.8f, 100, 500, 440, 600, 480, 154, 34, 254, 74 );

    pFig = AddFigure();
    pFig->MakeBar( 7, 100, 52, 436, 100, 484, 206, 206, 254, 254 );
    pFig = AddFigure();
    pFig->MakeBar( 7, 100, 100, 436, 148, 484, 254, 206, 206, 254 );
    */
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectCampBG::SetHeight(int nHeight)
{
    figures[25]->MakeBar(7.8f, 100, 0, nHeight - 20, 100, nHeight + 20, 254, 34, 154, 74);
    figures[26]->MakeBar(7.8f, 100, 100, nHeight - 20, 200, nHeight + 20, 154, 34, 254, 74);
}
/////////////////////////////////////////////////////////////////////////////////////
