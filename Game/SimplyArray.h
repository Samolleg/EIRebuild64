#pragma once
//  Copyright (C) 1998 Nival Entertainment
//  Description: Simply Array (w/o constructor/destructor calling for each element)
//  Author: Yuri V. Blazhevich

/////////////////////////////////////////////////////////////////////////////////////
#include <memory.h>
#include "SArray.h"
/////////////////////////////////////////////////////////////////////////////////////
#define CSimplyArray CAArray
/*template <class TYPE, class ARG_TYPE>
class CSimplyArray
{
private :
        TYPE* m_pData;                        // the actual array of data
        int m_nSize;                          // # of elements
        int m_nMaxSize;                       // max allocated
        int m_nGrowBy;                        // grow amount
public :
        CSimplyArray();
        ~CSimplyArray();
        // get/set size
        int GetSize() const;
        int GetUpperBound() const;
        void SetSize(int nNewSize, int nGrowBy = -1);
        void SetSizeNS(int nNewSize, int nGrowBy = -1);
        // Clean up
        void FreeExtra();
        void RemoveAll();
        // Accessing elements
        const TYPE& GetAt( int nIndex ) const;
        void SetAt( int nIndex, ARG_TYPE newElement );
        TYPE& ElementAt( int nIndex );
        // Direct Access to the element data (may return NULL)
        const TYPE* GetData() const;
        TYPE* GetData();
        // Potentially growing the array
        void SetAtGrow( int nIndex, ARG_TYPE newElement );
        int Add( ARG_TYPE newElement );
        int Append( const CSimplyArray& src );
        void Copy( const CSimplyArray& src );
        // overloaded operator helpers
        const TYPE& operator[]( int nIndex ) const;
        TYPE& operator[]( int nIndex );
        // Operations that move elements around
        void InsertAt( int nIndex, ARG_TYPE newElement, int nCount = 1 );
        void RemoveAt( int nIndex, int nCount = 1 );
        void InsertAt( int nStartIndex, CSimplyArray* pNewArray );
};
/////////////////////////////////////////////////////////////////////////////////////
#include "SimplyArray.inl"*/
/////////////////////////////////////////////////////////////////////////////////////
// simply type arrays typedef
typedef CSimplyArray<char, char> CCharArray;
typedef CSimplyArray<BYTE, BYTE> CBYTEArray;
typedef CSimplyArray<short, short> CShortArray;
typedef CSimplyArray<WORD, WORD> CWORDArray;
typedef CSimplyArray<unsigned short, unsigned short> CUShortArray;
typedef CSimplyArray<long, long> CLongArray;
typedef CSimplyArray<DWORD, DWORD> CDWORDArray;
typedef CSimplyArray<int, int> CIntArray;
typedef CSimplyArray<float, float> CFloatArray;
typedef CSimplyArray<double, double> CDoubleArray;
/////////////////////////////////////////////////////////////////////////////////////
