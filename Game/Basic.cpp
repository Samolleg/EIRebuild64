#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "BasicM.h"
#include "process.h"
/////////////////////////////////////////////////////////////////////////////////////
// running member function as a thread with refcounting
struct SFunctionThreadInfo
{
    CMPtr<CMFundament> pThis;
    void (CMFundament::*func)();
    //
    void RunIt() { (pThis->*func)(); }
};
/////////////////////////////////////////////////////////////////////////////////////
void __cdecl FunctionThreadStartup(void *pData)
{
    SFunctionThreadInfo *info = reinterpret_cast<SFunctionThreadInfo *>(pData);
    info->RunIt();
    delete info;
}
/////////////////////////////////////////////////////////////////////////////////////
void CMFundament::RunThread(MemberFuncPtr func, int nStackSize)
{
    SFunctionThreadInfo *info = dbgnew SFunctionThreadInfo;
    info->pThis = this; // ref count of 'this' object will be increased automatically here
    info->func = func;
    _beginthread(FunctionThreadStartup, nStackSize, info);
}
/////////////////////////////////////////////////////////////////////////////////////
