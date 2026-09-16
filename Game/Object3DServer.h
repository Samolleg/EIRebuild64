#pragma once

#include "Object.h"

class CObject3DServer : public CObject3DSpecific
{
private:
public:
    CObject3DServer();
    virtual ~CObject3DServer();
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    // figures-in-object management
    virtual BOOL AddFigure(CFigure *lpFigure) { return FALSE; }
    virtual BOOL RemoveFigure(CFigure *lpFigure) { return FALSE; }
    virtual CFigure *RemoveFigure(LPCSTR pszFigureName) { return NULL; }
    virtual CFigure *FindFigure(int nFigureTypeID) { return NULL; }
    virtual CFigure *FindFigure(LPCSTR pszFigureName) { return NULL; }
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CObject3DServer"; };
    virtual EClassIDs WhoAmI() const { return HPC_OBJECT3DSERVERSPECIFIC; };
};
