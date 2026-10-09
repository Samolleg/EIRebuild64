#include "StdAfx.h"
#include "GameJournal.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DMain.h"
#include "I3DInventory.h"
#include "TextManager.h"
#include "FigureManager.h"
#include "TextureManager.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "ModItemUiPolicy.h"
#include "ModCampStats.h"
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
#include "I3DScreenCamp.h"
#include "ScenarioTracker.h"
#include "ModCampSpellInfo.h"
#include "ModDescriptionFont.h"
#include "ModItemInfo.h"
#include "ModItemDetails.h"
#include "SpellDatabase.h"
static void ModInfoAbsorptionStrings(const float *values, CStringArray &rows, bool round)
{
    CString labels[RPG_NUM_ATTACK_TYPES - 1];
    for (int i = 0; i < RPG_NUM_ATTACK_TYPES - 1; ++i) {
        CString key; key.Format("dmg_%i", i);
        theWorld.GetTextManager()->GetSystemString(key, labels[i]);
    }
    ModItemInfo::Absorptions(values, labels, rows, round);
}
static int ModInfoMultiText(CI3DSurface *surface, LPCTSTR text, CRect rect,
    int lines, int font, COLORREF color, int format)
{
    auto *mod = ModRuntime::Current();
    return surface->DrawMultiText(text, rect, lines, font, color, format, TRUE,
        mod && mod->AdvancedUI("useLS_FormTextInfo") && mod->CombatSetting("JMVItemInfoLineHeight") ? 12 : 0);
}
static int ModInfoDescription(CInterface3D *ui, CI3DSurface *surface, LPCTSTR text,
    CRect rect, int lines, int font, COLORREF color, int format)
{
    ModDescriptionFont smaller(ui->GetFontSlot(font), ui->GetHeight());
    return ModInfoMultiText(surface, text, rect, lines, font, color, format);
}
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
namespace {
struct InventoryAtlasRecipe
{
    CString resourceName;
    int atlasSize, sourceWidth, sourceHeight, dstY;
    bool hasSource;
    std::unique_ptr<WORD[]> uploadedPixels;
};
std::map<CTexture *, InventoryAtlasRecipe> &InventoryAtlasRecipes()
{
    // Non-owning keys; cache allocator destruction removes each matching recipe.
    static auto *recipes = new std::map<CTexture *, InventoryAtlasRecipe>;
    return *recipes;
}
}
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
    CSmartPtr<CTexture> creating(pTex);
    const int dstY = atlasSize - nSrcSizeY;
    InventoryAtlasRecipe recipe;
    recipe.resourceName = resourceName;
    recipe.atlasSize = atlasSize;
    recipe.sourceWidth = nSrcSizeX;
    recipe.sourceHeight = nSrcSizeY;
    recipe.dstY = dstY;
    recipe.hasSource = hasSource;
    if (hasSource && !PasteTexture(pTex, resourceName, nSrcSizeX, nSrcSizeY,
                                  nSrcSizeX, nSrcSizeY, 0, dstY, 0, 0,
                                  &recipe.uploadedPixels))
        return 0;
    InventoryAtlasRecipes().emplace(pTex, std::move(recipe));
    pTex->AddRef(); // Transfer the existing cache reference after publication.
    return pTex;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CTextureLRUAllocator::EnsureReady(CTexture *pTex)
{
    if (!pTex) return false;
    if (pTex->GetSurface()) return true;
    auto &recipes = InventoryAtlasRecipes();
    auto found = recipes.find(pTex);
    if (found == recipes.end()) return false;
    const auto &recipe = found->second;
    if (recipe.hasSource && !recipe.uploadedPixels) return false;
    struct RestoreAtlas
    {
        CTexture *texture;
        bool ready = false;
        ~RestoreAtlas() { if (!ready) texture->Unload(); }
    } restoring{pTex};
    SMMPFileHeader hdr;
    hdr.InitializeDefaultFormat(MMP_FORMAT_5551);
    hdr.dwSignature = MMP_SIGNATURE;
    hdr.dwDimentionX = recipe.atlasSize;
    hdr.dwDimentionY = recipe.atlasSize;
    hdr.dwMipMapLevel = 1;
    if (!pTex->Create(&theRenderer, &hdr)) return false;
    if (recipe.hasSource)
    {
        D3D11_BOX region = {0};
        region.right = recipe.sourceWidth;
        region.top = recipe.dstY;
        region.bottom = recipe.dstY + recipe.sourceHeight;
        region.back = 1;
        D3D11_SUBRESOURCE_DATA data = {0};
        data.pSysMem = recipe.uploadedPixels.get();
        data.SysMemPitch = 2 * recipe.sourceWidth;
        pTex->GetSurface()->UpdateTexture(0, &region, &data);
    }
    restoring.ready = true;
    return true;
}
void CTextureLRUAllocator::DestroyObject(CTexture *pTex)
{
    InventoryAtlasRecipes().erase(pTex);
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
    if (!pFig || !CTextureLRUAllocator::EnsureReady(pTex))
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
    if (!pFig || !CTextureLRUAllocator::EnsureReady(pTex))
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
    int nSlotType = 0;
    CItem *pItem = GetMouseItem(point, nSlotType);
    if(pItem || nSlotType) GameJournal::Event("INPUT", "inventory", "element=%p class=%s part=%d part_name=%s slot=%d item=%d type=%u count=%d", this, GameJournal::Quote(typeid(*this).name()).c_str(), int(ePartType), GameJournal::ItemPart(int(ePartType)), nSlotType, pItem?pItem->GetID():-1, pItem?unsigned(pItem->GetSubtypeID()):0, pItem?pItem->GetCount():0);
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
        const auto* iconMod=ModRuntime::Current();
        const auto icon=ModItemUiPolicy::ItemIcon(iconMod && iconMod->AdvancedUI("useLS_ObjItemInfoDraw"));
        item.fZ = icon.z;
        item.fPixSize = icon.size;
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
    case VK_MENU:
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
    case VK_MENU:
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
    if (auto *mod = ModRuntime::Current();mod && mod->AdvancedUI("useLS_FormTextInfo")) {

	CItem* pItem = GetItem();
	CSpell* pSpelll = GetSpell();
	CString szTmp, szTmp2, szOut;
#ifndef NOGETDC
	surfaces[0]->GetDC();
	surfaces[0]->Clear();
	surfaces[0]->ReleaseDC();
#endif 
	if (!pItem)
		return;
	CTextManager* pTxtMng = theWorld.GetTextManager();
	CI3DSurface* pSurf = surfaces[0];
	CRect rc(15, 10, 185, 25);
	CPoint shift(0, mod->CombatSetting("JMVItemInfoLineHeight") ? 12 : 14);
	COLORREF color = INV_COLOR_INFO;
	COLORREF color2 = RGB(255, 242, 194);
	if (pScreen->IsItemMaterial(pItem))
	{
		FormMaterialInfo();
#ifndef NOGETDC
		pSurf->GetDC();
		CItemLoot* pLoot = static_cast<CItemLoot*>(pItem);
		const SIDBMaterial* pMat = theItemDatabase.materials.GetEntry(pLoot->GetMaterialIndex());
		CI3DScreenCamp* camp = dynamic_cast<CI3DScreenCamp*>(pScreen);
		int nShop = 0;
		if (camp && camp->GetCurrentShop()) {
			nShop = camp->GetCurrentShop()->nID;
			if (mod->InfoForbiddenSpell(pMat->szCodeName.GetString(), nShop)) {
				if (pTxtMng->GetText("string infoitem_91")) pTxtMng->GetLine(szTmp);
				szOut.Format("%s", szTmp);
				pSurf->DrawText(szOut.GetString(), rc, 0, RGB(255, 200, 0), DT_LEFT);
				rc += shift;
			}
		}
		pSurf->ReleaseDC();
#endif 
		return;
	}
	if (pScreen->IsItemInstruction(pItem))
	{
		FormInstructionInfo();
#ifndef NOGETDC
		pSurf->GetDC();
		const SIDBItemGeneralShape* pProt = pItem->GetShapeEntry();
		CI3DScreenCamp* camp = dynamic_cast<CI3DScreenCamp*>(pScreen);
		int nShop = 0;
		if (camp && camp->GetCurrentShop()) {
			nShop = camp->GetCurrentShop()->nID;
			if (mod->InfoForbiddenSpell(pProt->szName.GetString(), nShop)) {
				if (pTxtMng->GetText("string infoitem_91")) pTxtMng->GetLine(szTmp);
				szOut.Format("%s", szTmp);
				pSurf->DrawText(szOut.GetString(), rc, 0, RGB(255, 200, 0), DT_LEFT);
				rc += shift;
			}
		}
		pSurf->ReleaseDC();
#endif 
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
#ifndef NOGETDC
		pSurf->GetDC();
		CI3DScreenCamp* camp = dynamic_cast<CI3DScreenCamp*>(pScreen);
		int nShop = 0;
		if (camp && camp->GetCurrentShop()) {
			nShop = camp->GetCurrentShop()->nID;
			CItemLoot* pModLoot = static_cast<CItemLoot*>(pItem);
			const SSDBModifier* pModifier = theSpellDatabase.runes.GetEntry(pModLoot->GetInstructionItemIndex());
			if (mod->InfoForbiddenSpell(pModifier->GetCodeName(), nShop)) {
				CPoint shift2(0, 12);
				rc += shift2;
				rc += shift2;
				rc += shift2;
				rc += shift2;
				rc += shift2;
				rc += shift2;
				if (pTxtMng->GetText("string infoitem_91")) pTxtMng->GetLine(szTmp);
				szOut.Format("%s", szTmp);
				pSurf->DrawText(szOut.GetString(), rc, 0, RGB(255, 200, 0), DT_LEFT);
				rc += shift;
			}
		}
		pSurf->ReleaseDC();
#endif 
		return;
	}
	ETypeIDs tid = pItem->GetTypeID();
	if (tid == ITID_ITEM_QUICK &&
		static_cast<CItemQuick*>(pItem)->GetQuickItemType() == IDB_SCROLL)
	{
		FormPotionInfo();
		return;
	}
	if ((pI3D->IsCtrlDown() && pSpelll) || pItem->GetTypeID() == ITID_ITEM_SPELL)
	{
		FormSpellInfo();
		return;
	}
	CString szMaterial;
	szMaterial = theItemDatabase.materials.GetEntry(pItem->GetMaterial())->szName;
	NStrProc::ReplaceChars(szMaterial, ' ', '_');
	szMaterial.MakeLower();
	CString szName, szDesc, szType;
	pTxtMng->GetItemNameAndDesc(pItem, szName, szDesc, szType);
#ifndef NOGETDC
	pSurf->GetDC();
	int typeID = pItem->GetShapeEntry()->dwTypeID;
	bool deconstructable = pItem->GetShapeEntry()->bDeconstructable;
	CString itemName = pItem->GetShapeEntry()->szName;
	int complexNum = mod->InfoProperty("GetSetItemStats", itemName.GetString(), 2);
	int complexSize = mod->InfoComplexProperty(complexNum, 3);
	int currentComplexSize = 0;
	if (mod->InfoProperty("GetSetItemStats", itemName.GetString(), 2)) {
		CI3DScreenCamp* currentCamp = dynamic_cast<CI3DScreenCamp*>(pScreen);
        CUnit* pUnit = currentCamp ? currentCamp->GetCurrentUnit() : nullptr;
		if (pUnit) {
			for (int i = 0; i < 7; i++) {
				if (pUnit->GetQArmor(i)) {
					CString checkItemName = pUnit->GetQArmor(i)->GetShapeEntry()->szName;
					int checkComplexNum = mod->InfoProperty("GetSetItemStats", checkItemName.GetString(), 2);
					if (checkComplexNum == complexNum)
						currentComplexSize += 1;
				}
			}
			for (int i = 0; i < 3; i++) {
				if (pUnit->GetQWeapon(i)) {
					CString checkItemName = pUnit->GetQWeapon(i)->GetShapeEntry()->szName;
					int checkComplexNum = mod->InfoProperty("GetSetItemStats", checkItemName.GetString(), 2);
					if (checkComplexNum == complexNum)
						currentComplexSize += 1;
				}
			}
			for (int i = 0; i < 3; i++) {
				if (pUnit->GetQItem(i)) {
					CString checkItemName = pUnit->GetQItem(i)->GetShapeEntry()->szName;
					int checkComplexNum = mod->InfoProperty("GetSetItemStats", checkItemName.GetString(), 2);
					if (checkComplexNum == complexNum)
						currentComplexSize += 1;
				}
			}
		}
		szOut.Format("%s %d/%d", szName, currentComplexSize, complexSize);
		ModInfoMultiText(pSurf, szOut.GetString(), rc, 2, 0, RGB(0, 215, 0), DT_CENTER | DT_WORDBREAK); 
	}
	else if (!deconstructable && (tid == ITID_ITEM_WEAPON || tid == ITID_ITEM_ARMOR || tid == ITID_ITEM_QUICK))
		ModInfoMultiText(pSurf, szName.GetString(), rc, 2, 0, RGB(155, 155, 255), DT_CENTER | DT_WORDBREAK);	
	else
		ModInfoMultiText(pSurf, szName.GetString(), rc, 2, 0, INV_COLOR_HEADER, DT_CENTER | DT_WORDBREAK);
	rc += shift;
	rc += shift;
		if (tid == ITID_ITEM_WEAPON) {
			if (pItem->GetShapeEntry()->dwTypeID == 9) {
				szTmp = "string infoitem_65";
				if (pTxtMng->GetText(szTmp.GetString()))
				{
					pTxtMng->GetLine(szTmp);
				}
				szOut.Format("%s %s", szStrings[ITEMTYPE], szTmp);
			}
			else {
				if (typeID == RPGWST_HANDS2 || typeID == RPGWST_HANDS4 || typeID == RPGWST_HANDS5 || 
					typeID == RPGWST_HANDS6 || typeID == RPGWST_HANDS7 || 
					typeID == RPGWST_BOW    || typeID == RPGWST_CROSSBOW) 
				{
					szTmp = "string infoitem_99";
					if (pTxtMng->GetText(szTmp.GetString())) pTxtMng->GetLine(szTmp);
					szOut.Format("%s %s", szStrings[ITEMTYPE], szTmp);
				}
				else {
					szTmp = "string infoitem_98";
					if (pTxtMng->GetText(szTmp.GetString())) pTxtMng->GetLine(szTmp);
					szOut.Format("%s %s", szStrings[ITEMTYPE], szTmp);
				}
			}
			pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
			rc += shift;
            if (!pI3D->IsAltDown())
            {
				DWORD dwShape = pItem->GetShape();
				CString szShape = theItemDatabase.weapons.GetEntry(dwShape)->szName;
				int weightType = mod->InfoProperty("GetWeaponPropsInside", szShape.GetString(), 7);
				if (typeID == 9) {
					if (weightType == 1)
						szTmp = "string infoitem_70";
					else if (weightType == 2)
						szTmp = "string infoitem_71";
					else if (weightType == 3)
						szTmp = "string infoitem_72";
					else
						szTmp = "NOT SET";
				}
				else {
					if (weightType == 1)
						szTmp = "string infoitem_67";
					else if (weightType == 2)
						szTmp = "string infoitem_68";
					else
						szTmp = "string infoitem_69";
				}
				if (pTxtMng->GetText(szTmp.GetString()))
				{
					pTxtMng->GetLine(szTmp);
				}
				szTmp2 = "string infoitem_66";
				if (pTxtMng->GetText(szTmp2.GetString()))
				{
					pTxtMng->GetLine(szTmp2);
				}
				szOut.Format("%s %s", szTmp2, szTmp);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				rc += shift;
			}
		}
		else if (tid == ITID_ITEM_ARMOR) {
			szOut.Format("%s %s", szStrings[ITEMTYPE], szType);
			pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
			rc += shift;
			DWORD dwShape = pItem->GetShape();
			CString szShape = theItemDatabase.armors.GetEntry(dwShape)->szName;
            if (!pI3D->IsAltDown())
            {
				szTmp2 = "string infoitem_66";
				if (pTxtMng->GetText(szTmp2.GetString())) pTxtMng->GetLine(szTmp2);
				if (szShape.Left(2) == "vl")
					szType = "string infoitem_74";
				else if (szShape.Left(1) == "l")
					szType = "string infoitem_75";
				else if (szShape.Left(1) == "m")
					szType = "string infoitem_76";
				else if (szShape.Left(1) == "h")
					szType = "string infoitem_77";
				else if (szShape.Left(2) == "vh")
					szType = "string infoitem_78";
				else
					szType = szShape;
				if (pTxtMng->GetText(szType.GetString()))
				{
					pTxtMng->GetLine(szType);
				}
				szOut.Format("%s %s", szTmp2, szType);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				rc += shift;
			}
		}
		else {
			szOut.Format("%s %s", szStrings[ITEMTYPE], szType);
			pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
			rc += shift;
		}
		bool bNeedOut = true;
		if (tid == ITID_ITEM_QUICK)
		{
			if (pItem->GetShapeEntry() && pItem->GetShapeEntry()->szMaterialType.CompareNoCase(CString("none")) == 0)
				bNeedOut = false;
		}
		if (bNeedOut && tid != ITID_ITEM_LOOT && tid != ITID_ITEM_QUEST)
		{
			szTmp.Format("MATSHORT %s", szMaterial.GetString());
			if (pTxtMng->GetText(szTmp.GetString()))
			{
				pTxtMng->GetLine(szTmp);
			}
			szOut.Format("%s %s x%d", szStrings[MATERIAL], szTmp, pItem->GetMaterialAmount());
			pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
			rc += shift;
		}
		if (tid != ITID_ITEM_QUEST)
		{
			if (deconstructable)
				szOut.Format("%s %d", szStrings[WEIGHT], (int)pItem->GetWeight());
			else {
				szTmp = "string infoitem_101"; if (pTxtMng->GetText(szTmp.GetString())) pTxtMng->GetLine(szTmp);
				szOut.Format("%s %d (%s)", szStrings[WEIGHT], (int)pItem->GetWeight(), szTmp);
			}
			pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
			rc += shift;
		}
		int slots = pItem->GetSpellSlots();
		if (slots > 0) {
			if (tid == ITID_ITEM_ARMOR || tid == ITID_ITEM_WEAPON ||
				(tid == ITID_ITEM_QUICK &&
					static_cast<CItemQuick*>(pItem)->GetQuickItemType() == IDB_WAND))
			{
				CI3DScreenCamp* currentCamp = dynamic_cast<CI3DScreenCamp*>(pScreen);
        CUnit* pUnit = currentCamp ? currentCamp->GetCurrentUnit() : nullptr;
				int perk = pUnit ? pUnit->GetUserUnitStats()->cPerks[22] : 0;
				float item_rest_bonus  = mod->InfoPerkBonus("GetItemRestBonusFromPerkFlow", perk);
				float wand_rest_bonus  = mod->InfoPerkBonus("GetWandRestBonusFromPerkFlow", perk);
				float staff_rest_bonus = mod->InfoPerkBonus("GetStaffRestBonusFromPerkFlow", perk);
				float result_bonus = item_rest_bonus;
				if (tid == ITID_ITEM_QUICK) result_bonus = wand_rest_bonus;
				else if(tid == ITID_ITEM_WEAPON) result_bonus = staff_rest_bonus;

				szTmp = "string infoitem_88"; if (pTxtMng->GetText(szTmp.GetString())) pTxtMng->GetLine(szTmp);
				float regenMp = ModItemDetails::EnergyRegen(pItem->GetMaxMP(), perk, result_bonus);
				szOut.Format("%s %d(%.2f%s)", szStrings[ENERGY], (int)pItem->GetMaxMP(), regenMp, szTmp);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				rc += shift;
				int nSlotsFilled = 0;
				if (pSpelll)
					nSlotsFilled = pSpelll->GetComplexity();
					szOut.Format("%s %d/%d", szStrings[SLOTS], nSlotsFilled, pItem->GetSpellSlots());
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				rc += shift;
			}
		}
		else
		{
			if (tid == ITID_ITEM_ARMOR || tid == ITID_ITEM_WEAPON)
			{
				szTmp = "string infoitem_79";
				if (pTxtMng->GetText("string infoitem_79")) pTxtMng->GetLine(szTmp);
				szOut.Format("%s%s", szStrings[SLOTS], szTmp);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				rc += shift;
			}
		}
		if (tid == ITID_ITEM_ARMOR || tid == ITID_ITEM_WEAPON)
		{
			int nHP = pItem->GetHP() > 0 ? (int)pItem->GetHP() : 0;
			szOut.Format("%s %d/%d", szStrings[DURABILITY], nHP, (int)pItem->GetMaxHP());
			pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
			rc += shift;
		}
		if (tid == ITID_ITEM_WEAPON) 
		{
			float damages[RPG_NUM_ATTACK_TYPES];
			CItemWeapon* pWeapon = static_cast<CItemWeapon*>(pItem);
			if (pWeapon) {
				pWeapon->GetDamageDistribution(damages);
				int i = 0;
				if ((int)pWeapon->GetDamageMax() != 0)
				{
					for (; i < RPG_NUM_ATTACK_TYPES; i++)
					{
						if (damages[i] != 0)
						{
							if (i < RPG_NUM_ATTACK_TYPES)
							{
								szOut.Format("dmg_%d", i);
								pTxtMng->GetSystemString(szOut.GetString(), szTmp);
							}
							CString weaponProt = pWeapon->GetShapeEntry()->szName;

							float DPS = ModItemDetails::Dps(pWeapon->GetDamageMin(), pWeapon->GetDamageRandomRange(), pWeapon->GetActionPoints(),
                                mod->InfoProperty("GetWeaponPropsInside", weaponProt, 4), mod->InfoProperty("GetWeaponPropsInside", weaponProt, 5));
							szOut.Format("%s %.0f-%.0f%s", szStrings[DAMAGE], 
								(pWeapon->GetDamageMin() * damages[i]), 
								(pWeapon->GetDamageMax() * damages[i]), 
								szTmp);
							pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
							rc += shift;
							if (!(pI3D->IsAltDown()))
							{
								if (pTxtMng->GetText("string infoitem_100")) pTxtMng->GetLine(szTmp2);
                                szOut.Format("%s %s", szTmp2, (LPCSTR)ModItemDetails::DpsText(DPS));
								pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
								rc += shift; 
							}
						}
					}
				} 
				int actions = pWeapon->GetActionPoints();
				if (pWeapon->GetWeaponType() != 9) {
					if (actions != 0) {
						float result = actions / 15.0f;
						if (pTxtMng->GetText("string infoskill_49")) pTxtMng->GetLine(szTmp);
						szOut.Format("%s %.1f%s", szStrings[ACTIONPTS], result, szTmp);
						pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
						rc += shift;
					}
					if ( (int)pWeapon->GetRange() > 0 ) {
						szOut.Format("%s %d", szStrings[RANGE], (int)pWeapon->GetRange());
						pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
						rc += shift;
					}
				}
				if (pWeapon->GetWeaponType() != 9) {
					szOut.Format("%s %d", szStrings[MODATTACK], (int)pWeapon->GetAtackBonus());
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					rc += shift;
					szOut.Format("%s %d", szStrings[MODDEFENSE], (int)pWeapon->GetDefenceBonus());
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					rc += shift;
				}
				else {
					int attack = pWeapon->GetAtackBonus();
					if (attack != 0) {
						szOut.Format("%s %d", szStrings[MODATTACK], (int)attack);
						pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
						rc += shift;
					}
					int defence = pWeapon->GetDefenceBonus();
					if (defence != 0) {
						szOut.Format("%s %d", szStrings[MODDEFENSE], (int)defence);
						pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
						rc += shift;
					}
				}
				if (pI3D->IsAltDown())
                {
                }
                else
				{	
					CString weaponProt = pWeapon->GetShapeEntry()->szName;
					CString weaponMat = ModInfoMaterialName(pWeapon);
					const bool showWeaponBlocking = mod->CombatSetting("JMVWeaponBlocking") ||
						pWeapon->GetWeaponType() == 9;
					if (showWeaponBlocking)
					{
						float blockChance = mod->InfoProperty("GetWeaponPropsInside", weaponProt.GetString(), 2) * mod->InfoProperty("GetMaterialPropsInside", weaponMat.GetString(), 4);
						if (blockChance >= 0.001f)
						{
							szTmp2 = "Unknown";
							if (pTxtMng->GetText("string infoitem_45")) pTxtMng->GetLine(szTmp2);
							szTmp.Format("%.0f", blockChance);
							szOut = szTmp2 + " " + CString(blockChance > 0 ? "" : "") + szTmp + "%";
							pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
							rc += shift;
						}
						float blockProt = mod->InfoProperty("GetWeaponPropsInside", weaponProt.GetString(), 3);
						float blockMat = mod->InfoProperty("GetMaterialPropsInside", weaponMat.GetString(), 5);
						float blockPower = blockProt * blockMat;
						if (blockPower >= 0.001f)
						{
							szTmp2 = "Unknown";
							if (pTxtMng->GetText("string infoitem_46")) pTxtMng->GetLine(szTmp2);
							szTmp.Format("%.0f", blockPower);
							szOut = szTmp2 + " " + CString(blockPower > 0 ? "" : "") + szTmp;
							pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
							rc += shift;
						}
					}
					if (mod->InfoProperty("GetWeaponPropsInside", weaponProt.GetString(), 4) != 0)
					{
						szTmp2 = "Unknown";
						if (pTxtMng->GetText("string infoitem_48"))
						{
							pTxtMng->GetLine(szTmp2);
						}
						szTmp.Format("%.0f", mod->InfoProperty("GetWeaponPropsInside", weaponProt.GetString(), 4));
						szOut = szTmp2 + " " + CString(mod->InfoProperty("GetWeaponPropsInside", weaponProt.GetString(), 4) > 0 ? "" : "") + szTmp + "%";
						pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
						rc += shift;
					}
					if (mod->InfoProperty("GetWeaponPropsInside", weaponProt.GetString(), 5) != 0)
					{
						szTmp2 = "Unknown";
						if (pTxtMng->GetText("string infoitem_49"))
						{
							pTxtMng->GetLine(szTmp2);
						}
						szTmp.Format("%.0f", mod->InfoProperty("GetWeaponPropsInside", weaponProt.GetString(), 5));
						szOut = szTmp2 + " " + CString(mod->InfoProperty("GetWeaponPropsInside", weaponProt.GetString(), 5) > 0 ? "+" : "") + szTmp + "%";
						pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
						rc += shift;
					}
					if (mod->InfoProperty("GetWeaponPropsInside", weaponProt.GetString(), 6) != 0)
					{
						szTmp2 = "Unknown";
						if (pTxtMng->GetText("string infoitem_47"))
						{
							pTxtMng->GetLine(szTmp2);
						}
						szTmp.Format("%.0f", mod->InfoProperty("GetWeaponPropsInside", weaponProt.GetString(), 6) * mod->InfoProperty("GetMaterialPropsInside", weaponMat.GetString(), 8));
						szOut = szTmp2 + " " + CString(mod->InfoProperty("GetWeaponPropsInside", weaponProt.GetString(), 6) > 0 ? "" : "") + szTmp + "%";
						pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
						rc += shift;
					}
				}
			}
		}
		if (tid == ITID_ITEM_ARMOR)
		{
			float absorbs[RPG_NUM_ATTACK_TYPES];
			CItemArmor* pArmor = static_cast<CItemArmor*>(pItem);
			pArmor->GetAbsorbtions(absorbs, 0);
			CStringArray szResArray;
			ModInfoAbsorptionStrings(absorbs, szResArray, false);
			szOut = szStrings[ABSORPARMOR] + " " + szResArray[0] + " " + szResArray[1] + " " + szResArray[2];
			pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
			rc += shift;
			if (!szResArray[2].IsEmpty())
			{
				szOut = szStrings[ABSORPARMOR] + " " + szResArray[3] + " " + szResArray[4] + " " + szResArray[5];
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				rc += shift;
			}
            bool alt = pI3D->IsAltDown();
                        bool ctrl = pI3D->IsCtrlDown();
            if (pI3D->IsAltDown())
            {
            }
			else
			{	
				CString armorProt = pArmor->GetShapeEntry()->szName;
				CString armorMat = ModInfoMaterialName(pItem);
				if (mod->InfoProperty("GetArmorPropsInside", armorProt.GetString(), 2) != 0)
				{
					szTmp.Format("%.0f", mod->InfoProperty("GetArmorPropsInside", armorProt.GetString(), 2));
					szOut = szStrings[MODDEFENSE] + " " + CString(mod->InfoProperty("GetArmorPropsInside", armorProt.GetString(), 2) > 0 ? "+" : "") + szTmp;
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					rc += shift;
				}
				if (mod->InfoProperty("GetArmorPropsInside", armorProt.GetString(), 3) != 0)
				{
					szTmp.Format("%.0f", mod->InfoProperty("GetArmorPropsInside", armorProt.GetString(), 3));
					szOut = szStrings[MODATTACK] + " " + CString(mod->InfoProperty("GetArmorPropsInside", armorProt.GetString(), 3) > 0 ? "+" : "") + szTmp;
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					rc += shift;
				}
				if (mod->InfoProperty("GetArmorPropsInside", armorProt.GetString(), 4) != 0)
				{
					szTmp2 = "Unknown";
					if (pTxtMng->GetText("string infoitem_50"))
					{
						pTxtMng->GetLine(szTmp2);
					}
					szTmp.Format("%.0f", mod->InfoProperty("GetArmorPropsInside", armorProt.GetString(), 4) * 100);
					szOut = szTmp2 + " " + CString(mod->InfoProperty("GetArmorPropsInside", armorProt.GetString(), 4) > 0 ? "+" : "") + szTmp + "%";
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					rc += shift;
				}
				float visibilityProt = mod->InfoProperty("GetArmorPropsInside", armorProt.GetString(), 5);
				float visibilityMat = mod->InfoProperty("GetMaterialPropsInside", armorMat.GetString(), 13);
				if (visibilityProt != 0 || visibilityMat != 0)
				{
					szTmp2 = "Unknown";
					if (pTxtMng->GetText("string infoitem_53")) pTxtMng->GetLine(szTmp2);
					szTmp.Format("%.0f", (visibilityProt + visibilityMat) * 100);
					szOut = szTmp2 + " " + CString( visibilityProt + visibilityMat > 0 ? "+" : "") + szTmp + "%";
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					rc += shift;
				}
				float hearingProt = mod->InfoProperty("GetArmorPropsInside", armorProt.GetString(), 6);
				float hearingMat = mod->InfoProperty("GetMaterialPropsInside", armorMat.GetString(), 14);
				if (hearingProt != 0 || hearingMat != 0)
				{
					szTmp2 = "Unknown";
					if (pTxtMng->GetText("string infoitem_51")) pTxtMng->GetLine(szTmp2);
					szTmp.Format("%.0f", (hearingProt + hearingMat) * 100);
					szOut = szTmp2 + " " + CString(hearingProt + hearingMat > 0 ? "+" : "") + szTmp + "%";
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					rc += shift;
				}
				if (mod->InfoProperty("GetArmorPropsInside", armorProt.GetString(), 7) != 0)
				{
					szTmp2 = "Unknown";
					if (pTxtMng->GetText("string infoitem_52"))
					{
						pTxtMng->GetLine(szTmp2);
					}
					szTmp.Format("%.0f", mod->InfoProperty("GetArmorPropsInside", armorProt.GetString(), 7) * 100);
					szOut = szTmp2 + " " + CString(mod->InfoProperty("GetArmorPropsInside", armorProt.GetString(), 7) > 0 ? "+" : "") + szTmp + "%";
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					rc += shift;
				}
				float regen = mod->InfoProperty("GetMaterialPropsInside", armorMat.GetString(), 2);
				if (regen >= 0.00001f)
				{
					szTmp2 = "Unknown";
					if (pTxtMng->GetText("string infoitem_80")) pTxtMng->GetLine(szTmp2);
					szTmp.Format("+%.2f", regen);
					szOut = szTmp2 + " " + CString(regen > 0 ? "" : "") + szTmp + "%";
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					rc += shift;
				}
				float restor = mod->InfoProperty("GetMaterialPropsInside", armorMat.GetString(), 3);
				if (restor >= 0.00001f)
				{
					szTmp2 = "Unknown";
					if (pTxtMng->GetText("string infoitem_81")) pTxtMng->GetLine(szTmp2);
					szTmp.Format("+%.2f", restor);
					szOut = szTmp2 + " " + CString(restor > 0 ? "" : "") + szTmp + "%";
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					rc += shift;
				}
			}
		}
		const bool fixedArmorMaterialStats = mod->CombatSetting("JMVArmorMaterialStats");
		const COLORREF materialStatColor = fixedArmorMaterialStats ? color : INV_COLOR_HEADER;
		if (!fixedArmorMaterialStats || tid == ITID_ITEM_ARMOR)
		{
			float str = 0;
			float dex = 0;
			float inte = 0;
			float run = 0;
			CItemArmor* pArmor = static_cast<CItemArmor*>(pItem);
			CString armorMat = ModInfoMaterialName(pItem);
			str = mod->InfoProperty("GetMaterialPropsInside", armorMat.GetString(), 10);
			dex = mod->InfoProperty("GetMaterialPropsInside", armorMat.GetString(), 11);
			inte = mod->InfoProperty("GetMaterialPropsInside", armorMat.GetString(), 12);
			run = mod->InfoProperty("GetMaterialPropsInside", armorMat.GetString(), 9);
			if (fixedArmorMaterialStats ? run != 0.0f : run > 0) {
				if (pTxtMng->GetText("string infounit_2")) pTxtMng->GetLine(szTmp);
				if (fixedArmorMaterialStats)
					szOut.Format("%s %+.1f%%", szTmp, run);
				else
					szOut.Format("%s +%.2f", szTmp, str);
				pSurf->DrawText(szOut.GetString(), rc, 0, materialStatColor, DT_LEFT);
				rc += shift;
			}
			if (str > 0) {
				if (pTxtMng->GetText("PERK Strength")) pTxtMng->GetLine(szTmp);
				szOut.Format("%s +%.2f", szTmp, str);
				pSurf->DrawText(szOut.GetString(), rc, 0, materialStatColor, DT_LEFT);
				rc += shift;
			}
			if (dex > 0) {
				if (pTxtMng->GetText("PERK Dexterity")) pTxtMng->GetLine(szTmp);
				szOut.Format("%s +%.2f", szTmp, dex);
				pSurf->DrawText(szOut.GetString(), rc, 0, materialStatColor, DT_LEFT);
				rc += shift;
			}
			if (inte > 0) {
				if (pTxtMng->GetText("PERK Intelligence")) pTxtMng->GetLine(szTmp);
				szOut.Format("%s +%.2f", szTmp, inte);
				pSurf->DrawText(szOut.GetString(), rc, 0, materialStatColor, DT_LEFT);
				rc += shift;
			}
		}
		const SIDBItemGeneralShape* pProt = pItem->GetShapeEntry();
		CI3DScreenCamp* camp = dynamic_cast<CI3DScreenCamp*>(pScreen);
		int nShop = 0;
		if (!(pI3D->IsAltDown())) {
			CString itemName = pItem->GetShapeEntry()->szName;
            const auto itemBonuses = mod->InfoSetBonuses(itemName, currentComplexSize, complexNum, false);
            for (const auto &bonus : itemBonuses) {
                int s = bonus.first, statSizeSumm = bonus.second;
				if (statSizeSumm != 0) {
					if (s == 3)	if (pTxtMng->GetText("PERK Strength")) pTxtMng->GetLine(szTmp);
					if (s == 4)	if (pTxtMng->GetText("PERK Dexterity")) pTxtMng->GetLine(szTmp);
					if (s == 5)if (pTxtMng->GetText("PERK Intelligence")) pTxtMng->GetLine(szTmp);
					if (s == 6)	if (pTxtMng->GetText("string infoitem_48")) pTxtMng->GetLine(szTmp);
					if (s == 7)	if (pTxtMng->GetText("string infoitem_49")) pTxtMng->GetLine(szTmp);
					if (s == 8)		if (pTxtMng->GetText("string infoitem_105")) pTxtMng->GetLine(szTmp);
					if (s == 9)		if (pTxtMng->GetText("string infoitem_47")) pTxtMng->GetLine(szTmp);
					if (s == 10)	if (pTxtMng->GetText("string infoitem_80")) pTxtMng->GetLine(szTmp);
					if (s == 11)		
						if (pTxtMng->GetText("string infoitem_84")) pTxtMng->GetLine(szTmp);
					if (s == 12)		if (pTxtMng->GetText("string infoitem_107")) pTxtMng->GetLine(szTmp);
					if (s == 13)			if (pTxtMng->GetText("string infoitem_39")) pTxtMng->GetLine(szTmp);
					if (s == 14)			if (pTxtMng->GetText("string infoitem_38")) pTxtMng->GetLine(szTmp);
					if (s == 15)			if (pTxtMng->GetText("string infoitem_7")) pTxtMng->GetLine(szTmp);
					if (s == 16)			if (pTxtMng->GetText("string infoitem_53")) pTxtMng->GetLine(szTmp);
					if (s == 17)			if (pTxtMng->GetText("string infoitem_51")) pTxtMng->GetLine(szTmp);
					if (s == 18)			if (pTxtMng->GetText("string infoitem_87")) pTxtMng->GetLine(szTmp);
					if (s == 19)		if (pTxtMng->GetText("string infoitem_106")) pTxtMng->GetLine(szTmp);
					if (s == 20)	if (pTxtMng->GetText("string infoskill_22")) pTxtMng->GetLine(szTmp);
					if (s == 21)		if (pTxtMng->GetText("string infoitem_81")) pTxtMng->GetLine(szTmp);
					if (s <= 5 || s == 13 || s == 14) {
						if (statSizeSumm > 0) szOut.Format("%s +%d", szTmp, statSizeSumm); else szOut.Format("%s %d", szTmp, statSizeSumm);
					}else {
						if (statSizeSumm > 0) szOut.Format("%s +%d%%", szTmp, statSizeSumm); else szOut.Format("%s %d%%", szTmp, statSizeSumm);
					}
					pSurf->DrawText(szOut.GetString(), rc, 0, RGB(0, 215, 0), DT_LEFT);
					rc += shift;
				}
            }
			int str = 0;
			int dex = 0;
			int inte = 0;
			bool showComplexText = true;
            const auto complexBonuses = mod->InfoSetBonuses(itemName, currentComplexSize, complexNum, true);
            for (const auto &bonus : complexBonuses) {
                int s = bonus.first, statSizeSumm = bonus.second;
				if (statSizeSumm != 0) {
					if (showComplexText) {
						if (pTxtMng->GetText("string infoitem_104")) pTxtMng->GetLine(szOut);
						pSurf->DrawText(szOut.GetString(), rc, 0, RGB(0, 215, 0), DT_CENTER);
						rc += shift;
					}
					showComplexText = false;
					if (s == 3) if (pTxtMng->GetText("PERK Strength")) pTxtMng->GetLine(szTmp);
					if (s == 4) if (pTxtMng->GetText("PERK Dexterity")) pTxtMng->GetLine(szTmp);
					if (s == 5) if (pTxtMng->GetText("PERK Intelligence")) pTxtMng->GetLine(szTmp);
					if (s == 6) if (pTxtMng->GetText("string infoitem_48")) pTxtMng->GetLine(szTmp);
					if (s == 7) if (pTxtMng->GetText("string infoitem_49")) pTxtMng->GetLine(szTmp);
					if (s == 8) if (pTxtMng->GetText("string infoitem_105")) pTxtMng->GetLine(szTmp);
					if (s == 9) if (pTxtMng->GetText("string infoitem_47")) pTxtMng->GetLine(szTmp);
					if (s == 10) if (pTxtMng->GetText("string infoitem_80")) pTxtMng->GetLine(szTmp);
					if (s == 11) if (pTxtMng->GetText("string infoitem_84")) pTxtMng->GetLine(szTmp);
					if (s == 12) if (pTxtMng->GetText("DMG MAGIC")) pTxtMng->GetLine(szTmp);
					if (s == 13) if (pTxtMng->GetText("string infoitem_39")) pTxtMng->GetLine(szTmp);
					if (s == 14) if (pTxtMng->GetText("string infoitem_38")) pTxtMng->GetLine(szTmp);
					if (s == 15) if (pTxtMng->GetText("string infoitem_7")) pTxtMng->GetLine(szTmp);
					if (s == 16) if (pTxtMng->GetText("string infoitem_53")) pTxtMng->GetLine(szTmp);
					if (s == 17) if (pTxtMng->GetText("string infoitem_51")) pTxtMng->GetLine(szTmp);
					if (s == 18) if (pTxtMng->GetText("string infoitem_87")) pTxtMng->GetLine(szTmp);
					if (s == 19) if (pTxtMng->GetText("DMG HEAD")) pTxtMng->GetLine(szTmp);
					if (s == 20) if (pTxtMng->GetText("DMG BACKSTAB")) pTxtMng->GetLine(szTmp);
					if (s == 21) if (pTxtMng->GetText("string infoitem_81")) pTxtMng->GetLine(szTmp);
					if (s <= 5 || s == 13 || s == 14) {
						if (statSizeSumm > 0) szOut.Format("%s +%d", szTmp, statSizeSumm); else szOut.Format("%s %d", szTmp, statSizeSumm);
					}
					else {
						if (statSizeSumm > 0) szOut.Format("%s +%d%%", szTmp, statSizeSumm); else szOut.Format("%s %d%%", szTmp, statSizeSumm);
					}
					pSurf->DrawText(szOut.GetString(), rc, 0, RGB(0, 215, 0), DT_CENTER);
					rc += shift;
				}
            }
		}
		if (tid == ITID_ITEM_ARMOR || tid == ITID_ITEM_WEAPON ||
			(tid == ITID_ITEM_QUICK &&
				static_cast<CItemQuick*>(pItem)->GetQuickItemType() == IDB_WAND))
		{
			CSpell* pSpell = pSpelll;
			if (pSpell)
			{
				szTmp = "Unknown";
				CString plu;
				szTmp.Format("SPELL %s", pSpell->GetPrototype()->szCodeName.GetString());
				if (pTxtMng->GetText(szTmp.GetString()))
					pTxtMng->GetLine(szTmp);
				szOut.Format("%s %s", szStrings[SPELL], szTmp);
				pSurf->DrawText(szOut.GetString(), rc, 0, color2, DT_LEFT);
			}
			rc += shift;
			rc += shift;
		}
    if (tid == ITID_ITEM_LOOT )
	{
		pTxtMng->GetSystemString("loot_item", szTmp);
		szDesc = szDesc + szTmp;
		ModInfoDescription(pI3D, pSurf, szDesc.GetString(), rc, 8, 0, color, DT_CENTER | DT_WORDBREAK);
		pSurf->ReleaseDC();
		return;
	}
	if (tid == ITID_ITEM_SPELL)
		ModInfoDescription(pI3D, pSurf, szDesc.GetString(), rc, 8, 0, color, DT_CENTER | DT_WORDBREAK);
    else if ((tid == ITID_ITEM_WEAPON || tid == ITID_ITEM_ARMOR) && pI3D->IsAltDown())
		ModInfoDescription(pI3D, pSurf, szDesc.GetString(), rc, 8, 0, color, DT_LEFT | DT_WORDBREAK);
    else
        if ((tid != ITID_ITEM_WEAPON && tid != ITID_ITEM_ARMOR) && !pI3D->IsAltDown())
			ModInfoDescription(pI3D, pSurf, szDesc.GetString(), rc, 8, 0, color, DT_CENTER | DT_WORDBREAK);
    pSurf->ReleaseDC();
#endif 

        return;
    }
    const auto *mod = ModRuntime::Current();
    const bool compactInfoLines = mod && mod->AdvancedUI("useLS_FormTextInfo") && mod->CombatSetting("JMVItemInfoLineHeight");
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
        if (ModRuntime::Current()) {
            ModInfoAbsorptionStrings(absorbs, szResArray, false);
            szOut = szStrings[ABSORPARMOR] + " " + szResArray[0] + " " + szResArray[1] + " " + szResArray[2];
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
            if (!szResArray[2].IsEmpty()) {
                szOut = szStrings[ABSORPARMOR] + " " + szResArray[3] + " " + szResArray[4] + " " + szResArray[5];
                pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
                rc += shift;
            }
        } else {
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

        }
        if(mod && mod->AdvancedUI("useLS_FormTextInfo")){
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
        }
    const bool fixedMaterialStats = !ModRuntime::Current() || ModRuntime::Current()->CombatSetting("JMVArmorMaterialStats");
    if (mod && mod->AdvancedUI("useLS_FormTextInfo") && (!fixedMaterialStats || tid == ITID_ITEM_ARMOR) && ModInfoMaterialName(pItem))
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
    if (auto *mod = ModRuntime::Current();mod && mod->AdvancedUI("useLS_FormTextInfo")) {

	CItem* pItem = GetItem();
	CSpell* pSpell = GetSpell();
    if (!pItem || !pSpell) return;
	CTextManager* pTxtMng = theWorld.GetTextManager();
	CString szName, szDesc, szType;
	pTxtMng->GetItemNameAndDesc(pItem, szName, szDesc, szType);
#ifndef NOGETDC
	CI3DSurface* pSurf = surfaces[0];
	pSurf->GetDC();
	CString szOut, szTmp, szTmp2;
	CRect rc(15, 10, 185, 25);
	CPoint shift(0, mod->CombatSetting("JMVItemInfoLineHeight") ? 12 : 15);
	COLORREF color = INV_COLOR_INFO;
	ModInfoMultiText(pSurf, szName.GetString(), rc, 2, 0, INV_COLOR_HEADER, DT_CENTER | DT_WORDBREAK);
	rc += shift;
	rc += shift;
	if (pTxtMng->GetText(CString(CString("spell ") + pSpell->GetPrototype()->szCodeName).GetString())) pTxtMng->GetLine(szName);
	pTxtMng->GetSystemString("item_spell", szType);
	ModInfoMultiText(pSurf, szName.GetString(), rc, 2, 0, color, DT_LEFT);
	rc += shift;
	szOut.Format("%s %s", szStrings[ITEMTYPE], szType);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	CSpell * pSpellIn = pItem->GetSpell(0);
	if (pSpellIn)
	{
		CString name = pSpell->GetPrototype()->szCodeName;
		float subeffect = mod->InfoProperty("GetSpellAdds", name, 2);
        szOut.Format("%s %s", szStrings[EFFECT], (LPCSTR)ModItemInfo::PotionEffect(
            pSpellIn->GetPower(), subeffect, pSpellIn->GetPrototype()->bRuneAllowed[SDBAR_EFFECT], szStrings[FIXEDEFF]));
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		rc += shift;
		if (pSpellIn->GetDuration() < 2)
			szOut.Format("%s %s", szStrings[DURATION], szStrings[INSTANT]);
		else
			szOut.Format("%s %.1f%s", szStrings[DURATION], pSpellIn->GetDuration() / 15.f,
				szStrings[SECONDS]);
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		rc += shift;
		szOut.Format("%s %d", szStrings[WEIGHT], (int)pItem->GetWeight());
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		rc += shift;
		rc += shift;
	}
	const SIDBItemGeneralShape* pProt = pItem->GetShapeEntry();
	CI3DScreenCamp* camp = dynamic_cast<CI3DScreenCamp*>(pScreen);
	int nShop = 0;
	if (camp && camp->GetCurrentShop())
		nShop = camp->GetCurrentShop()->nID;
	if (mod->InfoForbiddenSpell(pProt->szName.GetString(), nShop)) {
		if (pTxtMng->GetText("string infoitem_91")) pTxtMng->GetLine(szTmp);
		szOut.Format("%s", szTmp);
		pSurf->DrawText(szOut.GetString(), rc, 0, RGB(255, 200, 0), DT_LEFT);
		rc += shift;
	}
	ModInfoDescription(pI3D, pSurf, szDesc.GetString(), rc, 8, 0, color, DT_CENTER | DT_WORDBREAK);
	pSurf->ReleaseDC();
#endif 

        return;
    }
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
    if (auto *mod = ModRuntime::Current();mod && mod->AdvancedUI("useLS_FormTextInfo")) {

	CItem* pItem = GetItem();
	CSpell* pSpell = GetSpell();
	if (!pSpell)
	{
		surfaces[0]->ReleaseDC();
		return;
	}
	CTextManager* pTxtMng = theWorld.GetTextManager();
	CString szName, szDesc, szType, szCode;
	if (pTxtMng->GetText(CString(CString("spell ") + pSpell->GetPrototype()->szCodeName).GetString()))
	{
		pTxtMng->GetLine(szName);
		pTxtMng->GetUntilNextCommand(szDesc);
	}
	pTxtMng->GetSystemString("item_spell", szType);
#ifndef NOGETDC
	CI3DSurface* pSurf = surfaces[0];
	pSurf->GetDC();
	CString szTmp,szTmp2,szTmp3, szOut;
	CRect rc(15, 10, 185, 25);
	CPoint shift(0, mod->CombatSetting("JMVItemInfoLineHeight") ? 12 : 15);
	COLORREF color = INV_COLOR_INFO;
    CI3DScreenCamp *camp = dynamic_cast<CI3DScreenCamp*>(pScreen);
    CUnit *pUnit = camp ? camp->GetCurrentUnit() : nullptr;
    if (!pUnit) { pSurf->ReleaseDC(); return; }
    ModSkills::Config meleeActiveConfig;
	if (mod->SkillConfig(pSpell, meleeActiveConfig))
	{
		ModInfoMultiText(pSurf, szName.GetString(), rc, 2, 0,
			INV_COLOR_HEADER, DT_CENTER | DT_WORDBREAK);
		rc += shift;
		rc += shift;
		szOut.Format("%s \xC1\xEE\xE5\xE2\xEE\xE9 \xED\xE0\xE2\xFB\xEA",
			szStrings[ITEMTYPE]);
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		rc += shift;
		szOut.Format("\xC7\xE0\xEF\xE0\xF1 \xF1\xE8\xEB: %.0f",
			meleeActiveConfig.Cost(pSpell->GetManaCost()));
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		rc += shift;
		const char* requiredSkillName = meleeActiveConfig.requiredArchery >= 0.0f
			? "\xD1\xF2\xF0\xE5\xEB\xFC\xE1\xE0"
			: "\xC1\xEB\xE8\xE6\xED\xE8\xE9 \xE1\xEE\xE9";
		szOut.Format("%s: %.0f", requiredSkillName,
			meleeActiveConfig.Requirement(float(pSpell->GetComplexity())));
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		rc += shift;
		szOut.Format("\xD3\xF0\xEE\xED: %.0f%%",
			meleeActiveConfig.damageMultiplier * 100.0f);
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		rc += shift;
		if (meleeActiveConfig.accuracyBonus > 0.0f)
		{
			szOut.Format("\xD2\xEE\xF7\xED\xEE\xF1\xF2\xFC: +%.0f",
				meleeActiveConfig.accuracyBonus);
			pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
			rc += shift;
		}
		if (meleeActiveConfig.areaRadius > 0.0f)
		{
			szOut.Format("\xD0\xE0\xE4\xE8\xF3\xF1: %.0f \xEC.",
				meleeActiveConfig.areaRadius);
			pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
			rc += shift;
		}
		szOut.Format(
			"\xCF\xE5\xF0\xE5\xE7\xE0\xF0\xFF\xE4\xEA\xE0: %.0f \xF1\xE5\xEA.",
			meleeActiveConfig.cooldownSeconds);
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		for (int i = 0; i < 7; ++i)
			rc += shift;
		ModInfoDescription(pI3D, pSurf, szDesc.GetString(), rc, 8, 0, color,
			DT_LEFT | DT_WORDBREAK);
		pSurf->ReleaseDC();
		return;
	}
	ModInfoMultiText(pSurf, szName.GetString(), rc, 2, 0, INV_COLOR_HEADER, DT_CENTER | DT_WORDBREAK);
	rc += shift;
	rc += shift;
	szOut.Format("%s %s", szStrings[ITEMTYPE], szType);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
    const float additional = mod->InfoProperty("GetSpellAdds", pSpell->GetPrototype()->szCodeName, 2);
    const auto info = ModCampSpellInfo::Calculate(*pUnit->GetUserUnitStats(), *pSpell,
        additional, theSettings.nDifficultyLevel, pI3D->IsCtrlDown());
    szOut.Format("%s %s", szStrings[MANA], (LPCSTR)ModCampSpellInfo::Mana(*pSpell, info));
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	szOut.Format("%s %d", szStrings[SLOTS], pSpell->GetComplexity());
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	szTmp.Format("school_%d", pSpell->GetMagicSubtype());
	pTxtMng->GetSystemString(szTmp.GetString(), szOut);
	szOut = szStrings[MAGICSCHOOL] + " " + szOut;
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
    const int spellID = pSpell->GetSubtypeID() >> 16;
    const bool attacking = spellID <= SPELL_ID_POISONFOG || spellID == SPELL_ID_VAMP;
    if (pSpell->GetMagicSubtype() == 0) szTmp.Format("damage_%d", 3);
    else if (pSpell->GetMagicSubtype() == 1) szTmp.Format("damage_%d", 5);
    else if (pSpell->GetMagicSubtype() == 2) szTmp.Format("damage_%d", 4);
    pTxtMng->GetSystemString(szTmp, szTmp);
    if (!attacking) szTmp = "";
    szOut.Format("%s %s%s", szStrings[EFFECT], (LPCSTR)ModCampSpellInfo::Effect(info), (LPCSTR)szTmp);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
    if (pSpell->GetActionPoints() < 1.f)
        szOut.Format("%s %s", szStrings[SPEED], szStrings[INSTANT]);
    else
        szOut.Format("%s %s%s", szStrings[SPEED], (LPCSTR)ModCampSpellInfo::Cast(*pSpell, info), (LPCSTR)szStrings[SECONDS]);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	szOut.Format("%s %d%s", szStrings[RANGE], (int)pSpell->GetRange(), szStrings[METERS]);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	if (pSpell->IsOnUnit())
		szOut.Format("%s %s", szStrings[AREA], szStrings[AREAUNIT]);
	else
		szOut.Format("%s %.1f%s", szStrings[AREA], pSpell->GetRadius(), szStrings[METERS]);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	if (pSpell->GetDuration() < 2)
		szOut.Format("%s %s", szStrings[DURATION], szStrings[INSTANT]);
	else
		szOut.Format("%s %.1f%s", szStrings[DURATION], pSpell->GetDuration() / 15.f,
			szStrings[SECONDS]);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	szOut.Format("%s %d", szStrings[TARGETS], pSpell->GetNumTargets());
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
    // Paired per-second/total suffixes are one row, as in the addon.
    const auto extras = ModCampSpellInfo::Extras(*pSpell, info);
    for (size_t i = 0; i < extras.size(); ++i) {
        const auto &extra = extras[i];
        szTmp = CString("string ") + extra.label;
        if (pTxtMng->GetText(szTmp)) pTxtMng->GetLine(szTmp);
        szOut = szTmp + " " + extra.value;
        if (extra.perSecond) {
            szTmp2 = "string infoitem_88";
            if (pTxtMng->GetText(szTmp2)) pTxtMng->GetLine(szTmp2);
            szOut += szTmp2;
            szOut += extras[++i].value;
        }
        pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
        rc += shift;
    }
	int nShop = 0;
	if (camp->GetCurrentShop()) {
		nShop = camp->GetCurrentShop()->nID;
		if (mod->InfoForbiddenSpell(pSpell->GetPrototype()->szCodeName, nShop)) {
			if (pTxtMng->GetText("string infoitem_91")) pTxtMng->GetLine(szTmp);
			szOut.Format("%s", szTmp);
			pSurf->DrawText(szOut.GetString(), rc, 0, RGB(255, 200, 0), DT_LEFT);
			rc += shift;
		}
	}
	if (pSpell->GetRunesCount() > 0)
	{
		szOut = szStrings[MODIFIERS];
		for (int i = 0; i < pSpell->GetRunesCount(); i++)
		{
			szOut = szOut + " ";
			szTmp = "?";
			szCode = theWorld.GetSpellManager()->GetModifier(pSpell->GetRunes()[i] - 1)->pszCode;
			if (pTxtMng->GetText(CString(CString("modifier ") + szCode).GetString()))
			{
				pTxtMng->SkipLine();
				pTxtMng->SkipLine();
				pTxtMng->GetLine(szTmp);
			}
			szOut = szOut + szTmp + ",";
		}
		szOut.TrimRight(",");
		szOut = szOut + ".";
		ModInfoMultiText(pSurf, szOut.GetString(), rc, 3, 0, color, DT_LEFT | DT_WORDBREAK);
	}
	for (int descriptionGapLine = 0; descriptionGapLine < 3; ++descriptionGapLine)
		rc += shift;
	if (pSpell->GetSubtypeID() >> 16 != SPELL_ID_STRENGTHEN && pSpell->GetSubtypeID() >> 16 != SPELL_ID_WEAKEN)
		ModInfoDescription(pI3D, pSurf, szDesc.GetString(), rc, 8, 0, color, DT_LEFT | DT_WORDBREAK);
	pSurf->ReleaseDC();
#endif 

        return;
    }
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
    if (ModRuntime::Current() && ModRuntime::Current()->AdvancedUI("useLS_FormTextInfo") && ModRuntime::Current()->CombatSetting("JMVItemInfoLineHeight")) {
        auto *mod = ModRuntime::Current();

	CItem* pItem = GetItem();
	CItemLoot* pLoot = static_cast<CItemLoot*>(pItem);
	const SIDBMaterial* pMat = pLoot
		? theItemDatabase.materials.GetEntry(pLoot->GetMaterialIndex())
		: nullptr;
	if (!pMat)
		return;
	CTextManager* pTxtMng = theWorld.GetTextManager();
	CString szName, szDesc, szType;
	pTxtMng->GetItemNameAndDesc(pItem, szName, szDesc, szType);
#ifndef NOGETDC
	CI3DSurface* pSurf = surfaces[0];
	pSurf->GetDC();
	CString szTmp, szOut;
	CRect rc(15, 10, 185, 25);
	CPoint shift(0, 12);
	COLORREF color = INV_COLOR_INFO;
	ModInfoMultiText(pSurf, szName.GetString(), rc, 2, 0,
		INV_COLOR_HEADER, DT_CENTER | DT_WORDBREAK);
	rc += shift;
	rc += shift;
	szOut.Format("%s %s", szStrings[ITEMTYPE], szType);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	szTmp = pMat->szType;
	szTmp.MakeLower();
	szTmp = CString("mattype_") + szTmp;
	pTxtMng->GetSystemString(szTmp.GetString(), szOut);
	szOut = szStrings[MATTYPE] + " " + szOut;
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	szOut.Format("%s %d", szStrings[WEIGHT], static_cast<int>(pMat->fpWeight));
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	szOut.Format("%s %d", szStrings[ENERGY], static_cast<int>(pMat->fpManaCapacity));
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	szOut.Format("%s %d", szStrings[SLOTS], pMat->dwSpellSlots);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	szOut.Format("%s %d", szStrings[DURABILITY], static_cast<int>(pMat->fpDurability));
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	szOut.Format("%s %.1f", szStrings[DAMAGE], pMat->fpDamageModifier);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	CStringArray szResArray;
	float absorbs[RPG_NUM_ATTACK_TYPES];
	for (int i = 0; i < RPG_NUM_ATTACK_TYPES; ++i)
		absorbs[i] = pMat->fpAbsorbtionMultipliers[i];
	ModInfoAbsorptionStrings(absorbs, szResArray, false);
	if (szResArray.GetSize() > 0)
	{
		szOut = szStrings[ABSORPTION] + " " + szResArray[0];
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		rc += shift;
	}
	if (szResArray.GetSize() > 2 && !szResArray[2].IsEmpty())
	{
		szOut = szStrings[VULNERABLE] + " " + szResArray[2];
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		rc += shift;
	}
	rc += shift;
	ModInfoDescription(pI3D, pSurf, szDesc.GetString(), rc, 8, 0, color,
		DT_CENTER | DT_WORDBREAK);
	pSurf->ReleaseDC();
#endif 

        return;
    }
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
    if (auto *mod = ModRuntime::Current();mod && mod->AdvancedUI("useLS_FormTextInfo")) {

	CItem* pItem = GetItem();
	CSpell* pSpell = GetSpell();
	enum
	{
		INSTR_WEAPON,
		INSTR_ARMOR,
		INSTR_WAND,
		INSTR_TRAP,
		INSTR_OTHER
	};
	int nType;
	CTextManager* pTxtMng = theWorld.GetTextManager();
	const SIDBItemGeneralShape* pShape = nullptr;
	CItemLoot* pLoot = static_cast<CItemLoot*>(pItem);
	assert((pLoot->GetInstructionType() >= IDBINS_WEAPON) &&
		(pLoot->GetInstructionType() <= IDBINS_QUICK));
	CString szTmp, szTmp2, szOut;
	switch (pLoot->GetInstructionType())
	{
	case IDBINS_WEAPON:
		nType = INSTR_WEAPON;
		pShape = theItemDatabase.weapons.GetEntry(pLoot->GetInstructionItemIndex());
		break;
	case IDBINS_ARMOR:
		nType = INSTR_ARMOR;
		pShape = theItemDatabase.armors.GetEntry(pLoot->GetInstructionItemIndex());
		break;
	case IDBINS_QUICK:
		nType = INSTR_WAND;
		pShape = theItemDatabase.quickItems.GetEntry(pLoot->GetInstructionItemIndex());
		break;
	default:
		return;
	}
    if (!pShape) return;
	CString szName, szDesc, szType;
	pTxtMng->GetItemNameAndDesc(pItem, szName, szDesc, szType);
#ifndef NOGETDC
	CI3DSurface* pSurf = surfaces[0];
	pSurf->GetDC();
	CRect rc(15, 10, 185, 25);
	CPoint shift(0, mod->CombatSetting("JMVItemInfoLineHeight") ? 12 : 15);
	COLORREF color = INV_COLOR_INFO;
	COLORREF colorRare = INV_COLOR_INFO;
	ModInfoMultiText(pSurf, szName.GetString(), rc, 2, 0, INV_COLOR_HEADER, DT_CENTER | DT_WORDBREAK);
	rc += shift;
	rc += shift;
	EIDBInstructionType tid = pLoot->GetInstructionType();
	int typeID = pShape->dwTypeID;
	if (tid == IDBINS_WEAPON) {
        const char *typeKey = ModItemInfo::WeaponTypeKey(typeID);
        if (typeKey) {
            szTmp = CString("string ") + typeKey;
            if (pTxtMng->GetText(szTmp)) pTxtMng->GetLine(szTmp);
            szOut.Format("%s %s", szStrings[ITEMTYPE], szTmp);
        } else szOut.Format("%s %s", szStrings[ITEMTYPE], szType);
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		rc += shift;
		DWORD dwShape = pItem->GetShape();
		CString szShape = pShape->szName;
		int weightType = mod->InfoProperty("GetWeaponPropsInside", szShape, 7);
        const char *weightKey = ModItemInfo::WeaponWeightKey(typeID, weightType);
        szTmp = weightKey ? CString("string ") + weightKey : CString("NOT SET");
		if (pTxtMng->GetText(szTmp.GetString()))
		{
			pTxtMng->GetLine(szTmp);
		}
		szTmp2 = "string infoitem_66";
		if (pTxtMng->GetText(szTmp2.GetString()))
		{
			pTxtMng->GetLine(szTmp2);
		}
		if (typeID == RPGWST_HANDS2 || typeID == RPGWST_HANDS4 || typeID == RPGWST_HANDS5 || typeID == RPGWST_HANDS6 || typeID == RPGWST_HANDS7 
			|| typeID == RPGWST_BOW || typeID == RPGWST_CROSSBOW) 
			szOut.Format("%s %s", szTmp2, szTmp);
		else
			szOut.Format("%s %s", szTmp2, szTmp);
	}
	else if (tid == IDBINS_ARMOR) {
		szOut.Format("%s %s", szStrings[ITEMTYPE], szType);
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		rc += shift;
		DWORD dwShape = pItem->GetShape();
		CString szShape = pShape->szName;
		szTmp2 = "string infoitem_66";
		if (pTxtMng->GetText(szTmp2.GetString()))
		{
			pTxtMng->GetLine(szTmp2);
		}
        const char *armorKey = ModItemInfo::ArmorWeightKey(szShape);
        szType = armorKey ? CString("string ") + armorKey : szShape;
		if (pTxtMng->GetText(szType.GetString()))
		{
			pTxtMng->GetLine(szType);
		}
		szOut.Format("%s %s", szTmp2, szType);
	}
	else
		szOut.Format("%s %s", szStrings[ITEMTYPE], szType);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	szTmp = pShape->szMaterialType;
	szTmp.MakeLower();
	szTmp = CString("mattype_") + szTmp;
	pTxtMng->GetSystemString(szTmp.GetString(), szOut);
	szTmp = szOut;
	szOut.Format("%s %s x %d", szStrings[MATERIAL], szTmp, pShape->dwComponents);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	szOut.Format("%s %d", szStrings[WEIGHT], (int)pShape->fpWeight);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	if (nType <= INSTR_WAND)
	{
		szOut.Format("%s %d", szStrings[ENERGY], (int)pShape->fpManaCapacity);
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		rc += shift;
		szOut.Format("%s %d", szStrings[SLOTS], pShape->dwSpellSlots);
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		rc += shift;
	}
	if (nType <= INSTR_ARMOR)
	{
		szOut.Format("%s %d", szStrings[DURABILITY], (int)pShape->fpDurability);
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		rc += shift;
	}
	if (nType == INSTR_WEAPON)
	{
		const SIDBWeaponShape* pWeaponShape = static_cast<const SIDBWeaponShape*>(pShape);
		int i = 0;
		for (; i < RPG_NUM_ATTACK_TYPES; i++)
			if (pWeaponShape->fpDamageDistributions[i] != 0)
				break;
		if (i < RPG_NUM_ATTACK_TYPES)
		{
			szOut.Format("damage_%d", i);
			pTxtMng->GetSystemString(szOut.GetString(), szTmp);
		}
		szOut.Format("%s %d-%d %s", szStrings[DAMAGE], (int)(pWeaponShape->fpDamageMin * 10),
			(int)((pWeaponShape->fpDamageMin + pWeaponShape->fpDamageRandom) * 10), szTmp);
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		rc += shift;
		szOut.Format("%s %d", szStrings[ACTIONPTS], pWeaponShape->dwActionPoints);
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		rc += shift;
		szOut.Format("%s %d", szStrings[MODATTACK], (int)pWeaponShape->GetAttackBonus());
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		rc += shift;
		if (pWeaponShape->dwTypeID == RPGWST_BOW || pWeaponShape->dwTypeID == RPGWST_CROSSBOW)
			szOut.Format("%s %d", szStrings[RANGE], (int)pWeaponShape->fpRange);
		else
			szOut.Format("%s %d", szStrings[MODDEFENSE], (int)pWeaponShape->GetDefenceBonus());
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		rc += shift;
	}
	if (nType == INSTR_ARMOR)
	{
		const SIDBArmorShape* pArmorShape = static_cast<const SIDBArmorShape*>(pShape);
		float absorbs[RPG_NUM_ATTACK_TYPES];
		for (int i = 0; i < RPG_NUM_ATTACK_TYPES; i++)
			absorbs[i] = pArmorShape->main.fpDistributions[i] * pArmorShape->main.fpAbsorbtion * 10;
		CStringArray szResArray;
		ModInfoAbsorptionStrings(absorbs, szResArray, false);
		szOut = szStrings[ABSORPTION] + " " + szResArray[0];
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		rc += shift;
		if (!szResArray[2].IsEmpty())
		{
			szOut = szStrings[VULNERABLE] + " " + szResArray[2];
			pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
			rc += shift;
		}
	}
	CI3DScreenCamp* camp = dynamic_cast<CI3DScreenCamp*>(pScreen);
	int nShop = 0;
	if (camp && camp->GetCurrentShop()) {
		nShop = camp->GetCurrentShop()->nID;
		if (mod->InfoForbiddenSpell(pShape->szName.GetString(), nShop)) {
			if (pTxtMng->GetText("string infoitem_91")) pTxtMng->GetLine(szTmp);
			szOut.Format("%s", szTmp);
			pSurf->DrawText(szOut.GetString(), rc, 0, RGB(255, 200, 0), DT_LEFT);
			rc += shift;
		}
	}
	rc += shift;
	ModInfoDescription(pI3D, pSurf, szDesc.GetString(), rc, 8, 0, color, DT_CENTER | DT_WORDBREAK);
	pSurf->ReleaseDC();
#endif 

        return;
    }
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
    if (auto *mod = ModRuntime::Current();mod && mod->AdvancedUI("useLS_FormTextInfo")) {

	CItem* pItem = GetItem();
	CSpell* pSpell = GetSpell();
	CTextManager* pTxtMng = theWorld.GetTextManager();
	CItemLoot* pLoot = static_cast<CItemLoot*>(pItem);
	const SSDBSpellPrototype* pProt = theSpellDatabase.spells.GetEntry( pLoot->GetInstructionItemIndex() );
    if (!pProt) return;
	CString szName, szDesc, szType;
	pTxtMng->GetItemNameAndDesc(pItem, szName, szDesc, szType);
#ifndef NOGETDC
	CI3DSurface* pSurf = surfaces[0];
	pSurf->GetDC();
	CString szTmp,szTmp2, szOut;
	CRect rc(15, 10, 185, 25);
	CPoint shift(0, mod->CombatSetting("JMVItemInfoLineHeight") ? 12 : 15);
	COLORREF color = INV_COLOR_INFO;
	ModInfoMultiText(pSurf, szName.GetString(), rc, 2, 0, INV_COLOR_HEADER, DT_CENTER | DT_WORDBREAK);
	rc += shift;
	rc += shift;
	szOut.Format("%s %s", szStrings[ITEMTYPE], szType);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	szOut.Format("%s %d", szStrings[MANA], (int)pProt->fpMana);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	szOut.Format("%s %d", szStrings[SLOTS], pProt->dwComplexity);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	szTmp.Format("school_%d", pProt->mstSubtype);
	pTxtMng->GetSystemString(szTmp.GetString(), szOut);
	szOut = szStrings[MAGICSCHOOL] + " " + szOut;
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	CString name = pProt->szCodeName;
	float subeffect = mod->InfoProperty("GetSpellAdds", name, 2);
    szOut.Format("%s %s", szStrings[EFFECT], (LPCSTR)ModItemInfo::PrototypeEffect(pProt->fpEffect, subeffect));
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	if (pProt->dwActions < 1.f)
		szOut.Format("%s %s", szStrings[SPEED], szStrings[INSTANT]);
	else
		szOut.Format("%s %.1f%s", szStrings[SPEED], ModItemInfo::PrototypeCast(pProt->dwActions), szStrings[SECONDS]);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	szOut.Format("%s %d%s", szStrings[RANGE], (int)pProt->fpRange, szStrings[METERS]);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	if (pProt->fpArea < 1.f)
		szOut.Format("%s %s", szStrings[AREA], szStrings[AREAUNIT]);
	else
		szOut.Format("%s %.1f%s", szStrings[AREA], ModItemInfo::PrototypeRadius(pProt->fpArea), szStrings[METERS]);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	if (pProt->dwDuration < 2)
		szOut.Format("%s %s", szStrings[DURATION], szStrings[INSTANT]);
	else
		szOut.Format("%s %.1f%s", szStrings[DURATION], pProt->dwDuration / 15.f,
			szStrings[SECONDS]);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	szOut.Format("%s %d", szStrings[TARGETS], pProt->dwNumTargets);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	rc += shift;
	ModInfoDescription(pI3D, pSurf, szDesc.GetString(), rc, 8, 0, color, DT_CENTER | DT_WORDBREAK);
	pSurf->ReleaseDC();
#endif 

        return;
    }
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
    if (ModRuntime::Current() && ModRuntime::Current()->AdvancedUI("useLS_FormTextInfo") && ModRuntime::Current()->CombatSetting("JMVItemInfoLineHeight")) {
        auto *mod = ModRuntime::Current();

	CItem* pItem = GetItem();
	CItemLoot* pLoot = static_cast<CItemLoot*>(pItem);
	const SSDBModifier* pMod = pLoot
		? theSpellDatabase.runes.GetEntry(pLoot->GetInstructionItemIndex())
		: nullptr;
	if (!pMod)
		return;
	CTextManager* pTxtMng = theWorld.GetTextManager();
	CString szName, szDesc, szType;
	pTxtMng->GetItemNameAndDesc(pItem, szName, szDesc, szType);
#ifndef NOGETDC
	CI3DSurface* pSurf = surfaces[0];
	pSurf->GetDC();
	CString szOut;
	CRect rc(15, 10, 185, 25);
	CPoint shift(0, 12);
	COLORREF color = INV_COLOR_INFO;
	ModInfoMultiText(pSurf, szName.GetString(), rc, 2, 0,
		INV_COLOR_HEADER, DT_CENTER | DT_WORDBREAK);
	rc += shift;
	rc += shift;
	szOut.Format("%s %s", szStrings[ITEMTYPE], szType);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	szOut.Format("%s %d", szStrings[MANA], static_cast<int>(pMod->fpMana));
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	szOut.Format("%s %d", szStrings[SLOTS], pMod->dwComplexity);
	pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
	rc += shift;
	rc += shift;
	ModInfoDescription(pI3D, pSurf, szDesc.GetString(), rc, 8, 0, color,
		DT_CENTER | DT_WORDBREAK);
	pSurf->ReleaseDC();
#endif 

        return;
    }
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
    auto* mod=ModRuntime::Current();
    if(mod && !mod->AdvancedUI("useLS_SetSkill"))mod=nullptr;
    const auto* shown=pSkillInfo->pUnit?pSkillInfo->pUnit->GetUserUnitStats():nullptr;
    ModCampStats::Primary primary={};
    if(mod && shown && pSkillInfo->nType==0 && pSkillInfo->nSkill>=0 && pSkillInfo->nSkill<=2)
        primary=ModCampStats::PrimaryValues(*shown,theAIGeneralSettings.fHPVal1,theAIGeneralSettings.fMPVal1,
            pSkillInfo->nSkill==2?mod->InfoPerkBonus("GetExpMultiplier2",shown->cIntelligence):1.0f);

    CString szTmp, szOut;
    CRect rc(15, 10, 185, 25);
    CPoint shift(0, mod?15:12);
    COLORREF color = INV_COLOR_INFO;

    // NAME
    CString szName, szDesc;
    const bool full=mod && shown && pScreen->GetCurrentPers();
    ModCampDescriptions::Result display;
    if(full && ((pSkillInfo->nType==0 && (pSkillInfo->nSkill==1||pSkillInfo->nSkill==7)) ||
        (pSkillInfo->nType==1&&pSkillInfo->nSkill==RPGASKL_BACKSTAB) || pSkillInfo->nType==5 ||
        (pSkillInfo->nType==7&&pSkillInfo->szNonlocalName=="camp_damage")))
        display=mod->CampDescriptions(*pScreen->GetCurrentPers(),*pSkillInfo->pUnit,true);
    auto drawRows=[&](const std::vector<ModCampDescriptions::Row>& rows){
        for(const auto& row:rows){
            CString label,value;std::string key=row.label;const auto suffix=key.find('|');std::string unitLabel;
            if(suffix!=std::string::npos){unitLabel=key.substr(suffix+1);key.resize(suffix);}
            if(key=="$WSLDEXT")label=szStrings[WSLDEXT];else if(key=="$WSLWEAPON")label=szStrings[WSLWEAPON];
            else if(key=="$WSLPERK")label=szStrings[WSLPERK];else if(key=="$WSLSUM")label=szStrings[WSLSUM];
            else if(row.damageType>=0){value.Format("damage_%d",row.damageType);pTxtMng->GetSystemString(value,label);}
            else{
                const bool second=key=="infoitem_58#2";if(second)key="infoitem_58";
                const CString text=key.find("PERK ")==0?CString(key.c_str()):CString("string ")+key.c_str();
                if(pTxtMng->GetText(text))pTxtMng->GetLine(label);else label=key.c_str();
                if(key=="infoitem_58")label=label+(second?" 2":" 1");
            }
            if(unitLabel=="difficulty"){
                CString difficulty,levelText;if(pTxtMng->GetText("string infoskill_53"))pTxtMng->GetLine(difficulty);
                levelText.Format("%s%.0f",(LPCSTR)difficulty,row.second);label+=levelText;
            }
            if(row.heading)pSurf->DrawText(label,rc,0,INV_COLOR_HEADER,DT_CENTER);
            else{
                if(row.damageType>=0||key=="infoskill_1")value.Format(row.format,row.value,row.second);else value.Format(row.format,row.value);
                if(!unitLabel.empty()&&unitLabel!="difficulty"){CString text;if(pTxtMng->GetText(CString("string ")+unitLabel.c_str()))pTxtMng->GetLine(text);value+=text;}
                pSurf->DrawText(CString("     ")+label,rc,0,color,DT_LEFT);pSurf->DrawText(value,rc-CPoint(10,0),0,color,DT_RIGHT);
            }
            rc+=shift;
        }
    };

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
                         mod && shown?primary.healthPercent:int(pSkillInfo->pStats->GetHealthMultiplier() * 100));
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
            szOut.Format("%s %d", szSkillStr[ENCUMBRANCE],
                         mod && shown?mod->CampSkillCapacity(*pScreen->GetCurrentPers(),pSkillInfo->pUnit):int(pSkillInfo->pStats->GetMaxEncumbrance()));
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
                if (race && mod)
                {
                    const bool accurate = mod->CombatSetting("JMVRegenerationTooltip") != 0;
                    CUnit *regenUnit = accurate && regenServer && commonServer ? serverUnit : pSkillInfo->pUnit;
                    const int magicBloodLevel = accurate
                        ? min(3, int(regenUnit->GetUserUnitStats()->cPerks[27]))
                        : thePerkDatabase.GetPerkIndex(thePerkDatabase.GetPerk(27));
                    const float magicBloodBonus =
                        float(mod->InfoMagicBlood(magicBloodLevel));
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
                        ? mod->InfoComplex(*regenUnit, 10) * 0.01f : 0.0f;
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
                         mod && shown?primary.staminaPercent:int(pSkillInfo->pStats->GetStaminaMultiplier() * 100));
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
            szOut.Format("%s %d", szSkillStr[ACTIONS], int(pSkillInfo->pStats->GetActionPoints()));
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
            if(full)drawRows(display.mpRows);
            rc += shift;
            break;
        case 7: // encumbrance: native loss breakdown
            if(full)drawRows(display.overloadRows);
            rc+=shift;
            break;
        case 2: // int
            szOut.Format("%s %d%%", szSkillStr[BONUSCP],
                         mod && shown?primary.experiencePercent:int(pSkillInfo->pStats->GetExpMultiplier() * 100));
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
            szOut.Format("%s %d%%", szSkillStr[BONUSMANA],
                         mod && shown?primary.manaPercent:int(pSkillInfo->pStats->GetManaBonusMultiplier() * 100));
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
            if(mod && shown) {
                szTmp="string defense";if(pTxtMng->GetText(szTmp))pTxtMng->GetLine(szTmp);
                szOut.Format("%s %d",(LPCSTR)szTmp,int(pSkillInfo->pStats->GetAutoSkill(eSkill)));
                pSurf->DrawText(szOut,rc,0,color,DT_LEFT);rc+=shift;
                szOut.Format("%s %.0f%%",(LPCSTR)szSkillStr[DAMAGEMULT],mod->InfoPerkBonus("GetSkillMod",shown->cAutoSkills[eSkill])*100.0f);
                pSurf->DrawText(szOut,rc,0,color,DT_LEFT);rc+=shift;
            }
            break;
        case RPGASKL_BACKSTAB:
            szOut.Format("%s %d%%", szSkillStr[DAMAGEMULT],
                         int((full?display.backstab:pSkillInfo->pStats->GetBaseBackstabDamageMultiplier()) * 100));
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
            break;
        case RPGASKL_ELEMENTAL:
        case RPGASKL_SENSE:
        case RPGASKL_ASTRAL:
            if(mod && shown) {
                const int intelligence=int(shown->cIntelligence)-25;
                if(intelligence>0) {
                    szTmp="PERK Intelligence";if(pTxtMng->GetText(szTmp))pTxtMng->GetLine(szTmp);
                    szOut.Format("%s +%d",(LPCSTR)szTmp,intelligence);
                    pSurf->DrawText(szOut,rc,0,color,DT_LEFT);rc+=shift;
                }
                rc+=shift;
                int label=MSLFIRE;
                for(int subtype=0;subtype<RPG_NUM_MAGIC_SUBTYPES;++subtype) {
                    if(subtype==RPGMST_DIVINATION||subtype==RPGMST_HEALING||subtype==RPGMST_DOMINATION)continue;
                    const int skill=ModCampStats::MagicSkill(*pSkillInfo->pStats,shown->cIntelligence,subtype);
                    szOut.Format(" %s",(LPCSTR)szStrings[label++]);pSurf->DrawText(szOut,rc,0,color,DT_LEFT);
                    szOut.Format("%d",skill);pSurf->DrawText(szOut,rc,0,color,DT_RIGHT);rc+=shift;
                    szOut.Format("  %s",(LPCSTR)szSkillStr[DAMAGEMULT]);pSurf->DrawText(szOut,rc,0,color,DT_LEFT);
                    szOut.Format("%.0f%%",ModCampStats::MagicDamagePercent(skill));pSurf->DrawText(szOut,rc,0,color,DT_RIGHT);rc+=shift;
                }
                rc+=shift;
            }
            break;
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
                         mod && shown?ModCampStats::ScienceSkill(*shown):int(pSkillInfo->pStats->GetManualSkill(eSkill)));
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
            if (!pPerk || (mod && !pPerk->bActive))
                continue;
            if (pTxtMng->GetText(CString("perk ") + pPerk->szCode))
                pTxtMng->GetLine(szName);
            pSurf->DrawText(szName, rc, 0, color, DT_LEFT);
            if (pSkillInfo->nSkill > j)
                pSurf->DrawText(szSkillStr[PERKTAKEN], rc, 0, color, DT_RIGHT);
            else if(mod){
                const std::string missing=mod->CampMissingRequirement(*pSkillInfo->pStats,pPerk->szCode);
                if(!missing.empty()){
                    CString code=missing.c_str(),name;const CString title=CString("perk ")+code.Left(code.GetLength()-1)+"0";
                    if(pTxtMng->GetText(title))pTxtMng->GetLine(name);else name=code;
                    szOut.Format("%s %c",(LPCSTR)name,code[code.GetLength()-1]);
                    pSurf->DrawText(szOut,rc,0,RGB(255,15,15),DT_RIGHT);
                }
            }
            rc += shift;

            // EFFECT
            if(mod) {
                const auto rows=ModCampStats::PerkRows(nPerkGroup,int(pPerk->dwModifier),mod->CombatSetting("JMV136")!=0,
                    mod->CombatSetting("JMVAnatomyLimbDamage")!=0,theAIGeneralSettings.fpBackstabAdd);
                for(size_t rowIndex=0;rowIndex<rows.size();++rowIndex) {
                    const auto& row=rows[rowIndex];CString label;
                    if(strcmp(row.label,"ATTRIBPLUS")==0)label=szSkillStr[ATTRIBPLUS];
                    else if(strcmp(row.label,"DAMAGEMULT")==0)label=szSkillStr[DAMAGEMULT];
                    else if(strcmp(row.label,"ACTIONS")==0)label=szSkillStr[ACTIONS];
                    else if(strcmp(row.label,"SPELLBONUS")==0)label=szSkillStr[SPELLBONUS];
                    else if(strcmp(row.label,"HEALTH")==0)label=szSkillStr[HEALTH];
                    else if(strcmp(row.label,"STAMINA")==0)label=szSkillStr[STAMINA];
                    else if(strcmp(row.label,"STAMINARGNR")==0)label=szSkillStr[STAMINARGNR];
                    else if(strcmp(row.label,"ENCUMBRANCE")==0)label=szSkillStr[ENCUMBRANCE];
                    else if(strcmp(row.label,"BSTABMULT")==0)label=szSkillStr[BSTABMULT];
                    else if(strcmp(row.label,"TOHITDEFENSE")==0){if(pTxtMng->GetText("string infoskill_25"))pTxtMng->GetLine(szTmp);label=szSkillStr[TOHITBONUS]+CString("/")+szTmp;}
                    else if(strcmp(row.label,"ANATOMYLIMBS")==0)label="\xD3\xE4\xE0\xF0 \xE2 \xEA\xEE\xED\xE5\xF7\xED\xEE\xF1\xF2\xE8";
                    else if(pTxtMng->GetText(CString("string ")+row.label))pTxtMng->GetLine(label);
                    CString format=CString("     ")+row.format;szOut.Format((LPCSTR)format,(LPCSTR)label,row.value);
                    if(strcmp(row.label,"infoskill_29")==0){CString meters;if(pTxtMng->GetText("string infounit_13"))pTxtMng->GetLine(meters);szOut+=meters;}
                    pSurf->DrawText(szOut,rc,0,color,DT_LEFT);if(rowIndex+1<rows.size())rc+=shift;
                }
            } else {
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
        pSurf->DrawText(szStrings[ABSORPARMOR], rc, 0, mod?INV_COLOR_HEADER:color, mod?DT_CENTER:DT_LEFT);
        rc += shift;

        float fpAbsorbtions[RPG_NUM_ATTACK_TYPES];
        for (int i = 0; i < RPG_NUM_ATTACK_TYPES; i++)
        {
            fpAbsorbtions[i] = 0;
            for (int j = 0; j < RPG_NUM_ARMOR_LAYERS; j++)
                fpAbsorbtions[i] +=
                    mod?ModCampDescriptions::Absorption(pSkillInfo->pStats->hls[pSkillInfo->eHL].layers[j].fpAbsorbtions[i],i,
                        pSkillInfo->pStats->GetPerkModifier(5),pSkillInfo->pStats->GetPerkModifier(22)):
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

    if (pSkillInfo->nType == 5 && full){
        pSurf->DrawText(szStrings[pSkillInfo->szNonlocalName=="defense"?MODDEFENSE:MODATTACK],rc,0,INV_COLOR_HEADER,DT_CENTER);rc+=shift;rc+=shift;
        drawRows(pSkillInfo->szNonlocalName=="defense"?display.defenseRows:display.attackRows);rc+=shift;
        pSurf->DrawMultiText(szDesc,rc,9,0,color,DT_CENTER|DT_WORDBREAK,TRUE,15);
    }
    else if (pSkillInfo->nType == 5)
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
        if(mod){pSurf->DrawText(szStrings[MAGICSCHOOL],rc,0,INV_COLOR_HEADER,DT_CENTER);rc+=shift;}
        const int subtypeCount = RPG_NUM_MAGIC_SUBTYPES;
        for (int i = 0; i < subtypeCount; i++)
        {
            ERPGMagicSubtypes subtype = (ERPGMagicSubtypes)i;
            if (subtype == RPGMST_DIVINATION || subtype == RPGMST_HEALING ||
                subtype == RPGMST_DOMINATION)
                continue;
            szOut.Format("     %s", szStrings[nStr++]);
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            float knowledge = pSkillInfo->pStats->GetMagicSkill(subtype);
            if (mod && pSkillInfo->pUnit)
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

    if(pSkillInfo->nType==7 && pSkillInfo->szNonlocalName=="camp_damage" && full){
        pSurf->DrawText(szStrings[DAMAGE],rc,0,INV_COLOR_HEADER,DT_CENTER);rc+=shift;
        drawRows(display.damageRows);
    }else if (pSkillInfo->nType == 7)
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
