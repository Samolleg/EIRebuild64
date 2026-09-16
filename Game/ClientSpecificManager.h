#pragma once

#include "BaseManager.h"
/////////////////////////////////////////////////////////////////////////////////////
class CClientSpecificManager : public CBaseManager
{
protected:
    // data create/release
    virtual LPGAMEBASE CreateInstant(ETypeIDs tidTypeID);

public:
    CClientSpecificManager();
    virtual ~CClientSpecificManager();
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CClientSpecificManager"; };
    virtual EClassIDs WhoAmI() const { return HPC_CLIENTSPECIFICMANAGER; };
};
/////////////////////////////////////////////////////////////////////////////////////
