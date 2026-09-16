#pragma once
/////////////////////////////////////////////////////////////////////////////////////
#include "StdAfx.h"
#include "GameBase.h"
#include "CMappedResource.h"

#include <list>
#include <string>
/////////////////////////////////////////////////////////////////////////////////////
class CItem;
/////////////////////////////////////////////////////////////////////////////////////
class CMultipleResource
{
    typedef std::pair<std::string, CMappedResource *> CResourcePair;
    typedef std::list<CResourcePair> CResourceList;

public:
    CResourceList resources;

public:
    ~CMultipleResource() { CloseResource(); }
    BOOL OpenResource(LPCSTR pszFileName, BOOL bWriteAccess = FALSE);
    void CloseResource();
    void CloseResource(LPCSTR pszFileName);
    // Ищет требуемый ресурс и возвращает его координаты
    // NULL если не найден, если размер ресурса не требуется,
    // можно оставить равным NULL
    LPBYTE GetFile(LPCSTR pszFileName, LPDWORD pnDataSize = NULL);
    bool CloseFile(LPBYTE pFileImage);
    //
    void GetFileList(CStringArray &aNames, LPCSTR pszMask = NULL); // список всех файлов в ресурсе
    BOOL CheckFileExist(LPCSTR pszFileName);
// Сборка мусора
#ifdef TRY_OPEN_EXTERNAL_RESOURCES
    void CloseFiles();
#else
    void CloseFiles() {}
#endif
};
/////////////////////////////////////////////////////////////////////////////////////
class CTextManager : public CGameBase
{
private:
    CMultipleResource resource;
    LPCTSTR pszCurPos;
    LPCTSTR pszBegin;
    DWORD dwLength;

public:
    CTextManager() { Create(); }
    virtual ~CTextManager() { Destroy(); }

    virtual BOOL Create();
    virtual void Destroy();

    void SelectResource(LPCTSTR pszResourceName);
    void CloseResource(LPCTSTR pszResourceName);
    void CloseResource();

    LPCTSTR LoadSingleLine(int nID);
    LPCTSTR LoadText(LPCTSTR pszFileName);
    void Release(LPCTSTR pszString);

    void GetTextNames(CStringArray &szNames, LPCTSTR pszMask = NULL);

    // following commands for the script parsing
    BOOL GetSystemString(LPCTSTR pszStrName, CString &szRes);
    BOOL GetItemNameAndDesc(CItem *pItem, CString &szName, CString &szDesc, CString &szType);
    BOOL GetExtendedItemName(CItem *pItem, CString &szName);

    BOOL GetText(LPCTSTR pszFileName); // open ( map ) text file
    BOOL CloseText();                  // close file

    LPCTSTR GetPos() { return pszCurPos; }
    void SetPos(LPCTSTR pszPos) { pszCurPos = pszPos; }

    BOOL FindNextCommand(LPCTSTR pszCommand = NULL); // find a #command
    BOOL SkipLine();

    BOOL NextLine(); // move ptr to the beginning of next line

    BOOL GetFirstWord(CString &szRes);               // get until space or cr or tab or something
    BOOL GetLine(CString &szRes, BOOL bMove = TRUE); // get all until cr
    BOOL GetUntilNextCommand(CString &szRes, BOOL bMove = TRUE); // get all until #command
    BOOL GetStringArray(CStringArray &szWords,
                        BOOL bMove = TRUE); // get all words from current line until cr
    BOOL IsEOF(LPCTSTR ptr);

    // class identification functions
    LPCSTR GetGameClassName() const { return "CTextManager"; }
    EClassIDs WhoAmI() const { return HPC_TEXTMANAGER; }
};
/////////////////////////////////////////////////////////////////////////////////////
