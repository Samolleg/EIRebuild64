#pragma once
/************************************************************************
 General class library for utilize Win32 functions
 Written by Ivanov E.N.
 Nival Entertainment
************************************************************************/

// Mapped file class. Uses Win32 File mapping to access file data.
// File data can be accessed only for read
class CMappedFile
{
public:
    CMappedFile();
    ~CMappedFile();

    BOOL Open(LPCSTR szFileName, BOOL bWriteAccess = FALSE); /* Opens file mapping */
    void Close();

    LPVOID GetAddress() /* Returns the starting address of the file or NULL */ { return pView; }
    DWORD GetSize() /* Returns the size of the mapped file */ { return dwFileSize; }

    void Flush(); /* Flushes writted data if opened with write access */

private:
    HANDLE hFile;
    HANDLE hMapping;
    LPVOID pView;
    DWORD dwFileSize;
};

// Critical section wrapper
class CCritSection
{
private:
    CRITICAL_SECTION m_critSection;

public:
    CCritSection() { InitializeCriticalSection(&m_critSection); }
    ~CCritSection() { DeleteCriticalSection(&m_critSection); }
    void Enter() { EnterCriticalSection(&m_critSection); }
    void Leave() { LeaveCriticalSection(&m_critSection); }
};

// Event wrapper
class CWinEvent
{
public:
    HANDLE hEvent;

public:
    CWinEvent() { hEvent = CreateEvent(0, FALSE, FALSE, 0); }
    ~CWinEvent() { CloseHandle(hEvent); }
    void Set() { SetEvent(hEvent); }
    void Wait(DWORD dwMilSec = INFINITE) { WaitForSingleObject(hEvent, dwMilSec); }
};
