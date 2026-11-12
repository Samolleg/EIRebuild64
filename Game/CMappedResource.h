#pragma once
/*--------------------------------------------------------------------------------------------
CMappedResource - class to access resources from resource file using
Win32 file mapping

Written by Ivanov E.N.
Nival Entertainment, 1998
--------------------------------------------------------------------------------------------*/
#include "Win32Classes.h"
#include "CResourceBase.h"
#include "../Pak/Pak.h"
#include <string>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <memory>
struct SResourceFileRecord;

class CMappedResource : public CResourceBase
{
private:
    std::unique_ptr<ei::pak::Archive> pak;
    bool looseDirectory = false;
    int hFile;           // handle to resource file
    LPBYTE pBaseAddress; // NULL if opened from file
    BOOL bWriteAccess;
    std::unordered_map<LPBYTE, SResourceFileRecord *> recByData;
    std::unordered_map<std::string, SResourceFileRecord *> recByName;

public:
    explicit CMappedResource(bool raw=false);
    static std::uint64_t LivePayloadBytes() noexcept;
    std::string sourcePath;
    bool rawResource=false,ignoreOriginal=false;
    std::vector<std::unique_ptr<CMappedResource>> overlays;
    void OpenOverlays(LPCSTR,bool,bool mapped=false);
    LPBYTE OverlayFile(LPCSTR,LPDWORD);
    virtual LPBYTE GetFileExact(LPCSTR, LPDWORD);
    bool IsFileExistExact(LPCSTR);
    bool IsDeleted(LPCSTR) const;
    virtual void GetFileList(CStringArray&,LPCSTR mask=NULL);
    virtual BOOL CheckFileExist(LPCSTR name){return IsFileExist(name);}
    virtual ~CMappedResource();
    //
    virtual BOOL OpenResource(LPCSTR pszFileName, BOOL bWriteAccess = FALSE);
    virtual BOOL OpenResourceMem(LPVOID pBaseAddress, std::size_t bytes);
    virtual void CloseResource();
    virtual void Flush() {}
    //
    std::string FileProvider(LPCSTR name);
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
    explicit CWin32MappedResource(bool raw=false);
    virtual ~CWin32MappedResource();

    // ќткрывает ресурсный файл, дл€ информации по pAccelTable см. CResourceBase.h
    virtual BOOL OpenResource(LPCSTR pszFileName, BOOL bWriteAccess = FALSE);
    virtual BOOL OpenResourceMem(LPVOID pBaseAddress, std::size_t bytes);
    virtual void CloseResource();
    virtual void Flush(); // flush written data if opened with write access

    // »щет требуемый ресурс и возвращает его координаты
    // NULL если не найден, если размер ресурса не требуетс€,
    // можно оставить равным NULL
    virtual LPBYTE GetFile(LPCSTR pszFileName, LPDWORD pnDataSize = NULL);
    LPBYTE GetFileExact(LPCSTR, LPDWORD) override;
    virtual bool CloseFile(LPBYTE pFileImage);
    virtual void CloseAllFiles();
};
