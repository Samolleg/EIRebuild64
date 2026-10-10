#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "BasicChunk.h"

/////////////////////////////////////////////////////////////////////////////////////
// class factory
CClassFactory<CFundament> SSClasses;
/////////////////////////////////////////////////////////////////////////////////////
// chunks operations with whole saves
/////////////////////////////////////////////////////////////////////////////////////
static bool ReadShortChunkSave(CSave &file, chunk_id &dwID, CMemorySave &chunk)
{
    DWORD dwLeng = 0, dwRead;
    if (file.Eof()) return false;
    file.Read(&dwID, sizeof(dwID));
    file.Read(&dwLeng, 1);
    if (dwLeng & 1)
        file.Read(((char *)&dwLeng) + 1, 3);
    dwLeng >>= 1;
    if (!file.IsOk())
        return false;
    if (!file.Require(dwLeng, "truncated_short_chunk")) return false;
    chunk.SetSizeFast(dwLeng);
    dwRead = file.Read(chunk.GetBufferForWrite(), dwLeng);
    return dwRead == dwLeng;
}
/////////////////////////////////////////////////////////////////////////////////////
static bool WriteShortChunkSave(CSave &file, chunk_id dwID, CMemorySave &chunk)
{
    DWORD dwLeng;
    file.Write(&dwID, sizeof(dwID));
    dwLeng = chunk.GetSize();
    dwLeng <<= 1;
    if (dwLeng >= 256)
    {
        dwLeng |= 1;
        file.Write(&dwLeng, sizeof(dwLeng));
    }
    else
        file.Write(&dwLeng, 1);
    file.Write(chunk.GetBuffer(), chunk.GetSize());
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
static bool GetShortChunkSave(CSave &file, chunk_id dwID, CMemorySave &chunk, int nNumber = 1)
{
    chunk_id dwRid;
    file.SetOk();
    file.Seek(0);
    file.StartLoading();
    while (ReadShortChunkSave(file, dwRid, chunk))
    {
        if (dwRid == dwID)
        {
            if (nNumber == 1)
                return true;
            nNumber--;
        }
    }
    chunk.Clear();
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
// chunks operations with ChunkLevels
/////////////////////////////////////////////////////////////////////////////////////
static void ReadPtrData(const char *pData, void *pDst, int &nPos, int nSize)
{
    memcpy(pDst, pData + nPos, nSize);
    nPos += nSize;
}
// should copy data from start
static void WritePtrData(char *pDst, const void *pSrc, int &nPos, int nSize)
{
    memcpy(pDst + nPos, pSrc, nSize);
    nPos += nSize;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CStructureSaver::ReadShortChunk(CChunkLevel &src, int &nPos, CChunkLevel &res)
{
    DWORD dwLeng = 0;
    if (nPos == src.nLength) return false;
    if (src.nStart < 0 || src.nLength < 0 || nPos < 0 || nPos > src.nLength ||
        static_cast<unsigned int>(src.nStart) > data.GetSize() ||
        static_cast<unsigned int>(src.nLength) > data.GetSize() - src.nStart ||
        src.nLength - nPos < 2)
    { data.Fail("invalid_short_chunk_header"); return false; }
    const char *pSrc = data.GetBuffer() + src.nStart;
    ReadPtrData(pSrc, &res.idChunk, nPos, sizeof(res.idChunk));
    ReadPtrData(pSrc, &dwLeng, nPos, 1);
    if (dwLeng & 1)
    {
        if (src.nLength - nPos < 3) { data.Fail("truncated_short_chunk_length"); return false; }
        ReadPtrData(pSrc, ((char *)&dwLeng) + 1, nPos, 3);
    }
    dwLeng >>= 1;
    if (dwLeng > static_cast<unsigned int>(src.nLength - nPos))
    { data.Fail("invalid_short_chunk_length"); return false; }
    res.nStart = nPos + src.nStart;
    res.nLength = dwLeng;
    nPos += dwLeng;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CStructureSaver::WriteShortChunk(CChunkLevel &dst, chunk_id dwID, const char *pData,
                                      int nLength)
{
    DWORD dwLeng;
    data.SetSize(dst.nStart + dst.nLength + 1 + 4 + nLength);
    char *pDst = data.GetBufferForWrite() + dst.nStart;
    WritePtrData(pDst, &dwID, dst.nLength, sizeof(dwID));
    dwLeng = nLength;
    dwLeng <<= 1;
    if (dwLeng >= 256)
    {
        dwLeng |= 1;
        WritePtrData(pDst, &dwLeng, dst.nLength, sizeof(dwLeng));
    }
    else
        WritePtrData(pDst, &dwLeng, dst.nLength, 1);
    // prevent copying to itself
    if (pDst + dst.nLength != pData)
        WritePtrData(pDst, pData, dst.nLength, nLength);
    else
        dst.nLength += nLength;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CStructureSaver::GetShortChunk(CChunkLevel &src, chunk_id dwID, CChunkLevel &res, int nNumber)
{
    int nPos = 0;
    while (ReadShortChunk(src, nPos, res))
    {
        if (res.idChunk == dwID)
        {
            if (nNumber == 1)
                return true;
            nNumber--;
        }
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
int CStructureSaver::CountShortChunks(CChunkLevel &src, chunk_id dwID)
{
    int nPos = 0, nRes = 0;
    CChunkLevel temp;
    while (ReadShortChunk(src, nPos, temp))
    {
        if (temp.idChunk == dwID)
            nRes++;
    }
    return nRes;
}
/////////////////////////////////////////////////////////////////////////////////////
// CStructureSaver main methods
/////////////////////////////////////////////////////////////////////////////////////
void CStructureSaver::DataChunk(const chunk_id idChunk, void *pData, int nSize)
{
    CChunkLevel &last = chunks.back();
    if (IsReading())
    {
        CChunkLevel res;
        if (GetShortChunk(last, idChunk, res, last.nChunkNumber))
        {
            if (nSize < 0 || res.nLength != nSize)
            { data.Fail("invalid_field_length"); return; }
            memcpy(pData, data.GetBuffer() + res.nStart, nSize);
        }
        else
            memset(pData, 0, nSize);
    }
    else
        WriteShortChunk(last, idChunk, (const char *)pData, nSize);
}
/////////////////////////////////////////////////////////////////////////////////////
void CStructureSaver::DataChunkString(const chunk_id idChunk, CString &str)
{
    CChunkLevel &last = chunks.back();
    if (IsReading())
    {
        CChunkLevel res;
        if (GetShortChunk(last, idChunk, res, last.nChunkNumber))
        {
            const char *pStr = data.GetBuffer() + res.nStart;
            if (res.nLength < 1 || pStr[res.nLength - 1] != 0)
            { data.Fail("unterminated_chunk_string"); return; }
            str = pStr;
        }
        else
            str = "";
    }
    else
        WriteShortChunk(last, idChunk, (const char *)str, str.GetLength() + 1);
}
/////////////////////////////////////////////////////////////////////////////////////
void CStructureSaver::DataChunkString(const chunk_id idChunk, stdString &str)
{
    CChunkLevel &last = chunks.back();
    if (IsReading())
    {
        CChunkLevel res;
        if (GetShortChunk(last, idChunk, res, last.nChunkNumber))
        {
            const char *pStr = data.GetBuffer() + res.nStart;
            if (res.nLength < 1 || pStr[res.nLength - 1] != 0)
            { data.Fail("unterminated_chunk_string"); return; }
            str = pStr;
        }
        else
            str = "";
    }
    else
        WriteShortChunk(last, idChunk, str.c_str(), strlen(str.c_str()) + 1);
}
/////////////////////////////////////////////////////////////////////////////////////
void CStructureSaver::DataChunkBLOB(const chunk_id idChunk, CMemorySave &file)
{
    if (!StartChunk(idChunk))
        return;
    int nLeng = file.GetSize();
    Add(1, nLeng);
    if (IsReading())
    {
        if (nLeng < 0 || nLeng > chunks.back().nLength)
        { data.Fail("invalid_blob_length"); return; }
        file.Clear();
        file.SetSizeFast(nLeng);
    }
    DataChunk(2, file.GetBufferForWrite(), nLeng);
    file.Seek(0);
    FinishChunk();
}
/////////////////////////////////////////////////////////////////////////////////////
bool CStructureSaver::StartChunk(const chunk_id idChunk)
{
    CChunkLevel &last = chunks.back();
    if (IsReading())
    {
        chunks.emplace_back();
        return GetShortChunk(last, idChunk, chunks.back(), last.nChunkNumber);
    }
    else
    {
        chunks.emplace_back();
        CChunkLevel &newChunk = chunks.back();
        newChunk.idChunk = idChunk;
        newChunk.nStart = last.nStart + last.nLength + sizeof(chunk_id) + 4;
        return true;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CStructureSaver::FinishChunk()
{
    if (IsReading())
    {
        chunks.pop_back();
    }
    else
    {
        CChunkLevelReverseIterator it = chunks.rbegin(), it1;
        it1 = it;
        ++it1;
        WriteShortChunk(*it1, it->idChunk, data.GetBuffer() + it->nStart, it->nLength);
        chunks.pop_back();
        AlignDataFileSize();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CStructureSaver::AlignDataFileSize()
{
    CChunkLevel &last = chunks.back();
    data.SetSize(last.nStart + last.nLength);
}
/////////////////////////////////////////////////////////////////////////////////////
int CStructureSaver::CountChunks(const chunk_id idChunk)
{
    return CountShortChunks(chunks.back(), idChunk);
}
/////////////////////////////////////////////////////////////////////////////////////
void CStructureSaver::Start(CSave &res, bool bRead, int _nVersion)
{
    chunks.clear();
    obj.Clear();
    data.Clear();
    chunks.emplace_back();
    bIsReading = bRead;
    if (bRead)
    {
        // read chunk with objects description
        GetShortChunkSave(res, 0, obj);
        GetShortChunkSave(res, 2, data);
        chunks.back().nLength = data.GetSize();
        // create all objects from obj
        if (obj.GetSize() % 8) obj.Fail("invalid_object_registry_length");
        while (obj.GetPosition() < obj.GetSize())
        {
            int nTypeID = 0;
            void *pServer = 0;
            obj.Read(&nTypeID, 4);
            obj.Read(&pServer, 4);
            if (!pServer || objects.find(pServer) != objects.end())
                obj.Fail("invalid_object_registry_id");
            CFundament *pObject = SSClasses.CreateObject(nTypeID);
            if (!pObject) obj.Fail("invalid_object_registry_type");
            toStore.push_back(pObject);
            objects[pServer] = pObject;
        }
        // CRAP
        if (_nVersion == 1)
            Add(2, nVersion);
        else
            nVersion = _nVersion;
        // end of CRAP
        // read information about every created object
        int nCount = CountChunks((chunk_id)1);
        for (int i = 0; i < nCount; i++)
        {
            void *pServer = 0;
            CFundament *pObject;
            SetChunkCounter(i + 1);
            StartChunk((chunk_id)1);
            DataChunk(0, &pServer, 4);
            pObject = (CFundament *)objects[pServer];
            if (!pObject) data.Fail("unknown_object_reference");
            if (pObject)
                Add(1, *pObject);
            FinishChunk();
        }
        SetChunkCounter(1);

        // read main objects data
        chunks.back().Clear();
        GetShortChunkSave(res, 1, data);
        chunks.back().nLength = data.GetSize();
    }
    else
        nVersion = _nVersion;
}
/////////////////////////////////////////////////////////////////////////////////////
void CStructureSaver::Finish(CSave &res)
{
    // CRAP check if chunks list has only one layer
    if (!IsReading())
    {
        // save standard data
        AlignDataFileSize();
        WriteShortChunkSave(res, 1, data);
        // store referenced objects
        data.Clear();
        chunks.back().Clear();
        while (!toStore.empty())
        {
            CFundament *pObject = toStore.front();
            toStore.pop_front();
            // save object type and its server pointer
            int nTypeID = SSClasses.GetObjectTypeID(pObject);
            ASSERT(nTypeID != -1);
            obj.Write(&nTypeID, 4);
            obj.Write(&pObject, 4);
            // save object data
            StartChunk((chunk_id)1);
            DataChunk(0, &pObject, 4);
            Add(1, *pObject);
            FinishChunk();
        }
        Add(2, nVersion);
        // save data into resulting file
        WriteShortChunkSave(res, 0, obj);
        AlignDataFileSize();
        WriteShortChunkSave(res, 2, data);
    }
    else
    {
        // remove unreferenced objects
        for (auto i = objects.begin(); i != objects.end(); i++)
        {
            CPtr<CObjectBase> ref((CObjectBase *)i->second);
        }
    }
    obj.Clear();
    data.Clear();
    objects.clear();
    toStore.clear();
    chunks.clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CStructureSaver::StoreObject(const chunk_id idChunk, CFundament *pObject)
{
    if (objects.find(pObject) == objects.end())
    {
        toStore.push_back(pObject);
        objects[pObject] = this; // важно присвоить хоть что-нибудь
    }
    DataChunk(idChunk, &pObject, 4);
}
/////////////////////////////////////////////////////////////////////////////////////
CFundament *CStructureSaver::LoadObject(const chunk_id idChunk)
{
    void *pServerPtr = 0;
    DataChunk(idChunk, &pServerPtr, 4);
    auto pFound = objects.find(pServerPtr);
    if (pFound != objects.end())
        return (CFundament *)pFound->second;
    if (pServerPtr) data.Fail("unknown_object_reference");
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
