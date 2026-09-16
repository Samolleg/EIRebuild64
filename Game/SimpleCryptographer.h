#pragma once
// SimpleCryptographer.h: interface for the CSimpleCryptographer class.
//
//////////////////////////////////////////////////////////////////////

#include <vector>

class CSimpleCryptographer
{
    std::vector<BYTE> m_aData;
    std::vector<BYTE> m_aCodedData;

    void Encode();
    void Decode();
    BYTE NextRandom(DWORD &dwHoldrand)
    {
        return BYTE(((dwHoldrand = dwHoldrand * 214013L + 2531011L) >> 16) & 0x7fff);
    }

public:
    void SetCodetSize(const int nSize) { m_aCodedData.resize(nSize); }
    void SetCodetData(const void *pData, const int nSize)
    {
        m_aCodedData.resize(nSize);
        memmove(&m_aCodedData[0], pData, nSize);
    }

    int GetCodetSize() { return m_aCodedData.size(); }
    void *GetCodetData() { return &m_aCodedData[0]; }

    void SetString(CString &szText);
    void GetString(CString &szText);
};
