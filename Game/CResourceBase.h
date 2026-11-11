#pragma once
/*--------------------------------------------------------------------------------------------
CResourceBase - base class for all resource classes.
Written by Ivanov E.N.
Nival Entertainment, 1998
--------------------------------------------------------------------------------------------*/
#include <assert.h>
#include <cstdint>
#include <cstddef>
// Структура таблицы для ускорения поиска файлов в ресурсе
// nResourceID используется как индекс в массиве, поэтому желательно
// чтобы они начинались с 0 и шли по очереди
struct SResToID
{
    LPCSTR szResourceName;
    int nResourceID;
};
// Макрос для вычисления количества элементов в таблице
#define ACCEL_TABLE_SIZE(a) (sizeof(a) / sizeof(SResToID))

//--------------------------------------------------------------------------------------------
#include "resFormat.h"
class CResourceBase
{
public:
    CResourceBase();
    virtual ~CResourceBase();

    virtual void GetFileList(CStringArray &aNames, LPCSTR pszMask = NULL); // список всех файлов в ресурсе
    virtual BOOL CheckFileExist(LPCSTR pszFileName) { return GetIndexOfName(pszFileName) != DWORD(-1); }

protected:
    SResHeader header;         // header of the resource file
    SResHashEntry *pHashTable; // hash table
    LPBYTE pNamePool;          // pool with names

    // returns index of hash table record or -1 if not found
    DWORD GetIndexOfName(LPCSTR pszName);
    bool SetResourceLayout(std::uint64_t bytes, bool mapped);
    bool BindResourceMemory(const void* address, std::size_t bytes);
    bool EntryAvailable(DWORD index) const;
    std::uint64_t resourceBytes = 0, namePoolBytes = 0;
    DWORD GetIndexOfID(int nResourceID);
#ifdef TRY_OPEN_EXTERNAL_RESOURCES
    LPCSTR FindNameByID(int nResourceID);
#endif

    BOOL CreateAccelTable(SResToID *pAccelTable, int nAccelNum);
    void FreeAccelTable();

private:
    LPDWORD pdwAccelTable; // таблица смещений
    int nAccelTableSize;   // размер таблицы смещений

#ifdef TRY_OPEN_EXTERNAL_RESOURCES
    SResToID *pAccelTable; // таблица соответсвий ID<->Name
    int nAccelNum;         // размер таблицы соответствий
#endif
};
