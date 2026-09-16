#pragma once
// ChunkFile.h: interface for the CChunkFil class.
//
///////////////////////////////////////////////////////////////////////////////////////////////
#include "Plot.h"
#include "Quaternion.h"
#include "sArray.h"
#include "HashMap.h"
///////////////////////////////////////////////////////////////////////////////////////////////
class CChunkTag
{
public:
    CChunkTag(CChunkTag *pParentChunk = NULL);
    ~CChunkTag() = default;

    float ReadAsFloat();
    CString ReadAsString();
    CString ReadAsCodedString();
    CQuaternion ReadAsQuaternion();
    CPlot3D ReadAsPlot();
    BYTE ReadAsByte();
    DWORD ReadAsDWord();
    WORD ReadAsWord();
    CRect ReadAsRect();
    void ReadAsStringArray(CStringArray &data);
    void ReadAsBinary(void *data, int lenght);

    void WriteBinary(DWORD dwID, void *data, int lenght);
    void WriteStringArray(DWORD dwID, CStringArray &data);
    void WriteString(DWORD dwID, CString data);
    void WriteCodedString(DWORD dwID, CString data);
    void WriteFloat(DWORD dwID, float data);
    void WriteDWord(DWORD dwID, DWORD data);
    void WriteWord(DWORD dwID, WORD data);
    void WriteByte(DWORD dwID, BYTE data);
    void WritePlot(DWORD dwID, CPlot3D data);
    void WriteQuaternion(DWORD dwID, CQuaternion data);
    void WriteRect(DWORD dwID, CRect data);

    template <class T, class T1>
    void ReadAsArray(CAArray<T, T1> &data)
    {
        DWORD dwSize = ReadAsDWord();
        data.SetSize(dwSize);
        for (int i = 0; i < dwSize; i++)
        {
            GetFile()->Read(&data[i], sizeof(T));
            AddLenght(sizeof(T));
        }
    }

    template <class T, class T1>
    void WriteArray(DWORD dwID, CAArray<T, T1> &data)
    {
        DWORD dwSize = data.GetSize();
        WriteHeader(dwID, sizeof(T) * dwSize + sizeof(DWORD));
        GetFile()->Write(&dwSize, sizeof(DWORD));
        for (int i = 0; i < dwSize; i++)
            GetFile()->Write(&data[i], sizeof(T));
    }

    virtual void ReadSubChunk() {}
    virtual CFile *GetFile();

    DWORD GetID() { return m_dwID; }
    void ReadNextHeader();
    void BeginChunk(DWORD dwID);
    void EndChunk();
    int GetDataSize() { return m_dwDataLenght - sizeof(DWORD) * 2; }
    bool IsAllData()
    {
        VERIFY(m_dwLenght <= m_dwDataLenght);
        return (m_dwDataLenght == m_dwLenght);
    }
    bool SkipLostData();
    void SetParentChunk(CChunkTag *pParentChunk) { m_pParentChunk = pParentChunk; }
    void ReadAllSubChunks();

protected:
    void WriteHeader(DWORD dwID, DWORD dwSize);
    void AddLenght(DWORD dwLeght);

    DWORD m_dwID;
    DWORD m_dwLenght;
    DWORD m_dwDataLenght;
    DWORD m_dwHeadPos;
    CChunkTag *m_pParentChunk;
};
///////////////////////////////////////////////////////////////////////////////////////////////
typedef CHashMap<DWORD, CChunkTag *> CChunkTagsMap;
///////////////////////////////////////////////////////////////////////////////////////////////
class CChunkFile : public CChunkTag
{
public:
    CChunkFile(CString szName);
    CChunkFile() = default;
    ~CChunkFile() = default;
    BOOL Open(CString szName, WORD falgs);
    void Close() { m_fFile.Close(); };
    CFile *GetFile() { return &m_fFile; }

protected:
    CFile m_fFile;
};
///////////////////////////////////////////////////////////////////////////////////////////////
