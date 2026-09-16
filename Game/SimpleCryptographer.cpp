// SimpleCryptographer.cpp: implementation of the CSimpleCryptographer class.
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "SimpleCryptographer.h"
#include <algorithm>

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

void CSimpleCryptographer::Encode()
{
    DWORD dwHoldrand;
    m_aCodedData.resize(m_aData.size() + sizeof(DWORD));
    std::copy(m_aData.begin(), m_aData.end(), m_aCodedData.begin() + sizeof(DWORD));
    *((DWORD *)&m_aCodedData[0]) = dwHoldrand = rand();

    for (auto it = m_aCodedData.begin() + sizeof(DWORD); it != m_aCodedData.end(); ++it)
        *it ^= NextRandom(dwHoldrand);
}

void CSimpleCryptographer::Decode()
{
    DWORD dwHoldrand;
    m_aData.resize(m_aCodedData.size() - sizeof(DWORD));
    std::copy(m_aCodedData.begin() + sizeof(DWORD), m_aCodedData.end(), m_aData.begin());
    dwHoldrand = *((DWORD *)&m_aCodedData[0]);

    for (auto it = m_aData.begin(); it != m_aData.end(); ++it)
        *it ^= NextRandom(dwHoldrand);
}

void CSimpleCryptographer::SetString(CString &szText)
{
    m_aData.resize(szText.GetLength());
    memmove(&m_aData[0], szText.GetBuffer(0), m_aData.size());
    szText.ReleaseBuffer();
    Encode();
}

void CSimpleCryptographer::GetString(CString &szText)
{
    Decode();
    memmove(szText.GetBufferSetLength(m_aData.size()), &m_aData[0], m_aData.size());
    szText.ReleaseBuffer();
}
