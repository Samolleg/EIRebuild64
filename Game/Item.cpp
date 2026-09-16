#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
/*==========================================================================================================

        Common item class implementation

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/
/////////////////////////////////////////////////////////////////////////////////////
#include "World.h"
#include "Item.h"
#include "Unit.h"
#include "NetProtocol.h"
#include "ItemManager.h"
#include "SpellManager.h"
#include "RandomGen.h"
/////////////////////////////////////////////////////////////////////////////////////
void AddItemToLists(CGameBaseList &items, CGameBaseList &spells, CItem *pItem)
{
    if (!pItem)
        return;
    items.AddTail(pItem);
    for (int k = 0; k < pItem->GetSpellsCount(); k++)
    {
        CSpell *pSpell = pItem->GetSpell(k);
        if (pSpell)
            spells.AddTail(pSpell);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// CItem
// ******************************************************************************** //
CItem::CItem()
{
    fpHP = fpMaxHP = -1;
    fpMP = fpMaxMP = -1;
    fpRegenMP = -1;
    fpWeight = -1;
    fpPrice = -1;
    fpSize = -1; // just size
    nCount = -1;

    Create();
}
/////////////////////////////////////////////////////////////////////////////////////
CItem::~CItem()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CItem::Create()
{
    nCount = 1;
    pSpell = 0;
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CItem::Destroy()
{
    // release spell
    pSpell = 0;
    pUserData = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CItem::Compare(CItem *lpItem)
{
    if (lpItem == this)
        return TRUE;
    if (GetTypeID() != lpItem->GetTypeID())
        return FALSE;
    if (GetSubtypeID() != lpItem->GetSubtypeID())
        return FALSE;
    // check HP/MP
    if ((fpHP != lpItem->fpHP) || (fpMaxHP != lpItem->fpMaxHP))
        return FALSE;
    if ((fpMP != lpItem->fpMP) || (fpMaxMP != lpItem->fpMaxMP))
        return FALSE;
    // check weight
    if (fpWeight != lpItem->fpWeight)
        return FALSE;
    // check price
    if (fpPrice != lpItem->fpPrice)
        return FALSE;
    // check size
    if (fpSize != lpItem->fpSize)
        return FALSE;
    // check spell slots
    if (dwSpellSlots != lpItem->dwSpellSlots)
        return FALSE;
    // check spells
    if (pSpell && lpItem->pSpell)
    {
        if ((pSpell->GetSubtypeID() != lpItem->pSpell->GetSubtypeID()) ||
            (memcmp(pSpell->GetRunes(), lpItem->pSpell->GetRunes(), 8) != 0))
            return FALSE;
    }
    else if (pSpell != lpItem->pSpell) // one of this spell pointers are non-zero
        return FALSE;
    // items are identical !
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// CItem* CItem::Duplicate() const
// create item with same type and write this item data to new one
CItem *CItem::Duplicate(CSpellManager *pSpellManager) const
{
    CItem *lpItem = (CItem *)GetManager()->CreateData(GetTypeID());
    if (Duplicate(lpItem, pSpellManager))
        return lpItem;
    else
    {
        lpItem->AddRef();
        lpItem->Release();
        return NULL;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CItem::Duplicate( CItem* lpItem ) const
// write this item data to lpItem
BOOL CItem::Duplicate(CItem *lpItem, CSpellManager *pSpellManager) const
{
    if (lpItem->GetTypeID() != GetTypeID())
        return FALSE;
    // destroy any old data
    lpItem->Destroy();
    // copy new data
    lpItem->SetSubtypeID(GetSubtypeID());
    lpItem->fpHP = fpHP;
    lpItem->fpMaxHP = fpMaxHP;
    lpItem->fpMP = fpMP;
    lpItem->fpMaxMP = fpMaxMP;
    lpItem->fpRegenMP = fpRegenMP;
    lpItem->fpWeight = fpWeight;
    lpItem->fpPrice = fpPrice;
    lpItem->fpSize = fpSize;
    lpItem->dwSpellSlots = dwSpellSlots;
    // copy spell
    if (pSpell)
        lpItem->pSpell = pSpell->Duplicate(pSpellManager);
    else
        lpItem->pSpell = 0;
    // set number of stocked items to 1
    lpItem->nCount = 1;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
int CItem::Segment()
{
    if (fpMP == fpMaxMP)
        return 0;
    if (fpRegenMP <= 0)
    {
        fpMP = fpMaxMP;
        return 1;
    }
    fpMP += fpMaxMP * fpRegenMP;
    if (fpMP >= fpMaxMP)
    {
        fpMP = fpMaxMP;
        return 1;
    }
    return 1;
}
/////////////////////////////////////////////////////////////////////////////////////
LPCSTR CItem::GetMaterialCode() const
{
    const SIDBMaterial *lpIDBMaterial = ((CItemManager *)lpManager)->GetMaterial(GetMaterial());
    if (lpIDBMaterial)
        return lpIDBMaterial->szCodeName;
    return "uq";
}
/////////////////////////////////////////////////////////////////////////////////////
const SIDBItemGeneralShape *CItem::GetShapeEntry() const
{
    switch (GetTypeID())
    {
    case ITID_ITEM_WEAPON:
        return theItemDatabase.weapons[GetShape()];
    case ITID_ITEM_ARMOR:
        return theItemDatabase.armors[GetShape()];
    case ITID_ITEM_QUICK:
        return theItemDatabase.quickItems[GetShape()];
    case ITID_ITEM_QUEST:
        return theItemDatabase.questItems[GetShape()];
    case ITID_ITEM_LOOT:
        return theItemDatabase.lootItems[GetShape()];
    default:
        return 0;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CItem::Load(CSave &file)
{
    file.Read(dwSubtypeID);

    file.Read(fpHP);
    file.Read(fpMaxHP);
    file.Read(fpMP);
    file.Read(fpMaxMP);
    file.Read(fpRegenMP);

    file.Read(fpWeight);
    file.Read(fpPrice);
    file.Read(fpSize);
    pSpell = LoadGBSmartPtr<CSpell>(file, (CBaseManager *)GetWorld()->GetSpellManager());
    file.Read(nCount);
    file.Read(dwSpellSlots);
}
/////////////////////////////////////////////////////////////////////////////////////
void CItem::Store(CSave &file)
{
    file.Write(dwSubtypeID);

    file.Write(fpHP);
    file.Write(fpMaxHP);
    file.Write(fpMP);
    file.Write(fpMaxMP);
    file.Write(fpRegenMP);

    file.Write(fpWeight);
    file.Write(fpPrice);
    file.Write(fpSize);
    StoreGBPtr(file, pSpell);
    file.Write(nCount);
    file.Write(dwSpellSlots);
}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CItem::UpdateTo( CBufferManager* pBufMng, BYTE cUpdateMask, BYTE cDeskType )
//{
//	SCP_UPDATE_ITEM pkt;

//	pkt.packetID = PID_UPDATE_ITEM;
//	pkt.itemID = GetID();

//	pkt.cUpdateMask = cUpdateMask;
//	pkt.cDeskType = cDeskType;

//	LPVOID pUpdateInfo = &pkt.updateInfo;
//	if ( cUpdateMask & CItem::VLD_MASK_UNIT )
//		SetInt( pUpdateInfo, pUnit ? pUnit->GetID() : -1 );
//	if ( cUpdateMask & CItem::VLD_MASK_COST )
//		SetInt( pUpdateInfo, nCost );
//	if ( cUpdateMask & CItem::VLD_MASK_NUMBER )
//		SetInt( pUpdateInfo, nNumber );
//	if ( cUpdateMask & CItem::VLD_MASK_NAME )
//		SetBASCII( pUpdateInfo, szName );

//	if ( !pBufMng->WriteData( &pkt, sizeof( pkt )-sizeof( pkt.updateInfo )+GetWrittenSize(
// pUpdateInfo, &pkt.updateInfo ) ) ) 		return FALSE; 	return TRUE;
//}
/////////////////////////////////////////////////////////////////////////////////////
// BOOL CItem::UpdateFrom( CBufferManager* pBufMng )
//{
//	SCP_UPDATE_ITEM pkt;
//	if ( !pBufMng->ReadData( &pkt, sizeof( pkt )-sizeof( pkt.updateInfo ) ) )
//		return FALSE;
//	assert( pkt.packetID == PID_UPDATE_ITEM );
//	assert( pkt.itemID == GetID() );

//	cValidMask |= pkt.cUpdateMask;

//	BOOL bRv = TRUE;
//	if ( pkt.cUpdateMask & CItem::VLD_MASK_UNIT )
//	{
//		int nUnitID;
//		bRv = bRv && GetInt( &nUnitID, pBufMng );
//		SetUnit( (CUnit*)((CWorld*)GetWorld())->GetMapObjectManager()->Find( nUnitID ) );
//	}
//	if ( pkt.cUpdateMask & CItem::VLD_MASK_COST )
//		bRv = bRv && GetInt( &nCost, pBufMng );
//	if ( pkt.cUpdateMask & CItem::VLD_MASK_NUMBER )
//		bRv = bRv && GetInt( &nNumber, pBufMng );
//	if ( pkt.cUpdateMask & CItem::VLD_MASK_NAME )
//		bRv = bRv && GetBASCII( szName, sizeof( szName ), pBufMng, 1 );
//	return bRv;
//}
/////////////////////////////////////////////////////////////////////////////////////
void CItem::Setup(const SIDBItemGeneralShape *lpShape)
{
    fpHP = fpMaxHP = lpShape->fpDurability;
    fpMP = fpMaxMP = lpShape->fpManaCapacity;
    fpRegenMP = 0.001f; // full restoring ~ 1 minute
    fpWeight = lpShape->fpWeight;
    fpPrice = lpShape->fpPrice;
    fpSize = lpShape->fpSize;
    dwSpellSlots = lpShape->dwSpellSlots;
}
/////////////////////////////////////////////////////////////////////////////////////
void CItem::AddSpell(CSpell *pNewSpell)
{
    if (pSpell)
        fpPrice -= pSpell->GetPrice();
    pSpell = pNewSpell;
    if (pSpell)
        fpPrice += pSpell->GetPrice();
}
/////////////////////////////////////////////////////////////////////////////////////
void CItem::AddFreeSpell(CSpell *pNewSpell)
{
    pSpell = pNewSpell;
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// CItemWeapon
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
CItemWeapon::CItemWeapon()
{
    dwProficiencyClass = 0;
    fpDamageMin = -1;
    fpDamageRnd = -1;
    fpRange = -1;

#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemWeapon::Setup(const SIDBItemGeneralShape *lpShape)
{
    CItem::Setup(lpShape);

    fpRange = ((const SIDBWeaponShape *)lpShape)->fpRange;
    fpDamageMin = ((const SIDBWeaponShape *)lpShape)->fpDamageMin;
    fpDamageRnd = ((const SIDBWeaponShape *)lpShape)->fpDamageRandom;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CItemWeapon::Compare(CItem *lpItem)
{
    if (!CItem::Compare(lpItem))
        return FALSE;
    //
    CItemWeapon *pWeapon = static_cast<CItemWeapon *>(lpItem);
    if (pWeapon->dwProficiencyClass != dwProficiencyClass)
        return FALSE;
    if (pWeapon->fpDamageMin != fpDamageMin)
        return FALSE;
    if (pWeapon->fpDamageRnd != fpDamageRnd)
        return FALSE;
    if (pWeapon->fpRange != fpRange)
        return FALSE;
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CItemWeapon::Duplicate(CItem *lpItem, CSpellManager *pSpellManager) const
{
    // duplicate common item's params
    if (CItem::Duplicate(lpItem, pSpellManager) == FALSE)
        return FALSE;
    CItemWeapon *lpWeapon = (CItemWeapon *)lpItem;
    lpWeapon->dwProficiencyClass = dwProficiencyClass;
    lpWeapon->fpDamageMin = fpDamageMin;
    lpWeapon->fpDamageRnd = fpDamageRnd;
    lpWeapon->fpRange = fpRange;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemWeapon::GetDamageDistribution(float *fpAttacks)
{
    const SIDBWeaponShape *lpShape = ((CItemManager *)GetManager())->GetWeaponShape(GetShape());
    if (lpShape)
        memcpy(fpAttacks, lpShape->fpDamageDistributions, sizeof(float) * RPG_NUM_ATTACK_TYPES);
    else
        MemSetDWord((DWORD *)fpAttacks, 0, RPG_NUM_ATTACK_TYPES);
}
/////////////////////////////////////////////////////////////////////////////////////
ERPGWeaponTypes CItemWeapon::GetWeaponType() const
{
    return (ERPGWeaponTypes)theItemDatabase.weapons[GetShape()]->dwTypeID;
}
/////////////////////////////////////////////////////////////////////////////////////
CString CItemWeapon::FormTextureNameMask() const
{
    CString szName;
    const SIDBMaterial *pMaterial = theItemDatabase.materials[GetMaterial()];
    const SIDBWeaponShape *pShape = theItemDatabase.weapons[GetShape()];
    if ((pShape == 0) || (pMaterial == 0))
        return szName;
    FormTextureNameMask(szName, pShape, pMaterial);
    return szName;
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemWeapon::FormTextureNameMask(CString &szName, DWORD dwShape, DWORD dwMaterial)
{
    const SIDBMaterial *pMaterial = theItemDatabase.materials[dwMaterial];
    const SIDBWeaponShape *pShape = theItemDatabase.weapons[dwShape];
    szName.Empty();
    if ((pShape != 0) && (pMaterial != 0))
        FormTextureNameMask(szName, pShape, pMaterial);
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemWeapon::FormTextureNameMask(CString &szName, const SIDBItemGeneralShape *pShape,
                                      const SIDBMaterial *pMaterial)
{
    char buff[4];
    // armor type code
    switch (pShape->dwTypeID)
    {
    case RPGWST_SWORD:
        strcpy(buff, "sw");
        break;
    case RPGWST_AXE:
        strcpy(buff, "ax");
        break;
    case RPGWST_DAGGER:
        strcpy(buff, "dg");
        break;
    case RPGWST_SPEAR:
        strcpy(buff, "sp");
        break;
    case RPGWST_HAMMER:
        strcpy(buff, "hm");
        break;
    case RPGWST_BOW:
        strcpy(buff, "bw");
        break;
    case RPGWST_CROSSBOW:
        strcpy(buff, "cb");
        break;
    }
    //
    szName.Format("%s_%.2d.%s.%d", buff, pShape->dwTextureTypeIndex, pMaterial->szCodeName,
                  pShape->dwTextureTypeIndex2);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CItemWeapon::GetCastPointName(DWORD dwShape, CString &szName)
{
    // baseaxe00-05
    // baseclub01-08
    // basedagger01-04
    // basepike00-05
    // baser_hand3
    // basesword01-04
    szName.Empty();
    if (dwShape == -1)
        return false;
    const SIDBItemGeneralShape *pShape = theItemDatabase.weapons[dwShape];
    if (pShape == 0)
        return false;
    char buff1[4], buff[32];
    sprintf(buff1, "%.2d", pShape->dwTextureTypeIndex);
    switch (pShape->dwTypeID)
    {
    case RPGWST_SWORD:
        strcpy(buff, "basesword");
        strcat(buff, buff1);
        break;
    case RPGWST_AXE:
        strcpy(buff, "baseaxe");
        strcat(buff, buff1);
        break;
    case RPGWST_DAGGER:
        strcpy(buff, "basedagger");
        strcat(buff, buff1);
        break;
    case RPGWST_SPEAR:
        strcpy(buff, "basepike");
        strcat(buff, buff1);
        break;
    case RPGWST_HAMMER:
        strcpy(buff, "baseclub");
        strcat(buff, buff1);
        break;
    case RPGWST_BOW:
        strcpy(buff, "basearrow00");
        break;
    case RPGWST_CROSSBOW:
        strcpy(buff, "basearrow01");
        break;
    default:
        strcpy(buff, "baser_hand3");
    }
    //
    szName = buff;

    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CItemWeapon::GetCastPointName(CString &szName)
{
    return GetCastPointName(GetShape(), szName);
}
/////////////////////////////////////////////////////////////////////////////////////
CString CItemWeapon::GetFigureName() const
{
    const SIDBWeaponShape *lpShape = ((CItemManager *)GetManager())->GetWeaponShape(GetShape());
    if (lpShape && lpShape->szFigures.GetSize())
        return lpShape->szFigures[0];
    else
        return "";
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemWeapon::GetWeaponModelNames(DWORD dwShape, CStringArray &szNames)
{
    char szBuffer[64];
    char szTTI[8];
    szNames.RemoveAll();

    const SIDBWeaponShape *pShape = theItemDatabase.weapons[dwShape];
    if (pShape == 0)
        return;
    sprintf(szTTI, "%.2d", pShape->dwTextureTypeIndex);
    switch (pShape->dwTypeID)
    {
    case RPGWST_SWORD:
        strcpy(szBuffer, "rh3.sword");
        strcat(szBuffer, szTTI);
        szNames.Add(szBuffer);
        break;
    case RPGWST_AXE:
        strcpy(szBuffer, "rh3.axe");
        strcat(szBuffer, szTTI);
        szNames.Add(szBuffer);
        break;
    case RPGWST_DAGGER:
        strcpy(szBuffer, "rh3.dagger");
        strcat(szBuffer, szTTI);
        szNames.Add(szBuffer);
        break;
    case RPGWST_SPEAR:
        strcpy(szBuffer, "rh3.pike");
        strcat(szBuffer, szTTI);
        szNames.Add(szBuffer);
        break;
    case RPGWST_HAMMER:
        strcpy(szBuffer, "rh3.club");
        strcat(szBuffer, szTTI);
        szNames.Add(szBuffer);
        break;
    case RPGWST_BOW:
        strcpy(szBuffer, "lh3.bwpartb");
        strcat(szBuffer, szTTI);
        szNames.Add(szBuffer);
        strcpy(szBuffer, "bwparta");
        strcat(szBuffer, szTTI);
        szNames.Add(szBuffer);
        strcpy(szBuffer, "bwtetivaa");
        strcat(szBuffer, szTTI);
        szNames.Add(szBuffer);
        strcpy(szBuffer, "bwtetivab");
        strcat(szBuffer, szTTI);
        szNames.Add(szBuffer);
        szNames.Add("quiver");
        szNames.Add("arrows");
        szNames.Add("rh3.arrow00");
        break;
    case RPGWST_CROSSBOW:
        strcpy(szBuffer, "rh3.crbow");
        strcat(szBuffer, szTTI);
        strcat(szBuffer, "main");
        szNames.Add(szBuffer);
        strcpy(szBuffer, "crbow");
        strcat(szBuffer, szTTI);
        strcat(szBuffer, "part01");
        szNames.Add(szBuffer);
        strcpy(szBuffer, "crbow");
        strcat(szBuffer, szTTI);
        strcat(szBuffer, "part02");
        szNames.Add(szBuffer);
        strcpy(szBuffer, "crbow");
        strcat(szBuffer, szTTI);
        strcat(szBuffer, "tetiva01");
        szNames.Add(szBuffer);
        strcpy(szBuffer, "crbow");
        strcat(szBuffer, szTTI);
        strcat(szBuffer, "tetiva02");
        szNames.Add(szBuffer);
        szNames.Add("quiver");
        szNames.Add("arrows");
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemWeapon::GetWeaponModelNames(CStringArray &szNames)
{
    GetWeaponModelNames(GetShape(), szNames);
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemWeapon::GetWeaponModelNames(DWORD dwWeapon, std::vector<std::string> &szNames)
{
    if (((dwWeapon >> 16) & 0xFFFF) >= theItemDatabase.weapons.GetNumEntries())
        return;
    const SIDBItemGeneralShape *pShape = theItemDatabase.weapons[(dwWeapon >> 16) & 0xFFFF];
    char szBuffer[64];
    char szTTI[8];

    sprintf(szTTI, "%.2d", pShape->dwTextureTypeIndex);

    switch (pShape->dwTypeID)
    {
    case RPGWST_SWORD:
        strcpy(szBuffer, "rh3.sword");
        strcat(szBuffer, szTTI);
        szNames.push_back(szBuffer);
        break;
    case RPGWST_AXE:
        strcpy(szBuffer, "rh3.axe");
        strcat(szBuffer, szTTI);
        szNames.push_back(szBuffer);
        break;
    case RPGWST_DAGGER:
        strcpy(szBuffer, "rh3.dagger");
        strcat(szBuffer, szTTI);
        szNames.push_back(szBuffer);
        break;
    case RPGWST_SPEAR:
        strcpy(szBuffer, "rh3.pike");
        strcat(szBuffer, szTTI);
        szNames.push_back(szBuffer);
        break;
    case RPGWST_HAMMER:
        strcpy(szBuffer, "rh3.club");
        strcat(szBuffer, szTTI);
        szNames.push_back(szBuffer);
        break;
    case RPGWST_BOW:
        strcpy(szBuffer, "lh3.bwpartb");
        strcat(szBuffer, szTTI);
        szNames.push_back(szBuffer);
        strcpy(szBuffer, "bwparta");
        strcat(szBuffer, szTTI);
        szNames.push_back(szBuffer);
        strcpy(szBuffer, "bwtetivaa");
        strcat(szBuffer, szTTI);
        szNames.push_back(szBuffer);
        strcpy(szBuffer, "bwtetivab");
        strcat(szBuffer, szTTI);
        szNames.push_back(szBuffer);
        szNames.push_back("quiver");
        szNames.push_back("arrows");
        szNames.push_back("rh3.arrow00");
        break;
    case RPGWST_CROSSBOW:
        strcpy(szBuffer, "rh3.crbow");
        strcat(szBuffer, szTTI);
        strcat(szBuffer, "main");
        szNames.push_back(szBuffer);
        strcpy(szBuffer, "crbow");
        strcat(szBuffer, szTTI);
        strcat(szBuffer, "part01");
        szNames.push_back(szBuffer);
        strcpy(szBuffer, "crbow");
        strcat(szBuffer, szTTI);
        strcat(szBuffer, "part02");
        szNames.push_back(szBuffer);
        strcpy(szBuffer, "crbow");
        strcat(szBuffer, szTTI);
        strcat(szBuffer, "tetiva01");
        szNames.push_back(szBuffer);
        strcpy(szBuffer, "crbow");
        strcat(szBuffer, szTTI);
        strcat(szBuffer, "tetiva02");
        szNames.push_back(szBuffer);
        szNames.push_back("quiver");
        szNames.push_back("arrows");
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
CString CItemWeapon::GetI3DTextureName() const
{
    return CString("unhuma") + FormTextureNameMask();
}
/////////////////////////////////////////////////////////////////////////////////////
CString CItemWeapon::GetI3DFigureName() const
{
    CString szName;
    char buff[4];

    switch (GetWeaponType())
    {
    case RPGWST_SWORD:
        strcpy(buff, "sw");
        break;
    case RPGWST_AXE:
        strcpy(buff, "ax");
        break;
    case RPGWST_DAGGER:
        strcpy(buff, "dg");
        break;
    case RPGWST_SPEAR:
        strcpy(buff, "sp");
        break;
    case RPGWST_HAMMER:
        strcpy(buff, "hm");
        break;
    case RPGWST_BOW:
        strcpy(buff, "bw");
        break;
    case RPGWST_CROSSBOW:
        strcpy(buff, "cb");
        break;
    }
    const SIDBWeaponShape *lpShape = ((CItemManager *)GetManager())->GetWeaponShape(GetShape());
    szName.Format("initwe%s%dweapon", buff, lpShape->dwTextureTypeIndex);

    return szName;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CItemWeapon::IsRanged(DWORD dwWeapon)
{
    const SIDBWeaponShape *pShape = theItemDatabase.weapons[CItem::GetShape(dwWeapon)];
    return (pShape->dwTypeID == RPGWST_BOW) || (pShape->dwTypeID == RPGWST_CROSSBOW);
}
/////////////////////////////////////////////////////////////////////////////////////
float CItemWeapon::GetDamageMin(DWORD dwWeapon)
{
    const SIDBWeaponShape *pShape = theItemDatabase.weapons[CItem::GetShape(dwWeapon)];
    const SIDBMaterial *pMaterial = theItemDatabase.materials[CItem::GetMaterial(dwWeapon)];
    return pShape->fpDamageMin * pMaterial->fpDamageModifier;
}
float CItemWeapon::GetDamageMax(DWORD dwWeapon)
{
    const SIDBWeaponShape *pShape = theItemDatabase.weapons[CItem::GetShape(dwWeapon)];
    const SIDBMaterial *pMaterial = theItemDatabase.materials[CItem::GetMaterial(dwWeapon)];
    return pShape->fpDamageMin * pMaterial->fpDamageModifier +
           pShape->fpDamageRandom * pMaterial->fpDamageModifier;
}
ERPGWeaponTypes CItemWeapon::GetWeaponType(DWORD dwWeapon)
{
    const SIDBWeaponShape *pShape = theItemDatabase.weapons[CItem::GetShape(dwWeapon)];
    return ERPGWeaponTypes(pShape->dwTypeID);
}
float CItemWeapon::GetWeight(DWORD dwWeapon)
{
    const SIDBWeaponShape *pShape = theItemDatabase.weapons[CItem::GetShape(dwWeapon)];
    const SIDBMaterial *pMaterial = theItemDatabase.materials[CItem::GetMaterial(dwWeapon)];
    return pShape->fpWeight * pMaterial->fpWeight;
}
ERPGAttackTypes CItemWeapon::GetAttackType(DWORD dwWeapon)
{
    const SIDBWeaponShape *pShape = theItemDatabase.weapons[CItem::GetShape(dwWeapon)];
    for (int i = 0; i < RPG_NUM_ATTACK_TYPES; ++i)
    {
        if (pShape->fpDamageDistributions[i] != 0)
            return ERPGAttackTypes(i);
    }
    return RPGAT_GENERAL;
}
float CItemWeapon::GetSkillModifier(DWORD dwWeapon, ERPGAutoSkills skill)
{
    const SIDBWeaponShape *pShape = theItemDatabase.weapons[CItem::GetShape(dwWeapon)];
    return pShape->GetSkillModifier(skill);
}
/////////////////////////////////////////////////////////////////////////////////////
float CItemWeapon::GetAtackBonus(DWORD dwWeapon)
{
    const SIDBWeaponShape *pShape = theItemDatabase.weapons[CItem::GetShape(dwWeapon)];
    return pShape->GetAttackBonus();
}
float CItemWeapon::GetDefenceBonus(DWORD dwWeapon)
{
    const SIDBWeaponShape *pShape = theItemDatabase.weapons[CItem::GetShape(dwWeapon)];
    return pShape->GetDefenceBonus();
}
/////////////////////////////////////////////////////////////////////////////////////
float CItemWeapon::GetRandomDamage() const
{
    if (fpDamageRnd)
        return random.GetFloat(fpDamageMin, fpDamageMin + fpDamageRnd);
    else
        return fpDamageMin;
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemWeapon::Load(CSave &file)
{
    CItem::Load(file);
    file.Read(dwProficiencyClass);
    file.Read(fpDamageMin);
    file.Read(fpDamageRnd);
    file.Read(fpRange);
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemWeapon::Store(CSave &file)
{
    CItem::Store(file);
    file.Write(dwProficiencyClass);
    file.Write(fpDamageMin);
    file.Write(fpDamageRnd);
    file.Write(fpRange);
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// CItemArmor
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
CItemArmor::CItemArmor()
{
    MemSetDWord((DWORD *)fpAbsorbtions[0], 0, RPG_NUM_ATTACK_TYPES);
    MemSetDWord((DWORD *)fpAbsorbtions[1], 0, RPG_NUM_ATTACK_TYPES);
    dwCrippleMasks[0] = dwCrippleMasks[1] = 0;

#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemArmor::Setup(const SIDBItemGeneralShape *lpShape)
{
    CItem::Setup(lpShape);
    memcpy(fpAbsorbtions[0], static_cast<const SIDBArmorShape *>(lpShape)->main.fpDistributions,
           sizeof(fpAbsorbtions[0]));
    memcpy(fpAbsorbtions[1], static_cast<const SIDBArmorShape *>(lpShape)->second.fpDistributions,
           sizeof(fpAbsorbtions[1]));
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CItemArmor::Compare(CItem *lpItem)
{
    if (!CItem::Compare(lpItem))
        return FALSE;
    //
    CItemArmor *pArmor = static_cast<CItemArmor *>(lpItem);
    //
    if (memcmp(pArmor->fpAbsorbtions[0], fpAbsorbtions[0], sizeof(fpAbsorbtions[0])) != 0)
        return FALSE;
    if (memcmp(pArmor->fpAbsorbtions[1], fpAbsorbtions[1], sizeof(fpAbsorbtions[1])) != 0)
        return FALSE;
    //
    if (memcmp(pArmor->dwCrippleMasks, dwCrippleMasks, sizeof(dwCrippleMasks)) != 0)
        return FALSE;
    //
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CItemArmor::Duplicate(CItem *lpItem, CSpellManager *pSpellManager) const
{
    // duplicate common item's params
    if (CItem::Duplicate(lpItem, pSpellManager) == FALSE)
        return FALSE;
    //
    CItemArmor *lpArmor = (CItemArmor *)lpItem;
    memcpy(lpArmor->fpAbsorbtions[0], fpAbsorbtions[0], sizeof(lpArmor->fpAbsorbtions[0]));
    memcpy(lpArmor->fpAbsorbtions[1], fpAbsorbtions[1], sizeof(lpArmor->fpAbsorbtions[1]));
    memcpy(lpArmor->dwCrippleMasks, dwCrippleMasks, sizeof(lpArmor->dwCrippleMasks));

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
int CItemArmor::GetALIndex() const
{
    const SIDBArmorShape *lpShape = ((CItemManager *)lpManager)->GetArmorShape(GetShape());
    if ((lpShape->dwTypeID == RPGAST_HELM) || (lpShape->dwTypeID == RPGAST_PLATE) ||
        (lpShape->dwTypeID == RPGAST_LEGGINGS))
        return 2;
    else
        return 1;
}
/////////////////////////////////////////////////////////////////////////////////////
DWORD CItemArmor::GetWearOrder(DWORD dwShape)
{
    const SIDBArmorShape *pShape = theItemDatabase.armors[dwShape];
    return pShape != 0 ? pShape->dwWearOrder : 0;
}
/////////////////////////////////////////////////////////////////////////////////////
CString CItemArmor::FormTextureNameMask() const
{
    const SIDBMaterial *pMaterial = theItemDatabase.materials[GetMaterial()];
    const SIDBArmorShape *pShape = theItemDatabase.armors[GetShape()];
    CString szName;
    if ((pMaterial != 0) && (pShape != 0))
        FormTextureNameMask(szName, pShape, pMaterial);
    return szName;
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemArmor::FormTextureNameMask(CString &szName, DWORD dwShape, DWORD dwMaterial)
{
    const SIDBMaterial *pMaterial = theItemDatabase.materials[dwMaterial];
    const SIDBArmorShape *pShape = theItemDatabase.armors[dwShape];
    szName.Empty();
    if ((pMaterial != 0) && (pShape != 0))
        FormTextureNameMask(szName, pShape, pMaterial);
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemArmor::FormTextureNameMask(CString &szName, const SIDBItemGeneralShape *pShape,
                                     const SIDBMaterial *pMaterial)
{
    char buff[4];
    // armor type code
    switch (pShape->dwTypeID)
    {
    case RPGAST_HELM:
        strcpy(buff, "hl");
        break;
    case RPGAST_PLATE:
        strcpy(buff, "pl");
        break;
    case RPGAST_LEGGINGS:
        strcpy(buff, "lg");
        break;
    case RPGAST_SHIRT:
        strcpy(buff, "sh");
        break;
    case RPGAST_PANTS:
        strcpy(buff, "pt");
        break;
    case RPGAST_BOOTS:
        strcpy(buff, "bt");
        break;
    case RPGAST_GLOVES:
        strcpy(buff, "gl");
        break;
    }
    //
    szName.Format("%s_%.2d.%s.%d", buff, pShape->dwTextureTypeIndex, pMaterial->szCodeName,
                  pShape->dwTextureTypeIndex2);
}
/////////////////////////////////////////////////////////////////////////////////////
CString CItemArmor::GetFigureName() const
{
    const SIDBArmorShape *lpShape = ((CItemManager *)GetManager())->GetArmorShape(GetShape());
    if (lpShape && lpShape->szFigures.GetSize())
        return lpShape->szFigures[0];
    else
        return "";
}
/////////////////////////////////////////////////////////////////////////////////////
CString CItemArmor::GetI3DTextureName() const
{
    return CString("unhuma") + FormTextureNameMask();
}
/////////////////////////////////////////////////////////////////////////////////////
CString CItemArmor::GetI3DFigureName() const
{
    CString szName;
    char buff[4];
    //
    switch (GetArmorType())
    {
    case RPGAST_HELM:
        strcpy(buff, "hl");
        break;
    case RPGAST_PLATE:
        strcpy(buff, "pl");
        break;
    case RPGAST_LEGGINGS:
        strcpy(buff, "lg");
        break;
    case RPGAST_SHIRT:
        strcpy(buff, "sh");
        break;
    case RPGAST_PANTS:
        strcpy(buff, "pt");
        break;
    case RPGAST_BOOTS:
        strcpy(buff, "bt");
        break;
    case RPGAST_GLOVES:
        strcpy(buff, "gl");
        break;
    }
    const SIDBArmorShape *lpShape = theItemDatabase.armors[GetShape()];
    szName.Format("initar%s%darmor", buff, lpShape->dwTextureTypeIndex);

    return szName;
}
/////////////////////////////////////////////////////////////////////////////////////
int CItemArmor::GetALIndex(ERPGArmorTypes type)
{
    if ((type == RPGAST_HELM) || (type == RPGAST_PLATE) || (type == RPGAST_LEGGINGS))
        return 2;
    else
        return 1;
}
/////////////////////////////////////////////////////////////////////////////////////
ERPGArmorTypes CItemArmor::GetArmorType() const
{
    return (ERPGArmorTypes)theItemDatabase.armors[GetShape()]->dwTypeID;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CItemArmor::IsUpper(DWORD dwArmor)
{
    return theItemDatabase.armors[CItem::GetShape(dwArmor)]->IsUpper();
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemArmor::Load(CSave &file)
{
    CItem::Load(file);
    file.Read(fpAbsorbtions[0], sizeof(fpAbsorbtions[0]));
    file.Read(fpAbsorbtions[1], sizeof(fpAbsorbtions[1]));
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemArmor::Store(CSave &file)
{
    CItem::Store(file);
    file.Write(fpAbsorbtions[0], sizeof(fpAbsorbtions[0]));
    file.Write(fpAbsorbtions[1], sizeof(fpAbsorbtions[1]));
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// CItemQuick
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
CItemQuick::CItemQuick()
{
}
CItemQuick::~CItemQuick()
{
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemQuick::Setup(const SIDBItemGeneralShape *lpShape)
{
    CItem::Setup(lpShape);

    dwSkill = ((const SIDBQuickItemShape *)lpShape)->dwSkill;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CItemQuick::Compare(CItem *lpItem)
{
    if (!CItem::Compare(lpItem))
        return FALSE;
    //
    CItemQuick *pQuick = static_cast<CItemQuick *>(lpItem);
    //
    if (pQuick->dwSkill != dwSkill)
        return FALSE;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CItemQuick::Duplicate(CItem *lpItem, CSpellManager *pSpellManager) const
{
    // duplicate common item's params
    if (CItem::Duplicate(lpItem, pSpellManager) == FALSE)
        return FALSE;
    //
    CItemQuick *lpQuick = (CItemQuick *)lpItem;
    lpQuick->dwSkill = dwSkill;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemQuick::Load(CSave &file)
{
    CItem::Load(file);
    file.Read(dwSkill);
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemQuick::Store(CSave &file)
{
    CItem::Store(file);
    file.Write(dwSkill);
}
/////////////////////////////////////////////////////////////////////////////////////
CString CItemQuick::GetI3DTextureName() const
{
    CString szName;
    if (GetShapeEntry() && GetShapeEntry()->szMaterialType.CompareNoCase("none") != 0)
        szName.Format("qitem%04d.%s", GetTTIndex(), GetMaterialCode());
    else
        szName.Format("qitem%04d", GetTTIndex());
    return szName;
}
/////////////////////////////////////////////////////////////////////////////////////
CString CItemQuick::GetI3DFigureName() const
{
    CString szName;
    szName.Format("initqi%ditem", GetTTIndex());
    return szName;
}
/////////////////////////////////////////////////////////////////////////////////////
EIDBQuickItemType CItemQuick::GetQuickItemType() const
{
    const SIDBQuickItemShape *pShape = static_cast<const SIDBQuickItemShape *>(GetShapeEntry());
    if (pShape)
        return pShape->idbType;
    else
        return IDB_QUICK_ITEM_FORCE_DWORD;
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// CItemQuest
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
CItemQuest::CItemQuest()
{
}
CItemQuest::~CItemQuest()
{
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemQuest::Setup(const SIDBItemGeneralShape *lpShape)
{
    CItem::Setup(lpShape);

    dwScriptID = ((SIDBQuestItemShape *)lpShape)->dwScriptID;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CItemQuest::Compare(CItem *lpItem)
{
    if (!CItem::Compare(lpItem))
        return FALSE;
    //
    CItemQuest *pQuest = static_cast<CItemQuest *>(lpItem);
    if (pQuest->dwScriptID != dwScriptID)
        return FALSE;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CItemQuest::Duplicate(CItem *lpItem, CSpellManager *pSpellManager) const
{
    // duplicate common item's params
    if (CItem::Duplicate(lpItem, pSpellManager) == FALSE)
        return FALSE;
    //
    CItemQuest *lpQuick = (CItemQuest *)lpItem;
    lpQuick->dwScriptID = dwScriptID;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemQuest::Load(CSave &file)
{
    CItem::Load(file);
    file.Read(dwScriptID);
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemQuest::Store(CSave &file)
{
    CItem::Store(file);
    file.Write(dwScriptID);
}
/////////////////////////////////////////////////////////////////////////////////////
CString CItemQuest::GetI3DTextureName() const
{
    CString szName;
    szName.Format("quitem%04d", GetTTIndex());
    return szName;
}
/////////////////////////////////////////////////////////////////////////////////////
CString CItemQuest::GetI3DFigureName() const
{
    CString szName;
    szName.Format("initqu%ditem", GetTTIndex());
    return szName;
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// CItemLoot
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
CItemLoot::CItemLoot()
{
}
/////////////////////////////////////////////////////////////////////////////////////
CItemLoot::~CItemLoot()
{
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemLoot::Setup(const SIDBItemGeneralShape *lpShape)
{
    CItem::Setup(lpShape);

    litType = ((const SIDBLootItemShape *)lpShape)->litType;
    itInstructionType = ((const SIDBLootItemShape *)lpShape)->itInstructionType;
    dwMaterialIndex = ((const SIDBLootItemShape *)lpShape)->dwMaterialIndex;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CItemLoot::Compare(CItem *lpItem)
{
    if (!CItem::Compare(lpItem))
        return FALSE;
    //
    CItemLoot *pLoot = static_cast<CItemLoot *>(lpItem);
    //
    if (pLoot->litType != litType)
        return FALSE;
    if (pLoot->itInstructionType != itInstructionType)
        return FALSE;
    if (pLoot->dwMaterialIndex != dwMaterialIndex)
        return FALSE;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CItemLoot::Duplicate(CItem *lpItem, CSpellManager *pSpellManager) const
{
    // duplicate common item's params
    if (CItem::Duplicate(lpItem, pSpellManager) == FALSE)
        return FALSE;
    //
    CItemLoot *pLoot = static_cast<CItemLoot *>(lpItem);
    pLoot->litType = litType;
    pLoot->itInstructionType = itInstructionType;
    pLoot->dwMaterialIndex = dwMaterialIndex;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
CString CItemLoot::GetI3DTextureName() const
{
    CString szName;
    if (GetLootType() == IDBLIT_INSTRUCTION)
    {
        CString szInstrType;
        EIDBInstructionType eType = GetInstructionType();
        switch (eType)
        {
        case IDBINS_WEAPON: {
            const SIDBWeaponShape *lpShape = theItemDatabase.weapons[GetInstructionItemIndex()];
            CString szFormat;
            switch ((ERPGWeaponTypes)lpShape->dwTypeID)
            {
            case RPGWST_SWORD:
                szFormat = "sw";
                break;
            case RPGWST_AXE:
                szFormat = "ax";
                break;
            case RPGWST_DAGGER:
                szFormat = "dg";
                break;
            case RPGWST_SPEAR:
                szFormat = "sp";
                break;
            case RPGWST_HAMMER:
                szFormat = "hm";
                break;
            case RPGWST_BOW:
                szFormat = "bw";
                break;
            case RPGWST_CROSSBOW:
                szFormat = "cb";
                break;
            }
            szName.Format("%s_%.2d.%d", szFormat, lpShape->dwTextureTypeIndex,
                          lpShape->dwTextureTypeIndex2);
        }
        break;
        case IDBINS_ARMOR: {
            const SIDBArmorShape *lpShape = theItemDatabase.armors[GetInstructionItemIndex()];
            CString szFormat;
            switch ((ERPGArmorTypes)lpShape->dwTypeID)
            {
            case RPGAST_HELM:
                szFormat = "hl";
                break;
            case RPGAST_PLATE:
                szFormat = "pl";
                break;
            case RPGAST_LEGGINGS:
                szFormat = "lg";
                break;
            case RPGAST_SHIRT:
                szFormat = "sh";
                break;
            case RPGAST_PANTS:
                szFormat = "pt";
                break;
            case RPGAST_BOOTS:
                szFormat = "bt";
                break;
            case RPGAST_GLOVES:
                szFormat = "gl";
                break;
            }
            szName.Format("%s_%.2d.%d", szFormat, lpShape->dwTextureTypeIndex,
                          lpShape->dwTextureTypeIndex2);
        }
        break;
        case IDBINS_QUICK: {
            const SIDBQuickItemShape *lpShape = theItemDatabase
                                                    .quickItems[GetInstructionItemIndex()];
            szName.Format("qitem%04d", lpShape->dwTextureTypeIndex);
        }
        break;
        case IDBINS_PROTOTYPE: {
            int nSpell = theSpellDatabase.spells[GetInstructionItemIndex()]->dwTextureTypeIndex;
            szName.Format("prototype%04d", nSpell);
        }
        break;
        case IDBINS_RUNE:
            szName.Format("modifier%04d", GetInstructionItemIndex());
            break;
        }
    }
    else if (GetLootType() == IDBLIT_MATERIAL)
    {
        szName.Format("material%04d", GetMaterialIndex());
    }
    else
    {
        szName.Format("litem%04d", GetTTIndex());
    }
    return szName;
}
/////////////////////////////////////////////////////////////////////////////////////
CString CItemLoot::GetI3DFigureName() const
{
    CString szName;
    if (GetLootType() == IDBLIT_INSTRUCTION)
    {
        EIDBInstructionType eType = GetInstructionType();
        switch (eType)
        {
        case IDBINS_WEAPON: {
            CString szFormat;
            const SIDBWeaponShape *lpShape = ((CItemManager *)GetManager())
                                                 ->GetWeaponShape(GetInstructionItemIndex());
            switch ((ERPGWeaponTypes)lpShape->dwTypeID)
            {
            case RPGWST_SWORD:
                szFormat = "sw";
                break;
            case RPGWST_AXE:
                szFormat = "ax";
                break;
            case RPGWST_DAGGER:
                szFormat = "dg";
                break;
            case RPGWST_SPEAR:
                szFormat = "sp";
                break;
            case RPGWST_HAMMER:
                szFormat = "hm";
                break;
            case RPGWST_BOW:
                szFormat = "bw";
                break;
            case RPGWST_CROSSBOW:
                szFormat = "cb";
                break;
            }
            szName.Format("initwe%s%dweapon", szFormat, lpShape->dwTextureTypeIndex);
        }
        break;
        case IDBINS_ARMOR: {
            CString szFormat;
            const SIDBArmorShape *lpShape = ((CItemManager *)GetManager())
                                                ->GetArmorShape(GetInstructionItemIndex());
            switch ((ERPGArmorTypes)lpShape->dwTypeID)
            {
            case RPGAST_HELM:
                szFormat = "hl";
                break;
            case RPGAST_PLATE:
                szFormat = "pl";
                break;
            case RPGAST_LEGGINGS:
                szFormat = "lg";
                break;
            case RPGAST_SHIRT:
                szFormat = "sh";
                break;
            case RPGAST_PANTS:
                szFormat = "pt";
                break;
            case RPGAST_BOOTS:
                szFormat = "bt";
                break;
            case RPGAST_GLOVES:
                szFormat = "gl";
                break;
            }
            szName.Format("initar%s%darmor", szFormat, lpShape->dwTextureTypeIndex);
        }
        break;
        case IDBINS_QUICK: {
            const SIDBQuickItemShape *lpShape = ((CItemManager *)GetManager())
                                                    ->GetQuickItemShape(GetInstructionItemIndex());
            szName.Format("initqi%ditem", lpShape->dwTextureTypeIndex);
        }
        break;
        case IDBINS_PROTOTYPE:
            szName = "initqi1item";
            break;
        case IDBINS_RUNE:
            szName = "initqi1item";
            break;
        }
    }
    else if (GetLootType() == IDBLIT_MATERIAL)
        szName.Format("initlimt%ditem", GetMaterialIndex());
    else
        szName.Format("initlitr%ditem", GetTTIndex());
    return szName;
}
/////////////////////////////////////////////////////////////////////////////////////
float CItemLoot::GetPrice() const
{
    switch (litType)
    {
    case IDBLIT_TREASURE:
        return CItem::GetPrice();
    case IDBLIT_MATERIAL:
        return theItemDatabase.materials[dwMaterialIndex]->fpPrice;
    case IDBLIT_INSTRUCTION: {
        switch (itInstructionType)
        {
        case IDBINS_ARMOR:
            return theItemDatabase.armors[dwMaterialIndex]->fpPrice;
        case IDBINS_WEAPON:
            return theItemDatabase.weapons[dwMaterialIndex]->fpPrice;
        case IDBINS_QUICK:
            return theItemDatabase.quickItems[dwMaterialIndex]->fpPrice;
        case IDBINS_PROTOTYPE:
            return theSpellDatabase.spells[dwMaterialIndex]->fpPrice;
        case IDBINS_RUNE:
            return theSpellDatabase.runes[dwMaterialIndex]->fpPrice;
        }
    }
    }

    return -1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemLoot::Load(CSave &file)
{
    CItem::Load(file);
    DWORD dwValue;
    file.Read(dwValue);
    litType = static_cast<EIDBLootItemType>(dwValue);
    file.Read(dwValue);
    itInstructionType = static_cast<EIDBInstructionType>(dwValue);
    file.Read(dwMaterialIndex);
}
/////////////////////////////////////////////////////////////////////////////////////
void CItemLoot::Store(CSave &file)
{
    CItem::Store(file);
    file.Write(litType);
    file.Write(itInstructionType);
    file.Write(dwMaterialIndex);
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// CItemSpell
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
CItemSpell::CItemSpell()
{
}
/////////////////////////////////////////////////////////////////////////////////////
CItemSpell::~CItemSpell()
{
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CItemSpell::Duplicate(CItem *lpItem, CSpellManager *pSpellManager) const
{
    // duplicate common item's params
    if (CItemQuick::Duplicate(lpItem, pSpellManager) == FALSE)
        return FALSE;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
CString CItemSpell::GetI3DTextureName() const
{
    CString szName;
    ASSERT(pSpell != 0);
    int nSpell = int(HIWORD(pSpell->GetSubtypeID()));
    nSpell = theSpellDatabase.spells[nSpell]->dwTextureTypeIndex;
    szName.Format("spell%04d", nSpell);
    return szName;
}
/////////////////////////////////////////////////////////////////////////////////////
CString CItemSpell::GetI3DFigureName() const
{
    return CString("initqi1item");
}
/////////////////////////////////////////////////////////////////////////////////////
