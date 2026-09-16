#include "StdAfx.h"

#include "A3ServerDesc.h"
#include "Streams.h"

/////////////////////////////////////////////////////////////////////////////////////
void SA3GameServer::UpdateTo(CMemoryStream &buf)
{
    buf << nClientID << nPwd;
    buf.WriteString(szName);
    buf.WriteString(szQuestName);
    buf << nPlayers << nMaxPlayers << bSecret << nAllod << nProtocolID;
}
/////////////////////////////////////////////////////////////////////////////////////
static void MakeStringReadable(std::string &s)
{
    for (int i = 0; i < s.size(); i++)
    {
        if (((unsigned char)s[i]) < 32)
            s[i] = '|';
    }
}
/////////////////////////////////////////////////////////////////////////////////////
bool SA3GameServer::UpdateFrom(CMemoryStream &buf)
{
    buf >> nClientID >> nPwd;
    buf.ReadString(szName, 256);
    buf.ReadString(szQuestName, 256);
    buf >> nPlayers >> nMaxPlayers >> bSecret >> nAllod >> nProtocolID;
    MakeStringReadable(szName);
    MakeStringReadable(szQuestName);
    return true; // TODO: check buf.IsOk();
}
/////////////////////////////////////////////////////////////////////////////////////
