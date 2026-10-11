#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include <locale.h>

#include "TextManager.h"
#include "StringProcessing.h"
#include "WorldClient.h"
#include "Item.h"
#include "ItemManager.h"
#include "SpellManager.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "ModItemUiPolicy.h"
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMultipleResource::OpenResource(LPCSTR pszFileName, BOOL bWriteAccess)
{
    for (auto pos = resources.begin(); pos != resources.end(); ++pos)
    {
        if (pos->first == pszFileName)
            return TRUE;
    }
    //
    resources.push_back(CResourcePair(pszFileName, dbgnew CMappedResource));
    return resources.back().second->OpenResource(pszFileName, bWriteAccess);
}
/////////////////////////////////////////////////////////////////////////////////////
void CMultipleResource::CloseResource(LPCSTR pszFileName)
{
    for (auto pos = resources.begin(); pos != resources.end(); ++pos)
    {
        if (pos->first == pszFileName)
        {
            pos->second->CloseResource();
            delete pos->second;
            resources.erase(pos);
            return;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CMultipleResource::CloseResource()
{
    for (auto pos = resources.begin(); pos != resources.end(); ++pos)
    {
        pos->second->CloseResource();
        delete pos->second;
    }
    resources.clear();
}
/////////////////////////////////////////////////////////////////////////////////////
LPBYTE CMultipleResource::GetFile(LPCSTR pszFileName, LPDWORD pnDataSize)
{
    for (auto pos = resources.begin(); pos != resources.end(); ++pos)
    {
        BYTE *pData = pos->second->GetFile(pszFileName, pnDataSize);
        if (pData)
            return pData;
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CMultipleResource::CloseFile(LPBYTE pFileImage)
{
    for (auto pos = resources.begin(); pos != resources.end(); ++pos)
    {
        if (pos->second->CloseFile(pFileImage) == true)
            return true;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CMultipleResource::GetFileList(CStringArray &aNames, LPCSTR pszMask)
{
    CStringArray szArray;
    aNames.RemoveAll();
    for (auto pos = resources.begin(); pos != resources.end(); ++pos)
    {
        pos->second->GetFileList(szArray, pszMask);
        aNames.Append(szArray);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMultipleResource::CheckFileExist(LPCSTR pszFileName)
{
    BOOL bExist = FALSE;
    for (auto pos = resources.begin(); pos != resources.end(); ++pos)
        bExist = bExist || pos->second->CheckFileExist(pszFileName);
    return bExist;
}
/////////////////////////////////////////////////////////////////////////////////////
// —борка мусора
#ifdef TRY_OPEN_EXTERNAL_RESOURCES
void CMultipleResource::CloseFiles()
{
    for (auto pos = resources.begin(); pos != resources.end(); ++pos)
        pos->second->CloseFiles();
}
#endif
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
//                                text manager
// ******************************************************************************** //
BOOL CTextManager::Create()
{
    setlocale(LC_CTYPE, theSettings.szCharSet);
#ifdef _MBCS
    _setmbcp(_MB_CP_LOCALE);
#endif // _MBCS
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTextManager::Destroy()
{
    resource.CloseFiles();
    resource.CloseResource();
}
/////////////////////////////////////////////////////////////////////////////////////
void CTextManager::SelectResource(LPCTSTR pszResourceName)
{
    resource.OpenResource(pszResourceName);
}
/////////////////////////////////////////////////////////////////////////////////////
void CTextManager::CloseResource(LPCTSTR pszResourceName)
{
    resource.CloseResource(pszResourceName);
}
/////////////////////////////////////////////////////////////////////////////////////
void CTextManager::CloseResource()
{
    resource.CloseResource();
}
/////////////////////////////////////////////////////////////////////////////////////
LPCTSTR CTextManager::LoadSingleLine(int nID)
{
    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
LPCTSTR CTextManager::LoadText(LPCTSTR pszFileName)
{
    LPBYTE pData;
    LPCTSTR pszResult;
    wchar_t *unicode;
    LPTSTR mbcs;
    DWORD lengthu, lengthm;

    pData = resource.GetFile(pszFileName, &lengthu);
    if (!pData)
        return NULL;

    unicode = (wchar_t *)malloc(lengthu + 2);
    memcpy(unicode, pData, lengthu);
    resource.CloseFile(pData);
    unicode[lengthu >> 1] = 0;

    mbcs = (LPTSTR)malloc(lengthu);
    lengthm = wcstombs(mbcs, unicode + 1, lengthu - 2);

    if (lengthm == -1)
    {
        free((void *)unicode);
        free((void *)mbcs);
        return NULL;
    }
    mbcs[lengthm] = 0;

    pszResult = (LPCTSTR)malloc(lengthm + 1);
    strcpy((LPTSTR)pszResult, mbcs);

    free((void *)unicode);
    free((void *)mbcs);
    return pszResult;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTextManager::Release(LPCTSTR pszString)
{
    free((void *)pszString);
}
/////////////////////////////////////////////////////////////////////////////////////
void CTextManager::GetTextNames(CStringArray &szNames, LPCTSTR pszMask)
{
    resource.GetFileList(szNames, pszMask);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTextManager::GetText(LPCTSTR pszFileName)
{
    if (pszBegin)
        CloseText();
    pszBegin = pszCurPos = (LPCTSTR)resource.GetFile(pszFileName, &dwLength);
    if (pszCurPos && dwLength > 0)
        return TRUE;
    else
        return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTextManager::CloseText()
{
    pszBegin = NULL;
    pszCurPos = NULL;
    resource.CloseFiles();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTextManager::FindNextCommand(LPCTSTR pszCommand)
{
    CStringArray szCommands, szWords;
    CString szTmp;
    if (!pszCommand)
        while (GetFirstWord(szTmp))
        {
            if (szTmp.Left(1) == "#" && szTmp.Left(2) != "##")
                return TRUE;
            NextLine();
        }
    else
    {
        NStrProc::SplitString(pszCommand, szCommands, ' ');
        if (szCommands.GetSize() > 1)
            while (GetStringArray(szWords, FALSE))
            {
                if (szWords.GetSize() >= szCommands.GetSize())
                {
                    bool bEqual = true;
                    for (int i = 0; i < szCommands.GetSize(); i++)
                    {
                        szTmp = szWords[i];
                        szTmp.MakeLower();
                        if (szTmp != szCommands[i])
                        {
                            bEqual = false;
                            break;
                        }
                    }
                    if (bEqual)
                        return TRUE;
                }
                SkipLine();
            }
        else
            while (GetFirstWord(szTmp))
            {
                if (szTmp == pszCommand)
                    return TRUE;
                SkipLine();
            }
    }
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTextManager::SkipLine()
{
    CString szTmp;
    NextLine();
    while (GetFirstWord(szTmp))
    {
        if (szTmp.Left(2) != "##")
            return TRUE;
        NextLine();
    }
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTextManager::NextLine()
{
    LPCTSTR ptr = pszCurPos;
    if (IsEOF(ptr))
        return FALSE;
    while (!IsEOF(ptr) && _tcsnextc(ptr) != '\n')
        ptr = _tcsinc(ptr);
    if (IsEOF(ptr))
    {
        pszCurPos = ptr;
        return FALSE;
    }
    pszCurPos = _tcsinc(ptr);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTextManager::GetFirstWord(CString &szRes)
{
    int iChar;
    CString szChar;
    int nMode = 0;
    LPCTSTR ptr = pszCurPos;
    szRes.Empty();
    if (IsEOF(ptr))
        return FALSE;
    while (!IsEOF(ptr))
    {
        iChar = _tcsnextc(ptr);
        if (iChar == '\r' || iChar == '\t')
            break;
        if (iChar == ' ' || iChar == '\n')
        {
            if (nMode == 1)
                break;
            ptr = _tcsinc(ptr);
            continue;
        }
        nMode = 1;
        if (isleadbyte(*ptr))
        {
            szChar = *ptr;
            szChar += *(ptr + 1);
        }
        else
            szChar = *ptr;
        szRes += szChar;
        ptr = _tcsinc(ptr);
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTextManager::GetLine(CString &szRes, BOOL bMove)
{
    int iChar;
    CString szChar;
    LPCTSTR ptr = pszCurPos;
    szRes.Empty();
    if (IsEOF(ptr))
        return FALSE;
    while (!IsEOF(ptr))
    {
        iChar = _tcsnextc(ptr);
        if (iChar == '\n' || iChar == '\r')
            break;
        if (isleadbyte(*ptr))
        {
            szChar = *ptr;
            szChar += *(ptr + 1);
        }
        else
            szChar = *ptr;
        szRes += szChar;
        ptr = _tcsinc(ptr);
    }
    if (bMove)
        SkipLine();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTextManager::GetUntilNextCommand(CString &szRes, BOOL bMove)
{
    int iChar;
    CString szChar;
    LPCTSTR ptr = pszCurPos;
    szRes.Empty();
    if (IsEOF(ptr))
        return FALSE;
    while (!IsEOF(ptr))
    {
        iChar = _tcsnextc(ptr);
        if (iChar == '#')
            break;
        if (isleadbyte(*ptr))
        {
            szChar = *ptr;
            szChar += *(ptr + 1);
        }
        else
            szChar = *ptr;
        szRes += szChar;
        ptr = _tcsinc(ptr);
    }
    if (bMove)
        pszCurPos = ptr;
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTextManager::GetStringArray(CStringArray &szWords, BOOL bMove)
{
    int iChar;
    CString szChar;
    int nMode = 0;
    LPCTSTR ptr = pszCurPos;
    CString szTmp;
    szWords.RemoveAll();
    if (IsEOF(ptr))
        return FALSE;
    while (!IsEOF(ptr))
    {
        iChar = _tcsnextc(ptr);
        if (iChar == '\n' || iChar == '\r')
        {
            if (!szTmp.IsEmpty())
            {
                szWords.Add(szTmp);
                szTmp.Empty();
            }
            break;
        }
        if (iChar == ' ' || iChar == '\t')
        {
            if (nMode == 1)
            {
                nMode = 0;
                if (!szTmp.IsEmpty())
                {
                    szWords.Add(szTmp);
                    szTmp.Empty();
                }
            }
            ptr = _tcsinc(ptr);
            continue;
        }
        nMode = 1;
        if (isleadbyte(*ptr))
        {
            szChar = *ptr;
            szChar += *(ptr + 1);
        }
        else
            szChar = *ptr;
        szTmp += szChar;
        ptr = _tcsinc(ptr);
    }
    if (bMove)
        SkipLine();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTextManager::IsEOF(LPCTSTR ptr)
{
    if ((LPBYTE)ptr - (LPBYTE)pszBegin < int(dwLength))
        return FALSE;
    else
        return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTextManager::GetSystemString(LPCTSTR pszStrName, CString &szRes)
{
    if (pszCurPos)
        CloseText();
    if (GetText(CString("string ") + pszStrName))
    {
        GetUntilNextCommand(szRes);
        szRes.TrimRight("\r\n");
        CloseText();
        return TRUE;
    }
    else
    {
        if ((LPCTSTR)szRes != pszStrName)
            szRes = pszStrName;
        CloseText();
        return FALSE;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTextManager::GetItemNameAndDesc(CItem *pItem, CString &szName, CString &szDesc,
                                      CString &szType)
{
    CItemManager *pItmMng = theWorld.GetItemManager();
    CSpellManager *pSplMng = theWorld.GetSpellManager();

    ETypeIDs tid = pItem->GetTypeID();
    DWORD dwShape = pItem->GetShape();
    DWORD dwMaterial = pItem->GetMaterial();

    const char *pszType = 0;
    const SIDBArmorShape *pShape;
    CString szShape, szMaterial;
    szName.Empty();
    szDesc.Empty();
    szType.Empty();

    switch (tid)
    {
    case ITID_ITEM_SPELL:
        if (pItem->GetSpellsCount() > 0)
        {
            CSpell *pSpell = pItem->GetSpell(0);
            if (GetText(CString("spell ") + pSpell->GetPrototype()->szCodeName))
            {
                GetLine(szName);
                GetUntilNextCommand(szDesc);
            }
            pszType = "item_spell";
        }
        break;

    case ITID_ITEM_ARMOR: {
        szMaterial = pItmMng->GetMaterial(dwMaterial)->szName;
        NStrProc::ReplaceChars(szMaterial, ' ', '_');
        szMaterial.MakeLower();

        pShape = pItmMng->GetArmorShape(dwShape);
        szShape = pShape->szName;
        NStrProc::ReplaceChars(szShape, ' ', '_');
        szShape.MakeLower();

        if (GetText(CString("armor ") + szShape + " " + szMaterial))
        {
            GetLine(szName);
            GetUntilNextCommand(szDesc);
        }
        if (pShape->IsUpper())
            pszType = "item_armor_upper";
        else
            pszType = "item_armor_lower";
    }
    break;

    case ITID_ITEM_WEAPON: {
        szMaterial = pItmMng->GetMaterial(dwMaterial)->szName;
        NStrProc::ReplaceChars(szMaterial, ' ', '_');
        szMaterial.MakeLower();

        szShape = pItmMng->GetWeaponShape(dwShape)->szName;
        NStrProc::ReplaceChars(szShape, ' ', '_');
        szShape.MakeLower();

        if (GetText(CString("weapon ") + szShape + " " + szMaterial))
        {
            GetLine(szName);
            GetUntilNextCommand(szDesc);
        }
        pszType = "item_weapon";
    }
    break;

    case ITID_ITEM_QUICK: {
        szShape = pItmMng->GetQuickItemShape(dwShape)->szName;
        NStrProc::ReplaceChars(szShape, ' ', '_');
        szShape.MakeLower();

        if (pItem->GetShapeEntry() &&
            pItem->GetShapeEntry()->szMaterialType.CompareNoCase("none") == 0)
        {
            if (GetText(CString("qitem ") + szShape))
            {
                GetLine(szName);
                GetUntilNextCommand(szDesc);
            }
            pszType = "item_potion";
        }
        else
        {
            szMaterial = pItmMng->GetMaterial(dwMaterial)->szName;
            NStrProc::ReplaceChars(szMaterial, ' ', '_');
            szMaterial.MakeLower();
            if (GetText(CString("qitem ") + szShape + " " + szMaterial))
            {
                GetLine(szName);
                GetUntilNextCommand(szDesc);
            }
            if (static_cast<CItemQuick *>(pItem)->GetQuickItemType() == IDB_WAND)
            {
                CSpell *pSpell = pItem->GetSpell(0);
                if (pSpell)
                {
                    CString szTmp("Unknown");
                    if (GetText(CString("spell ") + pSpell->GetPrototype()->szCodeName))
                        GetLine(szTmp);
                    szName += ": " + szTmp;
                }
                pszType = "item_wand";
            }
        }
    }
    break;

    case ITID_ITEM_QUEST: {
        szShape = pItmMng->GetQuestItemShape(dwShape)->szName;
        NStrProc::ReplaceChars(szShape, ' ', '_');
        szShape.MakeLower();

        if (GetText(CString("questitem ") + szShape))
        {
            GetLine(szName);
            GetUntilNextCommand(szDesc);
        }
        pszType = "item_quest";
    }
    break;

    case ITID_ITEM_LOOT: {
        CItemLoot *pLoot = static_cast<CItemLoot *>(pItem);
        if (pLoot->GetLootType() == IDBLIT_TREASURE)
        {
            szShape = pItmMng->GetLootItemShape(dwShape)->szName;
            NStrProc::ReplaceChars(szShape, ' ', '_');
            szShape.MakeLower();

            if (GetText(CString("litem ") + szShape))
            {
                GetLine(szName);
                GetUntilNextCommand(szDesc);
            }
            pszType = "item_loot";
        }
        else if (pLoot->GetLootType() == IDBLIT_MATERIAL)
        {
            szMaterial = pItmMng->GetMaterial(pLoot->GetMaterialIndex())->szName;
            NStrProc::ReplaceChars(szMaterial, ' ', '_');
            szMaterial.MakeLower();

            if (GetText(CString("material ") + szMaterial))
            {
                GetLine(szName);
                GetUntilNextCommand(szDesc);
            }
            pszType = "item_material";
        }
        else if (pLoot->GetLootType() == IDBLIT_INSTRUCTION)
        {
            switch (pLoot->GetInstructionType())
            {
            case IDBINS_WEAPON:
                szShape = pItmMng->GetWeaponShape(pLoot->GetInstructionItemIndex())->szName;
                NStrProc::ReplaceChars(szShape, ' ', '_');
                szShape.MakeLower();
                if (GetText(CString("instr ") + szShape))
                {
                    GetLine(szName);
                    GetUntilNextCommand(szDesc);
                }
                pszType = "item_weapon_shape";
                break;
            case IDBINS_ARMOR:
                pShape = pItmMng->GetArmorShape(pLoot->GetInstructionItemIndex());
                szShape = pShape->szName;
                NStrProc::ReplaceChars(szShape, ' ', '_');
                szShape.MakeLower();
                if (GetText(CString("instr ") + szShape))
                {
                    GetLine(szName);
                    GetUntilNextCommand(szDesc);
                }
                if (pShape->IsUpper())
                    pszType = "item_armor_shape_upper";
                else
                    pszType = "item_armor_shape_lower";
                break;
            case IDBINS_QUICK:
                szShape = pItmMng->GetQuickItemShape(pLoot->GetInstructionItemIndex())->szName;
                NStrProc::ReplaceChars(szShape, ' ', '_');
                szShape.MakeLower();
                if (GetText(CString("instr ") + szShape))
                {
                    GetLine(szName);
                    GetUntilNextCommand(szDesc);
                }
                pszType = "item_wand_shape";
                break;
            case IDBINS_PROTOTYPE:
                if (GetText(CString("spell ") +
                            pSplMng->GetPrototype(pLoot->GetInstructionItemIndex())->szCodeName))
                {
                    GetLine(szName);
                    GetUntilNextCommand(szDesc);
                }
                pszType = "item_prototype";
                break;
            case IDBINS_RUNE:
                if (GetText(CString("modifier ") +
                            pSplMng->GetModifier(pLoot->GetInstructionItemIndex())->pszCode))
                {
                    GetLine(szName);
                    GetUntilNextCommand(szDesc);
                }
                pszType = "item_modifier";
                break;
            }
        }
    }
    break;
    }
    if (pszType)
        GetSystemString(pszType, szType);
    if (auto* mod = ModRuntime::Current(); mod && tid == ITID_ITEM_SPELL)
    {
        CSpell* spell = pItem->GetSpell(0);
        const char* code = spell && spell->GetPrototype()
            ? LPCSTR(spell->GetPrototype()->szCodeName) : nullptr;
        const char* name = ModItemUiPolicy::WeaponSkillName(code);
        ModSkills::Config config;
        if (name && mod->SkillConfig(spell, config))
        {
            if (!_stricmp(code, "test") && GetText("spell powerhit"))
            {
                GetLine(szName);
                GetUntilNextCommand(szDesc);
            }
            szName = name;
        }
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTextManager::GetExtendedItemName(CItem *pItem, CString &szOut)
{
    CString szName, szTmp, szType;
    if (!GetItemNameAndDesc(pItem, szName, szTmp, szType))
        return FALSE;
    if (pItem->GetCount() > 1)
    {
        GetSystemString("format_item2", szTmp);
        szOut.Format(szTmp, szType, szName, pItem->GetCount());
    }
    else
    {
        GetSystemString("format_item1", szTmp);
        szOut.Format(szTmp, szType, szName);
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
