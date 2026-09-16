#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "CMMIOResource.h"
#include "Notification.h"
#ifdef TRY_OPEN_EXTERNAL_RESOURCES_FOR_MMIO
#include <io.h>
#endif
//--------------------------------------------------------------------------------------------
CMMIOResource::CMMIOResource()
{
    hMmio = NULL;
#ifdef TRY_OPEN_EXTERNAL_RESOURCES_FOR_MMIO
    hExtMmio = NULL;
#endif
    dwFileOffset = 0;
    dwFileSize = 0;
}
CMMIOResource::~CMMIOResource()
{
    CloseResource();
}
//--------------------------------------------------------------------------------------------
BOOL CMMIOResource::OpenResource(LPCSTR pszFileName, SResToID *pAccelTable, int nAccelTableSize)
{
    hMmio = mmioOpen((LPSTR)pszFileName, NULL, MMIO_DENYWRITE | MMIO_READ);
    if (!hMmio)
    {
        ReportError("CMMIOResource::OpenResource().\nUnable to open resource file.\n");
        return FALSE;
    }
    if (mmioRead(hMmio, (char *)&header, sizeof(header)) != sizeof(header))
    {
        ReportError("CMMIOResource::OpenResource().\nUnable to read header of resource file.\n");
        CloseResource();
        return FALSE;
    }
    if (header.dwSignature != SRESHEADER_SIGNATURE)
    {
        ReportError("CMappedResource::OpenResource().\nWrong format resource file.\n");
        CloseResource();
        return FALSE;
    }
    int nHashSize = sizeof(SResHashEntry) * header.dwEntryCount;
    int nNamePoolSize = header.dwNameDataSize;
    pHashTable = dbgnew SResHashEntry[header.dwEntryCount];
    pNamePool = dbgnew BYTE[nNamePoolSize];
    if (!pHashTable || !pNamePool)
    {
        ReportError("CMappedResource::OpenResource().\nNo enough memory.\n");
        CloseResource();
        return FALSE;
    }
    if (mmioSeek(hMmio, header.dwHashTableOffset, SEEK_SET) == -1 ||
        mmioRead(hMmio, (char *)pHashTable, nHashSize) != nHashSize ||
        mmioRead(hMmio, (char *)pNamePool, nNamePoolSize) != nNamePoolSize)
    {
        ReportError("CMappedResource::OpenResource().\nUnable to read resource file.\n");
        CloseResource();
        return FALSE;
    }
    CreateAccelTable(pAccelTable, nAccelTableSize);
    return TRUE;
}

void CMMIOResource::CloseResource()
{
    CloseFile();
    if (pHashTable)
    {
        delete[] pHashTable;
        pHashTable = NULL;
    }
    if (pNamePool)
    {
        delete[] pNamePool;
        pNamePool = NULL;
    }
    memset(&header, 0, sizeof(header));
    if (hMmio)
    {
        mmioClose(hMmio, 0);
        hMmio = NULL;
    }
    FreeAccelTable();
#ifdef TRY_OPEN_EXTERNAL_RESOURCES_FOR_MMIO
    if (hExtMmio)
    {
        mmioClose(hExtMmio, 0);
        hExtMmio = NULL;
    }
#endif
    dwFileOffset = 0;
    dwFileSize = 0;
}

//--------------------------------------------------------------------------------------------
// Открывает требуемый ресурс и ставит позицию для чтения на начало файла
BOOL CMMIOResource::OpenFile(LPCSTR pszFileName, LPDWORD pnDataSize)
{
    CloseFile();
#ifdef TRY_OPEN_EXTERNAL_RESOURCES_FOR_MMIO
    if (_access(pszFileName, 04) == 0)
    {
        struct _finddata_t fileinfo;
        _findfirst(pszFileName, &fileinfo);
        hExtMmio = mmioOpen((LPSTR)pszFileName, NULL, MMIO_DENYWRITE | MMIO_READ);
        if (hExtMmio)
        {
            if (pnDataSize)
                *pnDataSize = fileinfo.size;
            dwFileOffset = 1; // Just flag that file opened
            return TRUE;
        }
    }
#endif

    if (!pHashTable || !pNamePool)
        return FALSE;

    DWORD dwIndex = GetIndexOfName(pszFileName);
    if (dwIndex == -1)
        return FALSE;
    dwFileOffset = pHashTable[dwIndex].dwOffset;
    if (mmioSeek(hMmio, dwFileOffset, SEEK_SET) == -1)
    {
        dwFileOffset = 0;
        return FALSE;
    }
    if (pnDataSize)
        *pnDataSize = pHashTable[dwIndex].dwSize;
    return TRUE;
}

BOOL CMMIOResource::OpenFile(int nID, LPDWORD pnDataSize)
{
    CloseFile();

    if (!pHashTable || !pNamePool)
        return FALSE;

    DWORD dwIndex = GetIndexOfID(nID);
    if (dwIndex == -1)
        return FALSE;
    dwFileOffset = pHashTable[dwIndex].dwOffset;
    if (mmioSeek(hMmio, dwFileOffset, SEEK_SET) == -1)
    {
        CloseFile();
        return FALSE;
    }
    dwFileSize = pHashTable[dwIndex].dwSize;
    if (pnDataSize)
        *pnDataSize = dwFileSize;
    return TRUE;
}

// Закрыть открытый файл
#ifdef TRY_OPEN_EXTERNAL_RESOURCES_FOR_MMIO
void CMMIOResource::CloseFile()
{
    if (dwFileOffset)
    {
        dwFileOffset = 0;
        dwFileSize = 0;
        if (hExtMmio)
        {
            mmioClose(hExtMmio, 0);
            hExtMmio = NULL;
        }
    }
}
#endif

//--------------------------------------------------------------------------------------------
// Возвращает число прочитанных байт и -1 при ошибке
int CMMIOResource::Read(LPVOID pData, int nSize)
{
    if (!dwFileOffset)
        return -1;
#ifdef TRY_OPEN_EXTERNAL_RESOURCES_FOR_MMIO
    if (hExtMmio)
        return mmioRead(hExtMmio, (char *)pData, nSize);
#endif
    return mmioRead(hMmio, (char *)pData, nSize);
}

// nOrigin - CFile::begin, CFile::end, CFile::current
int CMMIOResource::Seek(int nOffset, int _nOrigin)
{
    if (!dwFileOffset)
        return -1;
    int nOrigin;
#ifdef TRY_OPEN_EXTERNAL_RESOURCES_FOR_MMIO
    if (hExtMmio)
    {
        switch (_nOrigin)
        {
        case CFile::end:
            nOrigin = SEEK_END;
            break;
        case CFile::current:
            nOrigin = SEEK_CUR;
        default:
            nOrigin = SEEK_SET;
            break;
        }
        return mmioSeek(hExtMmio, nOffset, nOrigin);
    }
#endif
    switch (_nOrigin)
    {
    case CFile::end:
        nOrigin = SEEK_SET;
        nOffset = dwFileSize + dwFileOffset + nOffset;
        break;
    case CFile::current:
        nOrigin = SEEK_CUR;
        break;
    default:
        nOrigin = SEEK_SET;
        nOffset += dwFileOffset;
        break;
    }
    LONG nRv = mmioSeek(hMmio, nOffset, nOrigin);
    if (nRv == -1)
        return -1;
    return nRv - dwFileOffset;
}
