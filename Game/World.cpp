#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "World.h"

#include "WinRegistry.h"

#include "Net.h"

#ifdef __GAME__
#include "NetChat.h"
#else
class CChatNetDriver : public CObjectBase
{
    OBJECT_BASIC_METHODS(CChatNetDriver);
};
#endif

#include "PlayerManager.h"
#include "FigureManager.h"
#include "MapObjectManager.h"
#include "EffectManager.h"
#include "SpellManager.h"
#include "SpellConstructor.h"
#include "ModConstructorLifetime.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "ItemManager.h"
#include "unit.h"
#include "player.h"
#include "ItemConstructor.h"

#include "RandomGen.h"
/////////////////////////////////////////////////////////////////////////////////////
// common settings for every world in program
CString CWorld::szRegistryKey;  // registry key name ( ZB "Nival Interactive\\EvilIslands" )
CString CWorld::szCurrentDir;   // current directory path
CString CWorld::szWorkPath;     // work folder path
CString CWorld::szResourcePath; // resource folder path
CString CWorld::szCDPath;       // CD-ROM path
/////////////////////////////////////////////////////////////////////////////////////
// store list of gamebase derivatives
void StoreList(const CGameBaseList &objs, CMemorySave &packet)
{
    POSITION pos;
    pos = objs.GetHeadPosition();
    while (pos)
    {
        CGameBase *pObj = objs.GetNext(pos);
        packet.Write((char)1);
        packet.Write(pObj->GetID());
        packet.Write((int)pObj->GetTypeID());
        packet.Write(pObj->GetSubtypeID());
        pObj->Store(packet);
    }
    packet.Write((char)0);
}
/////////////////////////////////////////////////////////////////////////////////////
// CWorld
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorld::Create()
{
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
CItem *CWorld::DuplicateItem(CItem *pItem)
{
    CItem *lpItem = GetItemManager()->CreateItem(pItem->GetTypeID(), pItem->GetShape(),
                                                 pItem->GetMaterial(), -1);
    pItem->Duplicate(lpItem, GetSpellManager());
    return lpItem;
}
/////////////////////////////////////////////////////////////////////////////////////
CString CWorld::GetMobPath()
{
    return CString(GetWorkPath()) + "maps\\";
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorld::InitCurrentDir()
{
    // get current directory
    char buffer[1024];
    // get current directory
    GetCurrentDirectory(1024, buffer);
    SetCurrentDir(CString(buffer) + '\\');
}
/////////////////////////////////////////////////////////////////////////////////////
// CWorldBase
/////////////////////////////////////////////////////////////////////////////////////
CWorldBase::CWorldBase()
{
    // data manager pointers initialization
    pPlayerManager = NULL;
    pFigureManager = NULL;
    pMapObjectManager = NULL;
    pSpecificManager = NULL;
    pEffectManager = NULL;
    pSpellManager = NULL;
    pSpellConstructor = NULL;
    pItemManager = NULL;
    pItemConstructor = NULL;
    //
    fpTimeModifier = 0.0f;
    fpTimeScale = 0.0f;
}
/////////////////////////////////////////////////////////////////////////////////////
CWorldBase::~CWorldBase()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
// create all in-game internal data structures
BOOL CWorldBase::Create()
{
    CWorld::Create();
    bCombatPause = false;
    // assign default directories
    SetWorkPath(theSettings.szWorkPath);
    SetResourcePath(theSettings.szResourcePath);
    SetCDPath(theSettings.szCDPath);
    //
    nSegmentTimeCounter = 0;
    nUpdateCounter = 0;
    // wind
    ptWindDirection = CPlot3D(1, 0, 0);
    fpWindForce = 0;
    // create data managers
    // player manager
    pPlayerManager = dbgnew CPlayerManager();
    pPlayerManager->AddRef();
    pPlayerManager->Create(this);
    // unit manager
    pMapObjectManager = dbgnew CMapObjectManager();
    pMapObjectManager->AddRef();
    pMapObjectManager->Create(this);
    // figure manager
    pFigureManager = dbgnew CFigureManager();
    pFigureManager->AddRef();
    pFigureManager->Create(this);
    // effect manager
    pEffectManager = dbgnew CEffectManager;
    pEffectManager->AddRef();
    pEffectManager->Create(this);
    // spell constructor
    pSpellConstructor = dbgnew CSpellConstructor;
    pSpellConstructor->AddRef();
    pSpellConstructor->Create();
    // spell manager
    pSpellManager = dbgnew CSpellManager;
    pSpellManager->AddRef();
    pSpellManager->Create(this);
    // item constructor
    pItemConstructor = dbgnew CItemConstructor;
    pItemConstructor->AddRef();
    pItemConstructor->Create();
    // item manager
    pItemManager = dbgnew CItemManager;
    pItemManager->AddRef();
    pItemManager->Create(this);
    // Net driver
    pNetDriver = dbgnew CLocalNetDriver;
    // Init random generator
    random.Init();
    //
    waterLevels.Clear();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldBase::StartNetDrivers()
{
    pChatDriver = dbgnew CChatNetDriver;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldBase::StopNetDrivers()
{
    pChatDriver = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// destroy all internal data structures
void CWorldBase::Destroy()
{
    // free refs of spells
    if (GetSpellManager())
    {
        {
            CManagerIterator spellIterator;
            CManagerIterator *spellIter=&spellIterator;
            CGameBase *pThing = spellIter->GetFirst(GetSpellManager());
            while (pThing)
            {
                pThing->FreeReferences();
                pThing = spellIter->GetNext();
            }

        }
    }
    // delete player manager
    if (pPlayerManager)
    {
        pPlayerManager->DecRefCount();
        delete pPlayerManager;
        pPlayerManager = NULL;
    }
    // network connections deinitialization
    pNetDriver = 0;
    pChatDriver = 0;

    // data managers destruction
    // delete map object manager
    if (pMapObjectManager)
    {
        pMapObjectManager->DecRefCount();
        delete pMapObjectManager;
        pMapObjectManager = NULL;
    }
    // delete figure manager
    if (pFigureManager)
    {
        pFigureManager->DecRefCount();
        delete pFigureManager;
        pFigureManager = NULL;
    }
    pGeometryManager = 0;
    // delete specific manager
    if (pSpecificManager)
    {
        pSpecificManager->DecRefCount();
        delete pSpecificManager;
        pSpecificManager = NULL;
    }
    // delete item constructor
    if (pItemConstructor)
    {
        pItemConstructor->DecRefCount();
        delete pItemConstructor;
        pItemConstructor = NULL;
    }
    // delete item manager
    if (pItemManager)
    {
        pItemManager->DecRefCount();
        delete pItemManager;
        pItemManager = NULL;
    }
    // delete spell constructor
    if (pSpellConstructor)
    {
        if (ModConstructorLifetime::PolicyBound())
            ModConstructorLifetime::ReleaseOwned(pSpellConstructor,
                [](const void* object, const void* vtable) {
                    ModConstructorLifetime::NoteInvalid(ModConstructorLifetime::World, object, vtable);
                });
        else
        {
            pSpellConstructor->DecRefCount();
            delete pSpellConstructor;
        }
        pSpellConstructor = NULL;
    }
    // delete spell manager
    if (pSpellManager)
    {
        pSpellManager->DecRefCount();
        delete pSpellManager;
        pSpellManager = NULL;
    }
    // delete effect manager
    if (pEffectManager)
    {
        pEffectManager->DecRefCount();
        delete pEffectManager;
        pEffectManager = NULL;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldBase::Segment()
{
    ASSERT(0);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
CMapObject *CWorldBase::GetMapObject(int nID)
{
    return (CMapObject *)((CMapObjectManager *)GetMapObjectManager())->Find(nID);
}
/////////////////////////////////////////////////////////////////////////////////////
CUnit *CWorldBase::GetUnit(int nID)
{
    CUnit *pUnit = (CUnit *)GetMapObject(nID);
    if (!pUnit)
        return NULL;
    if (pUnit->WhoAmI() != HPC_UNIT)
        return NULL;
    return pUnit;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldBase::SetWorldTime(float fpCurrentTime)
{
    fpTimeModifier = fpCurrentTime - GetWorldTime();
    // theSettings.nDefaultTimeScale = Float2Int( fpScale );
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldBase::SetWorldTimeScale(float fpScale)
{
    float fpTime = GetWorldTime();
    if (fpScale > 0)
        fpTimeScale = 1.0f / fpScale; // 1 / fpScale;
    else
    {
        if (std::abs(fpScale) < std::numeric_limits<float>().epsilon())
            fpTimeScale = 0;
        else
            fpTimeScale = 1.0f / float(theSettings.nDefaultTimeScale);
    }
    fpTimeScale /= 15.0f;
    SetWorldTime(fpTime);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldBase::GetTimeParams(float &fpCurrentTime, float &fpScale)
{
    fpCurrentTime = GetWorldTime();
    fpScale = 1 / (fpTimeScale * 15);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CWorldBase::IsNightTime()
{
    float fpTime = GetWorldTime();
    return (fpTime >= TIME_NIGHT_START) || (fpTime < TIME_DAY_START);
}
/////////////////////////////////////////////////////////////////////////////////////
float CWorldBase::GetNightPowerSlow()
{
    float fpTime = GetWorldTime(); // get current world time (in hours)
    const CModPackage *mod = ModRuntime::Current();
    if (mod && mod->SpellAddon().Feature32() == 1)
    {
        if (fpTime < 4.0f)
            return 1.0f;
        if (fpTime < 8.0f)
            return (8.0f - fpTime) * 0.25f;
        if (fpTime <= 18.0f)
            return 0.0f;
        if (fpTime < 22.0f)
            return (fpTime - 18.0f) * 0.25f;
        return 1.0f;
    }
    float fpRes = 0;
    if (fpTime > 20)
        fpRes = (fpTime - 20) / 4;
    else if (fpTime < 8)
    {
        if (fpRes > 4)
            fpRes = (8 - fpTime) / 4;
        else
            fpRes = 1;
    }
    return fpRes;
}
/////////////////////////////////////////////////////////////////////////////////////
void CWorldBase::SetWind(const CPlot3D &windDirection, float windForce)
{
    ptWindDirection = windDirection;
    fpWindForce = windForce;
}
/////////////////////////////////////////////////////////////////////////////////////
