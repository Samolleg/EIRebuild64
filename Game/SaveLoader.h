#pragma once
#include "ModRuntime.h"
#include <limits>
// The error owns its identity: the source stream can be destroyed during unwind.
struct CSaveReadError
{
    const char *reason;
    unsigned int offset;
    std::string stream, at;
    void *stack[16] = {};
    USHORT frames;
    CSaveReadError(const char *why, unsigned int position,
                   const std::string &source = {}, const std::string &reader = {})
        : reason(why), offset(position), stream(source), at(reader),
          frames(CaptureStackBackTrace(1, 16, stack, nullptr)) {}
};
/**********************************************************************
 *<
        FILE: SaveLoader.H

        DESCRIPTION:  universal class for storing information into save files

        CREATED BY: Andy Gulin

 *>	Copyright (c) 1999, Nival Enterntainemnt
 **********************************************************************/

/////////////////////////////////////////////////////////////////////////////////////
#include <AfxTempl.h>
/////////////////////////////////////////////////////////////////////////////////////
// generic class for reading/ writing operations
class CMemorySave;
class CSave
{
protected:
    bool bReading;
    bool bIsOk;
    bool bStrictRead = false;
    std::string readStream, readStage;
    int nVersion; // версия save/load файла
public:
    CSave()
    {
        nVersion = 2000000000;
        bIsOk = true;
    }
    virtual ~CSave() {}
    void StartLoading() { bReading = true; }
    void StartSaving() { bReading = false; }
    virtual UINT VRead(void *lpBuf, UINT nCount) = 0;
    virtual void VWrite(const void *lpBuf, UINT nCount) = 0;
    virtual void Seek(unsigned int nPos) = 0;
    virtual unsigned int GetSize() = 0;
    virtual unsigned int GetPosition() = 0;
    virtual void Clear() = 0;
    bool Eof() { return GetPosition() >= GetSize(); }

    bool IsOk() { return bIsOk; }
    void SetOk() { bIsOk = true; }

    void SetVersion(int nVers) { nVersion = nVers; }
    int GetVersion() { return nVersion; }

    // Only prepared save payloads and their serialized children inherit this.
    // Unrelated resources/packets on the same thread keep their VRead contract.
    void SetStrictRead(bool strict, const std::string &stream = {}, const std::string &stage = {})
    { bStrictRead = strict; readStream = stream; readStage = stage; }
    bool IsStrictRead() const { return bStrictRead; }
    void InheritReadPolicy(const CSave &source)
    { SetStrictRead(source.bStrictRead, source.readStream, source.readStage); }
    void SetReadStage(const char *stage) { readStage = stage; }
    void Fail(const char *reason)
    {
        bIsOk = false;
        if (bStrictRead) throw CSaveReadError{reason, GetPosition(), readStream, readStage};
    }
    unsigned int Remaining()
    {
        const unsigned int position = GetPosition(), size = GetSize();
        if (position > size) { Fail("invalid_position"); return 0; }
        return size - position;
    }
    bool Require(unsigned int count, const char *reason)
    {
        if (count > Remaining()) { Fail(reason); return false; }
        return true;
    }
    UINT Read(void *lpBuf, UINT nCount)
    {
        if (bStrictRead && (!bIsOk || !Require(nCount, "truncated_read")))
            Fail("failed_stream");
        UINT read = VRead(lpBuf, nCount);
        if (bStrictRead && read != nCount) Fail("short_read");
        return read;
    }
    void Read(long &res) { Read(&res, sizeof(res)); }
    void Read(unsigned long &res) { Read(&res, sizeof(res)); }
    void Read(int &res) { Read(&res, sizeof(res)); }
    void Read(unsigned int &res) { Read(&res, sizeof(res)); }
    void Read(short &res) { Read(&res, sizeof(res)); }
    void Read(unsigned short &res) { Read(&res, sizeof(res)); }
    void Read(char &res) { Read(&res, sizeof(res)); }
    void Read(signed char &res) { Read(&res, sizeof(res)); }
    void Read(unsigned char &res) { Read(&res, sizeof(res)); }
    void Read(bool &res) { Read(&res, sizeof(res)); }
    void Read(float &res) { Read(&res, sizeof(res)); }
    void Read(double &res) { Read(&res, sizeof(res)); }
    void Read(CString &res) { res = ReadString(); }
    void Read(CSave &res);
    void Write(const void *lpBuf, UINT nCount) { VWrite(lpBuf, nCount); }
    void Write(long res) { Write(&res, sizeof(res)); }
    void Write(unsigned long res) { Write(&res, sizeof(res)); }
    void Write(int res) { Write(&res, sizeof(res)); }
    void Write(unsigned int res) { Write(&res, sizeof(res)); }
    void Write(short res) { Write(&res, sizeof(res)); }
    void Write(unsigned short res) { Write(&res, sizeof(res)); }
    void Write(char res) { Write(&res, sizeof(res)); }
    void Write(signed char res) { Write(&res, sizeof(res)); }
    void Write(unsigned char res) { Write(&res, sizeof(res)); }
    void Write(bool res) { Write(&res, sizeof(res)); }
    void Write(float res) { Write(&res, sizeof(res)); }
    void Write(double res) { Write(&res, sizeof(res)); }
    void Write(const CString &res) { WriteString((const char *)res); }
    void Write(CSave &res);
    /*	CSave& operator>>( long &res ) { Read( res ); return *this; }
            CSave& operator>>( unsigned long &res ) { Read( res ); return *this; }
            CSave& operator>>( int &res ) { Read( res ); return *this; }
            CSave& operator>>( unsigned int &res ) { Read( res ); return *this; }
            CSave& operator>>( short &res ) { Read( res ); return *this; }
            CSave& operator>>( unsigned short &res ) { Read( res ); return *this; }
            CSave& operator>>( char &res ) { Read( res ); return *this; }
            CSave& operator>>( signed char &res ) { Read( res ); return *this; }
            CSave& operator>>( unsigned char &res ) { Read( res ); return *this; }
            CSave& operator>>( float &res ) { Read( res ); return *this; }
            CSave& operator>>( double &res ) { Read( res ); return *this; }
            CSave& operator<<( long res ) { Write( res ); return *this; }
            CSave& operator<<( unsigned long res ) { Write( res ); return *this; }
            CSave& operator<<( int res ) { Write( res ); return *this; }
            CSave& operator<<( unsigned int res ) { Write( res ); return *this; }
            CSave& operator<<( short res ) { Write( res ); return *this; }
            CSave& operator<<( unsigned short res ) { Write( res ); return *this; }
            CSave& operator<<( char res ) { Write( res ); return *this; }
            CSave& operator<<( signed char res ) { Write( res ); return *this; }
            CSave& operator<<( unsigned char res ) { Write( res ); return *this; }
            CSave& operator<<( float res ) { Write( res ); return *this; }
            CSave& operator<<( double res ) { Write( res ); return *this; }*/
    CString ReadString()
    {
        CString a;
        char ch = 1;
        while (Read(&ch, 1))
        {
            if (ch == 0)
                break;
            a = a + ch;
        }
        if (bStrictRead && ch != 0) Fail("unterminated_string");
        return a;
    }
    void ReadString(char *pDst, int nMaxSize);
    void WriteString(const char *psz) { Write(psz, strlen(psz) + 1); }

    bool ReadChunk(DWORD &dwID, CMemorySave &chunk);
    bool WriteChunk(DWORD dwID, CMemorySave &chunk);
    // заменить chunk с id == dwID на &chunk, если такого чанка в файле нет, то добавить его в конец
    // результат операции заносится в result
    void ReplaceChunk(DWORD dwID, CMemorySave &chunk, CSave &result);
    // считать чанк с id == dwID в chunk
    bool GetChunk(DWORD dwID, CMemorySave &chunk);
    virtual void CopyFrom(CSave &file);

    void StoreDifference(CSave &original, CSave &subj);
    void LoadFromDifference(CSave &original, CSave &dif);

    bool LoadSaveHeader(const int nSignature, const int nCurrentVersion, const int nMinimalVersion);
    void StoreSaveHeader(const int nSignature, const int nCurrentVersion);
};
/////////////////////////////////////////////////////////////////////////////////////
// CSave using file
class CFileSave : public CSave
{
    CFile file;

public:
    CFileSave() {}
    ~CFileSave() {}
    CFileSave(const CFileSave &a) { ASSERT(0 && "NO COPIES :)"); }
    CFileSave &operator=(const CFileSave &a)
    {
        ASSERT(0 && "NO COPIES :)");
        return *this;
    }
    bool OpenRead(const char *pszFName)
    {
        bReading = true;
        std::string resolved=ModRuntime::MapPath(pszFName);
        bIsOk = file.Open(resolved.c_str(), CFile::modeRead) != 0;
        return bIsOk;
    }
    bool OpenWrite(const char *pszFName)
    {
        bReading = false;
        bIsOk = file.Open(pszFName, CFile::modeCreate | CFile::modeWrite) != 0;
        return bIsOk;
    }
    bool Open(const char *pszFName)
    {
        bReading = true;
        bIsOk = file.Open(pszFName, CFile::modeReadWrite) != 0;
        return bIsOk;
    }
    void CloseFile() { file.Close(); }

    void Clear()
    {
        bIsOk = true;
        file.Seek(0, CFile::begin);
        ASSERT(0 && "Don`t know how to truncate file");
    }
    UINT VRead(void *lpBuf, UINT nCount)
    {
        ASSERT(bReading);
        UINT rv = file.Read(lpBuf, nCount);
        bIsOk &= (rv == nCount);
        return rv;
    }
    void VWrite(const void *lpBuf, UINT nCount)
    {
        ASSERT(!bReading);
        file.Write(lpBuf, nCount);
    }
    void Seek(unsigned int nPos)
    {
        if (bStrictRead && bReading && nPos > GetSize()) { Fail("seek_out_of_bounds"); return; }
        file.Seek(nPos, CFile::begin);
    }
    unsigned int GetSize() { return file.GetLength(); }
    unsigned int GetPosition() { return file.GetPosition(); }
};
/////////////////////////////////////////////////////////////////////////////////////
// file in memory, after writing bits make sure to make flushbits()||seek()
class CMemorySave : public CSave
{
    char cBits;
    int nBits, nBitsWritePos;
    char *pszData;
    unsigned int nBufSize, nPos, nFileSize;

    void Init()
    {
        pszData = NULL;
        nBufSize = 0;
        nPos = 0;
        nFileSize = 0;
        bIsOk = true;
        SetStrictRead(false);
        nBits = 0x80;
        nBitsWritePos = -1;
    }
    void AllocNewBitsByte();

public:
    CMemorySave()
    {
        pszData = NULL;
        Init();
    }
    ~CMemorySave()
    {
        if (pszData)
            delete[] pszData;
    }
    CMemorySave(const CMemorySave &a)
    {
        pszData = 0;
        Init();
        SetSizeFast(a.nFileSize);
        Read(a.pszData, nFileSize);
    }
    CMemorySave &operator=(const CMemorySave &a)
    {
        SetSizeFast(a.nFileSize);
        Read(a.pszData, nFileSize);
        return *this;
    }

    void Clear()
    {
        if (pszData)
            delete[] pszData;
        Init();
    }
    UINT VRead(void *lpBuf, UINT nCount);
    void VWrite(const void *lpBuf, UINT nCount);
    void Seek(unsigned int nNPos);
    unsigned int GetSize() { return nFileSize; }
    unsigned int GetPosition() { return nPos; }
    const char *GetBuffer() { return pszData; }
    char *GetBufferForWrite() { return pszData; }
    void SetSize(UINT nLeng);
    void SetSizeFast(UINT nLeng); // may corrupt data in file
    virtual void CopyFrom(CSave &file);
    void WriteBit(bool bBit)
    {
        bBit = bBit != 0;
        nBits <<= 1;
        if (nBits > 0x80)
            AllocNewBitsByte();
        if (bBit)
            cBits |= nBits;
    }
    bool ReadBit()
    {
        nBits <<= 1;
        if (nBits > 0x80)
        {
            nBits = 1;
            Read(&cBits, 1);
        }
        return (cBits & nBits) != 0;
    }
    void FlushBits();
};
/////////////////////////////////////////////////////////////////////////////////////
// some template load/storing functions
template <class VAR>
void LoadObject(VAR &a, CSave &file)
{
    a.Load(file);
}

template <class VAR>
void StoreObject(VAR &a, CSave &file)
{
    a.Store(file);
}

template <>
inline void LoadObject<float>(float &a, CSave &file)
{
    file.Read(&a, sizeof(a));
}

template <>
inline void StoreObject<float>(float &a, CSave &file)
{
    file.Write(&a, sizeof(a));
}

template <>
inline void LoadObject<int>(int &a, CSave &file)
{
    file.Read(&a, sizeof(a));
}

template <>
inline void StoreObject<int>(int &a, CSave &file)
{
    file.Write(&a, sizeof(a));
}

template <>
inline void LoadObject<unsigned long>(unsigned long &a, CSave &file)
{
    file.Read(&a, sizeof(a));
}

template <>
inline void StoreObject<unsigned long>(unsigned long &a, CSave &file)
{
    file.Write(&a, sizeof(a));
}

template <>
inline void LoadObject<CString>(CString &a, CSave &file)
{
    a = file.ReadString();
}

template <>
inline void StoreObject<CString>(CString &a, CSave &file)
{
    file.Write((const char *)a, a.GetLength() + 1);
}

/////////////////////////////////////////////////////////////////////////////////////
// loading & storing storage classes
/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
void StoreCArray(CArray<TYPE, ARG_TYPE> &arr, CSave &file)
{
    int i = arr.GetSize();
    file.Write(&i, sizeof(i));
    for (i = 0; i < arr.GetSize(); i++)
        StoreObject<TYPE>(arr[i], file);
}

/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
void LoadCArray(CArray<TYPE, ARG_TYPE> &arr, CSave &file)
{
    int i;
    file.Read(&i, sizeof(i));
    if (i < 0 || static_cast<unsigned int>(i) > file.Remaining())
    { file.Fail("invalid_array_count"); return; }
    arr.SetSize(i);
    for (i = 0; i < arr.GetSize(); i++)
        LoadObject<TYPE>(arr[i], file);
}

/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
void StoreCList(CList<TYPE, ARG_TYPE> &arr, CSave &file)
{
    int i = arr.GetCount();
    file.Write(&i, sizeof(i));
    POSITION pos = arr.GetHeadPosition();
    while (pos)
        StoreObject<TYPE>(arr.GetNext(pos), file);
}

/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE>
void LoadCList(CList<TYPE, ARG_TYPE> &arr, CSave &file)
{
    int i, num;
    file.Read(&num, sizeof(num));
    if (num < 0 || static_cast<unsigned int>(num) > file.Remaining())
    { file.Fail("invalid_list_count"); return; }
    for (i = 0; i < num; i++)
        arr.AddTail(TYPE());
    POSITION pos = arr.GetHeadPosition();
    while (pos)
        LoadObject<TYPE>(arr.GetNext(pos), file);
}

/////////////////////////////////////////////////////////////////////////////////////
inline void LoadCStringArray(CStringArray &arr, CSave &file)
{
    int i, n;
    file.Read(n);
    if (n < 0 || static_cast<unsigned int>(n) > file.Remaining())
    { file.Fail("invalid_string_count"); return; }
    for (i = 0; i < n; i++)
        arr.Add(file.ReadString());
}

/////////////////////////////////////////////////////////////////////////////////////
inline void StoreCStringArray(CStringArray &arr, CSave &file)
{
    int i;
    file.Write((int)arr.GetSize());
    for (i = 0; i < arr.GetSize(); i++)
        file.WriteString(arr[i]);
}
/////////////////////////////////////////////////////////////////////////////////////
