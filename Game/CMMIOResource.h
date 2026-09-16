#pragma once
/*--------------------------------------------------------------------------------------------
CMMIOResource - class to access resources from resource file using
Win32 multimedia io family functions
Can handle only one file at same time.

Written by Ivanov E.N.
Nival Entertainment, 1998
--------------------------------------------------------------------------------------------*/

#define TRY_OPEN_EXTERNAL_RESOURCES_FOR_MMIO

#include "CResourceBase.h"
#include <mmsystem.h>

class CMMIOResource : public CResourceBase
{
public:
    CMMIOResource();
    virtual ~CMMIOResource();

    // Открывает ресурсный файл, для информации по pAccelTable см. CResourceBase.h
    BOOL OpenResource(LPCSTR pszFileName, SResToID *pAccelTable = NULL, int nAccelTableSize = 0);
    void CloseResource();

    // Открывает требуемый ресурс и ставит позицию для чтения на начало файла
    BOOL OpenFile(LPCSTR pszFileName, LPDWORD pnDataSize = NULL);
    BOOL OpenFile(int nID, LPDWORD pnDataSize = NULL);
#ifdef TRY_OPEN_EXTERNAL_RESOURCES_FOR_MMIO
    void CloseFile();
#else
    void CloseFile() { dwFileOffset = 0; }
#endif

    // Возвращает число прочитанных байт и -1 при ошибке
    int Read(LPVOID pData, int nSize);
    // Возвращает новую относитлеьную позицию и -1 при ошибке
    // nOrigin - CFile::begin, CFile::end, CFile::current
    int Seek(int nOffset, int nOrigin = CFile::begin);

private:
    HMMIO hMmio;
    DWORD dwFileOffset; // Смещение на текущий открытый файл, =0 если файл не открыт
    DWORD dwFileSize;   // Размер файла
#ifdef TRY_OPEN_EXTERNAL_RESOURCES_FOR_MMIO
    HMMIO hExtMmio;
#endif
};
