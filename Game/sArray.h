#pragma once
/**********************************************************************
 *<
        FILE: sArray.H

        DESCRIPTION:  2D and 1D array with copying capability

        CREATED BY: Andy Gulin

 *>	Copyright (c) 1999, Nival Enterntainemnt
 **********************************************************************/

/////////////////////////////////////////////////////////////////////////////////////
#include <AfxTempl.h>
/////////////////////////////////////////////////////////////////////////////////////
template <class T>
struct CBoundCheck
{
    T *data;
    int nSize;
    CBoundCheck(T *d, int nS)
    {
        data = d;
        nSize = nS;
    }
    T &operator[](int i) const
    {
        ASSERT(i >= 0 && i < nSize);
        return data[i];
    }
};

template <class T>
class CArray2D
{
    typedef T *PT;
    T *data;
    T **pData;
    int nXSize, nYSize;
    void Copy(const CArray2D &a)
    {
        nXSize = a.nXSize;
        nYSize = a.nYSize;
        Create();
        for (int i = 0; i < nXSize * nYSize; i++)
            data[i] = a.data[i];
    }
    void Destroy()
    {
        delete[] data;
        delete[] pData;
    }
    void Create()
    {
        data = new T[nXSize * nYSize];
        pData = new PT[nYSize];
        for (int i = 0; i < nYSize; i++)
            pData[i] = data + i * nXSize;
    }

public:
    CArray2D(int xsize = 1, int ysize = 1)
    {
        nXSize = xsize;
        nYSize = ysize;
        Create();
    }
    CArray2D(const CArray2D &a) { Copy(a); }
    CArray2D &operator=(const CArray2D &a)
    {
        Destroy();
        Copy(a);
        return *this;
    }
    ~CArray2D() { Destroy(); }
    void SetSizes(int xsize, int ysize)
    {
        if (nXSize == xsize && nYSize == ysize)
            return;
        Destroy();
        nXSize = xsize;
        nYSize = ysize;
        Create();
    }
    void Clear() { SetSizes(1, 1); }
#ifdef _DEBUG
    CBoundCheck<T> operator[](int i) const
    {
        ASSERT(i >= 0 && i < nYSize);
        return CBoundCheck<T>(pData[i], nXSize);
    }
#else
    T *operator[](int i) const
    {
        ASSERT(i >= 0 && i < nYSize);
        return pData[i];
    }
#endif
    int GetXSize() const { return nXSize; }
    int GetYSize() const { return nYSize; }
    void FillZero() { memset(data, 0, sizeof(T) * nXSize * nYSize); }
    void FillEvery(const T &a)
    {
        for (int i = 0; i < nXSize * nYSize; i++)
            data[i] = a;
    }
    void LoadFromMemory(const T *pDataToLoad)
    {
        memmove(data, pDataToLoad, sizeof(T) * nXSize * nYSize);
    }
    T *GetBuffer() const { return data; }
    T *GetLine(int j) const
    {
        ASSERT(j >= 0 && j < nYSize);
        return pData[j];
    }
};

/////////////////////////////////////////////////////////////////////////////////////
template <class T>
class CArray2DIterator
{
private:
    CArray2D<T> *pArray;
    T *pCurItem;
    int nXSize, nXCnt;
    int nYSize;
    int nPrevX, nPrevY, nCurX, nCurY;

public:
    CArray2DIterator(CArray2D<T> *_pArray)
    {
        pArray = _pArray;
        pCurItem = NULL;
        nXCnt = nXSize = nYSize = nPrevX = nPrevY = nCurX = nCurY = -1;
    }
    ~CArray2DIterator() { pArray = NULL; }
    void BeginIterate(int nX, int nY, int nSX, int nSY);
    BOOL IsReachEnd() { return nYSize <= 0; }
    T &GetNext();
    T *GetNextPtr();
    int GetX() { return nPrevX; }
    int GetY() { return nPrevY; }
};

template <class T>
inline void CArray2DIterator<T>::BeginIterate(int nX, int nY, int nSX, int nSY)
{
    // clipping
    if (nX < 0)
    {
        nSX += nX;
        nX = 0;
    }
    if (nY < 0)
    {
        nSY += nY;
        nY = 0;
    }
    if (nX + nSX > pArray->GetXSize())
        nSX = pArray->GetXSize() - nX;
    if (nY + nSY > pArray->GetYSize())
        nSY = pArray->GetYSize() - nY;
    if (nSX <= 0 || nSY <= 0)
    {
        nSY = 0;
        return;
    }

    pCurItem = pArray->GetLine(nY) + nX;
    nXSize = nSX;
    nYSize = nSY;
    nXCnt = nXSize;

    nPrevX = -1;
    nCurX = nX;
    nPrevY = -1;
    nCurY = nY;
}

template <class T>
inline T *CArray2DIterator<T>::GetNextPtr()
{
    ASSERT(!IsReachEnd());
    nPrevX = nCurX;
    nPrevY = nCurY;
    T *rv = pCurItem++;

    nCurX++;
    if (--nXCnt == 0)
    {
        nXCnt = nXSize;
        nYSize--;

        pCurItem += pArray->GetXSize() - nXSize;

        nCurY++;
        nCurX -= nXSize;
    }
    return rv;
}

template <class T>
inline T &CArray2DIterator<T>::GetNext()
{
    return *GetNextPtr();
}

// #define CAArray CArray

/////////////////////////////////////////////////////////////////////////////////////
template <class TYPE, class ARG_TYPE = const TYPE &>
class CAArray : public CArray<TYPE, ARG_TYPE>
{
public:
    CAArray() {}
    void Swap(CAArray &other) noexcept
    {
        std::swap(this->m_pData, other.m_pData);
        std::swap(this->m_nSize, other.m_nSize);
        std::swap(this->m_nMaxSize, other.m_nMaxSize);
        std::swap(this->m_nGrowBy, other.m_nGrowBy);
    }
    CAArray(const CAArray &a)
    {
        this->SetSize(a.GetSize());
        for (int i = 0; i < a.GetSize(); i++)
            (*this)[i] = a[i];
    }
    CAArray &operator=(const CAArray &a)
    {
        this->SetSize(a.GetSize());
        for (int i = 0; i < a.GetSize(); i++)
            (*this)[i] = a[i];
        return *this;
    }
#ifndef _DEBUG
    // operator TYPE *() const { return m_pData; }
#endif
    void SafeRemoveAt(int i)
    {
        for (int k = i + 1; k < this->GetSize(); k++)
            (*this)[k - 1] = (*this)[k];
        SetSize(this->GetSize() - 1);
    }
    void Clear()
    {
        for (INT_PTR i = 0; i < this->m_nSize; i++)
        {
            this->m_pData[i].~TYPE();
        }
        this->m_nSize = 0;
    } // without freeing array
};
