#pragma once
////// ObjectDBReader.cpp header file  ///////////
//												//
//	  The Reader of object data base file       //
//												//
//												//
//	started             16/12/98				//
//												//
//	last time revised   12/05/99				//
//////////////////////////////////////////////////

#include <AfxTempl.h>
#include "MapEditObject.h"
#include "WorldServer.h"
#include "ChunkFile.h"

class CGameMOBFile : public CChunkFile
{
public:
    bool Load(CWorldServer *pWorldServer, bool bSkipScript = false);
    bool Read(CBasicObjectMap &objects, SDiplomation &dipl, CString &szScript);

protected:
};
