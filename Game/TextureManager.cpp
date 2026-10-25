#include "StdAfx.h"
#include "RuntimePaths64.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "TextureManager.h"
#include "camp_inventory.h"
#include "OwnedRender/E3HudVisual.h"
#include "ModHDEquipmentLayer.h"
#include "GameJournal.h"
#include "ModSecondaryAtlas.h"
#include "ModPrimaryAtlas.h"
#include "ModTextureMemory.h"
#include "ModPnt3Texture.h"

#include "mmpFormat.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "ModCriticalLifecycle.h"
#include "ModHitHistory.h"
#include <stdexcept>
#include "WorldClient.h"
#include "Renderer.h"
/////////////////////////////////////////////////////////////////////////////////////
CTextureManager::CTextureManager()
{
    nTotalLRUSize = 0;
    wStartMipMapLevel = 0;
    pRend = 0;

#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CTextureManager::~CTextureManager()
{
    Destroy();
    SetInterface(0);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTextureManager::SetInterface( CRenderer* pNewRend )
// set IDirect3DDevice# interface for further use during texture creating/loading/etc.
void CTextureManager::SetInterface(CRenderer *pNewRend)
{
    pRend = pNewRend;
    if (pRend)
    {
        switch (theSettings.nTextureQuality)
        {
        case 0:
            wStartMipMapLevel = 0;
            break;
        case 1:
            wStartMipMapLevel = 1;
            break;
        case 2:
            wStartMipMapLevel = 2;
            break;
        default:
            wStartMipMapLevel = 3;
        }
        nAllowedLRUSize = 1024 * 1024 * 128;
    }
    else
    {
        wStartMipMapLevel = 2;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CTextureManager::Create()
// create internal data and open resource
BOOL CTextureManager::Create()
{
    OwnedRender::E3HudRoleCreation hudResources(this,true);
    if (CResourceManager::Create() == FALSE)
        return FALSE;
    // delete unit layer textures
    DeleteUnitLayerTextures();
    //
    CString szResourcesPath = ((CWorld *)GetWorld())->GetResourcePath();
    BOOL bCheck = AddGraphicsResources("textures.res");
    bCheck = AddGraphicsResources("redress.res") && bCheck;
    bCheck = AddNewResource(szResourcesPath + "textures.res") && bCheck;
    bCheck = AddNewResource(szResourcesPath + "redress.res") && bCheck;
    bCheck = AddNewResource(szResourcesPath + "menus.res") && bCheck;
    return hudResources.Complete(bCheck);
}
/////////////////////////////////////////////////////////////////////////////////////
void CTextureManager::Destroy()
{
    owned::camp_inventory_texture_manager_destroying(this);
    pShadowBufferTexture = 0;
    //
    LPTEXTURE lpTexture = 0;
    POSITION pos = lru.GetHeadPosition();
    // destroy LRU map
    while (pos)
    {
        lpTexture = lru.GetNext(pos);
        lpTexture->Destroy();
        delete lpTexture;
    }
    lru.RemoveAll();
    nTotalLRUSize = 0;
    //
    DeleteUnitLayerTextures();
    pRend = 0;
    //
    CResourceManager::Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
namespace {
// Direct PNT3 uses the selected addon decoder, then the existing surface upload.
// Its linear-size field is an encoded byte count, never a mip count.
BOOL LoadPnt3Texture(CRenderer* renderer, LPTEXTURE texture, const void* source, std::size_t bytes) {
    const char* reason = "exception";
    BOOL loaded = FALSE;
    try {
        std::vector<uint32_t> decoded;
        if(Pnt3Texture::Decode(source,bytes,decoded,reason)) {
            auto* header=reinterpret_cast<SMMPFileHeader*>(decoded.data());
            DXGI_FORMAT format;
            if(renderer->FindBestTextureFormat(header,format) ||
               renderer->FindTextureFormat(MMP_FORMAT_4444,format)) {
                texture->SetTextureFormat(format);
                loaded=texture->Load(renderer,header,0);
                reason=loaded?"loaded":"surface-load-failed";
            } else reason="surface-format-unavailable";
        }
    } catch(...) { GameJournal::AllocationFailureInCatch("TextureManager");reason="decode-or-load-exception";}
    if(!loaded)ReportWarning("CTextureManager::LoadPnt3Texture. Texture \"%s\" rejected (%s).\n",
                            texture->GetName(),reason);
    return loaded;
}
// Process-lifetime storage avoids static World destructor ordering. Entries are
// bounded and cleared at the existing layer/world teardown; no texture ownership.
ModSecondaryAtlas::Registry& SecondaryAtlasRecipes() {
    static auto* registry=new ModSecondaryAtlas::Registry;return *registry;
}
}
BOOL CTextureManager::ComposeTexturesInMemory() const {
    const auto* package=ModRuntime::Current();
    return package && package->CombatSetting("JMVGraphics") && package->CombatSetting("writeComposeInMemory");
}
BOOL CTextureManager::RebuildSharedAtlasCpu(LPTEXTURE texture) {
    if(!texture || !ShareSecondaryCharacterAtlas() || !ComposeTexturesInMemory())return FALSE;
    const char* name=texture->GetName();
    const bool primary=ModPrimaryAtlas::Canonical(name);
    if((!primary && !ModSecondaryAtlas::Canonical(name)) ||
       texture->GetTypeID()!=(primary?TTID_UNIT_MAIN:TTID_UNIT_WEAPON))return FALSE;
    const auto* encoded=SecondaryAtlasRecipes().Find(name);
    if(!encoded || ModSecondaryAtlas::Name(*encoded,primary)!=name)return FALSE;
    ModSecondaryAtlas::DecodedRecipe recipe;
    if(!ModSecondaryAtlas::Decode(*encoded,recipe) || recipe.size!=1024 ||
       !recipe.mips || recipe.mips>11 || recipe.format!=MMP_FORMAT_8888)return FALSE;
    STextureSetDescription set;
    set.characterAtlas=primary?3:4;
    set.dwTextureDimention=recipe.size;
    set.data.SetSize(recipe.data.size());
    if(!recipe.data.empty())std::memcpy(set.data.GetData(),recipe.data.data(),recipe.data.size());
    for(int i=0;i<4;++i)set.equipmentSlots[i]=recipe.slots[i];
    for(const auto& layer:recipe.layers) {
        // A replay must not silently invent a replacement for an absent layer.
        auto* header=reinterpret_cast<SMMPFileHeader*>(GetFile(layer.c_str()));
        if(!header)return FALSE;
        struct CloseLayer {CTextureManager* manager;BYTE* bytes;~CloseLayer(){manager->CloseFile(bytes);}} close{this,reinterpret_cast<BYTE*>(header)};
        const DWORD dimension=primary?recipe.size:512;
        if(header->dwDimentionX!=dimension || header->dwDimentionY!=dimension)return FALSE;
        set.szTextures.Add(layer.c_str());
    }
    // Direct memory composition uses the original resolved HD names and slots;
    // it cannot recurse into provider reuse, select current equipment, or write disk.
    CString result;
    if(!ComposeTextureMemory(result,set,name,recipe.mips,recipe.format) || result!=name)return FALSE;
    return ModTextureMemory::Exact(ModTextureMemory::State().cache.Get(name),recipe.size,
                                  recipe.mips,recipe.format,recipe.data)?TRUE:FALSE;
}
BOOL CTextureManager::LoadTextureMemory(LPTEXTURE texture) {
    const bool privateAlias=SharePrimaryCharacterAtlas() && ModPrimaryAtlas::Private(texture->GetName());
    const bool shared=(texture->GetTypeID()==TTID_UNIT_MAIN && ModPrimaryAtlas::Canonical(texture->GetName())) ||
        (texture->GetTypeID()==TTID_UNIT_WEAPON && ModSecondaryAtlas::Canonical(texture->GetName()));
    struct RestoreShared {
        CTexture* texture;bool active,ready=false;
        ~RestoreShared(){if(active && !ready)texture->Unload();}
    } restoring{texture,shared && !texture->GetSurface()};
    if(restoring.active)texture->ResetFlag(TEX_LOADED);
    DWORD pnt3SourceSize=0;
    const bool loaded=ModTextureMemory::LoadSource(ModTextureMemory::State().cache,ModPrimaryAtlas::State(),
        texture->GetName(),privateAlias,
        [&](){return reinterpret_cast<SMMPFileHeader*>(GetFile(CString(texture->GetName())+".mmp",&pnt3SourceSize));},
        [&](SMMPFileHeader* header,bool resource) {
            struct CloseResource {CTextureManager* manager;BYTE* bytes;~CloseResource(){if(bytes)manager->CloseFile(bytes);}} close{this,resource?reinterpret_cast<BYTE*>(header):nullptr};
            if(resource && pnt3SourceSize>=sizeof(SMMPFileHeader) && header->dwFormat==MMP_FORMAT_PNT3)
                return LoadPnt3Texture(pRend,texture,header,pnt3SourceSize)!=FALSE;
            DXGI_FORMAT format;
            if(!pRend->FindBestTextureFormat(header,format)) {
                if(!resource || header->dwFormat!=MMP_FORMAT_8888 || !pRend->FindTextureFormat(MMP_FORMAT_4444,format))return false;
            }
            texture->SetTextureFormat(format);
            const DWORD first=resource && wStartMipMapLevel>=header->dwMipMapLevel?header->dwMipMapLevel-1:wStartMipMapLevel;
            const BOOL loaded=texture->Load(pRend,header,first);
            return resource?loaded!=FALSE:true;
        },[&](const char*){return RebuildSharedAtlasCpu(texture)!=FALSE;});
    restoring.ready=loaded && texture->GetSurface()!=nullptr;
    return (shared?restoring.ready:loaded)?TRUE:FALSE;
}
BOOL CTextureManager::ComposeTextureMemory(CString& result,const STextureSetDescription& set,
                                          const CString& desired,DWORD mips,DWORD format) {
    result=desired;
    std::string user;
    if(set.data.GetSize())user.assign(reinterpret_cast<const char*>(set.data.GetData()),set.data.GetSize());
    auto& cache=ModTextureMemory::State().cache;
    const bool canonical=ModPrimaryAtlas::Canonical(desired)||ModSecondaryAtlas::Canonical(desired);
    if(ShareSecondaryCharacterAtlas() && canonical && ModTextureMemory::Exact(cache.Get((LPCSTR)desired),set.dwTextureDimention,mips,format,user))return TRUE;
    if(!set.szTextures.GetSize()){cache.Remove((LPCSTR)desired);return FALSE;}
    try {
        SMMPFileHeader h;h.dwSignature=MMP_SIGNATURE;h.InitializeDefaultFormat(format);
        h.dwDimentionX=set.dwTextureDimention;h.dwDimentionY=set.dwTextureDimention;
        h.dwMipMapLevel=mips;h.dwUserDataShift=DWORD(user.size());
        ETextureFormats conversion=TEX_FORMAT_565;
        if(format==MMP_FORMAT_5551)conversion=TEX_FORMAT_1555;
        else if(format==MMP_FORMAT_4444)conversion=TEX_FORMAT_4444;
        else if(format!=MMP_FORMAT_5650 && format!=MMP_FORMAT_8888)return FALSE;
        // Native complete-MMP allocation precedes temporary pixels and layers.
        ModTextureMemory::Payload output;
        if(!ModTextureMemory::ReserveOutput(output,h,user.size(),theSettings.nDrawVisibleOnly,
            [](std::vector<DWORD>& words,std::size_t count){words.resize(count);return true;})){
            GameJournal::AllocationFailure("complete_mmp_output");return FALSE;
        }
        // Native memory composition initializes to zero, independently of BGColor.
        std::vector<DWORD> pixels(std::size_t(set.dwTextureDimention)*set.dwTextureDimention,0);
        auto* graphics=theRenderer.GetGraphicsProcessor();
        const auto* package=ModRuntime::Current();
        const bool debug=package && package->CombatSetting("showJmvDebug");
        const bool equipment=set.characterAtlas==4;
        std::vector<DWORD> layer(equipment?512*512:0);
        int x=0,y=0;
        for(int i=0;i<set.szTextures.GetSize();++i) {
            auto* h=reinterpret_cast<SMMPFileHeader*>(GetFile(set.szTextures[i]));
            if(!h){
                ReportWarning("Missing memory texture layer %s",(LPCSTR)set.szTextures[i]);
                if(!equipment && debug)std::fill(pixels.begin(),pixels.end(),0);
                continue; // Native counts missing layers; zero pixels still compose.
            }
            struct CloseLayer {CTextureManager* manager;BYTE* bytes;~CloseLayer(){manager->CloseFile(bytes);}} close{this,reinterpret_cast<BYTE*>(h)};
            const DWORD dimension=equipment?512:set.dwTextureDimention;
            if(h->dwDimentionX!=dimension || h->dwDimentionY!=dimension)return FALSE;
            DWORD* output=pixels.data();
            if(equipment){std::fill(layer.begin(),layer.end(),0);output=layer.data();}
            graphics->PutOneLayer(output,reinterpret_cast<DWORD*>(h->GetMipLevelData(0)),int(dimension*dimension),h->dwFormat==MMP_FORMAT_PNT3);
            if(equipment){
                if(set.equipmentSlots[0]==i){x=0;y=512;}
                else if(set.equipmentSlots[1]==i){x=512;y=512;}
                else if(set.equipmentSlots[2]==i){x=0;y=0;}
                else if(set.equipmentSlots[3]==i){x=512;y=0;}
                for(int row=0;row<512;++row)std::memcpy(pixels.data()+(y+row)*1024+x,layer.data()+row*512,512*sizeof(DWORD));
            }
        }
        auto payload=ModTextureMemory::EncodeInto(std::move(output),h,user,std::move(pixels),
            [&](WORD* dst,DWORD* src,int count){graphics->Convert32To16Bit(dst,src,conversion,count);},
            [&](DWORD* dst,DWORD* src,int dimension){graphics->GenerateMipmapLevel(dst,src,dimension);});
        cache.Put((LPCSTR)desired,std::move(payload));return TRUE;
    }catch(const std::bad_alloc&){ GameJournal::AllocationFailureInCatch("TextureManager");return FALSE;}
}

BOOL CTextureManager::ShareSecondaryCharacterAtlas() const {
    auto* package=ModRuntime::Current();
    return package && package->CombatSetting("JMVGraphics") &&
           package->CombatSetting("JMVCharacterAtlasSharing");
}
BOOL CTextureManager::SharePrimaryCharacterAtlas() const {
    return ShareSecondaryCharacterAtlas();
}
void CTextureManager::DeleteUnitLayerTextures()
{
    ModTextureMemory::State().cache.Clear();
    ModTextureMemory::State().forceCanonicalCpu=false;
    SecondaryAtlasRecipes().Clear();
    ModPrimaryAtlas::State().Clear();
    ModCriticalLifecycle::Clear(ModRuntime::Current());
    ModHitHistory::ClearRuntimeHitHistory("texture-layer-delete");
    CFileFind find;
    CString szMaskName = CString(RuntimePaths64::Textures().c_str()) + "*.mmp";
    BOOL bCheck = find.FindFile(szMaskName);
    //
    while (bCheck)
    {
        bCheck = find.FindNextFile();
        if (DeleteFile(find.GetFilePath()) == 0)
            ReportWarning("Can't delete file \"%s\" due to %d.\n", find.GetFileName(),
                          GetLastError());
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE CTextureManager::CreateInstant( ETypeIDs tidTypeID )
// create data instant with desired type ID
LPGAMEBASE CTextureManager::CreateInstant(ETypeIDs tidTypeID)
{
    LPTEXTURE pTexture = dbgnew CTexture();
    if (pTexture == NULL)
        return NULL;
    pTexture->SetTypeID(tidTypeID);

    return pTexture;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPGAMEBASE CTextureManager::CreateData( ETypeIDs tidTypeID, int id )
// create data via CResourceManager and set type flag to it
LPGAMEBASE CTextureManager::CreateData(ETypeIDs tidTypeID, int id)
{
    LPGAMEBASE lpData = CResourceManager::CreateData(tidTypeID, id);
    // set type flag
    switch (tidTypeID)
    {
    case TCTID_STATIC_TEXTURE:
        lpData->SetFlag(TEX_STATIC);
        break;
    case TCTID_PAINT_TEXTURE:
        lpData->SetFlag(TEX_PAINT);
        break;
    case TCTID_DYNAMIC_TEXTURE:
        lpData->SetFlag(TEX_DYNAMIC);
        break;
    case TCTID_SHADOW_TEXTURE:
        lpData->SetFlag(TEX_SHADOW);
        break;
    default:
        lpData->SetFlag(TEX_STATIC);
    }

    return lpData;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTextureManager::Release(LPGAMEBASE lpData)
{
    if (!lpData || lpData->IsReferenced())
        return;

    LPGAMEBASE pValue = NULL;
    int key = lpData->GetID();

    if (hash.Lookup(key, pValue))
    {
        pValue = RemoveInstantAt(pValue->GetID());
        if (!lpData->CheckFlag(TEX_LOADED | TEX_INSYSTEM | TEX_INVIDEO))
        {
            lpData->Destroy();
            lpData->SetManager(NULL);
            MoveToTrash(lpData);

            return;
        }
        // place it in the LRU list
        lru.AddTail((LPTEXTURE)lpData);
        nTotalLRUSize += ((LPTEXTURE)lpData)->GetTotalSize();
    }
    else // this is an texture from the LRU or 'missed' one
    {
        lpData->Destroy();
        lpData->SetManager(NULL);
        MoveToTrash(lpData);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// get texture from LRU list by name and type
LPTEXTURE CTextureManager::GetFromLRU(ETypeIDs tidTypeID, LPCSTR pszName)
{
    POSITION pos = lru.GetHeadPosition();
    POSITION lastpos = pos;
    LPTEXTURE lpTexture = NULL;

    while (pos)
    {
        lpTexture = lru.GetNext(pos);
        if ((lpTexture->GetTypeID() == tidTypeID) && (strcmp(lpTexture->GetName(), pszName) == 0))
        {
            TRACE1("Get texture \"%s\" from LRU map.\n", lpTexture->GetName());
            lru.RemoveAt(lastpos); // remove from LRU list
            nTotalLRUSize -= lpTexture->GetTotalSize();
            RegisterInstant(lpTexture); // register in main hash table
            return lpTexture;
        }
        lastpos = pos;
    }

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPBYTE CTextureManager::GetMMPData( LPCSTR pszMMPName )
// get mip-map data from mapped resource
LPBYTE CTextureManager::GetMMPData(LPCSTR pszMMPName)
{
    return GetFile(pszMMPName);
}
/////////////////////////////////////////////////////////////////////////////////////
// LPTEXTURE CTextureManager::CreateTexture( ETypeIDs tid, LPCSTR pszName )
// create texture load data from file
LPTEXTURE CTextureManager::CreateTexture(ETypeIDs tid, LPCSTR pszName)
{
    LPTEXTURE pTexture = FindTexture(tid, pszName);
    if (!pTexture)
        pTexture = (LPTEXTURE)CreateData(tid);
    pTexture->SetName(pszName);

    return pTexture;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPTEXTURE CTextureManager::CreateTexture( ETypeIDs tidTypeID )
// create texture by typeID
LPTEXTURE CTextureManager::CreateTexture(ETypeIDs tidTypeID)
{
    return (LPTEXTURE)CreateData(tidTypeID);
}
DWORD textureTypeToMMPFormat(ETypeIDs eTextureTypeID)
{
    switch (eTextureTypeID)
    {
    case TFTID_565:
        return MMP_FORMAT_5650;
    case TFTID_5551:
        return MMP_FORMAT_5551;
    case TFTID_4444:
        return MMP_FORMAT_4444;
    case TFTID_8888:
        return MMP_FORMAT_8888;
    default:
        GameJournal::Event("ERROR", "texture_format_skipped", "reason=unknown_type type_id=%d", eTextureTypeID);
        ReportWarning("Can't find texture format %d.\n", eTextureTypeID);
        return -1;
    }
}

/////////////////////////////////////////////////////////////////////////////////////
LPTEXTURE CTextureManager::CreateInterfaceTexture(DWORD dwSizeX, DWORD dwSizeY,
                                                  ETypeIDs eTextureTypeID)
{
    LPTEXTURE lpTexture = (LPTEXTURE)CreateData(eTextureTypeID);
    SMMPFileHeader hdr;
    memset(&hdr, 0, sizeof(SMMPFileHeader));
    lpTexture->SetSize(dwSizeX, dwSizeY);

    DWORD mmpFormat = textureTypeToMMPFormat(eTextureTypeID);
    if (mmpFormat == DWORD(-1))
    {
        lpTexture->Release();
        return nullptr;
    }
    if (mmpFormat == 0)
        return nullptr;
    DXGI_FORMAT texFormat;
    if (pRend->FindTextureFormat(mmpFormat, texFormat))
        lpTexture->SetTextureFormat(texFormat);
    hdr.dwFormat = mmpFormat;
    hdr.InitializeDefaultFormat(mmpFormat);
    hdr.dwSignature = MMP_SIGNATURE;
    hdr.dwDimentionX = dwSizeX;
    hdr.dwDimentionY = dwSizeY;
    hdr.dwMipMapLevel = 1;
    // create surfaces
    if (lpTexture->Create(pRend, &hdr) == FALSE)
    {
        lpTexture->Release();
        return 0;
    }

    return lpTexture;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPTEXTURE CTextureManager::CreateShadowTexture()
// create 5551 format texture with 64x64 dimentions for rendering shadows
LPTEXTURE CTextureManager::CreateShadowTexture()
{
    LPTEXTURE lpTexture = (LPTEXTURE)CreateData(TFTID_ALPHA4);
    DXGI_FORMAT texFormat;
    SMMPFileHeader hdr;
    //
    lpTexture->SetSize(64, 64);
    if (pRend->FindTextureFormat(MMP_FORMAT_4444, texFormat))
        lpTexture->SetTextureFormat(texFormat);
    else
    {
        lpTexture->Release();
        GameJournal::Event("ERROR", "shadow_texture_skipped", "reason=unsupported_format format=0x%08X", MMP_FORMAT_4444);
        ReportWarning("Can't find texture format %d.\n", TFTID_4444);
        return NULL;
    }
    // fill mmp header to create surfaces
    hdr.InitializeDefaultFormat4444();
    hdr.dwSignature = MMP_SIGNATURE;
    hdr.dwDimentionX = 64;
    hdr.dwDimentionY = 64;
    hdr.dwMipMapLevel = 1;
    //
    lpTexture->ResetFlag(TEX_STATIC | TEX_PAINT | TEX_DYNAMIC);
    lpTexture->SetFlag(TEX_SHADOW);
    // create surfaces
    if (lpTexture->Create(pRend, &hdr) == FALSE)
    {
        lpTexture->Release();
        return 0;
    }
    //
    return lpTexture;
}
/////////////////////////////////////////////////////////////////////////////////////
LPTEXTURE CTextureManager::GetShadowBuffer()
{
    if (pShadowBufferTexture)
        return pShadowBufferTexture;
    //
    LPTEXTURE lpTexture = (LPTEXTURE)CreateData(TFTID_ALPHA4);
    DXGI_FORMAT texFormat;
    SMMPFileHeader hdr;
    //
    lpTexture->SetSize(64, 64);
    if (pRend->FindTextureFormat(MMP_FORMAT_4444, texFormat))
        lpTexture->SetTextureFormat(texFormat);
    else
    {
        lpTexture->Release();
        GameJournal::Event("ERROR", "shadow_texture_skipped", "reason=unsupported_format format=0x%08X", MMP_FORMAT_4444);
        ReportWarning("Can't find texture format %d.\n", TFTID_4444);
        return NULL;
    }
    // fill mmp header to create surfaces
    hdr.InitializeDefaultFormat4444();
    hdr.dwSignature = MMP_SIGNATURE;
    hdr.dwDimentionX = 64;
    hdr.dwDimentionY = 64;
    hdr.dwMipMapLevel = 1;
    //
    lpTexture->ResetFlag(TEX_STATIC | TEX_PAINT | TEX_DYNAMIC | TEX_SHADOW);
    lpTexture->SetFlag(TEX_SYSMEM);
    // create surfaces
    if (lpTexture->Create(pRend, &hdr) == FALSE)
    {
        lpTexture->Release();
        return 0;
    }
    //
    pShadowBufferTexture = lpTexture;
    //
    return pShadowBufferTexture;
}
/////////////////////////////////////////////////////////////////////////////////////
// load texture in accordance with it's file name
BOOL CTextureManager::LoadTexture(LPTEXTURE lpTexture, BOOL bReportMissing)
{
    if (lpTexture == 0)
    {
        ReportWarning("CTextureManager::LoadTexture.\n Can't load texture to NULL pointer.\n");
        return FALSE;
    }
    if (lpTexture->CheckFlag(TEX_LOADED))
    {
        ReportWarning("Load already loaded texture \"%s\".\n", lpTexture->GetName());
        return TRUE;
    }

    if(ComposeTexturesInMemory())return LoadTextureMemory(lpTexture);
    const bool privateAlias=SharePrimaryCharacterAtlas() && ModPrimaryAtlas::Private(lpTexture->GetName());
    const char* textureName=lpTexture->GetName();
    std::string sourceName=textureName?textureName:"";
    if(privateAlias && !ModPrimaryAtlas::State().Source(lpTexture->GetName(),sourceName))return FALSE;
    ETypeIDs tidTextureFormat = TID_EMPTY;
    BOOL bCheck = FALSE;
    DXGI_FORMAT texFormat;
    // open file
    CFileFind find;
    CString szString = theSettings.szWorkPath + "textures\\" + sourceName.c_str() + ".bmp";
    DWORD dwTextureFormat;
    if (find.FindFile(szString)) // first, trying to open .bmp file
    {
        switch (lpTexture->GetTypeID())
        {
        case TTID_WATER_MAIN:
        case TTID_TERRAIN:
            dwTextureFormat = MMP_FORMAT_5650;
            break;
        case TTID_UNIT_MAIN:
        case TTID_UNIT_WEAPON:
        case TTID_HOUSE_MAIN:
        case TTID_OBJECT_MAIN:
        case TTID_TREE_MAIN:
            dwTextureFormat = MMP_FORMAT_5551;
            break;
        case TTID_SPELL_MAIN:
        case TTID_ANIMATED1:
            dwTextureFormat = MMP_FORMAT_4444;
            break;
        default:
            dwTextureFormat = MMP_FORMAT_5650;
        }
        if (pRend->FindTextureFormat(dwTextureFormat, texFormat))
            lpTexture->SetTextureFormat(texFormat);
        else
        {
            GameJournal::Event("ERROR", "texture_load_skipped",
                "name=%s reason=unsupported_source_format format=0x%08X", lpTexture->GetName(), dwTextureFormat);
            ReportWarning("CTextureManager::LoadTexture.\n Can't find desired texture format "
                          "%d for texture \"%s\".\n", tidTextureFormat, lpTexture->GetName());
            return FALSE;
        }
        //
        return lpTexture->Load(pRend, szString);
    }
    else
    {
        szString = CString(sourceName.c_str()) + ".mmp";
        DWORD pnt3SourceSize=0;
        LPMMPFILEHEADER lpHdr = reinterpret_cast<SMMPFileHeader *>(GetFile(szString,&pnt3SourceSize));
        if (lpHdr)
        {
            if(pnt3SourceSize>=sizeof(SMMPFileHeader) && lpHdr->dwFormat==MMP_FORMAT_PNT3) {
                const BOOL loaded=LoadPnt3Texture(pRend,lpTexture,lpHdr,pnt3SourceSize);
                CloseFile(reinterpret_cast<BYTE*>(lpHdr));
                return loaded;
            }
            if (pRend->FindBestTextureFormat(lpHdr, texFormat))
                lpTexture->SetTextureFormat(texFormat);
            else
            {
                if (lpHdr->dwFormat == MMP_FORMAT_8888 &&
                    pRend->FindTextureFormat(MMP_FORMAT_4444, texFormat))
                    lpTexture->SetTextureFormat(texFormat);
                else
                {
                    const DWORD format=lpHdr->dwFormat;
                    CloseFile(reinterpret_cast<BYTE*>(lpHdr));
                    GameJournal::Event("ERROR", "texture_load_skipped",
                        "name=%s reason=unsupported_source_format format=0x%08X", lpTexture->GetName(), format);
                    ReportWarning("CTextureManager::LoadTexture.\n Can't find texture format "
                                  "for texture \"%s\" (0x%08X).\n", lpTexture->GetName(), format);
                    return FALSE;
                }
            }
            BOOL bRetVal;
            if (wStartMipMapLevel >= lpHdr->dwMipMapLevel)
                bRetVal = lpTexture->Load(pRend, lpHdr, lpHdr->dwMipMapLevel - 1);
            else
                bRetVal = lpTexture->Load(pRend, lpHdr, wStartMipMapLevel);
            CloseFile((BYTE *)lpHdr);
            return bRetVal;
        }
        else
        {
            CFile file;
            szString.Format("%s%s.mmp", RuntimePaths64::Textures().c_str(),
                            sourceName.c_str());
            if (file.Open(szString, CFile::modeRead) == FALSE)
            {
                if (bReportMissing)
                    ReportWarning("CTextureManager::LoadTexture.\n Can't find texture \"%s\"\n",
                                  lpTexture->GetName());
                return privateAlias?FALSE:TRUE;
            }
            const DWORD pnt3DiskSize=static_cast<DWORD>(file.GetLength());
            LPBYTE lpData = dbgnew BYTE[pnt3DiskSize];
            const UINT pnt3ReadSize=file.Read(lpData,pnt3DiskSize);
            file.Close();
            if(pnt3ReadSize>=sizeof(SMMPFileHeader) &&
               reinterpret_cast<SMMPFileHeader*>(lpData)->dwFormat==MMP_FORMAT_PNT3) {
                const BOOL loaded=LoadPnt3Texture(pRend,lpTexture,lpData,pnt3ReadSize);
                delete[] lpData;
                return loaded;
            }

            if (pRend->FindBestTextureFormat(reinterpret_cast<SMMPFileHeader *>(lpData), texFormat))
                lpTexture->SetTextureFormat(texFormat);
            else
            {
                const DWORD format=reinterpret_cast<SMMPFileHeader*>(lpData)->dwFormat;
                delete[] lpData;
                GameJournal::Event("ERROR", "texture_load_skipped",
                    "name=%s reason=unsupported_source_format format=0x%08X", lpTexture->GetName(), format);
                ReportWarning("CTextureManager::LoadTexture.\n Can't find texture format for "
                              "texture \"%s\" (0x%08X).\n", lpTexture->GetName(), format);
                return FALSE;
            }
            lpTexture->Load(pRend, reinterpret_cast<SMMPFileHeader *>(lpData), wStartMipMapLevel);

            delete[] lpData;

            return TRUE;
        }
    }

    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// load texture from mmp file in accordance with it's file name and full directory path
BOOL CTextureManager::LoadTexture(LPTEXTURE lpTexture, LPCSTR pszDirectoryName)
{
    if (lpTexture == 0)
    {
        ReportWarning("CTextureManager::LoadTexture.\n Can't load texture to NULL pointer.\n");
        return FALSE;
    }
    if (lpTexture->CheckFlag(TEX_LOADED))
    {
        ReportWarning("Load already loaded texture \"%s\".\n", lpTexture->GetName());
        return TRUE;
    }

    CString szString;
    CFile file;

    szString.Format("%s%s.mmp", pszDirectoryName, lpTexture->GetName());
    if (file.Open(szString, CFile::modeRead) == FALSE)
    {
        ReportWarning(
            "CTextureManager::LoadTexture.\n Can't find texture \"%s\" in directory \"%s\"\n",
            lpTexture->GetName(), pszDirectoryName);
        return TRUE;
    }

    const DWORD pnt3DiskSize=static_cast<DWORD>(file.GetLength());
    LPBYTE lpData = dbgnew BYTE[pnt3DiskSize];
    const UINT pnt3ReadSize=file.Read(lpData,pnt3DiskSize);
    file.Close();
    if(pnt3ReadSize>=sizeof(SMMPFileHeader) &&
       reinterpret_cast<SMMPFileHeader*>(lpData)->dwFormat==MMP_FORMAT_PNT3) {
        const BOOL loaded=LoadPnt3Texture(pRend,lpTexture,lpData,pnt3ReadSize);
        delete[] lpData;
        return loaded;
    }

    DXGI_FORMAT texFormat;
    if (pRend->FindBestTextureFormat(reinterpret_cast<SMMPFileHeader *>(lpData), texFormat))
        lpTexture->SetTextureFormat(texFormat);
    else
    {
        const DWORD format=reinterpret_cast<SMMPFileHeader*>(lpData)->dwFormat;
        delete[] lpData;
        GameJournal::Event("ERROR", "texture_load_skipped",
            "name=%s reason=unsupported_source_format format=0x%08X", lpTexture->GetName(), format);
        ReportWarning("CTextureManager::LoadTexture.\n Can't find texture format for texture \"%s\" (0x%08X).\n",
            lpTexture->GetName(), format);
        return FALSE;
    }
    lpTexture->Load(pRend, reinterpret_cast<SMMPFileHeader *>(lpData), wStartMipMapLevel);

    delete[] lpData;
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CTextureManager::GetUserData( LPCSTR pszFileName, CBYTEArray& data )
// get user data from MMP file
BOOL CTextureManager::GetUserData(LPCSTR pszFileName, CBYTEArray &data)
{
    SMMPFileHeader *pHdr = NULL;
    SMMPFileHeader hdr;
    CString szString;
    CFile file;
    // first, try to load from file
    szString.Format("%s%s.mmp", RuntimePaths64::Textures().c_str(), pszFileName);
    if (file.Open(szString, CFile::modeRead) != FALSE)
    {
        file.Read(&hdr, sizeof(hdr));
        data.SetSize(hdr.dwUserDataShift); // ARRAY BOUNDS CHECK
        if (hdr.dwUserDataShift != 0)
            file.Read(data.GetData(), hdr.dwUserDataShift);
        file.Close();

        return TRUE;
    }
    // then, try to load from resource
    szString.Format("%s%s", pszFileName, ".mmp");
    pHdr = (SMMPFileHeader *)GetFile(szString);
    if (pHdr)
    {
        data.SetSize(pHdr->dwUserDataShift); // ARRAY BOUNDS CHECK
        if (pHdr->dwUserDataShift)
            memcpy(data.GetData(), pHdr->GetUsedData(), pHdr->dwUserDataShift);
        CloseFile((BYTE *)pHdr);

        return TRUE;
    }
    // requested data not found!
    data.SetSize(0); // ARRAY BOUNDS CHECK
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CTextureManager::LoadTextures()
// load all textures in accordance with it's file name
BOOL CTextureManager::LoadTextures()
{
    ETypeIDs tidTextureFormat = TID_EMPTY;
    BOOL bCheck = FALSE;

#ifdef _DEBUG
    if (!pRend)
        return ReportError("CTextureManager::LoadTextures.\n No valid CRenderer interface.\n");
#endif // _DEBUG

    //
    CHashMapIterator<int, LPGAMEBASE> hmi;
    LPGAMEBASE lpData = hmi.GetFirst(&hash);
    while (lpData)
    {
        LoadTexture(static_cast<LPTEXTURE>(lpData));
        lpData = hmi.GetNext();
    }

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CTextureManager::RestoreTextures()
// restore all textures via 'Restore' method call
BOOL CTextureManager::RestoreTextures()
{
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTextureManager::ReleaseTextures()
{
    CManagerIterator mni;
    LPGAMEBASE lpData = mni.GetFirst(this);
    //
    while (lpData)
    {
        ((LPTEXTURE)lpData)->Release();
        lpData = mni.GetNext();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// LPTEXTURE CTextureManager::FindTexture( ETypeIDs tidTextureTypeID, LPCSTR pszName )
// search texture by typeID and name
LPTEXTURE CTextureManager::FindTexture(ETypeIDs tidTextureTypeID, LPCSTR pszName)
{
    CManagerIterator mni;
    LPGAMEBASE lpData = mni.GetFirst(this);
    // first, try to find in existed textures
    while (lpData)
    {
        if ((lpData->GetTypeID() == tidTextureTypeID) && (strcmp(lpData->GetName(), pszName) == 0))
            return (LPTEXTURE)lpData;
        lpData = mni.GetNext();
    }
    // then, in LRU map
    return GetFromLRU(tidTextureTypeID, pszName);
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTextureManager::RemoveLRUTextures( DWORD dwCurrentSegmentCount )
// removes LPU textures from video memory
void CTextureManager::RemoveLRUTextures(DWORD dwCurrentSegmentCount)
{
    POSITION pos = lru.GetHeadPosition();
    POSITION lastpos = pos;
    LPTEXTURE lpTexture = NULL;
    //
    while (pos)
    {
        lpTexture = lru.GetNext(pos);
        if (lpTexture->GetLRUSegment() - dwCurrentSegmentCount > 15)
        {
            delete lpTexture;
            lru.RemoveAt(lastpos);
        }
        lastpos = pos;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// void CTextureManager::RemoveAllLRUTextures()
// remove all textures from LRU, regardless to their segment count
void CTextureManager::RemoveAllLRUTextures()
{
    POSITION pos = lru.GetHeadPosition();
    while (pos)
    {
        CTexture *pTexture = lru.GetNext(pos);
        delete pTexture; // physically destroy data
    }
    nTotalLRUSize = 0;
    lru.RemoveAll();
    /*
            if ( pVis )
                    pRend->EvictManagedTextures();
    */
}
/////////////////////////////////////////////////////////////////////////////////////
// LPTEXTURE CTextureManager::FindInLRU( ETypeIDs tidTypeID )
// find texture with such type in LRU map
// NOTE: don't remove it from LRU
LPTEXTURE CTextureManager::FindInLRU(ETypeIDs tidTypeID)
{
    POSITION pos = lru.GetHeadPosition();
    LPTEXTURE lpTexture = NULL;

    while (pos)
    {
        lpTexture = lru.GetNext(pos);
        if (lpTexture->GetTypeID() == tidTypeID)
            return lpTexture;
    }

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
// LPTEXTURE CTextureManager::FindInLRU( ETypeIDs tidTypeID, LPCSTR pszName )
// find texture with such type and name in LRU map
// NOTE: don't remove it from LRU
LPTEXTURE CTextureManager::FindInLRU(ETypeIDs tidTypeID, LPCSTR pszName)
{
    POSITION pos = lru.GetHeadPosition();
    LPTEXTURE lpTexture = NULL;

    while (pos)
    {
        lpTexture = lru.GetNext(pos);
        if ((lpTexture->GetTypeID() == tidTypeID) && (strcmp(lpTexture->GetName(), pszName) == 0))
            return lpTexture;
    }

    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
int CTextureManager::PutLayers(DWORD *pResultLayer, const CStringArray &szTextures,
                               DWORD dwTextureDimention)
{
    CGraphicsProcessor *pGraphicsProcessor = theRenderer.GetGraphicsProcessor();
    int i, nCount = 0;
    //
    for (i = 0; i < szTextures.GetSize(); ++i)
    {
        SMMPFileHeader *pMMPHeader = (SMMPFileHeader *)GetFile(szTextures[i]);
        if (pMMPHeader)
        {
            BYTE *pBaseData = (BYTE *)pMMPHeader;
            if ((pMMPHeader->dwDimentionX != dwTextureDimention) ||
                (pMMPHeader->dwDimentionY != dwTextureDimention))
            {
                GameJournal::Event("ERROR", "character_layer_skipped",
                    "name=%s reason=dimensions actual=%ux%u expected=%ux%u",
                    (LPCSTR)szTextures[i], pMMPHeader->dwDimentionX, pMMPHeader->dwDimentionY,
                    dwTextureDimention, dwTextureDimention);
                ReportWarning("Skipping character layer %s with dimensions %ux%u; expected %ux%u",
                    (LPCSTR)szTextures[i], pMMPHeader->dwDimentionX, pMMPHeader->dwDimentionY,
                    dwTextureDimention, dwTextureDimention);
                CloseFile(pBaseData);
                continue;
            }
            if (pMMPHeader->dwFormat == MMP_FORMAT_PNT3)
            {
                pGraphicsProcessor->PutOneLayer(pResultLayer,
                                                (DWORD *)pMMPHeader->GetMipLevelData(0),
                                                dwTextureDimention * dwTextureDimention, true);
            }
            else
            {
                pGraphicsProcessor->PutOneLayer(pResultLayer,
                                                (DWORD *)pMMPHeader->GetMipLevelData(0),
                                                dwTextureDimention * dwTextureDimention, false);
            }
            nCount++;
            CloseFile(pBaseData);
        }
        else
            ReportWarning("Can't find texture \"%s\" for layer composition.\n", szTextures[i]);
    }
    //
    return nCount;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTextureManager::ComposeTexture(CString &szResultName, const STextureSetDescription &set,
                                     const CString &szDesiredName, const CString &szAlternateName,
                                     DWORD dwNumMipLevels, DWORD dwBGColor, DWORD dwResultFormat)
{
    // Match the installed addon's HD names and atlas geometry in both precache and live assembly.
    if ((set.characterAtlas == 1 || set.characterAtlas == 2) &&
        ModRuntime::Current() && ModRuntime::Current()->CombatSetting("JMVGraphics"))
    {
        STextureSetDescription hd;
        hd.data.Copy(set.data);
        hd.characterAtlas = set.characterAtlas + 2;
        hd.dwTextureDimention = 1024;
        for (int slot = 0; slot < 4; ++slot) hd.equipmentSlots[slot] = set.equipmentSlots[slot];
        for (int layer = 0; layer < set.szTextures.GetSize(); ++layer)
        {
            CString name = set.szTextures[layer];
            if (name.GetLength() < 10 || name.Right(4).CompareNoCase(".mmp") != 0)
            {
                GameJournal::Event("ERROR", "character_layer_skipped", "name=%s reason=invalid_name", (LPCSTR)name);
                ReportWarning("Skipping invalid character layer name: %s", (LPCSTR)name);
                hd.szTextures.Add("__EIRebuild_invalid_character_layer__.mmp");
                continue;
            }
            name = name.Left(name.GetLength() - 4);
            if (name.Find("skin_") < 0) name = CString("unhuma") + name.Mid(6);
            name += set.characterAtlas == 1 ? ".x4.mmp" : ".x2.mmp";
            hd.szTextures.Add(name);
        }
        return ComposeTexture(szResultName, hd, szDesiredName, szAlternateName,
                              dwNumMipLevels, dwBGColor, MMP_FORMAT_8888);
    }

    CString desiredName=szDesiredName,alternateName=szAlternateName;
    if((set.characterAtlas==3 || set.characterAtlas==4) && ShareSecondaryCharacterAtlas()) {
        const bool primary=set.characterAtlas==3;
        std::string data;
        if(set.data.GetSize())data.assign(reinterpret_cast<const char*>(set.data.GetData()),set.data.GetSize());
        std::vector<std::string> layers;
        for(int i=0;i<set.szTextures.GetSize();++i)layers.emplace_back((LPCSTR)set.szTextures[i]);
        std::array<int,4> slots;
        for(int i=0;i<4;++i)slots[i]=primary?-1:set.equipmentSlots[i];
        const auto recipe=ModSecondaryAtlas::Recipe(set.dwTextureDimention,dwNumMipLevels,
            dwBGColor,dwResultFormat,data,layers,slots);
        std::string canonical;
        if(SecondaryAtlasRecipes().Resolve(recipe,canonical,primary)) {
            desiredName=canonical.c_str();alternateName=desiredName;
            if(!ModTextureMemory::State().forceCanonicalCpu && ModSecondaryAtlas::ReuseLoaded<CTexture>(
                [&](){return FindTexture(primary?TTID_UNIT_MAIN:TTID_UNIT_WEAPON,canonical.c_str());},
                [&](CTexture* provider){
                    if(!provider->GetSurface())LoadTexture(provider);
                    return provider->GetSurface()!=nullptr;
                },
                [](CTexture* provider){provider->AddRef();},
                [](CTexture* provider){provider->Release();})) {
                szResultName=desiredName;return TRUE;
            }
        }
    }
    if(ComposeTexturesInMemory())return ComposeTextureMemory(szResultName,set,desiredName,dwNumMipLevels,dwResultFormat);
    CString szResultFileName(CString(RuntimePaths64::Textures().c_str()) + desiredName + ".mmp");
    SMMPFileHeader hdr;
    CBYTEArray data;
    CFile file;
    // first, check for such texture already exist
    szResultName = desiredName;
    // check primary name texture
    if (file.Open(szResultFileName, CFile::modeRead))
    {
        // read user data
        file.Read(&hdr, sizeof(hdr));
        data.SetSize(hdr.dwUserDataShift); // ARRAY BOUNDS CHECK
        if (hdr.dwUserDataShift != 0)
            file.Read(data.GetData(), hdr.dwUserDataShift);
        file.Close();
        // compare user data
        if ((hdr.dwDimentionX == set.dwTextureDimention) &&
            (hdr.dwDimentionY == set.dwTextureDimention) &&
            (hdr.dwFormat == dwResultFormat) &&
            (data.GetSize() == set.data.GetSize()) &&
            (memcmp(data.GetData(), set.data.GetData(), data.GetSize()) == 0))
            return TRUE;
    }
    else if (desiredName != alternateName)
    {
        // check alternate name texture
        szResultName = alternateName;
        szResultFileName = CString(RuntimePaths64::Textures().c_str()) + alternateName + ".mmp";
        if (file.Open(szResultFileName, CFile::modeRead))
        {
            // read user data
            file.Read(&hdr, sizeof(hdr));
            data.SetSize(hdr.dwUserDataShift); // ARRAY BOUNDS CHECK
            if (hdr.dwUserDataShift != 0)
                file.Read(data.GetData(), hdr.dwUserDataShift);
            file.Close();
            // compare user data
            if ((hdr.dwDimentionX == set.dwTextureDimention) &&
            (hdr.dwDimentionY == set.dwTextureDimention) &&
            (hdr.dwFormat == dwResultFormat) &&
            (data.GetSize() == set.data.GetSize()) &&
                (memcmp(data.GetData(), set.data.GetData(), data.GetSize()) == 0))
                return TRUE;
            szResultName = desiredName;
            szResultFileName = CString(RuntimePaths64::Textures().c_str()) + desiredName + ".mmp";
        }
    }
    //
    if (set.szTextures.GetSize() == 0)
    {
        DeleteFile(szResultFileName);
        return FALSE;
    }
    // compose texture
    DWORD *pResultLayer = ModCriticalLifecycle::DiskLayer(
        [&](){return mm_malloc<DWORD>(set.dwTextureDimention * set.dwTextureDimention);},
        [](){ModCriticalLifecycle::Clear(ModRuntime::Current());ModHitHistory::ClearRuntimeHitHistory("disk-layer-allocation-failed");});
    if(!pResultLayer)return FALSE;
    WORD *pResultMipLevel = nullptr;
    DWORD *pMipLevel = nullptr;
    struct DiskComposeBuffers
    {
        DWORD *&layer;
        WORD *&converted;
        DWORD *&mip;
        ~DiskComposeBuffers()
        {
            if (converted) mm_free(converted);
            if (layer) mm_free(layer);
            if (mip) mm_free(mip);
        }
    } ownedBuffers{pResultLayer, pResultMipLevel, pMipLevel};
    MemSetDWord(pResultLayer, dwBGColor, set.dwTextureDimention * set.dwTextureDimention);
    int nCount = 0;
    if (set.characterAtlas == 4)
    {
        // Exact source quadrant layout: helm/shield above right/left weapon.
        std::vector<DWORD> layer(512 * 512);
        for (int slot = 0; slot < 4; ++slot)
        {
            int index = set.equipmentSlots[slot];
            if (index < 0 || index >= set.szTextures.GetSize()) continue;
            auto hdrLayer = reinterpret_cast<SMMPFileHeader *>(GetFile(set.szTextures[index]));
            if (!hdrLayer) {
                GameJournal::Event("ERROR", "hd_equipment_layer_skipped",
                    "name=%s reason=missing expected=512x512 formats=PNT3,PAINT32,8888",
                    (LPCSTR)set.szTextures[index]);
                ReportWarning("Missing HD equipment layer %s", (LPCSTR)set.szTextures[index]);
                continue;
            }
            if (!ModHDEquipmentLayer::Supported(hdrLayer->dwDimentionX, hdrLayer->dwDimentionY,
                                                hdrLayer->dwFormat))
            {
                GameJournal::Event("ERROR", "hd_equipment_layer_skipped",
                    "name=%s reason=unsupported_layer dimensions=%ux%u format=0x%08X expected=512x512 formats=PNT3,PAINT32,8888",
                    (LPCSTR)set.szTextures[index], hdrLayer->dwDimentionX, hdrLayer->dwDimentionY,
                    hdrLayer->dwFormat);
                ReportWarning("Skipping incompatible HD equipment layer %s (%ux%u, format 0x%08X)",
                    (LPCSTR)set.szTextures[index], hdrLayer->dwDimentionX, hdrLayer->dwDimentionY,
                    hdrLayer->dwFormat);
                CloseFile(reinterpret_cast<BYTE *>(hdrLayer));
                continue;
            }
            std::fill(layer.begin(), layer.end(), 0);
            theRenderer.GetGraphicsProcessor()->PutOneLayer(layer.data(),
                reinterpret_cast<DWORD *>(hdrLayer->GetMipLevelData(0)), 512 * 512,
                ModHDEquipmentLayer::UsesRunLength(hdrLayer->dwFormat));
            CloseFile(reinterpret_cast<BYTE *>(hdrLayer));
            int x = (slot == 1 || slot == 3) ? 512 : 0;
            int y = slot < 2 ? 512 : 0;
            for (int row = 0; row < 512; ++row)
                memcpy(pResultLayer + (y + row) * 1024 + x, layer.data() + row * 512, 512 * sizeof(DWORD));
            ++nCount;
        }
    }
    else nCount = PutLayers(pResultLayer, set.szTextures, set.dwTextureDimention);
    // check for any textures exist on this layer
    if (!nCount)
    {
        return FALSE;
    }
    // generate mip levels and write result texture
    hdr.dwSignature = MMP_SIGNATURE;
    hdr.InitializeDefaultFormat(dwResultFormat);
    hdr.dwMipMapLevel = dwNumMipLevels;
    hdr.dwDimentionX = set.dwTextureDimention;
    hdr.dwDimentionY = set.dwTextureDimention;
    hdr.dwUserDataShift = set.data.GetSize();
    //
    ETextureFormats texFormat = ETextureFormats(-1);
    switch (dwResultFormat)
    {
    case MMP_FORMAT_5650:
        texFormat = TEX_FORMAT_565;
        break;
    case MMP_FORMAT_5551:
        texFormat = TEX_FORMAT_1555;
        break;
    case MMP_FORMAT_4444:
        texFormat = TEX_FORMAT_4444;
        break;
    }
    // Allocate both temporary mip buffers before creating/truncating the MMP.
    DWORD dwNewSizeX = hdr.dwDimentionX;
    DWORD dwNewSizeY = hdr.dwDimentionY;
    try
    {
        pResultMipLevel = mm_malloc<WORD>(dwNewSizeX * dwNewSizeY);
        pMipLevel = mm_malloc<DWORD>(dwNewSizeX * dwNewSizeY);
    }
    catch (const std::bad_alloc &)
    { GameJournal::AllocationFailureInCatch("TextureManager");
        return FALSE;
    }
    if (!pResultMipLevel || !pMipLevel) { GameJournal::AllocationFailure("compose_layer_null"); return FALSE; }
    if (file.Open(szResultFileName, CFile::modeWrite | CFile::modeCreate) == FALSE)
    {
        GameJournal::Event("ERROR", "character_atlas_write_skipped", "path=%s", (LPCSTR)szResultFileName);
        ReportWarning("Can't open file \"%s\" to write result texture.\n", szResultFileName);
        return FALSE;
    }
    struct PartialMmpFile
    {
        CFile &file;
        const CString &path;
        bool complete = false;
        ~PartialMmpFile()
        {
            if (!complete)
            {
                file.Abort(); // Non-throwing handle cleanup during unwinding.
                if (!DeleteFile(path))
                {
                    OutputDebugStringA("Failed to remove incomplete composed MMP: ");
                    OutputDebugStringA((LPCSTR)path);
                    OutputDebugStringA("\n");
                }
            }
        }
    } outputFile{file, szResultFileName};
    // write header and user data
    file.Write(&hdr, sizeof(hdr));
    if (hdr.dwUserDataShift)
        file.Write(set.data.GetData(), hdr.dwUserDataShift);
    //
    // 0th mip level
    CGraphicsProcessor *pGraphicsProcessor = theRenderer.GetGraphicsProcessor();
    if (dwResultFormat == MMP_FORMAT_8888)
        file.Write(pResultLayer, sizeof(DWORD) * dwNewSizeX * dwNewSizeY);
    else
    {
        pGraphicsProcessor->Convert32To16Bit(pResultMipLevel, pResultLayer, texFormat, dwNewSizeX * dwNewSizeY);
        file.Write(pResultMipLevel, sizeof(WORD) * dwNewSizeX * dwNewSizeY);
    }
    // generate 1st and 2nd mip levels
    for (int i = 1; i < dwNumMipLevels; ++i)
    {
        dwNewSizeX >>= 1;
        dwNewSizeY >>= 1;

        pGraphicsProcessor->GenerateMipmapLevel(pMipLevel, pResultLayer, dwNewSizeX);
        if (dwResultFormat == MMP_FORMAT_8888)
            file.Write(pMipLevel, sizeof(DWORD) * dwNewSizeX * dwNewSizeY);
        else
        {
            pGraphicsProcessor->Convert32To16Bit(pResultMipLevel, pMipLevel, texFormat, dwNewSizeX * dwNewSizeY);
            file.Write(pResultMipLevel, sizeof(WORD) * dwNewSizeX * dwNewSizeY);
        }

        DWORD *pTmp = pMipLevel;
        pMipLevel = pResultLayer;
        pResultLayer = pTmp;
    }
    //
    file.Close();
    outputFile.complete = true;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTextureManager::ComposeLayer(const STextureSetDescription &set, const CString &szFileName,
                                   DWORD dwBGColor)
{
    CString szResultFileName(CString(RuntimePaths64::Textures().c_str()) + szFileName + ".mmp");
    SMMPFileHeader hdr;
    CFile file;
    // first, check for such texture already exist
    if (file.Open(szResultFileName, CFile::modeRead))
    {
        CBYTEArray data;
        // read user data
        file.Read(&hdr, sizeof(hdr));
        data.SetSize(hdr.dwUserDataShift); // ARRAY BOUNDS CHECK
        if (hdr.dwUserDataShift != 0)
            file.Read(data.GetData(), hdr.dwUserDataShift);
        file.Close();
        // compare user data
        if ((data.GetSize() == set.data.GetSize()) &&
            (memcmp(data.GetData(), set.data.GetData(), data.GetSize()) == 0))
            return FALSE;
    }
    // compose texture
    DWORD *pResultLayer = mm_malloc<DWORD>(set.dwTextureDimention * set.dwTextureDimention);
    MemSetDWord(pResultLayer, dwBGColor, set.dwTextureDimention * set.dwTextureDimention);
    int nCount = PutLayers(pResultLayer, set.szTextures, set.dwTextureDimention);
    // check for any textures exist on this layer
    if (!nCount)
    {
        mm_free(pResultLayer);
        return FALSE;
    }
    // precalculate as layer w/o RLE encoding
    CGraphicsProcessor *pGraphicsProcessor = theRenderer.GetGraphicsProcessor();
    pGraphicsProcessor->PrecalculateAsLayer(pResultLayer,
                                            set.dwTextureDimention * set.dwTextureDimention);
    // compose header
    hdr.dwSignature = MMP_SIGNATURE;
    hdr.InitializeDefaultFormatPNT2();
    hdr.dwMipMapLevel = 1;
    hdr.dwDimentionX = set.dwTextureDimention;
    hdr.dwDimentionY = set.dwTextureDimention;
    // write result layer
    if (file.Open(szResultFileName, CFile::modeCreate | CFile::modeWrite) != FALSE)
    {
        hdr.dwUserDataShift = set.data.GetSize();
        file.Write(&hdr, sizeof(hdr));
        file.Write(set.data.GetData(), hdr.dwUserDataShift);
        file.Write(pResultLayer, set.dwTextureDimention * set.dwTextureDimention * sizeof(DWORD));
        file.Close();
        //
        mm_free(pResultLayer);
    }
    else
    {
        GameJournal::Event("ERROR", "character_layer_write_skipped", "path=%s", (LPCSTR)szResultFileName);
        ReportWarning("Can't open file \"%s\" to write result layer texture.\n", szResultFileName);
        mm_free(pResultLayer);
        return FALSE;
    }
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
ITexture *CTextureManager::GetTexture(LPCSTR pszName)
{
    CManagerIterator mni;
    LPGAMEBASE pData = mni.GetFirst(this);
    // first, try to find in existed textures
    while (pData)
    {
        if (strcmp(pData->GetName(), pszName) == 0)
            return static_cast<ITexture *>(pData);
        pData = mni.GetNext();
    }
    // then, find in LRU
    return GetFromLRU(pszName);
}
/////////////////////////////////////////////////////////////////////////////////////
ITexture *CTextureManager::GetFromLRU(LPCSTR pszName)
{
    POSITION pos = lru.GetHeadPosition();
    POSITION lastpos = pos;
    ITexture *pTexture = NULL;

    while (pos)
    {
        pTexture = lru.GetNext(pos);
        if (strcmp(pTexture->GetName(), pszName) == 0)
        {
            TRACE1("Get texture \"%s\" from LRU map.\n", pTexture->GetName());
            lru.RemoveAt(lastpos);     // remove from LRU list
            nTotalLRUSize -= pTexture->GetTotalSize();
            RegisterInstant(pTexture); // register in main hash table
            return pTexture;
        }
        lastpos = pos;
    }

    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
std::string CTextureManager::CheckLRUAccountingG3()
{
    // CPU containers and empty providers only: do not call texture-manager Create,
    // load resources, create surfaces or initialize a renderer/live world.
    CTextureManager manager;
    if (!manager.CBaseManager::Create()) throw std::runtime_error("LRU fixture containers failed");
    CTexture* first = new CTexture();
    CTexture* last = new CTexture();
    first->SetName("lru-cpu-a"); first->SetTypeID(TTID_UNIT_MAIN); first->SetTotalSize(4096);
    last->SetName("lru-cpu-b"); last->SetTypeID(TTID_UNIT_MAIN); last->SetTotalSize(12288);
    manager.lru.AddTail(first); manager.lru.AddTail(last);
    manager.nTotalLRUSize = 16384;
    ITexture* hit = manager.GetAndLoadTexture(TTID_UNIT_MAIN, "lru-cpu-a");
    nlohmann::json cases = nlohmann::json::array();
    cases.push_back({{"id","name-hit"},{"same",hit==first},
        {"remainingBytes",manager.nTotalLRUSize},{"lruCount",manager.GetNumLRUElements()},
        {"registered",first->GetManager()==&manager}});
    POSITION head = manager.lru.GetHeadPosition();
    ITexture* miss = manager.GetTexture("lru-cpu-absent");
    cases.push_back({{"id","miss"},{"found",miss!=nullptr},
        {"remainingBytes",manager.nTotalLRUSize},{"lruCount",manager.GetNumLRUElements()},
        {"orderUnchanged",head==manager.lru.GetHeadPosition()}});
    ITexture* registered = manager.GetTexture("lru-cpu-a");
    ITexture* finalHit = manager.GetTexture("lru-cpu-b");
    const int afterLast = manager.nTotalLRUSize;
    ITexture* repeat = manager.GetTexture("lru-cpu-a");
    cases.push_back({{"id","registered-and-last"},{"sameRegistered",registered==first},
        {"lastSame",finalHit==last},{"remainingBytes",afterLast},
        {"lruCount",manager.GetNumLRUElements()},{"repeatSame",repeat==first},
        {"repeatBytes",manager.nTotalLRUSize}});
    // Both fixture pointers have transferred to the main hash; detach explicitly.
    manager.RemoveInstantAt(first->GetID()); first->SetManager(nullptr); delete first;
    manager.RemoveInstantAt(last->GetID()); last->SetManager(nullptr); delete last;
    return nlohmann::json({{"success",true},{"actualCpuManager",true},{"cpuTextures",2},
        {"cases",cases},{"resourceOpened",false},{"rendererCreated",false},
        {"luaExecuted",false},{"unitCreated",false},{"worldCreated",false},
        {"hostCreated",false},{"deviceCreated",false},{"audioStarted",false},
        {"saveRead",false},{"uiExecuted",false}}).dump();
}
/////////////////////////////////////////////////////////////////////////////////////
ITexture *CTextureManager::GetAndLoadTexture(ETypeIDs tid, LPCSTR pszName)
{
    ITexture *pTexture = GetTexture(pszName);
    if (pTexture != 0)
        return pTexture;
    // create new texture and load data for it
    pTexture = CreateTexture(tid, pszName);
    LoadTexture(pTexture);
    return pTexture;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CTextureManager::ReloadAllTextures()
{
    bool bRetVal = true;
    // restore manin texture
    CManagerIterator it;
    CTexture *pTexture = (CTexture *)it.GetFirst(this);
    while (pTexture)
    {
        pTexture->ResetFlag(TEX_LOADED);
        bRetVal = LoadTexture(pTexture) && bRetVal;
        pTexture = (CTexture *)it.GetNext();
    }
    // restore textures in the LRU map
    POSITION pos = lru.GetHeadPosition();
    while (pos)
    {
        pTexture = lru.GetNext(pos);
        pTexture->ResetFlag(TEX_LOADED);
        bRetVal = LoadTexture(pTexture) && bRetVal;
    }
    //
    return bRetVal;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTextureManager::ShrinkLRU()
{
    POSITION pos = lru.GetHeadPosition();
    POSITION lastpos = pos;
    while ((nTotalLRUSize > nAllowedLRUSize) && (pos != 0))
    {
        CTexture *pTexture = lru.GetNext(pos);
        lru.RemoveAt(lastpos); // remove from LRU list
        nTotalLRUSize -= pTexture->GetTotalSize();
        delete pTexture; // physically destroy data
        lastpos = pos;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
GameJournal::TextureMemory CTextureManager::ReadMemoryCounters() const noexcept
{
    GameJournal::TextureMemory result;result.available=true;
    // Both registered providers and LRU providers still belong to the manager.
    auto count=[&](const CTexture* texture){
        ++result.count;if(texture->CheckFlag(TEX_LOADED))++result.loaded;
        const int bytes=texture->GetTotalSize();if(bytes>0)result.bytes+=bytes;
    };
    POSITION pos=list.GetHeadPosition();
    while(pos)count(static_cast<const CTexture*>(list.GetNext(pos)));
    pos=lru.GetHeadPosition();while(pos)count(lru.GetNext(pos));
    result.lruCount=lru.GetCount();result.lruBytes=nTotalLRUSize>0?nTotalLRUSize:0;
    result.lruAllowed=pRend && nAllowedLRUSize>0?nAllowedLRUSize:0;
    if(const auto* state=ModTextureMemory::PeekState()){
        result.cacheCount=state->cache.Size();result.cacheBytes=state->cache.Bytes();
    }
    return result;
}

void CTextureManager::UnloadAllTextures()
{
    return;
    CManagerIterator mi;
    CTexture *pTexture = static_cast<CTexture *>(mi.GetFirst(this));
    while (pTexture)
    {
        if (pTexture->CheckFlag(TEX_CANT_UNLOAD) == 0)
            pTexture->Unload();
        pTexture = static_cast<CTexture *>(mi.GetNext());
    }
}
/////////////////////////////////////////////////////////////////////////////////////
