#pragma once
/*==========================================================================================================

        Network protocol identifiers

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/

#include "NetUpdate.h"

/////////////////////////////////////////////////////////////////////////////////////
// ------------------------------ Typedefs
// ----------------------------------------------------------------
typedef BYTE PACKET_ID; // Packet ID

// typedef int PLAYER_ID;						// Player ID
class CPlayer;
// typedef CHashMap< PLAYER_ID, CPlayer* > CPlayerPtrHash;
// typedef CHashMapIterator< PLAYER_ID, CPlayer* > CPlayerPtrHashIterator;

typedef CHashMap<int, class CGameBase *> GameBaseHashMap;

typedef int ITEM_ID; // Item ID
class CItem;
typedef CHashMap<ITEM_ID, CItem *> CItemPtrHash;
typedef CHashMapIterator<ITEM_ID, CItem *> CItemPtrHashIterator;

typedef int MAPOBJ_ID; // Map object ID
class CUnit;
typedef CHashMap<MAPOBJ_ID, CUnit *> CUnitPtrHash;
typedef CHashMapIterator<MAPOBJ_ID, CUnit *> CUnitPtrHashIterator;
class CMapObject;
typedef CHashMap<MAPOBJ_ID, CMapObject *> CMapObjPtrHash;
typedef CHashMapIterator<MAPOBJ_ID, CMapObject *> CMapObjPtrHashIterator;
typedef int EFFECT_ID; // Effect ID
class CEffect;
typedef CHashMap<EFFECT_ID, CEffect *> CEffectPtrHash;
typedef CHashMapIterator<EFFECT_ID, CEffect *> CEffectPtrHashIterator;
typedef int SPELL_ID; // Spell ID

/////////////////////////////////////////////////////////////////////////////////////
// ------------------------------ Constants
// ----------------------------------------------------------------
const WORD DW_REQUIRED_CLIENT_VERSION = 100;

// ----------------------  Client-to-server packet ids
// -----------------------------------------------------

// login
const PACKET_ID PID_LOGIN = 0x00;     // SNP_LOGIN
const PACKET_ID PID_LOGOUT = 0x01;    // nothing
const PACKET_ID PID_RECONNECT = 0x02; // nothing
const PACKET_ID PID_EXITZONE = 0x03;  // int nEntrance
const PACKET_ID PID_GOCAMP = 0x04;    // int nInterface ( EInterface )
const PACKET_ID PID_FINISHCAMP = 0x05;
const PACKET_ID PID_GOZONE = 0x06;        // string zone name, int nEntrance, single only
const PACKET_ID PID_BRIEFING_DONE = 0x07; // string briefing name
const PACKET_ID PID_ZONE_LOADED = 0x09;   // inform server that zone has been loaded
// general commands
const PACKET_ID PID_COMBAT_PAUSE = 0x10; // byte - pause state
// shop
const PACKET_ID PID_ENTER_SHOP = 0x20;   // MAPOBJ_ID
const PACKET_ID PID_LEAVE_SHOP = 0x21;   // MAPOBJ_ID
const PACKET_ID PID_CHANGE_SHELF = 0x22; // SNP_CHANGE_SHELF
const PACKET_ID PID_BUY = 0x23;          // MAPOBJ_ID
const PACKET_ID PID_SELL = 0x24;         // MAPOBJ_ID
// commands
const PACKET_ID PID_CMD_MOVETO = 0x30;     // SNP_CMD_MOVETO
const PACKET_ID PID_CMD_ATTACK = 0x31;     // SNP_CMD_ATTACK
const PACKET_ID PID_CMD_CASTPOINT = 0x32;  // SNP_CMD_CASTPOINT
const PACKET_ID PID_CMD_CASTTARGET = 0x33; // SNP_CMD_CASTTARGET
const PACKET_ID PID_CMD_SELWEAPON = 0x34;  // SNP_CMD_SELWEAPON
const PACKET_ID PID_CMD_POSE = 0x35;       // SNP_CMD_PHASE
const PACKET_ID PID_CMD_SKILL = 0x36;      // SNP_CMD_SKILL
const PACKET_ID PID_CMD_LOOT = 0x37;       // SNP_CMD_ATTACK
const PACKET_ID PID_CMD_TRAP = 0x38;       // byte - number of qitem
const PACKET_ID PID_CMD_FOLLOW = 0x39;     // SNP_CMD_FOLLOW
const PACKET_ID PID_CMD_SWARM = 0x3a;      // list of unit ids
const PACKET_ID PID_CMD_AGGRESSION = 0x3b; // nID, byte
// camera
const PACKET_ID PID_CAMERAPOSITION = 0x40; // SNP_CAMERAPOS
// party
const PACKET_ID PID_CAMPITEMS = 0x50; // packet with party without vars

// ------------------------ Server-to-client packet ids
// ----------------------------------------------------

// login
const PACKET_ID PID_LOGGED = 0x00; // SCP_LOGGED
// logout
const PACKET_ID PID_NEWZONE = 0x02;          // packet ( look WorldServer/WorldClient )
const PACKET_ID PID_NEWSPELLITEMS = 0x03;    // packet ( look WorldServer/WorldClient )
const PACKET_ID PID_MOVECAMERA = 0x04;       // cplot3d
const PACKET_ID PID_ADDSPELLITEMS = 0x05;    // packet ( look WorldServer/WorldClient )
const PACKET_ID PID_ADDITEMSTOHOOVER = 0x06; // packet ( look PlayerServer/PlayerClient )
const PACKET_ID PID_NEWINTERFACE = 0x07;     // int - EInterface
const PACKET_ID PID_SERVERCHANGE = 0x08;     // BASCII zone/server name
const PACKET_ID PID_GAMEOVER = 0x09;
const PACKET_ID PID_REMOVEITEM = 0x0a;    // int (id of unit)
const PACKET_ID PID_ACTIONFAILURE = 0x0b; // SCP_ACTION_FAILURE
const PACKET_ID PID_ITEMSMANA = 0x0c;     // SCP_ACTION_FAILURE
const PACKET_ID PID_PLAYMUSIC = 0x0d;     // string
const PACKET_ID PID_SHOWPATH = 0x0e;      // CAIFullRoute
const PACKET_ID PID_MOVECAMERA2 = 0x0f;   // 2xCPlot3d
// info
const PACKET_ID PID_INFO_MSG = 0x10;     // BASCII msg
const PACKET_ID PID_ERR_MSG = 0x11;      // BASCII msg
const PACKET_ID PID_EVENT = 0x12;        // SCP_EVENT
const PACKET_ID PID_STRINGEVENT = 0x13;  // BASCII msg
const PACKET_ID PID_ADDAITEM = 0x14;     // itemID, itemMana
const PACKET_ID PID_GAINEXP = 0x15;      // float fpExp
const PACKET_ID PID_REMOVEWEAPON = 0x16; //
const PACKET_ID PID_REMOVEHOOVER = 0x17;
const PACKET_ID PID_PERSGETEXP = 0x18;
const PACKET_ID PID_SELECTUNIT = 0x19;
// shop
const PACKET_ID PID_SHOP_ENTERED = 0x20; // SCP_SHOP_ENTERED
const PACKET_ID PID_SHOP_LEAVED = 0x21;  // MAPOBJ_ID
const PACKET_ID PID_UPDATE_ITEM = 0x22;  // SCP_UPDATE_ITEM
const PACKET_ID PID_REMOVE_ITEM = 0x23;  // SCP_REMOVE_ITEM
// inventory
const PACKET_ID PID_UPDATE_INVENTORY = 0x30; // SCP_UPDATE_ITEM
const PACKET_ID PID_REMOVE_INVENTORY = 0x31; // SCP_REMOVE_ITEM
// unit
const PACKET_ID PID_CREATE_MAPOBJ = 0x40; // SCP_CREATE_MAPOBJ
const PACKET_ID PID_UPDATE_MAPOBJ = 0x41; // SCP_UPDATE_MAPOBJ
const PACKET_ID PID_REMOVE_MAPOBJ = 0x42; // SCP_REMOVE_MAPOBJ
const PACKET_ID PID_UNIT_DEATH = 0x43;    // nID
// effects
const PACKET_ID PID_CREATE_EFFECT = 0x50; // SCP_CREATE_EFFECT
const PACKET_ID PID_UPDATE_EFFECT = 0x51; // SCP_UPDATE_EFFECT
const PACKET_ID PID_REMOVE_EFFECT = 0x52; // SCP_REMOVE_EFFECT
// visible units list
const PACKET_ID PID_VISIBLE_UNITS = 0x60; // N_UNITS AS DWORD, [UNIT_ID] AS DWORD
// general information
const PACKET_ID PID_WEATHER = 0x70;   // SCP_WEATHER
const PACKET_ID PID_WORLDTIME = 0x71; // SCP_WORLDTIME
const PACKET_ID PID_WIND = 0x72;      // cplot3d+float
const PACKET_ID PID_WATERLEVELS = 0x73;
// party and global script vars
const PACKET_ID PID_PARTY =
    0x80; // packet with party with vars - potential problem with extra large var set
const PACKET_ID PID_SETVAR = 0x81; // incremental operations with global vars
const PACKET_ID PID_DELVAR = 0x82;
const PACKET_ID PID_PARTYITEMS = 0x83; // packet with party without vars
