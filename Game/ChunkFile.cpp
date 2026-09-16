// ChunkFile.cpp: implementation of the CChunkFil class.
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "StdAfx.h"
#include "ModRuntime.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "ChunkFile.h"
#include "SimpleCryptographer.h"
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction/Destruction
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
CChunkTag::CChunkTag(CChunkTag *pParentChunk)
{
    m_dwLenght = 0;
    m_dwDataLenght = 0;
    m_dwID = 0;
    m_dwHeadPos = 0;
    m_pParentChunk = pParentChunk;
}

void CChunkTag::AddLenght(DWORD dwLenght)
{
    if (m_pParentChunk)
        m_pParentChunk->AddLenght(dwLenght);
    m_dwLenght += dwLenght;
}
CFile *CChunkTag::GetFile()
{
    if (m_pParentChunk)
        return m_pParentChunk->GetFile();
    return NULL;
}
void CChunkTag::BeginChunk(DWORD dwID)
{
    GetFile()->Write(&dwID, sizeof(DWORD));
    m_dwHeadPos = GetFile()->GetPosition();
    GetFile()->Write(&m_dwDataLenght, sizeof(DWORD));
    m_dwLenght = 0;
    AddLenght(sizeof(DWORD) * 2);
}
void CChunkTag::EndChunk()
{
    DWORD dwCurPos = GetFile()->GetPosition();
    GetFile()->Seek(m_dwHeadPos, CFile::begin);
    GetFile()->Write(&m_dwLenght, sizeof(DWORD));
    GetFile()->Seek(dwCurPos, CFile::begin);
}
void CChunkTag::WriteHeader(DWORD dwID, DWORD dwSize)
{
    dwSize += sizeof(DWORD) * 2;
    GetFile()->Write(&dwID, sizeof(DWORD));
    GetFile()->Write(&dwSize, sizeof(DWORD));
    AddLenght(dwSize);
}
bool CChunkTag::SkipLostData()
{
    if (sizeof(DWORD) * 2 == m_dwLenght && m_dwDataLenght != (sizeof(DWORD) * 2))
    {
        // Skip this "full" data
        GetFile()->Seek(m_dwDataLenght - sizeof(DWORD) * 2, CFile::current);
        AddLenght(m_dwDataLenght - sizeof(DWORD) * 2);
        return true;
    }
    ASSERT(IsAllData());
    return false;
}

void CChunkTag::ReadNextHeader()
{
    SkipLostData();
    VERIFY(IsAllData()); // Wrong mob file format
    GetFile()->Read(&m_dwID, sizeof(DWORD));
    GetFile()->Read(&m_dwDataLenght, sizeof(DWORD));
    m_dwLenght = 0;
    AddLenght(sizeof(DWORD) * 2);
}

void CChunkTag::ReadAllSubChunks()
{
    for (ReadNextHeader();; ReadNextHeader())
    {
        ReadSubChunk();
        if (SkipLostData())
        {
#if defined(_DEBUG) && defined(__MAP_EDITOR__)
            CString szText;
            szText.Format("Skiped unkown propites data with tag ID:%X", GetID());
            AfxMessageBox(szText);
#endif // _DEBUG
        }
        if (m_pParentChunk->IsAllData())
            break;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////
float CChunkTag::ReadAsFloat()
{
    float data;
    GetFile()->Read(&data, sizeof(float));
    AddLenght(sizeof(float));
    return data;
}
void CChunkTag::WriteFloat(DWORD dwID, float data)
{
    WriteHeader(dwID, sizeof(float));
    GetFile()->Write(&data, sizeof(float));
}
///////////////////////////////////////////////////////////////////////////////////////////////
DWORD CChunkTag::ReadAsDWord()
{
    DWORD data;
    GetFile()->Read(&data, sizeof(DWORD));
    AddLenght(sizeof(DWORD));
    return data;
}
void CChunkTag::WriteDWord(DWORD dwID, DWORD data)
{
    WriteHeader(dwID, sizeof(DWORD));
    GetFile()->Write(&data, sizeof(DWORD));
}
///////////////////////////////////////////////////////////////////////////////////////////////
WORD CChunkTag::ReadAsWord()
{
    WORD data;
    GetFile()->Read(&data, sizeof(WORD));
    AddLenght(sizeof(WORD));
    return data;
}
void CChunkTag::WriteWord(DWORD dwID, WORD data)
{
    WriteHeader(dwID, sizeof(WORD));
    GetFile()->Write(&data, sizeof(WORD));
}
///////////////////////////////////////////////////////////////////////////////////////////////
BYTE CChunkTag::ReadAsByte()
{
    BYTE data;
    GetFile()->Read(&data, sizeof(BYTE));
    AddLenght(sizeof(BYTE));
    return data;
}
void CChunkTag::WriteByte(DWORD dwID, BYTE data)
{
    WriteHeader(dwID, sizeof(BYTE));
    GetFile()->Write(&data, sizeof(BYTE));
}
///////////////////////////////////////////////////////////////////////////////////////////////
CString CChunkTag::ReadAsString()
{
    int nLength = GetDataSize();
    CString data;
    char *pString = data.GetBufferSetLength(nLength);
    for (int i = 0; i < nLength; i++)
        *pString++ = ReadAsByte();
    data.ReleaseBuffer();
    return data;
}
CString CChunkTag::ReadAsCodedString()
{
    int nLength = GetDataSize();
    CSimpleCryptographer cr;
    cr.SetCodetSize(nLength);
    ReadAsBinary(cr.GetCodetData(), nLength);
    CString data;
    cr.GetString(data);
    return data;
}
void CChunkTag::WriteString(DWORD dwID, CString data)
{
    WriteHeader(dwID, data.GetLength());
    GetFile()->Write(data.GetBuffer(0), data.GetLength());
}
void CChunkTag::WriteCodedString(DWORD dwID, CString data)
{
    CSimpleCryptographer cr;
    cr.SetString(data);
    WriteBinary(dwID, cr.GetCodetData(), cr.GetCodetSize());
}
///////////////////////////////////////////////////////////////////////////////////////////////
void CChunkTag::ReadAsStringArray(CStringArray &data)
{
    DWORD dwSize = ReadAsDWord();
    CChunkTag header(this);
    data.RemoveAll();
    for (DWORD i = 0; i < dwSize; i++)
    {
        header.ReadNextHeader();
        data.Add(header.ReadAsString());
    }
}
void CChunkTag::WriteStringArray(DWORD dwID, CStringArray &data)
{
    DWORD dwSize;
    DWORD dwTotalLenght = 0;
    for (int i = 0; i < data.GetSize(); i++)
        dwTotalLenght += data[i].GetLength();
    WriteHeader(dwID, dwTotalLenght + 8 * data.GetSize() + sizeof(DWORD));
    dwTotalLenght = data.GetSize();
    GetFile()->Write(&dwTotalLenght, sizeof(DWORD));
    for (int i = 0; i < data.GetSize(); i++)
    {
        dwSize = data[i].GetLength() + sizeof(DWORD) * 2;
        GetFile()->Write(&dwID, sizeof(DWORD));
        GetFile()->Write(&dwSize, sizeof(DWORD));
        GetFile()->Write(data[i].GetBuffer(0), data[i].GetLength());
    }
}
///////////////////////////////////////////////////////////////////////////////////////////////
CPlot3D CChunkTag::ReadAsPlot()
{
    float x, y, z;
    x = ReadAsFloat();
    y = ReadAsFloat();
    z = ReadAsFloat();
    return CPlot3D(x, y, z);
}
void CChunkTag::WritePlot(DWORD dwID, CPlot3D data)
{
    WriteHeader(dwID, sizeof(float) * 3);
    GetFile()->Write(&data.x, sizeof(float));
    GetFile()->Write(&data.y, sizeof(float));
    GetFile()->Write(&data.z, sizeof(float));
}
///////////////////////////////////////////////////////////////////////////////////////////////
void CChunkTag::WriteRect(DWORD dwID, CRect data)
{
    WriteHeader(dwID, sizeof(DWORD) * 4);
    GetFile()->Write(&data.top, sizeof(DWORD));
    GetFile()->Write(&data.bottom, sizeof(DWORD));
    GetFile()->Write(&data.left, sizeof(DWORD));
    GetFile()->Write(&data.right, sizeof(DWORD));
}
CRect CChunkTag::ReadAsRect()
{
    CRect data;
    data.top = ReadAsDWord();
    data.bottom = ReadAsDWord();
    data.left = ReadAsDWord();
    data.right = ReadAsDWord();
    return data;
}
///////////////////////////////////////////////////////////////////////////////////////////////
void CChunkTag::WriteBinary(DWORD dwID, void *data, int lenght)
{
    WriteHeader(dwID, lenght);
    GetFile()->Write(data, lenght);
}
void CChunkTag::ReadAsBinary(void *data, int lenght)
{
    GetFile()->Read(data, lenght);
    AddLenght(lenght);
}
///////////////////////////////////////////////////////////////////////////////////////////////
CQuaternion CChunkTag::ReadAsQuaternion()
{
    CQuaternion qt;
    qt.Load(*GetFile());
    AddLenght(4 * sizeof(float));
    return qt;
}
void CChunkTag::WriteQuaternion(DWORD dwID, CQuaternion data)
{
    WriteHeader(dwID, sizeof(float) * 4);
    data.Save(*GetFile());
}
/*void CChunkTag::WriteComplection( DWORD dwID, SComplection data)
{
        WriteHeader( dwID,  sizeof(float) * 3);
        GetFile()->Write( &data.grace, sizeof(float) );
        GetFile()->Write( &data.strength, sizeof(float) );
        GetFile()->Write( &data.tall, sizeof(float) );
}*/
//------------------------------------------------------------------------------
BOOL CChunkFile::Open(CString szName, WORD flags)
{
    std::string resolved=(flags&3)==CFile::modeRead?ModRuntime::MapPath(szName):std::string((LPCSTR)szName);
    return m_fFile.Open(resolved.c_str(), flags);
}
CChunkFile::CChunkFile(CString szName)
{
    Open(szName, CFile::modeRead);
}