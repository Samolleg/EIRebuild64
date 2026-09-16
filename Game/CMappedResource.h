#pragma once
/*--------------------------------------------------------------------------------------------
CMappedResource - class to access resources from resource file using
Win32 file mapping

Written by Ivanov E.N.
Nival Entertainment, 1998
--------------------------------------------------------------------------------------------*/
#include "Win32Classes.h"
#include "CResourceBase.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
struct SResourceFileRecord;

class CMappedResource : public CResourceBase
{
private:
    int hFile;           // handle to resource file
    LPBYTE pBaseAddress; // NULL if opened from file
    BOOL bWriteAccess;
    std::unordered_map<DWORD, SResourceFileRecord *> recByData;
    std::unordered_map<std::string, SResourceFileRecord *> recByName;

public:
    explicit CMappedResource(bool raw=false);
    bool rawResource=false,ignoreOriginal=false;
    std::vector<std::unique_ptr<CMappedResource>> overlays;
    void OpenOverlays(LPCSTR,bool);
    LPBYTE OverlayFile(LPCSTR,LPDWORD);
    virtual void GetFileList(CStringArray&,LPCSTR mask=NULL);
    virtual BOOL CheckFileExist(LPCSTR name){return IsFileExist(name);}
    virtual ~CMappedResource();
    //
    virtual BOOL OpenResource(LPCSTR pszFileName, BOOL bWriteAccess = FALSE);
    virtual BOOL OpenResourceMem(LPVOID pBaseAddress);
    virtual void CloseResource();
    virtual void Flush() {}
    //
    virtual LPBYTE GetFile(LPCSTR pszFileName, LPDWORD pnDataSize = NULL);
    virtual bool IsFileExist(LPCSTR pszFileName);
    virtual bool CloseFile(LPBYTE pFileImage);
    virtual void CloseAllFiles();
};

class CWin32MappedResource : public CMappedResource
{
private:
    CMappedFile cMappedFile; // FileMapping object for file
    LPBYTE pBaseAddress;     // base address
public:
    CWin32MappedResource();
    virtual ~CWin32MappedResource();

    // ќткрывает ресурсный файл, дл€ информации по pAccelTable см. CResourceBase.h
    virtual BOOL OpenResource(LPCSTR pszFileName, BOOL bWriteAccess = FALSE);
    virtual BOOL OpenResourceMem(LPVOID pBaseAddress);
    virtual void CloseResource();
    virtual void Flush(); // flush written data if opened with write access

    // »щет требуемый ресурс и возвращает его координаты
    // NULL если не найден, если размер ресурса не требуетс€,
    // можно оставить равным NULL
    virtual LPBYTE GetFile(LPCSTR pszFileName, LPDWORD pnDataSize = NULL);
    virtual bool CloseFile(LPBYTE pFileImage);
    virtual void CloseAllFiles();
};
