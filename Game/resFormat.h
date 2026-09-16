#pragma once
/************************************************************************
        Resource file format declaration
        Written by Ivanov E.N.
        Nival Entertainment, 1998
************************************************************************/
const DWORD SRESHEADER_SIGNATURE1 = 0x19CE23C;
const DWORD SRESHEADER_SIGNATURE2 = 0x19CE23D;

#pragma pack(1)
struct SResHeader
{
    DWORD dwSignature;       // must be SRESHEADER_SIGNATURE for valid file
    DWORD dwEntryCount;      // number of entries in resource
    DWORD dwHashTableOffset; // offset to hash table
    DWORD dwNameDataSize;    // size of name data pool
                             // Entry1
    //...
    // EntryN
    // Hash table for dwEntryCount entries
    // Name data pool
};

// Hash table entry
struct SResHashEntry1
{
    DWORD dwNextIndex; // index of the next item in hash table ( -1 if last entry )
    DWORD dwSize;      // size of the entry
    DWORD dwOffset;    // offset from the beginning of file to entry ( = 0 for unused entry )
    __time32_t tLastModification; // last modification time for this entry
    WORD wNameLen;                // length of name
    DWORD dwNameOffset;           // offset from beginning of name data pool to name of entry
};

struct SResHashEntry2
{
    DWORD dwSize;       // size of the entry
    DWORD dwNameOffset; // offset from beginning of name data pool to name of entry
    DWORD dwNextIndex;  // index of the next item in hash table ( -1 if last entry )
    WORD wNameLen;      // length of name
    DWORD dwOffset;     // offset from the beginning of file to entry ( = 0 for unused entry )
};

#ifndef __USE_ALTERNATE_RES_HEADERS__
typedef SResHashEntry1 SResHashEntry;
const DWORD SRESHEADER_SIGNATURE = SRESHEADER_SIGNATURE1;
#else
typedef SResHashEntry2 SResHashEntry;
const DWORD SRESHEADER_SIGNATURE = SRESHEADER_SIGNATURE2;
#endif //__USE_ALTERNATE_RES_HEADERS__

#pragma pack()
