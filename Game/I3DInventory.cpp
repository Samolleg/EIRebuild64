#include "StdAfx.h"
#include "model_draw.h"
#include "OwnedRender/ModelPreviewContext.h"
#include "model_draw_lifetime.h"
#include "OwnedRender/SinglePreview.h"
#include "OwnedRender/E4Camp.h"
#include "GameJournal.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "I3DMain.h"
#include "dx11/RendererDX11.h"
#include "I3DInventory.h"
#include "TextManager.h"
#include "FigureManager.h"
#include "TextureManager.h"
#include "ModRuntime.h"
#include "ModOutOfCombatRegeneration.h"
#include "ModPackage.h"
#include "ModItemUiPolicy.h"
#include "ModCampStats.h"
#include "ModStatRules.h"
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
#include "OwnedRender/PreviewComposition.h"
#include "RandomGen.h"
#include "Party.h"
#include "GameMain.h"
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
        pTex->GetSurface()->UpdateTexture(0, &region, &data, MMP_FORMAT_5551);
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

    const float previewTime=(timeGetTime()/1024.0f)*15;
    OwnedRender::ModelPreviewTimeScope clock(owned::ModelPreviewClock::inventory,previewTime);
    const auto composition=OwnedRender::inventory_model_preview(pDrawUnit->GetID(),fX,fAngle,bArmors,VIEWING_ANGLE_TAN);
    OwnedRender::produce_model_preview_composition(composition,
        [&](auto& rect,bool local){SourcePreviewRect(pI3D,rect,local);},
        [&](const auto* rect,unsigned count,float depth){SourcePreviewDepth(pI3D->GetRenderer(),rect,count,depth);},
        SourcePreviewQuaternion,
        [&]{pDrawUnit->SetExactPosition(previewTime);},
        [&]{
        const bool camp=OwnedRender::E4::OwnsDraw();
        std::uint64_t before=0,after=0;
        if(camp)pRend->GetD3D()->GetRendererFacade().Visit([&](owned::Renderer& renderer){before=renderer.native_logical_draws();return S_OK;});
        owned::ModelDrawScope previewScope(owned::ModelDrawDomain::preview);
        const BOOL drawn=pDrawUnit->DrawWithoutBBox(pRend);
        if(camp){
            const bool flushed=pRend->GetD3D()->FlushC56CampFigures();
            pRend->GetD3D()->GetRendererFacade().Visit([&](owned::Renderer& renderer){after=renderer.native_logical_draws();return S_OK;});
            static std::map<int,unsigned> reported;
            const unsigned status=(drawn?1u:0u)|(flushed?2u:0u)|(after>before?4u:0u)|(pDrawUnit->CheckFlag(OBJ_HIDDEN)?8u:0u);
            auto found=reported.find(pDrawUnit->GetID());
            if(found==reported.end()||found->second!=status){
                reported[pDrawUnit->GetID()]=status;
                GameJournal::Event("RENDER","camp_unit_preview","id=%d draw=%d flush=%d hidden=%d submissions=%llu",pDrawUnit->GetID(),int(drawn),int(flushed),int(pDrawUnit->CheckFlag(OBJ_HIDDEN)!=0),static_cast<unsigned long long>(after-before));
            }
        }
        },
        [&]{if(OwnedRender::E4::OwnsDraw())pRend->GetD3D()->FlushC56CampFigures();});
}
/////////////////////////////////////////////////////////////////////////////////////
static const D3DCOLORVALUE dcvMagicColors[] = { { 1, 0, 0, 1 }, { 0, 1, 1, 1 }, { 0, 1, 0, 1 },
                                                { 1, 1, 0, 1 }, { 1, 1, 0, 1 }, { 1, 0, 1, 1 },
                                                { 1, 0, 1, 1 }, { 0, 0, 1, 1 } };
#include "OwnedRender/CampItemModel.inl"
#include "OwnedRender/SinglePreview.inl"
void CI3DItemScreen::BeginCampPreviews(int shop)
{
    owned::ResourceStoreConfig mounts;
    std::uint64_t generation=0;
    const bool available=owned::owned_resource_runtime_mounts(mounts,&generation);
    const unsigned cellPixels=static_cast<unsigned>((pI3D->GetHeight()+5)/6);
    if(!available || !pI3D->lastCampModels || shop!=pI3D->campModelShop ||
       generation!=pI3D->campModelGeneration || pI3D->lastCampModels->cellPixels!=cellPixels) {
        pI3D->lastCampModels=std::make_shared<CampItemModelCache>();
        pI3D->lastCampModels->cellPixels=cellPixels;
        pI3D->campModelShop=shop;pI3D->campModelGeneration=generation;
    }
    campItemModels=pI3D->lastCampModels;
    campItemModels->cache.begin();
}
void CI3DItemScreen::FinishCampPreviews()
{
    const auto started=GetTickCount64();
    const auto count=campItemModels->cache.finish();
    GameJournal::Event("SHOP","preview_stock_ready","keys=%zu final_wait_ms=%llu",count,GetTickCount64()-started);
}
void CI3DItemScreen::PreloadCampParty(const CParty& party)
{
    if(!campItemModels) campItemModels=std::make_shared<CampItemModelCache>();
    const auto queue=[&](const auto& items) {
        for(int i=0;i<items.GetSize();++i) if(items[i]) campItemModels->find(*items[i],true);
    };
    for(int i=0;i<party.people.GetSize();++i) {
        queue(party.people[i].weapons);
        queue(party.people[i].armors);
    }
    queue(party.allWagonItems);
}
void CI3DItemScreen::DrawItem(CRenderer *pRend, SDrawItem &item, std::vector<std::string>* visibleLegacyIcons)
{
    if (!item.pItem) return;
    owned::camp::Composition campPlacement;
    const bool campPlaced = E4CampAccess::ItemPlacement(this, item, campPlacement);
    owned::camp::VisualScope campVisual(campPlaced ? campPlacement : owned::camp::visual, campPlaced || owned::camp::composing);
    CampPreviewSelection<CampItemModel> selection{nullptr,true};
    if (dynamic_cast<CI3DScreenCamp*>(this)) {
        if (!campItemModels) campItemModels=std::make_shared<CampItemModelCache>();
        selection=campItemModels->find(*item.pItem);
    }
    owned::ModelDrawReference figureLifetime;
    CI3DFigure* pFig=nullptr;
    std::shared_ptr<const owned::PreparedCampIcon> campIcon;
    const auto& model=selection.model;
    if (selection.useFigure) {
        if (!figuresLRU.Contains(item.pItem->GetI3DFigureName())) OwnedRender::E4::FigureMiss();
        pFig = figuresLRU.GetObject(item.pItem->GetI3DFigureName(), [&](CI3DFigure* figure) {
            figureLifetime.hold(figure,
                [](void* value) noexcept { static_cast<CI3DFigure*>(value)->AddRef(); },
                [](void* value) noexcept { static_cast<CI3DFigure*>(value)->Release(); });
        });
        CTexture *pTex = nullptr;
        if (OwnedRender::E4::OwnsDraw()) {
            const auto textureName=item.pItem->GetI3DTextureName();
            if(visibleLegacyIcons) visibleLegacyIcons->emplace_back(LPCSTR(textureName));
            campIcon = owned::camp_inventory_request_icon(textureName,pRend->GetD3D()->GetDevice().Get());
            if (!pFig || !campIcon) return; // Pending or rejected icons are omitted; never use OpenEI.
        } else {
            OwnedRender::E4::MissCost textureMiss(!texturesLRU.Contains(item.pItem->GetI3DTextureName()));
            pTex = texturesLRU.GetObject(item.pItem->GetI3DTextureName());
            if (!pFig || !CTextureLRUAllocator::EnsureReady(pTex)) return;
        }
        pFig->SetTexture(pTex);
        pFig->SetMaterial(pMat1);
    }
    if (item.pItem->GetUserData() == 0)
    {
        CRotateItem *pInfo = dbgnew CRotateItem;
        pInfo->fAngle = 0;
        pInfo->fSpeed = 0;
        pInfo->bIncludePrice = false;
        pInfo->fFlashVal = random.GetFloat(0, FP_2PI);
        item.pItem->AttachUserData(pInfo);
    }
    const auto* itemMod = ModRuntime::Current();
    const bool nativeItemGraphics = itemMod && itemMod->CombatSetting("JMVGraphics2");
    const auto type = item.pItem->GetTypeID();
    const bool flatInstruction = nativeItemGraphics
        ? type == ITID_ITEM_LOOT &&
          static_cast<CItemLoot*>(item.pItem)->GetLootType() == IDBLIT_INSTRUCTION &&
          static_cast<CItemLoot*>(item.pItem)->GetInstructionType() == IDBINS_QUICK
        : IsItemModifier(item.pItem);
    OwnedRender::SinglePreviewFacts facts;
    facts.x=item.fX;facts.y=item.fY;facts.z=item.fZ;facts.size=item.fPixSize;
    facts.viewing_tangent=VIEWING_ANGLE_TAN;
    facts.slot=item.eFrom!=ISP_UNKNOWN;
    facts.elapsed=facts.slot?GetFrameTime():0;
    facts.hovered=item.pItem==pMouseItem;
    facts.flat=IsItemSpell(item.pItem)||IsItemPrototype(item.pItem)||flatInstruction;
    facts.weapon=type==ITID_ITEM_WEAPON && !(nativeItemGraphics &&
        static_cast<CItemWeapon*>(item.pItem)->GetWeaponType()==RPGWST_HANDS1);
    facts.instruction=IsItemInstruction(item.pItem);
    facts.gray_in_front=IsItemPrototype(item.pItem)||IsItemModifier(item.pItem)||IsItemSpell(item.pItem);
    facts.pulse=item.pItem->GetSpellsCount()!=0&&(type==ITID_ITEM_WEAPON||type==ITID_ITEM_ARMOR);
    if (facts.pulse) {
        if (const auto* spell=item.pItem->GetSpell(0)) {
            const auto subtype=spell->GetMagicSubtype();
            if (subtype<RPGMST_UNKNOWN) {
                const auto& color=dcvMagicColors[subtype];
                facts.valid_palette=true;facts.palette={color.r,color.g,color.b};
            }
        }
    }
    if (!facts.slot && model) {
        facts.bounds_max=model->cardMax;facts.bounds_center=model->cardCenter;
    } else if (!facts.slot && pFig) {
        const auto* bounds=pFig->GetBoundingVolume();
        facts.bounds_max={bounds->ptMax.x,bounds->ptMax.y};
        facts.bounds_center={bounds->ptCenter.x,bounds->ptCenter.y};
    }
    if (!pFig && !model) facts.bounds_max={1,1};
    if (IsItemSpell(item.pItem)) {
        if (const auto* spell=item.pItem->GetSpell(0)) facts.rune_count=spell->GetRunesCount();
    }
    auto* info=static_cast<CRotateItem*>(item.pItem->GetUserData());
    OwnedRender::SinglePreviewRotationState rotation{info->fAngle,info->fSpeed,info->fFlashVal};
    SourceSinglePreview adapter{this,pRend,pFig,textures[2],textures[1]};
    adapter.item=&item;adapter.model=model.get();adapter.state=info;adapter.campPlaced=campPlaced;adapter.icon=campIcon;
    OwnedRender::produce_single_item_preview(facts,rotation,adapter);
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

    owned::ModelDrawReference figureLifetime;
    CI3DFigure *pFig = figuresLRU.GetObject(szFigureName, [&](CI3DFigure* figure) {
        figureLifetime.hold(figure,
            [](void* value) noexcept { static_cast<CI3DFigure*>(value)->AddRef(); },
            [](void* value) noexcept { static_cast<CI3DFigure*>(value)->Release(); });
    });
    CTexture *pTex = nullptr;
    std::shared_ptr<const owned::PreparedCampIcon> headIcon;
    if (!pRend->GetD3D()->GetDevice()) {
        headIcon = owned::camp_inventory_request_icon(szTextureName, nullptr);
        if (!pFig || !headIcon) return;
    } else {
        pTex = texturesLRU.GetObject(szTextureName);
        if (!pFig || !CTextureLRUAllocator::EnsureReady(pTex)) return;
    }
    pFig->SetTexture(pTex);
    pFig->SetMaterial(pMat1);
    SourceSinglePreview adapter{this,pRend,pFig,textures[2],textures[1]};
    adapter.icon = std::move(headIcon);
    OwnedRender::produce_single_head_preview(head.fX,head.fY,head.fZ,head.fPixSize,
        VIEWING_ANGLE_TAN,adapter);
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
    owned::camp::frontend::slot_click(pItem,nSlotType,int(ePartType),
        [&](CItem* item,int){pScreen->ClickOnItem(item,ePartType);},
        [&](int slot,int){pScreen->ClickOnSlot(slot,ePartType);});
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
    if (E4CampAccess::CardDraw(this, pRend)) return TRUE;
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
    E4CampAccess::Content publication(this,owned::camp::frontend::HoverKind::item,pNewItem!=nullptr);
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
    E4CampAccess::Text(this);
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
    static_cast<CItemLoot *>(GetItem())->ValidateInstruction();
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

    CItemLoot *pLoot = static_cast<CItemLoot *>(pItem);
    const SIDBItemGeneralShape *pShape = pLoot->GetInstructionShape();
    assert((pLoot->GetInstructionType() >= IDBINS_WEAPON) &&
           (pLoot->GetInstructionType() <= IDBINS_QUICK));
    switch (pLoot->GetInstructionType())
    {
    case IDBINS_WEAPON:
        nType = INSTR_WEAPON;
        break;
    case IDBINS_ARMOR:
        nType = INSTR_ARMOR;
        break;
    case IDBINS_QUICK:
        nType = INSTR_WAND;
        break;
    default:
        throw std::runtime_error("unexpected physical instruction type");
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
void CI3DObjectItemInfo::SetModSkill(SCampSkillMouseInfo* pSkillInfo)
{
    const auto Log = [](float value, float base) { return static_cast<float>(log(value) / log(base)); };
	SetItem(0);
	if (!pSkillInfo || pSkillInfo->nType < 0)
		return;
#ifndef NOGETDC
	CI3DSurface* pSurf = surfaces[0];
	pSurf->GetDC();
    auto* mod = ModRuntime::Current();
    CPersonage* pPers = pScreen ? pScreen->GetCurrentPers() : nullptr;
    CUnit* pUnit = pSkillInfo->pUnit;
    if (!mod || !pPers || !pUnit || !pSkillInfo->pStats) { pSurf->ReleaseDC(); return; }
    int nWeight = int(pPers->GetItemsWeight());
    int nEncumbr = mod->CampSkillCapacity(*pPers, pUnit);
    CUnit* servUnit = theWorldServer.GetUnit(pUnit->GetID());
	CTextManager* pTxtMng = theWorld.GetTextManager();
	CString szTmp, szTmp2, szOut;
	szTmp = "Unknown";
	int backs = 0;
	CRect rc(15, 10, 185, 25);
	CPoint shift(0, 15);
	COLORREF color = INV_COLOR_INFO;
	COLORREF colorH = INV_COLOR_HEADER;
	CString szName, szDesc;
	switch (pSkillInfo->nType)
	{
	case 0: {
		szName = CString("perk ") + pSkillInfo->szNonlocalName ;
		if (pTxtMng->GetText(szName.GetString() ) ) pTxtMng->GetLine(szName);
		CString descKey = pSkillInfo->szNonlocalName + "_desc";
        if (pTxtMng->GetText(CString("perk ") + descKey)) {
            pTxtMng->GetUntilNextCommand(szDesc); szDesc.TrimRight("\r\n"); pTxtMng->CloseText();
        } else szDesc = descKey;
		break;
	}
	case 1:
	case 2:
	case 3: {
		ERPGAutoSkills eSkill = (ERPGAutoSkills)pSkillInfo->nSkill;
		if (eSkill != 4 && eSkill != 5) {
			if (pTxtMng->GetText(CString(CString("perk ") + pSkillInfo->szNonlocalName).GetString()))
			{
				pTxtMng->GetLine(szName);
				pTxtMng->GetLine(szDesc);
			}
		}
		break;
	}
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	{
		if (pTxtMng->GetText(pSkillInfo->szNonlocalName.GetString())) pTxtMng->GetLine(szName);
		pTxtMng->GetSystemString(CString(pSkillInfo->szNonlocalName + "_desc").GetString(), szDesc);
		break;
	}
	}
	if(szName != ""){
		pSurf->DrawMultiText(szName.GetString(), rc, 2, 0, colorH, DT_CENTER | DT_WORDBREAK, TRUE, 0);
		rc += shift;
	}
	if (pSkillInfo->nType == 0 && pSkillInfo->nSkill == 7)
	{
		rc += shift;
		int nVal;
		if (pTxtMng->GetText("string infoskill_1")) pTxtMng->GetLine(szTmp);
		szOut.Format("     %s", szTmp);
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		szOut.Format("%d/%d", nWeight, nEncumbr);
		pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
		rc += shift;
		if (pTxtMng->GetText("string infoitem_39")) pTxtMng->GetLine(szTmp);
		szOut.Format("     %s", szTmp);
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		szOut.Format("%d", nVal = (int)mod->InfoDisplayScalar("GetOverloadDefenceOnServer", {float(nWeight), float(nEncumbr)}));
		pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
		rc += shift;
		if (pTxtMng->GetText("string infoitem_38")) pTxtMng->GetLine(szTmp);
		szOut.Format("     %s", szTmp);
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		szOut.Format("%d", nVal = (int)mod->InfoDisplayScalar("GetOverloadAttackOnServer", {float(nWeight), float(nEncumbr)}));
		pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
		rc += shift;
		float enc_mod = 1.0f;
		float overloadDecr = mod->InfoDisplayScalar("GetOverloadDamageOnServer", {float(nWeight), float(nEncumbr)});
		enc_mod = overloadDecr;
		enc_mod = enc_mod - 1;
		enc_mod = enc_mod * 100;
		if (pTxtMng->GetText("string infoitem_15"))
		{
			pTxtMng->GetLine(szTmp);
		}
		szOut.Format("     %s", szTmp);
		pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_LEFT);
		if (enc_mod > 0)
			szOut.Format("+%.0f%%", enc_mod);
		else
			szOut.Format("%.0f%%", enc_mod);
		pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_RIGHT);
		rc += shift;
		float block = 0;
		if(pUnit)
			block = mod->InfoDisplayActor("GetShieldBlockChance", *pUnit);
		overloadDecr = mod->InfoDisplayScalar("GetOverloadBlockOnServer", {float(nWeight), float(nEncumbr)});
		enc_mod = block * overloadDecr - block;
		if (pTxtMng->GetText("string infoitem_45"))
		{
			pTxtMng->GetLine(szTmp);
		}
		szOut.Format("     %s", szTmp);
		pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_LEFT);
		if (enc_mod > 0)
			szOut.Format("+%.0f%%", enc_mod);
		else
			szOut.Format("%.0f%%", enc_mod);
		pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_RIGHT);
		rc += shift;
		float parry = 0;
		float parry2 = 0;
		CUnit* servUnit = theWorldServer.GetUnit(pUnit->GetID());
		if(servUnit)
			if (int(servUnit->GetCurrentWeaponSlotIndex()) != -1 && int(servUnit->GetCurrentWeaponSlotIndex()) >=0 && int(servUnit->GetCurrentWeaponSlotIndex()) < 4) {
				int left = ModStats::LeftHand(*servUnit);
				if (left == -1) {
					float clrParry = mod->InfoDisplayActor("GetWeaponParryChance", *servUnit, int(servUnit->GetCurrentWeaponSlotIndex())) * 0.01;
					float overload = mod->InfoDisplayScalar("GetOverloadParryOnServer", {float(nWeight), float(nEncumbr)});
					parry = clrParry;
					parry = clrParry;
				}
				else {
					parry = mod->InfoDisplayActor("GetWeaponParryChance", *servUnit, int(servUnit->GetCurrentWeaponSlotIndex())) * 0.01 - mod->InfoDisplayActor("GetWeaponParryChance", *servUnit, int(servUnit->GetCurrentWeaponSlotIndex())) * mod->InfoDisplayScalar("GetOverloadParryOnServer", {float(nWeight), float(nEncumbr)}) * 0.01;
					parry2 = mod->InfoDisplayActor("GetWeaponParryChance", *servUnit, left) * 0.01 - mod->InfoDisplayActor("GetWeaponParryChance", *servUnit, left) * mod->InfoDisplayScalar("GetOverloadParryOnServer", {float(nWeight), float(nEncumbr)}) * 0.01;
					if (parry < 0.0001f) {
						if (parry2 < 0.0001f)
							parry2 = parry * 100;
						parry = parry2 * 100;
					}
					else {
						if (parry2 < 0.0001f)
							parry2 = parry * 100;
						else
							parry = (parry + parry2 - 2 * (parry * parry2)) * 100;
					}
				}
			}
		else
			if (pUnit->GetQWeapon(pUnit->GetCurrentWeaponSlotIndex()) && pUnit->GetQWeapon(pUnit->GetCurrentWeaponSlotIndex())->GetShapeEntry()) {
				int left = ModStats::LeftHand(*pUnit);
				if (left == -1) {
					float clrParry = mod->InfoDisplayActor("GetWeaponParryChance", *pUnit, int(pUnit->GetCurrentWeaponSlotIndex())) * 0.01;
					float overload = mod->InfoDisplayScalar("GetOverloadParryOnServer", {float(nWeight), float(nEncumbr)});
					parry = clrParry;
					parry = clrParry;
				}
				else {
					parry = mod->InfoDisplayActor("GetWeaponParryChance", *pUnit, int(pUnit->GetCurrentWeaponSlotIndex())) * 0.01 - mod->InfoDisplayActor("GetWeaponParryChance", *pUnit, int(pUnit->GetCurrentWeaponSlotIndex())) * mod->InfoDisplayScalar("GetOverloadParryOnServer", {float(nWeight), float(nEncumbr)}) * 0.01;
					parry2 = mod->InfoDisplayActor("GetWeaponParryChance", *pUnit, left) * 0.01 - mod->InfoDisplayActor("GetWeaponParryChance", *pUnit, left) * mod->InfoDisplayScalar("GetOverloadParryOnServer", {float(nWeight), float(nEncumbr)}) * 0.01;
					if (parry < 0.0001f) {
						if (parry2 < 0.0001f)
							parry2 = parry * 100;
						parry = parry2 * 100;
					}
					else {
						if (parry2 < 0.0001f)
							parry2 = parry * 100;
						else
							parry = (parry + parry2 - 2 * (parry * parry2)) * 100;
					}
				}
			}
		if (pTxtMng->GetText("string infoitem_47"))
		{
			pTxtMng->GetLine(szTmp);
		}
		szOut.Format("     %s", szTmp);
		pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_LEFT);
		if (enc_mod > 0)
			szOut.Format("+%.1f%%", -parry);
		else
			szOut.Format("%.1f%%", -parry);
		pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_RIGHT);
		rc += shift;
		rc += shift;
	}
	int leftWeapon = ModStats::LeftHand(*pUnit);
	if (leftWeapon != -1) {
		CItemWeapon* leftWeaponItem = pUnit->GetQWeapon(leftWeapon);
		if (leftWeaponItem == NULL) {
			rc += shift;
		}
	}
	if (pSkillInfo->nType == 0)
	{
		switch (pSkillInfo->nSkill)
		{
		case 0: {
			float healthMult = 25 / theAIGeneralSettings.fMPVal1;
			if (pUnit)
				healthMult = pUnit->GetUserUnitStats()->cStrength / theAIGeneralSettings.fHPVal1;
			szOut.Format("%s %d%%", szSkillStr[HEALTHMULT],
				int(healthMult * 100));
			pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
			rc += shift;
			szOut.Format("%s %d", szSkillStr[ENCUMBRANCE],
				nEncumbr);
			pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
			rc += shift;
			if (pUnit) {
				const SUDBRaceModel* pRace = pUnit->GetRaceModel();
				const bool accurateRegenTooltip = mod->CombatSetting("JMVRegenerationTooltip");
				CUnit* regenUnit = accurateRegenTooltip && servUnit ? servUnit : pUnit;
				const int magicBloodArgument = accurateRegenTooltip
					? min(3, int(regenUnit->GetUserUnitStats()->cPerks[27]))
					: thePerkDatabase.GetPerkIndex(thePerkDatabase.GetPerk(27));
				const float magicBloodBonus =
					float(mod->InfoMagicBlood(magicBloodArgument));
				float JewelryRegeneration = 0;
				for (int i = 0; i < pPers->items.GetSize(); i++) {
					if (pPers)
						if (pPers->items.GetSize() != 0)
							if (pPers->items[i]) {
								CString q_part = pPers->items[i]->GetShapeEntry()->szName;
								JewelryRegeneration += mod->InfoProperty("GetJewelryPropsInside", q_part.GetString(), 14) * 0.01f;
							}
				}
				float ArmorRegeneration = 0;
				if (accurateRegenTooltip) {
					for (int i = 0; i < RPG_NUM_ARMOR_TYPES; i++) {
						if (regenUnit->GetQArmor(i)) {
							CString armorMat = ModInfoMaterialName(regenUnit->GetQArmor(i));
							ArmorRegeneration += mod->InfoProperty("GetMaterialPropsInside", armorMat.GetString(), 2) * 0.01f;
						}
					}
				}
				else {
					for (int i = 0; i < pPers->armors.GetSize(); i++) {
						if (pPers)
							if (pPers->armors.GetSize() != 0)
								if (pPers->armors[i]) {
									CString armorMat = ModInfoMaterialName(pPers->armors[i]);
									ArmorRegeneration += mod->InfoProperty("GetMaterialPropsInside", armorMat.GetString(), 2) * 0.01f;
								}
					}
				}
				float hp_regen;
				float result;
				float fullresult;
				if (accurateRegenTooltip) {
					const float complexRegeneration =
						mod->InfoComplex(*regenUnit, 10) * 0.01f;
					hp_regen = pRace->fpRegenerateHP *
						(magicBloodBonus + JewelryRegeneration + ArmorRegeneration +
						 complexRegeneration);
					const float maxHp = max(1.0f, float(regenUnit->GetMaxHP()));
					const float safeMultiplier = ModOutOfCombatRegeneration::GetMultiplier(regenUnit);
					const float regenSpell = hp_regen > 0.0f
						? regenUnit->GetEffectValue(SPELL_ID_REGENERATION) / 30.0f
						: 0.0f;
					result = maxHp * hp_regen * safeMultiplier + regenSpell;
					fullresult = result > 0.000001f ? maxHp / result : 0.0f;
				}
				else {
					hp_regen = pRace->fpRegenerateHP *
						(magicBloodBonus + JewelryRegeneration + ArmorRegeneration);
					const float Health = 1 + pSkillInfo->pStats->GetPerkModifier(2) * 0.01f;
					const float hp = 30.0f * powf(1.10f, Log(pSkillInfo->pStats->fpExperience / 500.0f, 1.61f));
					const float strength = pUnit->GetUserUnitStats()->cStrength;
					const float hpresult = (hp * (strength / 25.0f) - 5) * Health;
					result = hpresult * hp_regen;
					fullresult = hpresult / result;
				}
				if (pTxtMng->GetText("string infoskill_50")) pTxtMng->GetLine(szTmp);
				szOut.Format("%s", szTmp);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_CENTER);
				rc += shift;
				if (pTxtMng->GetText("string infoskill_45")) pTxtMng->GetLine(szTmp);
				szOut.Format("%s %.4f/100", szTmp, hp_regen*100.0f);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				rc += shift;
				if (pTxtMng->GetText("string infoskill_46")) pTxtMng->GetLine(szTmp);
				if (pTxtMng->GetText("string infoskill_47")) pTxtMng->GetLine(szTmp2);
				szOut.Format("%s %.2f%s", szTmp, result, szTmp2);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				rc += shift;
				if (pTxtMng->GetText("string infoskill_51")) pTxtMng->GetLine(szTmp);
				if (pTxtMng->GetText("string infoskill_49")) pTxtMng->GetLine(szTmp2);
				szOut.Format("%s %.1f%s", szTmp, fullresult, szTmp2);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				rc += shift;
			}
			rc += shift;
			break; }
		case 1: {
			float staminamult = 25 / theAIGeneralSettings.fMPVal1;
			if(pUnit)
				staminamult = pUnit->GetUserUnitStats()->cDexterity / theAIGeneralSettings.fMPVal1;
			szOut.Format("%s %d%%", szSkillStr[STAMINAMULT],
				int(staminamult * 100));
			pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
			rc += shift;
			szOut.Format("%s %d", szSkillStr[ACTIONS], int(pSkillInfo->pStats->GetActionPoints()));
			pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
			rc += shift;
			if (pUnit) {
				const SUDBRaceModel* pRace = pUnit->GetRaceModel();
				float Spirit = pSkillInfo->pStats->GetPerkModifier(18) * 0.01f;
				float Magic = pSkillInfo->pStats->GetPerkModifier(23) * 0.01f;
				float JewelryRestoration = 0;
				for (int i = 0; i < pPers->items.GetSize(); i++) {
					if (pPers)
						if (pPers->items[i]) {
							CString q_part = pPers->items[i]->GetShapeEntry()->szName;
							JewelryRestoration += mod->InfoProperty("GetJewelryPropsInside", q_part.GetString(), 15) * 0.01f;
						}
				}
				float ArmorRestoration = 0;
				for (int i = 0; i < pPers->armors.GetSize(); i++) {
					if (pPers)
						if (pPers->armors.GetSize() != 0)
							if (pPers->armors[i] && reinterpret_cast<uintptr_t>(pPers->armors[i]) != 0x249 && reinterpret_cast<uintptr_t>(pPers->armors[i]) != 0x219) {
								CString armorMat = ModInfoMaterialName(pPers->armors[i]);
								ArmorRestoration += mod->InfoProperty("GetMaterialPropsInside", armorMat.GetString(), 3) * 0.01f;
							}
				}
				float overload = nWeight == 0 ? 0 : float(nWeight) / float(nEncumbr);
				float manacost = 1;
				if (overload >= 1.5)		manacost = 0.01f;
				else if (overload >= 1.25)  manacost = 0.3f;
				else if (overload >= 1.0)   manacost = 0.5f;
				else if (overload >= 0.8)   manacost = 0.8f;
				else if (overload >= 0.65)  manacost = 0.9f;
				float mp_regen = pRace->fpRegenerateMP;
				mp_regen = mp_regen * (1 + JewelryRestoration + ArmorRestoration + Spirit);
				mp_regen = mp_regen * manacost;
				float mp = 30 * powf(1.10f, Log(pSkillInfo->pStats->fpExperience / 500.0f, 1.61f)) - 41.9f;
				float dexterity = pUnit->GetUserUnitStats()->cDexterity;
				float dexterity_modifier = dexterity / 25.0f;
				float perk_modifier = 1 + Magic;
				float mpresult = dexterity_modifier * perk_modifier * mp * 1.2f + 47.0f;
				float result = mpresult * mp_regen;
				float fullresult = mpresult / result;
				if (pTxtMng->GetText("string infoskill_52")) pTxtMng->GetLine(szTmp);
				szOut.Format("%s", szTmp);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_CENTER);
				rc += shift;
				if (pTxtMng->GetText("string infoskill_45")) pTxtMng->GetLine(szTmp);
				szOut.Format("%s %.4f/100", szTmp, mp_regen * 100);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				rc += shift;
				if (pTxtMng->GetText("string infoskill_46")) pTxtMng->GetLine(szTmp);
				if (pTxtMng->GetText("string infoskill_48")) pTxtMng->GetLine(szTmp2);
				szOut.Format("%s %.2f%s", szTmp, result, szTmp2);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				rc += shift;
				if (pTxtMng->GetText("string infoskill_51")) pTxtMng->GetLine(szTmp);
				if (pTxtMng->GetText("string infoskill_49")) pTxtMng->GetLine(szTmp2);
				szOut.Format("%s %.1f%s", szTmp, fullresult, szTmp2);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				rc += shift;
			}
			rc += shift;
			break;
		}
		case 2:
			float intelligenceMult = 1;
			if (pUnit)
				intelligenceMult = mod->InfoPerkBonus("GetExpMultiplier2", int(pUnit->GetUserUnitStats()->cIntelligence));
			szOut.Format("%s %d%%", szSkillStr[BONUSCP],
				int(intelligenceMult * 100));
			pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
			rc += shift;
			float IntelligenceBonusMult = 1;
			if(pUnit)
				IntelligenceBonusMult = (25.0f / pUnit->GetUserUnitStats()->cIntelligence);
			szOut.Format("%s %d%%", szSkillStr[BONUSMANA], int(IntelligenceBonusMult * 100));
			pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
			rc += shift;
			rc += shift;
			break;
		}
		pSurf->DrawMultiText(szDesc.GetString(), rc, 13, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, 0);
	}
	if (pSkillInfo->nType == 1)
	{
		ERPGAutoSkills eSkill = (ERPGAutoSkills)pSkillInfo->nSkill;
		int skill = 0;
		float mskill_mod = 0;
		if (eSkill < 4) {
			int nValue = pSkillInfo->pStats->GetBaseAutoSkill(eSkill);
			szOut.Format("%s %d", szSkillStr[SKILL2], nValue);
			pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
			rc += shift;
			switch (eSkill)
			{
			case RPGASKL_MELEE:
			case RPGASKL_ARCHERY:
				szOut.Format("%s %d", szSkillStr[BASETOHIT],
					int(pSkillInfo->pStats->GetAutoSkill(eSkill)));
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				rc += shift;
				szTmp = CString("string defense");
				if (pTxtMng->GetText(szTmp.GetString())) pTxtMng->GetLine(szTmp);
				szOut.Format("%s %d", szTmp,
					int(pSkillInfo->pStats->GetAutoSkill(eSkill)));
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				rc += shift;
				if(eSkill == RPGASKL_MELEE)
					skill = pUnit->GetUserUnitStats()->cAutoSkills[RPGASKL_MELEE];
				if (eSkill == RPGASKL_ARCHERY)
					skill = pUnit->GetUserUnitStats()->cAutoSkills[RPGASKL_ARCHERY];
				mskill_mod = mod->InfoDisplayScalar("GetSkillMod", {float(skill)});
				szOut.Format("%s %.0f%%", szSkillStr[DAMAGEMULT], mskill_mod * 100.0f);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				rc += shift;
				break;
			case RPGASKL_BACKSTAB:
				szOut.Format("%s %d%%", szSkillStr[DAMAGEMULT],
					int(mod->CampDescriptions(*pPers, *pUnit, true).backstab * 100));
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				rc += shift;
				break;
			case RPGASKL_ELEMENTAL:
			case RPGASKL_SENSE:
			case RPGASKL_ASTRAL:
				int intelligence = int((int)pUnit->GetUserUnitStats()->cIntelligence - 25);
				if (intelligence > 0) {
					szTmp = CString("PERK Intelligence");
					if (pTxtMng->GetText(szTmp.GetString())) pTxtMng->GetLine(szTmp);
					szOut.Format("%s +%d", szTmp, intelligence);
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					rc += shift;
				}
				rc += shift;
				int nStr = MSLFIRE;
				for (int i = 0; i < 3; i++)
				{
					ERPGMagicSubtypes subtype = (ERPGMagicSubtypes)i;
					if (subtype == RPGMST_DIVINATION || subtype == RPGMST_HEALING ||
						subtype == RPGMST_DOMINATION)
						continue;
					int skill = 0;
					int nPerkIndex = RPGPRK_FIRE + subtype;
					float fBaseSkill = pSkillInfo->pStats->GetBaseMagicSkill(subtype);
					fBaseSkill += pSkillInfo->pStats->GetPerkModifier(nPerkIndex);
					skill = fBaseSkill + intelligence;
					szOut.Format(" %s", szStrings[nStr++]);
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					szOut.Format("%d", skill);
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_RIGHT);
					rc += shift;
					float skill_mod = (0.8f + (skill * 30.0f / 120.0f) / 100.0f) * 100.0f;
					szOut.Format("  %s", szSkillStr[DAMAGEMULT]);
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					szOut.Format("%.0f%%", skill_mod);
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_RIGHT);
					rc += shift;
				}
				rc += shift;
				break;
			}
			rc += shift;
			pSurf->DrawMultiText(szDesc.GetString(), rc, 13, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, 0);
		}
	}
	if (pSkillInfo->nType == 2)
	{
		ERPGManualSkills eSkill = (ERPGManualSkills)pSkillInfo->nSkill;
		int nValue = pSkillInfo->pStats->GetBaseManualSkill(eSkill);
		szOut.Format("%s %d", szSkillStr[SKILL2], nValue);
		pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
		rc += shift;
		switch (eSkill)
		{
		case RPGMSKL_SCIENCE:
			if (pTxtMng->GetText("string infoskill_44")) pTxtMng->GetLine(szTmp);
			int dext = (int)pUnit->GetUserUnitStats()->cDexterity - 25;
			int mskill = (int)pUnit->GetUserUnitStats()->cManualSkills[eSkill];
			int fpOurSkill = dext + mskill;
			szOut.Format("%s %d", szTmp, fpOurSkill);
			pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
			rc += shift;
			break;
		}
		rc += shift;
		pSurf->DrawMultiText(szDesc.GetString(), rc, 13, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, 0);
	}
	if (pSkillInfo->nType == 3)
	{
		CString szPerkName(pSkillInfo->szNonlocalName);
		szPerkName.TrimRight("0");
		szPerkName = szPerkName + "1";
		int nPerk = thePerkDatabase.GetPerkIndex(szPerkName.GetString());
		int nPerkGroup = nPerk / 3;
		for (int j = 0; j < 3; j++)
		{
			const SPDBPerk* pPerk = thePerkDatabase.GetPerk(nPerkGroup * 3 + j);
			if (!pPerk)
				continue;
			if (!pPerk->bActive)
				continue;
			if (pTxtMng->GetText(CString(CString("perk ") + pPerk->szCode).GetString()))
				pTxtMng->GetLine(szName);
			pSurf->DrawText(szName.GetString(), rc, 0, color, DT_LEFT);
			if (pSkillInfo->nSkill > j)
				pSurf->DrawText(szSkillStr[PERKTAKEN].GetString(), rc, 0, color, DT_RIGHT);
            else {
                const std::string missing = mod->CampMissingRequirement(pPers->stats, pPerk->szCode);
                if (!missing.empty()) {
                    const int index = thePerkDatabase.GetPerkIndex(missing.c_str());
                    CString name = CString("perk ") + missing.c_str();
                    name = name.Left(name.GetLength() - 1) + "0";
                    if (pTxtMng->GetText(name)) pTxtMng->GetLine(name);
                    szOut.Format("%s%i", (LPCSTR)name, index % 3 + 1);
                    pSurf->DrawText(szOut, rc, 0, RGB(255, 15, 15), DT_RIGHT);
                }
            }
			rc += shift;
			switch (nPerkGroup)
			{
			case 0:
			case 10:
			case 17:
				szOut.Format("     %s %d", szSkillStr[ATTRIBPLUS], pPerk->dwModifier);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				break;
			case 16:
				if (mod->CombatSetting("JMV136")) {
					szOut.Format("     %s +%d%%", szSkillStr[DAMAGEMULT], pPerk->dwModifier * 3);
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					rc += shift;
				}
				if (pTxtMng->GetText("string infoskill_25")) pTxtMng->GetLine(szTmp);
				szOut.Format("     %s/%s %d", szSkillStr[TOHITBONUS], szTmp, pPerk->dwModifier);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				break;
			case 24:
				if (mod->CombatSetting("JMV136")) {
					szOut.Format("     %s +%d%%", szSkillStr[DAMAGEMULT], pPerk->dwModifier);
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					rc += shift;
					szOut.Format("     %s +%d%%", szSkillStr[ACTIONS], pPerk->dwModifier);
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					rc += shift;
				}
				if (pTxtMng->GetText("string infoskill_25")) pTxtMng->GetLine(szTmp);
				szOut.Format("     %s/%s %d", szSkillStr[TOHITBONUS], szTmp, pPerk->dwModifier);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				break;
			case 6:
				if (mod->CombatSetting("JMV136")) {
					szOut.Format("     %s +%d%%", szSkillStr[ACTIONS], pPerk->dwModifier * 2);
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					rc += shift;
				}
				if (pTxtMng->GetText("string infoskill_25")) pTxtMng->GetLine(szTmp);
				szOut.Format("     %s/%s %d", szSkillStr[TOHITBONUS], szTmp, pPerk->dwModifier);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				break;
			case 7:
			case 8:
			case 9:
				szOut.Format("     %s %d", szSkillStr[SPELLBONUS], pPerk->dwModifier);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				break;
			case 4:
				if (pTxtMng->GetText("string infoskill_27"))
				{
					pTxtMng->GetLine(szTmp);
				}
				szOut.Format("     %s %d%%", szTmp + " +", pPerk->dwModifier);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				break;
			case 5:
				if (pTxtMng->GetText("string infoskill_28"))
				{
					pTxtMng->GetLine(szTmp);
				}
				szOut.Format("     %s %d%%", szTmp + " +", pPerk->dwModifier);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				break;
			case 2:
				szOut.Format("     %s %d%%", szSkillStr[HEALTH], pPerk->dwModifier + 100);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				break;
			case 11:
				if (pTxtMng->GetText("string infoskill_29"))pTxtMng->GetLine(szTmp);
				if (pTxtMng->GetText("string infounit_13"))pTxtMng->GetLine(szTmp2);
				szOut.Format("     %s +%d%s", szTmp, pPerk->dwModifier,szTmp2);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				break;
			case 12:
				if (pTxtMng->GetText("string infoskill_30"))
				{
					pTxtMng->GetLine(szTmp);
				}
				szOut.Format("     %s %d%%", szTmp + " +", pPerk->dwModifier);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				break;
			case 13:
				if (pTxtMng->GetText("string infoskill_31"))
				{
					pTxtMng->GetLine(szTmp);
				}
				szOut.Format("     %s %d%%", szTmp + " +", pPerk->dwModifier);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				break;
			case 19:
				if (pTxtMng->GetText("string infoskill_32"))
				{
					pTxtMng->GetLine(szTmp);
				}
				szOut.Format("     %s %d%%", szTmp + " +", pPerk->dwModifier);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				break;
			case 3:
				if (pTxtMng->GetText("string infoskill_26"))
				{
					pTxtMng->GetLine(szTmp);
				}
				szOut.Format("     %s %d%%", szTmp + " +", pPerk->dwModifier);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				break;
			case 18:
				szOut.Format("     %s %d%%", szSkillStr[STAMINARGNR], pPerk->dwModifier + 100);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				break;
			case 14:
				szOut.Format("     %s %d%%", szSkillStr[ACTIONS] + " +", pPerk->dwModifier);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				break;
			case 1:
				szOut.Format("     %s %d%%", szSkillStr[ENCUMBRANCE], pPerk->dwModifier + 100);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				break;
			case 15:
				backs = theAIGeneralSettings.fpBackstabAdd * 100 - 100;
				szOut.Format("     %s %d%%", szSkillStr[BSTABMULT] + " =", pPerk->dwModifier + backs + 100);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				break;
			case 20:
				if (mod->CombatSetting("JMVAnatomyLimbDamage"))
				{
					szTmp = "\xD3\xE4\xE0\xF0 \xE2 \xEA\xEE\xED\xE5\xF7\xED\xEE\xF1\xF2\xE8";
				}
				else if (pTxtMng->GetText("string infoskill_33"))
				{
					pTxtMng->GetLine(szTmp);
				}
				szOut.Format("     %s %d%%", szTmp + " +", pPerk->dwModifier);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				break;
			case 21:
				if (pTxtMng->GetText("string infoskill_34"))
				{
					pTxtMng->GetLine(szTmp);
				}
				szOut.Format("     %s %d%%", szTmp, 100 -pPerk->dwModifier);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				break;
			case 22:
				if (pTxtMng->GetText("string infoskill_35"))
				{
					pTxtMng->GetLine(szTmp);
				}
				szOut.Format("     %s %d%%", szTmp, pPerk->dwModifier + 100);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				break;
			case 23:
				szOut.Format("     %s %d%%", szSkillStr[STAMINA], pPerk->dwModifier + 100);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				break;
			default:
				break;
			}
			rc += shift;
			rc.right = rc.right + 15;
			int nPerkPrice = pSkillInfo->pStats->GetPerkPrice(pPerk);
			if (nPerkPrice < 0)
				nPerkPrice = INT_MAX;
			szOut.Format("     %s %d", szSkillStr[COST], nPerkPrice);
			pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
			rc.right -= 15;
			rc += shift;
		}
		rc += shift;
		pSurf->DrawMultiText(szDesc.GetString(), rc, 6, 0, color, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS | DT_WORD_ELLIPSIS, TRUE, 0);
	}
	if (pSkillInfo->nType == 4)
	{
		pSurf->DrawText(szStrings[ABSORPARMOR].GetString(), rc, 0, colorH, DT_CENTER);
		rc += shift;
		rc += shift;
		float fpAbsorbtions[RPG_NUM_ATTACK_TYPES];
		for (int i = 0; i < RPG_NUM_ATTACK_TYPES; i++)
		{
			fpAbsorbtions[i] = 0;
			for (int j = 0; j < RPG_NUM_ARMOR_LAYERS; j++)
				if (i < 3)
					fpAbsorbtions[i] += pSkillInfo->pStats->hls[pSkillInfo->eHL].layers[j].fpAbsorbtions[i] +
						pSkillInfo->pStats->hls[pSkillInfo->eHL].layers[j].fpAbsorbtions[i] * pSkillInfo->pStats->GetPerkModifier(5)* 0.01f;
				else
					fpAbsorbtions[i] += pSkillInfo->pStats->hls[pSkillInfo->eHL].layers[j].fpAbsorbtions[i] +
						pSkillInfo->pStats->hls[pSkillInfo->eHL].layers[j].fpAbsorbtions[i] * pSkillInfo->pStats->GetPerkModifier(22) * 0.01f;
		}
		for (int i = 0; i < RPG_NUM_ATTACK_TYPES - 1; i++)
		{
			szOut.Format("damage_%d", i);
			pTxtMng->GetSystemString(szOut.GetString(), szTmp);
			szOut.Format("     %s:", szTmp);
			pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
			szOut.Format("%.1f", fpAbsorbtions[i]);
			pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
			rc += shift;
		}
		rc += shift;
		pSurf->DrawMultiText(szDesc.GetString(), rc, 8, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, 0);
	}
	if (pSkillInfo->nType == 5)
	{
		if (pSkillInfo->pWeapon) {
			ERPGWeaponTypes type = pSkillInfo->pWeapon->GetWeaponType();
			if (pSkillInfo->szNonlocalName == "defense")
				szOut.Format("     %s", szStrings[MODDEFENSE]);
			else
				szOut.Format("     %s", szStrings[MODATTACK]);
			pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_HEADER, DT_CENTER);
			rc += shift;
			rc += shift;
			int nSum = 0;
			int nVal;
			{
				szOut.Format("     %s", szStrings[WSLDEXT]);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				int dext = (int)pUnit->GetUserUnitStats()->cDexterity - 25;
				szOut.Format("%d", nVal = dext);
				pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
				nSum += nVal;
				rc += shift;
				nVal = (int)pSkillInfo->pStats->GetWSLBaseSkill(type);
				CString szTmp = "Unknown";
				if ((ModCampDescriptions::Ranged(type) ? RPGASKL_ARCHERY : RPGASKL_MELEE) == RPGASKL_MELEE) {
					if (pTxtMng->GetText("string infoitem_55"))
					{
						pTxtMng->GetLine(szTmp);
					}
				}
				else {
					if (pTxtMng->GetText("string infoitem_56"))
					{
						pTxtMng->GetLine(szTmp);
					}
				}
				szOut.Format("     %s", szTmp);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				szOut.Format("%d", nVal);
				pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
				nSum += nVal;
				rc += shift;
			}
			{
				if (pTxtMng->GetText("string infoitem_54"))
				{
					pTxtMng->GetLine(szTmp);
				}
				szOut.Format("     %s", szTmp);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				if (pSkillInfo->szNonlocalName == "defense")
					szOut.Format("%d", nVal = (int)mod->InfoDisplayScalar("GetOverloadDefenceOnServer", {float(nWeight), float(nEncumbr)}));
				else
					szOut.Format("%d", nVal = (int)mod->InfoDisplayScalar("GetOverloadAttackOnServer", {float(nWeight), float(nEncumbr)}));
				pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
				nSum += nVal;
				rc += shift;
			}
			{
				int perkLevel = int(pSkillInfo->pStats->cPerks[5]);
				if (pTxtMng->GetText("string infoitem_57"))
				{
					pTxtMng->GetLine(szTmp);
				}
				szOut.Format("     %s", szTmp);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				if (pSkillInfo->szNonlocalName == "defense") {
					int defs = 0;
					for (int i = 0; i < pPers->armors.GetSize(); i++) {
						if (pPers->armors[i]) {
							const SIDBItemGeneralShape* pNameShape = pPers->armors[i]->GetShapeEntry();
							if (pNameShape) {
								CString pName = pNameShape->szName;
								defs += static_cast<int>(mod->InfoProperty("GetArmorPropsInside", pName.GetString(), 2));
							}
						}
					}
					float def_mastery_mod = mod->InfoDisplayScalar("GetArmoryDefenceDebuff", {float(perkLevel)});
					defs += max(0, min(-defs, int(def_mastery_mod)));
					szOut.Format("%d", nVal = defs);
				}
				else {
					int atts = 0;
					for (int i = 0; i < pPers->armors.GetSize(); i++) {
						if (pPers->armors[i]) {
							const SIDBItemGeneralShape* pNameShape = pPers->armors[i]->GetShapeEntry();
							if (pNameShape) {
								CString pName = pNameShape->szName;
								atts += mod->InfoProperty("GetArmorPropsInside", pName.GetString(), 3);
							}
						}
					}
					float atc_mastery_mod = mod->InfoDisplayScalar("GetArmoryAttackDebuff", {float(perkLevel)});
					atts += max(0, min(-atts, int(atc_mastery_mod)));
					szOut.Format("%d", nVal = atts);
				}
				pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
				nSum += nVal;
				rc += shift;
			}
				CString weaponProt = "";
				if(pSkillInfo->pWeapon) weaponProt = pSkillInfo->pWeapon->GetShapeEntry()->szName;
				nVal = 0;
				if (pSkillInfo->pWeapon) nVal = mod->InfoProperty("GetWeaponPropsInside", weaponProt.GetString(), 7);
				bool duelist = int(pSkillInfo->pStats->cPerks[28]);
				CItemWeapon* rightItem = pUnit->GetQWeapon(pPers->dwCurrentWeapon);
				int rtID = rightItem && rightItem->GetShapeEntry() ? rightItem->GetShapeEntry()->dwTypeID : -1;
				bool twoHandedWeaponRight = (rtID == RPGWST_HANDS2 || rtID == RPGWST_HANDS4 || rtID == RPGWST_HANDS5 || rtID == RPGWST_HANDS6 || rtID == RPGWST_HANDS7);
				if (nVal != 0) {
					if (duelist && !twoHandedWeaponRight) {
						if (pTxtMng->GetText("string infoitem_61")) pTxtMng->GetLine(szTmp);
						szOut.Format("     %s", szTmp);
					}
					else {
						if (nVal == 1) {
							if (pTxtMng->GetText("string infoskill_38")) pTxtMng->GetLine(szTmp);
							szOut.Format("     %s", szTmp);
						}
						else if (nVal == 2) {
							if (pTxtMng->GetText("string infoskill_37")) pTxtMng->GetLine(szTmp);
							szOut.Format("     %s", szTmp);
						}
						else if (nVal == 3) {
							if (pTxtMng->GetText("string infoskill_36")) pTxtMng->GetLine(szTmp);
							szOut.Format("     %s", szTmp);
						}
						else
							szOut.Format("     %s", szStrings[WSLPERK]);
					}
				}
				else {
					if (duelist && !twoHandedWeaponRight) {
						if (pTxtMng->GetText("PERK duel0")) pTxtMng->GetLine(szTmp);
						szOut.Format("     %s", szTmp);
					}
				}
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				int duelist_def = 0;
				if (pSkillInfo->szNonlocalName == "defense")
					if (duelist && !twoHandedWeaponRight)
						duelist_def = 3;
				if (nVal == 1) {
					szOut.Format("%i", int(pSkillInfo->pStats->GetPerkModifier(16)) + duelist_def);
					nVal = int(pSkillInfo->pStats->GetPerkModifier(16) + duelist_def);
				}
				else if (nVal == 2) {
					szOut.Format("%i", int(pSkillInfo->pStats->GetPerkModifier(24)) + duelist_def);
					nVal = int(pSkillInfo->pStats->GetPerkModifier(24) + duelist_def);
				}
				else if (nVal == 3) {
					szOut.Format("%i", int(pSkillInfo->pStats->GetPerkModifier(6)) + duelist_def);
					nVal = int(pSkillInfo->pStats->GetPerkModifier(6) + duelist_def);
				}
				else{
					szOut.Format("%i", 0);
					nVal = 0;
				}
					pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
				nSum += nVal;
				rc += shift;
				int left = ModStats::LeftHand(*pUnit);
				if (left != -1 && pUnit->GetQWeapon(left) && pUnit->GetQWeapon(left)->GetShapeEntry()) {
					CItemWeapon* rightItem = pUnit->GetQWeapon(pPers->dwCurrentWeapon);
					int rtID = rightItem && rightItem->GetShapeEntry() ? rightItem->GetShapeEntry()->dwTypeID : -1;
					int ltID = pUnit->GetQWeapon(left)->GetShapeEntry()->dwTypeID;
					bool twoHandedWeaponRight = (rtID == RPGWST_HANDS2 || rtID == RPGWST_HANDS4 || rtID == RPGWST_HANDS5 || rtID == RPGWST_HANDS6 || rtID == RPGWST_HANDS7);
					bool twoHandedWeaponLeft = (ltID == RPGWST_HANDS2 || ltID == RPGWST_HANDS4 || ltID == RPGWST_HANDS5 || ltID == RPGWST_HANDS6 || ltID == RPGWST_HANDS7);
					bool ShieldLeft = (ltID == RPGWST_HANDS1);
					bool strong = int(pSkillInfo->pStats->cPerks[29]);
					int strong_mod = 0;
					bool master = int(pSkillInfo->pStats->cPerks[26]);
					int master_mod = 0;
					if(ShieldLeft){
						szOut.Format("     %s", szStrings[WSLWEAPON]);
						pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
						if (pSkillInfo->szNonlocalName == "defense")
							nVal = (int)pSkillInfo->pWeapon->GetDefenceBonus();
						else
							nVal = (int)pSkillInfo->pWeapon->GetAtackBonus();
						szOut.Format("%d", nVal);
						pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
						nSum += nVal;
						rc += shift;
						if (pTxtMng->GetText("string infoskill_39"))
						{
							pTxtMng->GetLine(szTmp);
						}
						szOut.Format("     %s", szTmp);
						pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
						if (pSkillInfo->szNonlocalName == "defense")
							nVal = pPers->weapons[left]->GetDefenceBonus();
						else
							nVal = pPers->weapons[left]->GetAtackBonus();
						szOut.Format("%d", nVal);
						pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
						nSum += nVal;
						rc += shift;
						if (pSkillInfo->szNonlocalName != "defense")
							if (strong && twoHandedWeaponRight) {
								strong_mod = mod->InfoDisplayScalar("GetStrongMod", {});
								if (pTxtMng->GetText("string infoskill_42"))
								{
									pTxtMng->GetLine(szTmp);
								}
								szOut.Format("     %s", szTmp);
								pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
								nVal = strong_mod;
								szOut.Format("%d", nVal);
								pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
								nSum += nVal;
								rc += shift;
							}
					}
					else {
						if (pTxtMng->GetText("string infoskill_40"))
						{
							pTxtMng->GetLine(szTmp);
						}
						szOut.Format("     %s", szTmp);
						pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
						if (pSkillInfo->szNonlocalName == "defense"){
							int rightWeapDef = pPers->weapons[pPers->dwCurrentWeapon]->GetDefenceBonus();
							int leftWeapDef = pPers->weapons[left]->GetDefenceBonus();
							int def_mod = float(rightWeapDef + leftWeapDef) * 0.5f;
							nVal = def_mod;
						}
						else{
							int rightWeapAtk = pPers->weapons[pPers->dwCurrentWeapon]->GetAtackBonus();
							int leftWeapAtk = pPers->weapons[left]->GetAtackBonus();
							int atk_mod = float(rightWeapAtk + leftWeapAtk) * 0.5f;
							nVal = atk_mod;
						}
						szOut.Format("%d", nVal);
						pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
						nSum += nVal;
						rc += shift;
						if (pSkillInfo->szNonlocalName != "defense") {
							if (strong && (twoHandedWeaponRight || twoHandedWeaponLeft))
								strong_mod = mod->InfoDisplayScalar("GetStrongMod", {});
							if (!ShieldLeft) {
								if (master) master_mod = mod->InfoDisplayScalar("GetMasterMod", {});
								else		master_mod = mod->InfoDisplayScalar("GetMasterMod2", {});
								if (master) {
									if (pTxtMng->GetText("string infoskill_41")) pTxtMng->GetLine(szTmp);
								}
								else
								{
									if (pTxtMng->GetText("string infoskill_41_2")) pTxtMng->GetLine(szTmp);
								}
								szOut.Format("     %s", szTmp);
								pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
								nVal = master_mod;
								szOut.Format("%d", nVal);
								pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
								nSum += nVal;
								rc += shift;
							}
							if (strong && (twoHandedWeaponRight || twoHandedWeaponLeft)) {
								if (pTxtMng->GetText("string infoskill_42"))
								{
									pTxtMng->GetLine(szTmp);
								}
								szOut.Format("     %s", szTmp);
								pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
								nVal = strong_mod;
								szOut.Format("%d", nVal);
								pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
								nSum += nVal;
								rc += shift;
							}
						}
					}
				}
				else {
					szOut.Format("     %s", szStrings[WSLWEAPON]);
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					if (pSkillInfo->szNonlocalName == "defense")
						nVal = (int)pSkillInfo->pWeapon->GetDefenceBonus();
					else
						nVal = (int)pSkillInfo->pWeapon->GetAtackBonus();
					szOut.Format("%d", nVal);
					pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
					nSum += nVal;
					rc += shift;
				}
				szOut.Format("     %s", szStrings[WSLSUM]);
				pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
				szOut.Format("%d", nSum);
				pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
				rc += shift;
				rc += shift;
				if (pSkillInfo->szNonlocalName == "defense") {
					float blockC = mod->InfoDisplayActor("GetShieldBlockChance", *pUnit);
					float overloadDecr = mod->InfoDisplayScalar("GetOverloadBlockOnServer", {float(nWeight), float(nEncumbr)});
					blockC = blockC * overloadDecr;
					bool rightS = pUnit->GetQWeapon(int(pUnit->GetCurrentWeaponSlotIndex())) && pUnit->GetQWeapon(int(pUnit->GetCurrentWeaponSlotIndex()))->GetShapeEntry()->dwTypeID == 9;
					bool leftS = pUnit->GetQWeapon(left) && pUnit->GetQWeapon(left)->GetShapeEntry()->dwTypeID == 9;
					float blockP = mod->InfoDisplayActor("GetShieldBlock", *pUnit);
					blockP = blockP * overloadDecr;
					if (rightS || leftS) {
						if (pTxtMng->GetText("string infoskill_26")) pTxtMng->GetLine(szTmp);
						szOut.Format("     %s:", szTmp);
						pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
						szOut.Format("%.0f", blockP);
						pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
						rc += shift;
						if (pTxtMng->GetText("string infoskill_31")) pTxtMng->GetLine(szTmp);
						szOut.Format("     %s:", szTmp);
						pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
						szOut.Format("%.0f%%", blockC);
						pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
						rc += shift;
					}
					else {
					}
					float parry = 0;
					float parry2 = 0;
					if (int(pUnit->GetCurrentWeaponSlotIndex()) != -1 && int(pUnit->GetCurrentWeaponSlotIndex()) >= 0 && int(pUnit->GetCurrentWeaponSlotIndex()) <= 3) {
						if (left == -1)
							parry = mod->InfoDisplayActor("GetWeaponParryChance", *pUnit, int(pUnit->GetCurrentWeaponSlotIndex())) * mod->InfoDisplayScalar("GetOverloadParryOnServer", {float(nWeight), float(nEncumbr)});
						else {
							parry = mod->InfoDisplayActor("GetWeaponParryChance", *pUnit, int(pUnit->GetCurrentWeaponSlotIndex())) * mod->InfoDisplayScalar("GetOverloadParryOnServer", {float(nWeight), float(nEncumbr)}) * 0.01;
							parry2 = mod->InfoDisplayActor("GetWeaponParryChance", *pUnit, left) * mod->InfoDisplayScalar("GetOverloadParryOnServer", {float(nWeight), float(nEncumbr)}) * 0.01;
							if (parry < 0.0001f) {
								if (parry2 < 0.0001f)
									parry2 = parry * 100;
								parry = parry2 * 100;
							}
							else {
								if (parry2 < 0.0001f)
									parry2 = parry * 100;
								else
									parry = (parry + parry2 - 2 * (parry * parry2)) * 100;
							}
						}
					}
					if (pTxtMng->GetText("string infoskill_32")) pTxtMng->GetLine(szTmp);
					szOut.Format("     %s:", szTmp);
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					szOut.Format("%.0f%%", parry);
					pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
					rc += shift;
					float hearing = 1;
					float sight = 1;
					for (int i = 0; i < 7; i++) {
						if (pUnit->GetQArmor(i)) {
							const char* armor_part = pUnit->GetQArmor(i)->GetShapeEntry()->szName.GetString();
							sight += mod->InfoProperty("GetArmorPropsInside", armor_part, 5);
							hearing += mod->InfoProperty("GetArmorPropsInside", armor_part, 6);
						}
					}
					float backstabMod = pUnit->GetUserUnitStats()->GetPerkModifier(15);
					int backstab = int(pSkillInfo->pStats->cPerks[15]);
					if (backstab == 1){
						hearing -= 0.05f;
						sight -= 0.05f;
					}
					else if (backstab == 2) {
						hearing -= 0.1f;
						sight -= 0.1f;
					}
					else if (backstab == 3) {
						hearing -= 0.2f;
						sight -= 0.2f;
					}
					if (pTxtMng->GetText("string infoitem_51")) pTxtMng->GetLine(szTmp);
					szOut.Format("     %s", szTmp);
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					szOut.Format("%.0f%%", hearing*100.0f);
					pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
					rc += shift;
					if (pTxtMng->GetText("string infoitem_53")) pTxtMng->GetLine(szTmp);
					szOut.Format("     %s", szTmp);
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					szOut.Format("%.0f%%", sight* 100.0f);
					pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
					rc += shift;
				}
				else {
					float CriticalChanceRight = 0;
					float CriticalChanceLeft = 0;
					float CriticalDamageRight = 0;
					float CriticalDamageLeft = 0;
					if (pUnit->GetQWeapon(pUnit->GetCurrentWeaponSlotIndex()) && pUnit->GetQWeapon(pUnit->GetCurrentWeaponSlotIndex())->GetShapeEntry()) {
						CriticalChanceRight = mod->InfoProperty("GetWeaponPropsInside", pUnit->GetQWeapon(int(pUnit->GetCurrentWeaponSlotIndex()))->GetShapeEntry()->szName.GetString(), 4) * 0.01f;
						CriticalDamageRight = mod->InfoProperty("GetWeaponPropsInside", pUnit->GetQWeapon(int(pUnit->GetCurrentWeaponSlotIndex()))->GetShapeEntry()->szName.GetString(), 5) * 0.01f;
					}
					if (left != -1 && pUnit->GetQWeapon(left) && pUnit->GetQWeapon(left)->GetShapeEntry()) {
						CriticalChanceLeft = mod->InfoProperty("GetWeaponPropsInside", pUnit->GetQWeapon(left)->GetShapeEntry()->szName.GetString(), 4) * 0.01f;
						CriticalDamageLeft = mod->InfoProperty("GetWeaponPropsInside", pUnit->GetQWeapon(left)->GetShapeEntry()->szName.GetString(), 5) * 0.01f;
					}
					float critChancePerkMod = pUnit->GetUserUnitStats()->GetPerkModifier(12);
					float critDamagePerkMod = pUnit->GetUserUnitStats()->GetPerkModifier(4);
					float ArmorCritChance = 0;
					float ArmorCritChanceMat = 0;
					float ArmorCritDamage = 0;
					float ArmorCritDamageMat = 0;
					for (int i = 0; i < 7; i++) {
						if (pUnit->GetQArmor(i)) {
							const char* armor_part = pUnit->GetQArmor(i)->GetShapeEntry()->szName.GetString();
							ArmorCritChance += mod->InfoProperty("GetArmorPropsInside", armor_part, 8) * 0.01f;
							ArmorCritChanceMat += mod->InfoProperty("GetMaterialPropsInside", armor_part, 6);
						}
					}
					float JewelryCritChance = 0;
					float JewelryCritDamage = 0;
					for (int i = 0; i < pPers->items.GetSize(); i++) {
						if (pPers->items[i]) {
							CString armor_part = pPers->items[i]->GetShapeEntry()->szName;
							JewelryCritChance += mod->InfoProperty("GetJewelryPropsInside", armor_part.GetString(), 7) * 0.01f;
							JewelryCritDamage += mod->InfoProperty("GetJewelryPropsInside", armor_part.GetString(), 8) * 0.01f;
						}
					}
					ArmorCritChance = ArmorCritChance * ArmorCritChanceMat;
					CriticalChanceRight = CriticalChanceRight + ArmorCritChance + JewelryCritChance;
					CriticalChanceLeft = CriticalChanceLeft + ArmorCritChance + JewelryCritChance;
					CriticalChanceRight = (CriticalChanceRight +  critChancePerkMod * 0.01f) * 100;
					CriticalChanceLeft = (CriticalChanceLeft + critChancePerkMod * 0.01f) * 100;
					CriticalDamageRight += JewelryCritDamage;
					CriticalDamageLeft += JewelryCritDamage;
					CriticalDamageRight = (CriticalDamageRight + critDamagePerkMod * 0.01f) * 100;
					CriticalDamageLeft = (CriticalDamageLeft + critDamagePerkMod * 0.01f) * 100;
					if (pTxtMng->GetText("string infoitem_58"))
					{
						pTxtMng->GetLine(szTmp);
					}
					szOut.Format("    1. %s", szTmp);
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					rc += shift;
					if (pTxtMng->GetText("string infoitem_48"))
					{
						pTxtMng->GetLine(szTmp);
					}
					szOut.Format("     %s", szTmp);
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					szOut.Format("%.0f%%", CriticalChanceRight);
					pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
					rc += shift;
					if (pTxtMng->GetText("string infoitem_49"))
					{
						pTxtMng->GetLine(szTmp);
					}
					szOut.Format("     %s", szTmp);
					pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
					szOut.Format("%.0f%%", CriticalDamageRight);
					pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
					rc += shift;
					if (left != -1 && pUnit->GetQWeapon(left) && pUnit->GetQWeapon(left)->GetShapeEntry() && pUnit->GetQWeapon(left)->GetShapeEntry()->dwTypeID != 9) {
						if (pTxtMng->GetText("string infoitem_58"))
						{
							pTxtMng->GetLine(szTmp);
						}
						szOut.Format("    2. %s", szTmp);
						pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
						rc += shift;
						if (pTxtMng->GetText("string infoitem_48"))
						{
							pTxtMng->GetLine(szTmp);
						}
						szOut.Format("     %s", szTmp);
						pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
						szOut.Format("%.0f%%", CriticalChanceLeft);
						pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
						rc += shift;
						if (pTxtMng->GetText("string infoitem_49"))
						{
							pTxtMng->GetLine(szTmp);
						}
						szOut.Format("     %s", szTmp);
						pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
						szOut.Format("%.0f%%", CriticalDamageLeft);
						pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
						rc += shift;
					}
				}
			}
	}
	if (pSkillInfo->nType == 6)
	{
		pSurf->DrawText(szStrings[MAGICSCHOOL].GetString(), rc, 0, colorH, DT_CENTER);
		rc += shift;
		rc += shift;
		int nStr = MSLFIRE;
		for (int i = 0; i < 3; i++)
		{
			ERPGMagicSubtypes subtype = (ERPGMagicSubtypes)i;
			if (subtype == RPGMST_DIVINATION || subtype == RPGMST_HEALING ||
				subtype == RPGMST_DOMINATION)
				continue;
			szOut.Format("     %s", szStrings[nStr++]);
			int intelligence = (int)pUnit->GetUserUnitStats()->cIntelligence - 25;
			int skill = 0;
			int nPerkIndex = RPGPRK_FIRE + subtype;
			float fBaseSkill = pSkillInfo->pStats->GetBaseMagicSkill(subtype);
			fBaseSkill += pSkillInfo->pStats->GetPerkModifier(nPerkIndex);
			skill = fBaseSkill + intelligence;
			pSurf->DrawText(szOut.GetString(), rc, 0, color, DT_LEFT);
			szOut.Format("%d", (int)skill);
			pSurf->DrawText(szOut.GetString(), rc - CPoint(10, 0), 0, color, DT_RIGHT);
			rc += shift;
		}
		rc += shift;
		pSurf->DrawMultiText(szDesc.GetString(), rc, 9, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, 0);
	}
	if (pSkillInfo->nType == 7) {
		if(pPers){
			if(pPers->dwCurrentWeapon != -1 && pPers->weapons.GetSize() != 0 && pPers->dwCurrentWeapon >= 0 && pPers->dwCurrentWeapon < 4){
				CItemWeapon* pWeapon = pPers->weapons[pPers->dwCurrentWeapon];
				if(pWeapon && pWeapon != (CItemWeapon*)0x00000001){
					pSurf->DrawText(szStrings[DAMAGE].GetString(), rc, 0, colorH, DT_CENTER);
					rc += shift;
					float damages[RPG_NUM_ATTACK_TYPES];
					pWeapon->GetDamageDistribution(damages);
					int i = 0;
					int leftWeapon = ModStats::LeftHand(*pUnit);
					if ((int)pWeapon->GetDamageMax() != 0)
					{
						for (; i < RPG_NUM_ATTACK_TYPES; i++)
						{
							if (damages[i] != 0)
							{
								if (i < RPG_NUM_ATTACK_TYPES)
								{
									szOut.Format("damage_%d", i);
								}
								pTxtMng->GetSystemString(szOut.GetString(), szTmp);
								if (pTxtMng->GetText("string infoitem_58"))
								{
									pTxtMng->GetLine(szTmp2);
								}
								if (leftWeapon != -1) {
									CItemWeapon* leftWeaponItem = pUnit->GetQWeapon(leftWeapon);
									if (leftWeaponItem != NULL) {
										szOut.Format("  1 %s %s", szTmp2, szTmp);
									}
									else {
										szOut.Format("  %s %s", szTmp2, szTmp);
									}
								}
								else {
									szOut.Format("  %s %s", szTmp2, szTmp);
								}
								pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_LEFT);
								float minDamage = pWeapon->GetDamageMin();
								float maxDamage = pWeapon->GetDamageMax();
								szOut.Format("%.0f-%.0f", minDamage, maxDamage);
								pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_RIGHT);
								rc += shift;
							}
						}
					}
					if (pTxtMng->GetText("string infoitem_59"))
					{
						pTxtMng->GetLine(szTmp);
					}
					pSurf->DrawText(szTmp.GetString(), rc, 0, INV_COLOR_INFO, DT_LEFT);
					rc += shift;
					float skill_mod = 1;
					int weapon_type = pWeapon->GetShapeEntry()->dwTypeID;
					bool archery = (weapon_type == RPGWST_BOW || weapon_type == RPGWST_CROSSBOW || weapon_type == RPGWST_HANDS3 || weapon_type == RPGWST_HANDS7);
					int curSkill = static_cast<float>(pSkillInfo->pStats->cAutoSkills[RPGASKL_MELEE]);
					if (archery)
						curSkill = static_cast<float>(pSkillInfo->pStats->cAutoSkills[RPGASKL_ARCHERY]);
					skill_mod = mod->InfoDisplayScalar("GetSkillMod", {float(curSkill)});
					skill_mod = skill_mod - 1;
					skill_mod = skill_mod * 100;
					if (pTxtMng->GetText("string infoitem_62"))
					{
						pTxtMng->GetLine(szTmp);
					}
					szOut.Format("  %s", szTmp.GetString());
					pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_LEFT);
					if(skill_mod>0)
						szOut.Format("+%.0f%%", skill_mod);
					else
						szOut.Format("%.0f%%", skill_mod);
					pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_RIGHT);
					rc += shift;
					float attr_mod = 0;
					const SPDBPerk* pPerk = thePerkDatabase.GetPerk(30);
					attr_mod = mod->InfoDisplayScalar("GetDamageModAttr", {float(weapon_type), float(int(pSkillInfo->pStats->psStrength.fpEffectiveValue)), float(int(pSkillInfo->pStats->psDexterity.fpEffectiveValue)), float(int(pSkillInfo->pStats->psIntelligence.fpEffectiveValue)), float(0.5f)});
					attr_mod = attr_mod - 1;
					attr_mod = attr_mod * 100;
					if (pTxtMng->GetText("string infoitem_60"))
					{
						pTxtMng->GetLine(szTmp);
					}
					szOut.Format("  %s", szTmp.GetString());
					pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_LEFT);
					if (attr_mod > 0)
						szOut.Format("+%.0f%%", attr_mod);
					else
						szOut.Format("%.0f%%", attr_mod);
					pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_RIGHT);
						rc += shift;
					float wperk_mod = 0;
					if (mod->CombatSetting("JMV136")) {
						int weightType = mod->InfoProperty("GetWeaponPropsInside", pWeapon->GetShapeEntry()->szName.GetString(), 7);
						int perk = 24;
						if (weightType == 1)
							perk = 16;
						if (weightType != 3)
							wperk_mod = pSkillInfo->pStats->GetPerkModifier(perk);
						if (weightType == 1)
							wperk_mod = wperk_mod * 3;
					}
					float perk_mod = mod->InfoDisplayScalar("GetDamageModAttr", {float(weapon_type), float(int(pSkillInfo->pStats->psStrength.fpEffectiveValue)), float(int(pSkillInfo->pStats->psDexterity.fpEffectiveValue)), float(int(pSkillInfo->pStats->psIntelligence.fpEffectiveValue)), float(0.25f * int(pSkillInfo->pStats->cPerks[thePerkDatabase.GetPerkIndex(pPerk)]))});
					perk_mod = perk_mod - 1;
					perk_mod = perk_mod * 100;
					if (pTxtMng->GetText("string infoitem_61")) pTxtMng->GetLine(szTmp);
					szOut.Format("  %s",szTmp.GetString());
					pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_LEFT);
					if(perk_mod + wperk_mod > 0)
						szOut.Format("+%.0f%%", perk_mod + wperk_mod);
					else
						szOut.Format("%.0f%%", perk_mod + wperk_mod);
					pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_RIGHT);
					rc += shift;
					float enc_mod = 1.0f;
					float overloadDecr = mod->InfoDisplayScalar("GetOverloadDamageOnServer", {float(nWeight), float(nEncumbr)});
					enc_mod = overloadDecr;
					enc_mod = enc_mod - 1;
					enc_mod = enc_mod * 100;
					if (pTxtMng->GetText("string infoitem_57"))
					{
						pTxtMng->GetLine(szTmp);
					}
					szOut.Format("  %s", szTmp);
					float compl = mod->InfoComplex(*pUnit, 11);
					pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_LEFT);
					if(enc_mod>0)
						szOut.Format("+%.0f%%", enc_mod + compl);
					else
						szOut.Format("%.0f%%", enc_mod + compl);
					pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_RIGHT);
					rc += shift;
					float dificulty_mod = 1.0f;
					dificulty_mod = 1.0f + theSettings.nDifficultyLevel * 0.25f;
					float sum = (skill_mod + attr_mod + perk_mod + enc_mod + wperk_mod + compl);
					if (pTxtMng->GetText("string infoitem_64")) pTxtMng->GetLine(szTmp);
					if (pTxtMng->GetText("string infoskill_53")) pTxtMng->GetLine(szTmp2);
					int difficultyDraw = theSettings.nDifficultyLevel == 1 ? 2 : 1;
					if(dificulty_mod > 1)
						szOut.Format("%s(%d %s)", szTmp, difficultyDraw,  szTmp2);
					else
						szOut.Format("%s", szTmp);
					pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_LEFT);
					if (sum > 0)
						szOut.Format("+%.0f%%", sum + (dificulty_mod - 1) * 100);
					else
						szOut.Format("%.0f%%", sum + (dificulty_mod - 1) * 100);
					pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_RIGHT);
					rc += shift;
					for (int i = 0; i < RPG_NUM_ATTACK_TYPES; i++)
					{
						if (damages[i] != 0)
						{
							if (i < RPG_NUM_ATTACK_TYPES)
							{
								szOut.Format("damage_%d", i);
								pTxtMng->GetSystemString(szOut.GetString(), szTmp);
								if (pTxtMng->GetText("string infoitem_58"))
								{
									pTxtMng->GetLine(szTmp2);
								}
								if (leftWeapon >= 0) {
									CItemWeapon* leftWeaponItem = pUnit->GetQWeapon(leftWeapon);
									if (leftWeaponItem != NULL) {
										szOut.Format("  1 %s %s", szTmp2, szTmp);
									}
									else {
										szOut.Format("  %s %s", szTmp2, szTmp);
									}
								}
								else {
									szOut.Format("  %s %s", szTmp2, szTmp);
								}
								pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_LEFT);
								float hitLocDecr = mod->InfoDisplayWound(*pSkillInfo->pStats);
								float minDamage = pWeapon->GetDamageMin();
								float maxDamage = pWeapon->GetDamageMax();
								minDamage = minDamage * (100 + sum + hitLocDecr) * 0.01;
								maxDamage = maxDamage * (100 + sum + hitLocDecr) * 0.01;
								szOut.Format("%0.f-%0.f", minDamage* dificulty_mod, maxDamage *dificulty_mod);
								pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_RIGHT);
								rc += shift;
							}
						}
					}
					float damagesL[RPG_NUM_ATTACK_TYPES];
					if (leftWeapon >= 0) {
						CItemWeapon* leftWeaponItem = pUnit->GetQWeapon(leftWeapon);
						if (leftWeaponItem != NULL) {
							if (leftWeaponItem->GetShapeEntry()->dwTypeID != 9) {
								pSurf->DrawText("--------------------------", rc, 0, INV_COLOR_INFO, DT_LEFT);
								rc += shift;
								leftWeaponItem->GetDamageDistribution(damagesL);
								if ((int)leftWeaponItem->GetDamageMax() != 0)
								{
									for (int i = 0; i < RPG_NUM_ATTACK_TYPES; i++)
									{
										if (damagesL[i] != 0)
										{
											if (i < RPG_NUM_ATTACK_TYPES)
											{
												szOut.Format("damage_%d", i);
												pTxtMng->GetSystemString(szOut.GetString(), szTmp);
												if (pTxtMng->GetText("string infoitem_58"))
												{
													pTxtMng->GetLine(szTmp2);
												}
												szOut.Format("  2 %s %s", szTmp2, szTmp);
												pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_LEFT);
												float minDamageL = leftWeaponItem->GetDamageMin();
												float maxDamageL = leftWeaponItem->GetDamageMax();
												szOut.Format("%.0f-%.0f", minDamageL, maxDamageL);
												pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_RIGHT);
												rc += shift;
											}
										}
									}
								}
								if (pTxtMng->GetText("string infoitem_59"))
								{
									pTxtMng->GetLine(szTmp);
								}
								pSurf->DrawText(szTmp.GetString(), rc, 0, INV_COLOR_INFO, DT_LEFT);
								rc += shift;
								float skill_mod = 1;
								int weapon_typeL = leftWeaponItem->GetShapeEntry()->dwTypeID;
								bool archery = (weapon_typeL == RPGWST_BOW || weapon_typeL == RPGWST_CROSSBOW || weapon_typeL == RPGWST_HANDS3 || weapon_typeL == RPGWST_HANDS7);
								int curSkill = static_cast<float>(pSkillInfo->pStats->cAutoSkills[RPGASKL_MELEE]);
								if (archery)
									curSkill = static_cast<float>(pSkillInfo->pStats->cAutoSkills[RPGASKL_ARCHERY]);
								skill_mod = mod->InfoDisplayScalar("GetSkillMod", {float(curSkill)});
								skill_mod = skill_mod - 1;
								skill_mod = skill_mod * 100;
								if (pTxtMng->GetText("string infoitem_62"))
								{
									pTxtMng->GetLine(szTmp);
								}
								szOut.Format("  %s", szTmp.GetString());
								pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_LEFT);
								if (skill_mod > 0)
									szOut.Format("+%.0f%%", skill_mod);
								else
									szOut.Format("%.0f%%", skill_mod);
								pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_RIGHT);
								rc += shift;
								float attr_mod2 = 0;
								attr_mod2 = mod->InfoDisplayScalar("GetDamageModAttr", {float(weapon_typeL), float(int(pSkillInfo->pStats->psStrength.fpEffectiveValue)), float(int(pSkillInfo->pStats->psDexterity.fpEffectiveValue)), float(int(pSkillInfo->pStats->psIntelligence.fpEffectiveValue)), float(0.5f)});
								attr_mod2 = attr_mod2 - 1;
								attr_mod2 = attr_mod2 * 100;
								if (pTxtMng->GetText("string infoitem_60"))
								{
									pTxtMng->GetLine(szTmp);
								}
								szOut.Format("  %s", szTmp.GetString());
								pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_LEFT);
								if (attr_mod2 > 0)
									szOut.Format("+%.0f%%", attr_mod2);
								else
									szOut.Format("%.0f%%", attr_mod2);
								pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_RIGHT);
								rc += shift;
								float perk_mod = mod->InfoDisplayScalar("GetDamageModAttr", {float(weapon_type), float(int(pSkillInfo->pStats->psStrength.fpEffectiveValue)), float(int(pSkillInfo->pStats->psDexterity.fpEffectiveValue)), float(int(pSkillInfo->pStats->psIntelligence.fpEffectiveValue)), float(0.25f * int(pSkillInfo->pStats->cPerks[thePerkDatabase.GetPerkIndex(pPerk)]))});
								perk_mod = perk_mod - 1;
								perk_mod = perk_mod * 100;
								if (pTxtMng->GetText("PERK dexter0"))
								{
									pTxtMng->GetLine(szTmp);
								}
								szOut.Format("  %s:", szTmp.GetString());
								pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_LEFT);
								if (perk_mod > 0)
									szOut.Format("+%.0f%%", perk_mod);
								else
									szOut.Format("%.0f%%", perk_mod);
								pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_RIGHT);
								rc += shift;
								float enc_mod = 1.0f;
								float overloadDecr = mod->InfoDisplayScalar("GetOverloadDamageOnServer", {float(nWeight), float(nEncumbr)});
								enc_mod = overloadDecr;
								enc_mod = enc_mod - 1;
								enc_mod = enc_mod * 100;
								if (pTxtMng->GetText("string infoitem_63"))
								{
									pTxtMng->GetLine(szTmp);
								}
								szOut.Format("  %s", szTmp);
								pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_LEFT);
								if (enc_mod > 0)
									szOut.Format("+%.0f%%", enc_mod);
								else
									szOut.Format("%.0f%%", enc_mod);
								pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_RIGHT);
								rc += shift;
								float sum2 = (skill_mod + attr_mod2 + perk_mod + enc_mod);
								if (pTxtMng->GetText("string infoitem_64"))
								{
									pTxtMng->GetLine(szTmp);
								}
								szOut.Format("%s", szTmp);
								pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_LEFT);
								if (sum2 > 0)
									szOut.Format("+%.0f%%", sum2 + (dificulty_mod - 1) * 100);
								else
									szOut.Format("%.0f%%", sum2 + (dificulty_mod - 1) * 100);
								pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_RIGHT);
								rc += shift;
								if ((int)leftWeaponItem->GetDamageMax() != 0)
								{
									for (int i = 0; i < RPG_NUM_ATTACK_TYPES; i++)
									{
										if (damagesL[i] != 0)
										{
											if (i < RPG_NUM_ATTACK_TYPES)
											{
												szOut.Format("damage_%d", i);
												pTxtMng->GetSystemString(szOut.GetString(), szTmp);
												if (pTxtMng->GetText("string infoitem_58"))
												{
													pTxtMng->GetLine(szTmp2);
												}
												szOut.Format("  2 %s %s", szTmp2, szTmp);
												pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_LEFT);
												float hitLocDecr = mod->InfoDisplayWound(*pSkillInfo->pStats);
												float minDamageL = leftWeaponItem->GetDamageMin();
												float maxDamageL = leftWeaponItem->GetDamageMax();
												minDamageL = minDamageL * (100 + sum + hitLocDecr) * 0.01;
												maxDamageL = maxDamageL * (100 + sum + hitLocDecr) * 0.01;
												szOut.Format("%0.f-%0.f", minDamageL* dificulty_mod, maxDamageL* dificulty_mod);
												pSurf->DrawText(szOut.GetString(), rc, 0, INV_COLOR_INFO, DT_RIGHT);
												rc += shift;
											}
										}
									}
								}
							}
						}
						rc += shift;
					}
				}
			}
		}
	}
	pSurf->ReleaseDC();
#endif
}
// Native display body above; original-game fallback below.
void CI3DObjectItemInfo::SetSkill(SCampSkillMouseInfo *pSkillInfo)
{
    E4CampAccess::Content publication(this,owned::camp::frontend::HoverKind::skill,pSkillInfo&&pSkillInfo->nType>=0);
    if (auto* mod = ModRuntime::Current(); mod && mod->AdvancedUI("useLS_SetSkill")) {
        SetModSkill(pSkillInfo);

        return;
    }

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
        const bool damagePanel=pSkillInfo->nType==7 && pSkillInfo->szNonlocalName=="camp_damage";
        const bool dual=display.damageNumbered;
        int hand=1;
        for(const auto& row:rows){
            if(damagePanel && (row.label=="infoitem_58" || row.label=="infoitem_58#2")){
                hand=row.label=="infoitem_58#2"?2:1;
                if(hand==2){pSurf->DrawText("--------------------------",rc,0,INV_COLOR_INFO,DT_LEFT);rc+=shift;}
                continue;
            }
            CString label,value;std::string key=row.label;const auto suffix=key.find('|');std::string unitLabel;
            if(suffix!=std::string::npos){unitLabel=key.substr(suffix+1);key.resize(suffix);}
            if(key=="$WSLDEXT")label=szStrings[WSLDEXT];else if(key=="$WSLWEAPON")label=szStrings[WSLWEAPON];
            else if(key=="$WSLPERK")label=szStrings[WSLPERK];else if(key=="$WSLSUM")label=szStrings[WSLSUM];
            else if(row.damageType>=0){value.Format("damage_%d",row.damageType);pTxtMng->GetSystemString(value,label);}
            else{
                const bool second=key=="infoitem_58#2";if(second)key="infoitem_58";
                const CString text=key.find("PERK ")==0?CString(key.c_str()):CString("string ")+key.c_str();
                if(pTxtMng->GetText(text))pTxtMng->GetLine(label);else label=key.c_str();
                if(key=="infoitem_58")label=CString(second?"    2. ":"    1. ")+label;
                if((damagePanel && key=="PERK dexter0") || (pSkillInfo->nType==5 && (key=="infoskill_26"||key=="infoskill_31"||key=="infoskill_32")))label+=":";
            }
            if(unitLabel=="difficulty"){
                CString difficulty,levelText;if(pTxtMng->GetText("string infoskill_53"))pTxtMng->GetLine(difficulty);
                levelText.Format("(%.0f %s)",row.second,(LPCSTR)difficulty);label+=levelText;
            }
            if(damagePanel && row.damageType>=0){
                CString weapon;if(pTxtMng->GetText("string infoitem_58"))pTxtMng->GetLine(weapon);
                CString combined;if(dual)combined.Format("  %d %s %s",hand,(LPCSTR)weapon,(LPCSTR)label);
                else combined.Format("  %s %s",(LPCSTR)weapon,(LPCSTR)label);
                value.Format(row.format,row.value,row.second);
                pSurf->DrawText(combined,rc,0,INV_COLOR_INFO,DT_LEFT);
                pSurf->DrawText(value,rc,0,INV_COLOR_INFO,DT_RIGHT);
            }
            else if(pSkillInfo->nType==0 && pSkillInfo->nSkill==1 && !row.heading){
                value.Format(row.format,row.value);
                if(!unitLabel.empty()){CString unit;if(pTxtMng->GetText(CString("string ")+unitLabel.c_str()))pTxtMng->GetLine(unit);value+=unit;}
                pSurf->DrawText(label+" "+value,rc,0,color,DT_LEFT);
            }
            else if(row.heading)pSurf->DrawText(label,rc,0,damagePanel?INV_COLOR_INFO:color,
                damagePanel||key=="infoitem_58"?DT_LEFT:DT_CENTER);
            else{
                if(row.damageType>=0||key=="infoskill_1")value.Format(row.format,row.value,row.second);else value.Format(row.format,row.value);
                if(!unitLabel.empty()&&unitLabel!="difficulty"){CString text;if(pTxtMng->GetText(CString("string ")+unitLabel.c_str()))pTxtMng->GetLine(text);value+=text;}
                pSurf->DrawText(CString(damagePanel?(key=="infoitem_64"?"":"  "):"     ")+label,rc,0,damagePanel?INV_COLOR_INFO:color,DT_LEFT);
                pSurf->DrawText(value,rc-CPoint(damagePanel||(pSkillInfo->nType==0&&pSkillInfo->nSkill==7&&key!="infoskill_1")?0:10,0),0,damagePanel?INV_COLOR_INFO:color,DT_RIGHT);
            }
            rc+=shift;
            if(pSkillInfo->nType==5 && key=="$WSLSUM")rc+=shift;
        }
    };

    if(mod){
switch (pSkillInfo->nType)
	{
	case 0: {
		szName = CString("perk ") + pSkillInfo->szNonlocalName ;
		if (pTxtMng->GetText(szName ) ) pTxtMng->GetLine(szName);
		CString descKey=pSkillInfo->szNonlocalName+"_desc";
        if(pTxtMng->GetText(CString("perk ")+descKey)) {
            pTxtMng->GetUntilNextCommand(szDesc);szDesc.TrimRight("\r\n");pTxtMng->CloseText();
        } else szDesc=descKey;
		break;
	}
	case 1:
	case 2:
	case 3: {
		ERPGAutoSkills eSkill = (ERPGAutoSkills)pSkillInfo->nSkill;

		if (eSkill != 4 && eSkill != 5) {
			if (pTxtMng->GetText(CString(CString("perk ") + pSkillInfo->szNonlocalName)))
			{
				pTxtMng->GetLine(szName);
				pTxtMng->GetLine(szDesc);
			}
		}
		break;
	}
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	{
		if (pTxtMng->GetText(pSkillInfo->szNonlocalName)) pTxtMng->GetLine(szName);
		pTxtMng->GetSystemString(CString(pSkillInfo->szNonlocalName + "_desc"), szDesc);
		break;
	}

	}
    }else switch (pSkillInfo->nType)
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

    if(!mod || !szName.IsEmpty()) {
        pSurf->DrawMultiText(szName, rc, 2, 0, INV_COLOR_HEADER, DT_CENTER | DT_WORDBREAK, TRUE, mod?0:12);
        rc += shift;
    }
    if(!mod || (pSkillInfo->nType==0 && pSkillInfo->nSkill==7))rc += shift;

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
                        result = maxHp * hpRegeneration * ModOutOfCombatRegeneration::GetMultiplier(regenUnit) + regenerationSpell;
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
        pSurf->DrawMultiText(szDesc, rc, 13, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, mod?0:12);
    }

    if (pSkillInfo->nType == 1 && (!mod || pSkillInfo->nSkill<4))
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
                for(int subtype=0;subtype<3;++subtype) {
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
        pSurf->DrawMultiText(szDesc, rc, 13, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, mod?0:12);
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
            szTmp=szSkillStr[USECOMPLEX];
            if(mod && pTxtMng->GetText("string infoskill_44"))pTxtMng->GetLine(szTmp);
            szOut.Format("%s %d", szTmp,
                         mod && shown?ModCampStats::ScienceSkill(*shown):int(pSkillInfo->pStats->GetManualSkill(eSkill)));
            pSurf->DrawText(szOut, rc, 0, color, DT_LEFT);
            rc += shift;
            break;
        }
        rc += shift;
        // DESCRIPTION
        pSurf->DrawMultiText(szDesc, rc, 13, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, mod?0:12);
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
        pSurf->DrawMultiText(szDesc, rc, 6, 0, color, mod?(DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS | DT_WORD_ELLIPSIS):(DT_CENTER | DT_WORDBREAK), TRUE, mod?0:12);
    }

    if (pSkillInfo->nType == 4)
    {
        pSurf->DrawText(szStrings[ABSORPARMOR], rc, 0, mod?INV_COLOR_HEADER:color, mod?DT_CENTER:DT_LEFT);
        rc += shift;
        if(mod)rc += shift;

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
        pSurf->DrawMultiText(szDesc, rc, 8, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, mod?0:12);
    }

    if (pSkillInfo->nType == 5 && full){
        pSurf->DrawText(CString("     ")+szStrings[pSkillInfo->szNonlocalName=="defense"?MODDEFENSE:MODATTACK],rc,0,INV_COLOR_HEADER,DT_CENTER);rc+=shift;rc+=shift;
        drawRows(pSkillInfo->szNonlocalName=="defense"?display.defenseRows:display.attackRows);
    }
    else if (pSkillInfo->nType == 5)
    {
        ERPGWeaponTypes type = pSkillInfo->pWeapon->GetWeaponType();
        if (pSkillInfo->szNonlocalName == "defense" &&
            (type == RPGWST_BOW || type == RPGWST_CROSSBOW))
        {
            pSurf->DrawMultiText(szStrings[WSLBOWDEFENSE], rc, 2, 0, color,
                                 DT_CENTER | DT_WORDBREAK, TRUE, mod?0:12);
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
        pSurf->DrawMultiText(szDesc, rc, 9, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, mod?0:12);
    }

    if (pSkillInfo->nType == 6)
    {
        int nStr = MSLFIRE;
        // Addon has three elemental subtypes; retain native storage for save compatibility.
        if(mod){pSurf->DrawText(szStrings[MAGICSCHOOL],rc,0,INV_COLOR_HEADER,DT_CENTER);rc+=shift;rc+=shift;}
        const int subtypeCount = mod ? 3 : RPG_NUM_MAGIC_SUBTYPES;
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
        pSurf->DrawMultiText(szDesc, rc, 9, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, mod?0:12);
    }

    if(pSkillInfo->nType==7 && pSkillInfo->szNonlocalName=="camp_damage" && full){
        CPersonage* pers=pScreen->GetCurrentPers();
        const int slot=int(pers->dwCurrentWeapon);
        if(slot>=0 && slot<pers->weapons.GetSize() && pers->weapons[slot]){
            pSurf->DrawText(szStrings[DAMAGE],rc,0,INV_COLOR_HEADER,DT_CENTER);rc+=shift;
            drawRows(display.damageRows);
        }
    }else if (pSkillInfo->nType == 7)
        pSurf->DrawMultiText(szDesc, rc, 16, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, mod?0:12);

    pSurf->ReleaseDC();
#endif // NOGETDC
}
/////////////////////////////////////////////////////////////////////////////////////
void CI3DObjectItemInfo::SetHelp(int nHelpID)
{
    E4CampAccess::Content publication(this,owned::camp::frontend::HoverKind::help,nHelpID!=0,nHelpID);
    SetItem(0);


#ifndef NOGETDC
    CI3DSurface *pSurf = surfaces[0];
    pSurf->GetDC();

    CTextManager *pTxtMng = theWorld.GetTextManager();

    CRect rc(15, 10, 185, 25);
    const auto* mod = ModRuntime::Current();
    const int lineHeight = mod && mod->CombatSetting("JMVPerks") ? 15 : 12;
    CPoint shift(0, lineHeight);
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
    pSurf->DrawMultiText(szName, rc, 2, 0, INV_COLOR_HEADER, DT_CENTER | DT_WORDBREAK, TRUE, lineHeight);
    rc += shift;
    rc += shift;

    // DESCRIPTION
    pSurf->DrawMultiText(szDesc, rc, 16, 0, color, DT_CENTER | DT_WORDBREAK, TRUE, lineHeight);

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
