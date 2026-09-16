#pragma once

#include <stddef.h>
#include <assert.h>
#include <malloc.h>

// --------------------------------------------------------------------------------------------
// allocate memory, aligned on 32 bytes, for nNumElements of the type 'TYPE'
// --------------------------------------------------------------------------------------------
template <class TYPE>
inline TYPE *mm_malloc(int nNumElements)
{
    int size = nNumElements * sizeof(TYPE);
    char *oldp = dbgnew char[size + 32 + 4]; // malloc( size + 32 + 4 );
    char *newp = NULL;
    unsigned *savedp = NULL;

    newp = oldp + 31 + 4;
    newp = reinterpret_cast<char *>(DWORD(newp) & (~0x1F));

    savedp = reinterpret_cast<unsigned *>(newp - 4);
    *savedp = reinterpret_cast<unsigned>(oldp);

#ifdef _DEBUG
    assert((DWORD(newp) & 0x1F) == 0);               // alignment is correct
    assert((newp - 4) >= oldp);                      // stay above lower bound
    assert((newp + size) <= (oldp + size + 32 + 4)); // stay below upper bound
#endif                                               // _DEBUG

    return (TYPE *)newp;
}

// --------------------------------------------------------------------------------------------
// frees memory allocated with mm_malloc
// --------------------------------------------------------------------------------------------
inline void mm_free(void *ptr)
{
#ifdef _DEBUG
    unsigned *savedp = reinterpret_cast<unsigned *>(reinterpret_cast<char *>(ptr) - 4);
    char *oldp = reinterpret_cast<char *>(*savedp);
    delete[] oldp;
//	free( oldp );
#else  // in non-debug case
    delete[] ((char *)(*((unsigned *)((char *)ptr - 4))));
//	free( (char*)(*((unsigned*)((char*)ptr - 4))) );
#endif // _DEBUG
}
