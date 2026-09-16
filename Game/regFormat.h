#pragma once
/************************************************************************
        Registry file format declaration file
        Written by Ivanov E.N.
        Nival Entertainment
************************************************************************/
const int CFILEREGISTRY_QUOTE = ',';

const DWORD SREGHEADER_SIGNATURE = 0x45ab3efb;
#pragma pack(1)
// Header of registry file
struct SRegHeader
{
    DWORD dwSignature;    // signature of registry file
    WORD wSectionsNumber; // number of sections in file
                          //... hash table for sections lookup
                          //... sections
};

// Header of section
struct SSectionHeader
{
    WORD wValuesNumber; // number of values in file
    WORD wNameLen;      // length of section name
                        //... name
                        //... hash table for values lookup
                        //... values
};

enum ERegValueType
{
    ERT_DWORD = 0,
    ERT_FLOAT = 1,
    ERT_STRING = 2,
    ERT_NOTDEF = 3
};
const BYTE ERT_ARRAY = 0x80;
// Header of value
struct SValueHeader
{
    BYTE cType;    // type of value
    WORD wNameLen; // length of value name
                   //... name
                   //[ WORD wArraySize ]
                   //... value data
};

// Hash table entry
struct SRegHashEntry
{
    WORD wNextIndex; // index of the next item in hash table ( -1 if last entry )
    DWORD dwOffset;  // offset from the beginning of file or section ( == 0 for empty entry )
};
#pragma pack()

WORD GetHashValue(const char *, WORD wHashBase);
