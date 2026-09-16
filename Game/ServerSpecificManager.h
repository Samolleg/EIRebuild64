#pragma once
#include "BaseManager.h"
/////////////////////////////////////////////////////////////////////////////////////
class CServerSpecificManager : public CBaseManager
{
protected:
    // data create/release
    virtual LPGAMEBASE CreateInstant(ETypeIDs tidTypeID);

public:
    CServerSpecificManager();
    virtual ~CServerSpecificManager();
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CServerSpecificManager"; };
    virtual EClassIDs WhoAmI() const { return HPC_SERVERSPECIFICMANAGER; };
};
/////////////////////////////////////////////////////////////////////////////////////
