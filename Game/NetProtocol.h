#pragma once

/*==========================================================================================================

        Network protocol classes source

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/

#include "PacketIDs.h"
#include "ClassIDs.h"
#include "TypeIDs.h"
#include "Object.h"
#include "ServerConsts.h"

#pragma pack(1)

// --------------------------------------------------------------------------------------------
// Login
// --------------------------------------------------------------------------------------------
/** Client **/
const int N_SNP_LOGIN_NAME_LEN = 64;
struct SNP_LOGIN
{
    DWORD dwClientVersion;                  // client net protocol version
    char pszUser[N_SNP_LOGIN_NAME_LEN];     // user
    char pszPassword[N_SNP_LOGIN_NAME_LEN]; // user's password
    SPartyID partyID;
};
/** Server **/
struct SCP_LOGGED
{
    int nPlayerID; // server player ID
};

// --------------------------------------------------------------------------------------------
// Map objects
// --------------------------------------------------------------------------------------------
/** Server **/
struct SCP_CREATE_MAPOBJ
{
    MAPOBJ_ID mapobjID; // ID of map object
    ETypeIDs typeID;    // typeID of map object
    DWORD dwSubtype;    // subtypeID of map object
    DWORD dwFlags;      // flags
};
/////////////////////////////////////////////////////////////////////////////////////
struct SCP_UPDATE_MAPOBJ
{
    MAPOBJ_ID mapobjID; // ID of map object
};
/////////////////////////////////////////////////////////////////////////////////////
struct SCP_REMOVE_MAPOBJ
{
    MAPOBJ_ID mapobjID; // ID of amp object
};

// --------------------------------------------------------------------------------------------
// Items
// --------------------------------------------------------------------------------------------
const BYTE SCPUI_BASIC = 0;
const BYTE SCPUI_TABLE = 1;
const BYTE SCPUI_INVENTORY = 2;
// --------------------------------------------------------------------------------------------
/** Server **/
// --------------------------------------------------------------------------------------------
struct SCP_UPDATE_ITEM
{
    ITEM_ID itemID;   // ID of item
    BYTE cUpdateMask; // updated fields mask
    BYTE cDeskType;   // SCPUI_BASIC or SCPUI_TABLE
    BYTE updateInfo[1024];
};
/////////////////////////////////////////////////////////////////////////////////////
struct SCP_REMOVE_ITEM
{
    ITEM_ID itemID; // ID of item
    BYTE cDeskType; // SCPUI_BASIC or SCPUI_TABLE
};

// --------------------------------------------------------------------------------------------
// Shop
// --------------------------------------------------------------------------------------------
/** Client **/
// --------------------------------------------------------------------------------------------
struct SNP_CHANGE_SHELF
{
    MAPOBJ_ID mapobjID; // ID of unit
    ITEM_ID itemID;     // ID of item
    BYTE cDeskFromTo;   // SCPUI_BASIC, SCPUI_TABLE or SCPUI_BACKPACK | ( SCPUI_??? << 4 )
    int nNumber;        // number of items
};
/////////////////////////////////////////////////////////////////////////////////////
struct SCP_SHOP_ENTERED
{
    MAPOBJ_ID mapobjID; // ID of unit
                        // ToDo
    // various shop info
};

// --------------------------------------------------------------------------------------------
// Commands
// --------------------------------------------------------------------------------------------
/** Client **/
// --------------------------------------------------------------------------------------------
struct SNP_CMD_MOVETO
{
    MAPOBJ_ID mapobjID;  // ID of unit
    float fpX, fpY, fpZ; // coordinates to move
    BYTE bFlags;
};
/////////////////////////////////////////////////////////////////////////////////////
struct SNP_CMD_ATTACK
{
    MAPOBJ_ID mapobjID; // ID of unit
    MAPOBJ_ID targetID; // ID of target
    BYTE bHl;           // hit location to attack
    BYTE bDoItFast;
};
/////////////////////////////////////////////////////////////////////////////////////
struct SNP_CMD_FOLLOW
{
    MAPOBJ_ID mapobjID; // ID of unit
    MAPOBJ_ID targetID; // ID of target
};
/////////////////////////////////////////////////////////////////////////////////////
struct SNP_CMD_SKILL
{
    MAPOBJ_ID mapobjID; // ID of unit
    MAPOBJ_ID targetID; // ID of target
    BYTE bSkill;
    int nItemID; // ID of item if one is used with skill
    BYTE bDoItFast;
};
/////////////////////////////////////////////////////////////////////////////////////
struct SNP_CMD_CAST_POINT
{
    int spellNum;        // number of quick spell slot
    MAPOBJ_ID mapobjID;  // ID of unit
    CPlot3D targetCoord; // target coordinates of area spells
    BYTE bDoItFast;
};
/////////////////////////////////////////////////////////////////////////////////////
struct SNP_CMD_CAST_UNIT
{
    int spellNum;       // number of quick spell slot
    MAPOBJ_ID mapobjID; // ID of unit
    MAPOBJ_ID targetID; // ID of target
    BYTE bDoItFast;
};
/////////////////////////////////////////////////////////////////////////////////////
struct SNP_CMD_SELWEAPON
{
    MAPOBJ_ID mapobjID; // ID of unit
    BYTE cSlotToSelect; // quick weapon slot to select
};
/////////////////////////////////////////////////////////////////////////////////////
// universal packet for all phase changing commands for units ( lay/crawl/stand/run/walk )
// actually not used during sending command
struct SNP_CMD_PHASE
{
    MAPOBJ_ID mapobjID; // ID of unit
    BYTE bPose;
};
// --------------------------------------------------------------------------------------------
// Camera
// --------------------------------------------------------------------------------------------
/** Client **/
struct SNP_CAMERAPOS
{
    float fpX, fpY; // camera position
};
// --------------------------------------------------------------------------------------------
// Effects
// --------------------------------------------------------------------------------------------
/** Server **/
// --------------------------------------------------------------------------------------------
struct SCP_CREATE_EFFECT
{
    EFFECT_ID effectID; // ID of effect
    EClassIDs classID;  // class ID of effect
};
/////////////////////////////////////////////////////////////////////////////////////
struct SCP_UPDATE_EFFECT // actually not used
{
    EFFECT_ID effectID; // ID of effect
};
/////////////////////////////////////////////////////////////////////////////////////
struct SCP_REMOVE_EFFECT // actually not used
{
    EFFECT_ID effectID; // ID of effect
};
/////////////////////////////////////////////////////////////////////////////////////
struct SCP_WEATHER
{
    BYTE bWeather;
    USHORT nTime;
};
/////////////////////////////////////////////////////////////////////////////////////
struct SCP_WORLDTIME
{
    float fpTimeMod;
    float fpTimeScale;
};
// --------------------------------------------------------------------------------------------
// Event
// --------------------------------------------------------------------------------------------
struct SCP_EVENT
{
    char bPlayerNum;
    DWORD dwEventID;
};
/////////////////////////////////////////////////////////////////////////////////////
struct SCP_ACTION_FAILURE
{
    DWORD dwUnitID;
    DWORD dwAction;
};
/////////////////////////////////////////////////////////////////////////////////////
#pragma pack()
/////////////////////////////////////////////////////////////////////////////////////
// ------------------------------ Packet parsers support functions
// -----------------------------------------
// #include "BufferManager.h"
/////////////////////////////////////////////////////////////////////////////////////
// Returns false if string is non-printable or have invalid length
BOOL CheckSymbString(LPCSTR pszName, int nLen, int nMinLen);
/*
// BYTE
inline BOOL GetBYTE( LPBYTE pDst, CBufferManager* pBufMng )					{
return pBufMng->ReadData( pDst, sizeof( BYTE ) ); } inline void SetBYTE( LPVOID& pDstBuf, BYTE src )
{ *((BYTE*)pDstBuf) = src; pDstBuf = (BYTE*)pDstBuf + sizeof( BYTE ); }
// WORD
inline BOOL GetWORD( LPWORD pDst, CBufferManager* pBufMng )					{
return pBufMng->ReadData( pDst, sizeof( WORD ) ); } inline void SetWORD( LPVOID& pDstBuf, WORD src )
{ *((WORD*)pDstBuf) = src; pDstBuf = (BYTE*)pDstBuf + sizeof( WORD ); }
// DWORD
inline BOOL GetDWORD( LPDWORD pDst, CBufferManager* pBufMng )				{ return
pBufMng->ReadData( pDst, sizeof( DWORD ) ); } inline void SetDWORD( LPVOID& pDstBuf, DWORD src )
{ *((DWORD*)pDstBuf) = src; pDstBuf = (BYTE*)pDstBuf + sizeof( DWORD ); }
// int
inline BOOL GetInt( int* pDst, CBufferManager* pBufMng )					{
return pBufMng->ReadData( pDst, sizeof( int ) ); } inline void SetInt( LPVOID& pDstBuf, int src )
{ *((int*)pDstBuf) = src; pDstBuf = (BYTE*)pDstBuf + sizeof( int ); }
// float
inline BOOL GetFloat( float* pDst, CBufferManager* pBufMng )				{ return
pBufMng->ReadData( pDst, sizeof( float ) ); } inline void SetFloat( LPVOID& pDstBuf, float src )
{ *((float*)pDstBuf) = src; pDstBuf = (BYTE*)pDstBuf + sizeof( float ); }
// common data
inline BOOL GetCommonData( LPVOID pDst, int nDstBufLen, CBufferManager* pBufMng )	{ return
pBufMng->ReadData( pDst, nDstBufLen ); } inline void SetCommonData( LPVOID& pDstBuf, LPVOID pDst,
int nDstBufLen )			{ memcpy( pDstBuf, pDst, nDstBufLen ); pDstBuf =
(BYTE*)pDstBuf + nDstBufLen; }
// BASCII string
BOOL GetBASCII( LPSTR pszDstBuf, int nDstBufLen, CBufferManager* pBufMng, int nMinLen = -1 );
BOOL GetBASCII( CString& szDstBuf, CBufferManager* pBufMng, int nMinLen = -1 );
void SetBASCII( LPVOID& pDstBuf, LPCSTR pszBuf );
// Get written data size
inline int GetWrittenSize( LPVOID pDst, LPVOID pSrc )	{ return LPBYTE( pDst ) - LPBYTE( pSrc ); }
*/
// Sort CGameBasePtr array by ID using RADIX sort
void SortGameBaseArrayByID(CGameBasePtrArray &toSort);
// Select objects to add/remove/update, keeping previousIDs updated and sorted
void SelectIDs(CGameBasePtrArray &previousIDs, CGameBasePtrArray &newIDs, CGameBasePtrArray &toAdd,
               CGameBasePtrArray &toRemove, CGameBasePtrArray &toUpdate);
// Release pointers to ids
void ReleaseIDs(CGameBasePtrArray &previousIDs);
void ReleaseHashMap(GameBaseHashMap &hash);
