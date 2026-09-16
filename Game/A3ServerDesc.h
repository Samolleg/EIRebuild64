#pragma once

#include "CSlink.h"

// --------------------------------------------------------------------------------------------
struct SA3GameServer
{
    std::string szName, szQuestName;
    unsigned char nPlayers, nMaxPlayers, nAllod;
    bool bSecret;             // is server password protected
    unsigned int nClientID;   // "unique" client ID (to separate clients)
    unsigned int nPwd;        // master server provided constant to sort out attacks
    unsigned int nProtocolID; // protocolID
    // some other info
    void UpdateTo(CMemoryStream &buf);
    bool UpdateFrom(CMemoryStream &buf);
};
