#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "WorldServer.h"
#include "Object3DServer.h"

//---------------------------- Create / destroy
//------------------------------------------------------------
CObject3DServer::CObject3DServer()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}

CObject3DServer::~CObject3DServer()
{
    Destroy();
}

BOOL CObject3DServer::Create()
{
    return TRUE;
}

// destroy all internal data structures
void CObject3DServer::Destroy()
{
    RegisterObject3D(NULL);
}
