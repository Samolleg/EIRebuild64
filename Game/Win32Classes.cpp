#include "StdAfx.h"
#include "ModRuntime.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include <string.h>
#include <ctype.h>
#include "Notification.h"
#include "Win32Classes.h"
#include "OwnedRender/ResourceMapping.h"

//----------------------- Mapped file class -------------------------------------------------

CMappedFile::CMappedFile()
{
    hFile = INVALID_HANDLE_VALUE;
    hMapping = NULL;
    pView = NULL;
    dwFileSize = 0;
}

CMappedFile::~CMappedFile()
{
    Close();
}

void CMappedFile::Close()
{
    if (NULL != pView)
    {
        OwnedRender::UnmapResourceView(pView);
        pView = NULL;
    }
    if (NULL != hMapping)
    {
        CloseHandle(hMapping);
        hMapping = NULL;
    }
    if (INVALID_HANDLE_VALUE != hFile)
    {
        CloseHandle(hFile);
        hFile = INVALID_HANDLE_VALUE;
    }
    dwFileSize = 0;
}

BOOL CMappedFile::Open(LPCSTR szFileName, BOOL bWriteAccess)
{
    std::string resolved=bWriteAccess?szFileName:ModRuntime::MapPath(szFileName);szFileName=resolved.c_str();
    if (bWriteAccess)
        ReportWarning("CMappedFile::Open().\nMapping file \"%s\" with writting access.\n",
                      szFileName);
    DWORD dwFlags = FILE_ATTRIBUTE_NORMAL;
    // Determine can we use FILE_FLAG_NO_BUFFERING
    DWORD d1, dwBytesPerSector = 0xFFFFFFFF, d2, d3;
    char szNameBuf[4];
    strncpy(szNameBuf, szFileName, 3);
    szNameBuf[3] = 0;
    char *pRootDir = szNameBuf;
    if (!isalpha(szNameBuf[0]) || szNameBuf[1] != ':' || szNameBuf[2] != '\\')
        pRootDir = NULL;
    GetDiskFreeSpace(pRootDir, &d1, &dwBytesPerSector, &d2, &d3);
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    if (sysInfo.dwPageSize >= dwBytesPerSector && (sysInfo.dwPageSize % dwBytesPerSector == 0))
        dwFlags |= FILE_FLAG_NO_BUFFERING;
    else
        dwFlags |= FILE_FLAG_RANDOM_ACCESS;
    // Open file
    if (bWriteAccess)
        hFile = CreateFile(szFileName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, dwFlags, NULL);
    else
        hFile = CreateFile(szFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, dwFlags,
                           NULL);
    if (INVALID_HANDLE_VALUE == hFile)
    {
        DWORD dwError = GetLastError();
        char buf[256];
        sprintf(buf, "CMappedFile::Open().\nUnable to open file \"%s\".\n", szFileName);
        ReportWarning(buf);
        return FALSE;
    }
    // Query file size
    dwFileSize = GetFileSize(hFile, NULL);
    if (0xFFFFFFFF == dwFileSize)
    {
        ReportError("CMappedFile::Open().\nUnable to determine file size.\n");
        Close();
        return FALSE;
    }
    if (dwFileSize > 1024u * 1024u * 1024u * 2u)
    {
        ReportError("CMappedFile::Open().\nToo large file to map.\n");
        Close();
        return FALSE;
    }
    // Create file mapping
    if (bWriteAccess)
        hMapping = CreateFileMapping(hFile, NULL, PAGE_READWRITE, 0, 0, NULL);
    else
        hMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (NULL == hMapping)
    {
        ReportError("CMappedFile::Open().\nUnable to create file mapping.\n");
        Close();
        return FALSE;
    }
    // Create view of file
    if (bWriteAccess)
        pView = OwnedRender::MapResourceView(hMapping, FILE_MAP_WRITE, dwFileSize, hFile);
    else
        pView = OwnedRender::MapResourceView(hMapping, FILE_MAP_READ, dwFileSize, hFile);
    if (NULL == pView)
    {
        DWORD dwErrorCode = GetLastError();
        ReportError("CMappedFile::Open( \"%s\" ).\nUnable to create view of file (error=0x%X).\n",
                    szFileName, dwErrorCode);
        Close();
        return FALSE;
    }
    return TRUE;
}

void CMappedFile::Flush()
{
    if (pView)
    {
        FlushViewOfFile(pView, 0);
        BY_HANDLE_FILE_INFORMATION info;
        SYSTEMTIME time;
        GetSystemTime(&time);
        GetFileInformationByHandle(hFile, &info);
        SystemTimeToFileTime(&time, &info.ftLastWriteTime);
        SetFileTime(hFile, 0, 0, &info.ftLastWriteTime);
    }
}
