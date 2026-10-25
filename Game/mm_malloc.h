#pragma once

#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <malloc.h>

// --------------------------------------------------------------------------------------------
// allocate memory, aligned on 32 bytes, for nNumElements of the type 'TYPE'
// --------------------------------------------------------------------------------------------
template <class TYPE>
inline TYPE *mm_malloc(int nNumElements)
{
    const size_t size = static_cast<size_t>(nNumElements) * sizeof(TYPE);
    const size_t overhead = 31 + sizeof(char *);
    char *oldp = dbgnew char[size + overhead];
    const uintptr_t address = reinterpret_cast<uintptr_t>(oldp + overhead);
    char *newp = reinterpret_cast<char *>(address & ~uintptr_t(0x1F));
    char **savedp = reinterpret_cast<char **>(newp - sizeof(char *));
    *savedp = oldp;

#ifdef _DEBUG
    assert((reinterpret_cast<uintptr_t>(newp) & 0x1F) == 0);
    assert((newp - sizeof(char *)) >= oldp);
    assert((newp + size) <= (oldp + size + overhead));
#endif // _DEBUG

    return (TYPE *)newp;
}

// --------------------------------------------------------------------------------------------
// frees memory allocated with mm_malloc
// --------------------------------------------------------------------------------------------
inline void mm_free(void *ptr)
{
    char **savedp = reinterpret_cast<char **>(reinterpret_cast<char *>(ptr) - sizeof(char *));
    delete[] *savedp;
}
